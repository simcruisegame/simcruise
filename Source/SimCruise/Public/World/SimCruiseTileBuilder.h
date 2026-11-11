// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/SimCruiseRoadNetwork.h"

struct FSimCruiseTileData;
class FStaticMeshRenderData;

/** Mesh layers of a tile; each becomes one section of the tile's mesh component. */
enum class ESimCruiseTileLayer : uint8
{
	Terrain = 0,
	Asphalt,
	Concrete,
	Paint,
	Water,
	Building,
	Props,      // street furniture and palm trunks (opaque, the building material's kinds)
	Foliage,    // palm fronds (masked, two-sided)
	Count
};

/**
 * Surface kinds the building, props and foliage materials branch on (UV1.x).
 * UV0 carries (metres along the face, metres above the building's ground).
 */
namespace SimCruiseSurfaceKind
{
	inline constexpr float Wall = 0.f;        // stucco with windows on the storey grid
	inline constexpr float Trim = 1.f;        // plain stucco: eyebrows, bands, fins, soffits
	inline constexpr float Roof = 2.f;
	inline constexpr float Neon = 3.f;        // glowing tube, colour in the vertex colour
	inline constexpr float Storefront = 4.f;  // ground-floor glass behind a porch
	inline constexpr float Terrazzo = 5.f;    // porch floor and steps
	inline constexpr float Metal = 6.f;       // painted or galvanised poles
	inline constexpr float Bark = 7.f;        // palm trunk
	inline constexpr float LampGlass = 8.f;   // luminaire lens, lit at night
	inline constexpr float Railing = 9.f;     // balcony glass
	inline constexpr float Frond = 10.f;      // palm leaf card
	inline constexpr float WallFront = 11.f;  // a street facade: shopfront glass in the ground storey
}

/** Plain mesh buffers in tile-local centimetres (x east, y south, z up). */
struct SIMCRUISE_API FSimCruiseMeshData
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FLinearColor> Colors;
	TArray<int32> Indices;
	/** Optional texture coordinates: empty, or one per position. */
	TArray<FVector2D> UV0;
	TArray<FVector2D> UV1;

	bool IsEmpty() const { return Indices.Num() == 0; }
	int32 NumTriangles() const { return Indices.Num() / 3; }
	int32 Add(const FVector& P, const FVector& N, const FLinearColor& C = FLinearColor::White);
	/** A vertex with both texture coordinates (layers that use them use them for every vertex). */
	int32 Add(const FVector& P, const FVector& N, const FLinearColor& C, const FVector2D& InUV0, const FVector2D& InUV1);
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

/** One placed palm: a shared mesh variant, tile-local position (cm), yaw and scale. */
struct FSimCruisePalmInstance
{
	FVector LocationCm = FVector::ZeroVector;
	float YawDeg = 0.f;
	float Scale = 1.f;
	uint8 Variant = 0;
};

/** A parked car: on the ground at a stall's centre (tile cm), facing the traffic beside it. */
struct FSimCruiseParkedCar
{
	FVector LocationCm = FVector::ZeroVector;
	float YawDeg = 0.f;
	/** Stable random bytes for this stall: which car, and which colour. */
	uint8 Pick = 0;
	uint8 Colour = 0;
};

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

	/**
	 * What is drawn, ready-made on the worker: the layers as one static mesh's render data
	 * (vertex and index buffers, one section per non-empty layer; RenderSlots maps section
	 * to layer). Null in headless runs.
	 */
	TUniquePtr<FStaticMeshRenderData> RenderData;
	TArray<int32> RenderSlots;

	/** Palms to instance on this tile. */
	TArray<FSimCruisePalmInstance> PalmInstances;

	/** Parked cars in the painted stalls of parking lanes. */
	TArray<FSimCruiseParkedCar> ParkedCars;

	/** Street-light sources (tile cm, the lamp head's lens centre) for the night light pool. */
	TArray<FVector> LampPointsCm;

	int32 ConditionedVertices = 0;
	int32 MarkingQuads = 0;
	int32 Buildings = 0;
	int32 Palms = 0;
	int32 StreetLights = 0;
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

	/** A static mesh's render data from mesh buffers (any thread; one section per part, 2 UV sets, colours). */
	SIMCRUISE_API TUniquePtr<FStaticMeshRenderData> MakeRenderData(const TArray<const FSimCruiseMeshData*>& Parts);

	/** Ear-clip a simple polygon (any winding); false if it could not be clipped. */
	SIMCRUISE_API bool Triangulate(const TArray<FVector2d>& Ring, TArray<int32>& OutIndices);

	/** World-anchored texture offset (m) for a tile: (tile * size) mod Period, so detail textures run on across seams. */
	SIMCRUISE_API FVector2d TextureAnchorM(const FIntPoint& Tile, double TileSizeM, double PeriodM = 64.0);
}
