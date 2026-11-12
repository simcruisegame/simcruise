// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCruiseWeather.generated.h"

/**
 * Observed weather the environment renders. The fields follow helper/conditions.mjs,
 * which reads the nearest METAR (aviationweather.gov) within 85 km:
 * {clear_sky, wet, station, at, km, cloud, visibility_m, description}.
 */
USTRUCT(BlueprintType)
struct SIMCRUISE_API FSimCruiseWeatherConditions
{
	GENERATED_BODY()

	/** Sky cover fraction: CLR 0, FEW 0.19, SCT 0.44, BKN 0.78, OVC 1. The Godot default sky was 0.12. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "0", ClampMax = "1"))
	float CloudCover = 0.12f;

	/** Meteorological visibility in metres. METAR reports cap at 10 statute miles (16 093 m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather", meta = (ClampMin = "50"))
	float VisibilityM = 40000.0f;

	/** True when the station reported CLR/SKC, at least 10 km visibility and no weather. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bClearSky = false;

	/** Liquid precipitation now (rain, drizzle, showers, thunderstorms). Snow is not wet road. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bRaining = false;

	/** Frozen precipitation now (snow, grains, pellets, hail). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bSnowing = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FString Station;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	FString Description;

	/** Distance from the place to the reporting station, km (0 when not observed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float StationKm = 0.0f;

	/**
	 * Column aerosol optical depth at 550 nm from the live air-quality feed (CAMS); negative
	 * when unknown. When known and the air is not misty it sets the haze instead of visibility.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float AerosolOpticalDepth = -1.0f;

	/** Wind at 10 m, m/s, and the direction it comes from (degrees clockwise from north). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float WindSpeedMs = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float WindFromDeg = 0.0f;

	/** Air temperature at 2 m, degrees C (NaN-free default: a mild 20). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	float TemperatureC = 20.0f;

	/** True when these conditions came from an observation rather than the default sky. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather")
	bool bObserved = false;
};

/** Parsing and the slow-changing state derived from weather (surface water). */
struct SIMCRUISE_API FSimCruiseWeather
{
	/**
	 * Reads one conditions object as written by helper/conditions.mjs. An empty object
	 * ({} - no station within 85 km) leaves Out at its defaults and returns true.
	 * The METAR "wet" flag also matches snow; the raw wx string, when present as
	 * "wx", splits rain from snow.
	 */
	static bool ParseConditionsJson(const FString& Json, FSimCruiseWeatherConditions& Out, FString& OutError);

	/**
	 * Advances road surface water w in [0,1]: rain wets it with a 3-minute time constant,
	 * dry weather dries it with a 15-minute one (proposal 18, road-load-weather).
	 */
	static float StepRoadWetness(float Wetness, bool bRaining, float DeltaSeconds);

	/** Volumetric-cloud coverage for Epic's simple cloud material from a sky cover fraction. */
	static float CloudMaterialCoverage(float CloudCover);
};
