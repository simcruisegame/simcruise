// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Which tiles a drive needs, which jobs may run and which results may land.
 * Pure bookkeeping (no engine objects), so it is testable and the streamer
 * actor stays thin. Ported from godot-final:game/Drive.gd stream():
 *  - the ring of tiles around the car's tile is wanted, nearest first, with a
 *    look-ahead toward where the car will be in a few seconds; the tiles on the
 *    line to that point are wanted too, out to one tile past the ring (the
 *    streamer sets the point at speed x (2 x load time + 4 s)); route tiles
 *    go first and always keep one load slot while the car moves;
 *  - tiles beyond the ring plus one tile of hysteresis are retired;
 *  - each job carries the generation of the place it was started for; a new
 *    place (search, Surprise me, a jump) bumps the generation, and a result
 *    of an older generation, or for a tile no longer wanted, is rejected;
 *  - a failed tile waits before it is tried again;
 *  - resident tiles and jobs in flight are bounded.
 */
struct FSimCruiseTileJobTicket
{
	FIntPoint Tile = FIntPoint::ZeroValue;
	uint32 Generation = 0;
	uint64 Serial = 0;
};

struct FSimCruiseTileBuild;

/**
 * Fallback ground: a tile in range whose load failed (or is still loading) must not leave
 * a hole the car falls through. The streamer lays a flat collision slab over it at this
 * height (m, the frame's datum): the mean terrain height along the edges it shares with
 * built neighbours (their fine grid), else DefaultM (the car's own height). The real
 * tile replaces it when its collision is ready.
 */
namespace SimCruiseFallbackGround
{
	SIMCRUISE_API double HeightM(const FIntPoint& Tile, TFunctionRef<const FSimCruiseTileBuild*(const FIntPoint&)> Find, double DefaultM, int32* OutNeighbours = nullptr);
}

class SIMCRUISE_API FSimCruiseTileManager
{
public:
	int32 RingRadius = 1;          // 3 x 3 tiles (Drive.gd)
	int32 MaxInFlight = 2;
	double RetryDelaySeconds = 15.0;

	/** Forget everything and start a new place. Returns the new generation. */
	uint32 Reset();
	uint32 GetGeneration() const { return Generation; }

	/** The car's tile and where it is heading (tile coordinates, fractional). */
	void SetFocus(const FVector2d& FocusTileCoord, const FVector2d& PredictedTileCoord);
	FIntPoint GetFocusTile() const { return FocusTile; }

	/** Next tiles to start (marked in flight), best first. */
	TArray<FSimCruiseTileJobTicket> TakeRequests(double NowSeconds);

	/** Resident tiles that should be retired now (removed from the resident set). */
	TArray<FIntPoint> TakeEvictions();

	/** A finished job: true if its result should be installed (and the tile is now resident). */
	bool Complete(const FSimCruiseTileJobTicket& Ticket, bool bSucceeded, double NowSeconds);

	/** Whether a ticket's result would still be accepted (does not change state). */
	bool IsCurrent(const FSimCruiseTileJobTicket& Ticket) const;

	bool IsWanted(const FIntPoint& Tile, int32 Slack = 1) const;
	bool IsResident(const FIntPoint& Tile) const { return Resident.Contains(Tile); }
	bool IsInFlight(const FIntPoint& Tile) const { return InFlight.Contains(Tile); }
	int32 NumResident() const { return Resident.Num(); }
	int32 NumInFlight() const { return InFlight.Num(); }
	int32 MaxResident() const { return FMath::Square(2 * (RingRadius + 1) + 1); }

	/** Shift every tile key (not needed for rebasing: tiles keep absolute ids). */
	const TSet<FIntPoint>& GetResident() const { return Resident; }

private:
	uint32 Generation = 1;
	uint64 NextSerial = 1;
	bool bHasFocus = false;
	FIntPoint FocusTile = FIntPoint::ZeroValue;
	FVector2d Focus = FVector2d::ZeroVector;
	FVector2d Predicted = FVector2d::ZeroVector;
	TSet<FIntPoint> Resident;
	TMap<FIntPoint, uint64> InFlight;   // tile -> serial
	TMap<FIntPoint, double> RetryAt;
};
