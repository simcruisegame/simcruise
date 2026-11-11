// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseWorldTile.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
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
