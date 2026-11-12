// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruisePlaceGraphSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Kismet/GameplayStatics.h"
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
	TMap<FIntPoint, FSimCruiseTilePlan>& PlanStore() { static TMap<FIntPoint, FSimCruiseTilePlan> S; return S; }

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
		PlanStore().Add(Plan.Tile, Plan);
		// Keep the plans of the tiles around the car only (a tile holds a few hundred buildings).
		if (PlanStore().Num() > 64)
		{
			TArray<FIntPoint> Keys;
			PlanStore().GetKeys(Keys);
			Keys.Sort([&](const FIntPoint& A, const FIntPoint& B) { return (A - Plan.Tile).SizeSquared() > (B - Plan.Tile).SizeSquared(); });
			for (int32 i = 0; i < Keys.Num() - 48; ++i) PlanStore().Remove(Keys[i]);
		}
	}

	bool FindPlan(const FIntPoint& Tile, FSimCruiseTilePlan& Out)
	{
		FScopeLock Lock(&HubLock);
		if (const FSimCruiseTilePlan* P = PlanStore().Find(Tile)) { Out = *P; return true; }
		return false;
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
		SimCruisePlaceGraph::BakeTile(SimCruisePlaceGraph::Rules(), Data, LatDeg, LonDeg, SideM, Records, Out);
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
	bNetwork = !FParse::Param(Cmd, TEXT("SimCruiseNoLive")) && !FParse::Param(Cmd, TEXT("SimCruiseNoPlaceFetch"))
		&& (bForce || (!FApp::IsUnattended() && !GIsAutomationTesting));
	const FSimCruisePlaceRules& R = SimCruisePlaceGraph::Rules();
	UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Place graph: %d rules, %d live rules, %d regions, %d palettes; open-data fetch %s."),
		R.Rules.Num(), R.LiveRules.Num(), R.Regions.Num(), R.Palettes.Num(), bNetwork ? TEXT("on") : TEXT("off"));
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
		UpdateCard(Now);
	}
	if (bNetwork && !bFetching && Now >= NextFetch)
	{
		NextFetch = Now + 2.0;
		FetchAhead();
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
	PendingOsm.Reset();
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(SimCruisePlaceFacts::OverpassUrl(OverpassAttempt));
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
	FString Error;
	if (!bOk || !SimCruisePlaceFacts::ParseOverpass(Body, PendingOsm, Error))
	{
		// Overpass is busy at times (429/504): try again later, keep going with Wikidata alone.
		LastError = FString::Printf(TEXT("Overpass %s (%s): %s"), *SimCruiseTileName(Tile), *SimCruisePlaceFacts::OverpassUrl(OverpassAttempt), bOk ? *Error : TEXT("no answer"));
		++OverpassAttempt; // the next request goes to the next instance
		UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("%s"), *LastError);
		PendingOsm.Reset();
	}
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(SimCruisePlaceFacts::WikidataUrl(SimCruisePlaceFacts::WikidataQuery(Tile)));
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
	bFetching = false;
	if (!bOk)
	{
		Failed.Add(Tile);
		NextFetch = FPlatformTime::Seconds() + 10.0;
		return;
	}
	TArray<FSimCruisePlaceRecord> Wd;
	FString Error;
	if (!Body.IsEmpty() && !SimCruisePlaceFacts::ParseWikidata(Body, Wd, Error))
	{
		LastError = FString::Printf(TEXT("Wikidata %s: %s"), *SimCruiseTileName(Tile), *Error);
	}
	TArray<FSimCruisePlaceRecord> All, Useful;
	SimCruisePlaceFacts::Merge(PendingOsm, Wd, All);
	for (FSimCruisePlaceRecord& R : All) if (SimCruisePlaceFacts::IsUseful(R)) Useful.Add(MoveTemp(R));
	SimCruisePlaceFacts::Store(Tile, Useful, FString::Printf(TEXT("overpass:%d wikidata:%d"), PendingOsm.Num(), Wd.Num()));
	++Fetched;
	UE_LOG(LogSimCruisePlaceGraph, Display, TEXT("Place facts %s: %d useful records (%d OSM, %d Wikidata)."), *SimCruiseTileName(Tile), Useful.Num(), PendingOsm.Num(), Wd.Num());
	PendingOsm.Reset();
	// The Overpass fair-use rule: one request at a time, a pause between them.
	NextFetch = FPlatformTime::Seconds() + 3.0;
}

FString USimCruisePlaceGraphSubsystem::Report() const
{
	FScopeLock Lock(&HubLock);
	int32 Buildings = 0, Matched = 0, Records = 0;
	double Ms = 0.0;
	for (const auto& KV : PlanStore())
	{
		Buildings += KV.Value.Buildings.Num();
		Matched += KV.Value.Matched;
		Records += KV.Value.Records;
		Ms = FMath::Max(Ms, KV.Value.BakeMs);
	}
	return FString::Printf(TEXT("Place graph: %d tiles baked, %d buildings, %d with their own open data (%d records), slowest bake %.2f ms; %d tiles fetched%s%s; %d live facts."),
		PlanStore().Num(), Buildings, Matched, Records, Ms, Fetched, LastError.IsEmpty() ? TEXT("") : TEXT(", last error: "), *LastError, LiveStore().Facts.Num());
}

FString USimCruisePlaceGraphSubsystem::Explain(int32 Count) const
{
	double Lat, Lon;
	if (!ObserverLonLat(Lat, Lon)) return TEXT("Place graph: no world yet.");
	const FVector2d T = SimCruiseGeo::LonLatToTile(Lon, Lat);
	const FIntPoint Tile(FMath::FloorToInt(T.X), FMath::FloorToInt(T.Y));
	FSimCruiseTilePlan Plan;
	if (!SimCruisePlaceHub::FindPlan(Tile, Plan)) return FString::Printf(TEXT("Place graph: tile %s has no plan yet."), *SimCruiseTileName(Tile));
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
	FSimCruiseTilePlan Plan;
	if (!SimCruisePlaceHub::FindPlan(Tile, Plan)) return;
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
