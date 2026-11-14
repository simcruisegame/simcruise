// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruisePlaceGraphSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Async/Async.h"
#include "Misc/ScopeExit.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
#include "Knowledge/SimCruisePlaceAI.h"
#include "Knowledge/SimCruiseFurnitureData.h"
#include "Knowledge/SimCruiseRoadsideData.h"
#include "Knowledge/SimCruisePlaceFacts.h"
// The live feeds (Source/SimCruise/*/Live) join when that code is in the tree; until then
// other systems push live facts with SimCruisePlaceHub::SetLive.
#if __has_include("Live/SimCruiseLiveWorldSubsystem.h")
#include "Live/SimCruiseLiveData.h"
#include "Live/SimCruiseLiveWorldSubsystem.h"
#define SIMCRUISE_PLACE_GRAPH_LIVE 1
#else
#define SIMCRUISE_PLACE_GRAPH_LIVE 0
#endif
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeLock.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruisePlaceGraph, Log, All);

namespace
{
	FCriticalSection HubLock;
	FSimCruiseFactSet& LiveStore() { static FSimCruiseFactSet S; return S; }
	using FPlanRef = TSharedPtr<const FSimCruiseTilePlan, ESPMode::ThreadSafe>;
	TMap<FIntPoint, FPlanRef>& PlanStore() { static TMap<FIntPoint, FPlanRef> S; return S; }

	/** -SimCruisePlaceOffline: every request of the graph goes to a closed local port, to test that a start with no network neither stalls nor fails. */
	FString TestUrl(const FString& Url)
	{
		static const bool bOffline = FParse::Param(FCommandLine::Get(), TEXT("SimCruisePlaceOffline"));
		return bOffline ? FString(TEXT("http://127.0.0.1:9/")) : Url;
	}

	const TCHAR* UserAgent = TEXT("SimCruise/2.0 (+https://github.com/simcruisegame/simcruise)");

	FAutoConsoleCommandWithWorldAndArgs GExplain(
		TEXT("SimCruise.Knowledge.Explain"), TEXT("SimCruise.Knowledge.Explain [n]: the n buildings nearest the car, their facts and why each detail"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (const USimCruisePlaceGraphSubsystem* G = USimCruisePlaceGraphSubsystem::Get(World))
			{
				UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("%s"), *G->Explain(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 3));
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs GLive(
		TEXT("SimCruise.Knowledge.Live"), TEXT("SimCruise.Knowledge.Live: the live facts the graph holds now"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld*)
		{
			UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Live facts:\n%s"), *SimCruisePlaceHub::Live().Describe());
		}));

	FAutoConsoleCommandWithWorldAndArgs GReport(
		TEXT("SimCruise.Knowledge.Report"), TEXT("SimCruise.Knowledge.Report: tiles baked, records, fetches"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>&, UWorld* World)
		{
			if (const USimCruisePlaceGraphSubsystem* G = USimCruisePlaceGraphSubsystem::Get(World))
			{
				UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("%s"), *G->Report());
			}
		}));
}

// ---------------------------------------------------------------------------------------------
// Hub store

namespace SimCruisePlaceHub
{
	void SetLive(FName Fact, float Confidence, const FString& Why)
	{
		FScopeLock Lock(&HubLock);
		if (Confidence <= 0.0f) { LiveStore().Facts.Remove(Fact); return; }
		FSimCruiseFactSet::FEntry& E = LiveStore().Facts.FindOrAdd(Fact);
		E.Confidence = FMath::Clamp(Confidence, 0.0f, 1.0f);
		E.Why = Why;
	}

	void ClearLive(const TCHAR* Prefix)
	{
		FScopeLock Lock(&HubLock);
		for (auto It = LiveStore().Facts.CreateIterator(); It; ++It)
		{
			if (It.Key().ToString().StartsWith(Prefix)) It.RemoveCurrent();
		}
	}

	FSimCruiseFactSet Live()
	{
		FScopeLock Lock(&HubLock);
		return LiveStore();
	}

	void StorePlan(const FSimCruiseTilePlan& Plan)
	{
		FScopeLock Lock(&HubLock);
		PlanStore().Add(Plan.Tile, MakeShared<const FSimCruiseTilePlan, ESPMode::ThreadSafe>(Plan));
		// Keep the plans of the tiles around the car only (a tile holds a few hundred buildings).
		if (PlanStore().Num() > 64)
		{
			TArray<FIntPoint> Keys;
			PlanStore().GetKeys(Keys);
			Keys.Sort([&](const FIntPoint& A, const FIntPoint& B) { return (A - Plan.Tile).SizeSquared() > (B - Plan.Tile).SizeSquared(); });
			for (int32 i = 0; i < Keys.Num() - 48; ++i) PlanStore().Remove(Keys[i]);
		}
	}

	TSharedPtr<const FSimCruiseTilePlan, ESPMode::ThreadSafe> FindPlan(const FIntPoint& Tile)
	{
		FScopeLock Lock(&HubLock);
		const FPlanRef* P = PlanStore().Find(Tile);
		return P ? *P : FPlanRef();
	}

	void ForgetPlan(const FIntPoint& Tile)
	{
		FScopeLock Lock(&HubLock);
		PlanStore().Remove(Tile);
	}

	void BakeForBuild(const FSimCruiseTileData& Data, double LatDeg, double LonDeg, double SideM, FSimCruiseTilePlan& Out)
	{
		TArray<FSimCruisePlaceRecord> Records;
		SimCruisePlaceFacts::ForTile(Data.Id, Records);
		FSimCruiseAreaPrior Prior;
		FSimCruiseFactSet AreaFacts;
		if (SimCruisePlaceAI::ForArea(SimCruisePlaceAI::AreaOf(Data.Id), Prior)) SimCruisePlaceAI::ToFacts(Prior, AreaFacts);
		SimCruisePlaceGraph::BakeTile(SimCruisePlaceGraph::Rules(), Data, LatDeg, LonDeg, SideM, Records, Out, &AreaFacts);
		StorePlan(Out);
	}
}

// ---------------------------------------------------------------------------------------------
// Subsystem

bool USimCruisePlaceGraphSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void USimCruisePlaceGraphSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	const bool bForce = FParse::Param(Cmd, TEXT("SimCruiseLive"));
	bAI = !FParse::Param(Cmd, TEXT("SimCruiseNoPlaceAI"));
	bNetwork = !FParse::Param(Cmd, TEXT("SimCruiseNoLive")) && !FParse::Param(Cmd, TEXT("SimCruiseNoPlaceFetch"))
		&& (bForce || (!FApp::IsUnattended() && !GIsAutomationTesting));
	const FSimCruisePlaceRules& R = SimCruisePlaceGraph::Rules();
	UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Place graph: %d rules, %d live rules, %d regions, %d palettes; open-data fetch %s."),
		R.Rules.Num(), R.LiveRules.Num(), R.Regions.Num(), R.Palettes.Num(), bNetwork ? TEXT("on") : TEXT("off"));
}

void USimCruisePlaceGraphSubsystem::Deinitialize()
{
	// The run's summary: what was baked and fetched, and what the graph cost the game thread.
	if (GetWorld() && GetWorld()->IsGameWorld() && (Fetched > 0 || AIAreas > 0 || SlowestTickMs > 0.0)) UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("%s"), *Report());
	Super::Deinitialize();
}

TStatId USimCruisePlaceGraphSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCruisePlaceGraphSubsystem, STATGROUP_Tickables);
}

USimCruisePlaceGraphSubsystem* USimCruisePlaceGraphSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<USimCruisePlaceGraphSubsystem>() : nullptr;
}

bool USimCruisePlaceGraphSubsystem::ObserverLonLat(double& OutLat, double& OutLon) const
{
	const UWorld* World = GetWorld();
	const USimCruiseWorldSubsystem* WS = World ? World->GetSubsystem<USimCruiseWorldSubsystem>() : nullptr;
	const ASimCruiseWorldStreamer* Streamer = WS ? WS->GetStreamer() : nullptr;
	if (!Streamer || !Streamer->GetFrame().IsValid()) return false;
	FVector At = FVector::ZeroVector;
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0)) At = Pawn->GetActorLocation();
	else if (const APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0)) At = Cam->GetCameraLocation();
	Streamer->GetFrame().WorldCmToLonLat(At, OutLon, OutLat);
	return true;
}

void USimCruisePlaceGraphSubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	ON_SCOPE_EXIT { SlowestTickMs = FMath::Max(SlowestTickMs, (FPlatformTime::Seconds() - Now) * 1000.0); };
	if (Now >= NextLive)
	{
		NextLive = Now + 5.0;
		UpdateLiveFacts();
	}
	if (bCardShotPending && Now - CardStart > 1.2)
	{
		bCardShotPending = false;
		const FString Shot = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Knowledge/cards"), FString::Printf(TEXT("card-%d.png"), CardsShown.Num()));
		FScreenshotRequest::RequestScreenshot(Shot, true, false);
		UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("%s"), *Explain(1));
	}
	if (Now >= NextCardCheck)
	{
		NextCardCheck = Now + 0.5;
		const double C0 = FPlatformTime::Seconds();
		UpdateCard(Now);
		SlowestCardMs = FMath::Max(SlowestCardMs, (FPlatformTime::Seconds() - C0) * 1000.0);
	}
	if (bNetwork && !bFetching && Now >= NextFetch)
	{
		NextFetch = Now + 2.0;
		FetchAhead();
	}
	if (bNetwork && bAI && AIStep == EAIStep::Idle && Now >= NextAI)
	{
		NextAI = Now + 5.0;
		StartAI();
	}
}

void USimCruisePlaceGraphSubsystem::UpdateLiveFacts()
{
#if SIMCRUISE_PLACE_GRAPH_LIVE
	using namespace SimCruisePlaceHub;
	const USimCruiseLiveWorldSubsystem* Live = USimCruiseLiveWorldSubsystem::Get(GetWorld());
	if (!Live) return;
	auto Set = [](const TCHAR* F, bool bOn, float C, const FString& Why) { SetLive(FName(F), bOn ? C : 0.0f, Why); };

	const FSimCruiseLiveWeather& W = Live->GetWeather();
	if (W.bValid)
	{
		const FString When = W.TimeUtc.ToIso8601();
		const FString Src = FString::Printf(TEXT("Open-Meteo %s"), *When);
		Set(TEXT("live:day"), W.bIsDay, 1.0f, Src);
		Set(TEXT("live:night"), !W.bIsDay, 1.0f, Src);
		const bool bRain = FSimCruiseLiveData::IsRainCode(W.WmoCode) || W.PrecipitationMm > 0.1f;
		Set(TEXT("live:rain"), bRain, 1.0f, FString::Printf(TEXT("%s: %s, %.1f mm"), *Src, *FSimCruiseLiveData::DescribeWmo(W.WmoCode), W.PrecipitationMm));
		Set(TEXT("live:fog"), FSimCruiseLiveData::IsFogCode(W.WmoCode) || (W.VisibilityM > 0.0f && W.VisibilityM < 1000.0f), 1.0f,
			FString::Printf(TEXT("%s: visibility %.0f m"), *Src, W.VisibilityM));
		Set(TEXT("live:sunny"), W.bIsDay && W.CloudCover < 0.4f, 1.0f - W.CloudCover, FString::Printf(TEXT("%s: cloud %.0f%%"), *Src, W.CloudCover * 100.0f));
		Set(TEXT("live:overcast"), W.CloudCover > 0.8f, W.CloudCover, FString::Printf(TEXT("%s: cloud %.0f%%"), *Src, W.CloudCover * 100.0f));
		Set(TEXT("live:hot"), W.TemperatureC >= 27.0f, FMath::Clamp((W.TemperatureC - 24.0f) / 6.0f, 0.0f, 1.0f), FString::Printf(TEXT("%s: %.1f C"), *Src, W.TemperatureC));
		Set(TEXT("live:cold"), W.TemperatureC <= 5.0f, 1.0f, FString::Printf(TEXT("%s: %.1f C"), *Src, W.TemperatureC));
		const FString Wind = FString::Printf(TEXT("%s: wind %.1f m/s, gusts %.1f"), *Src, W.WindSpeedMs, W.WindGustMs);
		Set(TEXT("live:wind_strong"), W.WindSpeedMs >= 10.0f || W.WindGustMs >= 15.0f, 1.0f, Wind);
		Set(TEXT("live:wind_breezy"), W.WindSpeedMs >= 4.5f && W.WindSpeedMs < 10.0f, 1.0f, Wind);
	}
	const FSimCruiseLiveAir& A = Live->GetAir();
	if (A.bValid)
	{
		Set(TEXT("live:hazy"), A.AerosolOpticalDepth > 0.3f || A.UsAqi > 100.0f, FMath::Clamp(FMath::Max(A.AerosolOpticalDepth / 0.6f, A.UsAqi / 150.0f), 0.0f, 1.0f),
			FString::Printf(TEXT("CAMS: AOD %.2f, AQI %.0f"), A.AerosolOpticalDepth, A.UsAqi));
	}
	const FSimCruiseLiveSea& S = Live->GetSea();
	if (S.bValid)
	{
		Set(TEXT("live:sea_rough"), S.WaveHeightM >= 1.5f, FMath::Clamp(S.WaveHeightM / 2.5f, 0.0f, 1.0f), FString::Printf(TEXT("Open-Meteo marine: waves %.1f m"), S.WaveHeightM));
	}

	// Traffic on the road under the car (FHWA counts, scaled to the hour).
	const UWorld* World = GetWorld();
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		float PerKmLane = 0.0f, PerHour = 0.0f;
		FString Road;
		if (Live->TrafficAt(Pawn->GetActorLocation(), PerKmLane, PerHour, &Road))
		{
			const FString Why = FString::Printf(TEXT("FHWA count on %s: %.0f veh/h, %.1f veh/km/lane"), *Road, PerHour, PerKmLane);
			Set(TEXT("live:traffic_heavy"), PerKmLane >= 20.0f, FMath::Clamp(PerKmLane / 35.0f, 0.0f, 1.0f), Why);
			Set(TEXT("live:traffic_light"), PerKmLane < 6.0f, 1.0f, Why);
		}
	}
	const int32 Photos = Live->GetPhotos().Num();
	Set(TEXT("live:photos"), Photos > 0, FMath::Clamp(Photos / 20.0f, 0.1f, 1.0f), FString::Printf(TEXT("Wikimedia Commons: %d photographs nearby"), Photos));
#endif
}

void USimCruisePlaceGraphSubsystem::FetchAhead()
{
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return;
	const FVector2d T = SimCruiseGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Here(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	// Nearest first: the tile under the car, its ring, then the next ring.
	for (int32 R = 0; R <= 2; ++R)
	{
		for (int32 dy = -R; dy <= R; ++dy)
		{
			for (int32 dx = -R; dx <= R; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R) continue;
				const FIntPoint Tile = Here + FIntPoint(dx, dy);
				if (Failed.Contains(Tile) || SimCruisePlaceFacts::IsKnown(Tile)) continue;
				StartFetch(Tile);
				return;
			}
		}
	}
}

void USimCruisePlaceGraphSubsystem::StartFetch(const FIntPoint& Tile)
{
	bFetching = true;
	FetchTile = Tile;
	PendingOsmBody.Reset();
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TestUrl(SimCruisePlaceFacts::OverpassUrl(OverpassAttempt)));
	Req->SetVerb(TEXT("POST"));
	Req->SetHeader(TEXT("User-Agent"), UserAgent);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/x-www-form-urlencoded"));
	Req->SetContentAsString(TEXT("data=") + FGenericPlatformHttp::UrlEncode(SimCruisePlaceFacts::OverpassQuery(Tile)));
	Req->SetTimeout(40.0f);
	TWeakObjectPtr<USimCruisePlaceGraphSubsystem> Weak(this);
	Req->OnProcessRequestComplete().BindLambda([Weak, Tile](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		if (!Weak.IsValid()) return;
		const bool bOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
		Weak->OnOverpass(Tile, bOk ? Response->GetContentAsString() : FString(), bOk);
	});
	Req->ProcessRequest();
}

void USimCruisePlaceGraphSubsystem::OnOverpass(const FIntPoint& Tile, const FString& Body, bool bOk)
{
	// Keep the answer as text: Overpass answers run to megabytes, so they are parsed on a
	// worker thread with the Wikidata answer (OnWikidata), never on the game thread.
	PendingOsmBody = bOk ? Body : FString();
	if (!bOk)
	{
		// Overpass is busy at times (429/504): the next request goes to the next instance;
		// keep going with Wikidata alone.
		LastError = FString::Printf(TEXT("Overpass %s (%s): no answer"), *SimCruiseTileName(Tile), *SimCruisePlaceFacts::OverpassUrl(OverpassAttempt));
		++OverpassAttempt;
		UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("%s"), *LastError);
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(TestUrl(SimCruisePlaceFacts::WikidataUrl(SimCruisePlaceFacts::WikidataQuery(Tile))));
	Req->SetVerb(TEXT("GET"));
	Req->SetHeader(TEXT("User-Agent"), UserAgent);
	Req->SetHeader(TEXT("Accept"), TEXT("application/sparql-results+json"));
	Req->SetTimeout(40.0f);
	const bool bOsmOk = bOk;
	TWeakObjectPtr<USimCruisePlaceGraphSubsystem> Weak(this);
	Req->OnProcessRequestComplete().BindLambda([Weak, Tile, bOsmOk](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
	{
		if (!Weak.IsValid()) return;
		const bool bWdOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
		if (!bOsmOk && !bWdOk) { Weak->OnWikidata(Tile, FString(), false); return; }
		Weak->OnWikidata(Tile, bWdOk ? Response->GetContentAsString() : FString(), true);
	});
	Req->ProcessRequest();
}

void USimCruisePlaceGraphSubsystem::OnWikidata(const FIntPoint& Tile, const FString& Body, bool bOk)
{
	if (!bOk)
	{
		bFetching = false;
		PendingOsmBody.Reset();
		Failed.Add(Tile);
		NextFetch = FPlatformTime::Seconds() + 10.0;
		return;
	}
	// Parse, merge and save on a worker thread; only the result comes back.
	FString OsmBody = MoveTemp(PendingOsmBody);
	PendingOsmBody.Reset();
	TWeakObjectPtr<USimCruisePlaceGraphSubsystem> Weak(this);
	Async(EAsyncExecution::ThreadPool, [Weak, Tile, OsmBody = MoveTemp(OsmBody), WdBody = Body]()
	{
		TArray<FSimCruisePlaceRecord> Osm, Wd, All, Useful;
		FString Error, OsmError, WdError;
		if (!OsmBody.IsEmpty() && !SimCruisePlaceFacts::ParseOverpass(OsmBody, Osm, OsmError)) Osm.Reset();
		if (!WdBody.IsEmpty() && !SimCruisePlaceFacts::ParseWikidata(WdBody, Wd, WdError)) Wd.Reset();
		SimCruisePlaceFacts::Merge(Osm, Wd, All);
		for (FSimCruisePlaceRecord& R : All) if (SimCruisePlaceFacts::IsUseful(R)) Useful.Add(MoveTemp(R));
		// Both sources answered: saved for good. One missing: kept for this run only, asked again next time.
		const bool bComplete = !OsmBody.IsEmpty() && OsmError.IsEmpty() && !WdBody.IsEmpty() && WdError.IsEmpty();
		SimCruisePlaceFacts::Store(Tile, Useful, FString::Printf(TEXT("overpass:%d wikidata:%d"), Osm.Num(), Wd.Num()), bComplete);
		// The street furniture comes in the same Overpass answer.
		if (!OsmBody.IsEmpty() && OsmError.IsEmpty())
		{
			TArray<FSimCruiseFurniture> Furniture;
			FString FError;
			if (SimCruiseFurniture::ParseOverpass(OsmBody, Furniture, FError)) SimCruiseFurniture::Store(Tile, Furniture, true);
			// ... and the roadside: road-edge tags, barriers, poles, lamps, signals, named shops.
			FSimCruiseRoadside Roadside;
			if (SimCruiseRoadsideData::ParseOverpass(OsmBody, Roadside, FError)) SimCruiseRoadsideData::Store(Tile, Roadside, true);
		}
		const FString Line = FString::Printf(TEXT("Place facts %s: %d useful records (%d OSM, %d Wikidata)%s%s."), *SimCruiseTileName(Tile), Useful.Num(), Osm.Num(), Wd.Num(),
			OsmError.IsEmpty() ? TEXT("") : *(TEXT("; ") + OsmError), WdError.IsEmpty() ? TEXT("") : *(TEXT("; ") + WdError));
		AsyncTask(ENamedThreads::GameThread, [Weak, Line]()
		{
			if (!Weak.IsValid()) return;
			USimCruisePlaceGraphSubsystem& S = *Weak;
			S.bFetching = false;
			++S.Fetched;
			UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("%s"), *Line);
			// The Overpass fair-use rule: one request at a time, a pause between them.
			S.NextFetch = FPlatformTime::Seconds() + 3.0;
		});
	});
}

FString USimCruisePlaceGraphSubsystem::Report() const
{
	FScopeLock Lock(&HubLock);
	int32 Buildings = 0, Matched = 0, Records = 0;
	double Ms = 0.0;
	for (const auto& KV : PlanStore())
	{
		Buildings += KV.Value->Buildings.Num();
		Matched += KV.Value->Matched;
		Records += KV.Value->Records;
		Ms = FMath::Max(Ms, KV.Value->BakeMs);
	}
	return FString::Printf(TEXT("Place graph: %d tiles baked, %d buildings, %d with their own open data (%d records), slowest bake %.2f ms (worker); %d tiles fetched, %d areas read by the AI%s%s; %d live facts; game thread: slowest tick %.2f ms, slowest card check %.2f ms."),
		PlanStore().Num(), Buildings, Matched, Records, Ms, Fetched, AIAreas, LastError.IsEmpty() ? TEXT("") : TEXT(", last error: "), *LastError, LiveStore().Facts.Num(),
		SlowestTickMs, SlowestCardMs);
}

FString USimCruisePlaceGraphSubsystem::Explain(int32 Count) const
{
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return TEXT("Place graph: no world yet.");
	const FVector2d T = SimCruiseGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Tile(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	const TSharedPtr<const FSimCruiseTilePlan, ESPMode::ThreadSafe> PlanRef = SimCruisePlaceHub::FindPlan(Tile);
	if (!PlanRef.IsValid()) return FString::Printf(TEXT("Place graph: tile %s has no plan yet."), *SimCruiseTileName(Tile));
	const FSimCruiseTilePlan& Plan = *PlanRef;
	const double Side = SimCruiseGeo::TileSizeMetres(Lat);
	const FVector2d Here = (T - FVector2d(Tile.X, Tile.Y)) * Side;
	TArray<const FSimCruiseBuildingPlan*> Near;
	for (const FSimCruiseBuildingPlan& B : Plan.Buildings) Near.Add(&B);
	Near.Sort([&](const FSimCruiseBuildingPlan& A, const FSimCruiseBuildingPlan& B) { return FVector2d::DistSquared(A.CentreM, Here) < FVector2d::DistSquared(B.CentreM, Here); });
	const FSimCruiseFactSet Live = SimCruisePlaceHub::Live();
	const FSimCruisePlaceRules& Rules = SimCruisePlaceGraph::Rules();
#if SIMCRUISE_PLACE_GRAPH_LIVE
	const USimCruiseLiveWorldSubsystem* LiveWorld = USimCruiseLiveWorldSubsystem::Get(GetWorld());
#endif
	FString Out = FString::Printf(TEXT("Place graph at %.5f, %.5f (tile %s, %d buildings, %d with open data, bake %.2f ms)\nTile facts:\n%s"),
		Lat, Lon, *SimCruiseTileName(Tile), Plan.Buildings.Num(), Plan.Matched, Plan.BakeMs, *Plan.TileFacts.Describe());
	for (int32 i = 0; i < FMath::Min(Count, Near.Num()); ++i)
	{
		const FSimCruiseBuildingPlan& B = *Near[i];
		Out += FString::Printf(TEXT("\n--- %.0f m away, %.0f m tall ---\n"), FVector2d::Distance(B.CentreM, Here), B.HeightM);
		Out += B.Explain();
		FSimCruiseFactSet View;
		SimCruisePlaceGraph::LiveView(Rules, B, Live, View);
		FString LiveLines;
		for (const auto& KV : View.Facts)
		{
			const FString K = KV.Key.ToString();
			if (K.StartsWith(TEXT("light:")) || K.StartsWith(TEXT("street:")) || K.StartsWith(TEXT("surface:")) || K.StartsWith(TEXT("air:"))
				|| K.StartsWith(TEXT("sea:")) || K.StartsWith(TEXT("vegetation:")) || K.StartsWith(TEXT("sky:")))
			{
				LiveLines += FString::Printf(TEXT("  %-28s %.2f  %s\n"), *K, KV.Value.Confidence, *KV.Value.Why);
			}
		}
		if (!LiveLines.IsEmpty()) Out += TEXT(" Live view now:\n") + LiveLines;
#if SIMCRUISE_PLACE_GRAPH_LIVE
		// Photographs of this place on Wikimedia Commons (for the builders and the AI).
		if (LiveWorld && !B.Name.IsEmpty())
		{
			// The name, or its longest word ("The Carlyle" -> "Carlyle"), as photo titles write it.
			TArray<FString> Words;
			B.Name.ParseIntoArray(Words, TEXT(" "));
			FString First;
			for (const FString& Wd : Words) if (Wd.Len() > First.Len() && Wd != TEXT("Hotel") && Wd != TEXT("South") && Wd != TEXT("Beach")) First = Wd;
			const FString Key = B.Name;
			for (const FSimCruisePlacePhoto& P : LiveWorld->GetPhotos())
			{
				if (P.Title.Contains(Key, ESearchCase::IgnoreCase) || (First.Len() > 4 && P.Title.Contains(First, ESearchCase::IgnoreCase)))
				{
					Out += FString::Printf(TEXT(" Photo: %s (%s, %s)\n"), *P.Title, *P.License, *P.PageUrl);
				}
			}
		}
#endif
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------
// The co-driver's place card

namespace
{
	constexpr double CardSeconds = 7.0;
	constexpr double CardFadeSeconds = 0.8;
	constexpr double CardRangeM = 45.0;
}

FString USimCruisePlaceGraphSubsystem::StyleLabel(FName Style)
{
	FString S = Style.ToString();
	S.RemoveFromStart(TEXT("style:"));
	static const TMap<FString, FString> Special = {
		{ TEXT("mimo"), TEXT("MiMo (Miami Modern)") }, { TEXT("art_deco"), TEXT("Art Deco") }, { TEXT("beaux_arts"), TEXT("Beaux-Arts") },
		{ TEXT("mid_century_modern"), TEXT("Mid-century Modern") }, { TEXT("queen_anne"), TEXT("Queen Anne") } };
	if (const FString* L = Special.Find(S)) return *L;
	TArray<FString> Words;
	S.ParseIntoArray(Words, TEXT("_"));
	for (FString& W : Words) if (W.Len() > 0) W[0] = FChar::ToUpper(W[0]);
	return FString::Join(Words, TEXT(" "));
}

bool USimCruisePlaceGraphSubsystem::MakeCard(const FSimCruiseBuildingPlan& Plan, FSimCruisePlaceCard& Out)
{
	if (Plan.Name.IsEmpty()) return false;
	TArray<FString> Parts;
	// Only a style the place's own data gives (not one spread from the neighbours).
	if (!Plan.Style.IsNone() && Plan.StyleConfidence >= 0.95f) Parts.Add(StyleLabel(Plan.Style));
	if (Plan.Year > 0) Parts.Add(FString::FromInt(Plan.Year));
	if (!Plan.Architect.IsEmpty()) Parts.Add(Plan.Architect);
	if (Parts.Num() == 0) return false;
	Out.Title = Plan.Name.ToUpper();
	Out.Line = FString::Join(Parts, TEXT("  ·  "));
	Out.Note = Plan.Facts.Has(TEXT("heritage:nrhp")) ? TEXT("National Register of Historic Places")
		: Plan.Facts.Has(TEXT("heritage:listed")) ? TEXT("Listed building") : TEXT("Wikidata, OpenStreetMap");
	return true;
}

void USimCruisePlaceGraphSubsystem::UpdateCard(double Now)
{
	if (Now - CardStart < CardSeconds + 1.5) return; // one card at a time, with a pause
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return;
	const FVector2d T = SimCruiseGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Tile(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	const TSharedPtr<const FSimCruiseTilePlan, ESPMode::ThreadSafe> PlanRef = SimCruisePlaceHub::FindPlan(Tile);
	if (!PlanRef.IsValid()) return;
	const FSimCruiseTilePlan& Plan = *PlanRef;
	const FVector2d Here = (T - FVector2d(Tile.X, Tile.Y)) * SimCruiseGeo::TileSizeMetres(Lat);
	const FSimCruiseBuildingPlan* Best = nullptr;
	double BestD = CardRangeM;
	FSimCruisePlaceCard BestCard;
	for (const FSimCruiseBuildingPlan& B : Plan.Buildings)
	{
		const double D = FVector2d::Distance(B.CentreM, Here);
		if (D >= BestD || CardsShown.Contains(B.Name)) continue;
		FSimCruisePlaceCard C;
		if (!MakeCard(B, C)) continue;
		Best = &B;
		BestD = D;
		BestCard = C;
	}
	if (!Best) return;
	CardsShown.Add(Best->Name);
	Card = BestCard;
	CardStart = Now;
	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseCardShots")))
	{
		bCardShotPending = true; // taken once the card has faded in
	}
	UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Place card: %s | %s | %s (%.0f m)"), *Card.Title, *Card.Line, *Card.Note, BestD);
}

bool USimCruisePlaceGraphSubsystem::GetPlaceCard(FSimCruisePlaceCard& Out) const
{
	const double Age = FPlatformTime::Seconds() - CardStart;
	if (Age < 0.0 || Age > CardSeconds || Card.Title.IsEmpty()) return false;
	Out = Card;
	Out.Alpha = static_cast<float>(FMath::Clamp(FMath::Min(Age, CardSeconds - Age) / CardFadeSeconds, 0.0, 1.0));
	return true;
}

// ---------------------------------------------------------------------------------------------
// The local AI reads the area (SimCruisePlaceAI)

namespace
{
	using FAIThen = TFunction<void(USimCruisePlaceGraphSubsystem&, bool, const FString&)>;

	void AIRequest(TWeakObjectPtr<USimCruisePlaceGraphSubsystem> Weak, const FString& Url, const FString& Verb, const FString& Body, float Timeout, FAIThen Then)
	{
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
		Req->SetURL(TestUrl(Url));
		Req->SetVerb(Verb);
		Req->SetHeader(TEXT("User-Agent"), UserAgent);
		if (!Body.IsEmpty())
		{
			Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
			Req->SetContentAsString(Body);
		}
		Req->SetTimeout(Timeout);
		Req->SetActivityTimeout(Timeout); // a local model answers only when done
		Req->OnProcessRequestComplete().BindLambda([Weak, Then](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnected)
		{
			if (!Weak.IsValid()) return;
			const bool bOk = bConnected && Response.IsValid() && Response->GetResponseCode() == 200;
			Then(*Weak, bOk, bOk ? Response->GetContentAsString() : FString());
		});
		Req->ProcessRequest();
	}
}

void USimCruisePlaceGraphSubsystem::AIFail(const FString& Why)
{
	AIStep = EAIStep::Idle;
	AIFailed.Add(AIArea);
	LastError = Why;
	UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Place AI %d_%d: %s"), AIArea.X, AIArea.Y, *Why);
}

void USimCruisePlaceGraphSubsystem::StartAI()
{
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return;
	const FVector2d T = SimCruiseGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Area = SimCruisePlaceAI::AreaOf(FIntPoint(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y)));
	if (SimCruisePlaceAI::IsKnown(Area) || AIFailed.Contains(Area)) return;
	AIArea = Area;
	AIPrior = FSimCruiseAreaPrior();
	AILines.Reset();
	AIStep = EAIStep::Busy;
	const TWeakObjectPtr<USimCruisePlaceGraphSubsystem> Weak(this);

	// 1. Is the local model there? (Ollama /api/tags: the smallest chat model that fits beside the game.)
	AIRequest(Weak, SimCruisePlaceAI::OllamaUrl() + TEXT("/api/tags"), TEXT("GET"), FString(), 5.0f, [Weak](USimCruisePlaceGraphSubsystem& S, bool bOk, const FString& Tags)
	{
		S.AIModel = bOk ? SimCruisePlaceAI::PickModel(Tags) : FString();
		if (S.AIModel.IsEmpty())
		{
			S.bAI = false; // no local model: stop asking
			S.AIStep = EAIStep::Idle;
			UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Place AI off: no local model at %s (install Ollama and a 2-6 GB model)."), *SimCruisePlaceAI::OllamaUrl());
			return;
		}
		double CLat, CLon;
		SimCruisePlaceAI::AreaCentre(S.AIArea, CLat, CLon);
		// 2. The Wikipedia articles about places near the area's centre.
		AIRequest(Weak, SimCruisePlaceAI::GeosearchUrl(CLat, CLon), TEXT("GET"), FString(), 30.0f, [Weak, CLat, CLon](USimCruisePlaceGraphSubsystem& S, bool bOk, const FString& Geo)
		{
			TArray<int64> Ids;
			FString Error;
			if (!bOk || !SimCruisePlaceAI::ParseGeosearch(Geo, Ids, Error)) { S.AIFail(TEXT("Wikipedia geosearch failed")); return; }
			if (Ids.Num() == 0)
			{
				// Nothing written about this area: an empty answer, so it is not asked again.
				S.AIPrior.Model = S.AIModel;
				SimCruisePlaceAI::Store(S.AIArea, S.AIPrior);
				S.AIStep = EAIStep::Idle;
				return;
			}
			// 3. Their lead extracts.
			AIRequest(Weak, SimCruisePlaceAI::ExtractsUrl(Ids), TEXT("GET"), FString(), 30.0f, [Weak, Ids, CLat, CLon](USimCruisePlaceGraphSubsystem& S, bool bOk, const FString& Ex)
			{
				FString Error;
				if (!bOk || !SimCruisePlaceAI::ParseExtracts(Ex, Ids, S.AILines, S.AIPrior.Sources, Error)) { S.AIFail(TEXT("Wikipedia extracts failed")); return; }
				const UWorld* World = S.GetWorld();
				const USimCruiseWorldSubsystem* WS = World ? World->GetSubsystem<USimCruiseWorldSubsystem>() : nullptr;
				const ASimCruiseWorldStreamer* Streamer = WS ? WS->GetStreamer() : nullptr;
				const FString Label = Streamer ? Streamer->GetTitle() : FString();
				const double T0 = FPlatformTime::Seconds();
				// 4. The local model reads them (CPU only) and answers in the graph's words.
				AIRequest(Weak, SimCruisePlaceAI::OllamaUrl() + TEXT("/api/chat"), TEXT("POST"),
					SimCruisePlaceAI::ChatRequest(SimCruisePlaceGraph::Rules(), S.AIModel, Label, CLat, CLon, S.AILines), 600.0f,
					[T0](USimCruisePlaceGraphSubsystem& S, bool bOk, const FString& Chat)
				{
					FString Error;
					const TArray<FString> Sources = S.AIPrior.Sources;
					if (!bOk || !SimCruisePlaceAI::ParseChat(SimCruisePlaceGraph::Rules(), Chat, S.AIPrior, Error)) { S.AIFail(TEXT("local model failed: ") + Error); return; }
					S.AIPrior.Sources = Sources;
					SimCruisePlaceAI::Store(S.AIArea, S.AIPrior);
					S.AIStep = EAIStep::Idle;
					++S.AIAreas;
					FSimCruiseFactSet F;
					SimCruisePlaceAI::ToFacts(S.AIPrior, F);
					UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Place AI %d_%d (%s, %.0f s on the CPU, %d articles):\n%s"), S.AIArea.X, S.AIArea.Y, *S.AIPrior.Model,
						FPlatformTime::Seconds() - T0, Sources.Num(), *F.Describe());
				});
			});
		});
	});
}
