// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseLatencyProbe.h"

#include "Engine/World.h"
#include "Features/IModularFeatures.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Performance/LatencyMarkerModule.h"
#include "Performance/MaxTickRateHandlerModule.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "World/SimCruiseWorldStreamer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseLatency, Log, All);

namespace
{
	/** Shipping builds write no log: the result lines also go to Saved/Profiling/SimCruiseLatency.log. */
	void Record(const FString& Line)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Profiling"), TEXT("SimCruiseLatency.log"));
		FFileHelper::SaveStringToFile(FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S ")) + Line + LINE_TERMINATOR, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
	}
}

namespace
{
	constexpr int32 Trials = 20;
	constexpr double RestSeconds = 1.5;
	constexpr double StepTimeoutSeconds = 1.0;
	constexpr double MovedRad = 0.0005; // 0.03 deg of rack: the first visible change

	ASimCruisePlayerController* GetSimPlayer(const UObject* Context)
	{
		return Cast<ASimCruisePlayerController>(UGameplayStatics::GetPlayerController(Context, 0));
	}
}

namespace
{
	TAutoConsoleVariable<int32> CVarFrameCap(TEXT("simcruise.FrameCap"), 60,
		TEXT("Frame-rate cap for the drive, frames per second (0 = no cap). Applied at the start of a drive (t.MaxFPS changes it at once)."));
	TAutoConsoleVariable<int32> CVarReflex(TEXT("simcruise.Reflex"), 0,
		TEXT("NVIDIA Reflex: 0 off (default), 1 low latency, 3 low latency + boost. Needs an NVIDIA GPU and driver 455 or newer. -SimCruiseReflex=<n> sets it at the start."));

	/**
	 * Release bug R6: with Reflex's sleep mode and latency markers on, the Shipping game hung in
	 * its shutdown (window gone, no CPU, never exited). Both are turned off before the engine exits.
	 */
	void DisableReflex()
	{
		for (IMaxTickRateHandlerModule* Handler : IModularFeatures::Get().GetModularFeatureImplementations<IMaxTickRateHandlerModule>(
				 IMaxTickRateHandlerModule::GetModularFeatureName()))
		{
			Handler->SetEnabled(false);
			Handler->SetFlags(0);
		}
		for (ILatencyMarkerModule* Markers : IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(
				 ILatencyMarkerModule::GetModularFeatureName()))
		{
			Markers->SetEnabled(false);
		}
	}

	void DisableReflexBeforeExit()
	{
		static bool bRegistered = false;
		if (!bRegistered)
		{
			bRegistered = true;
			FCoreDelegates::OnEnginePreExit.AddStatic(&DisableReflex);
		}
	}
}

void SimCruiseFramePacing::Apply()
{
	FString Ignored;
	const bool bMeasuring = FParse::Value(FCommandLine::Get(), TEXT("SimCruiseDrivePerf="), Ignored);
	if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
	{
		const int32 Cap = bMeasuring ? 0 : FMath::Max(0, CVarFrameCap.GetValueOnGameThread());
		// A cap the player set with t.MaxFPS on the command line or console wins.
		if (MaxFps->GetFloat() <= 0.f || bMeasuring)
		{
			MaxFps->Set(static_cast<float>(Cap), ECVF_SetByGameSetting);
		}
	}
	int32 Mode = CVarReflex.GetValueOnGameThread();
	FParse::Value(FCommandLine::Get(), TEXT("SimCruiseReflex="), Mode);
	Mode = FMath::Clamp(Mode, 0, 3);
	int32 Handlers = 0;
	// Off: the Reflex modules are not touched at all.
	if (Mode != 0)
	{
		DisableReflexBeforeExit();
		for (IMaxTickRateHandlerModule* Handler : IModularFeatures::Get().GetModularFeatureImplementations<IMaxTickRateHandlerModule>(
				 IMaxTickRateHandlerModule::GetModularFeatureName()))
		{
			if (Handler->GetAvailable())
			{
				Handler->SetEnabled(true);
				Handler->SetFlags(static_cast<uint32>(Mode));
				++Handlers;
			}
		}
		for (ILatencyMarkerModule* Markers : IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(
				 ILatencyMarkerModule::GetModularFeatureName()))
		{
			if (Markers->GetAvailable())
			{
				Markers->SetEnabled(true);
			}
		}
	}
	UE_LOG(LogSimCruiseLatency, Display, TEXT("Frame pacing: cap %s, Reflex mode %d (%s)"),
		bMeasuring ? TEXT("off (measuring)") : *FString::FromInt(CVarFrameCap.GetValueOnGameThread()), Mode,
		Mode == 0 ? TEXT("off") : Handlers > 0 ? TEXT("available") : TEXT("not available on this GPU"));
}

ASimCruiseLatencyProbe::ASimCruiseLatencyProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the vehicle has taken this frame's physics results: the state this frame draws.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ASimCruiseLatencyProbe::MarkerLatencyMs(float& OutMs)
{
	const TArray<ILatencyMarkerModule*> Modules =
		IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	for (ILatencyMarkerModule* Module : Modules)
	{
		if (Module && Module->GetAvailable() && Module->GetEnabled())
		{
			const float Ms = Module->GetTotalLatencyInMs();
			if (Ms > 0.f)
			{
				OutMs = Ms;
				return true;
			}
		}
	}
	return false;
}

void ASimCruiseLatencyProbe::BeginPlay()
{
	Super::BeginPlay();
	StageStarted = FPlatformTime::Seconds();
	const TArray<ILatencyMarkerModule*> Modules =
		IModularFeatures::Get().GetModularFeatureImplementations<ILatencyMarkerModule>(ILatencyMarkerModule::GetModularFeatureName());
	// The probe reads the game-to-render time from the Reflex latency markers; they and the
	// sleep mode are turned off before the engine exits (release bug R6).
	DisableReflexBeforeExit();
	for (ILatencyMarkerModule* Module : Modules)
	{
		if (Module && Module->GetAvailable() && !Module->GetEnabled())
		{
			Module->SetEnabled(true);
		}
	}
	// -SimCruiseReflex=<0|1|3> (read by SimCruiseFramePacing::Apply at the start of the drive).
	int32 ReflexMode = 0;
	FParse::Value(FCommandLine::Get(), TEXT("SimCruiseReflex="), ReflexMode);
	{ const FString RecordLine = FString::Printf(TEXT("Latency probe: waiting for the car (%d latency marker modules, Reflex mode %d)"), Modules.Num(), ReflexMode); UE_LOG(LogSimCruiseLatency, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
}

void ASimCruiseLatencyProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASimCruisePlayerController* Player = GetSimPlayer(this);
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const double Now = FPlatformTime::Seconds();
	switch (Stage)
	{
	case EStage::WaitCar:
	{
		const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
		const bool bPlaced = Mode ? Mode->IsDriveReady() : Pawn != nullptr;
		const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
		const bool bTiles = !Stream || (Pawn && Stream->AreTilesReadyAround(Pawn->GetActorLocation(), 1));
		if ((Pawn && bPlaced && bTiles) || Now - StageStarted > 400.0)
		{
			Stage = EStage::Settle;
			StageStarted = Now;
			SettleFrames = 0;
		}
		break;
	}
	case EStage::Settle:
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
		{
			StageStarted = Now;
			SettleFrames = 0;
			break;
		}
		if (++SettleFrames > 120 && Now - StageStarted > 8.0)
		{
			Stage = EStage::Rest;
			StageStarted = Now;
		}
		break;
	case EStage::Rest:
		if (!Player)
		{
			Finish();
			break;
		}
		Player->InjectAxis(EKeys::Gamepad_LeftX, 0.f);
		if (Now - StageStarted > RestSeconds)
		{
			if (Trial >= Trials)
			{
				Finish();
				break;
			}
			// The step: read by the controller at the start of the next frame.
			RestRackRad = Player->GetTelemetry().RackAngleRad;
			Player->InjectAxis(EKeys::Gamepad_LeftX, StickSign);
			InputSeconds = FPlatformTime::Seconds();
			InputFrame = GFrameCounter;
			Stage = EStage::Step;
			StageStarted = Now;
		}
		break;
	case EStage::Step:
	{
		if (!Player)
		{
			Finish();
			break;
		}
		Player->InjectAxis(EKeys::Gamepad_LeftX, StickSign);
		const double Rack = Player->GetTelemetry().RackAngleRad;
		if (FMath::Abs(Rack - RestRackRad) > MovedRad)
		{
			// This frame's game thread started at FApp::GetCurrentTime(); its picture is done
			// a game-to-render latency later.
			const double FrameStart = FApp::GetCurrentTime();
			const double ToFrameMs = FMath::Max(0.0, (FrameStart - InputSeconds) * 1000.0);
			float RenderMs = 0.f;
			const bool bMarkers = MarkerLatencyMs(RenderMs);
			if (!bMarkers)
			{
				static const IConsoleVariable* Lag = IConsoleManager::Get().FindConsoleVariable(TEXT("r.OneFrameThreadLag"));
				const double FrameMs = DeltaSeconds * 1000.0;
				const double GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
				RenderMs = static_cast<float>(FrameMs * (1.0 + (Lag && Lag->GetInt() > 0 ? 1.0 : 0.0)) + GpuMs);
			}
			const double TotalMs = ToFrameMs + RenderMs;
			const int64 Frames = static_cast<int64>(GFrameCounter - InputFrame);
			Samples.Add(TotalMs);
			FrameSamples.Add(static_cast<double>(Frames));
			{ const FString RecordLine = FString::Printf(TEXT("SimCruiseLatency: trial %d, %lld frames to the first moved rack, %.1f ms to that frame + %.1f ms game-to-render (%s) = %.1f ms; frame %.1f ms"),
				Trial + 1, Frames, ToFrameMs, RenderMs, bMarkers ? TEXT("Reflex markers") : TEXT("estimate"), TotalMs, DeltaSeconds * 1000.0); UE_LOG(LogSimCruiseLatency, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
			++Trial;
			StickSign = -StickSign;
			Stage = EStage::Rest;
			StageStarted = Now;
		}
		else if (Now - StageStarted > StepTimeoutSeconds)
		{
			{ const FString RecordLine = FString::Printf(TEXT("SimCruiseLatency: trial %d, the rack did not move within %.1f s"), Trial + 1, StepTimeoutSeconds); UE_LOG(LogSimCruiseLatency, Warning, TEXT("%s"), *RecordLine); Record(RecordLine); }
			++Trial;
			Stage = EStage::Rest;
			StageStarted = Now;
		}
		break;
	}
	default:
		break;
	}
}

void ASimCruiseLatencyProbe::Finish()
{
	Stage = EStage::Done;
	if (Samples.Num() > 0)
	{
		TArray<double> Sorted = Samples;
		Sorted.Sort();
		double Sum = 0.0, FrameSum = 0.0;
		for (double S : Samples) { Sum += S; }
		for (double F : FrameSamples) { FrameSum += F; }
		{ const FString RecordLine = FString::Printf(TEXT("SimCruiseLatency summary: %d trials, mean %.1f ms, median %.1f ms, max %.1f ms, mean %.1f frames"),
			Samples.Num(), Sum / Samples.Num(), Sorted[Sorted.Num() / 2], Sorted.Last(), FrameSum / FrameSamples.Num()); UE_LOG(LogSimCruiseLatency, Display, TEXT("%s"), *RecordLine); Record(RecordLine); }
	}
	else
	{
		{ const FString RecordLine = FString::Printf(TEXT("SimCruiseLatency summary: no trials measured")); UE_LOG(LogSimCruiseLatency, Error, TEXT("%s"), *RecordLine); Record(RecordLine); }
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseKeepOpen")))
	{
		return;
	}
	// Not the "quit" console command: a Shipping build did not act on it here.
	FPlatformMisc::RequestExit(false, TEXT("SimCruise release probe"));
}
