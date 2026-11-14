// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCruiseTileData;
struct FSimCruiseTileBuild;
struct FSimCruiseTileContext;
struct FSimCruiseScanChannel;
struct FSimCruiseClassTint;
struct FSimCruiseAerialImage;

/**
 * The tile scan (docs/research/scan-engine.md): everything the game knows about one zoom-16
 * tile, fused once on a worker and cached on disk, read by one renderer path. This is the
 * core: the file format, the rules-only scan (a full scan with no eye) and the cache. Eyes
 * (Public/Scan/SimCruiseScanEye.h, the AI thread's) add channels; rules always give every
 * cell a class.
 *
 * Grid: 512 cells across the tile plus an 8-cell guard on each side (528 x 528 stored),
 * north-up, row 0 north. A cell is named by its global z25 pixel (tile x 512 + i), so it is
 * the same in every session; the guard band is computed from the tile's own apron, never
 * copied from a neighbour, so a tile does not depend on build order.
 */
namespace SimCruiseScan
{
	inline constexpr uint16 Version = 3;
	inline constexpr int32 Cells = 512;
	inline constexpr int32 Guard = 8;
	inline constexpr int32 Stored = Cells + 2 * Guard;

	/** Surface classes (contract v1); 26-63 reserved. data/scan/classes.json gives albedo ranges. */
	enum class EClass : uint8
	{
		Unknown = 0, Asphalt = 1, Concrete = 2, Paving = 3, Gravel = 4, DirtTrack = 5, BareSoil = 6, Rock = 7, Scree = 8,
		Sand = 9, GrassShort = 10, GrassTall = 11, DryGrass = 12, Crop = 13, Shrub = 14, ForestBroadleaf = 15,
		ForestConifer = 16, Wetland = 17, Water = 18, SnowIce = 19, Roof = 20, Parking = 21, Rail = 22, Kerb = 23,
		ShoulderGravel = 24, RoadMarking = 25, Count = 26
	};
	SIMCRUISE_API const TCHAR* ClassName(uint8 Class);
	/** A display colour per class (the debug view). */
	SIMCRUISE_API FColor ClassColour(uint8 Class);

	/** INST: 32 bytes a record. */
#pragma pack(push, 1)
	struct FInstance
	{
		int32 Xcm = 0, Ycm = 0, Zcm = 0;  // tile-local centimetres (x east, y south)
		uint16 Kind = 0;                  // 1 tree, 2 palm, 3 shrub, 10 lamp, 11 pole, 12 signal, 13 sign ...
		uint16 Subtype = 0;               // species group, sign type ...
		uint16 HeightDm = 0;
		uint16 CrownDm = 0;
		uint8 Yaw = 0;                    // 256 steps
		uint8 Flags = 0;
		uint16 StringId = 0;
		uint32 Seed = 0;
		uint8 Confidence = 0;
		uint8 Reserved[3] = { 0, 0, 0 };
	};
	/** TINT: one entry per class (64). */
	struct FTint
	{
		uint8 R = 128, G = 128, B = 128;  // linear multiplier, 128 = x1.0
		uint8 Confidence = 0;
		uint16 Samples = 0;
	};
#pragma pack(pop)
	static_assert(sizeof(FInstance) == 32, "INST records are 32 bytes");
	static_assert(sizeof(FTint) == 6, "TINT entries are 6 bytes");
}

/** One tile's scan. Rasters are Stored x Stored, row-major from the north-west guard corner. */
struct SIMCRUISE_API FSimCruiseTileScan
{
	FIntPoint Tile = FIntPoint::ZeroValue;
	uint8 Zoom = 16;
	uint32 DataHash = 0;
	int32 RulesVersion = 0;
	double BuildSeconds = 0.0;

	/** CLAS: R class a, G class b, B weight of a (0-255), A confidence. */
	TArray<FColor> Clas;
	/** COND: R wear/age, G dryness/season, B moisture (0-255), A reserved. */
	TArray<FColor> Cond;
	/** ALBD: RGB de-lit albedo (sRGB), A canopy cover. */
	TArray<FColor> Albd;
	/** HGHT: ground offset (signed, 0.5 cm units) and canopy top (10 cm units). */
	TArray<int16> GroundOffset;
	TArray<uint16> CanopyTop;
	TArray<SimCruiseScan::FInstance> Instances;
	SimCruiseScan::FTint Tint[64];
	TArray<FString> Strings;
	/** META: sources and licences, eyes, timings, self-check, warnings (JSON). */
	FString Meta;

	int32 Index(int32 I, int32 J) const { return J * SimCruiseScan::Stored + I; }
	/** The class a cell holds most of. */
	uint8 ClassAt(int32 I, int32 J) const { return Clas.IsValidIndex(Index(I, J)) ? Clas[Index(I, J)].R : 0; }
	/** Cell (i, j) of the stored grid for a point in tile metres (guard included). */
	FIntPoint CellOf(const FVector2d& PointM, double SideM) const;
	/** The global z25 pixel of a stored cell (session-free id). */
	FIntPoint GlobalCell(int32 I, int32 J) const { return FIntPoint(Tile.X * SimCruiseScan::Cells + I - SimCruiseScan::Guard, Tile.Y * SimCruiseScan::Cells + J - SimCruiseScan::Guard); }

	/** The SCN1 file (little-endian): header, chunk table, chunks (raw). */
	TArray<uint8> Serialize() const;
	bool Deserialize(const TArray<uint8>& Bytes, FString& OutError);
	/** Bytes without the build time (the determinism test compares these). */
	TArray<uint8> SerializeWithoutTime() const;
};

namespace SimCruiseScan
{
	/** Saved/Scan/v<Version>/16/<x>/<y>.scan */
	SIMCRUISE_API FString CachePath(const FIntPoint& Tile);

	/**
	 * The rules-only scan of a built tile: every cell a class from the map and the ground
	 * (carriageways, curbs and sidewalks, roofs, water, sand, lawns and woods, slope, the
	 * season's dryness), the albedo from the class's range and the photograph when there is
	 * one, the ground offset of the road cross-section, trees from the canopy rows, and the
	 * tile's road context. Pure; worker-thread safe.
	 */
	SIMCRUISE_API void BakeRules(const FSimCruiseTileData& Data, const FSimCruiseTileBuild& Build, FSimCruiseTileScan& Out);

	/**
	 * TINT by colour match, no model: for the ground classes the materials colour themselves
	 * (short grass, bare soil, sand, paving), the median colour of the tile's aerial photograph
	 * over the build's mask where that class is pure (grass only where the photograph is green),
	 * as a hue: relative to the class colour, at the class colour's own luminance (the
	 * photograph's brightness carries its sun and haze), held to x0.8-x1.25 a channel, and
	 * faded to x1.0 under 4000 samples. Classes with under 400 samples keep x1.0. Returns the
	 * number of classes tinted.
	 */
	SIMCRUISE_API int32 MatchTints(const TArray<FColor>& Mask, int32 MaskSize, FIntPoint TileId, const FSimCruiseAerialImage& Aerial, FTint (&Out)[64]);
	/** The ground material's class colours (linear) that the tints multiply (GroundCode). */
	SIMCRUISE_API FLinearColor GroundClassColour(EClass Class);

	/** The scan from the disk cache by (tile, version, rules version, data hash), else baked and saved. */
	SIMCRUISE_API TSharedRef<const FSimCruiseTileScan, ESPMode::ThreadSafe> ForBuild(const FSimCruiseTileData& Data, const FSimCruiseTileBuild& Build, bool* bOutCacheHit = nullptr);

	/**
	 * Fuses an eye's channels (Public/Scan/SimCruiseScanEye.h: 528 x 528, the scan's own grid)
	 * into a rules scan. Map geometry (carriageways, curbs, shoulders, sidewalks, roofs, mapped
	 * water) always wins. Elsewhere the eye replaces the rule's ground class where its probability
	 * x the class's held-out IoU x the channel's confidence beats the rule's confidence; built and
	 * bare ground below 0.5 probability are ignored (IoU 0.33 and 0.27). Tints fill TINT where their
	 * confidence beats none. Returns the cells changed; META records the eye.
	 */
	SIMCRUISE_API int32 FuseEye(FSimCruiseTileScan& Scan, FName EyeName, const TArray<FSimCruiseScanChannel>& Channels, const TArray<FSimCruiseClassTint>& Tints);

	/** The scan class an eye channel or tint names ("surface_logits_grass_short" -> GrassShort), Unknown when none. */
	SIMCRUISE_API uint8 ClassOfChannel(FName Channel);

	/** -SimCruiseScanView: the class map as a PNG in Saved/Scan/view/<x>_<y>.png (and the path). */
	SIMCRUISE_API bool WriteClassView(const FSimCruiseTileScan& Scan, FString* OutPath = nullptr);
}
