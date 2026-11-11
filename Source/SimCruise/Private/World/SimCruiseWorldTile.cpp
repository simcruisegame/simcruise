// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseWorldTile.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "StaticMeshResources.h"
#include "Misc/App.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Presentation/SimCruiseCarAssets.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseWorldResources.h"

namespace
{
	TAutoConsoleVariable<int32> CVarParkedCars(TEXT("simcruise.World.ParkedCars"), 1,
		TEXT("1: parked cars in the parking lanes' stalls (drawn runs only); 0: empty stalls."));

	/**
	 * The cars that park: the bundled models with a fitted presentation. Mostly what a
	 * Miami street holds (SUVs, a pickup, a minivan, an electric sedan), and the classics
	 * and sports cars Ocean Drive is known for.
	 */
	const TCHAR* ParkedCarFor(uint8 Pick)
	{
		struct FShare { int32 UpTo; const TCHAR* Id; };
		static const FShare Shares[] = {
			{ 30, TEXT("nb1999") }, { 60, TEXT("mustang65") }, { 90, TEXT("meridian_z") }, { 130, TEXT("model3") },
			{ 170, TEXT("grandcherokee13") }, { 210, TEXT("f150raptor18") }, { 256, TEXT("odyssey18") },
		};
		for (const FShare& Share : Shares)
		{
			if (Pick < Share.UpTo) return Share.Id;
		}
		return TEXT("meridian_z");
	}

	/** Cars whose model could not be fitted: tried once per run, then a stand-in parks there. */
	TSet<FString> UnfitCars;

	/**
	 * Body colours in about the shares US cars have them (white 25 %, black 20 %, grey 18 %,
	 * silver 11 %, blue 9 %, red 9 %, then green, orange and yellow), sRGB.
	 */
	FLinearColor ParkedColourFor(uint8 Byte)
	{
		struct FShade { int32 UpTo; FColor Colour; };
		static const FShade Shades[] = {
			{ 64, FColor(232, 232, 226) },  // white
			{ 115, FColor(14, 14, 16) },    // black
			{ 161, FColor(88, 91, 95) },    // grey
			{ 189, FColor(167, 169, 171) }, // silver
			{ 212, FColor(22, 52, 112) },   // blue
			{ 235, FColor(150, 18, 22) },   // red
			{ 245, FColor(31, 74, 54) },    // green
			{ 251, FColor(205, 92, 20) },   // orange
			{ 256, FColor(226, 182, 30) },  // yellow
		};
		for (const FShade& Shade : Shades)
		{
			if (Byte < Shade.UpTo) return FLinearColor(Shade.Colour);
		}
		return FLinearColor(Shades[0].Colour);
	}
}

const TCHAR* SimCruiseParkedCars::CarFor(uint8 Pick)
{
	FString Id = ParkedCarFor(Pick);
	return UnfitCars.Contains(Id) ? TEXT("meridian_z") : ParkedCarFor(Pick);
}

FLinearColor SimCruiseParkedCars::ColourFor(uint8 Byte)
{
	return ParkedColourFor(Byte);
}

ASimCruiseWorldTile::ASimCruiseWorldTile()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable); // the floating origin moves tiles
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCastShadow(true);
	RootComponent = Mesh;
}

void ASimCruiseWorldTile::Install(const TSharedRef<FSimCruiseTileBuild>& InBuild, USimCruiseWorldResources* Resources, UTexture2D* Aerial, bool bAsyncCollision)
{
	Build = InBuild;
	TileId = InBuild->Tile;
	bCollisionReady = false;
	Mesh->bUseAsyncCooking = bAsyncCollision;
	Mesh->ClearAllMeshSections();

	// What is drawn: one static mesh of every layer (static draw path, so virtual shadow
	// pages stay cached; in editor builds a mesh distance field, so Lumen sees the street in
	// reflections and shaded sides). The procedural mesh keeps only the collision and is
	// hidden once the static mesh is built. Headless runs draw nothing and skip it.
	if (RenderMesh)
	{
		RenderMesh->DestroyComponent();
		RenderMesh = nullptr;
	}
	RenderSlots.Reset();
	if (Resources && InBuild->RenderData)
	{
		// The worker filled the buffers: here they only become a mesh and go to the GPU.
		UStaticMesh* Static = NewObject<UStaticMesh>(this, *FString::Printf(TEXT("SM_SimCruiseTile_%d_%d"), TileId.X, TileId.Y), RF_Transient);
		for (int32 Slot = 0; Slot < InBuild->RenderSlots.Num(); ++Slot)
		{
			Static->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), *FString::Printf(TEXT("Layer%d"), InBuild->RenderSlots[Slot])));
		}
		Static->SetRenderData(MoveTemp(InBuild->RenderData));
		Static->InitResources();
		Static->CalculateExtendedBounds();
		RenderSlots = InBuild->RenderSlots;
		RenderMesh = NewObject<UStaticMeshComponent>(this, TEXT("RenderMesh"));
		RenderMesh->SetMobility(EComponentMobility::Movable);
		RenderMesh->SetupAttachment(Mesh);
		RenderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RenderMesh->SetCastShadow(true);
		RenderMesh->SetStaticMesh(Static);
		RenderMesh->RegisterComponent();
		AddInstanceComponent(RenderMesh);
		// Drawn by the static mesh: the procedural mesh keeps only the collision.
		Mesh->SetVisibility(false, false);
	}

	const TArray<FVector2D> NoUV;
	for (int32 i = 0; i < static_cast<int32>(ESimCruiseTileLayer::Count); ++i)
	{
		FSimCruiseMeshData& Layer = InBuild->Layers[i];
		if (Layer.IsEmpty())
		{
			continue;
		}
		// A tangent square to every normal: the default (1, 0, 0) degenerates on walls and
		// curb faces that look along X, and the shading frame (and the normal) collapses.
		TArray<FProcMeshTangent> Tangents;
		Tangents.SetNumUninitialized(Layer.Normals.Num());
		for (int32 v = 0; v < Layer.Normals.Num(); ++v)
		{
			const FVector& N = Layer.Normals[v];
			const FVector T = FMath::Abs(N.Z) < 0.9 ? FVector::CrossProduct(FVector::UpVector, N).GetSafeNormal() : FVector::CrossProduct(N, FVector::RightVector).GetSafeNormal() * -1.0;
			Tangents[v] = FProcMeshTangent(T.IsNearlyZero() ? FVector::ForwardVector : T, false);
		}
		const ESimCruiseTileLayer Kind = static_cast<ESimCruiseTileLayer>(i);
		const bool bCollision = Kind == ESimCruiseTileLayer::Terrain || Kind == ESimCruiseTileLayer::Asphalt || Kind == ESimCruiseTileLayer::Concrete
			|| Kind == ESimCruiseTileLayer::Building || Kind == ESimCruiseTileLayer::Props;
		if (RenderMesh && !bCollision)
		{
			// Drawn by the static mesh only.
			Layer.Positions.Empty(); Layer.Normals.Empty(); Layer.Colors.Empty(); Layer.Indices.Empty(); Layer.UV0.Empty(); Layer.UV1.Empty();
			continue;
		}
		const bool bUVs = Layer.UV0.Num() == Layer.Positions.Num() && Layer.UV1.Num() == Layer.Positions.Num();
		Mesh->CreateMeshSection_LinearColor(i, Layer.Positions, Layer.Indices, Layer.Normals, bUVs ? Layer.UV0 : NoUV, bUVs ? Layer.UV1 : NoUV, NoUV, NoUV,
			Layer.Colors, Tangents, bCollision, false);
		// The game thread copy is gone once the section owns it.
		Layer.Positions.Empty();
		Layer.Normals.Empty();
		Layer.Colors.Empty();
		Layer.Indices.Empty();
		Layer.UV0.Empty();
		Layer.UV1.Empty();
	}

	// Parked cars wait for PlaceParkedCars (drawn runs only: they are scenery and obstacles).
	for (AActor* Old : ParkedCars)
	{
		if (Old) Old->Destroy();
	}
	ParkedCars.Reset();
	ParkedWaiting.Reset();
	if (Resources && CVarParkedCars.GetValueOnGameThread() != 0)
	{
		ParkedWaiting = MoveTemp(InBuild->ParkedCars);
	}
	InBuild->ParkedCars.Empty();

	// Palms: one instanced component per variant on this tile.
	for (UInstancedStaticMeshComponent* Old : PalmComponents)
	{
		if (Old) Old->DestroyComponent();
	}
	PalmComponents.Reset();
	if (Resources && !InBuild->PalmInstances.IsEmpty())
	{
		for (int32 Variant = 0; Variant < Resources->PalmMeshes.Num(); ++Variant)
		{
			TArray<FTransform> Transforms;
			for (const FSimCruisePalmInstance& Palm : InBuild->PalmInstances)
			{
				if (Palm.Variant != Variant) continue;
				Transforms.Add(FTransform(FRotator(0.0, Palm.YawDeg, 0.0), Palm.LocationCm, FVector(Palm.Scale)));
			}
			if (Transforms.IsEmpty() || !Resources->PalmMeshes[Variant]) continue;
			UInstancedStaticMeshComponent* Palms = NewObject<UInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Palms%d"), Variant));
			Palms->SetMobility(EComponentMobility::Movable);
			Palms->SetupAttachment(Mesh);
			Palms->SetStaticMesh(Resources->PalmMeshes[Variant]);
			Palms->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Palms->SetCastShadow(true);
			Palms->RegisterComponent();
			Palms->AddInstances(Transforms, false, false);
			AddInstanceComponent(Palms);
			PalmComponents.Add(Palms);
		}
		InBuild->PalmInstances.Empty();
	}

	if (Resources)
	{
		MaskTexture = USimCruiseWorldResources::CreateTexture(this, NAME_None, InBuild->Mask, SimCruiseGroundMask::Size, SimCruiseGroundMask::Size, true, false);
		InBuild->Mask.Empty();
		const FVector2d Anchor = SimCruiseTileBuilder::TextureAnchorM(TileId, InBuild->TileSizeM);
		const FIntPoint AerialTile = SimCruiseGeo::AerialTileOf(TileId);
		const FLinearColor Cell(TileId.X - AerialTile.X * 4, TileId.Y - AerialTile.Y * 4, Aerial ? 1.f : 0.f, 0.f);
		Materials.Reset();
		for (int32 i = 0; i < static_cast<int32>(ESimCruiseTileLayer::Count); ++i)
		{
			UMaterialInterface* Base = Resources->GetLayerMaterial(static_cast<ESimCruiseTileLayer>(i));
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetVectorParameterValue(TEXT("Anchor"), FLinearColor(Anchor.X, Anchor.Y, 0.f, 0.f));
			MID->SetScalarParameterValue(TEXT("TileSizeCm"), InBuild->TileSizeM * 100.0);
			MID->SetVectorParameterValue(TEXT("Cell"), Cell);
			MID->SetTextureParameterValue(TEXT("Mask"), MaskTexture);
			MID->SetTextureParameterValue(TEXT("Aerial"), Aerial ? Aerial : Resources->BlankTexture.Get());
			MID->SetTextureParameterValue(TEXT("Grass"), Resources->GrassTexture);
			MID->SetTextureParameterValue(TEXT("Sand"), Resources->SandTexture);
			MID->SetTextureParameterValue(TEXT("Grain"), Resources->AsphaltTexture);
			MID->SetTextureParameterValue(TEXT("Asphalt"), Resources->AsphaltTexture);
			MID->SetVectorParameterValue(TEXT("GrassMean"), Resources->GrassMean);
			MID->SetVectorParameterValue(TEXT("SandMean"), Resources->SandMean);
			MID->SetVectorParameterValue(TEXT("GrainMean"), Resources->AsphaltMean);
			MID->SetVectorParameterValue(TEXT("AsphaltMean"), Resources->AsphaltMean);
			MID->SetScalarParameterValue(TEXT("Lights"), LightsOn);
			Materials.Add(MID);
			Mesh->SetMaterial(i, MID);
			const int32 Slot = RenderSlots.IndexOfByKey(i);
			if (RenderMesh && Slot != INDEX_NONE)
			{
				RenderMesh->SetMaterial(Slot, MID);
			}
		}
	}
}

void ASimCruiseWorldTile::SetLightsOn(float Amount)
{
	if (FMath::Abs(Amount - LightsOn) < 0.01f)
	{
		return;
	}
	LightsOn = Amount;
	for (UMaterialInstanceDynamic* MID : Materials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Lights"), Amount);
		}
	}
}

double ASimCruiseWorldTile::SurfaceHeightAt(const FVector2d& TileMetres) const
{
	if (!Build.IsValid())
	{
		return 0.0;
	}
	FSimCruiseRoadHit Hit;
	if (Build->Roads.FindCarriageway(TileMetres, Hit))
	{
		return Hit.Height;
	}
	return Build->TerrainHeightAt(TileMetres);
}

bool ASimCruiseWorldTile::IsCollisionReady() const
{
	if (bCollisionReady || !Build.IsValid() || !Mesh)
	{
		return bCollisionReady;
	}
	const double Half = Build->TileSizeM * 50.0;
	const FVector Centre = GetActorLocation() + FVector(Half, Half, 0.0);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruiseTileCollisionReady), true);
	// +-10 km covers every US elevation; a hit means the cooked triangles are in the scene.
	bCollisionReady = Mesh->LineTraceComponent(Hit, Centre + FVector(0.0, 0.0, 1.0e6), Centre - FVector(0.0, 0.0, 1.0e6), Params);
	return bCollisionReady;
}

int32 ASimCruiseWorldTile::PlaceParkedCars(int32 Budget)
{
	int32 Placed = 0;
	UWorld* World = GetWorld();
	while (Placed < Budget && !ParkedWaiting.IsEmpty() && World)
	{
		const FSimCruiseParkedCar Stall = ParkedWaiting.Pop(EAllowShrinking::No);
		FString CarId = ParkedCarFor(Stall.Pick);
		if (UnfitCars.Contains(CarId)) CarId = TEXT("meridian_z");
		FString Error;
		TSharedPtr<FSimCruiseBuiltCar> Built = SimCruiseCarAssets::Get(CarId, Error);
		if (!Built.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("SimCruise parked car %s: %s"), *CarId, *Error);
			UnfitCars.Add(CarId);
			CarId = TEXT("meridian_z");
			Built = SimCruiseCarAssets::Get(CarId, Error);
		}
		if (!Built.IsValid() || !Built->Body.Mesh.IsValid())
		{
			continue;
		}
		// The car's frame has its origin between the axles at the body's height; the tyres
		// touch the ground a tyre radius under the hubs.
		const double GroundInFrameCm = Built->HubsCm[0].Z - Built->TyreRadiusCm;
		const FVector Location = GetActorTransform().TransformPosition(Stall.LocationCm - FVector(0.0, 0.0, GroundInFrameCm));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		ASimCruiseCarPresentationActor* Car = World->SpawnActor<ASimCruiseCarPresentationActor>(Location, FRotator(0.0, Stall.YawDeg, 0.0), Params);
		if (!Car)
		{
			continue;
		}
		if (!Car->Presentation->InitializeForCar(CarId))
		{
			Car->Destroy();
			continue;
		}
		Car->Presentation->SetPaintColor(ParkedColourFor(Stall.Colour));
		Car->Presentation->SetParked();
		// Solid: a box round the body.
		const FBox Body = Built->Body.Mesh->GetBoundingBox().TransformBy(Built->Body.PartToVehicle);
		UBoxComponent* Box = NewObject<UBoxComponent>(Car, TEXT("Collision"));
		Box->SetupAttachment(Car->Presentation);
		Box->SetRelativeLocation(Body.GetCenter());
		Box->SetBoxExtent(Body.GetExtent());
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Movable);
		Box->RegisterComponent();
		Car->AddInstanceComponent(Box);
		Car->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		ParkedCars.Add(Car);
		++Placed;
		if (ParkedWaiting.IsEmpty())
		{
			UE_LOG(LogTemp, Display, TEXT("SimCruise tile %d_%d: %d parked cars"), TileId.X, TileId.Y, ParkedCars.Num());
		}
	}
	return Placed;
}

void ASimCruiseWorldTile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AActor* Car : ParkedCars)
	{
		if (Car) Car->Destroy();
	}
	ParkedCars.Reset();
	ParkedWaiting.Reset();
	Super::EndPlay(EndPlayReason);
}
