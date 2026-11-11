// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Frame-time statistics over a sampling window. All times in milliseconds. */
struct SIMCRUISE_API FSimCruisePerfStats
{
	FString Label;
	int32 Frames = 0;
	double Seconds = 0.0;
	double FrameAvg = 0.0;
	double FrameP50 = 0.0;
	double FrameP95 = 0.0;
	double FrameP99 = 0.0;
	double GpuAvg = 0.0;
	double GpuP95 = 0.0;
	double GameAvg = 0.0;
	double RenderAvg = 0.0;
	FIntPoint Output = FIntPoint::ZeroValue;
	float ScreenPercentage = 0.0f;

	double Fps() const { return FrameAvg > 0.0 ? 1000.0 / FrameAvg : 0.0; }
	FString ToLogLine() const;

	/** Percentile (0..100) of a set of samples, nearest-rank. */
	static double Percentile(TArray<double> Samples, double Percent);
};

/**
 * Samples frame, GPU, game- and render-thread times each frame for a fixed window
 * and logs one "SimCruisePerf:" line (also appended to Saved/Profiling/SimCruisePerf.csv).
 * GPU time is the RHI's timestamp-query frame time (what "stat unit" shows as GPU).
 */
class SIMCRUISE_API FSimCruisePerfSampler
{
public:
	/** Starts a window; OnDone is called on the game thread with the result. */
	static void Start(const FString& Label, float Seconds, TFunction<void(const FSimCruisePerfStats&)> OnDone = nullptr);
	static bool IsRunning();
};
