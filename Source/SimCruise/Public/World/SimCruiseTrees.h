// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseTrees.generated.h"

class ASimCruiseWorldStreamer;
class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTexture2D;

/** One measured tree: where it stands in its tile, how tall and how wide its crown is. */
struct FSimCruiseTreeRow
{
	FVector2D TileUV = FVector2D::ZeroVector;
	double HeightM = 0.0;
	double CrownM = 0.0;
};

/**
 * Trees anywhere (godot-final:game/Grove.gd): every tree the WRI / Meta canopy
 * height map v2 sees round the car (the atlas helper's `canopy x y`: one crown
 * per local height peak, with its height and crown width), drawn with the
 * Godot client's broadleaf and conifer meshes at three detail levels. Trees
 * on roads are left out; the species follows the region (conifers in the
 * north, the western mountains and the Pacific Northwest, mixed pine in the
 * south-east). South Florida keeps its palms (SimCruiseTileDetail). Display
 * only: no collision yet.
 */
UCLASS(NotPlaceable, Transient)
class SIMCRUISE_API ASimCruiseTrees : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseTrees();

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	TWeakObjectPtr<ASimCruiseWorldStreamer> Streamer;

	int32 NumTrees() const { return Placed; }

	/** The leaf and bark materials (built from code in editor builds; saved by the commandlet for packaged ones). */
	static UMaterial* BuildLeafMaterial(UObject* Outer, FName Name, EObjectFlags Flags);
	static UMaterial* BuildBarkMaterial(UObject* Outer, FName Name, EObjectFlags Flags);
	static FString LeafMaterialPath();
	static FString BarkMaterialPath();

	/** The helper's canopy document: trees [[u, v, height m, crown m], ...]. */
	static TArray<FSimCruiseTreeRow> ParseCanopy(const TSharedPtr<class FJsonValue>& Json);

	/** Share of conifers at a place (0..1). */
	static double ConiferShare(double LatDeg, double LonDeg, double ElevationM);

private:
	struct FFetch;
	bool BuildMeshes();
	void Refresh();

	/** [species * 3 + level]: 0..2 broadleaf, 3..5 conifer. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> Rings;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> Materials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> Textures;

	TMap<FIntPoint, TArray<FSimCruiseTreeRow>> Canopy;
	TMap<FIntPoint, TSharedPtr<FFetch, ESPMode::ThreadSafe>> Fetches;
	TMap<FIntPoint, double> RetryAt;
	TSet<FIntPoint> Shown;
	FIntPoint ShownOrigin = FIntPoint(MAX_int32, MAX_int32);
	FString SeenTitle;
	bool bMeshesTried = false;
	bool bDirty = false;
	int32 Placed = 0;
};
