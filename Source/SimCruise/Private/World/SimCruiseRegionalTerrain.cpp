// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseRegionalTerrain.h"

#include "Async/Async.h"
#include "Atlas/SimCruiseAtlasClient.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "World/SimCruiseWorldResources.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseLandscape, Log, All);

namespace
{
	const TCHAR* LandscapeColour = TEXT(R"HLSL(
float2 uv = LP.xy / (2.0 * RadiusCm) + 0.5;
float3 c = HasPhoto > 0.5 ? Texture2DSample(Photo, PhotoSampler, uv).rgb : float3(0.17, 0.16, 0.14);
// The middle 13 km have a sharper photograph (RegionalTerrain.gdshader).
float2 st = (uv - 0.5) * (2.0 * RadiusCm / 1300000.0) + 0.5;
float edge = min(min(st.x, 1.0 - st.x), min(st.y, 1.0 - st.y));
if (HasMiddle > 0.5 && edge > 0.0)
{
	c = lerp(c, Texture2DSample(Middle, MiddleSampler, st).rgb, smoothstep(0.0, 0.065, edge));
}
// The grass field on hillsides (docs/research/world-detail-methods.md): the ground's meso layer
// (FGF, USimCruiseWorldResources::GrassFieldHlsl) at 25 times a lawn's sizes: clumps about 11 m,
// patches of yellower or bluer grass about 125 m, dry patches about 175 m, which turn straw-gold
// with GrassDry. Only where the photograph shows plants: not rock or sand (bright and grey),
// water (bluer than green) or today's snow. No micro texture or blades: past 550 m a pixel
// covers metres of hill. Each part fades out before a pixel is wide enough to alias it.
float fpx = length(fwidth(LP.xy)) * 0.01; // metres per pixel, outside the branch
if (GrassField > 0.5)
{
	const float3 W = float3(0.2126, 0.7152, 0.0722);
	float L = max(dot(c, W), 1e-4);
	float sat = (max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b))) / L;
	float herb = saturate((sat - 0.12) * 5.0) * saturate((0.45 - L) * 5.0) * saturate((c.g - c.b) / L * 4.0 + 0.2);
	if (herb > 0.01)
	{
		FGF gf;
		const float K = 25.0;
		float2 p = LP.xy * 0.01 / K;
		p -= floor(p / 4096.0) * 4096.0; // one seam per 100 km, beyond the landscape's edge
		float clump, dr;
		float3 meso = gf.Meso(p, GrassDry, saturate(2.0 - fpx / (0.1 * K)), saturate(2.0 - fpx / (1.5 * K)), clump, dr);
		float ml = clamp(dot(meso, W), 0.6, 1.6);
		c *= lerp(1.0, ml, 0.5 * herb) * lerp(float3(1, 1, 1), meso / ml, 0.25 * herb);
		// cured grass: the same brightness, straw hue, a little lighter (dry blades reflect more)
		c = lerp(c, dot(c, W) * float3(1.45, 1.05, 0.55) * 1.3, dr * herb * 0.7);
	}
}// Today's snow (MODIS Terra NDSI snow cover, 500 m): the photograph may be from summer.
if (HasSnow > 0.5)
{
	float snow = smoothstep(0.38, 0.76, Texture2DSample(Snow, SnowSampler, uv).r);
	c = lerp(c, float3(0.80, 0.84, 0.88), snow * 0.95);
}
// An orthophoto is taken from above with some shading in it; seen from the ground across many
// kilometres its tones read a little lighter and flatter.
return c * 0.92;
)HLSL");

	// Earth curvature, and a gradual underlap beneath the detailed driving tiles (RegionalTerrain.gdshader).
	const TCHAR* LandscapeOffset = TEXT(R"HLSL(
float2 delta = (WP.xy - Cam.xy) * 0.01;
float d = length(delta);
float drop = dot(delta, delta) / 12742000.0 + lerp(65.0, 0.0, smoothstep(600.0, 3000.0, d));
return float3(0.0, 0.0, -drop * 100.0);
)HLSL");

	// Not drawn within 550 m of the camera: the driving tiles are the ground there.
	const TCHAR* LandscapeMask = TEXT(R"HLSL(
return length(WP.xy - Cam.xy) > 55000.0 ? 1.0 : 0.0;
)HLSL");

#if WITH_EDITOR
	// Material graph building is editor-only; a packaged game loads the saved materials.
	template <typename T>
	T* AddExpression(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Material->GetExpressionCollection().AddExpression(Expression);
		return Expression;
	}

	void AddInput(UMaterialExpressionCustom* Custom, const TCHAR* Name, UMaterialExpression* From, int32 Output = 0)
	{
		FCustomInput Input;
		Input.InputName = Name;
		Input.Input.Connect(Output, From);
		Custom->Inputs.Add(Input);
	}

	UMaterialExpressionCustom* Custom(UMaterial* Material, const TCHAR* Code, ECustomMaterialOutputType Type)
	{
		UMaterialExpressionCustom* Node = AddExpression<UMaterialExpressionCustom>(Material);
		Node->OutputType = Type;
		Node->Inputs.Reset();
		Node->Code = Code;
		return Node;
	}
#endif

	bool ReadGrid(const FString& Path, TArray<FVector>& OutPositions, TArray<int32>& OutIndices)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path) || Bytes.Num() < 8 || Bytes.Num() > 5000000)
		{
			return false;
		}
		uint32 VertexCount = 0, IndexCount = 0;
		FMemory::Memcpy(&VertexCount, Bytes.GetData(), 4);
		FMemory::Memcpy(&IndexCount, Bytes.GetData() + 4, 4);
		if (VertexCount < 3 || VertexCount > 250000 || IndexCount < 3 || IndexCount > 440000 || IndexCount % 3 != 0
			|| static_cast<int64>(Bytes.Num()) != 8 + static_cast<int64>(VertexCount) * 12 + static_cast<int64>(IndexCount) * 4)
		{
			return false;
		}
		const float* P = reinterpret_cast<const float*>(Bytes.GetData() + 8);
		OutPositions.SetNumUninitialized(VertexCount);
		for (uint32 i = 0; i < VertexCount; ++i)
		{
			// Godot's east / up / south metres to Unreal's east / south / up centimetres.
			OutPositions[i] = FVector(P[i * 3 + 0] * 100.0, P[i * 3 + 2] * 100.0, P[i * 3 + 1] * 100.0);
		}
		const int32* I = reinterpret_cast<const int32*>(Bytes.GetData() + 8 + VertexCount * 12);
		OutIndices.SetNumUninitialized(IndexCount);
		for (uint32 i = 0; i < IndexCount; i += 3)
		{
			if (I[i] < 0 || I[i + 1] < 0 || I[i + 2] < 0 || I[i] >= static_cast<int32>(VertexCount)
				|| I[i + 1] >= static_cast<int32>(VertexCount) || I[i + 2] >= static_cast<int32>(VertexCount))
			{
				return false;
			}
			// The axis swap mirrors the mesh, and Unreal's front faces wind the other way: the two cancel.
			OutIndices[i] = I[i];
			OutIndices[i + 1] = I[i + 1];
			OutIndices[i + 2] = I[i + 2];
		}
		return true;
	}
}

struct ASimCruiseRegionalTerrain::FJob
{
	FIntPoint Region = FIntPoint(-1, -1);
	std::atomic<bool> bDone{ false };
	bool bOk = false;
	FString Why;
	int32 Requests = 0;
	TArray<FVector> Positions;
	TArray<int32> Indices;
	TArray<FVector> Normals;
	TArray<FColor> Photo, Middle, Snow;
	int32 PhotoW = 0, PhotoH = 0, MiddleW = 0, MiddleH = 0, SnowW = 0, SnowH = 0;
	FString SnowDate;
	double MaxErrorM = 0.0;
};

FString ASimCruiseRegionalTerrain::MaterialPath()
{
	return TEXT("/Game/World/Materials/M_SimCruiseLandscape.M_SimCruiseLandscape");
}

UMaterial* ASimCruiseRegionalTerrain::BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags)
{
#if WITH_EDITOR
	UTexture* DefaultTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
	UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
	Material->TwoSided = true;
	Material->BlendMode = BLEND_Masked;
	Material->SetUsageByFlag(MATUSAGE_StaticLighting, false);

	UMaterialExpressionLocalPosition* LP = AddExpression<UMaterialExpressionLocalPosition>(Material);
	LP->IncludedOffsets = EPositionIncludedOffsets::ExcludeOffsets;
	UMaterialExpressionWorldPosition* WP = AddExpression<UMaterialExpressionWorldPosition>(Material);
	WP->WorldPositionShaderOffset = WPT_ExcludeAllShaderOffsets;
	UMaterialExpressionCameraPositionWS* Cam = AddExpression<UMaterialExpressionCameraPositionWS>(Material);

	auto TextureParam = [Material, DefaultTexture](const TCHAR* ParamName)
	{
		UMaterialExpressionTextureObjectParameter* P = AddExpression<UMaterialExpressionTextureObjectParameter>(Material);
		P->ParameterName = ParamName;
		P->Texture = DefaultTexture;
		P->SamplerType = SAMPLERTYPE_Color;
		return P;
	};
	auto ScalarParam = [Material](const TCHAR* ParamName, float Default)
	{
		UMaterialExpressionScalarParameter* P = AddExpression<UMaterialExpressionScalarParameter>(Material);
		P->ParameterName = ParamName;
		P->DefaultValue = Default;
		return P;
	};

	UMaterialExpressionCustom* Colour = Custom(Material, *(FString(USimCruiseWorldResources::GrassFieldHlsl()) + LandscapeColour), CMOT_Float3);
	AddInput(Colour, TEXT("LP"), LP);
	AddInput(Colour, TEXT("RadiusCm"), ScalarParam(TEXT("RadiusCm"), static_cast<float>(RadiusM * 100.0)));
	AddInput(Colour, TEXT("HasPhoto"), ScalarParam(TEXT("HasPhoto"), 0.f));
	AddInput(Colour, TEXT("HasMiddle"), ScalarParam(TEXT("HasMiddle"), 0.f));
	AddInput(Colour, TEXT("HasSnow"), ScalarParam(TEXT("HasSnow"), 0.f));
	AddInput(Colour, TEXT("Snow"), TextureParam(TEXT("Snow")));
	AddInput(Colour, TEXT("Photo"), TextureParam(TEXT("Photo")));
	AddInput(Colour, TEXT("Middle"), TextureParam(TEXT("Middle")));
	AddInput(Colour, TEXT("GrassField"), ScalarParam(TEXT("GrassField"), 0.f));
	AddInput(Colour, TEXT("GrassDry"), ScalarParam(TEXT("GrassDry"), 0.f));

	UMaterialExpressionCustom* Offset = Custom(Material, LandscapeOffset, CMOT_Float3);
	AddInput(Offset, TEXT("WP"), WP);
	AddInput(Offset, TEXT("Cam"), Cam);

	UMaterialExpressionCustom* Mask = Custom(Material, LandscapeMask, CMOT_Float1);
	AddInput(Mask, TEXT("WP"), WP);
	AddInput(Mask, TEXT("Cam"), Cam);

	UMaterialExpressionConstant* Rough = AddExpression<UMaterialExpressionConstant>(Material);
	Rough->R = 0.95f;
	UMaterialExpressionConstant* Spec = AddExpression<UMaterialExpressionConstant>(Material);
	Spec->R = 0.3f;

	Material->GetEditorOnlyData()->BaseColor.Connect(0, Colour);
	Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
	Material->GetEditorOnlyData()->Specular.Connect(0, Spec);
	Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0, Offset);
	Material->GetEditorOnlyData()->OpacityMask.Connect(0, Mask);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	return Material;
#else
	return nullptr;
#endif
}

ASimCruiseRegionalTerrain::ASimCruiseRegionalTerrain()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Landscape"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bUseAsyncCooking = true;
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetVisibility(false);
	// 45 km of land: never culled by its own bounds as the camera turns.
	Mesh->BoundsScale = 1.1f;
	RootComponent = Mesh;
}

void ASimCruiseRegionalTerrain::EndPlay(const EEndPlayReason::Type Reason)
{
	Job.Reset();
	Super::EndPlay(Reason);
}

void ASimCruiseRegionalTerrain::StartJob(const FIntPoint& Region)
{
	const FSimCruiseAtlasConfig Config = FSimCruiseAtlasConfig::ResolveDefault();
	if (!Config.IsConfigured())
	{
		Failed = Region;
		RetryAt = FPlatformTime::Seconds() + 300.0;
		return;
	}
	Requested = Region;
	TSharedPtr<FJob, ESPMode::ThreadSafe> NewJob = MakeShared<FJob, ESPMode::ThreadSafe>();
	NewJob->Region = Region;
	Job = NewJob;
	Async(EAsyncExecution::ThreadPool, [NewJob, Config]()
	{
		FJob& J = *NewJob;
		FSimCruiseAtlasRequest Request;
		Request.Command = TEXT("landscape");
		Request.Args = { FString::FromInt(J.Region.X), FString::FromInt(J.Region.Y) };
		Request.TimeoutSeconds = 180.0;
		const FSimCruiseAtlasResult Result = FSimCruiseAtlasClient::RunBlocking(Config, Request, nullptr);
		const TSharedPtr<FJsonObject> Doc = Result.IsOk() && Result.Json->Type == EJson::Object ? Result.Json->AsObject() : nullptr;
		FString Status, Grid, Aerial, MiddleName;
		double Radius = 0.0;
		if (!Doc.IsValid() || !Doc->TryGetStringField(TEXT("status"), Status) || Status != TEXT("available")
			|| !Doc->TryGetStringField(TEXT("grid"), Grid) || !Doc->TryGetNumberField(TEXT("radius"), Radius) || !FMath::IsNearlyEqual(Radius, RadiusM))
		{
			J.Why = Result.IsOk() ? FString::Printf(TEXT("status %s"), *Status) : Result.Describe();
			J.bDone = true;
			return;
		}
		Doc->TryGetNumberField(TEXT("requests"), J.Requests);
		Doc->TryGetNumberField(TEXT("maxError"), J.MaxErrorM);
		if (Grid != FString::Printf(TEXT("relief-tin-v4-%d-%d.bin"), J.Region.X, J.Region.Y)
			|| !ReadGrid(FPaths::Combine(Config.CacheDir, Grid), J.Positions, J.Indices))
		{
			J.Why = TEXT("bad grid file");
			J.bDone = true;
			return;
		}
		// Smooth normals from the faces round each vertex.
		J.Normals.Init(FVector::ZeroVector, J.Positions.Num());
		for (int32 i = 0; i + 2 < J.Indices.Num(); i += 3)
		{
			const FVector& A = J.Positions[J.Indices[i]];
			const FVector& B = J.Positions[J.Indices[i + 1]];
			const FVector& C = J.Positions[J.Indices[i + 2]];
			FVector N = FVector::CrossProduct(C - A, B - A);
			if (N.Z < 0.0) N = -N;
			J.Normals[J.Indices[i]] += N;
			J.Normals[J.Indices[i + 1]] += N;
			J.Normals[J.Indices[i + 2]] += N;
		}
		for (FVector& N : J.Normals) N = N.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		if (Doc->TryGetStringField(TEXT("aerial"), Aerial) && Aerial == FString::Printf(TEXT("relief-aerial-v1-%d-%d.jpg"), J.Region.X, J.Region.Y))
		{
			USimCruiseWorldResources::DecodeImageFile(FPaths::Combine(Config.CacheDir, Aerial), J.Photo, J.PhotoW, J.PhotoH);
		}
		if (Doc->TryGetStringField(TEXT("middle"), MiddleName) && MiddleName == FString::Printf(TEXT("relief-middle-v1-%d-%d.jpg"), J.Region.X, J.Region.Y))
		{
			USimCruiseWorldResources::DecodeImageFile(FPaths::Combine(Config.CacheDir, MiddleName), J.Middle, J.MiddleW, J.MiddleH);
		}
		// Snow on the ground now (RegionalSnow.gd): the helper's `snow` reads a day's MODIS snow
		// cover over the same 90 km square as a 256 px mask. NASA GIBS publishes a day two to
		// four days late, so the newest day it has within a week.
		for (int32 DaysBack = 1; DaysBack <= 7; ++DaysBack)
		{
			FSimCruiseAtlasRequest Snow;
			Snow.Command = TEXT("snow");
			const FString Date = (FDateTime::UtcNow() - FTimespan::FromDays(DaysBack)).ToString(TEXT("%Y-%m-%d"));
			Snow.Args = { FString::FromInt(J.Region.X), FString::FromInt(J.Region.Y), Date };
			Snow.TimeoutSeconds = 60.0;
			const FSimCruiseAtlasResult SnowResult = FSimCruiseAtlasClient::RunBlocking(Config, Snow, nullptr);
			const TSharedPtr<FJsonObject> SnowDoc = SnowResult.IsOk() && SnowResult.Json->Type == EJson::Object ? SnowResult.Json->AsObject() : nullptr;
			FString SnowStatus, Mask;
			if (!SnowDoc || !SnowDoc->TryGetStringField(TEXT("status"), SnowStatus) || SnowStatus != TEXT("available")) continue;
			J.SnowDate = Date;
			TArray<uint8> Png;
			if (SnowDoc->TryGetStringField(TEXT("mask"), Mask) && FBase64::Decode(Mask, Png))
			{
				const FString File = FPaths::Combine(Config.CacheDir, FString::Printf(TEXT("relief-snow-%d-%d-%s.png"), J.Region.X, J.Region.Y, *Date));
				if (FFileHelper::SaveArrayToFile(Png, *File))
				{
					USimCruiseWorldResources::DecodeImageFile(File, J.Snow, J.SnowW, J.SnowH);
				}
			}
			break;
		}
		J.bOk = true;
		J.bDone = true;
	});
}

void ASimCruiseRegionalTerrain::Install(FJob& Done)
{
	if (!BaseMaterial)
	{
#if WITH_EDITOR
		if (FApp::CanEverRender())
		{
			BaseMaterial = BuildMaterial(this, TEXT("M_SimCruiseLandscape"), RF_Transient);
		}
#else
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, *MaterialPath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
#endif
	}
	TArray<FVector2D> UV;
	UV.SetNumUninitialized(Done.Positions.Num());
	for (int32 i = 0; i < Done.Positions.Num(); ++i)
	{
		UV[i] = FVector2D(Done.Positions[i].X, Done.Positions[i].Y) / (2.0 * RadiusM * 100.0) + FVector2D(0.5, 0.5);
	}
	Mesh->ClearAllMeshSections();
	Mesh->CreateMeshSection(0, Done.Positions, Done.Indices, Done.Normals, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	Textures.Reset();
	if (BaseMaterial)
	{
		Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		Material->SetScalarParameterValue(TEXT("RadiusCm"), static_cast<float>(RadiusM * 100.0));
		// The grass field (world-detail-methods.md), as on the ground tiles: on unless
		// -SimCruiseNoGrassField; the dry share from -SimCruiseGrassDry=0..1.
		static const bool bGrassField = !FParse::Param(FCommandLine::Get(), TEXT("SimCruiseNoGrassField"));
		static const float GrassDry = [] { float V = 0.f; FParse::Value(FCommandLine::Get(), TEXT("SimCruiseGrassDry="), V); return FMath::Clamp(V, 0.f, 1.f); }();
		Material->SetScalarParameterValue(TEXT("GrassField"), bGrassField ? 1.f : 0.f);
		Material->SetScalarParameterValue(TEXT("GrassDry"), GrassDry);
		if (Done.Photo.Num() > 0)
		{
			UTexture2D* Photo = USimCruiseWorldResources::CreateTexture(this, TEXT("T_SimCruiseLandscapePhoto"), Done.Photo, Done.PhotoW, Done.PhotoH, true, false);
			Textures.Add(Photo);
			Material->SetTextureParameterValue(TEXT("Photo"), Photo);
			Material->SetScalarParameterValue(TEXT("HasPhoto"), 1.f);
		}
		if (Done.Middle.Num() > 0)
		{
			UTexture2D* MiddlePhoto = USimCruiseWorldResources::CreateTexture(this, TEXT("T_SimCruiseLandscapeMiddle"), Done.Middle, Done.MiddleW, Done.MiddleH, true, false);
			Textures.Add(MiddlePhoto);
			Material->SetTextureParameterValue(TEXT("Middle"), MiddlePhoto);
			Material->SetScalarParameterValue(TEXT("HasMiddle"), 1.f);
		}
		SnowMask = Done.Snow;
		SnowMaskSize = Done.Snow.Num() > 0 ? Done.SnowW : 0;
		NextSnowGround = 0.0;
		if (Done.Snow.Num() > 0)
		{
			UTexture2D* SnowTexture = USimCruiseWorldResources::CreateTexture(this, TEXT("T_SimCruiseLandscapeSnow"), Done.Snow, Done.SnowW, Done.SnowH, false, false);
			Textures.Add(SnowTexture);
			Material->SetTextureParameterValue(TEXT("Snow"), SnowTexture);
			Material->SetScalarParameterValue(TEXT("HasSnow"), 1.f);
		}
		Mesh->SetMaterial(0, Material);
	}
	Triangles = Done.Indices.Num() / 3;
	Shown = Done.Region;
	Failed = FIntPoint(-1, -1);
	UE_LOG(LogSimCruiseLandscape, Display, TEXT("Landscape %d_%d: %d triangles to %.0f km, %.0f m tolerance, photo %s, middle %s, snow of %s %s, %d requests"),
		Shown.X, Shown.Y, Triangles, RadiusM / 1000.0, Done.MaxErrorM, Done.Photo.Num() ? TEXT("yes") : TEXT("no"), Done.Middle.Num() ? TEXT("yes") : TEXT("no"),
		*Done.SnowDate, Done.Snow.Num() ? TEXT("seen") : TEXT("none"), Done.Requests);
}

void ASimCruiseRegionalTerrain::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* Car = Player ? Player->GetPawn() : nullptr;
	if (!Stream || !Stream->GetFrame().IsValid())
	{
		Mesh->SetVisibility(false);
		return;
	}
	const FSimCruiseWorldFrame& Frame = Stream->GetFrame();
	// A new place: the old land goes at once.
	if (Stream->GetTitle() != SeenTitle)
	{
		SeenTitle = Stream->GetTitle();
		Shown = FIntPoint(-1, -1);
		Requested = FIntPoint(-1, -1);
		Mesh->SetVisibility(false);
	}
	const double Now = FPlatformTime::Seconds();
	if (Job.IsValid() && Job->bDone)
	{
		const TSharedPtr<FJob, ESPMode::ThreadSafe> Done = Job;
		Job.Reset();
		if (Done->bOk && Done->Region == Requested)
		{
			Install(*Done);
		}
		else if (!Done->bOk)
		{
			UE_LOG(LogSimCruiseLandscape, Warning, TEXT("Landscape %d_%d unavailable: %s"), Done->Region.X, Done->Region.Y, *Done->Why);
			Failed = Done->Region;
			RetryAt = Now + 45.0;
		}
	}
	if (!Car)
	{
		Mesh->SetVisibility(false);
		return;
	}
	const FIntPoint Tile = Frame.TileAtWorldCm(Car->GetActorLocation());
	const FIntPoint Here(FMath::FloorToInt(Tile.X / 8.0), FMath::FloorToInt(Tile.Y / 8.0));
	if (Shown.X >= 0)
	{
		// The mesh's centre is the north-west corner of the region's middle tile.
		const FVector Centre = Frame.TileOriginCm(Shown * 8 + FIntPoint(4, 4));
		SetActorLocation(FVector(Centre.X, Centre.Y, 0.0));
		const bool bNear = FVector::Dist2D(Centre, Car->GetActorLocation()) < 1200000.0;
		Mesh->SetVisibility(bNear);
	}
	if (Now >= NextSnowGround)
	{
		NextSnowGround = Now + 2.0;
		ApplySnowToTiles();
	}
	if (Shown == Here || Job.IsValid() || (Failed == Here && Now < RetryAt) || Now < NextPoll)
	{
		return;
	}
	NextPoll = Now + 0.9;
	// Wait for the driving tiles first: they matter more (RegionalTerrain.gd: 4 tiles).
	if (Stream->NumTiles() < 4)
	{
		return;
	}
	StartJob(Here);
}

void ASimCruiseRegionalTerrain::ApplySnowToTiles()
{
	// Snow on the near ground too (RegionalSnow.gd): each driving tile's ground takes the
	// snow cover the 500 m mask has under its middle.
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream || Shown.X < 0) return;
	const FSimCruiseWorldFrame& Frame = Stream->GetFrame();
	const FVector Centre = Frame.TileOriginCm(Shown * 8 + FIntPoint(4, 4));
	for (int32 X = Shown.X * 8 - 8; X < Shown.X * 8 + 16; ++X)
	for (int32 Y = Shown.Y * 8 - 8; Y < Shown.Y * 8 + 16; ++Y)
	{
		ASimCruiseWorldTile* Tile = Stream->GetTile(FIntPoint(X, Y));
		if (!Tile) continue;
		const int32 Terrain = static_cast<int32>(ESimCruiseTileLayer::Terrain);
		UMaterialInstanceDynamic* Ground = Tile->Materials.IsValidIndex(Terrain) ? Tile->Materials[Terrain].Get() : nullptr;
		if (!Ground) continue;
		double Cover = 0.0;
		if (SnowMaskSize > 0)
		{
			const FVector Mid = Tile->GetActorLocation() + FVector(Frame.TileSizeCm() * 0.5, Frame.TileSizeCm() * 0.5, 0.0) - Centre;
			const FVector2D UV = FVector2D(Mid.X, Mid.Y) / (2.0 * RadiusM * 100.0) + FVector2D(0.5, 0.5);
			const int32 Px = FMath::Clamp(FMath::FloorToInt(UV.X * SnowMaskSize), 0, SnowMaskSize - 1);
			const int32 Py = FMath::Clamp(FMath::FloorToInt(UV.Y * SnowMaskSize), 0, SnowMaskSize - 1);
			Cover = FMath::SmoothStep(0.38, 0.76, SnowMask[Py * SnowMaskSize + Px].R / 255.0);
		}
		Ground->SetScalarParameterValue(TEXT("SnowCover"), static_cast<float>(Cover));
	}
}
