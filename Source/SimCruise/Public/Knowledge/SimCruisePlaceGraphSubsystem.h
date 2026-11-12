// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Knowledge/SimCruisePlaceAI.h"
#include "Knowledge/SimCruisePlaceGraph.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCruisePlaceGraphSubsystem.generated.h"

/**
 * The hub of the place knowledge graph in a running game.
 *
 *  - Open data ahead of the car: the tiles within two of the car get their OpenStreetMap
 *    and Wikidata records (SimCruisePlaceFacts) before the streamer builds them (it builds
 *    one ring), one tile at a time, each saved under Saved/Knowledge.
 *  - Live facts: every 5 s the live feeds become facts (live:night, live:rain,
 *    live:wind_strong, live:hazy, live:sea_rough, live:traffic_heavy, live:photos ...).
 *    Any system can write more with SimCruisePlaceHub::SetLive.
 *  - Plans: each tile build stores its baked plan here (SimCruisePlaceHub::StorePlan), so
 *    any system can ask what a building is and why, and the live view of it.
 *
 * Network follows the live world: on in play, off in unattended runs unless -SimCruiseLive;
 * -SimCruiseNoLive or -SimCruiseNoPlaceFetch turn it off.
 * Console: SimCruise.Knowledge.Explain [n] (the n buildings nearest the car, why each
 * detail), SimCruise.Knowledge.Live, SimCruise.Knowledge.Report.
 */
/** The co-driver's card for a notable building the car passes: what it is, from the fact store. */
struct FSimCruisePlaceCard
{
	FString Title;   // "THE CARLYLE"
	FString Line;    // "Streamline Moderne  -  1941  -  Richard Kiehnel"
	FString Note;    // "National Register of Historic Places", or the data source
	float Alpha = 0.0f;
};

UCLASS()
class SIMCRUISE_API USimCruisePlaceGraphSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static USimCruisePlaceGraphSubsystem* Get(const UWorld* World);

	FString Report() const;
	/** The Count buildings nearest the car: facts, plan, live view and reasons. */
	FString Explain(int32 Count) const;

	bool ObserverLonLat(double& OutLat, double& OutLon) const;

	/** The card to show now (Alpha fades it in and out), false when none. */
	bool GetPlaceCard(FSimCruisePlaceCard& Out) const;
	/** The card text for a building plan; false when the plan knows too little (no name, or no year, own style or architect). */
	static bool MakeCard(const FSimCruiseBuildingPlan& Plan, FSimCruisePlaceCard& Out);
	/** "style:streamline_moderne" -> "Streamline Moderne". */
	static FString StyleLabel(FName Style);

private:
	void UpdateLiveFacts();
	void UpdateCard(double Now);
	void StartAI();
	void AIFail(const FString& Why);
	void FetchAhead();
	void StartFetch(const FIntPoint& Tile);
	void OnOverpass(const FIntPoint& Tile, const FString& Body, bool bOk);
	void OnWikidata(const FIntPoint& Tile, const FString& Body, bool bOk);

	bool bNetwork = false;
	double NextLive = 0.0;
	double NextFetch = 0.0;
	bool bFetching = false;
	FIntPoint FetchTile = FIntPoint::ZeroValue;
	TArray<FSimCruisePlaceRecord> PendingOsm;
	TSet<FIntPoint> Failed;
	int32 Fetched = 0;
	int32 OverpassAttempt = 0;
	FString LastError;
	double NextCardCheck = 0.0;
	FSimCruisePlaceCard Card;
	double CardStart = -100.0;
	TSet<FString> CardsShown;
	bool bCardShotPending = false;

	// The local AI (one area at a time).
	enum class EAIStep : uint8 { Idle, Busy };
	EAIStep AIStep = EAIStep::Idle;
	bool bAI = true;
	double NextAI = 0.0;
	FIntPoint AIArea = FIntPoint::ZeroValue;
	FString AIModel;
	FSimCruiseAreaPrior AIPrior;
	TArray<FString> AILines;
	TSet<FIntPoint> AIFailed;
	int32 AIAreas = 0;
};

/** Thread-safe store shared by the tile builders (worker threads) and the game thread. */
namespace SimCruisePlaceHub
{
	SIMCRUISE_API void SetLive(FName Fact, float Confidence, const FString& Why);
	SIMCRUISE_API void ClearLive(const TCHAR* Prefix);
	SIMCRUISE_API FSimCruiseFactSet Live();

	SIMCRUISE_API void StorePlan(const FSimCruiseTilePlan& Plan);
	SIMCRUISE_API bool FindPlan(const FIntPoint& Tile, FSimCruiseTilePlan& Out);
	SIMCRUISE_API void ForgetPlan(const FIntPoint& Tile);

	/**
	 * The plan of every building of a tile, as the tile builder needs it: the tile's open
	 * data records (bundled or cached), the rule graph, the bake; stored for the console.
	 */
	SIMCRUISE_API void BakeForBuild(const FSimCruiseTileData& Data, double LatDeg, double LonDeg, double SideM, FSimCruiseTilePlan& Out);
}
