// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseRegionalTerrain.generated.h"

class ASimCruiseWorldStreamer;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
class UTexture2D;

/**
 * The land to the horizon: mountains, buttes, ridges and valleys 45 km round
 * the car, past the streamed driving tiles (godot-final:game/RegionalTerrain.gd).
 *
 * The atlas helper's `landscape <rx> <ry>` command (helper/regional-terrain.mjs)
 * builds a simplified triangle mesh of USGS 3DEP elevation (about 20 m
 * tolerance) for a region of 8 x 8 zoom-16 tiles and a 512 px aerial
 * photograph over the 90 km square, plus a 1536 px photograph over the middle
 * 13 km. Display only: no collision. The material sinks the mesh 65 m under
 * the driving tiles near the camera and bends it with the Earth's curvature,
 * as the Godot shader did; the height fog gives the haze.
 */
UCLASS(NotPlaceable, Transient)
class SIMCRUISE_API ASimCruiseRegionalTerrain : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseRegionalTerrain();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	TWeakObjectPtr<ASimCruiseWorldStreamer> Streamer;

	/** Region shown now (zoom-16 tile / 8), or (-1,-1). */
	FIntPoint GetShownRegion() const { return Shown; }
	int32 NumTriangles() const { return Triangles; }

	/** The material (built from code in editor builds; saved by the SimCruiseBuildWorldMaterials commandlet for packaged ones). */
	static UMaterial* BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags);
	static FString MaterialPath();

	/** Radius of the helper's landscape (RegionalTerrain.gd RADIUS_M). */
	static constexpr double RadiusM = 45000.0;

	/**
	 * Heights no land has: a vertex whose height is further from the median of the vertices
	 * within 600 m (more where the mesh is coarse) than max(100 m, 4 x their interquartile
	 * range), with under 15 % of them within that limit of its own height (a plateau's corner
	 * has its own ground round it), takes that median (up to three passes, so a ripple's
	 * shoulders go too). Where the
	 * neighbourhood is that flat, a vertex 100 m off it would stand on a 50 degree wall to its
	 * 88 m neighbours. A corrupt elevation source pixel otherwise rings into a spike of hundreds
	 * of metres in flat country or at sea
	 * (Stonington, Maine: a 1438 m peak beside a -314 m pit on a coast at sea level). Real
	 * mountains and cliffs keep their relief: their neighbourhood's spread raises the limit.
	 * Positions in cm (x, y across, z up). Returns the number of vertices changed.
	 */
	static int32 RejectHeightOutliers(TArray<FVector>& PositionsCm);

private:
	struct FJob;
	void StartJob(const FIntPoint& Region);
	void Install(FJob& Job);

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Textures;

	TSharedPtr<FJob, ESPMode::ThreadSafe> Job;
	FIntPoint Requested = FIntPoint(-1, -1);
	FIntPoint Shown = FIntPoint(-1, -1);
	FIntPoint Failed = FIntPoint(-1, -1);
	double RetryAt = 0.0;
	double NextPoll = 0.0;
	int32 Triangles = 0;
	/** The snow mask of the region shown (256 px over the 90 km square), for the ground of the driving tiles. */
	TArray<FColor> SnowMask;
	int32 SnowMaskSize = 0;
	double NextSnowGround = 0.0;
	void ApplySnowToTiles();
	uint32 SeenGeneration = 0;
	FString SeenTitle;
};
