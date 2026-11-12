// Copyright SimCruise contributors. All Rights Reserved.

#include "Live/SimCruiseLiveWorldSubsystem.h"

#include "Engine/World.h"
#include "Environment/SimCruiseDaylightModel.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/SimCruiseHUD.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Live/SimCruiseLiveSky.h"
#include "Live/SimCruiseNeuralDetail.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "UObject/UObjectIterator.h"
#include "World/SimCruiseWorldResources.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseLive, Log, All);

namespace SimCruiseLiveWorldPrivate
{
	constexpr double MoveRefetchKm = 5.0;
	constexpr int32 AircraftRadiusNm = 25;

	FAutoConsoleCommandWithWorldAndArgs GReport(
		TEXT("SimCruise.Live.Report"), TEXT("SimCruise.Live.Report: log the live real-world data and the state of each feed"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (USimCruiseLiveWorldSubsystem* Live = USimCruiseLiveWorldSubsystem::Get(World))
			{
				UE_LOG(LogSimCruiseLive, Display, TEXT("%s"), *Live->Report());
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GShot(
		TEXT("SimCruise.Live.Shot"), TEXT("SimCruise.Live.Shot <name> [fov]: save a frame from the camera toward the nearest live aircraft"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USimCruiseLiveWorldSubsystem* Live = USimCruiseLiveWorldSubsystem::Get(World))
			{
				Live->Shot(Args.Num() > 0 ? Args[0] : TEXT("live-aircraft"), Args.Num() > 1 ? FCString::Atof(*Args[1]) : 20.0f);
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GRefresh(
		TEXT("SimCruise.Live.Refresh"), TEXT("SimCruise.Live.Refresh: read every live feed again now"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (USimCruiseLiveWorldSubsystem* Live = USimCruiseLiveWorldSubsystem::Get(World))
			{
				Live->RefreshAll();
			}
		}));
}

using namespace SimCruiseLiveWorldPrivate;

const TCHAR* USimCruiseLiveWorldSubsystem::FeedName(EFeed Feed)
{
	switch (Feed)
	{
	case EFeed::Weather: return TEXT("weather");
	case EFeed::Metar: return TEXT("metar");
	case EFeed::Air: return TEXT("air");
	case EFeed::Sea: return TEXT("sea");
	case EFeed::TideStations: return TEXT("tide-stations");
	case EFeed::Tide: return TEXT("tide");
	case EFeed::Aircraft: return TEXT("aircraft");
	case EFeed::State: return TEXT("state");
	case EFeed::Roads: return TEXT("roads");
	case EFeed::Photos: return TEXT("photos");
	default: return TEXT("?");
	}
}

double USimCruiseLiveWorldSubsystem::PeriodSeconds(EFeed Feed)
{
	switch (Feed)
	{
	case EFeed::Weather: return 600.0;
	case EFeed::Metar: return 600.0;
	case EFeed::Air: return 1800.0;
	case EFeed::Sea: return 1800.0;
	case EFeed::TideStations: return 30.0 * 86400.0;
	case EFeed::Tide: return 360.0;
	case EFeed::Aircraft: return 10.0;
	case EFeed::State: return 7.0 * 86400.0;
	case EFeed::Roads: return 7.0 * 86400.0;
	case EFeed::Photos: return 7.0 * 86400.0;
	default: return 600.0;
	}
}

bool USimCruiseLiveWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

USimCruiseLiveWorldSubsystem* USimCruiseLiveWorldSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<USimCruiseLiveWorldSubsystem>() : nullptr;
}

TStatId USimCruiseLiveWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCruiseLiveWorldSubsystem, STATGROUP_Tickables);
}

void USimCruiseLiveWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	// The local AI detail engine loads before the first tile streams (it needs the game thread).
	if (InWorld.IsGameWorld() && !GIsAutomationTesting)
	{
		FSimCruiseNeuralDetail::Get().Initialize();
		// A sharper photograph from the background worker replaces the one the tiles draw now.
		TWeakObjectPtr<UWorld> WeakWorld(&InWorld);
		FSimCruiseNeuralDetail::Get().OnEnhanced = [WeakWorld](const FString& Key, const TArray<FColor>& Pixels, int32 Width, int32 Height)
		{
			UWorld* World = WeakWorld.Get();
			USimCruiseWorldSubsystem* WorldSubsystem = World ? World->GetSubsystem<USimCruiseWorldSubsystem>() : nullptr;
			ASimCruiseWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
			if (!Streamer || !Streamer->Resources)
			{
				return;
			}
			UTexture2D* New = nullptr;
			UTexture2D* Old = Streamer->Resources->ReplaceAerial(Key, Pixels, Width, Height, New);
			if (!Old || !New)
			{
				return;
			}
			int32 Swapped = 0;
			for (TObjectIterator<UMaterialInstanceDynamic> It; It; ++It)
			{
				UTexture* Current = nullptr;
				if (It->GetWorld() == World && It->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Aerial")), Current) && Current == Old)
				{
					It->SetTextureParameterValue(TEXT("Aerial"), New);
					++Swapped;
				}
			}
			UE_LOG(LogSimCruiseLive, Display, TEXT("AI detail: %s now drawn at %dx%d on %d ground materials"), *Key, Width, Height, Swapped);
		};
	}
	const bool bForce = FParse::Param(Cmd, TEXT("SimCruiseLive"));
	bEnabled = InWorld.IsGameWorld() && !FParse::Param(Cmd, TEXT("SimCruiseNoLive")) && !FParse::Param(Cmd, TEXT("SimCruiseNoEnvironment"))
		&& (bForce || (!FApp::IsUnattended() && !GIsAutomationTesting));
	if (!bEnabled)
	{
		return;
	}
	FString Ignored;
	float IgnoredFloat = 0.0f;
	bWeatherFromCommandLine = FParse::Value(Cmd, TEXT("SimCruiseConditions="), Ignored) || FParse::Value(Cmd, TEXT("SimCruiseCloud="), IgnoredFloat)
		|| FParse::Value(Cmd, TEXT("SimCruiseVisibilityKm="), IgnoredFloat) || FParse::Param(Cmd, TEXT("SimCruiseRain"));
	// -SimCruiseLiveShots=<s>: a telephoto shot toward the nearest aircraft every s seconds (play tests).
	if (FParse::Value(Cmd, TEXT("SimCruiseLiveShots="), ShotEverySeconds))
	{
		NextShotAt = ShotEverySeconds;
	}
	UE_LOG(LogSimCruiseLive, Display, TEXT("Live world on%s."), bWeatherFromCommandLine ? TEXT(" (weather chosen on the command line)") : TEXT(""));
}

void USimCruiseLiveWorldSubsystem::Deinitialize()
{
	for (const FHttpRequestPtr& Request : Requests)
	{
		if (Request.IsValid())
		{
			Request->OnProcessRequestComplete().Unbind();
			Request->CancelRequest();
		}
	}
	Requests.Reset();
	Super::Deinitialize();
}

bool USimCruiseLiveWorldSubsystem::ObserverLonLat(double& OutLat, double& OutLon) const
{
	const UWorld* World = GetWorld();
	const USimCruiseWorldSubsystem* WorldSubsystem = World ? World->GetSubsystem<USimCruiseWorldSubsystem>() : nullptr;
	const ASimCruiseWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (Streamer && Streamer->GetFrame().IsValid())
	{
		FVector At = FVector::ZeroVector;
		if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
		{
			At = Pawn->GetActorLocation();
		}
		else if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(World, 0))
		{
			At = Camera->GetCameraLocation();
		}
		Streamer->GetFrame().WorldCmToLonLat(At, OutLon, OutLat);
		return true;
	}
	if (const USimCruiseEnvironmentSubsystem* EnvSubsystem = USimCruiseEnvironmentSubsystem::Get(World))
	{
		if (const ASimCruiseEnvironment* Env = EnvSubsystem->GetEnvironment())
		{
			OutLat = Env->LatitudeDeg;
			OutLon = Env->LongitudeDeg;
			return true;
		}
	}
	return false;
}

void USimCruiseLiveWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bEnabled)
	{
		return;
	}
	Clock += DeltaTime;
	if (ShotEverySeconds > 0.0f && Clock >= NextShotAt)
	{
		NextShotAt = Clock + ShotEverySeconds;
		Shot(FString::Printf(TEXT("live-aircraft-%d"), ++ShotCount), 20.0f);
	}

	double Lat = 0.0, Lon = 0.0;
	if (!ObserverLonLat(Lat, Lon))
	{
		return;
	}

	UWorld* World = GetWorld();
	USimCruiseWorldSubsystem* WorldSubsystem = World->GetSubsystem<USimCruiseWorldSubsystem>();
	ASimCruiseWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (Streamer && !Sky)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("SimCruiseLiveSky");
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Sky = World->SpawnActor<ASimCruiseLiveSky>(ASimCruiseLiveSky::StaticClass(), FTransform::Identity, Params);
		if (Sky && !Aircraft.IsEmpty())
		{
			Sky->SetAircraft(Aircraft, Feeds[static_cast<int32>(EFeed::Aircraft)].LastOkUtc);
		}
		TWeakObjectPtr<USimCruiseLiveWorldSubsystem> Weak(this);
		RebaseHandle = Streamer->OnRebased.AddLambda([Weak](const FVector& Offset)
		{
			if (Weak.IsValid() && Weak->Sky)
			{
				Weak->Sky->ShiftOrigin(Offset);
			}
		});
	}
	if (Sky)
	{
		if (const USimCruiseEnvironmentSubsystem* EnvSubsystem = USimCruiseEnvironmentSubsystem::Get(World))
		{
			if (const ASimCruiseEnvironment* Env = EnvSubsystem->GetEnvironment())
			{
				Sky->OriginElevationM = Env->ElevationM;
				Sky->SunElevationDeg = Env->GetSunPosition().ApparentElevationDeg;
				Sky->ShotEV100 = static_cast<float>(FSimCruiseDaylightModel::DisplayEV100(Env->GetReferenceEV100())) - Env->ExposureCompensation;
			}
		}
	}

	// The first place: the saved answers for it (within 5 km) apply at once.
	if (!bHaveFeedPlace)
	{
		FeedLat = Lat;
		FeedLon = Lon;
		bHaveFeedPlace = true;
		LoadCache(Lat, Lon);
		ApplyWeather();
		UE_LOG(LogSimCruiseLive, Display, TEXT("%s"), *Report());
	}
	// A new place: the slow feeds read again for it.
	else if (FSimCruiseLiveData::DistanceKm(FeedLat, FeedLon, Lat, Lon) > MoveRefetchKm)
	{
		FeedLat = Lat;
		FeedLon = Lon;
		for (EFeed Feed : {EFeed::Weather, EFeed::Metar, EFeed::Air, EFeed::Sea, EFeed::Tide, EFeed::State, EFeed::Roads, EFeed::Photos})
		{
			Feeds[static_cast<int32>(Feed)].NextAt = 0.0;
		}
		StateCode.Reset();
		Roads.Reset();
		Photos.Reset();
		TideStation.Reset();
		if (const FSimCruiseTideStation* Near = FSimCruiseLiveData::NearestTideStation(TideStations, Lat, Lon))
		{
			TideStation = *Near;
		}
		Sea.bTideObserved = false;
	}

	for (int32 Index = 0; Index < static_cast<int32>(EFeed::Count); ++Index)
	{
		const EFeed Feed = static_cast<EFeed>(Index);
		FFeedState& State = Feeds[Index];
		if (State.bInFlight || Clock < State.NextAt)
		{
			continue;
		}
		switch (Feed)
		{
		case EFeed::Weather: Fetch(Feed, FSimCruiseLiveData::WeatherUrl(FeedLat, FeedLon)); break;
		case EFeed::Metar: Fetch(Feed, FSimCruiseLiveData::MetarUrl(FeedLat, FeedLon)); break;
		case EFeed::Air: Fetch(Feed, FSimCruiseLiveData::AirUrl(FeedLat, FeedLon)); break;
		case EFeed::Sea: Fetch(Feed, FSimCruiseLiveData::MarineUrl(FeedLat, FeedLon)); break;
		case EFeed::TideStations: Fetch(Feed, FSimCruiseLiveData::TideStationsUrl()); break;
		case EFeed::Tide:
			if (TideStation.IsSet())
			{
				Fetch(Feed, FSimCruiseLiveData::TideUrl(TideStation->Id));
			}
			else
			{
				State.NextAt = Clock + 30.0; // no gauge near (or the list is not read yet)
			}
			break;
		case EFeed::State: Fetch(Feed, FSimCruiseLiveData::StateUrl(FeedLat, FeedLon)); break;
		case EFeed::Roads:
			if (!StateCode.IsEmpty())
			{
				Fetch(Feed, FSimCruiseLiveData::RoadsUrl(StateCode, FeedLat, FeedLon));
			}
			else
			{
				State.NextAt = Clock + 20.0; // the state first (or not in the US)
			}
			break;
		case EFeed::Photos: Fetch(Feed, FSimCruiseLiveData::PhotosUrl(FeedLat, FeedLon)); break;
		case EFeed::Aircraft:
			// Only where the world is drawn: the aircraft need the map frame.
			if (Sky)
			{
				Fetch(Feed, FSimCruiseLiveData::AircraftUrl(Lat, Lon, AircraftRadiusNm));
			}
			else
			{
				State.NextAt = Clock + 2.0;
			}
			break;
		default: break;
		}
	}
}

void USimCruiseLiveWorldSubsystem::Shot(const FString& Name, float FovDeg)
{
	const FString What = Sky ? Sky->StartShot(Name, FovDeg) : FString();
	UE_LOG(LogSimCruiseLive, Display, TEXT("Live shot %s: %s"), *Name, What.IsEmpty() ? TEXT("no aircraft above 3 degrees") : *What);
}

void USimCruiseLiveWorldSubsystem::RefreshAll()
{
	for (FFeedState& State : Feeds)
	{
		State.NextAt = 0.0;
	}
}

void USimCruiseLiveWorldSubsystem::Fetch(EFeed Feed, const FString& Url)
{
	FFeedState& State = Feeds[static_cast<int32>(Feed)];
	State.bInFlight = true;
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("SimCruise/2.0 (+https://github.com/simcruisegame/simcruise)"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(Feed == EFeed::TideStations ? 60.0f : 20.0f);
	TWeakObjectPtr<USimCruiseLiveWorldSubsystem> Weak(this);
	Request->OnProcessRequestComplete().BindLambda([Weak, Feed](FHttpRequestPtr Req, FHttpResponsePtr Response, bool bConnected)
	{
		USimCruiseLiveWorldSubsystem* Self = Weak.Get();
		if (!Self)
		{
			return;
		}
		Self->Requests.Remove(Req);
		FFeedState& S = Self->Feeds[static_cast<int32>(Feed)];
		S.bInFlight = false;
		if (!bConnected || !Response.IsValid() || Response->GetResponseCode() != 200)
		{
			S.LastError = Response.IsValid() ? FString::Printf(TEXT("HTTP %d"), Response->GetResponseCode()) : TEXT("no connection");
			S.Failures++;
			S.NextAt = Self->Clock + FMath::Min(PeriodSeconds(Feed), 15.0 * FMath::Pow(2.0, FMath::Min(S.Failures, 6)));
			if (S.Failures <= 2)
			{
				UE_LOG(LogSimCruiseLive, Display, TEXT("Live %s: %s"), FeedName(Feed), *S.LastError);
			}
			return;
		}
		Self->OnFeed(Feed, Response->GetContentAsString(), FDateTime::UtcNow(), false);
	});
	Requests.Add(Request);
	Request->ProcessRequest();
}

void USimCruiseLiveWorldSubsystem::OnFeed(EFeed Feed, const FString& Body, const FDateTime& ReceivedUtc, bool bFromCache)
{
	FFeedState& State = Feeds[static_cast<int32>(Feed)];
	FString Error;
	bool bOk = false;
	switch (Feed)
	{
	case EFeed::Weather:
		bOk = FSimCruiseLiveData::ParseWeather(Body, Weather, Error);
		break;
	case EFeed::Metar:
		bOk = FSimCruiseLiveData::ParseMetars(Body, Metars, Error);
		break;
	case EFeed::Air:
		bOk = FSimCruiseLiveData::ParseAir(Body, Air, Error);
		break;
	case EFeed::Sea:
	{
		FSimCruiseLiveSea Parsed = Sea;
		bOk = FSimCruiseLiveData::ParseMarine(Body, Parsed, Error);
		if (bOk)
		{
			Sea = Parsed;
		}
		else
		{
			Sea.bValid = false; // inland: no sea
		}
		break;
	}
	case EFeed::TideStations:
		bOk = FSimCruiseLiveData::ParseTideStations(Body, TideStations, Error);
		if (bOk && bHaveFeedPlace)
		{
			TideStation.Reset();
			if (const FSimCruiseTideStation* Near = FSimCruiseLiveData::NearestTideStation(TideStations, FeedLat, FeedLon))
			{
				TideStation = *Near;
				Feeds[static_cast<int32>(EFeed::Tide)].NextAt = 0.0;
			}
		}
		break;
	case EFeed::Tide:
	{
		float Level = 0.0f;
		FDateTime When;
		bOk = FSimCruiseLiveData::ParseTide(Body, Level, When, Error);
		if (bOk && TideStation.IsSet())
		{
			Sea.bTideObserved = true;
			Sea.TideM = Level;
			Sea.TideStation = TideStation->Name;
		}
		break;
	}
	case EFeed::State:
		bOk = FSimCruiseLiveData::ParseState(Body, StateCode, Error);
		if (bOk)
		{
			Feeds[static_cast<int32>(EFeed::Roads)].NextAt = FMath::Min(Feeds[static_cast<int32>(EFeed::Roads)].NextAt, Clock);
		}
		else if (!bFromCache)
		{
			// Outside the US: no road inventory; ask again only after a move.
			StateCode.Reset();
			Feeds[static_cast<int32>(EFeed::Roads)].NextAt = Clock + PeriodSeconds(EFeed::Roads);
		}
		break;
	case EFeed::Roads:
		bOk = FSimCruiseLiveData::ParseRoads(Body, Roads, Error);
		if (bOk)
		{
			UE_LOG(LogSimCruiseLive, Display, TEXT("Live roads: %d counted road sections (FHWA HPMS %s)"), Roads.Num(), *StateCode);
		}
		break;
	case EFeed::Photos:
		bOk = FSimCruiseLiveData::ParsePhotos(Body, Photos, Error);
		if (bOk)
		{
			UE_LOG(LogSimCruiseLive, Display, TEXT("Live photos: %d freely licensed photographs within 600 m (Wikimedia Commons)"), Photos.Num());
		}
		break;
	case EFeed::Aircraft:
		bOk = FSimCruiseLiveData::ParseAircraft(Body, Aircraft, Error);
		if (bOk && Sky)
		{
			Sky->SetAircraft(Aircraft, ReceivedUtc);
		}
		break;
	default:
		break;
	}

	if (!bOk)
	{
		State.LastError = Error;
		if (!bFromCache)
		{
			State.Failures++;
			State.NextAt = Clock + PeriodSeconds(Feed);
			UE_LOG(LogSimCruiseLive, Display, TEXT("Live %s: %s"), FeedName(Feed), *Error);
		}
		return;
	}
	State.LastOkUtc = ReceivedUtc;
	State.LastError.Reset();
	if (!bFromCache)
	{
		State.Failures = 0;
		State.NextAt = Clock + PeriodSeconds(Feed);
		if (Feed != EFeed::Aircraft)
		{
			FFileHelper::SaveStringToFile(Body, *CachePath(Feed), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			FFileHelper::SaveStringToFile(FString::Printf(TEXT("%.5f %.5f %lld"), FeedLat, FeedLon, ReceivedUtc.ToUnixTimestamp()),
				*(CachePath(Feed) + TEXT(".meta")));
		}
	}
	if (Feed == EFeed::Weather || Feed == EFeed::Metar || Feed == EFeed::Air)
	{
		ApplyWeather();
	}
	if (!bFromCache)
	{
		Announce();
	}
}

void USimCruiseLiveWorldSubsystem::ApplyWeather()
{
	NearMetar.Reset();
	if (bHaveFeedPlace)
	{
		if (const FSimCruiseMetar* Near = FSimCruiseLiveData::NearestMetar(Metars, FeedLat, FeedLon, FDateTime::UtcNow()))
		{
			FSimCruiseMetar Copy = *Near;
			Copy.Km = static_cast<float>(FSimCruiseLiveData::DistanceKm(FeedLat, FeedLon, Copy.Lat, Copy.Lon));
			NearMetar = Copy;
		}
	}
	if (bWeatherFromCommandLine || (!Weather.bValid && !NearMetar.IsSet() && !Air.bValid))
	{
		return;
	}
	const USimCruiseEnvironmentSubsystem* EnvSubsystem = USimCruiseEnvironmentSubsystem::Get(GetWorld());
	ASimCruiseEnvironment* Env = EnvSubsystem ? EnvSubsystem->GetEnvironment() : nullptr;
	if (!Env)
	{
		return;
	}
	const FSimCruiseWeatherConditions Fused = FSimCruiseLiveData::Fuse(Weather.bValid ? &Weather : nullptr, NearMetar.GetPtrOrNull(),
		Air.bValid ? &Air : nullptr, Env->GetWeather());
	Env->SetWeather(Fused);
}

void USimCruiseLiveWorldSubsystem::Announce()
{
	// Once the weather is in, tell the driver what the real place is like now.
	if (bAnnounced || !Weather.bValid)
	{
		return;
	}
	if (APlayerController* Controller = UGameplayStatics::GetPlayerController(GetWorld(), 0))
	{
		if (ASimCruiseHUD* Hud = Cast<ASimCruiseHUD>(Controller->GetHUD()))
		{
			bAnnounced = true;
			Hud->ShowMessage(TEXT("Live now: ") + FSimCruiseLiveData::Summary(&Weather, NearMetar.GetPtrOrNull(), Air.bValid ? &Air : nullptr,
				Sea.bValid ? &Sea : nullptr, Sky ? Sky->NumAircraft() : 0), 10.0f);
		}
	}
	UE_LOG(LogSimCruiseLive, Display, TEXT("%s"), *Report());
}

FString USimCruiseLiveWorldSubsystem::CachePath(EFeed Feed) const
{
	return FPaths::ProjectSavedDir() / TEXT("Live") / (FString(FeedName(Feed)) + TEXT(".json"));
}

void USimCruiseLiveWorldSubsystem::LoadCache(double Lat, double Lon)
{
	const FDateTime Now = FDateTime::UtcNow();
	for (EFeed Feed : {EFeed::TideStations, EFeed::Weather, EFeed::Metar, EFeed::Air, EFeed::Sea, EFeed::State, EFeed::Roads, EFeed::Photos})
	{
		FString Body, Meta;
		if (!FFileHelper::LoadFileToString(Body, *CachePath(Feed)) || !FFileHelper::LoadFileToString(Meta, *(CachePath(Feed) + TEXT(".meta"))))
		{
			continue;
		}
		TArray<FString> Parts;
		Meta.ParseIntoArrayWS(Parts);
		if (Parts.Num() < 3)
		{
			continue;
		}
		const FDateTime Saved = FDateTime::FromUnixTimestamp(FCString::Atoi64(*Parts[2]));
		const double AgeHours = (Now - Saved).GetTotalHours();
		// Old weather is no longer the real sky; the station list keeps for a month.
		const double MaxHours = Feed == EFeed::TideStations ? 24.0 * 30.0 : (Feed == EFeed::State || Feed == EFeed::Roads || Feed == EFeed::Photos) ? 24.0 * 7.0 : 6.0;
		// Saved data belongs to its place: another place's sky is not this one's.
		const bool bHere = Feed == EFeed::TideStations
			|| FSimCruiseLiveData::DistanceKm(Lat, Lon, FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1])) <= MoveRefetchKm;
		if (AgeHours < 0.0 || AgeHours > MaxHours || !bHere)
		{
			continue;
		}
		OnFeed(Feed, Body, Saved, true);
		// Fresh enough data waits for its own period; older data is read again at once.
		Feeds[static_cast<int32>(Feed)].NextAt = FMath::Max(0.0, Clock + PeriodSeconds(Feed) - AgeHours * 3600.0);
	}
	TideStation.Reset();
	if (const FSimCruiseTideStation* Near = FSimCruiseLiveData::NearestTideStation(TideStations, Lat, Lon))
	{
		TideStation = *Near;
	}
}

FDateTime USimCruiseLiveWorldSubsystem::LocalNow() const
{
	FDateTime Utc = FDateTime::UtcNow();
	if (const USimCruiseEnvironmentSubsystem* EnvSubsystem = USimCruiseEnvironmentSubsystem::Get(GetWorld()))
	{
		if (const ASimCruiseEnvironment* Env = EnvSubsystem->GetEnvironment())
		{
			Utc = Env->GetUtcTime();
		}
	}
	// The place's own offset (Open-Meteo, time zone and daylight saving), else solar time.
	const double Offset = Weather.bValid ? Weather.UtcOffsetSeconds : FeedLon / 15.0 * 3600.0;
	return Utc + FTimespan::FromSeconds(Offset);
}

bool USimCruiseLiveWorldSubsystem::TrafficAt(const FVector& WorldCm, float& OutPerKmLane, float& OutPerHour, FString* OutRoad) const
{
	const UWorld* World = GetWorld();
	const USimCruiseWorldSubsystem* WorldSubsystem = World ? World->GetSubsystem<USimCruiseWorldSubsystem>() : nullptr;
	const ASimCruiseWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (Roads.IsEmpty() || !Streamer || !Streamer->GetFrame().IsValid())
	{
		return false;
	}
	double Lat = 0.0, Lon = 0.0, Metres = 0.0;
	Streamer->GetFrame().WorldCmToLonLat(WorldCm, Lon, Lat);
	const FSimCruiseRoadSection* Road = FSimCruiseLiveData::NearestRoad(Roads, Lat, Lon, 40.0, Metres);
	if (!Road)
	{
		return false;
	}
	const USimCruiseEnvironmentSubsystem* EnvSubsystem = USimCruiseEnvironmentSubsystem::Get(World);
	const ASimCruiseEnvironment* Env = EnvSubsystem ? EnvSubsystem->GetEnvironment() : nullptr;
	const bool bRain = Env && Env->GetWeather().bRaining;
	const bool bSnow = Env && Env->GetWeather().bSnowing;
	OutPerHour = FSimCruiseLiveData::FlowPerHour(*Road, LocalNow(), bRain, bSnow);
	OutPerKmLane = FSimCruiseLiveData::DensityPerKmLane(*Road, OutPerHour);
	if (OutRoad)
	{
		*OutRoad = FString::Printf(TEXT("%s (%d a day, %d lanes)"), Road->Name.IsEmpty() ? *Road->RouteId : *Road->Name, Road->Aadt, Road->Lanes);
	}
	return true;
}

FString USimCruiseLiveWorldSubsystem::Report() const
{
	FString Out = FString::Printf(TEXT("Live world at %.4f,%.4f: %s"), FeedLat, FeedLon,
		*FSimCruiseLiveData::Summary(Weather.bValid ? &Weather : nullptr, NearMetar.GetPtrOrNull(), Air.bValid ? &Air : nullptr,
			Sea.bValid ? &Sea : nullptr, Sky ? Sky->NumAircraft() : 0));
	float PerKmLane = 0.0f, PerHour = 0.0f;
	FString Road;
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0); Pawn && TrafficAt(Pawn->GetActorLocation(), PerKmLane, PerHour, &Road))
	{
		Out += FString::Printf(TEXT("\n  traffic here at %s local: %.0f vehicles an hour, %.1f per km per lane on %s"),
			*LocalNow().ToString(TEXT("%H:%M %a")), PerHour, PerKmLane, *Road);
	}
	Out += FString::Printf(TEXT("\n  %d counted road sections, %d photographs of the place"), Roads.Num(), Photos.Num());
	const FDateTime Now = FDateTime::UtcNow();
	for (int32 Index = 0; Index < static_cast<int32>(EFeed::Count); ++Index)
	{
		const FFeedState& State = Feeds[Index];
		Out += FString::Printf(TEXT("\n  %-13s %s%s"), FeedName(static_cast<EFeed>(Index)),
			State.LastOkUtc.GetTicks() ? *FString::Printf(TEXT("ok %.0f s ago"), (Now - State.LastOkUtc).GetTotalSeconds()) : TEXT("not read"),
			State.LastError.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", last error: %s"), *State.LastError));
	}
	return Out;
}
