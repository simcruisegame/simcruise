// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Math/RandomStream.h"

/**
 * A place to drive: what the place search, the start pool, the presets and
 * the road atlas hand to ASimCruiseGameMode::TravelTo.
 *
 * Ported from godot-final:game/StartPool.gd and the place parts of Drive.gd
 * and Hud.gd. Bearings are compass degrees (0 north, 90 east).
 */
struct SIMCRUISE_API FSimCruisePlace
{
	FString Title;
	/** The street to start on near the point (empty: the nearest public road). */
	FString Street;
	double Lon = 0.0;
	double Lat = 0.0;
	bool bHasBearing = false;
	double BearingDeg = 0.0;
	/** A start-pool point: when the named street is not found, the main road under the point will do. */
	bool bLoose = false;

	/** The game refuses places past 82 degrees (Web Mercator, Drive.gd). */
	bool IsValid() const;

	static FSimCruisePlace Make(double Lon, double Lat, const FString& Title, const FString& Street = FString(), double BearingDeg = -1.0);
};

namespace SimCruisePlaces
{
	/** Pool places tried after the first before the bundled fallback (StartPool.gd RETRIES). */
	constexpr int32 Retries = 3;

	/** The bundled offline place: Ocean Drive, Miami Beach (StartPool.gd FALLBACK). */
	SIMCRUISE_API FSimCruisePlace Fallback();

	/** The presets of the search box (Hud.gd preset_places). */
	SIMCRUISE_API const TArray<FSimCruisePlace>& Presets();

	/** data/start/start-pool.json: verified US road points (licenses/Road Atlas.txt). */
	SIMCRUISE_API FString StartPoolPath();

	/** Valid entries only: a damaged entry only shrinks the choice. */
	SIMCRUISE_API TArray<FSimCruisePlace> ParseStartPool(const FString& Json);
	SIMCRUISE_API TArray<FSimCruisePlace> LoadStartPool(const FString& Path = StartPoolPath());

	/** Up to Count distinct places in random order (partial Fisher-Yates); the one titled Avoid is left out while others remain. */
	SIMCRUISE_API TArray<FSimCruisePlace> Sequence(const TArray<FSimCruisePlace>& Pool, FRandomStream& Rng, int32 Count, const FString& Avoid = FString());

	enum class EQueryKind : uint8
	{
		/** The answer is in Places (coordinates or an offline alias). */
		Places,
		/** "surprise me": a random pool road. */
		Surprise,
		/** Ask the atlas helper (`search`). */
		Search,
		/** Nothing typed. */
		Empty,
	};

	struct FQueryAnswer
	{
		EQueryKind Kind = EQueryKind::Empty;
		TArray<FSimCruisePlace> Places;
		FString Note;
	};

	/** "lat, lon", the offline aliases and "surprise me" (Drive.gd search); anything else goes to the helper. */
	SIMCRUISE_API FQueryAnswer ParseQuery(const FString& Query);

	/** The helper's `search` document: [{title, street, lon, lat}, ...]. */
	SIMCRUISE_API TArray<FSimCruisePlace> ParseSearchResults(const TSharedPtr<FJsonValue>& Json);

	/** Compass bearing to world yaw: the world frame has +X east and +Y south (SimCruiseGeo). */
	SIMCRUISE_API double BearingToYawDeg(double BearingDeg);
	SIMCRUISE_API double YawToBearingDeg(double YawDeg);
}
