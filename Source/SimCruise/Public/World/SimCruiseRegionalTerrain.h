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
