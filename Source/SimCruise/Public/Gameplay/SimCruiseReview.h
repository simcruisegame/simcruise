// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ASimCruisePlayerController;

/** Steering and gauge measurements that need no world, kept apart for the tests. */
namespace SimCruiseReviewRules
{
	/** Median of |rim| / |road wheel| over the samples where the road wheel is turned more than MinWheelDeg (0 when none). */
	SIMCRUISE_API double SteeringRatio(const TArray<FVector2D>& RimDegWheelDeg, double MinWheelDeg = 2.0);
	/** Steering-wheel turns from full lock to full lock: 2 x the road wheel's full lock x the ratio, over 360. */
	SIMCRUISE_API double LockToLockTurns(double FullLockRoadWheelDeg, double Ratio);
	/** A gauge reads wrong when it is off the true value by more than this share of it, or this many units at low values. */
	constexpr double MaxGaugeErrorShare = 0.05;
	constexpr double MaxGaugeErrorFloor = 2.0;
	SIMCRUISE_API bool GaugeIsWrong(double Needle, double True);
}

/**
 * Evidence for a person to review (-SimCruiseReview), recorded by the pre-flight robot while it
 * drives: full-size PNG frames at the start, every 5 s and at each event (a pre-flight flag, a
 * spin, a run-off, a hitch over 100 ms, a tile built, a place card shown), each with a JSON
 * sidecar: the true speed and engine speed next to what the speedometer and tachometer needles
 * show, the stick, the rack, the road wheels' drawn steer and the steering wheel's drawn angle,
 * and screen boxes for close-ups (steering wheel, instrument cluster, mirrors, windscreen).
 * At the end, review.json gives the steering ratio the wheel is drawn with (rim degrees per
 * road-wheel degree), lock-to-lock turns, the fastest rim speed, and the gauge errors.
 * Output: <dir>/review/NNN-<seconds>s-<event>.png and .json, <dir>/review.json.
 */
class SIMCRUISE_API FSimCruiseReviewRecorder
{
public:
	void Begin(const FString& InOutputDir);
	bool IsOn() const { return bOn; }

	/** Each drive frame: the steering and gauge samples, the place card, and the timed shots. */
	void Tick(ASimCruisePlayerController& Player, float DriveSeconds, float DeltaSeconds, float Stick);
	/** Something worth a picture happened; at most one event shot a second. */
	void Event(const FString& Name, float DriveSeconds);

	/** A shot is due: the next screenshot is also saved full size. */
	bool WantsShot() const { return bOn && !PendingName.IsEmpty() && !bPrepared; }
	/** At the screenshot request: the car's state and the close-up boxes for this frame. */
	void PrepareShot(ASimCruisePlayerController& Player, float DriveSeconds);
	/** The screenshot's pixels: saves the PNG and its sidecar when a shot was prepared. */
	void OnFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels);

	/** review.json: steering, gauges, events and shots. */
	void Finish(const FString& Verdict, const FString& Reasons);

	/**
	 * The graphics the engine really has now, as one "REVIEW SETTINGS" log line and JSON: preset,
	 * sg.* levels, output resolution, r.ScreenPercentage, dynamic resolution and the mean actual
	 * screen percentage sampled during the drive. valid=yes only at the top preset, every sg.*
	 * group at the preset's level or higher, the screen's own resolution (-SimCruiseNativeRes=WxH,
	 * else the primary display) and a screen percentage not below the preset's.
	 */
	FString SettingsJson(bool bLog);

private:
	void SampleResolution();
	double ResFractionSum = 0.0;
	int32 ResFractionSamples = 0;
	float ResFractionMin = 0.f;
	float ResFractionMax = 0.f;
	bool bOn = false;
	FString OutputDir;
	float NextTimedS = 0.f;
	float LastEventS = -10.f;
	int32 ShotIndex = 0;
	FString PendingName;
	bool bPrepared = false;
	FString PendingSidecar;
	FString LastPlaceCard;
	float PlaceClock = 0.f;
	/** The stick the driver (the robot) gives now, -1..1. */
	float StickNow = 0.f;

	// Steering.
	TArray<FVector2D> RimWheel;
	double LastRimDeg = 0.0;
	bool bHasRim = false;
	double MaxRimRateDps = 0.0;
	double MaxRimDeg = 0.0;
	double MaxWheelDeg = 0.0;
	double FullLockDeg = 0.0;
	double ProfileRatio = 0.0;
	FString CarId;

	// Gauges.
	FString SpeedUnit;
	TArray<double> SpeedErrors;
	TArray<double> RpmErrors;
	int32 SpeedWrongSamples = 0;
	int32 RpmWrongSamples = 0;
	int32 GaugeSamples = 0;
	double LastSpeedMps = 0.0;

	TArray<FString> Events;
	TArray<FString> Shots;
};
