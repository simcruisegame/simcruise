// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseRouteSubsystem.h"

#include "Async/Async.h"
#include "Atlas/SimCruiseAtlasClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "HAL/PlatformTime.h"
#include "World/SimCruiseWorldStreamer.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseRoute, Log, All);

struct USimCruiseRouteSubsystem::FJob
{
	std::atomic<bool> bDone{ false };
	std::atomic<bool> bCancel{ false };
	bool bReroute = false;
	TArray<FSimCruiseRouteStop> Stops;
	FSimCruiseAtlasResult Result;
};

namespace
{
	const FSimCruiseAtlasConfig& RouteHelperConfig()
	{
		static const FSimCruiseAtlasConfig Config = FSimCruiseAtlasConfig::ResolveDefault();
		return Config;
	}
}

USimCruiseRouteSubsystem* USimCruiseRouteSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<USimCruiseRouteSubsystem>() : nullptr;
}

bool USimCruiseRouteSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USimCruiseRouteSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCruiseRouteSubsystem, STATGROUP_Tickables);
}

void USimCruiseRouteSubsystem::Deinitialize()
{
	if (Job.IsValid()) Job->bCancel = true;
	if (PreviewJob.IsValid()) PreviewJob->bCancel = true;
	Job.Reset();
	PreviewJob.Reset();
	Super::Deinitialize();
}

bool USimCruiseRouteSubsystem::CurrentFrame(FSimCruiseWorldFrame& Out) const
{
	const ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
	const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	if (!Stream || !Stream->GetFrame().IsValid()) return false;
	Out = Stream->GetFrame();
	return true;
}

bool USimCruiseRouteSubsystem::GetCarM(FVector2D& OutM, double* OutYawDeg) const
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Car = Controller ? Controller->GetPawn() : nullptr;
	if (!Car) return false;
	OutM = FVector2D(Car->GetActorLocation().X, Car->GetActorLocation().Y) / 100.0;
	if (OutYawDeg) *OutYawDeg = Car->GetActorRotation().Yaw;
	return true;
}

bool USimCruiseRouteSubsystem::StartStops(const TArray<FSimCruiseRouteStop>& Stops, TArray<FSimCruiseRouteStop>& OutWithStart)
{
	FSimCruiseWorldFrame Now;
	FVector2D CarM;
	if (!CurrentFrame(Now) || !GetCarM(CarM)) return false;
	FSimCruiseRouteStop Start;
	Start.Title = TEXT("You are here");
	Now.WorldCmToLonLat(FVector(CarM * 100.0, 0.0), Start.Lon, Start.Lat);
	OutWithStart.Reset();
	OutWithStart.Add(Start);
	OutWithStart.Append(Stops);
	return true;
}

void USimCruiseRouteSubsystem::StartRoute(const TArray<FSimCruiseRouteStop>& Stops)
{
	bPendingAfterTravel = false;
	TArray<FSimCruiseRouteStop> All;
	if (!StartStops(Stops, All))
	{
		Status = TEXT("The route can start when the car is on the road");
		StatusTime = 4.0;
		return;
	}
	Launch(All, false);
}

void USimCruiseRouteSubsystem::RequestPreview(const TArray<FSimCruiseRouteStop>& Stops)
{
	if (PreviewJob.IsValid()) PreviewJob->bCancel = true;
	PreviewJob.Reset();
	Preview.Clear();
	++PreviewSerial;
	TArray<FSimCruiseRouteStop> All;
	if (Stops.Num() == 0 || !StartStops(Stops, All)) return;
	PreviewJob = RunJob(All);
}

void USimCruiseRouteSubsystem::StartRouteAfterTravel(const TArray<FSimCruiseRouteStop>& Stops)
{
	ClearRoute();
	PendingStops = Stops;
	bPendingAfterTravel = true;
	PendingSince = FPlatformTime::Seconds();
	Status = TEXT("The route starts when the road is ready");
	StatusTime = 1e9;
}

void USimCruiseRouteSubsystem::ClearRoute()
{
	if (Job.IsValid()) Job->bCancel = true;
	Job.Reset();
	Route.Clear();
	bPendingAfterTravel = false;
	Status.Reset();
}

void USimCruiseRouteSubsystem::Launch(const TArray<FSimCruiseRouteStop>& StopsWithStart, bool bReroute)
{
	if (StopsWithStart.Num() < 2)
	{
		Status = TEXT("Add a stop to make a route");
		StatusTime = 3.0;
		return;
	}
	if (Job.IsValid()) Job->bCancel = true;
	Job = RunJob(StopsWithStart);
	if (!Job.IsValid())
	{
		Status = TEXT("The map helper is not installed, so there is no route");
		StatusTime = 5.0;
		return;
	}
	Job->bReroute = bReroute;
	Status = bReroute ? TEXT("Finding a new route") : TEXT("Finding the route");
	StatusTime = 1e9;
}

TSharedPtr<USimCruiseRouteSubsystem::FJob, ESPMode::ThreadSafe> USimCruiseRouteSubsystem::RunJob(const TArray<FSimCruiseRouteStop>& StopsWithStart)
{
	const FSimCruiseAtlasConfig& Config = RouteHelperConfig();
	if (!Config.IsConfigured()) return nullptr;
	TSharedPtr<FJob, ESPMode::ThreadSafe> NewJob = MakeShared<FJob, ESPMode::ThreadSafe>();
	NewJob->Stops = StopsWithStart;
	FSimCruiseAtlasRequest Request;
	Request.Command = TEXT("route");
	for (const FSimCruiseRouteStop& Stop : StopsWithStart)
	{
		Request.Args.Add(FString::Printf(TEXT("%.6f,%.6f"), Stop.Lon, Stop.Lat));
	}
	Request.TimeoutSeconds = 60.0;
	UE_LOG(LogSimCruiseRoute, Display, TEXT("Route request: %s"), *FString::Join(Request.Args, TEXT(" ")));
	Async(EAsyncExecution::ThreadPool, [NewJob, Config, Request]()
	{
		NewJob->Result = FSimCruiseAtlasClient::RunBlocking(Config, Request, &NewJob->bCancel);
		NewJob->bDone = true;
	});
	return NewJob;
}

bool USimCruiseRouteSubsystem::AcceptHelperAnswer(const FString& Raw, const TArray<FSimCruiseRouteStop>& StopsWithStart)
{
	FString Error;
	if (!Route.ParseHelperString(Raw, StopsWithStart, &Error))
	{
		Status = Error;
		StatusTime = 5.0;
		return false;
	}
	ProjectRoute();
	RouteAge = 0.0;
	Status.Reset();
	return true;
}

void USimCruiseRouteSubsystem::CollectJob()
{
	if (PreviewJob.IsValid() && PreviewJob->bDone)
	{
		const TSharedPtr<FJob, ESPMode::ThreadSafe> Done = PreviewJob;
		PreviewJob.Reset();
		FString Error;
		if (!Done->bCancel && Done->Result.IsOk() && Preview.ParseHelperString(Done->Result.Stdout, Done->Stops, &Error))
		{
			FSimCruiseWorldFrame Now;
			if (CurrentFrame(Now))
			{
				Preview.Project(FSimCruiseLonLatToM::CreateLambda([Now](double Lon, double Lat)
				{
					const FVector Cm = Now.LonLatToWorldCm(Lon, Lat);
					return FVector2D(Cm.X, Cm.Y) / 100.0;
				}));
			}
		}
		++PreviewSerial;
	}
	if (!Job.IsValid() || !Job->bDone) return;
	const TSharedPtr<FJob, ESPMode::ThreadSafe> Done = Job;
	Job.Reset();
	if (Done->bCancel) return;
	if (!Done->Result.IsOk())
	{
		UE_LOG(LogSimCruiseRoute, Warning, TEXT("Route request failed: %s"), *Done->Result.Describe());
		Status = TEXT("The route could not be found. Try again or move a stop.");
		StatusTime = 5.0;
		// A failed new route keeps the old one on screen.
		RerouteBlockedUntil = FPlatformTime::Seconds() + 15.0;
		return;
	}
	if (AcceptHelperAnswer(Done->Result.Stdout, Done->Stops))
	{
		UE_LOG(LogSimCruiseRoute, Display, TEXT("Route: %.0f m, %.0f s, %d legs in %.1f s"), Route.TotalLengthM(), Route.TotalDurationS(), Route.GetLegs().Num(), Done->Result.Seconds);
	}
	else
	{
		UE_LOG(LogSimCruiseRoute, Warning, TEXT("Route refused: %s"), *Status);
		RerouteBlockedUntil = FPlatformTime::Seconds() + 15.0;
	}
}

void USimCruiseRouteSubsystem::ProjectRoute()
{
	if (!CurrentFrame(Frame)) return;
	const FSimCruiseWorldFrame Use = Frame;
	Route.Project(FSimCruiseLonLatToM::CreateLambda([Use](double Lon, double Lat)
	{
		const FVector Cm = Use.LonLatToWorldCm(Lon, Lat);
		return FVector2D(Cm.X, Cm.Y) / 100.0;
	}));
}

void USimCruiseRouteSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	CollectJob();
	if (StatusTime < 1e8)
	{
		StatusTime -= DeltaTime;
		if (StatusTime <= 0.0) Status.Reset();
	}
	if (GetWorld() && GetWorld()->IsPaused()) return;
	if (bPendingAfterTravel)
	{
		const ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
		FVector2D CarM;
		if (Mode && Mode->IsDriveReady() && GetCarM(CarM) && FPlatformTime::Seconds() - PendingSince > 1.0)
		{
			const TArray<FSimCruiseRouteStop> Stops = PendingStops;
			StartRoute(Stops);
		}
		return;
	}
	// The world origin moves by whole tiles as the car drives: project again.
	FSimCruiseWorldFrame Now;
	if (Route.IsActive() && CurrentFrame(Now) && Now.OriginTile != Frame.OriginTile)
	{
		ProjectRoute();
	}
	FVector2D CarM;
	if (!GetCarM(CarM)) return;
	if (Route.IsActive() || !Route.GetArrivalMessage().IsEmpty())
	{
		RouteAge += DeltaTime;
		Route.Update(CarM, DeltaTime);
	}
	if (Route.IsActive() && Route.GetProgress().bOffRoute && !Job.IsValid() && FPlatformTime::Seconds() > RerouteBlockedUntil)
	{
		Route.ResetOffRoute();
		TArray<FSimCruiseRouteStop> Stops;
		FSimCruiseRouteStop Start;
		Start.Title = TEXT("You are here");
		Frame.WorldCmToLonLat(FVector(CarM * 100.0, 0.0), Start.Lon, Start.Lat);
		Stops.Add(Start);
		Stops.Append(Route.RemainingStops());
		UE_LOG(LogSimCruiseRoute, Display, TEXT("Off the route: a new route from the car"));
		Launch(Stops, true);
		RerouteBlockedUntil = FPlatformTime::Seconds() + 10.0;
	}
}
