// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Street furniture where OpenStreetMap maps it: benches, litter bins, bicycle racks, post
 * boxes, fire hydrants, bollards and bus stops (with or without a shelter). Free, keyless
 * (Overpass, ODbL). One file per zoom-16 tile: data/knowledge/furniture/<x>_<y>.json
 * (bundled start tiles), then Saved/Knowledge/furniture/ (fetched while playing).
 * SimCruiseStreetFurniture (World/) draws them, at their real size, on the sidewalk.
 */
struct SIMCRUISE_API FSimCruiseFurniture
{
	double Lat = 0.0;
	double Lon = 0.0;
	FName Kind;            // bench, waste_basket, bicycle_parking, post_box, fire_hydrant, bollard, bus_stop
	bool bShelter = false; // bus_stop: shelter=yes
	bool bBench = false;   // bus_stop: bench=yes
	float DirectionDeg = -1.0f; // OSM direction=*, degrees from north, or -1
	FString Colour;        // OSM colour=* / fire_hydrant:colour=*, if mapped
};

namespace SimCruiseFurniture
{
	/** The Overpass union lines for a bounding box "(s,w,n,e)". */
	SIMCRUISE_API FString OverpassLines(const FString& Box);
	/** Furniture nodes out of an Overpass answer (other elements are skipped). */
	SIMCRUISE_API bool ParseOverpass(const FString& Json, TArray<FSimCruiseFurniture>& Out, FString& OutError);

	SIMCRUISE_API FString ToJson(const FIntPoint& Tile, const TArray<FSimCruiseFurniture>& Items);
	SIMCRUISE_API bool FromJson(const FString& Json, TArray<FSimCruiseFurniture>& Out);
	SIMCRUISE_API FString BundledPath(const FIntPoint& Tile);
	SIMCRUISE_API FString CachePath(const FIntPoint& Tile);

	/** The tile's furniture from the cache or the bundle (thread-safe). False when none was fetched. */
	SIMCRUISE_API bool ForTile(const FIntPoint& Tile, TArray<FSimCruiseFurniture>& Out);
	SIMCRUISE_API void Store(const FIntPoint& Tile, const TArray<FSimCruiseFurniture>& Items, bool bSave = true);
}
