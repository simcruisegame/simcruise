// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruisePlaces.h"
#include "Gameplay/SimCruiseRoute.h"

/**
 * Famous drives: a place to start and the stops of the route that follows
 * the road (docs/plan/player-first-roadmap.md, the hero roads). Each end was
 * checked with the helper's router on the OpenStreetMap roads.
 */
struct SIMCRUISE_API FSimCruiseFamousRoad
{
	FString Title;
	/** One line: where it goes and what it is like. */
	FString Note;
	FSimCruisePlace Start;
	TArray<FSimCruiseRouteStop> Stops;
	double LengthMi = 0.0;
};

namespace SimCruiseFamousRoads
{
	SIMCRUISE_API const TArray<FSimCruiseFamousRoad>& All();
}
