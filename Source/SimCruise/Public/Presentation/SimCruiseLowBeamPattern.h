// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * The low beam's photometric pattern as a light function on a head-lamp spot
 * light: a sharp horizontal cut-off 0.57 degrees (1 %) below the lamp's horizon
 * on the oncoming side, rising at 15 degrees on the curb side (ECE R112 / SAE
 * J1383 shape), the hot spot just under the cut-off and slightly to the curb
 * side, a dimmer foreground and about 1.5 % spill above the cut-off.
 */
namespace SimCruiseLowBeamPattern
{
	/** Asset path for cooked builds (/Game/Vehicle/Materials/LF_SimCruiseLowBeam). */
	SIMCRUISE_API FString AssetPath();

	/** The light function material: the saved asset, or (editor builds) one built in memory. Null without either. */
	SIMCRUISE_API UMaterialInterface* Get();

	/** Fraction of the peak intensity at a direction relative to the road (degrees; + up, + toward the curb side). */
	SIMCRUISE_API double Relative(double ElevationDeg, double CurbwardAzimuthDeg);
}
