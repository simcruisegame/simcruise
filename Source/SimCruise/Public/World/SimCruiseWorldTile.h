// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SimCruiseTileBuilder.h"
#include "SimCruiseWorldTile.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class USimCruiseWorldResources;

/**
 * One streamed zoom-16 tile: terrain, road ribbons, curbs, markings and water
 * in one procedural mesh (one section per layer). Terrain, asphalt and
 * concrete carry collision (complex as simple), so the car's wheels meet the
 * same triangles that are drawn. The actor sits at the tile's north-west
 * corner in the drive's local frame.
 */
UCLASS(NotPlaceable, Transient)
class SIMCRUISE_API ASimCruiseWorldTile : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseWorldTile();

	/** Create the mesh sections, collision and per-tile materials (game thread). */
	void Install(const TSharedRef<FSimCruiseTileBuild>& InBuild, USimCruiseWorldResources* Resources, UTexture2D* Aerial, bool bAsyncCollision);

	FIntPoint GetTileId() const { return TileId; }
	const FSimCruiseTileBuild* GetBuild() const { return Build.Get(); }

	/** Drivable surface height (m) at tile metres: the carriageway where there is one, else the ground. */
	double SurfaceHeightAt(const FVector2d& TileMetres) const;

	/**
	 * The physics scene holds the tile's collision. With asynchronous cooking a
	 * tile is drawn a few frames before its triangles can be hit; a car placed
	 * in that window would fall through. Probed with a trace through the tile
	 * centre (terrain covers the whole tile), remembered once true.
	 */
	bool IsCollisionReady() const;

	/** Night lighting of the tile's materials, 0..1: lit windows and street-light lenses (neon brightens). */
	void SetLightsOn(float Amount);
	float GetLightsOn() const { return LightsOn; }

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|World")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

private:
	FIntPoint TileId = FIntPoint::ZeroValue;
	TSharedPtr<FSimCruiseTileBuild> Build;
	mutable bool bCollisionReady = false;
	float LightsOn = 0.f;
};
