// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * The car's dashboard clock: a seven-segment LCD drawn by a material over the
 * model's own display texture, showing the local time at the place being
 * driven. The model's texture has a fixed time painted on it ("2:15" in the
 * NB); the profile names the material and the digits' box in the texture.
 */
namespace SimCruiseCarClock
{
	/** The clock material (editor builds make it in memory; null without one). */
	SIMCRUISE_API UMaterialInterface* Get();

	/**
	 * Local civil time at a place, as minutes after midnight. US time zones by
	 * their approximate boundaries (Eastern east of 85.5 W, Central to 101 W,
	 * Mountain to 114.5 W, Pacific, Alaska west of 130 W, Hawaii), elsewhere the
	 * nearest 15-degree zone; US daylight saving time from the second Sunday of
	 * March to the first Sunday of November at 2:00 local, except in Hawaii and Arizona.
	 */
	SIMCRUISE_API int32 LocalMinutes(const FDateTime& Utc, double LatitudeDeg, double LongitudeDeg);

	/** Hours and minutes as the 12-hour clock shows them (12:00-11:59, no leading zero). */
	SIMCRUISE_API FString Format12(int32 LocalMinutes);
}
