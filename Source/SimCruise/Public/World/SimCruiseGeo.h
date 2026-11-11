// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Web Mercator tile math and the game's local frame.
 *
 * The atlas helper addresses the world in zoom-16 Web Mercator tiles
 * (65536 x 65536 for the planet; x grows east, y grows south), the same grid
 * game/Drive.gd uses. Everything inside a tile is in "tile units" (0..1 along
 * each axis, (0,0) at the north-west corner), which is what every row of the
 * helper's tile JSON carries.
 *
 * Unreal axes: X = east, Y = south, Z = up, centimetres. Facing east, south is
 * on the right, so this is Unreal's own left-handed frame with no mirroring;
 * a tile's u maps to +X and v to +Y.
 *
 * Distances: one session uses one tile size in metres, the Mercator scale at
 * the latitude where the drive started (Drive.gd scale_at). Over the few
 * kilometres a drive streams, the true scale drifts by about 1e-4 per km of
 * north-south travel, far under any seam tolerance, and every tile's
 * geometry is built in the same metres, so neighbouring tiles always meet.
 */
namespace SimCruiseGeo
{
	/** Equatorial circumference used by the helper (helper/road-grade.mjs). */
	inline constexpr double EarthCircumferenceM = 40075016.68557849;
	inline constexpr int32 TileZoom = 16;
	inline constexpr double TilesPerAxis = 65536.0;
	/** Web Mercator stops at ~85.05 degrees; the game refuses places past 82 (Drive.gd). */
	inline constexpr double MaxLatitudeDeg = 82.0;

	/** Fractional zoom-16 tile coordinates of a longitude/latitude in degrees. */
	SIMCRUISE_API FVector2d LonLatToTile(double LonDeg, double LatDeg);

	/** Longitude/latitude in degrees of fractional zoom-16 tile coordinates. */
	SIMCRUISE_API void TileToLonLat(const FVector2d& Tile, double& OutLonDeg, double& OutLatDeg);

	/** Ground metres along one side of a zoom-16 tile at a latitude (Drive.gd scale_at). */
	SIMCRUISE_API double TileSizeMetres(double LatDeg);

	/** Latitude of the centre of a zoom-16 tile row. */
	SIMCRUISE_API double TileRowLatitudeDeg(int64 TileY);

	/** The zoom-14 aerial photograph (helper `aerial`, files a-<x>-<y>.jpg) holding a zoom-16 tile. */
	SIMCRUISE_API FIntPoint AerialTileOf(const FIntPoint& Tile16);

	/** Integer floor that is right for negative values too. */
	inline int64 FloorToInt64(double Value) { return static_cast<int64>(FMath::FloorToDouble(Value)); }
}

/**
 * The local frame of one drive: an origin tile and the session's tile size.
 * World position of a tile's north-west corner = (Tile - Origin) * size, so
 * tile actors and everything built in tile metres line up exactly. Rebasing
 * moves the origin by whole tiles; the returned offset is added to every
 * existing world position (floating origin).
 */
struct SIMCRUISE_API FSimCruiseWorldFrame
{
	FIntPoint OriginTile = FIntPoint::ZeroValue;
	double TileSizeM = 0.0;

	bool IsValid() const { return TileSizeM > 1.0; }
	double TileSizeCm() const { return TileSizeM * 100.0; }

	/** A frame for a drive starting at a place; the origin is the tile holding it. */
	static FSimCruiseWorldFrame ForPlace(double LonDeg, double LatDeg);

	/** World position (cm) of a tile's north-west corner, at height 0. */
	FVector TileOriginCm(const FIntPoint& Tile) const;

	/** World position (cm, z = 0) of fractional zoom-16 tile coordinates. */
	FVector2d TileCoordToWorldCm(const FVector2d& TileCoord) const;

	/** Fractional zoom-16 tile coordinates of a world position (cm). */
	FVector2d WorldCmToTileCoord(const FVector2d& WorldCm) const;

	/** The tile holding a world position. */
	FIntPoint TileAtWorldCm(const FVector& WorldCm) const;

	/** World position (cm) of a place and a height in metres. */
	FVector LonLatToWorldCm(double LonDeg, double LatDeg, double HeightM = 0.0) const;

	/** Longitude/latitude of a world position. */
	void WorldCmToLonLat(const FVector& WorldCm, double& OutLonDeg, double& OutLatDeg) const;

	/** Move the origin to another tile. Returns the offset (cm) to add to world positions. */
	FVector RebaseTo(const FIntPoint& NewOriginTile);
};

/** A zoom-16 tile id as a map key and a printable name ("18179_27907", the bundled file stem). */
inline FString SimCruiseTileName(const FIntPoint& Tile) { return FString::Printf(TEXT("%d_%d"), Tile.X, Tile.Y); }
