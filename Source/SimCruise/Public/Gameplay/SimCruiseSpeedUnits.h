// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** The speed units the driver reads: the HUD's speed and the GPS distances. */
enum class ESimCruiseSpeedUnits : uint8
{
	Mph = 0,
	Kmh = 1,
};

/**
 * The driver's units (pause menu, Settings, "Speed units"), kept in the console
 * variable simcruise.Hud.Units (0 mph, 1 km/h) and in preferences.json as
 * "speed_units" ("mph" or "kmh"). Miles per hour is the default.
 */
namespace SimCruiseSpeedUnits
{
	inline constexpr double MphPerMps = 1.0 / 0.44704;
	inline constexpr double KmhPerMps = 3.6;

	SIMCRUISE_API ESimCruiseSpeedUnits Get();
	SIMCRUISE_API void Set(ESimCruiseSpeedUnits Units);
	SIMCRUISE_API ESimCruiseSpeedUnits Next(ESimCruiseSpeedUnits Units);
	/** Apply the remembered choice (preferences.json; the default when preferences are off). */
	SIMCRUISE_API void LoadPreference();
	/** Switch to the other units and remember the choice. */
	SIMCRUISE_API void Toggle();

	/** A speed in m/s as the units' number (sign kept). */
	SIMCRUISE_API double FromMps(double Mps, ESimCruiseSpeedUnits Units);
	/** The whole number the HUD shows: the size of the speed, rounded; never negative. */
	SIMCRUISE_API int32 Shown(double Mps, ESimCruiseSpeedUnits Units);
	/** "mph" or "km/h". */
	SIMCRUISE_API const TCHAR* Label(ESimCruiseSpeedUnits Units);
	/** Engine rpm as the HUD shows it: to the nearest 50, never negative. */
	SIMCRUISE_API int32 ShownRpm(double Rpm);
}
