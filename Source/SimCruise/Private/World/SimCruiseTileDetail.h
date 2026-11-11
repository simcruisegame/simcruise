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
	 * A US stop sign (MUTCD R1-1): a 30 in red octagon with a white border and STOP, its
	 * bottom 7 ft above the ground (urban), on a galvanised square post at BaseM (tile
	 * metres, ground at GroundZM), the face toward Facing (the approaching drivers).
	 */
	SIMCRUISE_API void AddStopSign(FSimCruiseMeshData& Props, const FVector2d& BaseM, double GroundZM, const FVector2d& Facing);

	/** True when a point (tile metres) lies inside any building footprint of the tile. */
	SIMCRUISE_API bool IsInsideBuilding(const FSimCruiseTileData& Data, double Side, const FVector2d& P);
}
