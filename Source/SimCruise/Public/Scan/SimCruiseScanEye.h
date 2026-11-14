// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The scan engine's eye interface (docs/research/scan-engine.md, "Eyes"). An eye is an optional CPU
 * model that reads a tile's inputs into tile-aligned channels and points. No eye is required: the
 * scan rules fill every channel, and a missing or failed eye only lowers confidence.
 *
 * Rules for every eye: ONNX Runtime on the CPU through NNE, all eyes together at most 2 cores, 1.5 GB
 * and 3 s per tile, no static ONNX Runtime objects with destructors, Run never on the game thread.
 */

/** Map facts of a tile (roads, footprints, land cover, climate); defined by the scan engine. */
struct FSimCruiseScanMapFacts;

/** Read-only inputs of one tile. */
struct FSimCruiseScanInputs
{
	/** Web Mercator tile (z = 16 for the world tiles). */
	FIntVector TileZXY = FIntVector(16, 0, 0);
	/** Ground size of one output cell, metres (about 1). */
	double CellMetres = 1.0;
	/** Cells of each output raster across the tile, without the guard. */
	int32 Cells = 512;
	/** Border cells on each side, copied from the neighbours so tile edges match. */
	int32 GuardCells = 8;
	/**
	 * Aerial photo, AerialSize x AerialSize, north-up, covering the tile PLUS its guard border
	 * (the tile's bounds grown by GuardCells cells on each side). sRGB in an FColor container;
	 * de-lit when the scan engine has done that.
	 */
	TArray<FColor> AerialRGB;
	int32 AerialSize = 0;
	/** Near-infrared on the same grid, or empty. */
	TArray<uint8> AerialNIR;
	/** Base terrain heights (m), TerrainSize x TerrainSize over the same area, or empty. */
	TArray<float> Terrain;
	int32 TerrainSize = 0;
	/** Lidar surface and ground models on the terrain grid, or empty. */
	TArray<float> LidarDSM;
	TArray<float> LidarDTM;
	const FSimCruiseScanMapFacts* Map = nullptr;
};

/** One probability or value layer: Size x Size uint8, north-up, row 0 at the north edge of the guard border. */
struct FSimCruiseScanChannel
{
	FName Name;
	int32 Size = 0;
	TArray<uint8> Data;
	/** The eye's own trust for this tile, 0..1. */
	float Confidence = 0.f;
};

/** A point an eye finds (for example a tree crown), in output cells of the raster grid. */
struct FSimCruiseScanPoint
{
	FName Kind;
	float X = 0.f, Y = 0.f;
	float Radius = 0.f;
	float Confidence = 0.f;
};

class ISimCruiseScanEye
{
public:
	virtual ~ISimCruiseScanEye() = default;
	/** For example "satlas_landcover". */
	virtual FName Name() const = 0;
	virtual int32 Version() const = 0;
	/** The weights' licence and credit (the hard gate). */
	virtual FString Licence() const = 0;
	/** The known origin and licences of the training data (recorded, docs/data-sources.md). */
	virtual FString TrainingDataLicence() const = 0;
	/** False when the inputs lack what the eye needs (no photo, needs NIR ...). */
	virtual bool CanRun(const FSimCruiseScanInputs& Inputs) const = 0;
	/** Seconds per tile on the reference PC, 2 cores. */
	virtual double CostSecondsEstimate() const = 0;
	/** Worker thread only. */
	virtual bool Run(const FSimCruiseScanInputs& Inputs, TArray<FSimCruiseScanChannel>& OutChannels, TArray<FSimCruiseScanPoint>& OutPoints) = 0;
};
