// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCruiseTileBuild;

/** A built tile as the start pick sees it: where it lies (world cm) and its build. */
struct FSimCruiseStartTile
{
	FVector OriginCm = FVector::ZeroVector;
	const FSimCruiseTileBuild* Build = nullptr;
};

/** A place to start a car: on a road, in its lane, facing the way it drives. */
struct FSimCruiseStartPose
{
	FVector LocationCm = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	int32 Tile = INDEX_NONE;
	int32 Ribbon = INDEX_NONE;
	int32 Station = INDEX_NONE;
	int32 Dir = 0;
	/** How far the road goes on ahead the way the drive robot follows it (capped). */
	double AheadM = 0.0;
	/** From the requested point. */
	double DistanceM = 0.0;
	/** Lower is better (DistanceM, plus penalties for a short road ahead or the wrong way). */
	double Score = 0.0;
};

/**
 * Where a car starts (the player's, the drive robot's), anywhere in the US, by general rules:
 * on a public road (no service ways), mid-block (15 m or more from a junction, 25 m of room
 * before the next one), 25 m or more inside its tile (a seam's road ends are the least sure
 * ground), on a straight piece (under 25 degrees of turn over 24 m), in the lane the traffic
 * drives, with a car's footprint (4.6 x 2 m) inside the carriageway and no ground above the
 * road under it, facing a road that goes on: 800 m or more ahead the way the drive robot follows
 * it (along its ribbon, then the straightest ribbon that starts or ends where it ends), not into
 * a dead end. Candidates come out best first; the caller takes the first one a ground trace
 * confirms (the road surface is there, at the road's height).
 */
namespace SimCruiseStartPick
{
	/** The road ahead the start wants (60 s of driving at about 13 m/s). */
	constexpr double WantedAheadM = 800.0;

	/**
	 * How far the road goes on from station S of a ribbon in direction Dir (+1 along the
	 * stations, -1 against), the way the drive robot follows it, up to CapM; a dead end stops it,
	 * a loop counts as going on. Tiles: every built tile it may cross.
	 */
	SIMCRUISE_API double AheadM(const TArray<FSimCruiseStartTile>& Tiles, int32 Tile, int32 Ribbon, double S, int32 Dir, double CapM);

	/**
	 * Whether a car's footprint at Pose stands on the carriageway of its ribbon with no ground
	 * above the road under it; Why says what failed.
	 */
	SIMCRUISE_API bool OnCarriageway(const TArray<FSimCruiseStartTile>& Tiles, const FSimCruiseStartPose& Pose, FString* Why = nullptr);

	/**
	 * Start candidates within RadiusM of NearCm, best first (at most MaxCandidates): on the road
	 * named RoadName (empty: any public road), facing PreferredYawDeg where the road allows it
	 * (>= 1e9: no preference).
	 */
	SIMCRUISE_API TArray<FSimCruiseStartPose> Candidates(const TArray<FSimCruiseStartTile>& Tiles, const FString& RoadName, const FVector& NearCm,
		double RadiusM, double PreferredYawDeg, int32 MaxCandidates = 24);
}
