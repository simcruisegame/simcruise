// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The driver's remembered choices (godot-final:game/DrivingPreferences.gd):
 * car, gearbox, sound, HUD level and lens, in Saved/SimCruise/preferences.json,
 * written whole to a temporary file and moved into place so a crash or a
 * second window never leaves half a file. Checks, captures and automation
 * runs neither read nor write it (QA isolation), so they always start from
 * the defaults; SIMCRUISE_QA_PREFS_DIR points a check at a folder of its own.
 */
namespace SimCruisePreferences
{
	/** False in checks, captures and automation runs. */
	SIMCRUISE_API bool IsEnabled();
	SIMCRUISE_API FString Path();

	SIMCRUISE_API FString GetString(const FString& Key, const FString& Default);
	SIMCRUISE_API int32 GetInt(const FString& Key, int32 Default, int32 Min, int32 Max);
	SIMCRUISE_API bool GetBool(const FString& Key, bool Default);

	SIMCRUISE_API void SetString(const FString& Key, const FString& Value);
	SIMCRUISE_API void SetInt(const FString& Key, int32 Value);
	SIMCRUISE_API void SetBool(const FString& Key, bool Value);
}
