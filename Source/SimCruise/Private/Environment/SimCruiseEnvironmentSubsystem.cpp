// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseEnvironmentSubsystem.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseGraphics.h"
#include "Environment/SimCruiseLightingLab.h"
#include "Environment/SimCruisePerfSampler.h"
#include "Engine/Level.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseEnvironmentSubsystem, Log, All);

namespace
{
	ASimCruiseEnvironment* FindEnvironment(UWorld* World)
	{
		if (USimCruiseEnvironmentSubsystem* Subsystem = USimCruiseEnvironmentSubsystem::Get(World))
		{
			return Subsystem->GetEnvironment();
		}
		return nullptr;
	}

	void ApplyCommandLine(ASimCruiseEnvironment& Env)
	{
		const TCHAR* Cmd = FCommandLine::Get();
		double Latitude = Env.LatitudeDeg;
		double Longitude = Env.LongitudeDeg;
		double Elevation = Env.ElevationM;
		const bool bPlace = FParse::Value(Cmd, TEXT("SimCruiseLat="), Latitude) | FParse::Value(Cmd, TEXT("SimCruiseLon="), Longitude)
			| FParse::Value(Cmd, TEXT("SimCruiseElevation="), Elevation);
		if (bPlace)
		{
			Env.SetPlace(Latitude, Longitude, Elevation);
		}

		FString Iso;
		FDateTime Utc;
		if (FParse::Value(Cmd, TEXT("SimCruiseUtc="), Iso, false) && FDateTime::ParseIso8601(*Iso, Utc))
		{
			Env.SetUtcTime(Utc);
		}
		else if (FParse::Param(Cmd, TEXT("SimCruiseRealTime")))
		{
			Env.FollowSystemClock();
		}
		else
		{
			// Owner decision (11 Nov 2026): a drive starts in daylight, the real clock is an option.
			Env.StartInDaylight();
		}
		float TimeScale = 1.0f;
		if (FParse::Value(Cmd, TEXT("SimCruiseTimeScale="), TimeScale))
		{
			Env.bFollowSystemClock = false;
			Env.TimeScale = TimeScale;
		}

		FSimCruiseWeatherConditions Weather = Env.GetWeather();
		bool bWeather = false;
		FString ConditionsPath;
		if (FParse::Value(Cmd, TEXT("SimCruiseConditions="), ConditionsPath, false))
		{
			FString Json;
			FString Error;
			if (FFileHelper::LoadFileToString(Json, *ConditionsPath) && FSimCruiseWeather::ParseConditionsJson(Json, Weather, Error))
			{
				bWeather = true;
			}
			else
			{
				UE_LOG(LogSimCruiseEnvironmentSubsystem, Warning, TEXT("Could not read conditions %s: %s"), *ConditionsPath, *Error);
			}
		}
		float Cloud = 0.0f;
		if (FParse::Value(Cmd, TEXT("SimCruiseCloud="), Cloud))
		{
			Weather.CloudCover = Cloud;
			bWeather = true;
		}
		float VisibilityKm = 0.0f;
		if (FParse::Value(Cmd, TEXT("SimCruiseVisibilityKm="), VisibilityKm))
		{
			Weather.VisibilityM = VisibilityKm * 1000.0f;
			bWeather = true;
		}
		if (FParse::Param(Cmd, TEXT("SimCruiseRain")))
		{
			Weather.bRaining = true;
			bWeather = true;
		}
		if (bWeather)
		{
			Env.SetWeather(Weather);
		}
	}

	void ApplyGraphicsFromCommandLine()
	{
		// Once per process: the command line's preset, else the auto-pick for this GPU.
		// "-SimCruiseGraphics=keep" leaves Unreal's saved scalability alone.
		static bool bApplied = false;
		if (bApplied)
		{
			return;
		}
		bApplied = true;
		FString Choice = TEXT("auto");
		const bool bExplicit = FParse::Value(FCommandLine::Get(), TEXT("SimCruiseGraphics="), Choice);
		if (Choice.Equals(TEXT("keep"), ESearchCase::IgnoreCase))
		{
			return;
		}
		// Nothing to pick for without a renderer (-nullrhi tests, servers), and Play-In-Editor
		// shares the editor's own scalability: there only an explicit preset is applied.
		if (!bExplicit && (!FApp::CanEverRender() || GIsEditor))
		{
			return;
		}
		ESimCruiseGraphicsPreset Preset = ESimCruiseGraphicsPreset::High;
		if (Choice.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			Preset = FSimCruiseGraphics::AutoPickForThisMachine(true);
		}
		else if (!FSimCruiseGraphics::Parse(Choice, Preset))
		{
			UE_LOG(LogSimCruiseEnvironmentSubsystem, Warning, TEXT("Unknown graphics preset '%s'"), *Choice);
			return;
		}
		FSimCruiseGraphics::Apply(Preset);
	}

	FAutoConsoleCommandWithWorldAndArgs GTimeCommand(
		TEXT("SimCruise.Env.Time"),
		TEXT("SimCruise.Env.Time <ISO-8601 UTC, e.g. 2025-04-24T16:20:00Z> [timescale] | now: set the sky clock"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ASimCruiseEnvironment* Env = FindEnvironment(World);
			if (!Env || Args.IsEmpty())
			{
				return;
			}
			FDateTime Utc;
			if (Args[0].Equals(TEXT("now"), ESearchCase::IgnoreCase))
			{
				Env->FollowSystemClock();
			}
			else if (FDateTime::ParseIso8601(*Args[0], Utc))
			{
				Env->SetUtcTime(Utc);
				Env->TimeScale = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f;
			}
			UE_LOG(LogSimCruiseEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GPlaceCommand(
		TEXT("SimCruise.Env.Place"),
		TEXT("SimCruise.Env.Place <latitude> <longitude> [elevation m]: move the observer"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ASimCruiseEnvironment* Env = FindEnvironment(World);
			if (!Env || Args.Num() < 2)
			{
				return;
			}
			Env->SetPlace(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), Args.Num() > 2 ? FCString::Atod(*Args[2]) : 0.0);
			UE_LOG(LogSimCruiseEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GWeatherCommand(
		TEXT("SimCruise.Env.Weather"),
		TEXT("SimCruise.Env.Weather <cloud 0-1> [visibility km] [rain 0/1]: set the weather"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			ASimCruiseEnvironment* Env = FindEnvironment(World);
			if (!Env || Args.IsEmpty())
			{
				return;
			}
			FSimCruiseWeatherConditions Weather = Env->GetWeather();
			Weather.CloudCover = FCString::Atof(*Args[0]);
			if (Args.Num() > 1)
			{
				Weather.VisibilityM = FCString::Atof(*Args[1]) * 1000.0f;
			}
			if (Args.Num() > 2)
			{
				Weather.bRaining = FCString::Atoi(*Args[2]) != 0;
			}
			Env->SetWeather(Weather);
			UE_LOG(LogSimCruiseEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GLookCommand(
		TEXT("SimCruise.Env.Look"),
		TEXT("SimCruise.Env.Look <name> <value>: look development. Names: a cloud material scalar (Cloud_GlobalCoverage, Cloud_GlobalDensity), LayerBottomKm, LayerHeightKm, AerosolScaleHeightKm, AerosolAlbedo, MieAnisotropy, MultiScattering, ExposureCompensation, OpenStops, DaylightOpenStops, DriverDaylightOpenStops, LowPercent, CloseStops, Saturation"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (ASimCruiseEnvironment* Env = FindEnvironment(World); Env && Args.Num() >= 2)
			{
				Env->SetLookParameter(FName(*Args[0]), FCString::Atof(*Args[1]));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GReportCommand(
		TEXT("SimCruise.Env.Report"),
		TEXT("SimCruise.Env.Report: log place, time, sun, moon, exposure and weather"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (ASimCruiseEnvironment* Env = FindEnvironment(World))
			{
				UE_LOG(LogSimCruiseEnvironmentSubsystem, Display, TEXT("%s"), *Env->Describe());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GGraphicsCommand(
		TEXT("SimCruise.Graphics"),
		TEXT("SimCruise.Graphics <auto|low|medium|high|ultra>: apply a graphics preset"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			ESimCruiseGraphicsPreset Preset = ESimCruiseGraphicsPreset::High;
			if (Args.IsEmpty() || Args[0].Equals(TEXT("auto"), ESearchCase::IgnoreCase))
			{
				Preset = FSimCruiseGraphics::AutoPickForThisMachine(true);
			}
			else if (!FSimCruiseGraphics::Parse(Args[0], Preset))
			{
				return;
			}
			FSimCruiseGraphics::Apply(Preset);
		}));

	FAutoConsoleCommandWithWorldAndArgs GPerfCommand(
		TEXT("SimCruise.Perf"),
		TEXT("SimCruise.Perf <seconds> [label]: log frame, GPU, game and render thread times"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 10.0f;
			FSimCruisePerfSampler::Start(Args.Num() > 1 ? Args[1] : FSimCruiseGraphics::Name(FSimCruiseGraphics::Current()), Seconds);
		}));

	FAutoConsoleCommandWithWorldAndArgs GCaptureCommand(
		TEXT("SimCruise.Capture"),
		TEXT("SimCruise.Capture <name>: save the frame (no UI) to Saved/Screenshots/<name>.png"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld*)
		{
			const FString Name = Args.Num() > 0 ? Args[0] : TEXT("capture");
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (Name + TEXT(".png")), false, false);
		}));
}

bool USimCruiseEnvironmentSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

void USimCruiseEnvironmentSubsystem::Deinitialize()
{
	if (LevelAddedHandle.IsValid())
	{
		FWorldDelegates::LevelAddedToWorld.Remove(LevelAddedHandle);
		LevelAddedHandle.Reset();
	}
	Super::Deinitialize();
}

void USimCruiseEnvironmentSubsystem::HandleLevelAdded(ULevel* Level, UWorld* World)
{
	if (Level && World == GetWorld() && IsValid(Environment))
	{
		Environment->TakeOverLevelLighting(Level);
	}
}

USimCruiseEnvironmentSubsystem* USimCruiseEnvironmentSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<USimCruiseEnvironmentSubsystem>() : nullptr;
}

void USimCruiseEnvironmentSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	const TCHAR* Cmd = FCommandLine::Get();
	ApplyGraphicsFromCommandLine();
	if (FParse::Param(Cmd, TEXT("SimCruiseNoEnvironment")))
	{
		return;
	}

	for (TActorIterator<ASimCruiseEnvironment> It(&InWorld); It; ++It)
	{
		Environment = *It;
		break;
	}
	if (!Environment)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("SimCruiseEnvironment");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Environment = InWorld.SpawnActor<ASimCruiseEnvironment>(ASimCruiseEnvironment::StaticClass(), FTransform::Identity, Params);
	}
	if (!Environment)
	{
		return;
	}
	Environment->TakeOverLevelLighting();
	if (!LevelAddedHandle.IsValid())
	{
		LevelAddedHandle = FWorldDelegates::LevelAddedToWorld.AddUObject(this, &USimCruiseEnvironmentSubsystem::HandleLevelAdded);
	}
	ApplyCommandLine(*Environment);
	UE_LOG(LogSimCruiseEnvironmentSubsystem, Display, TEXT("SimCruise environment: %s"), *Environment->Describe());

	if (FParse::Param(Cmd, TEXT("SimCruiseLab")))
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("SimCruiseLightingLab");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Lab = InWorld.SpawnActor<ASimCruiseLightingLab>(ASimCruiseLightingLab::StaticClass(), FTransform::Identity, Params);
	}
}
