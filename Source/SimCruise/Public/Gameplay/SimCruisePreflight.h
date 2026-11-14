// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Async/Future.h"
#include "Gameplay/SimCruiseReview.h"
#include "SimCruisePreflight.generated.h"

class APlayerController;
class ASimCruisePlayerController;
class ASimCruiseWorldStreamer;
struct FSimCruiseRibbon;

/** What one pre-flight run measured. */
struct SIMCRUISE_API FSimCruisePreflightStats
{
	// The drive.
	float DriveSeconds = 0.f;
	float DistanceM = 0.f;
	float MeanSpeedMps = 0.f;
	/** Longest time (s) without a road surface under the car. */
	float OffRoadSeconds = 0.f;
	/** The start fell back to the bundled place (the requested place did not load). */
	bool bFellBack = false;
	/** The start's VIEWCHECK (when it ran). */
	bool bViewCheckRan = false;
	bool bViewCheckPassed = false;
	FString ViewCheckReasons;

	// White sky: of the frame pixels where a view ray hits nothing, the percent that is near white.
	int32 SkySamples = 0;
	float SkyWhitePercent = 0.f;
	float ReferenceEV100 = 0.f;

	// Pop-in: tiles installed, or cars appearing, within the pop-in range in front of the camera.
	int32 TilePopIns = 0;
	int32 CarPopIns = 0;
	/** Frames whose picture changed much more than the drive's usual change, with the camera steady. */
	int32 FlashFrames = 0;
	float MedianFrameDiff = 0.f;

	// Road seams, from the road surface under the path.
	int32 RoadSteps = 0;
	float MaxStepCm = 0.f;
	int32 RoadGaps = 0;
	/** Vertical body jolts, and those near a tile border. */
	int32 Jolts = 0;
	int32 BorderJolts = 0;

	// Traffic.
	int32 StuckTraffic = 0;

	int32 Frames = 0;

	// Car control: a spin, a slide backwards, a run-off.
	/** Peak slip angle of the rear axle (deg, above 5 m/s): the drive robot's spin measure. */
	float PeakRearSlipDeg = 0.f;
	float PeakYawDps = 0.f;
	/** Longest time (s) the car slid backwards faster than 2 m/s. */
	float ReversedSeconds = 0.f;
	/** Farthest the car's centre went past the paved edge of the road ribbon (m), and the longest time (s) more than MaxOffRibbonM past it. */
	float MaxOffRibbonM = 0.f;
	float OffRibbonSeconds = 0.f;
	/** -SimCruisePreflightForceSpin: the negative test yanked the stick with the handbrake on. */
	bool bForcedSpin = false;

	// Frame times after the warm-up, without the frames the robot's own screenshots slow down.
	int32 TimedFrames = 0;
	float FrameP95Ms = 0.f;
	/** Frames over 50 ms, and over 100 ms (hitches a player sees). */
	int32 SlowFrames = 0;
	int32 Hitches = 0;
};

/** The pass rules and the measurements that need no world, kept apart for the tests. */
namespace SimCruisePreflightRules
{
	constexpr float MinDistanceM = 200.f;
	/** Off the road surface longer than this is a run-off (a crack under the wheels takes about 0.1 s). */
	constexpr float MaxOffRoadSeconds = 1.f;
	/** A spin: the rear axle slips past 15 deg (the drive robot's SPUN line), or the car slides backwards. */
	constexpr float MaxRearSlipDeg = 15.f;
	constexpr float MaxReversedSeconds = 0.3f;
	/** A run-off: the car's centre more than 2 m past the paved edge for more than 0.5 s. */
	constexpr float MaxOffRibbonM = 2.f;
	constexpr float MaxOffRibbonSeconds = 0.5f;
	/** Slow frames, after FrameWarmupS of driving: the 95th percentile frame time, and frames over 100 ms. */
	constexpr float FrameWarmupS = 5.f;
	constexpr float MaxFrameP95Ms = 50.f;
	constexpr int32 MaxHitches = 5;
	/** Frame-time rules apply only with at least this many timed frames. */
	constexpr int32 MinTimedFrames = 100;
	constexpr float MaxSkyWhitePercent = 25.f;
	/** Luminance (0-255) from which a sky pixel counts as near white. */
	constexpr float WhiteLuma = 245.f;
	constexpr float PopInRangeM = 150.f;
	constexpr int32 MaxCarPopIns = 2;
	constexpr int32 MaxFlashFrames = 2;
	constexpr float StepCm = 5.f;
	constexpr float JoltMps2 = 8.f;

	/** Empty when the run passes, else the reasons (comma-separated). */
	SIMCRUISE_API FString Judge(const FSimCruisePreflightStats& Stats);

	/** Fills the frame-time fields of Stats (p95, frames over 50 and 100 ms) from the timed frames (ms). */
	SIMCRUISE_API void MeasureFrames(const TArray<float>& TimedMs, FSimCruisePreflightStats& Stats);

	/**
	 * Height steps along a path: samples (distance m, height m) in path order. A step is a
	 * sample further than StepCm from the line through its neighbours, all within MaxGapM.
	 */
	SIMCRUISE_API int32 CountSteps(const TArray<FVector2f>& DistanceHeight, float& OutMaxStepCm, float MaxGapM = 1.f);

	/**
	 * Frames whose change (the percent of the picture that changed by more than 40 of 255) is above
	 * max(Floor, Factor x the median), skipping those marked turning.
	 */
	SIMCRUISE_API int32 CountFlashes(const TArray<float>& Diffs, const TArray<bool>& Turning, float& OutMedian, float Factor = 3.f, float Floor = 50.f);
}

/**
 * The pre-flight robot (-SimCruisePreflight): it drives the real road for 60 s
 * (-SimCruisePreflightSeconds=N) through the real input stack, keeping to the
 * right lane of the road ribbon under it near the road's speed limit, slowing
 * for curves and for cars ahead. It records the view at 10 frames a second as
 * JPEG frames, and measures white sky, pop-in, road seams and jolts, and stuck
 * traffic. When -SimCruiseViewCheck is on, it drives after the VIEWCHECK line.
 *
 * Output: -SimCruisePreflightOut=DIR (default Saved/Preflight/manual-<time>)
 * holds frames/00000.jpg ... and preflight.json, and the log has one line
 * PREFLIGHT PASS|FAIL road=<-SimCruisePreflightRoad=> car=<id> reasons=<...>.
 * It quits the game when done (-SimCruiseKeepOpen keeps it).
 */
UCLASS()
class SIMCRUISE_API ASimCruisePreflight : public AActor
{
	GENERATED_BODY()

public:
	ASimCruisePreflight();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EStage : uint8 { WaitReady, Settle, Drive, Done };

	/** A road centreline in world cm, one per tile ribbon. */
	struct FLine
	{
		TArray<FVector2d> P;
		TArray<double> HalfW;
		FString Class;
		int8 Travel = 0;
		/** The tile's ribbon (alive while the tile is; the lines are gathered again each frame). */
		const FSimCruiseRibbon* Ribbon = nullptr;
	};
	/** A place on a line, walking in Dir (+1 along the stations, -1 against). */
	struct FCursor
	{
		int32 Line = INDEX_NONE;
		int32 Seg = 0;
		double T = 0.0;
		int32 Dir = 1;
		bool IsValid() const { return Line != INDEX_NONE; }
	};

	void GatherLines(const ASimCruiseWorldStreamer* Stream, const FVector& CarCm);
	FCursor FindCursor(const FVector2d& CarXY, const FVector2d& Forward) const;
	/** Walk DistanceCm along the road; false at a dead end (the cursor stops at the end). */
	bool Walk(FCursor& Cursor, double DistanceCm) const;
	FVector2d PointAt(const FCursor& Cursor) const;
	FVector2d TangentAt(const FCursor& Cursor) const;

	void DriveTick(ASimCruisePlayerController* Player, float DeltaSeconds);
	void MeasureTick(ASimCruisePlayerController* Player, float DeltaSeconds);
	void RequestFrame(APlayerController* Player);
	void OnFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	void OnRebased(const FVector& OffsetCm);
	void Finish();

	EStage Stage = EStage::WaitReady;
	double StageStarted = 0.0;
	double Started = 0.0;
	float DriveSeconds = 60.f;
	FString OutputDir;
	FString RoadName;
	FString CarId;
	bool bRequestedStart = false;
	double RequestedLat = 0.0, RequestedLon = 0.0;

	TArray<FLine> Lines;
	FCursor Last;
	float Stick = 0.f;
	float SteerIntegral = 0.f;
	double LastYawDeg = 0.0;
	float JoltCooldown = 0.f;

	// Recording.
	float FrameClock = 0.f;
	bool bFramePending = false;
	/** -SimCruisePreflightNoFrames: no screenshots, so the frame times are the game's own. */
	bool bNoFrames = false;
	/** -SimCruisePreflightKmh=: drive at this speed (slower only for curves and cars), not the road's limit. */
	double SpeedOverrideMps = 0.0;
	bool bKeen = false;
	float TelemetryClock = 0.f;
	/** The robot holds the car on the parking brake while it waits (the brake trigger at rest selects reverse). */
	bool bHoldingParkingBrake = false;
	/** Every drive frame's real time (ms) and the path distance at the long ones. */
	TArray<float> FrameMs;
	/** The frame times the rules judge: after the warm-up, none a screenshot touched. */
	TArray<float> TimedFrameMs;
	/** Frames still to skip after a screenshot request or capture. */
	int32 CaptureSkip = 0;
	/** -SimCruisePreflightForceSpin: the negative test. */
	bool bForceSpin = false;
	float ForceSpinClock = 0.f;
	bool bForceSpinBrake = false;
	float ReverseNow = 0.f;
	/** The car has been above 5 m/s once in this drive. */
	bool bHasDriven = false;
	float OffRibbonNow = 0.f;
	FDelegateHandle FrameHandle;
	int32 FrameIndex = 0;
	TArray<FVector2D> PendingSky;
	FRotator PendingRotation = FRotator::ZeroRotator;
	FRotator LastFrameRotation = FRotator::ZeroRotator;
	TArray<float> LastThumb;
	TArray<float> FrameDiffs;
	TArray<bool> FrameTurning;
	int32 SkyWhite = 0;

	// Measurements.
	FSimCruisePreflightStats Stats;
	/** -SimCruiseReview: frames, sidecars and close-ups for a person to look at. */
	FSimCruiseReviewRecorder Review;
	/** Frames still being written as JPEG on worker threads. */
	TArray<TFuture<void>> JpegJobs;
	double SpeedSum = 0.0;
	int32 SpeedSamples = 0;
	FVector LastCarCm = FVector::ZeroVector;
	bool bHasLastCar = false;
	double PathM = 0.0;
	TArray<FVector2f> Surface;
	TArray<uint8> SurfaceIsRoad;
	/** Where each surface sample was (lat, lon), so a step can be found again. */
	TArray<FVector2d> SurfaceLatLon;
	float OffRoadNow = 0.f;
	double LastVz = 0.0;
	bool bHasLastVz = false;
	float PopClock = 0.f;
	TSet<FIntPoint> KnownTiles;
	TMap<TWeakObjectPtr<AActor>, FVector> KnownCars;
	TSet<TWeakObjectPtr<AActor>> StuckCars;
	FDelegateHandle RebaseHandle;
	TWeakObjectPtr<ASimCruiseWorldStreamer> RebaseStream;
};
