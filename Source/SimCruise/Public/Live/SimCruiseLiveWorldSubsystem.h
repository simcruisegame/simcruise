// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Live/SimCruiseLiveData.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCruiseLiveWorldSubsystem.generated.h"

class ASimCruiseLiveSky;
class UMaterialInstanceDynamic;

/**
 * The live world: reads free public real-time feeds for the place the car is now and
 * makes the game world match it (see FSimCruiseLiveData for the feeds).
 *
 *   weather   every 10 min   sky cover, haze, mist, rain (wet roads), wind, temperature
 *   metar     every 10 min   the nearest airport observation overrides what it measures
 *   air       every 30 min   aerosol optical depth sets the haze in clear air
 *   sea       every 30 min   waves, sea temperature, tide (NOAA gauge within 30 km if any)
 *   aircraft  every 10 s     the real aircraft within 25 nm fly overhead (ASimCruiseLiveSky)
 *   roads     each place     measured daily traffic per road (US), scaled to the hour: TrafficAt
 *   photos    each place     freely licensed photographs near the car (Wikimedia Commons)
 *
 * The car moving more than 5 km reads the slow feeds again for the new place. Every
 * answer is saved under Saved/Live, so the next start uses the last data at once and an
 * offline start keeps the last real sky (weather up to 6 hours old). Nothing needs a key.
 *
 * Command line: -SimCruiseNoLive turns it off; -SimCruiseLive turns it on in unattended
 * runs (tests and captures are offline by default). A chosen sky (-SimCruiseConditions,
 * -SimCruiseCloud, -SimCruiseVisibilityKm, -SimCruiseRain) wins over the live weather.
 * Console: SimCruise.Live.Report, SimCruise.Live.Refresh.
 */
UCLASS()
class SIMCRUISE_API USimCruiseLiveWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static USimCruiseLiveWorldSubsystem* Get(const UWorld* World);

	/** Read every feed again now. */
	void RefreshAll();

	FString Report() const;

	/** Saves a telephoto frame toward the nearest live aircraft (SimCruise.Live.Shot). */
	void Shot(const FString& Name, float FovDeg);

	const FSimCruiseLiveWeather& GetWeather() const { return Weather; }
	const FSimCruiseLiveAir& GetAir() const { return Air; }
	const FSimCruiseLiveSea& GetSea() const { return Sea; }
	bool IsEnabled() const { return bEnabled; }

	/** Road sections with measured traffic around the place (US only; FHWA HPMS). */
	const TArray<FSimCruiseRoadSection>& GetRoads() const { return Roads; }

	/** Freely licensed photographs near the place (Wikimedia Commons), for builders and the AI. */
	const TArray<FSimCruisePlacePhoto>& GetPhotos() const { return Photos; }

	/**
	 * The traffic that belongs on the road at a world position now: vehicles per km on each
	 * lane and per hour (both directions), from the road's measured daily count, the sky
	 * clock's local hour and weekday, and the live rain. False when no counted road is within
	 * 40 m (local streets are not counted; outside the US there is no inventory).
	 */
	bool TrafficAt(const FVector& WorldCm, float& OutPerKmLane, float& OutPerHour, FString* OutRoad = nullptr) const;

	/** Local time at the place on the sky clock (UTC plus the place's offset). */
	FDateTime LocalNow() const;

private:
	enum class EFeed : uint8 { Weather, Metar, Air, Sea, TideStations, Tide, Aircraft, State, Roads, Photos, Count };

	struct FFeedState
	{
		double NextAt = 0.0;
		bool bInFlight = false;
		FDateTime LastOkUtc;
		FString LastError;
		int32 Failures = 0;
	};

	bool ObserverLonLat(double& OutLat, double& OutLon) const;
	void Fetch(EFeed Feed, const FString& Url);

	/**
	 * The one request budget for every live request: at most 3 in flight, each host no more
	 * often than its spacing (1 s; Wikimedia 1.5 s), and at most 600 requests an hour in all.
	 * True takes a slot now; false means try again on a later tick.
	 */
	bool TakeRequestSlot(const FString& Url);

	/**
	 * Every 3 s: the road water (the environment's, or WetOverride) on every asphalt material
	 * ("Wet"), the tide and waves on every sea material ("SeaLevelCm", "WaveScale", "WaveDir"),
	 * and the wind on the palm fronds ("WindMs", "WindDir").
	 */
	void ApplyRoadWater();
	double NextRoadWaterAt = 0.0;
	float AppliedRoadWater = -1.0f;

public:
	/** Test override of the road water 0..1 (SimCruise.Live.Wet); negative follows the weather. */
	float WetOverride = -1.0f;

	/** Test override of the wind in the palms, m/s (-SimCruiseLiveWind); negative follows the weather. */
	float WindOverride = -1.0f;

private:
	TMap<FString, double> HostNextAt;
	TArray<double> RecentRequests;
	int32 InFlight = 0;
	/** One feed's answer, parsed off the game thread (JSON of up to a few MB: roads, tide stations). */
	struct FParsedFeed
	{
		bool bOk = false;
		FString Error;
		FSimCruiseLiveWeather Weather;
		TArray<FSimCruiseMetar> Metars;
		FSimCruiseLiveAir Air;
		FSimCruiseLiveSea Sea;
		TArray<FSimCruiseTideStation> TideStations;
		float TideLevelM = 0.0f;
		FString StateCode;
		TArray<FSimCruiseRoadSection> Roads;
		TArray<FSimCruisePlacePhoto> Photos;
		TArray<FSimCruiseLiveAircraft> Aircraft;
	};

	/** Pure: parses a feed's text (any thread). Photos also learn which copies are on disk. */
	static TSharedRef<FParsedFeed> ParseFeed(EFeed Feed, const FString& Body);

	/** Game thread: applies a parsed answer to the live state and the world. */
	void ApplyFeed(EFeed Feed, FParsedFeed& Parsed, const FDateTime& ReceivedUtc, bool bFromCache);

	/** Synchronous parse and apply (the cache at the start of a drive). */
	void OnFeed(EFeed Feed, const FString& Body, const FDateTime& ReceivedUtc, bool bFromCache);
	void ApplyWeather();
	void Announce();
	FString CachePath(EFeed Feed) const;
	void LoadCache(double Lat, double Lon);
	static const TCHAR* FeedName(EFeed Feed);
	static double PeriodSeconds(EFeed Feed);

	bool bEnabled = false;
	bool bWeatherFromCommandLine = false;
	bool bAnnounced = false;
	float ShotEverySeconds = 0.0f;
	double NextShotAt = 0.0;
	int32 ShotCount = 0;
	double WorstFrameS = 0.0;
	double NextFrameReportAt = 30.0;
	bool bViewShotTaken = true;
	bool bParseTestStarted = false;
	double Clock = 0.0;
	double FeedLat = 0.0;
	double FeedLon = 0.0;
	bool bHaveFeedPlace = false;
	FFeedState Feeds[static_cast<int32>(EFeed::Count)];

	FSimCruiseLiveWeather Weather;
	TArray<FSimCruiseMetar> Metars;
	TOptional<FSimCruiseMetar> NearMetar;
	FSimCruiseLiveAir Air;
	FSimCruiseLiveSea Sea;
	TArray<FSimCruiseTideStation> TideStations;
	TOptional<FSimCruiseTideStation> TideStation;
	TArray<FSimCruiseLiveAircraft> Aircraft;
	FString StateCode;
	TArray<FSimCruiseRoadSection> Roads;
	TArray<FSimCruisePlacePhoto> Photos;
	/** Photographs still to read, one every 1.5 s (Wikimedia asks clients to be gentle). */
	TArray<int32> PhotoQueue;
	double NextPhotoAt = 0.0;
	bool bPhotoInFlight = false;
	void TickPhotoDownloads();
	static FString PhotoPath(const FSimCruisePlacePhoto& Photo);

	TArray<FHttpRequestPtr> Requests;

	UPROPERTY(Transient)
	TObjectPtr<ASimCruiseLiveSky> Sky;

	/** The palm fronds' material with the live wind on it (the palms' meshes share the layer material). */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FrondMaterial;

	FDelegateHandle RebaseHandle;
};
