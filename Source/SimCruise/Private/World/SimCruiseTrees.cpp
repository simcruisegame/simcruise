// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTrees.h"

#include "Async/Async.h"
#include "Atlas/SimCruiseAtlasClient.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
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
	const TCHAR* LeafCode = TEXT(R"HLSL(
float4 t = Texture2DSample(Foliage, FoliageSampler, UV);
Mask = t.a;
float3 c = t.rgb * Tint.rgb;
Sss = c * float3(0.7, 1.0, 0.4);
Roughness = 0.8;
return c;
)HLSL");

	const TCHAR* BarkCode = TEXT(R"HLSL(
float3 b = Texture2DSample(Bark, BarkSampler, UV * float2(2.0, 6.0)).rgb;
Roughness = 0.95;
return b;
)HLSL");

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

	UMaterialExpression* TextureParam(UMaterial* Material, const TCHAR* Name)
	{
		UMaterialExpressionTextureObjectParameter* P = AddExpression<UMaterialExpressionTextureObjectParameter>(Material);
		P->ParameterName = Name;
		P->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		P->SamplerType = SAMPLERTYPE_Color;
		return P;
	}

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

	uint32 Hash2(int32 A, int32 B)
	{
		uint32 H = static_cast<uint32>(A) * 73856093u ^ static_cast<uint32>(B) * 19349663u;
		H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
		return H;
	}
}

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
	Custom->Code = LeafCode;
	Custom->AdditionalOutputs.Add({ FName(TEXT("Mask")), CMOT_Float1 });
	Custom->AdditionalOutputs.Add({ FName(TEXT("Sss")), CMOT_Float3 });
	Custom->AdditionalOutputs.Add({ FName(TEXT("Roughness")), CMOT_Float1 });
	AddInput(Custom, TEXT("UV"), AddExpression<UMaterialExpressionTextureCoordinate>(Material));
	AddInput(Custom, TEXT("Foliage"), TextureParam(Material, TEXT("Foliage")));
	UMaterialExpressionVectorParameter* Tint = AddExpression<UMaterialExpressionVectorParameter>(Material);
	Tint->ParameterName = TEXT("Tint");
	Tint->DefaultValue = FLinearColor(0.62f, 0.68f, 0.55f);
	AddInput(Custom, TEXT("Tint"), Tint);
	Custom->RebuildOutputs();
	Material->GetEditorOnlyData()->BaseColor.Connect(0, Custom);
	Material->GetEditorOnlyData()->OpacityMask.Connect(1, Custom);
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
	Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
	UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material);
	Custom->Inputs.Reset();
	Custom->OutputType = CMOT_Float3;
	Custom->Code = BarkCode;
	Custom->AdditionalOutputs.Add({ FName(TEXT("Roughness")), CMOT_Float1 });
	AddInput(Custom, TEXT("UV"), AddExpression<UMaterialExpressionTextureCoordinate>(Material));
	AddInput(Custom, TEXT("Bark"), TextureParam(Material, TEXT("Bark")));
	Custom->RebuildOutputs();
	Material->GetEditorOnlyData()->BaseColor.Connect(0, Custom);
	Material->GetEditorOnlyData()->Roughness.Connect(1, Custom);
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
	for (const TSharedPtr<FJsonValue>& T : *Trees)
	{
		const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
		if (!T.IsValid() || !T->TryGetArray(R) || R->Num() < 4) continue;
		FSimCruiseTreeRow Row;
		Row.TileUV = FVector2D((*R)[0]->AsNumber(), (*R)[1]->AsNumber());
		Row.HeightM = (*R)[2]->AsNumber();
		Row.CrownM = (*R)[3]->AsNumber();
		// Peaks under 3 m are shrubs; the map tops out near 60 m.
		if (Row.HeightM < 3.0 || Row.HeightM > 70.0 || Row.CrownM <= 0.0) continue;
		if (Row.TileUV.X < 0.0 || Row.TileUV.Y < 0.0 || Row.TileUV.X > 1.0 || Row.TileUV.Y > 1.0) continue;
		Rows.Add(Row);
	}
	return Rows;
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
	auto Texture = [this](const TCHAR* File) -> UTexture2D*
	{
		TArray<FColor> Pixels;
		int32 W = 0, H = 0;
		if (!USimCruiseWorldResources::DecodeImageFile(TreeArt(File), Pixels, W, H)) return nullptr;
		UTexture2D* T = USimCruiseWorldResources::CreateTexture(this, FName(*FString::Printf(TEXT("T_SimCruiseTree_%s"), *FPaths::GetBaseFilename(File))), Pixels, W, H, true, true);
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
			Parts.SetNum(Slots.Num());
			TArray<const FSimCruiseMeshData*> Ptrs;
			for (const FSimCruiseMeshData& P : Parts) Ptrs.Add(&P);
			UStaticMesh* Mesh = SimCruiseWorldMaterials::BuildRuntimeMesh(*FString::Printf(TEXT("SM_SimCruiseTree_%s%d"), Species[S], Level), Ptrs, true, false);
			if (!Mesh) return false;
			TArray<FStaticMaterial>& StaticSlots = Mesh->GetStaticMaterials();
			StaticSlots.SetNum(Slots.Num());
			for (int32 i = 0; i < Slots.Num(); ++i) StaticSlots[i].MaterialInterface = Slots[i];
			Meshes.Add(Mesh);

			UHierarchicalInstancedStaticMeshComponent* Ring = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
			Ring->SetStaticMesh(Mesh);
			Ring->SetMobility(EComponentMobility::Movable);
			Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ring->SetupAttachment(RootComponent);
			// The crowns' detail by distance (Grove.gd: 33 m, 92 m, 485 m).
			static const int32 Near[3] = { 0, 3300, 9200 };
			static const int32 Far[3] = { 3300, 9200, 60000 };
			Ring->SetCullDistances(Near[Level], Far[Level]);
			Ring->SetCastShadow(Level < 2);
			Ring->bAffectDistanceFieldLighting = false;
			Ring->RegisterComponent();
			Rings.Add(Ring);
		}
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
	Placed = 0;
	for (const FIntPoint& Id : Shown)
	{
		const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
		const TArray<FSimCruiseTreeRow>* Rows = Canopy.Find(Id);
		if (!Tile || !Rows || !Tile->GetBuild()) continue;
		const FVector Origin = Tile->GetActorLocation();
		double Lon = 0.0, Lat = 0.0;
		Frame.WorldCmToLonLat(Origin, Lon, Lat);
		const double Conifers = ConiferShare(Lat, Lon, Tile->GetBuild()->MinHeightM);

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
			const FVector2D Local = Row.TileUV * Side;
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
			const bool bPine = (H % 1000) < Conifers * 1000.0;
			const double Yaw = (H >> 10) % 360;
			// A conifer's crown is narrower than the canopy peak's footprint suggests.
			const double Width = FMath::Clamp(Row.CrownM * (bPine ? 0.75 : 1.0), 1.5, Row.HeightM * 0.95);
			const FTransform Xf(FRotator(0.0, Yaw, 0.0), At, FVector(Width, Width, Row.HeightM));
			for (int32 Level = 0; Level < 3; ++Level)
			{
				Lists[(bPine ? 3 : 0) + Level].Add(Xf);
			}
			++Placed;
		}
	}
	for (int32 i = 0; i < 6; ++i)
	{
		Rings[i]->AddInstances(Lists[i], false, true, false);
	}
	UE_LOG(LogSimCruiseTrees, Display, TEXT("%d trees on %d tiles"), Placed, Shown.Num());
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
		if (SimCruiseTileDetail::IsCoconutPalmRegion(Lat, Lon))
		{
			Canopy.Add(Id); // palms there (SimCruiseTileDetail)
			continue;
		}
		TSharedPtr<FFetch, ESPMode::ThreadSafe> Job = MakeShared<FFetch, ESPMode::ThreadSafe>();
		Fetches.Add(Id, Job);
		++Running;
		Async(EAsyncExecution::ThreadPool, [Job, Config, Id]()
		{
			FSimCruiseAtlasRequest Request;
			Request.Command = TEXT("canopy");
			Request.Args = { FString::FromInt(Id.X), FString::FromInt(Id.Y) };
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
