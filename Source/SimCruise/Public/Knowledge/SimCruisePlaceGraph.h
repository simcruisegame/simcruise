// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCruiseTileData;

/**
 * The place knowledge graph: decides which real details go where, anywhere.
 *
 * A fact is a token "kind:value" (style:art_deco, era:1930s, climate:tropical,
 * poi:hotel, live:night ...) with a confidence 0-1. Facts come from the tile data
 * (height, footprint, named places), the location (data/knowledge regions), open
 * data fetched per tile (OpenStreetMap tags, Wikidata style, year, architect,
 * heritage: FSimCruisePlaceRecord) and the live feeds (live:*).
 *
 * Rules (data/knowledge/place-graph.json) join facts into new facts with a weight:
 *   style:art_deco -> detail:eyebrows 0.9, palette:deco_pastel 0.9
 *   family:deco + poi:hotel -> detail:neon_sign 0.9
 *   live:night + detail:neon_sign -> light:neon_on 0.95
 * Several rules for one fact combine as independent evidence (noisy OR); a negative
 * weight suppresses. A building with no facts of its own takes the style and era of
 * its dated neighbours ("spread"), with less confidence.
 *
 * Everything here is a pure function of its inputs: safe on the tile build's worker
 * thread, deterministic per building, and fast (a tile of 300 buildings bakes in well
 * under a millisecond). Network and caching live in SimCruisePlaceFacts.
 */

/** A set of facts with their confidence, and why each one holds. */
struct SIMCRUISE_API FSimCruiseFactSet
{
	struct FEntry
	{
		float Confidence = 0.0f;
		FString Why;
	};
	TMap<FName, FEntry> Facts;

	float Get(FName Fact) const { const FEntry* E = Facts.Find(Fact); return E ? E->Confidence : 0.0f; }
	bool Has(FName Fact, float Threshold = 0.5f) const { return Get(Fact) >= Threshold; }
	/** Keeps the stronger of the old and new confidence. */
	void Add(FName Fact, float Confidence, const FString& Why);
	void Append(const FSimCruiseFactSet& Other);
	/** The strongest fact of a kind ("style" -> style:art_deco), or NAME_None. */
	FName Strongest(const TCHAR* Kind, float* OutConfidence = nullptr) const;
	/** "fact 0.90 (why)" lines, strongest first; Kind filters by prefix when set. */
	FString Describe(const TCHAR* Kind = nullptr) const;
};

/** One rule edge: all If facts (minus Unless) imply each Then fact with its weight. */
struct FSimCruisePlaceRule
{
	TArray<FName> If;
	TArray<FName> Unless;
	TArray<TPair<FName, float>> Then;
	FString Why;
};

struct FSimCruisePlaceRegion
{
	FString Id;
	double LatMin = 0.0, LonMin = 0.0, LatMax = 0.0, LonMax = 0.0;
	TArray<TPair<FName, float>> Facts;
	FString Why;
};

struct FSimCruisePalette
{
	TArray<FLinearColor> Wall;
	TArray<FLinearColor> Accent;
};

/** The parsed rule graph. */
struct SIMCRUISE_API FSimCruisePlaceRules
{
	int32 Version = 0;
	TMap<FString, FString> StyleAliases;  // lower-case label -> style token value
	TArray<FSimCruisePlaceRegion> Regions;
	TArray<FSimCruisePlaceRule> Rules;
	TArray<FSimCruisePlaceRule> LiveRules;
	TMap<FName, FSimCruisePalette> Palettes;
	double SpreadRadiusM = 140.0;
	int32 SpreadMinNeighbours = 2;
	float SpreadStrength = 0.7f;
	TArray<FString> SpreadKinds;

	bool Parse(const FString& JsonText, FString& OutError);

	/** Facts of the regions that hold the point. */
	void RegionFacts(double LatDeg, double LonDeg, FSimCruiseFactSet& Out) const;

	/** Forward-chains the rules over the facts until nothing changes (at most a few passes). */
	void Infer(FSimCruiseFactSet& Facts, bool bLive = false) const;

	/** "Art Deco architecture" -> "art_deco" through the aliases, else a slug of the label. */
	FString StyleToken(const FString& Label) const;
};

/** One record of open data about a place (a building, a named place or a district). */
struct SIMCRUISE_API FSimCruisePlaceRecord
{
	double Lat = 0.0;
	double Lon = 0.0;
	FString Name;
	FString OsmId;          // "way/123", "node/45", empty for Wikidata only
	FString Wikidata;       // "Q123"
	TArray<FString> Styles; // style labels as the source writes them
	int32 Year = 0;         // built (OSM start_date, Wikidata P571 inception)
	FString Architect;
	FString Heritage;       // e.g. "National Register of Historic Places contributing property"
	FString Material;       // OSM building:material, Wikidata P186
	FString Colour;         // OSM building:colour (hex or name)
	FString RoofShape;      // OSM roof:shape
	int32 Levels = 0;
	FString Building;       // OSM building=* (hotel, apartments, house ...)
	FString Amenity;        // OSM amenity / tourism / shop
	bool bDistrict = false; // a historic district or neighbourhood: its facts cover the tile
};

/** The plan for one building: its style, colours and how likely each detail is. */
struct SIMCRUISE_API FSimCruiseBuildingPlan
{
	FSimCruiseFactSet Facts;
	FName Style;
	float StyleConfidence = 0.0f;
	int32 Year = 0;
	FString Name;
	FString Architect;
	FName Palette;
	FLinearColor Wall = FLinearColor::White;
	FLinearColor Accent = FLinearColor::White;
	bool bHasColour = false;  // false: the recipe keeps its own colour
	bool bColourFromData = false;
	/** 0-1 weathering (age, salt air, soot) less restoration; Weathered() applies it to a wall colour. */
	float Wear = 0.0f;
	uint32 Seed = 0;
	/** Index into FSimCruiseTileData::Buildings. */
	int32 Building = INDEX_NONE;
	/** Footprint centre in tile metres (x east, y south from the north-west corner). */
	FVector2d CentreM = FVector2d::ZeroVector;
	float HeightM = 0.0f;

	/** Probability of a detail ("detail:eyebrows"), or Default when the graph says nothing. */
	float P(FName Detail, float Default = 0.0f) const;
	/** Deterministic yes/no: a 0-1 draw (the recipe's own hash) under the probability. */
	bool Draw(FName Detail, double Unit01, float Default = 0.0f) const { return Unit01 < P(Detail, Default); }
	/** The strongest recipe is this one (recipe:deco, recipe:resort_tower ...) at 0.5 or more. */
	bool IsRecipe(const TCHAR* Recipe) const;
	/** A wall colour darkened and warmed by the building's wear (a few per cent; stable per building). */
	FLinearColor Weathered(const FLinearColor& InWall) const;
	FString Explain() const;
};

struct SIMCRUISE_API FSimCruiseTilePlan
{
	FIntPoint Tile = FIntPoint::ZeroValue;
	TArray<FSimCruiseBuildingPlan> Buildings; // one per FSimCruiseTileData::Buildings row
	FSimCruiseFactSet TileFacts;              // region, district and climate facts
	int32 Records = 0;                        // open-data records that matched
	int32 Matched = 0;                        // buildings with a record of their own
	double BakeMs = 0.0;

	const FSimCruiseBuildingPlan* Find(int32 Building) const { return Buildings.IsValidIndex(Building) ? &Buildings[Building] : nullptr; }
};

namespace SimCruisePlaceGraph
{
	/** The rule graph from data/knowledge/place-graph.json, loaded once (thread-safe). */
	SIMCRUISE_API const FSimCruisePlaceRules& Rules();
	SIMCRUISE_API FString RulesPath();

	/** Facts a record gives its building (style:*, era:*, architect:*, heritage:*, material:* ...). */
	SIMCRUISE_API void RecordFacts(const FSimCruisePlaceRules& Rules, const FSimCruisePlaceRecord& Record, FSimCruiseFactSet& Out);

	/** era:1930s plus a coarse band (era:before_1945, era:1945_1975, era:after_1975). */
	SIMCRUISE_API void EraFacts(int32 Year, const FString& Why, FSimCruiseFactSet& Out);

	/** OSM/Overture POI kind -> poi:hotel, poi:bar, poi:restaurant, poi:shop ... (empty when none). */
	SIMCRUISE_API FName PoiFact(const FString& Kind);

	/** CSS colour name or #hex -> colour. */
	SIMCRUISE_API bool ParseColour(const FString& Text, FLinearColor& Out);

	/**
	 * Bakes the plan of every building of a tile: tile, region and record facts, the
	 * spread from neighbours, the rules, then colours. Records are the tile's open data
	 * (SimCruisePlaceFacts::ForTile), matched to footprints by position (inside, or the
	 * nearest within 18 m).
	 */
	SIMCRUISE_API void BakeTile(const FSimCruisePlaceRules& Rules, const FSimCruiseTileData& Data, double LatDeg, double LonDeg, double SideM,
		const TArray<FSimCruisePlaceRecord>& Records, FSimCruiseTilePlan& Out);

	/** The building's facts with the live facts added and the live rules run (light:*, street:*, surface:*). */
	SIMCRUISE_API void LiveView(const FSimCruisePlaceRules& Rules, const FSimCruiseBuildingPlan& Plan, const FSimCruiseFactSet& Live, FSimCruiseFactSet& Out);
}
