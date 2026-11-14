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
 * Model: the semantic head of Detectron2's Panoptic FPN R50 (code Apache 2.0, model-zoo weights CC BY-SA 3.0; COCO panoptic, labels
 * CC BY 4.0), ONNX export by Qualcomm AI Hub, fetched by scripts/ai/fetch_open_models.py into
 * Saved/AI (120 MB; a development tool, not committed or shipped). Input 1x3x800x800 in 0..1,
 * 54 class scores at a quarter of that, mapped to the 19 classes above. A photograph is
 * letterboxed into the input; the labels come back on a grid over the photograph.
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


private:
	FSimCruiseSceneParser() = default;

	bool bTried = false;
	bool bReady = false;
	bool bTimedOut = false;
	FCriticalSection RunLock;
	TSharedPtr<class FSimCruiseSceneModel> Model;
	class IImageWrapperModule* ImageWrappers = nullptr;
};
