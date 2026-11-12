// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/SimCruiseTileBuilder.h"

struct FSimCruiseTileData;

/**
 * Detail built on top of a tile's terrain and roads: building recipes, palms
 * and street lights. Pure functions of the tile data, run on the tile build's
 * worker thread like the rest of SimCruiseTileBuilder.
 *
 * Everything here is light geometry whose look comes from the building
 * material (SimCruiseSurfaceKind in UV1): a facade is a handful of quads, the
 * windows, stucco and neon are drawn by the shader (docs/unreal-architecture.md,
 * "World detail").
 */
namespace SimCruiseTileDetail
{
	/** Regional character of the buildings at a place. */
	enum class EBuildingStyle : uint8
	{
		Generic,
		/** The Miami Beach Architectural District (National Register, 1979): 1930s-40s Art Deco and MiMo. */
		MiamiBeachDeco,
	};

	SIMCRUISE_API EBuildingStyle StyleForPlace(double LatDeg, double LonDeg);

	/** Where coconut palms (Cocos nucifera) grow outdoors in the US: South Florida's coasts and the Keys, Hawaii. */
	SIMCRUISE_API bool IsCoconutPalmRegion(double LatDeg, double LonDeg);

	struct FContext
	{
		const FSimCruiseTileData& Data;
		FSimCruiseTileBuild& B;
		double Side = 0.0;        // tile size, m
		double LatDeg = 0.0;      // tile centre
		double LonDeg = 0.0;
		/** Land cover at a point (tile metres). */
		TFunction<bool(const FVector2d&)> IsVegetation;
		TFunction<bool(const FVector2d&)> IsPavedPath;
		TFunction<bool(const FVector2d&)> IsWater;
	};

	/** Footprints extruded by the place's recipe into the Building layer. */
	SIMCRUISE_API void BuildBuildings(const FContext& Context);

	/** Coconut palms on the parks and lawns of the palm region, as instances (FSimCruiseTileBuild::PalmInstances). */
	SIMCRUISE_API void BuildPalms(const FContext& Context);

	/** Number of palm meshes the instances choose from. */
	inline constexpr int32 PalmVariants = 6;

	/** One palm variant at the origin (cm), leaning toward +X: trunk (building material kinds) and fronds (foliage). */
	SIMCRUISE_API void BuildPalmVariant(int32 Variant, FSimCruiseMeshData& Trunk, FSimCruiseMeshData& Fronds);

	/** Street lights behind the curbs of public streets (Props), with their lamp points for the night light pool. */
	SIMCRUISE_API void BuildStreetLights(const FContext& Context);

	/**
	 * Promenades: a low stone seawall where a mapped footway runs along sand (the beach
	 * side), and benches and lamp posts along footways 3 m and wider (Props; lamps join the
	 * night light pool).
	 */
	SIMCRUISE_API void BuildPromenades(const FContext& Context);

	/**
	 * A US stop sign (MUTCD R1-1): a 30 in red octagon with a white border and STOP, its
	 * bottom 7 ft above the ground (urban), on a galvanised square post at BaseM (tile
	 * metres, ground at GroundZM), the face toward Facing (the approaching drivers).
	 */
	SIMCRUISE_API void AddStopSign(FSimCruiseMeshData& Props, const FVector2d& BaseM, double GroundZM, const FVector2d& Facing);

	/** How a named place signs its building: 0 none, 1 painted letters, 2 neon letters; a hotel ranks first on its building. */
	struct FSignStyle
	{
		int32 Rank = 0;       // 0: no sign; 3 hotel, 2 bar or club, 1 shop, restaurant, office
		bool bNeon = false;
		bool bHotel = false;
	};
	SIMCRUISE_API FSignStyle SignStyleFor(const FString& PoiKind);

	/**
	 * The words on a place's sign from its map name: the branch or location tail
	 * ("Cardozo Hotel South Beach" -> "CARDOZO", "Mercato Di Mare - Ocean Drive" ->
	 * "Mercato Di Mare"); hotels in capitals without "Hotel". Empty when nothing is left.
	 */
	SIMCRUISE_API FString SignText(const FString& Name, bool bHotel);

	/**
	 * Footprint corner rounding (Streamline Moderne): convex corners of street fronts become
	 * quarter arcs. Ring in metres, Sign the winding (+1/-1), Front per edge. Out: the new ring,
	 * per new edge its source edge (or INDEX_NONE on an arc).
	 */
	SIMCRUISE_API void RoundCorners(const TArray<FVector2d>& Ring, double Sign, const TArray<bool>& Front, const TArray<bool>& Keep,
		double MaxRadiusM, TArray<FVector2d>& OutRing, TArray<int32>& OutSource);

	/** True when a point (tile metres) lies inside any building footprint of the tile. */
	SIMCRUISE_API bool IsInsideBuilding(const FSimCruiseTileData& Data, double Side, const FVector2d& P);
}
