// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTrees.h"

#include "Async/Async.h"
#include "Atlas/SimCruiseAtlasClient.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseGltfModel.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileDetail.h"
#include "World/SimCruiseWorldResources.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseTrees, Log, All);

namespace
{
	/**
	 * Where a tree changes detail level (cm from the camera), and the cross-fade band either side.
	 * Outside the bands one level draws; inside, the two levels dither into each other. The
	 * distance is the tree's (its bounds centre, the point the GPU's per-instance draw distances
	 * test), the same for both levels, so a ring ends exactly where its fade does.
	 */
	constexpr float LodSwitchCm[2] = { 3300.f, 9200.f };
	constexpr float LodBandCm[2] = { 200.f, 400.f };
}

namespace
{
	// The detail levels cross-fade over a band of distance instead of switching: a level fades
	// out (pixel shown while n < w) as the next fades in (shown while 1 - n < w) with the same
	// screen noise n, so every pixel shows exactly one level. Depth is the tree's distance (one
	// value for the whole tree and both levels), not the pixel's. Shadows keep the whole mask
	// (the rings' own draw distances switch them).
	const TCHAR* FadeCode = TEXT(R"HLSL(
float fin = FadeIn > 0.0 ? saturate((Depth - (FadeIn - BandIn)) / max(2.0 * BandIn, 1.0)) : 1.0;
float fout = saturate(((FadeOut + BandOut) - Depth) / max(2.0 * BandOut, 1.0));
float2 sp = Parameters.SvPosition.xy + float2(View.StateFrameIndexMod8 * 7.0, View.StateFrameIndexMod8 * 3.0);
float n = frac(52.9829189 * frac(dot(sp, float2(0.06711056, 0.00583715)))); // interleaved gradient noise
float keep = ((1.0 - n) < fin ? 1.0 : 0.0) * (n < fout ? 1.0 : 0.0);
)HLSL");

	const TCHAR* LeafCode = TEXT(R"HLSL(
float4 t = Texture2DSample(Foliage, FoliageSampler, UV);
RawMask = t.a;
Mask = t.a * keep;
float3 c = t.rgb * Tint.rgb;
Sss = c * float3(0.7, 1.0, 0.4);
Roughness = 0.8;
return c;
)HLSL");

	const TCHAR* BarkCode = TEXT(R"HLSL(
Mask = keep;
float3 b = Texture2DSample(Bark, BarkSampler, UV * float2(2.0, 6.0)).rgb;
Roughness = 0.95;
return b;
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

	UMaterialExpression* ScalarParam(UMaterial* Material, const TCHAR* Name, float Default)
	{
		UMaterialExpressionScalarParameter* P = AddExpression<UMaterialExpressionScalarParameter>(Material);
		P->ParameterName = Name;
		P->DefaultValue = Default;
		return P;
	}

	/** The cross-fade's inputs: the tree's distance and the level's band (cm; FadeIn 0: no fade in). */
	void AddFadeInputs(UMaterial* Material, UMaterialExpressionCustom* Custom)
	{
		// The tree's distance from the camera: its bounds centre (FadeCentre, in the mesh's own
		// space, the same for all three levels of a species) to the camera, radial, as the GPU's
		// per-instance draw distances measure it. Worked out per vertex (the instance's transform
		// is there) and passed on, constant over the tree.
		UMaterialExpressionVectorParameter* Centre = AddExpression<UMaterialExpressionVectorParameter>(Material);
		Centre->ParameterName = TEXT("FadeCentre");
		Centre->DefaultValue = FLinearColor(0.f, 0.f, 0.f, 0.f);
		UMaterialExpressionTransformPosition* ToWorld = AddExpression<UMaterialExpressionTransformPosition>(Material);
		ToWorld->TransformSourceType = TRANSFORMPOSSOURCE_Local;
		ToWorld->TransformType = TRANSFORMPOSSOURCE_World;
		ToWorld->Input.Connect(0, Centre);
		UMaterialExpressionDistance* Dist = AddExpression<UMaterialExpressionDistance>(Material);
		Dist->A.Connect(0, ToWorld);
		Dist->B.Connect(0, AddExpression<UMaterialExpressionCameraPositionWS>(Material));
		UMaterialExpressionVertexInterpolator* PerTree = AddExpression<UMaterialExpressionVertexInterpolator>(Material);
		PerTree->Input.Connect(0, Dist);
		AddInput(Custom, TEXT("Depth"), PerTree);
		AddInput(Custom, TEXT("FadeIn"), ScalarParam(Material, TEXT("FadeIn"), 0.f));
		AddInput(Custom, TEXT("BandIn"), ScalarParam(Material, TEXT("BandIn"), 0.f));
		AddInput(Custom, TEXT("FadeOut"), ScalarParam(Material, TEXT("FadeOut"), 1.0e9f));
		AddInput(Custom, TEXT("BandOut"), ScalarParam(Material, TEXT("BandOut"), 1.f));
	}

	/** Main passes take the faded mask, shadow passes the whole one. */
	UMaterialExpression* FadeOrShadow(UMaterial* Material, UMaterialExpression* From, int32 Faded, UMaterialExpression* Whole, int32 WholeOut)
	{
		UMaterialExpressionShadowReplace* S = AddExpression<UMaterialExpressionShadowReplace>(Material);
		S->Default.Connect(Faded, From);
		if (Whole) S->Shadow.Connect(WholeOut, Whole);
		else
		{
			UMaterialExpressionConstant* One = AddExpression<UMaterialExpressionConstant>(Material);
			One->R = 1.f;
			S->Shadow.Connect(0, One);
		}
		return S;
	}

	UMaterialExpression* TextureParam(UMaterial* Material, const TCHAR* Name)
	{
		UMaterialExpressionTextureObjectParameter* P = AddExpression<UMaterialExpressionTextureObjectParameter>(Material);
		P->ParameterName = Name;
		P->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		P->SamplerType = SAMPLERTYPE_Color;
		return P;
	}
#endif

	FString TreeArt(const FString& File)
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("art"), TEXT("trees"), File);
	}

	/** One glTF primitive as mesh buffers: Godot's Y-up metres to Unreal's Z-up centimetres. */
	void AppendPrimitive(const FSimCruiseGltfPrimitive& P, FSimCruiseMeshData& Out)
	{
		const int32 Base = Out.Positions.Num();
		for (int32 i = 0; i < P.Positions.Num(); ++i)
		{
			const FVector3f& V = P.Positions[i];
			const FVector3f N = P.Normals.IsValidIndex(i) ? P.Normals[i] : FVector3f::UpVector;
			const FVector2f UV = P.UV0.IsValidIndex(i) ? P.UV0[i] : FVector2f::ZeroVector;
			Out.Add(FVector(V.X, V.Z, V.Y) * 100.0, FVector(N.X, N.Z, N.Y), FLinearColor::White, FVector2D(UV), FVector2D::ZeroVector);
		}
		for (int32 i = 0; i + 2 < P.Indices.Num(); i += 3)
		{
			// The Y/Z swap mirrors the mesh: swap two corners to keep the winding.
			Out.Indices.Add(Base + static_cast<int32>(P.Indices[i]));
			Out.Indices.Add(Base + static_cast<int32>(P.Indices[i + 2]));
			Out.Indices.Add(Base + static_cast<int32>(P.Indices[i + 1]));
		}
	}

	/**
	 * The leaf cards' mip chain. The PNGs are black where they are transparent, so a plain box
	 * filter (and the bilinear lookup at the cut edge) mixes black into every leaf edge: the
	 * crowns darken into flat dark blobs with distance. Here the transparent texels take the
	 * mean leaf colour, each mip averages colour weighted by alpha, and each mip's alpha is
	 * scaled so the share of texels above the clip value stays that of the full-size image
	 * (a box-filtered alpha grows the clusters: 46 % to 53 % covered by the 96 px mip).
	 * All of it on the CPU at load: no cost on the GPU.
	 */
	void LeafMipChain(TArray<FColor> Pixels, int32 Width, int32 Height, float Clip, TArray<TArray<FColor>>& Out)
	{
		Out.Reset();
		FLinearColor Mean(0, 0, 0, 0);
		double MeanW = 0.0;
		for (const FColor& C : Pixels)
		{
			const float A = C.A / 255.f;
			Mean += FLinearColor(C) * A;
			MeanW += A;
		}
		const FColor Fill = MeanW > 0.0 ? (Mean * static_cast<float>(1.0 / MeanW)).ToFColor(true) : FColor::Black;
		for (FColor& C : Pixels)
		{
			if (C.A == 0) { C.R = Fill.R; C.G = Fill.G; C.B = Fill.B; }
		}
		const uint8 Cut = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Clip * 255.f), 1, 254));
		auto Covered = [Cut](const TArray<FColor>& L, float Scale)
		{
			int64 N = 0;
			for (const FColor& C : L) N += FMath::Min(C.A * Scale, 255.f) > Cut ? 1 : 0;
			return static_cast<double>(N) / FMath::Max(1, L.Num());
		};
		const double Target = Covered(Pixels, 1.f);
		Out.Add(MoveTemp(Pixels));
		int32 W = Width, H = Height;
		while (W > 1 || H > 1)
		{
			const TArray<FColor>& Level = Out.Last();
			const int32 NW = FMath::Max(1, W / 2), NH = FMath::Max(1, H / 2);
			TArray<FColor> Next;
			Next.SetNumUninitialized(NW * NH);
			for (int32 y = 0; y < NH; ++y)
			for (int32 x = 0; x < NW; ++x)
			{
				FLinearColor Sum(0, 0, 0, 0), Plain(0, 0, 0, 0);
				float A = 0.f;
				for (int32 dy = 0; dy < 2; ++dy)
				for (int32 dx = 0; dx < 2; ++dx)
				{
					const FColor& C = Level[FMath::Min(2 * y + dy, H - 1) * W + FMath::Min(2 * x + dx, W - 1)];
					const FLinearColor L(C);
					Sum += L * (C.A / 255.f);
					Plain += L;
					A += C.A / 255.f;
				}
				FColor C = (A > 1e-4f ? Sum * (1.f / A) : Plain * 0.25f).ToFColor(true);
				C.A = static_cast<uint8>(FMath::RoundToInt(A * 0.25f * 255.f));
				Next[y * NW + x] = C;
			}
			// The alpha scale that keeps the coverage (bisection; coverage grows with the scale).
			if (Target > 0.0 && Next.Num() >= 16)
			{
				float Lo = 0.25f, Hi = 4.f;
				for (int32 i = 0; i < 16; ++i)
				{
					const float Mid = 0.5f * (Lo + Hi);
					(Covered(Next, Mid) > Target ? Hi : Lo) = Mid;
				}
				const float Scale = 0.5f * (Lo + Hi);
				for (FColor& C : Next) C.A = static_cast<uint8>(FMath::Min(FMath::RoundToInt(C.A * Scale), 255));
			}
			Out.Add(MoveTemp(Next));
			W = NW;
			H = NH;
		}
	}

	uint32 Hash2(int32 A, int32 B)
	{
		uint32 H = static_cast<uint32>(A) * 73856093u ^ static_cast<uint32>(B) * 19349663u;
		H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
		return H;
	}
}

struct ASimCruiseTrees::FShrubFetch
{
	std::atomic<bool> bDone{ false };
	TArray<FColor> Pixels;
};

struct ASimCruiseTrees::FFetch
{
	std::atomic<bool> bDone{ false };
	bool bOk = false;
	TArray<FSimCruiseTreeRow> Rows;
	FString Why;
};

FString ASimCruiseTrees::LeafMaterialPath() { return TEXT("/Game/World/Materials/M_SimCruiseTreeLeaf.M_SimCruiseTreeLeaf"); }
FString ASimCruiseTrees::BarkMaterialPath() { return TEXT("/Game/World/Materials/M_SimCruiseTreeBark.M_SimCruiseTreeBark"); }

UMaterial* ASimCruiseTrees::BuildLeafMaterial(UObject* Outer, FName Name, EObjectFlags Flags)
{
#if WITH_EDITOR
	UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
	Material->BlendMode = BLEND_Masked;
	Material->OpacityMaskClipValue = 0.3f;
	Material->TwoSided = true;
	Material->SetShadingModel(MSM_TwoSidedFoliage);
	Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
	UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material);
	Custom->Inputs.Reset();
	Custom->OutputType = CMOT_Float3;
	Custom->Code = FString(FadeCode) + LeafCode;
	Custom->AdditionalOutputs.Add({ FName(TEXT("Mask")), CMOT_Float1 });
	Custom->AdditionalOutputs.Add({ FName(TEXT("Sss")), CMOT_Float3 });
	Custom->AdditionalOutputs.Add({ FName(TEXT("Roughness")), CMOT_Float1 });
	Custom->AdditionalOutputs.Add({ FName(TEXT("RawMask")), CMOT_Float1 });
	AddFadeInputs(Material, Custom);
	AddInput(Custom, TEXT("UV"), AddExpression<UMaterialExpressionTextureCoordinate>(Material));
	AddInput(Custom, TEXT("Foliage"), TextureParam(Material, TEXT("Foliage")));
	UMaterialExpressionVectorParameter* Tint = AddExpression<UMaterialExpressionVectorParameter>(Material);
	Tint->ParameterName = TEXT("Tint");
	Tint->DefaultValue = FLinearColor(0.62f, 0.68f, 0.55f);
	AddInput(Custom, TEXT("Tint"), Tint);
	Custom->RebuildOutputs();
	Material->GetEditorOnlyData()->BaseColor.Connect(0, Custom);
	Material->GetEditorOnlyData()->OpacityMask.Connect(0, FadeOrShadow(Material, Custom, 1, Custom, 4));
	Material->GetEditorOnlyData()->SubsurfaceColor.Connect(2, Custom);
	Material->GetEditorOnlyData()->Roughness.Connect(3, Custom);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	return Material;
#else
	return nullptr;
#endif
}

UMaterial* ASimCruiseTrees::BuildBarkMaterial(UObject* Outer, FName Name, EObjectFlags Flags)
{
#if WITH_EDITOR
	UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
	Material->TwoSided = true;
	// Masked only for the detail levels' cross-fade (dithered, never translucent).
	Material->BlendMode = BLEND_Masked;
	Material->OpacityMaskClipValue = 0.5f;
	Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
	UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material);
	Custom->Inputs.Reset();
	Custom->OutputType = CMOT_Float3;
	Custom->Code = FString(FadeCode) + BarkCode;
	Custom->AdditionalOutputs.Add({ FName(TEXT("Roughness")), CMOT_Float1 });
	Custom->AdditionalOutputs.Add({ FName(TEXT("Mask")), CMOT_Float1 });
	AddFadeInputs(Material, Custom);
	AddInput(Custom, TEXT("UV"), AddExpression<UMaterialExpressionTextureCoordinate>(Material));
	AddInput(Custom, TEXT("Bark"), TextureParam(Material, TEXT("Bark")));
	Custom->RebuildOutputs();
	Material->GetEditorOnlyData()->BaseColor.Connect(0, Custom);
	Material->GetEditorOnlyData()->Roughness.Connect(1, Custom);
	Material->GetEditorOnlyData()->OpacityMask.Connect(0, FadeOrShadow(Material, Custom, 2, nullptr, 0));
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	return Material;
#else
	return nullptr;
#endif
}

TArray<FSimCruiseTreeRow> ASimCruiseTrees::ParseCanopy(const TSharedPtr<FJsonValue>& Json)
{
	TArray<FSimCruiseTreeRow> Rows;
	const TSharedPtr<FJsonObject>* Doc = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Trees = nullptr;
	if (!Json.IsValid() || !Json->TryGetObject(Doc) || !(*Doc)->TryGetArrayField(TEXT("trees"), Trees))
	{
		return Rows;
	}
	// With `--segment` the helper adds "shapes", one per segmented crown, in the order of the
	// first rows of "trees" (rows a city inventory adds after them have none): the 8 reaches and
	// the crown base.
	const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
	if (!(*Doc)->TryGetArrayField(TEXT("shapes"), Shapes)) Shapes = nullptr;
	for (int32 i = 0; i < Trees->Num(); ++i)
	{
		const TSharedPtr<FJsonValue>& T = (*Trees)[i];
		const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
		if (!T.IsValid() || !T->TryGetArray(R) || R->Num() < 4) continue;
		FSimCruiseTreeRow Row;
		Row.TileUV = FVector2D((*R)[0]->AsNumber(), (*R)[1]->AsNumber());
		Row.HeightM = (*R)[2]->AsNumber();
		Row.CrownM = (*R)[3]->AsNumber();
		// Peaks under 3 m are shrubs; the map tops out near 60 m.
		if (Row.HeightM < 3.0 || Row.HeightM > 70.0 || Row.CrownM <= 0.0) continue;
		if (Row.TileUV.X < 0.0 || Row.TileUV.Y < 0.0 || Row.TileUV.X > 1.0 || Row.TileUV.Y > 1.0) continue;
		const TArray<TSharedPtr<FJsonValue>>* Shape = nullptr;
		if (Shapes && Shapes->IsValidIndex(i) && (*Shapes)[i].IsValid() && (*Shapes)[i]->TryGetArray(Shape) && Shape->Num() >= 9)
		{
			bool bOk = true;
			for (int32 k = 0; k < 8; ++k)
			{
				Row.RadiiM[k] = static_cast<float>((*Shape)[k]->AsNumber());
				bOk &= Row.RadiiM[k] > 0.f && Row.RadiiM[k] < 30.f;
			}
			Row.CrownBaseM = static_cast<float>(FMath::Clamp((*Shape)[8]->AsNumber(), 0.0, Row.HeightM));
			Row.bShape = bOk;
		}
		Rows.Add(Row);
	}
	return Rows;
}

FSimCruiseCrownFit ASimCruiseTrees::FitCrown(const FSimCruiseTreeRow& Row)
{
	FSimCruiseCrownFit Fit;
	Fit.LongM = Fit.ShortM = Row.CrownM;
	if (!Row.bShape) return Fit;
	// Directions k * 45 degrees anticlockwise from east (north up); the reach r_k from the top.
	// The crown's middle: opposite reaches differ by twice the offset along their axis, and the
	// four axes 45 degrees apart give o = 1/2 sum_k (r_k - r_k+4) / 2 * dir_k.
	FVector2D East(0.0, 0.0); // x east, y north
	double Half[4];
	for (int32 k = 0; k < 4; ++k)
	{
		const double A = k * UE_DOUBLE_PI / 4.0;
		const double D = 0.5 * (Row.RadiiM[k] - Row.RadiiM[k + 4]);
		East += 0.5 * D * FVector2D(FMath::Cos(A), FMath::Sin(A));
		Half[k] = FMath::Max(0.25, 0.5 * (Row.RadiiM[k] + Row.RadiiM[k + 4]));
	}
	Fit.OffsetM = FVector2D(East.X, -East.Y); // tile axes: y south
	// An ellipse's 1/r^2 = P + Q cos 2t + R sin 2t (semi-axes a >= b at angle f: P = (1/a^2 +
	// 1/b^2) / 2, (Q, R) = (1/a^2 - 1/b^2) / 2 (cos 2f, sin 2f)): fitted from the four axes' half
	// widths. The long axis is where 1/r^2 is least.
	double Inv[4];
	for (int32 k = 0; k < 4; ++k) Inv[k] = 1.0 / (Half[k] * Half[k]);
	const double P = 0.25 * (Inv[0] + Inv[1] + Inv[2] + Inv[3]);
	const double Q = 0.5 * (Inv[0] - Inv[2]), R = 0.5 * (Inv[1] - Inv[3]);
	const double Amp = FMath::Min(FMath::Sqrt(Q * Q + R * R), 0.9 * P);
	const double LongAngle = 0.5 * FMath::Atan2(-R, -Q); // anticlockwise from east, north up
	Fit.LongM = 2.0 / FMath::Sqrt(P - Amp);
	Fit.ShortM = 2.0 / FMath::Sqrt(P + Amp);
	Fit.YawDeg = FMath::RadiansToDegrees(-LongAngle); // tile axes (y south): clockwise from above
	return Fit;
}

double ASimCruiseTrees::ConiferShare(double LatDeg, double LonDeg, double ElevationM)
{
	// US forest regions (USDA Forest Service forest-type groups), broadly: the Pacific
	// Northwest and the western mountains are conifer forests, the north woods mixed,
	// the south-east coastal plain loblolly and longleaf pine with hardwoods, the rest
	// of the east mostly broadleaf.
	if (LonDeg < -117.0 && LatDeg > 40.0) return 0.9;
	if (LonDeg < -102.0) return ElevationM > 1500.0 ? 0.9 : 0.5;
	if (LatDeg > 44.0) return 0.55;
	if (LatDeg < 34.5 && LonDeg > -97.0) return 0.45;
	if (ElevationM > 1100.0) return 0.5;
	return 0.15;
}

ASimCruiseTrees::ASimCruiseTrees()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseTrees::BeginPlay()
{
	Super::BeginPlay();
}

void ASimCruiseTrees::EndPlay(const EEndPlayReason::Type Reason)
{
	Fetches.Reset();
	Super::EndPlay(Reason);
}

bool ASimCruiseTrees::BuildMeshes()
{
	bMeshesTried = true;
	UMaterialInterface* LeafBase = nullptr;
	UMaterialInterface* BarkBase = nullptr;
#if WITH_EDITOR
	LeafBase = BuildLeafMaterial(this, TEXT("M_SimCruiseTreeLeaf"), RF_Transient);
	BarkBase = BuildBarkMaterial(this, TEXT("M_SimCruiseTreeBark"), RF_Transient);
#else
	LeafBase = LoadObject<UMaterialInterface>(nullptr, *LeafMaterialPath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
	BarkBase = LoadObject<UMaterialInterface>(nullptr, *BarkMaterialPath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
#endif
	if (!LeafBase || !BarkBase)
	{
		UE_LOG(LogSimCruiseTrees, Error, TEXT("No tree materials (run the SimCruiseBuildWorldMaterials commandlet)"));
		return false;
	}
	// -SimCruiseOldTreeMips: the plain box-filtered leaf mips (for an A/B).
	const bool bLeafMips = !FParse::Param(FCommandLine::Get(), TEXT("SimCruiseOldTreeMips"));
	auto Texture = [this, bLeafMips](const TCHAR* File) -> UTexture2D*
	{
		TArray<FColor> Pixels;
		int32 W = 0, H = 0;
		if (!USimCruiseWorldResources::DecodeImageFile(TreeArt(File), Pixels, W, H)) return nullptr;
		const FName Name(*FString::Printf(TEXT("T_SimCruiseTree_%s"), *FPaths::GetBaseFilename(File)));
		UTexture2D* T = nullptr;
		if (bLeafMips && FPaths::GetExtension(File) == TEXT("png"))
		{
			TArray<TArray<FColor>> Mips;
			LeafMipChain(MoveTemp(Pixels), W, H, 0.3f, Mips); // 0.3: the leaf material's clip value
			T = USimCruiseWorldResources::CreateTextureFromMips(this, Name, MoveTemp(Mips), W, H, true, true);
		}
		else
		{
			T = USimCruiseWorldResources::CreateTexture(this, Name, Pixels, W, H, true, true);
		}
		Textures.Add(T);
		return T;
	};
	auto Leaf = [&](const TCHAR* File)
	{
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(LeafBase, this);
		if (UTexture2D* T = Texture(File)) M->SetTextureParameterValue(TEXT("Foliage"), T);
		Materials.Add(M);
		return M;
	};
	UMaterialInstanceDynamic* Bark = UMaterialInstanceDynamic::Create(BarkBase, this);
	if (UTexture2D* T = Texture(TEXT("tree-bark.jpg"))) Bark->SetTextureParameterValue(TEXT("Bark"), T);
	Materials.Add(Bark);

	const TCHAR* Species[2] = { TEXT("leaf"), TEXT("pine") };
	for (int32 S = 0; S < 2; ++S)
	{
		UMaterialInstanceDynamic* Crown = Leaf(*FString::Printf(TEXT("cluster-%s.png"), Species[S]));
		UMaterialInstanceDynamic* Spray = Leaf(*FString::Printf(TEXT("spray-%s.png"), Species[S]));
		UStaticMesh* LevelMeshes[3] = { nullptr, nullptr, nullptr };
		TArray<UMaterialInstanceDynamic*> LevelMaterials;
		for (int32 Level = 0; Level < 3; ++Level)
		{
			// Level 0: the wood with its sprays plus the crown clusters; levels 1 and 2: one mesh of bark and crown.
			TArray<FSimCruiseMeshData> Parts;
			TArray<UMaterialInterface*> Slots;
			auto LoadInto = [&](const FString& File) -> bool
			{
				FSimCruiseGltfModel Model;
				FString Error;
				if (!SimCruiseGltf::Load(TreeArt(File), Model, Error))
				{
					UE_LOG(LogSimCruiseTrees, Error, TEXT("%s"), *Error);
					return false;
				}
				for (const FSimCruiseGltfPrimitive& P : Model.Primitives)
				{
					AppendPrimitive(P, Parts.AddDefaulted_GetRef());
				}
				return true;
			};
			if (Level == 0)
			{
				if (!LoadInto(FString::Printf(TEXT("wood-%s-0.glb"), Species[S])) || !LoadInto(FString::Printf(TEXT("crown-%s-0.glb"), Species[S]))) return false;
				Slots = { Bark, Spray, Crown };
			}
			else
			{
				if (!LoadInto(FString::Printf(TEXT("crown-%s-%d.glb"), Species[S], Level))) return false;
				Slots = { Bark, Crown };
			}
			// This level's own instances of the materials: its cross-fade band (FadeCode).
			{
				static const float FadeIn[3] = { 0.f, LodSwitchCm[0], LodSwitchCm[1] };
				static const float BandIn[3] = { 0.f, LodBandCm[0], LodBandCm[1] };
				static const float FadeOut[3] = { LodSwitchCm[0], LodSwitchCm[1], 1.0e9f };
				static const float BandOut[3] = { LodBandCm[0], LodBandCm[1], 1.f };
				for (UMaterialInterface*& M : Slots)
				{
					// From the base material with the slot's overrides copied (its leaf texture and
					// tint): an instance parented to another dynamic instance drew with the default
					// texture and a solid mask (grey shards in the Dragon capture, 2026-11-14).
					UMaterialInstanceDynamic* Src = Cast<UMaterialInstanceDynamic>(M);
					UMaterialInstanceDynamic* L = UMaterialInstanceDynamic::Create(Src ? Src->GetMaterial() : M, this);
					if (Src) L->CopyParameterOverrides(Src);
					L->SetScalarParameterValue(TEXT("FadeIn"), FadeIn[Level]);
					L->SetScalarParameterValue(TEXT("BandIn"), BandIn[Level]);
					L->SetScalarParameterValue(TEXT("FadeOut"), FadeOut[Level]);
					L->SetScalarParameterValue(TEXT("BandOut"), BandOut[Level]);
					Materials.Add(L);
					M = L;
				}
			}
			Parts.SetNum(Slots.Num());
			TArray<const FSimCruiseMeshData*> Ptrs;
			for (const FSimCruiseMeshData& P : Parts) Ptrs.Add(&P);
			UStaticMesh* Mesh = SimCruiseWorldMaterials::BuildRuntimeMesh(*FString::Printf(TEXT("SM_SimCruiseTree_%s%d"), Species[S], Level), Ptrs, true, false);
			if (!Mesh) return false;
			TArray<FStaticMaterial>& StaticSlots = Mesh->GetStaticMaterials();
			StaticSlots.SetNum(Slots.Num());
			for (int32 i = 0; i < Slots.Num(); ++i) StaticSlots[i].MaterialInterface = Slots[i];
			Meshes.Add(Mesh);
			LevelMeshes[Level] = Mesh;
			for (UMaterialInterface* M : Slots) LevelMaterials.Add(Cast<UMaterialInstanceDynamic>(M));
		}
		// One bounds box for the species' three levels (their union), so a tree has one centre
		// and one distance in every level: the GPU's draw distances and the materials' fade
		// (FadeCentre) both measure from it, and the two levels in a band add up to one tree.
		FBox Union(ForceInit);
		for (UStaticMesh* M : LevelMeshes) Union += M->GetBoundingBox();
		for (UStaticMesh* M : LevelMeshes)
		{
			const FBox Own = M->GetBoundingBox();
			M->SetPositiveBoundsExtension(Union.Max - Own.Max);
			M->SetNegativeBoundsExtension(Own.Min - Union.Min);
			M->CalculateExtendedBounds();
		}
		const FVector Centre = Union.GetCenter();
		for (UMaterialInstanceDynamic* M : LevelMaterials)
		{
			if (M) M->SetVectorParameterValue(TEXT("FadeCentre"), FLinearColor(Centre.X, Centre.Y, Centre.Z, 0.f));
		}
		for (int32 Level = 0; Level < 3; ++Level)
		{
			UHierarchicalInstancedStaticMeshComponent* Ring = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			Ring->SetStaticMesh(LevelMeshes[Level]);
			Ring->SetMobility(EComponentMobility::Movable);
			Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ring->SetupAttachment(RootComponent);
			// The crowns' detail by distance: the full tree to 33 m, the cluster crown to 92 m, the low
			// crown to 600 m (Grove.gd's distances; 45 and 130 m were tried and cost frame time). SetCullDistances is where a ring
			// ends; its near limit is InstanceMinDrawDistance. Without one every ring drew from the
			// camera out, so near the car all three crowns stood on top of each other (three times
			// the triangles, and the blobby low crown over the detailed one). Each ring starts where
			// its fade in starts and ends where its fade out ends (one tree distance for both, see
			// FadeCentre), so outside the bands one level draws; 50 cm spare for rounding.
			constexpr float SpareCm = 50.f;
			const int32 Near = Level == 0 ? 0 : FMath::RoundToInt(FMath::Max(0.f, LodSwitchCm[Level - 1] - LodBandCm[Level - 1] - SpareCm));
			const int32 Far = Level == 2 ? 60000 : FMath::RoundToInt(LodSwitchCm[Level] + LodBandCm[Level] + SpareCm);
			Ring->InstanceMinDrawDistance = Near;
			Ring->SetCullDistances(Far, Far);
			Ring->SetCastShadow(Level < 2);
			Ring->bAffectDistanceFieldLighting = false;
			Ring->RegisterComponent();
			Rings.Add(Ring);
		}
	}
	// Shrubs: the low-detail broadleaf crown, small and sage-green (big sagebrush, rabbitbrush).
	{
		UMaterialInstanceDynamic* Sage = Leaf(TEXT("cluster-leaf.png"));
		Sage->SetVectorParameterValue(TEXT("Tint"), FLinearColor(0.62f, 0.64f, 0.5f));
		ShrubRing = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		ShrubRing->SetStaticMesh(Meshes[2]);
		ShrubRing->SetMaterial(0, Bark);
		ShrubRing->SetMaterial(1, Sage);
		ShrubRing->SetMobility(EComponentMobility::Movable);
		ShrubRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShrubRing->SetupAttachment(RootComponent);
		ShrubRing->SetCullDistances(0, 25000);
		ShrubRing->SetCastShadow(true);
		ShrubRing->bAffectDistanceFieldLighting = false;
		ShrubRing->RegisterComponent();
	}
	for (int32 i = 0; i < 32; ++i)
	{
		UCapsuleComponent* Trunk = NewObject<UCapsuleComponent>(this);
		Trunk->SetupAttachment(RootComponent);
		Trunk->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Trunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Trunk->SetHiddenInGame(true);
		Trunk->SetMobility(EComponentMobility::Movable);
		Trunk->RegisterComponent();
		Trunks.Add(Trunk);
	}
	UE_LOG(LogSimCruiseTrees, Display, TEXT("Built %d tree meshes"), Meshes.Num());
	return true;
}

void ASimCruiseTrees::Refresh()
{
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream || Rings.Num() != 6) return;
	for (UHierarchicalInstancedStaticMeshComponent* Ring : Rings) Ring->ClearInstances();
	const FSimCruiseWorldFrame& Frame = Stream->GetFrame();
	const double Side = Frame.TileSizeM;
	TArray<TArray<FTransform>> Lists;
	Lists.SetNum(6);
	// The palm variants the tiles plant on lawns, for the measured trees where palms grow.
	if (PalmRings.Num() == 0 && Stream->Resources && Stream->Resources->PalmMeshes.Num() > 0)
	{
		for (UStaticMesh* Palm : Stream->Resources->PalmMeshes)
		{
			UHierarchicalInstancedStaticMeshComponent* Ring = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			Ring->SetStaticMesh(Palm);
			Ring->SetMobility(EComponentMobility::Movable);
			Ring->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Ring->SetupAttachment(RootComponent);
			Ring->SetCullDistances(0, 60000);
			Ring->bAffectDistanceFieldLighting = false;
			Ring->RegisterComponent();
			PalmRings.Add(Ring);
		}
	}
	for (UHierarchicalInstancedStaticMeshComponent* Ring : PalmRings) Ring->ClearInstances();
	TArray<TArray<FTransform>> PalmLists;
	PalmLists.SetNum(PalmRings.Num());
	Placed = 0;
	Feet.Reset();
	LastTrunkFocus = FVector(1e30);
	for (const FIntPoint& Id : Shown)
	{
		const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
		const TArray<FSimCruiseTreeRow>* Rows = Canopy.Find(Id);
		if (!Tile || !Rows || !Tile->GetBuild()) continue;
		const FVector Origin = Tile->GetActorLocation();
		double Lon = 0.0, Lat = 0.0;
		Frame.WorldCmToLonLat(Origin, Lon, Lat);
		const double Conifers = ConiferShare(Lat, Lon, Tile->GetBuild()->MinHeightM);
		const bool bPalms = PalmRings.Num() > 0 && SimCruiseTileDetail::IsCoconutPalmRegion(Lat, Lon);
		const TArray<FColor>& Mask = Tile->GetBuild()->Mask;

		// Roads keep clear: a coarse grid of the carriageway's centre points and half widths.
		TMultiMap<FIntPoint, FVector3d> RoadCells;
		for (const FSimCruiseRibbon& Ribbon : Tile->GetBuild()->Roads.Ribbons)
		{
			for (const FSimCruiseRibbonStation& St : Ribbon.St)
			{
				RoadCells.Add(FIntPoint(FMath::FloorToInt(St.C.X / 20.0), FMath::FloorToInt(St.C.Y / 20.0)), FVector3d(St.C.X, St.C.Y, St.HalfW));
			}
		}
		for (const FSimCruiseTreeRow& Row : *Rows)
		{
			// A segmented crown stands at its own middle (its top is where the canopy is highest,
			// which for a one-sided or leaning crown is off the middle), turned to its long axis.
			const FSimCruiseCrownFit Fit = FitCrown(Row);
			const FVector2D Local = Row.TileUV * Side + Fit.OffsetM;
			const double Clear = FMath::Max(1.5, Row.CrownM * 0.3);
			bool bOnRoad = false;
			const FIntPoint Cell(FMath::FloorToInt(Local.X / 20.0), FMath::FloorToInt(Local.Y / 20.0));
			for (int32 dx = -1; dx <= 1 && !bOnRoad; ++dx)
			for (int32 dy = -1; dy <= 1 && !bOnRoad; ++dy)
			{
				TArray<FVector3d> Points;
				RoadCells.MultiFind(Cell + FIntPoint(dx, dy), Points);
				for (const FVector3d& P : Points)
				{
					if (FVector2D::Distance(Local, FVector2D(P.X, P.Y)) < P.Z + Clear) { bOnRoad = true; break; }
				}
			}
			if (bOnRoad) continue;
			FVector At = Origin + FVector(Local.X * 100.0, Local.Y * 100.0, 0.0);
			double Z = 0.0;
			if (!Stream->SurfaceHeightAt(At, Z)) continue;
			At.Z = Z - 10.0;
			const uint32 H = Hash2(FMath::RoundToInt(Local.X * 10.0) + Id.X * 7919, FMath::RoundToInt(Local.Y * 10.0) + Id.Y * 104729);
			if (bPalms)
			{
				// Lawns and parks have the tile's own palms; elsewhere half the crowns are coconut palms
				// (variants 8.7 to 15.3 m tall: the nearest to the measured height, scaled to it).
				const int32 MaskSize = SimCruiseGroundMask::Size;
				const int32 Mx = FMath::Clamp(FMath::FloorToInt(Local.X / Side * MaskSize), 0, MaskSize - 1);
				const int32 My = FMath::Clamp(FMath::FloorToInt(Local.Y / Side * MaskSize), 0, MaskSize - 1);
				if (Mask.Num() == MaskSize * MaskSize && Mask[My * MaskSize + Mx].R > 128) continue;
				if ((H >> 7) % 2 == 0)
				{
					const int32 Variants = PalmRings.Num();
					const int32 V = FMath::Clamp(FMath::RoundToInt((Row.HeightM - 8.0) / 8.0 * Variants - 0.5), 0, Variants - 1);
					const double Nominal = 8.0 + 8.0 * (V + 0.5) / Variants;
					PalmLists[V].Add(FTransform(FRotator(0.0, (H >> 10) % 360, 0.0), At, FVector(FMath::Clamp(Row.HeightM / Nominal, 0.6, 1.5))));
					++Placed;
					continue;
				}
			}
			const bool bPine = !bPalms && (H % 1000) < Conifers * 1000.0;
			// A conifer's crown is narrower than the canopy peak's footprint suggests; a segmented
			// crown is its own measured width (a conifer's mesh a little inside it).
			const double Narrow = Row.bShape ? (bPine ? 0.85 : 1.0) : (bPine ? 0.75 : 1.0);
			const double Long = FMath::Clamp(Fit.LongM * Narrow, 1.5, Row.HeightM * 0.95);
			const double Short = FMath::Clamp(Fit.ShortM * Narrow, 1.5, Row.HeightM * 0.95);
			// A round crown (within 15 %) keeps a random turn, so the meshes do not all face one way.
			const double Yaw = Row.bShape && Fit.LongM > 1.15 * Fit.ShortM ? Fit.YawDeg : (H >> 10) % 360;
			const FTransform Xf(FRotator(0.0, Yaw, 0.0), At, FVector(Long, Short, Row.HeightM));
			for (int32 Level = 0; Level < 3; ++Level)
			{
				Lists[(bPine ? 3 : 0) + Level].Add(Xf);
			}
			// Trunk about a sixtieth of the height across (Grove.gd row[4]), 8 to 45 cm radius.
			Feet.Add(FVector4(At.X, At.Y, At.Z, FMath::Clamp(Row.HeightM * 100.0 / 120.0, 8.0, 45.0)));
			++Placed;
		}
	}
	// Rebuild each ring's cluster tree now, not on a worker: with the async build the cleared
	// rings drew no trees until it finished, so every refresh (each canopy tile that arrives)
	// showed one frame of bare hills and a sunlit cabin (Tail of the Dragon pre-flight, the
	// Miata's "flash frames", 2026-11-13).
	for (int32 i = 0; i < 6; ++i)
	{
		Rings[i]->AddInstances(Lists[i], false, true, false);
		Rings[i]->BuildTreeIfOutdated(false, true);
	}
	for (int32 i = 0; i < PalmRings.Num(); ++i)
	{
		PalmRings[i]->AddInstances(PalmLists[i], false, true, false);
		PalmRings[i]->BuildTreeIfOutdated(false, true);
	}

	// Shrubs: each 34 m cell of the zoom-12 shrub map holds its cover fraction of crowns of its
	// height (crown about 2.2 times as wide as tall), at most sixteen drawn per cell.
	TArray<FTransform> ShrubList;
	if (ShrubRing)
	{
		ShrubRing->ClearInstances();
		for (const FIntPoint& Id : Shown)
		{
			const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
			const TArray<FColor>* Mask = ShrubMasks.Find(FIntPoint(Id.X >> 4, Id.Y >> 4));
			if (!Tile || !Tile->GetBuild() || !Mask || Mask->Num() != 256 * 256) continue;
			const FVector Origin = Tile->GetActorLocation();
			const double Px = Side / 16.0;
			TMultiMap<FIntPoint, FVector3d> RoadCells;
			for (const FSimCruiseRibbon& Ribbon : Tile->GetBuild()->Roads.Ribbons)
				for (const FSimCruiseRibbonStation& St : Ribbon.St)
					RoadCells.Add(FIntPoint(FMath::FloorToInt(St.C.X / 20.0), FMath::FloorToInt(St.C.Y / 20.0)), FVector3d(St.C.X, St.C.Y, St.HalfW));
			for (int32 j = 0; j < 16; ++j)
			for (int32 i = 0; i < 16; ++i)
			{
				const FColor C = (*Mask)[((Id.Y & 15) * 16 + j) * 256 + (Id.X & 15) * 16 + i];
				const double Cover = C.R / 100.0, HeightM = C.G / 100.0;
				if (Cover <= 0.03 || HeightM < 0.08) continue;
				const double Crown = FMath::Clamp(HeightM * 2.2, 0.5, 3.0);
				const int32 Count = FMath::Min(16, FMath::RoundToInt(Cover * Px * Px / (Crown * Crown) * 0.15));
				for (int32 n = 0; n < Count; ++n)
				{
					const uint32 H = Hash2(Id.X * 4099 + i * 131 + n, Id.Y * 7919 + j * 197 + n * 31);
					const FVector2D Local((i + (H & 1023) / 1024.0) * Px, (j + ((H >> 10) & 1023) / 1024.0) * Px);
					bool bOnRoad = false;
					const FIntPoint Cell(FMath::FloorToInt(Local.X / 20.0), FMath::FloorToInt(Local.Y / 20.0));
					for (int32 dx = -1; dx <= 1 && !bOnRoad; ++dx)
					for (int32 dy = -1; dy <= 1 && !bOnRoad; ++dy)
					{
						TArray<FVector3d> Points;
						RoadCells.MultiFind(Cell + FIntPoint(dx, dy), Points);
						for (const FVector3d& P : Points) if (FVector2D::Distance(Local, FVector2D(P.X, P.Y)) < P.Z + 2.0) { bOnRoad = true; break; }
					}
					if (bOnRoad) continue;
					FVector At = Origin + FVector(Local.X * 100.0, Local.Y * 100.0, 0.0);
					double Z = 0.0;
					if (!Stream->SurfaceHeightAt(At, Z)) continue;
					At.Z = Z - 5.0;
					const double Vary = 0.7 + ((H >> 20) & 255) / 255.0 * 0.6;
					ShrubList.Add(FTransform(FRotator(0.0, (H >> 3) % 360, 0.0), At, FVector(Crown * Vary, Crown * Vary, HeightM * Vary * 1.3)));
				}
			}
		}
		ShrubRing->AddInstances(ShrubList, false, true, false);
		ShrubRing->BuildTreeIfOutdated(false, true);
	}
	Shrubs = ShrubList.Num();
	UE_LOG(LogSimCruiseTrees, Display, TEXT("%d trees and %d shrubs on %d tiles"), Placed, Shrubs, Shown.Num());
}

void ASimCruiseTrees::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream || !Stream->GetFrame().IsValid()) return;
	if (!bMeshesTried && !BuildMeshes())
	{
		SetActorTickEnabled(false);
		return;
	}
	if (Rings.Num() != 6) return;
	if (Stream->GetTitle() != SeenTitle)
	{
		SeenTitle = Stream->GetTitle();
		Canopy.Reset();
		Fetches.Reset();
		RetryAt.Reset();
		Shown.Reset();
		for (UHierarchicalInstancedStaticMeshComponent* Ring : Rings) Ring->ClearInstances();
		Placed = 0;
	}
	const FSimCruiseWorldFrame& Frame = Stream->GetFrame();
	const double Now = FPlatformTime::Seconds();
	const FSimCruiseAtlasConfig Config = FSimCruiseAtlasConfig::ResolveDefault();

	// The tiles there are now; ask for the trees of each once (two at a time).
	TSet<FIntPoint> Present;
	int32 Running = 0;
	for (const TPair<FIntPoint, TSharedPtr<FFetch, ESPMode::ThreadSafe>>& Pair : Fetches) Running += Pair.Value->bDone ? 0 : 1;
	for (int32 dx = -3; dx <= 3; ++dx)
	for (int32 dy = -3; dy <= 3; ++dy)
	{
		const APlayerController* Player = GetWorld()->GetFirstPlayerController();
		const APawn* Car = Player ? Player->GetPawn() : nullptr;
		if (!Car) continue;
		const FIntPoint Id = Frame.TileAtWorldCm(Car->GetActorLocation()) + FIntPoint(dx, dy);
		const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
		if (!Tile || !Tile->GetBuild()) continue;
		Present.Add(Id);
		if (Canopy.Contains(Id) || Fetches.Contains(Id) || Running >= 2 || !Config.IsConfigured()) continue;
		if (const double* Retry = RetryAt.Find(Id); Retry && *Retry > Now) continue;
		double Lon = 0.0, Lat = 0.0;
		Frame.WorldCmToLonLat(Tile->GetActorLocation() + FVector(Frame.TileSizeCm() * 0.5, Frame.TileSizeCm() * 0.5, 0.0), Lon, Lat);
		TSharedPtr<FFetch, ESPMode::ThreadSafe> Job = MakeShared<FFetch, ESPMode::ThreadSafe>();
		Fetches.Add(Id, Job);
		++Running;
		Async(EAsyncExecution::ThreadPool, [Job, Config, Id]()
		{
			FSimCruiseAtlasRequest Request;
			Request.Command = TEXT("canopy");
			Request.Args = { FString::FromInt(Id.X), FString::FromInt(Id.Y) };
			// Crowns segmented from the canopy height grid (helper/crown-segments.mjs); the old
			// peak finder with -SimCruiseOldCrowns (for an A/B).
			static const bool bOldCrowns = FParse::Param(FCommandLine::Get(), TEXT("SimCruiseOldCrowns"));
			if (!bOldCrowns) Request.Args.Add(TEXT("--segment"));
			Request.TimeoutSeconds = 90.0;
			const FSimCruiseAtlasResult Result = FSimCruiseAtlasClient::RunBlocking(Config, Request, nullptr);
			if (Result.IsOk())
			{
				Job->Rows = ParseCanopy(Result.Json);
				Job->bOk = true;
			}
			else
			{
				Job->Why = Result.Describe();
			}
			Job->bDone = true;
		});
	}
	for (auto It = Fetches.CreateIterator(); It; ++It)
	{
		if (!It.Value()->bDone) continue;
		if (It.Value()->bOk)
		{
			Canopy.Add(It.Key(), MoveTemp(It.Value()->Rows));
		}
		else
		{
			UE_LOG(LogSimCruiseTrees, Warning, TEXT("Trees of tile %d_%d unavailable: %s"), It.Key().X, It.Key().Y, *It.Value()->Why);
			RetryAt.Add(It.Key(), Now + 60.0);
		}
		It.RemoveCurrent();
	}

	if (const APlayerController* Player = GetWorld()->GetFirstPlayerController(); Player && Player->GetPawn())
	{
		UpdateTrunks(Player->GetPawn()->GetActorLocation());
	}

	// Shrub maps for the districts under the tiles (one fetch at a time; outside the
	// helper's continental range the answer is "unsupported" and no shrubs are drawn).
	for (auto It = ShrubFetches.CreateIterator(); It; ++It)
	{
		if (!It.Value()->bDone) continue;
		ShrubMasks.Add(It.Key(), MoveTemp(It.Value()->Pixels));
		It.RemoveCurrent();
		ShownOrigin = FIntPoint(MAX_int32, MAX_int32); // place them
	}
	if (ShrubFetches.Num() == 0 && Config.IsConfigured())
	{
		for (const FIntPoint& Id : Present)
		{
			const FIntPoint District(Id.X >> 4, Id.Y >> 4);
			if (ShrubMasks.Contains(District)) continue;
			TSharedPtr<FShrubFetch, ESPMode::ThreadSafe> Job = MakeShared<FShrubFetch, ESPMode::ThreadSafe>();
			ShrubFetches.Add(District, Job);
			Async(EAsyncExecution::ThreadPool, [Job, Config, District]()
			{
				FSimCruiseAtlasRequest Request;
				Request.Command = TEXT("shrub-cover");
				Request.Args = { FString::FromInt(District.X), FString::FromInt(District.Y) };
				Request.TimeoutSeconds = 90.0;
				const FSimCruiseAtlasResult Result = FSimCruiseAtlasClient::RunBlocking(Config, Request, nullptr);
				FString Status, Png;
				const TSharedPtr<FJsonObject> Doc = Result.IsOk() && Result.Json->Type == EJson::Object ? Result.Json->AsObject() : nullptr;
				int32 W = 0, H = 0;
				if (Doc && Doc->TryGetStringField(TEXT("status"), Status) && Status == TEXT("available") && Doc->TryGetStringField(TEXT("png"), Png)
					&& USimCruiseWorldResources::DecodeImageFile(FPaths::Combine(Config.CacheDir, Png), Job->Pixels, W, H) && W == 256 && H == 256)
				{
				}
				else
				{
					Job->Pixels.Reset();
				}
				Job->bDone = true;
			});
			break;
		}
	}

	// Re-place when the tiles with trees change or the floating origin moves.
	TSet<FIntPoint> Ready;
	for (const FIntPoint& Id : Present) if (Canopy.Contains(Id)) Ready.Add(Id);
	if (Ready.Num() != Shown.Num() || !Ready.Includes(Shown) || Frame.OriginTile != ShownOrigin)
	{
		Shown = MoveTemp(Ready);
		ShownOrigin = Frame.OriginTile;
		Refresh();
	}
}

void ASimCruiseTrees::UpdateTrunks(const FVector& CarCm)
{
	if (FVector::Dist2D(CarCm, LastTrunkFocus) < 400.0) return;
	LastTrunkFocus = CarCm;
	TArray<TPair<double, int32>> Near;
	for (int32 i = 0; i < Feet.Num(); ++i)
	{
		const double D = FVector2D::DistSquared(FVector2D(Feet[i].X, Feet[i].Y), FVector2D(CarCm.X, CarCm.Y));
		if (D < 3500.0 * 3500.0) Near.Emplace(D, i);
	}
	Near.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
	for (int32 i = 0; i < Trunks.Num(); ++i)
	{
		UCapsuleComponent* Trunk = Trunks[i];
		if (!Near.IsValidIndex(i))
		{
			Trunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			continue;
		}
		const FVector4& F = Feet[Near[i].Value];
		const float Radius = static_cast<float>(F.W);
		Trunk->SetCapsuleSize(Radius, 250.f);
		Trunk->SetWorldLocation(FVector(F.X, F.Y, F.Z + 240.0));
		Trunk->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}
