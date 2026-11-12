// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** The 19 street-scene classes of the parser (Cityscapes order). */
enum class ESimCruiseSceneClass : uint8
{
	Road, Sidewalk, Building, Wall, Fence, Pole, TrafficLight, TrafficSign, Vegetation, Terrain,
	Sky, Person, Rider, Car, Truck, Bus, Train, Motorcycle, Bicycle, Count
};

/**
 * The local street-scene parser: a semantic segmentation network that labels every pixel of
 * a street photograph (building, wall, sky, vegetation, road, car, pole, sign ...), run on the
 * CPU and system RAM through Unreal's Neural Network Engine (NNERuntimeORTCpu), so the
 * power-capped GPU stays free.
 *
 * Model: DDRNet23-Slim (Hong et al. 2021; code MIT) trained on Cityscapes, ONNX export by
 * Qualcomm AI Hub, weights embedded in data/ai/ddrnet23-slim-cityscapes.onnx
 * (scripts/ai/embed_onnx_external.py). Input 1x3x1024x2048 in 0..1, output 19 class scores
 * at an eighth of that. A photograph is letterboxed into the input; the labels come back on a
 * grid over the photograph (OutWidth x OutHeight, an eighth of the letterboxed size).
 *
 * Use: the place graph measures a building's wall colour only on the pixels labelled building
 * or wall (SimCruisePlacePhotos::WallColourMasked), not on sky, trees, cars or shadow.
 *
 * The instance lives on the heap and is never destroyed: an ONNX Runtime environment torn
 * down at process exit waits on its worker threads under the loader lock and hangs the exit.
 */
class SIMCRUISE_API FSimCruiseSceneParser
{
public:
	static FSimCruiseSceneParser& Get();

	/** Loads the model (game thread: it creates a UObject). False when unavailable. */
	bool Initialize();

	bool IsReady() const { return bReady; }

	/**
	 * Labels a photograph (BGRA, Width x Height). OutLabels holds one ESimCruiseSceneClass per
	 * cell of an OutWidth x OutHeight grid laid over the whole photograph. Workers only (refused on
	 * the game thread); one run at a time, a second caller gets false at once.
	 */
	bool Parse(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutLabels, int32& OutWidth, int32& OutHeight, double* OutSeconds = nullptr);

	/**
	 * For callers on the game thread (commandlets): runs Parse on a worker and waits at most
	 * TimeoutSeconds. A run that does not answer turns the parser off for the rest of the
	 * process, so a hang inside the runtime costs one timeout, never the run.
	 */
	bool ParseWithTimeout(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutLabels, int32& OutWidth, int32& OutHeight, double TimeoutSeconds = 90.0);

	/** The class of the photograph's pixel (X, Y) in a label grid from Parse. */
	static ESimCruiseSceneClass ClassAt(const TArray<uint8>& Labels, int32 GridWidth, int32 GridHeight, int32 X, int32 Y, int32 Width, int32 Height);

	/** Fraction of a label grid in each class. */
	static TArray<float> Shares(const TArray<uint8>& Labels);

	static const TCHAR* ClassName(ESimCruiseSceneClass Class);

	/** Parses an image file and saves photo | labels as Saved/SceneParse/<name>-parse.png (any thread, after Initialize). */
	bool ParseFileToPicture(const FString& File);

	static constexpr int32 InputWidth = 2048;
	static constexpr int32 InputHeight = 1024;
	static constexpr int32 OutputScale = 8;

private:
	FSimCruiseSceneParser() = default;

	bool bTried = false;
	bool bReady = false;
	bool bTimedOut = false;
	FCriticalSection RunLock;
	TSharedPtr<class FSimCruiseSceneModel> Model;
	class IImageWrapperModule* ImageWrappers = nullptr;
};
