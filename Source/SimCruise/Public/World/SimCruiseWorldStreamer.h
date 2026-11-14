// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "GameFramework/Actor.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileManager.h"
#include "SimCruiseWorldStreamer.generated.h"

class ASimCruiseWorldTile;
class USpotLightComponent;
class FSimCruiseAtlasClient;
class USimCruiseWorldResources;
struct FSimCruiseTileBuild;

DECLARE_MULTICAST_DELEGATE_OneParam(FSimCruiseWorldRebased, const FVector& /*OffsetCm*/);

/**
 * Streams the real place around the car: a bounded ring of zoom-16 tiles in
 * the drive's local frame, each read (bundled offline data first, then the
 * atlas helper) and built off the game thread, installed on it one per
 * frame. A floating origin keeps the car near (0,0,0): past
 * RebaseDistanceM the origin moves to the car's tile, every tile actor moves
 * by whole tiles, and OnRebased tells the car, cameras and traffic to move
 * by the same offset.
 */
UCLASS()
class SIMCRUISE_API ASimCruiseWorldStreamer : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseWorldStreamer();

	/** Begin a drive at a place: a new frame and a new generation (older jobs are rejected). */
	void StartAt(double LonDeg, double LatDeg, const FString& Title);

	/** Where the car is (world cm) and how fast it moves (cm/s); without it the player camera is used. */
	void SetFocus(const FVector& WorldCm, const FVector& VelocityCmPerSecond);

	const FSimCruiseWorldFrame& GetFrame() const { return Frame; }
	const FString& GetTitle() const { return Title; }
	ASimCruiseWorldTile* GetTile(const FIntPoint& Tile) const;
	int32 NumTiles() const { return Tiles.Num(); }
	bool IsBusy() const;

	/** All tiles within Radius of the tile holding WorldCm are installed and their collision is in the physics scene. */
	bool AreTilesReadyAround(const FVector& WorldCm, int32 Radius = 1) const;

	/** Drivable surface height (world cm) under a world position; false where no tile is loaded. */
	bool SurfaceHeightAt(const FVector& WorldCm, double& OutZCm) const;

	/**
	 * A pose on a named road near a point: in the travel lane on the right,
	 * facing the direction of travel. Name match is case-insensitive; an empty
	 * name takes the nearest public road (not a service way).
	 */
	bool FindRoadPose(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, FVector& OutLocationCm, FRotator& OutRotation) const;

	/**
	 * As above, but on a two-way road the car faces the given yaw (degrees, world frame)
	 * when that way has room; a one-way road keeps its direction of travel (Drive.gd: "requested
	 * bearing honoured on two-way roads, one-way direction respected").
	 */
	bool FindRoadPoseFacing(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, double PreferredYawDeg, FVector& OutLocationCm, FRotator& OutRotation) const;

	/** Tiles of this drive (since StartAt) that failed to load or build: no network, no helper, a bad document. */
	int32 NumTilesFailed() const { return TilesFailed; }

	FSimCruiseWorldRebased OnRebased;

	/** One streaming step (called from Tick; tests drive it directly). */
	void TickStreaming(float DeltaSeconds);

	UPROPERTY(EditAnywhere, Category = "SimCruise|World")
	int32 RingRadius = 1;

	UPROPERTY(EditAnywhere, Category = "SimCruise|World")
	int32 MaxConcurrentBuilds = 2;

	UPROPERTY(EditAnywhere, Category = "SimCruise|World")
	double RebaseDistanceM = 2000.0;

	/** Ask the atlas helper for tiles that are not bundled (needs the helper and, for new places, the network). */
	UPROPERTY(EditAnywhere, Category = "SimCruise|World")
	bool bUseHelper = true;

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|World")
	int32 TilesInstalled = 0;

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|World")
	int32 ResultsRejected = 0;

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|World")
	int32 TilesFailed = 0;
	/** Seconds a tile takes from request to result (a running mean; the look-ahead reads it). */
	double LoadSecondsMean = 6.0;
	/** How far ahead the streamer asks for tiles: speed x (2 x LoadSecondsMean + LookAheadMarginSeconds). */
	static constexpr double LookAheadMarginSeconds = 4.0;

	UPROPERTY(Transient)
	TObjectPtr<USimCruiseWorldResources> Resources;

	/** Street lights that really light the road at night: the nearest lamp heads get one of these. */
	UPROPERTY(EditAnywhere, Category = "SimCruise|World")
	int32 StreetLightPoolSize = 24;

	/** How much the night lighting is on (0 day .. 1 night), from the sun's elevation. */
	float GetLightsOn() const { return LightsOn; }

	/** The moving traffic round the player (drawn runs only). */
	UPROPERTY(Transient)
	TObjectPtr<class ASimCruiseTraffic> Traffic;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FResult
	{
		FSimCruiseTileJobTicket Ticket;
		TSharedPtr<FSimCruiseTileBuild> Build;
		FString Error;
		FString AerialKey;
		TArray<FColor> AerialPixels;
		/** Its mip chain, built on the worker so the game thread only copies it in. */
		TArray<TArray<FColor>> AerialMips;
		int32 AerialWidth = 0;
		int32 AerialHeight = 0;
		/** When the worker finished it (FPlatformTime::Seconds), for the load-time mean. */
		double FinishedAt = 0.0;
	};
	struct FInbox
	{
		TQueue<TSharedPtr<FResult, ESPMode::ThreadSafe>, EQueueMode::Mpsc> Queue;
		std::atomic<bool> bClosed{ false };
		// Aerial photographs being fetched by a worker: one helper run per photograph,
		// never two processes writing the same cache file.
		FCriticalSection AerialLock;
		TSet<FString> AerialsFetching;
	};

	void Launch(const FSimCruiseTileJobTicket& Ticket);
	/** When each job was launched (game thread), by ticket serial. */
	TMap<uint64, double> LaunchedAt;

	/** Flat collision under every tile in range that has none yet (SimCruiseFallbackGround). */
	void UpdateFallbackGround(const FVector& FocusCm);
	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<class UBoxComponent>> FallbackGround;
	/** -SimCruiseFailTiles=x_y,x_y: these tiles' loads fail (the fallback-ground test). */
	TSet<FIntPoint> ForcedFailures;
public:
	int32 NumFallbackGround() const { return FallbackGround.Num(); }
	bool HasFallbackGround(const FIntPoint& Tile) const { return FallbackGround.Contains(Tile); }
private:
	void Rebase(const FIntPoint& NewOrigin);
	void RemoveTile(const FIntPoint& Tile);
	FVector CurrentFocus(FVector& OutVelocity) const;
	/** Windows, lenses and the street-light pool follow the sun (photocells switch near sunset). */
	void UpdateNightLighting(float DeltaSeconds);

	FSimCruiseWorldFrame Frame;
	FString Title;
	FSimCruiseTileManager Manager;
	TSharedPtr<FSimCruiseAtlasClient, ESPMode::ThreadSafe> Atlas;
	TSharedPtr<FInbox, ESPMode::ThreadSafe> Inbox;
	/** A finished tile held back a frame while its aerial photograph was made. */
	TSharedPtr<FResult, ESPMode::ThreadSafe> Deferred;

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<ASimCruiseWorldTile>> Tiles;

	bool bHasFocus = false;
	FVector FocusCm = FVector::ZeroVector;
	FVector FocusVelocity = FVector::ZeroVector;
	bool bStarted = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> StreetLightPool;
	float LightsOn = 0.f;
	float NightLightingClock = 0.f;
};
