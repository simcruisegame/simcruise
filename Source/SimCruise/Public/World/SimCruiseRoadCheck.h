// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCruiseTileBuild;

/**
 * The road-step check: the surface a wheel rolls on must be continuous. Along every
 * ribbon's centre line and the middle of each half (where the wheels run), every 0.1 m,
 * the top of the tile's asphalt (the carriageway triangles the wheels meet) is sampled;
 * a change from one sample to the next that the slope just before it does not explain
 * (the pre-flight's own step measure) larger than the threshold is a step: another
 * carriageway's edge standing proud at a junction, a hole, a seam. A deck more than 1 m
 * above the ribbon's own profile is a bridge across and is skipped. Pure and worker-thread safe: it runs on
 * every tile build (logged) and in the tests.
 */
struct SIMCRUISE_API FSimCruiseRoadStep
{
	int32 Ribbon = INDEX_NONE;
	double S = 0.0;              // station along the ribbon (m)
	double Lateral = 0.0;        // the sampled line's offset from the centre (m)
	FVector2d PositionM = FVector2d::ZeroVector; // tile metres
	double JumpCm = 0.0;
	double HeightBeforeM = 0.0;
	double HeightAfterM = 0.0;
};

namespace SimCruiseRoadCheck
{
	inline constexpr double DefaultThresholdCm = 2.0;
	inline constexpr double SpacingM = 0.1;

	/** Steps above ThresholdCm, largest first. */
	SIMCRUISE_API TArray<FSimCruiseRoadStep> FindSteps(const FSimCruiseTileBuild& Build, double ThresholdCm = DefaultThresholdCm);

	/** The top of the collision surfaces at P (tile metres); false when nothing covers P. */
	SIMCRUISE_API bool TopSurface(const FSimCruiseTileBuild& Build, const FVector2d& P, double& OutHeightM);

	SIMCRUISE_API FString Describe(const FSimCruiseTileBuild& Build, const TArray<FSimCruiseRoadStep>& Steps, int32 Max = 8);
}
