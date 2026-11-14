// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The local AI detail engine: a neural super-resolution network that runs on the
 * player's own GPU through Unreal's Neural Network Engine (NNE, ONNX Runtime with
 * DirectML), with no cloud and no key.
 *
 * It adds detail to the aerial photographs the ground is shaded with: each zoom-14
 * photograph (1024 px for 2.2 km, about 2 m a pixel) goes through Real-ESRGAN General
 * x4v3 (BSD-3, Xintao Wang et al.; ONNX export by Qualcomm AI Hub) in 128 px windows
 * with an 8 px overlap, and comes out at OutputScale times the pixels. That runs once
 * per photograph, on a worker thread while the tile streams in, and the result is
 * kept under Saved/NeuralDetail, so a place costs its inference only the first time
 * and nothing while you drive.
 *
 * Model: data/ai/real-esrgan-general-x4v3.onnx (weights embedded by
 * scripts/ai/embed_onnx_external.py). Command line: -SimCruiseNoNeural turns it off,
 * -SimCruiseNeuralScale=2|4 sets the kept resolution (default 2: 2048 px photographs).
 * Console: SimCruise.Neural.Bench <image> times one photograph and logs the GPU cost.
 *
 * Licence: code and weights BSD 3-Clause (licenses/Real-ESRGAN-BSD-3-Clause.txt); the project's
 * rule is the makers' licence for the weights. Without the model file, or with no NNE runtime,
 * aerial photographs get a classic unsharp mask instead (UnsharpMask).
 */
class SIMCRUISE_API FSimCruiseNeuralDetail
{
public:
	static FSimCruiseNeuralDetail& Get();

	/** Loads the model on the GPU runtime. Game thread (it creates a UObject). False when unavailable. */
	bool Initialize();

	bool IsReady() const { return bReady; }

	/**
	 * Replaces Pixels (Width x Height, BGRA) with the enhanced photograph of SourcePath when the
	 * cache has it (any thread). Otherwise it leaves the pixels alone, so the tile shows at once,
	 * queues the photograph for the background worker and returns false; OnEnhanced reports
	 * the result on the game thread.
	 */
	bool EnhanceAerial(const FString& SourcePath, TArray<FColor>& Pixels, int32& Width, int32& Height);

	/** Game thread: a queued photograph is enhanced (key = its file name, as the streamer's AerialKey). */
	TFunction<void(const FString& Key, TArray<TArray<FColor>>& Mips, int32 Width, int32 Height)> OnEnhanced;

	/**
	 * GPU share the background worker may take: after each window it rests (1 / Duty - 1) times
	 * the window's own time, so the frame rate keeps most of the GPU. 1 = no rest.
	 */
	float Duty = 0.35f;

	/** Runs the network over an image without the cache. Seconds of inference in OutSeconds. */
	bool Upscale(const TArray<FColor>& In, int32 Width, int32 Height, TArray<FColor>& Out, int32& OutWidth, int32& OutHeight, double& OutSeconds);

	/** Window and overlap of the tiled inference, input pixels. */
	static constexpr int32 Window = 128;
	static constexpr int32 Overlap = 8;
	static constexpr int32 ModelScale = 4;

	/** Number of network runs for an image (windows of Window - 2 * Overlap core pixels). */
	static int32 WindowCount(int32 Width, int32 Height);

	/** Box-filters an image down by an integer factor. */
	static void Downsample(const TArray<FColor>& In, int32 Width, int32 Height, int32 Factor, TArray<FColor>& Out);

	/**
	 * The fallback when the model is not there or no NNE runtime loads: a classic luminance-only
	 * unsharp mask (the photograph minus a Gaussian blur of SigmaPx, times Amount, added back,
	 * leaving differences under Threshold alone as JPEG noise). Pure, any thread; about 15 ms for
	 * a 1024 px photograph.
	 */
	static void UnsharpMask(TArray<FColor>& Pixels, int32 Width, int32 Height, float Amount, float SigmaPx = 1.0f, int32 Threshold = 3);

	FString Describe() const;

private:
	FString CachePath(const FString& SourcePath) const;
	void RunWorker();
	void Process(const FString& SourcePath);

	FCriticalSection QueueLock;
	TArray<FString> Queue;
	TSet<FString> Queued;
	bool bWorkerRunning = false;

	bool bTried = false;
	bool bReady = false;
	bool bSharpenFallback = false;
	int32 OutputScale = 2;
	FString RuntimeName;
	FCriticalSection RunLock;
	TSharedPtr<class FSimCruiseNeuralModel> Model;
	/** Loaded on the game thread in Initialize; the worker only uses it. */
	class IImageWrapperModule* ImageWrappers = nullptr;
	double TotalSeconds = 0.0;
	int32 Images = 0;
};
