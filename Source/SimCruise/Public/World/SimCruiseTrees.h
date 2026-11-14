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
	/**
	 * The crown's segmented shape (helper/crown-segments.mjs, `canopy --segment`), when there is
	 * one: its reach from the top in 8 directions (east first, anticlockwise, metres) and the
	 * crown base height. bShape false: a round crown of CrownM.
	 */
	bool bShape = false;
	float RadiiM[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	float CrownBaseM = 0.f;
};

/** A crown grown to its segmented shape: the ellipse the 8 reaches fit. */
struct FSimCruiseCrownFit
{
	/** Where the crown's middle is, from the top (metres, x east, y south: tile axes). */
	FVector2D OffsetM = FVector2D::ZeroVector;
	/** The ellipse's long and short widths (metres) and the long axis' yaw (degrees, tile axes: from x towards y). */
	double LongM = 0.0, ShortM = 0.0, YawDeg = 0.0;
};

/**
 * Trees anywhere (godot-final:game/Grove.gd): every tree the WRI / Meta canopy
 * height map v2 sees round the car (the atlas helper's `canopy x y`: one crown
 * per local height peak, with its height and crown width), drawn with the
 * Godot client's broadleaf and conifer meshes at three detail levels. Trees
 * on roads are left out; the species follows the region (conifers in the
 * north, the western mountains and the Pacific Northwest, mixed pine in the
 * south-east). Where coconut palms grow (South Florida, Hawaii) half of the trees
 * off the lawns are palms (the lawns get SimCruiseTileDetail's). The 32
 * trees nearest the car have solid trunks.
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
	/** The ellipse a segmented crown's 8 reaches fit (its middle, widths and long axis). */
	static FSimCruiseCrownFit FitCrown(const FSimCruiseTreeRow& Row);

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

	/** Coconut palms for the measured trees where palms grow (the shared palm variants of USimCruiseWorldResources). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> PalmRings;

	/** Low shrubs (sagebrush, rangeland scrub) from the USDA RAP shrub cover and RCMAP shrub height. */
	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ShrubRing;
	struct FShrubFetch;
	/** Zoom-12 districts: 256 x 256 px, R cover %, G height cm (the helper's shrub-cover). */
	TMap<FIntPoint, TArray<FColor>> ShrubMasks;
	TMap<FIntPoint, TSharedPtr<FShrubFetch, ESPMode::ThreadSafe>> ShrubFetches;
	int32 Shrubs = 0;

	/** Solid trunks for the nearest trees (Grove.gd: 32 pooled trunk bodies). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<class UCapsuleComponent>> Trunks;

	/** Every placed tree's foot (world cm) and trunk radius (cm), for the trunk pool. */
	TArray<FVector4> Feet;
	FVector LastTrunkFocus = FVector(1e30);
	void UpdateTrunks(const FVector& CarCm);

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
