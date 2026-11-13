// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseViewCheck.generated.h"

class APlayerController;

/** What a view ray hits first. */
enum class ESimCruiseViewHit : uint8
{
	Road,      // asphalt and concrete of the world tiles (carriageways, sidewalks, curbs)
	Building,
	Ground,    // the tiles' terrain and the regional terrain
	Tree,      // palms and the tree actor
	Car,       // traffic and parked cars
	Props,     // street furniture, bridges, guard rails, signals
	Other,     // any other actor (for example the drive robot's test pad)
	Empty,     // no hit inside the range (sky)
	Count
};

/** The measured view: ray classes below the horizon and the screenshot's statistics. */
struct SIMCRUISE_API FSimCruiseViewStats
{
	/** Percent of the rays below the horizon, per ESimCruiseViewHit. */
	float Percent[static_cast<int32>(ESimCruiseViewHit::Count)] = {};
	int32 RaysBelowHorizon = 0;
	/** Distance (m) from the car to the nearest road hit; negative when no ray hit a road. */
	float NearestRoadM = -1.f;
	/** Distinct 10 m cells of building hits (all rays). */
	int32 BuildingCells = 0;
	/** The tiles around the car hold mapped roads / buildings. */
	bool bPlaceHasRoads = false;
	/** A built-up place: at least MinPlaceBuildings mapped buildings on the 9 tiles round the car. */
	bool bPlaceHasBuildings = false;
	int32 PlaceBuildings = 0;
	/** Lower 60 % of the screenshot: luminance standard deviation (0-255), mean absolute gradient, colour hue bins. */
	bool bHasImage = false;
	float LumaStdDev = 0.f;
	float Edges = 0.f;
	int32 HueBins = 0;

	float Get(ESimCruiseViewHit Hit) const { return Percent[static_cast<int32>(Hit)]; }
	/** Road, building, ground, tree, car and props: the real world. */
	float WorldPercent() const;
};

/** The pass rules, kept apart from the actor so that the tests can check them. */
namespace SimCruiseViewCheckRules
{
	/** Percent of the rays below the horizon that must hit the real world. */
	constexpr float MinWorldPercent = 70.f;
	/** A road hit must be nearer than this (m) at a place with mapped roads. */
	constexpr float MaxRoadDistanceM = 300.f;
	/** Mean absolute luminance gradient of the lower 60 % (0-255 a pixel) must be above this. */
	constexpr float MinEdges = 2.f;
	/** A building must be in view only in a built-up place: a few farms or houses along a coast road need not be. */
	constexpr int32 MinPlaceBuildings = 30;

	/** Empty when the view passes, else the reasons (comma-separated). */
	SIMCRUISE_API FString Judge(const FSimCruiseViewStats& Stats);

	/** Fills the image fields of Stats from the lower 60 % of an sRGB image. */
	SIMCRUISE_API void MeasureImage(int32 Width, int32 Height, const TArray<FColor>& Pixels, FSimCruiseViewStats& Stats);

	SIMCRUISE_API const TCHAR* Name(ESimCruiseViewHit Hit);

	/** Lower-case words and digits joined with dashes ("Ocean Drive, Miami Beach" -> "ocean-drive-miami-beach"). */
	SIMCRUISE_API FString Slug(const FString& Title);
}

/**
 * An automatic look at what the player sees (-SimCruiseViewCheck[=seconds]):
 * a set time (default 6 s) after the car is placed, the tiles round it are
 * built and the shaders are compiled, it shoots a 48 x 27 grid of line traces
 * from the player camera through the view (to 2 km, the player car ignored),
 * sorts each hit by what it hit (ESimCruiseViewHit), takes a screenshot to
 * Saved/Screenshots/ViewCheck/<place>-<time>.png with image statistics, and
 * writes one log line:
 *
 *   VIEWCHECK PASS|FAIL place="..." road=..% building=..% ground=..% ... shot=<path> reason=<...>
 *
 * and a JSON file of the same numbers next to the screenshot. With
 * -SimCruiseDriveTestPad it waits for the drive robot's test pad first (the
 * pad view must fail). -SimCruiseViewCheckQuit quits the game afterwards.
 */
UCLASS()
class SIMCRUISE_API ASimCruiseViewCheck : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseViewCheck();

	/** -SimCruiseViewCheck or -SimCruiseViewCheck=<seconds> is on the command line. */
	static bool IsRequested(const TCHAR* CommandLine);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The VIEWCHECK line is written. */
	bool IsFinished() const { return Stage == EStage::Done; }
	/** The view passed (valid once IsFinished). */
	bool HasPassed() const { return bPassed; }
	const FString& GetReasons() const { return Reasons; }

private:
	enum class EStage : uint8 { WaitReady, Delay, WaitShot, Done };

	void TraceView(APlayerController* Player);
	void OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	void Report();

	EStage Stage = EStage::WaitReady;
	float DelaySeconds = 6.f;
	bool bWantPad = false;
	bool bQuit = false;
	double StageStarted = 0.0;
	double Started = 0.0;
	FString PlaceTitle;
	FString ShotFile;
	FSimCruiseViewStats Stats;
	bool bShotDone = false;
	bool bPassed = false;
	FString Reasons;
	FDelegateHandle ShotHandle;
};
