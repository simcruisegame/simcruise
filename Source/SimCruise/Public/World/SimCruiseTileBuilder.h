// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/SimCruiseRoadNetwork.h"

struct FSimCruiseTileData;

/** Mesh layers of a tile; each becomes one section of the tile's mesh component. */
enum class ESimCruiseTileLayer : uint8
{
	Terrain = 0,
	Asphalt,
	Concrete,
	Paint,
	Water,
	Building,
	Count
};

/** Plain mesh buffers in tile-local centimetres (x east, y south, z up). */
struct SIMCRUISE_API FSimCruiseMeshData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;
	TArray<int32> Indices;

	bool IsEmpty() const { return Indices.Num() == 0; }
	int32 NumTriangles() const { return Indices.Num() / 3; }
	int32 Add(const FVector& P, const FVector& N, const FLinearColor& C = FLinearColor::White);
	/** Add a triangle whose front face looks along Facing (Unreal's winding). */
	void Tri(int32 A, int32 B, int32 C, const FVector& Facing);
	void Quad(int32 A, int32 B, int32 C, int32 D, const FVector& Facing) { Tri(A, B, C, Facing); Tri(A, C, D, Facing); }
};

/** Land-cover mask channels (tile UV, north-west origin). */
namespace SimCruiseGroundMask
{
	inline constexpr int32 Size = 1024;   // ~0.54 m per texel on a 550 m tile
	// R vegetation, G sand, B paved paths, A water.
}

/** Everything one tile needs on the game thread; built off it. */
struct SIMCRUISE_API FSimCruiseTileBuild
{
	FIntPoint Tile = FIntPoint::ZeroValue;
	double TileSizeM = 0.0;
	FSimCruiseMeshData Layers[static_cast<int32>(ESimCruiseTileLayer::Count)];
	TArray<FColor> Mask;                   // SimCruiseGroundMask::Size^2, linear
	FString AerialKey;
	FSimCruiseRoadNetwork Roads;

	// Fine terrain grid (metres), (TerrainN+1)^2, after road conditioning.
	int32 TerrainN = 0;
	TArray<float> TerrainHeights;
	TArray<float> NaturalHeights;

	// Road ends on this tile's edges (seam contracts for neighbours built later).
	TArray<FSimCruiseSeamEnd> SeamEnds;

	// Every top-surface vertex of every carriageway, tile cm (collision checks).
	TArray<FVector> CarriagewayPoints;

	int32 ConditionedVertices = 0;
	int32 MarkingQuads = 0;
	int32 Buildings = 0;
	double MinHeightM = 0.0;
	double MaxHeightM = 0.0;
	double BuildMs = 0.0;

	FSimCruiseMeshData& Layer(ESimCruiseTileLayer L) { return Layers[static_cast<int32>(L)]; }
	const FSimCruiseMeshData& Layer(ESimCruiseTileLayer L) const { return Layers[static_cast<int32>(L)]; }
	int32 NumTriangles() const;

	/** Terrain height (m) of the fine grid at tile metres (matches the mesh). */
	double TerrainHeightAt(const FVector2d& P) const;
};

namespace SimCruiseTileBuilder
{
	/** Build terrain, road ribbons, curbs, markings, water and the ground mask. Thread-safe, pure. */
	SIMCRUISE_API TSharedRef<FSimCruiseTileBuild> Build(const FSimCruiseTileData& Data, double TileSizeM, const TArray<FSimCruiseSeamEnd>& NeighbourEnds = TArray<FSimCruiseSeamEnd>());

	/** Seam contracts of built edge neighbours, moved into the frame of Tile. */
	SIMCRUISE_API TArray<FSimCruiseSeamEnd> NeighbourContracts(const FIntPoint& Tile, double TileSizeM, TFunctionRef<const FSimCruiseTileBuild*(const FIntPoint&)> Find);

	/**
	 * Collapse points of an offset line that would run backwards on the inside of a
	 * bend tighter than the offset (the drawn edge never folds over itself).
	 */
	SIMCRUISE_API void UnfoldLine(TArray<FVector2d>& Line, const TArray<FVector2d>& Tangents);

	/** World-anchored texture offset (m) for a tile: (tile * size) mod Period, so detail textures run on across seams. */
	SIMCRUISE_API FVector2d TextureAnchorM(const FIntPoint& Tile, double TileSizeM, double PeriodM = 64.0);
}
