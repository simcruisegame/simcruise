// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SimCruiseTileBuilder.h"
#include "SimCruiseWorldTile.generated.h"

class UProceduralMeshComponent;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
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
/** The cars that park (and drive): which model a random byte picks, and which body colour. */
namespace SimCruiseParkedCars
{
	SIMCRUISE_API const TCHAR* CarFor(uint8 Pick);
	/** Every model a parked or traffic car can be (to build them all before the drive). */
	SIMCRUISE_API TArray<FString> AllCarIds();
	SIMCRUISE_API FLinearColor ColourFor(uint8 Byte);
}

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
	/** The named places' lettering on this tile. */
	const TArray<TObjectPtr<UTextRenderComponent>>& GetSigns() const { return SignComponents; }

	/** Drivable surface height (m) at tile metres: the carriageway where there is one, else the ground. */
	double SurfaceHeightAt(const FVector2d& TileMetres) const;

	/**
	 * The physics scene holds the tile's collision. With asynchronous cooking a
	 * tile is drawn a few frames before its triangles can be hit; a car placed
	 * in that window would fall through. Probed with a trace through the tile
	 * centre (terrain covers the whole tile), remembered once true.
	 */
	bool IsCollisionReady() const;

	/**
	 * Places up to Budget of the tile's waiting parked cars (game thread, a few a frame so
	 * a tile streaming in does not hitch); returns how many it placed.
	 */
	int32 PlaceParkedCars(int32 Budget);

	/**
	 * The rest of the install, spread over frames: the ground mask, one collision section at
	 * a time, the palms, the signs. Runs steps until BudgetMs is spent (at least one); returns
	 * the milliseconds used. The tile is not collision-ready until its steps are done.
	 */
	double ContinueInstall(double BudgetMs);
	bool HasPendingInstall() const { return PendingSteps.Num() > 0; }

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Dry grip of the surface under a collision face, as a multiplier on dry asphalt
	 * (FHitResult::FaceIndex of a trace with bReturnFaceIndex), and how much rain takes
	 * from it when soaked (0..1). Safe to call from the physics thread.
	 */
	void SurfaceGripAtFace(int32 FaceIndex, float& OutDryGrip, float& OutWetLoss) const;

	/** The layer of a collision face (FHitResult::FaceIndex of a complex trace on Mesh); Count when it is not known. */
	ESimCruiseTileLayer LayerAtFace(int32 FaceIndex) const;

	/** Night lighting of the tile's materials, 0..1: lit windows and street-light lenses (neon brightens). */
	void SetLightsOn(float Amount);
	float GetLightsOn() const { return LightsOn; }

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|World")
	TObjectPtr<UProceduralMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> MaskTexture;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	/** What is drawn: the tile's layers as one static mesh (null in headless runs). */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> RenderMesh;

	/** The tile's palms, one instanced component per shared palm mesh. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> PalmComponents;

	/** The tile's parked cars (attached, so they move with the floating origin). */
	/** The named places' lettering (FSimCruiseTileBuild::Signs). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> SignComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> ParkedCars;

private:
	FIntPoint TileId = FIntPoint::ZeroValue;
	TSharedPtr<FSimCruiseTileBuild> Build;
	mutable bool bCollisionReady = false;
	float LightsOn = 0.f;
	TArray<FSimCruiseParkedCar> ParkedWaiting;
	/** Static mesh slot -> tile layer. */
	TArray<int32> RenderSlots;
	/** Install steps not run yet, in order (ContinueInstall). */
	TArray<TFunction<void()>> PendingSteps;
	/** Mesh sections that get collision once they are all in. */
	TArray<int32> CollisionSections;

	/** Collision faces in section order: where each layer's faces start, and which asphalt faces are gravel. */
	struct FSurfaceMap
	{
		TArray<int32> FirstFace;
		TArray<uint8> Layer;
		int32 AsphaltFirstFace = -1;
		TBitArray<> GravelAsphalt;
	};
	TSharedPtr<const FSurfaceMap, ESPMode::ThreadSafe> SurfaceMap;
	mutable FCriticalSection SurfaceLock;
};
