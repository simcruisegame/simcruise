// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Knowledge/SimCruisePlaceGraph.h"

struct FSimCruiseTileData;
class FSimCruiseRoadNetwork;
struct FSimCruiseRoadside;

/**
 * One context record per ~32 m of road (docs/team/research/realtime-context.md, items
 * 1-3 and 10): what the roadside is, built once on the tile build's worker thread
 * before any detail generator runs. The generators (palms, street furniture, the grass
 * field, later signs, lamps and the detail layer) read it; none of them guesses on its
 * own any more.
 *
 * Cells are cut at world-anchored 32 m marks along each ribbon (as the street lights'
 * rhythm), so a cell's ends do not move when the tile is rebuilt. A cell's seed comes
 * from the OpenStreetMap way it lies on and its 32 m station along that way (stable
 * when the tile is refetched or its rows reorder); a road with no matched way seeds
 * from its quantised world position.
 *
 * Sources, in order: OSM road-edge tags (SimCruiseRoadsideData), the tile's ribbons
 * (curb, sidewalk), the tile's land cover and buildings, the Köppen-Geiger class
 * (SimCruiseClimate), the place graph's region facts. Each field says whether it was
 * mapped or inferred.
 */

/** One side of a cell, 0 left (-R) and 1 right (+R) of the ribbon's station direction. */
struct SIMCRUISE_API FSimCruiseCellSide
{
	uint8 Edge = 0;              // ESimCruiseRoadEdge: 0 rural shoulder, 1 town edge, 2 curb
	FName Kerb;                  // raised / lowered / flush / none
	bool bKerbMapped = false;
	FName Sidewalk;              // yes / no / separate
	bool bSidewalkMapped = false;
	float SidewalkM = 0.0f;
	FName Shoulder;              // yes / no
	FName Barrier;               // guard_rail / wall / retaining_wall / fence, NAME_None when none within 12 m
	float BarrierOffsetM = 0.0f; // from the paved edge
	FName LandUse;               // water / beach / green / commercial / residential / built / open
};

struct SIMCRUISE_API FSimCruiseCellContext
{
	int32 Ribbon = INDEX_NONE;
	double S0 = 0.0;             // stations along the ribbon (m)
	double S1 = 0.0;
	FVector2d CentreM = FVector2d::ZeroVector; // tile metres
	FVector2d Tangent = FVector2d(1.0, 0.0);
	FVector2d Right = FVector2d(0.0, 1.0);
	double HalfW = 0.0;

	int64 WayId = 0;             // the OSM way the cell lies on (0 when none matched)
	int32 WayStation = 0;        // the 32 m station along that way
	FString StreetName;
	FName RoadClass;
	FName Surface;               // asphalt / concrete / paving_stones / gravel / unpaved ...
	bool bSurfaceMapped = false;
	FName Smoothness;            // excellent / good / intermediate / bad ..., NAME_None when unmapped
	FName Lit;                   // yes / no
	bool bLitMapped = false;
	int32 MaxSpeedKph = 0;
	FSimCruiseCellSide Sides[2];

	uint32 Seed = 0;
};

/** A mapped point snapped to the road: (road, station, side). */
struct SIMCRUISE_API FSimCruisePlacedPoint
{
	FName Kind;                  // street_lamp, traffic_signals, stop, give_way, power_pole, utility_pole, business ...
	int32 Point = INDEX_NONE;    // index into the tile's FSimCruiseRoadside::Points
	int32 Ribbon = INDEX_NONE;   // INDEX_NONE: farther than 30 m from any road (a business on its building)
	int32 Cell = INDEX_NONE;
	double S = 0.0;              // station on the ribbon
	int32 Side = 0;              // 0 left, 1 right
	double OffsetM = 0.0;        // from the paved edge (negative: on the carriageway)
	FVector2d PositionM = FVector2d::ZeroVector; // tile metres
};

struct SIMCRUISE_API FSimCruiseTileContext
{
	FIntPoint Tile = FIntPoint::ZeroValue;
	int32 RulesVersion = 0;
	uint32 DataHash = 0;
	int32 Month = 0;

	uint8 Koppen = 0;            // Beck class number (SimCruiseClimate), 0 when unknown
	FName Climate;               // climate:tropical ...
	FName Season;                // season:winter ... / season:wet, season:dry
	float GrassDry = 0.0f;       // 0 green ... 1 straw, for the grass field material
	/** Tree and palm species with their weight at this place (0-1). */
	TArray<TPair<FName, float>> Species;
	/** Region, climate and season facts with the rules run (the local look: hydrant colours ...). */
	FSimCruiseFactSet TileFacts;
	uint32 TileSeed = 0;

	TArray<FSimCruiseCellContext> Cells;
	/** Per ribbon: X its first cell, Y how many (cells of a ribbon run in station order). */
	TArray<FIntPoint> RibbonCells;
	TArray<FSimCruisePlacedPoint> Points;
	/** OSM data the record was built from (shared with the item-2 generators). */
	int32 MappedWays = 0;
	int32 MatchedCells = 0;
	double BakeMs = 0.0;

	float SpeciesWeight(FName Species) const;
	/** The cell nearest to a point (tile metres) within MaxM of its centre line, or null. */
	const FSimCruiseCellContext* Nearest(const FVector2d& P, double MaxM = 40.0) const;
	/** The cell of a ribbon at a station, or null. */
	const FSimCruiseCellContext* At(int32 Ribbon, double S) const;
	FString Describe(int32 MaxCells = 6) const;
};

namespace SimCruiseCellContext
{
	/** Bumped when the rules of the bake change (part of the cache key). */
	inline constexpr int32 RulesVersion = 1;
	inline constexpr double CellM = 32.0;

	struct FInputs
	{
		const FSimCruiseTileData& Data;
		const FSimCruiseRoadNetwork& Roads;
		double Side = 0.0;
		double LatDeg = 0.0;
		double LonDeg = 0.0;
		int32 Month = 1;
		const FSimCruiseRoadside* Roadside = nullptr;
		/** Land cover at a point (tile metres); unset means none. */
		TFunction<bool(const FVector2d&)> IsVegetation;
		TFunction<bool(const FVector2d&)> IsWater;
		TFunction<bool(const FVector2d&)> IsSand;
	};

	/** The record's cache key part from the data: ribbons, land, buildings, roadside, climate, month. */
	SIMCRUISE_API uint32 DataHash(const FInputs& In);

	/** Bakes the record (pure; worker thread safe). */
	SIMCRUISE_API void Bake(const FInputs& In, FSimCruiseTileContext& Out);

	/** The record from the cache by (tile, rules version, data hash), baked and stored when missing. */
	SIMCRUISE_API TSharedRef<const FSimCruiseTileContext, ESPMode::ThreadSafe> ForBuild(const FInputs& In, bool* bOutCacheHit = nullptr);
	SIMCRUISE_API void ClearCache();

	/** The stable seed of a cell: its OSM way and 32 m station, else its quantised world position. */
	SIMCRUISE_API uint32 StableSeed(int64 WayId, int32 WayStation, const FVector2d& WorldM, FName RoadClass);

	/** Species weights for a climate class at a place (data/knowledge has no tree rules yet: estimates here). */
	SIMCRUISE_API void SpeciesFor(uint8 Koppen, FName Climate, double LatDeg, double LonDeg, TArray<TPair<FName, float>>& Out);

	/** The month for builds: -SimCruiseMonth=1..12, else today's (local time). */
	SIMCRUISE_API int32 CurrentMonth();
}
