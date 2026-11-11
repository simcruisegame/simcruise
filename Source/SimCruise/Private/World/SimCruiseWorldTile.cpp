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
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseWorldResources.h"

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
