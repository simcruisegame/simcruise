// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Knowledge/SimCruisePlaceGraph.h"

/**
 * Open data about the places of a tile, for the place knowledge graph. Free, keyless,
 * legal sources only:
 *
 *   OpenStreetMap  Overpass API: building and named-place tags (start_date,
 *                  building:architecture, building:material, building:colour,
 *                  roof:shape, building:levels, heritage, wikidata). ODbL, credit
 *                  "(c) OpenStreetMap contributors".
 *   Wikidata       SPARQL (wikibase:around): architectural style P149, inception
 *                  P571, architect P84, heritage designation P1435, material P186,
 *                  instance of P31 (historic districts). CC0.
 *
 * Records are cached per tile: the bundled data/knowledge/places/<x>_<y>.json first,
 * then Saved/Knowledge/places/<x>_<y>.json (fetched while playing). No network code
 * here: the parsers and the merge are pure, so each one has a test with a recorded
 * answer (USimCruisePlaceGraphSubsystem does the fetching).
 */
namespace SimCruisePlaceFacts
{
	/** Overpass QL for a tile's buildings and tagged places. */
	SIMCRUISE_API FString OverpassQuery(const FIntPoint& Tile);
	/** Wikidata SPARQL for the items around the tile centre. */
	SIMCRUISE_API FString WikidataQuery(const FIntPoint& Tile);

	/** The public Overpass instances, main first (all free; each is busy at times). */
	SIMCRUISE_API const TArray<FString>& OverpassUrls();
	SIMCRUISE_API FString OverpassUrl(int32 Attempt = 0);
	SIMCRUISE_API FString WikidataUrl(const FString& Query);

	SIMCRUISE_API bool ParseOverpass(const FString& Json, TArray<FSimCruisePlaceRecord>& Out, FString& OutError);
	SIMCRUISE_API bool ParseWikidata(const FString& Json, TArray<FSimCruisePlaceRecord>& Out, FString& OutError);

	/** One record per place: Wikidata items join the OSM element that names them (styles, year, architect added). */
	SIMCRUISE_API void Merge(const TArray<FSimCruisePlaceRecord>& Osm, const TArray<FSimCruisePlaceRecord>& Wikidata, TArray<FSimCruisePlaceRecord>& Out);

	/** Keeps only records that tell the graph something (a style, a year, a colour, a heritage ...). */
	SIMCRUISE_API bool IsUseful(const FSimCruisePlaceRecord& Record);

	SIMCRUISE_API FString ToJson(const FIntPoint& Tile, const TArray<FSimCruisePlaceRecord>& Records, const FString& Sources);
	SIMCRUISE_API bool FromJson(const FString& Json, TArray<FSimCruisePlaceRecord>& Out, FString& OutError);

	SIMCRUISE_API FString BundledPath(const FIntPoint& Tile);
	SIMCRUISE_API FString CachePath(const FIntPoint& Tile);

	/** The tile's records from the bundle or the cache (thread-safe, kept in memory). False when none is saved yet. */
	SIMCRUISE_API bool ForTile(const FIntPoint& Tile, TArray<FSimCruisePlaceRecord>& Out);
	/** Saves fetched records to the cache and makes them the tile's records. */
	SIMCRUISE_API void Store(const FIntPoint& Tile, const TArray<FSimCruisePlaceRecord>& Records, const FString& Sources);
	/** True when the tile has a saved answer (even an empty one). */
	SIMCRUISE_API bool IsKnown(const FIntPoint& Tile);
}
