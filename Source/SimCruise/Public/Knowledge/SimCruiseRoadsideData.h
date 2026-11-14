// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The roadside as OpenStreetMap maps it, for the per-segment context record
 * (World/SimCruiseCellContext.h). It comes in the same Overpass answer as the place
 * facts and the street furniture (same cache, same back-off, ODbL, credit
 * "(c) OpenStreetMap contributors"):
 *
 *   roads     the tile's highway ways with their geometry (clipped to the tile) and
 *             surface, smoothness, shoulder, sidewalk, kerb, lit, maxspeed, name
 *   barriers  barrier=guard_rail / fence / wall / retaining_wall within 15 m of a road
 *   lines     power=line / minor_line
 *   points    highway=street_lamp / traffic_signals / stop / give_way, power=pole /
 *             tower, man_made=utility_pole, and named shop / amenity / office with
 *             their brand
 *
 * Cached per zoom-16 tile in Saved/Knowledge/roadside/<x>_<y>.json (fetched while
 * playing; never committed). Pure parsers, so each has a test with a recorded answer.
 */
struct SIMCRUISE_API FSimCruiseRoadsideWay
{
	int64 Id = 0;
	FName Kind;              // road: the highway class; barrier: guard_rail ...; line: line / minor_line
	FString Name;
	FString Surface;         // surface=*
	FString Smoothness;      // smoothness=*
	/** Per side of the way's drawing direction, 0 left, 1 right: yes / no / separate, or empty when not mapped. */
	FString Sidewalk[2];
	FString Shoulder[2];
	FString Kerb[2];         // raised / lowered / flush / no
	FString Lit;             // yes / no
	int32 MaxSpeedKph = 0;
	TArray<FVector2d> LonLat; // x lon, y lat
};

struct SIMCRUISE_API FSimCruiseRoadsidePoint
{
	int64 Id = 0;
	double Lat = 0.0;
	double Lon = 0.0;
	FName Kind;              // street_lamp, traffic_signals, stop, give_way, power_pole, power_tower, utility_pole, business
	FString Name;            // business: its name
	FString Category;        // business: "shop=bakery", "amenity=cafe", "office=insurance"
	FString Brand;           // brand=*
	FString BrandWikidata;   // brand:wikidata=* (the Name Suggestion Index key)
	FString Colour;          // colour=* / brand:colour=* when mapped
	FString Direction;       // direction=* (degrees, forward, backward)
};

struct SIMCRUISE_API FSimCruiseRoadside
{
	TArray<FSimCruiseRoadsideWay> Roads;
	TArray<FSimCruiseRoadsideWay> Barriers;
	TArray<FSimCruiseRoadsideWay> Lines;
	TArray<FSimCruiseRoadsidePoint> Points;

	bool IsEmpty() const { return Roads.Num() + Barriers.Num() + Lines.Num() + Points.Num() == 0; }
	/** A stable hash of everything (the context record's cache key). */
	uint32 Hash() const;
};

namespace SimCruiseRoadsideData
{
	/** Point lines for the Overpass union whose answer is "out tags center" ("(s,w,n,e)" box). */
	SIMCRUISE_API FString OverpassPointLines(const FString& Box);
	/** Statements after that union's out: roads, barriers near them and power lines, with clipped geometry. */
	SIMCRUISE_API FString OverpassGeometryStatements(const FString& Box);

	/** Road ways, barriers, lines and points out of an Overpass answer (other elements are skipped). */
	SIMCRUISE_API bool ParseOverpass(const FString& Json, FSimCruiseRoadside& Out, FString& OutError);

	/** "50", "35 mph", "50 km/h", "none" -> km/h (0 when unknown). */
	SIMCRUISE_API int32 ParseMaxSpeedKph(const FString& Text);

	SIMCRUISE_API FString ToJson(const FIntPoint& Tile, const FSimCruiseRoadside& Data);
	SIMCRUISE_API bool FromJson(const FString& Json, FSimCruiseRoadside& Out);
	SIMCRUISE_API FString CachePath(const FIntPoint& Tile);

	/** The tile's roadside from the cache (thread-safe, kept in memory). False when none was fetched. */
	SIMCRUISE_API bool ForTile(const FIntPoint& Tile, FSimCruiseRoadside& Out);
	SIMCRUISE_API void Store(const FIntPoint& Tile, const FSimCruiseRoadside& Data, bool bSave = true);
}
