// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruisePerfSampler.h"

#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "RHIGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruisePerf, Log, All);

namespace
{
	struct FWindow
	{
		FString Label;
		float Seconds = 0.0f;
		double Elapsed = 0.0;
		TArray<double> Frame;
		TArray<double> Gpu;
		TArray<double> Game;
		TArray<double> Render;
		TFunction<void(const FSimCruisePerfStats&)> OnDone;
		FTSTicker::FDelegateHandle Handle;
	};

	TUniquePtr<FWindow> GWindow;

	double Average(const TArray<double>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		double Sum = 0.0;
		for (double V : Values)
		{
			Sum += V;
		}
		return Sum / Values.Num();
	}

	void Finish()
	{
		FWindow& W = *GWindow;
		FSimCruisePerfStats Stats;
		Stats.Label = W.Label;
		Stats.Frames = W.Frame.Num();
		Stats.Seconds = W.Elapsed;
		Stats.FrameAvg = Average(W.Frame);
		Stats.FrameP50 = FSimCruisePerfStats::Percentile(W.Frame, 50.0);
		Stats.FrameP95 = FSimCruisePerfStats::Percentile(W.Frame, 95.0);
		Stats.FrameP99 = FSimCruisePerfStats::Percentile(W.Frame, 99.0);
		Stats.GpuAvg = Average(W.Gpu);
		Stats.GpuP95 = FSimCruisePerfStats::Percentile(W.Gpu, 95.0);
		Stats.GameAvg = Average(W.Game);
		Stats.RenderAvg = Average(W.Render);
		if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
		{
			Stats.Output = GEngine->GameViewport->Viewport->GetSizeXY();
		}
		if (IConsoleVariable* ScreenPercentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
		{
			Stats.ScreenPercentage = ScreenPercentage->GetFloat();
		}

		const FString Line = Stats.ToLogLine();
		UE_LOG(LogSimCruisePerf, Display, TEXT("%s"), *Line);

		const FString CsvPath = FPaths::ProjectSavedDir() / TEXT("Profiling") / TEXT("SimCruisePerf.csv");
		if (!FPaths::FileExists(CsvPath))
		{
			FFileHelper::SaveStringToFile(TEXT("label,adapter,output,screen_pct,frames,fps,frame_avg,frame_p50,frame_p95,frame_p99,gpu_avg,gpu_p95,game_avg,render_avg\n"), *CsvPath);
		}
		const FString Row = FString::Printf(TEXT("%s,%s,%dx%d,%.1f,%d,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n"),
			*Stats.Label, *GRHIAdapterName.Replace(TEXT(","), TEXT(" ")), Stats.Output.X, Stats.Output.Y, Stats.ScreenPercentage, Stats.Frames, Stats.Fps(),
			Stats.FrameAvg, Stats.FrameP50, Stats.FrameP95, Stats.FrameP99, Stats.GpuAvg, Stats.GpuP95, Stats.GameAvg, Stats.RenderAvg);
		FFileHelper::SaveStringToFile(Row, *CsvPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);

		TFunction<void(const FSimCruisePerfStats&)> Done = MoveTemp(W.OnDone);
		FTSTicker::GetCoreTicker().RemoveTicker(W.Handle);
		GWindow.Reset();
		if (Done)
		{
			Done(Stats);
		}
	}

	bool TickWindow(float DeltaSeconds)
	{
		if (!GWindow)
		{
			return false;
		}
		FWindow& W = *GWindow;
		W.Elapsed += FApp::GetDeltaTime();
		W.Frame.Add(FApp::GetDeltaTime() * 1000.0);
		W.Gpu.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
		W.Game.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
		W.Render.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		if (W.Elapsed >= W.Seconds)
		{
			Finish();
			return false;
		}
		return true;
	}
}

FString FSimCruisePerfStats::ToLogLine() const
{
	return FString::Printf(
		TEXT("SimCruisePerf: label=%s output=%dx%d screen_pct=%.0f frames=%d fps=%.1f frame_ms avg=%.2f p50=%.2f p95=%.2f p99=%.2f gpu_ms avg=%.2f p95=%.2f game_ms=%.2f render_ms=%.2f"),
		*Label, Output.X, Output.Y, ScreenPercentage, Frames, Fps(), FrameAvg, FrameP50, FrameP95, FrameP99, GpuAvg, GpuP95, GameAvg, RenderAvg);
}

double FSimCruisePerfStats::Percentile(TArray<double> Samples, double Percent)
{
	if (Samples.IsEmpty())
	{
		return 0.0;
	}
	Samples.Sort();
	const int32 Rank = FMath::Clamp(FMath::CeilToInt(Percent / 100.0 * Samples.Num()) - 1, 0, Samples.Num() - 1);
	return Samples[Rank];
}

void FSimCruisePerfSampler::Start(const FString& Label, float Seconds, TFunction<void(const FSimCruisePerfStats&)> OnDone)
{
	if (GWindow)
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GWindow->Handle);
	}
	GWindow = MakeUnique<FWindow>();
	GWindow->Label = Label;
	GWindow->Seconds = FMath::Max(Seconds, 0.5f);
	GWindow->OnDone = MoveTemp(OnDone);
	GWindow->Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickWindow));
}

bool FSimCruisePerfSampler::IsRunning()
{
	return GWindow.IsValid();
}
