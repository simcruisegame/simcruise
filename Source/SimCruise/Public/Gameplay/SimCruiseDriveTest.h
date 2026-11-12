// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseDriveTest.generated.h"

class ASimCruisePlayerController;

/**
 * A scripted test driver (-SimCruiseDriveTest): once the car stands on the
 * start road with its tiles built, it drives a fixed set of manoeuvres the
 * way a person with a gamepad does, through the real input stack
 * (ASimCruisePlayerController::InjectAxis, so the same dead zones, shaper,
 * rack rates and speed cap apply), and writes one CSV row per game frame
 * with the stick and pedals, the rack, the body's motion and every tyre's
 * load, slip and force.
 *
 * Manoeuvres (seconds after the settle): 0-7 full-throttle launch; 7-11
 * hold 50 km/h; 11-13 a lane change (stick 0.35 right, then left); 15-15.5
 * a quick full-stick flick; 18-21 full throttle then lift mid-way through a
 * gentle turn; 23 on hard braking with a little stick until stopped.
 *
 * -SimCruiseDriveTestPad moves the car onto a flat open pad and drives the
 * limit manoeuvres instead: a turning launch, 20 and 30 km/h junction turns
 * on the throttle, a 50 km/h circle with a lift, a 50 km/h full-stick
 * turn-in, braking in a turn from 70 km/h and an 80 km/h lane change.
 *
 * -SimCruiseDriveLog drives nothing: it records the same CSV while a person
 * plays (the stick column then holds the shaped rack angle in degrees and
 * the pedal columns the shaped pedals) to Saved/DriveTest/drive-log-<time>.csv,
 * saved every 10 s and on exit.
 *
 * Output: Saved/DriveTest/drive-test.csv (-SimCruiseDriveTestOut=FILE)
 * and one summary log line per manoeuvre: peak rear-axle slip angle (the
 * rear axle's velocity angle to the body), peak yaw rate and whether the car
 * spun (rear-axle slip past 15 deg above 3 m/s).
 */
UCLASS()
class SIMCRUISE_API ASimCruiseDriveTest : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseDriveTest();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	enum class EStage : uint8 { WaitCar, Settle, Drive, Done };

	ASimCruisePlayerController* GetPlayer() const;
	void WriteRow(float DeltaSeconds, float Stick, float Throttle, float Brake);
	void Finish();

	/** -SimCruiseDriveTestPad: a 4 km flat pad 60 m above the start, the car moved onto it. */
	bool bPad = false;
	/** -SimCruiseDriveLog: a person drives; record only. */
	bool bLogOnly = false;
	float LastFlush = 0.f;
	int32 PadStep = 0;
	bool bActing = false;
	float StepClock = 0.f;
	float SteadyClock = 0.f;
	void TickPad(ASimCruisePlayerController* Player, float DeltaSeconds);
	void SpawnPad(APawn* Pawn);

	EStage Stage = EStage::WaitCar;
	FString OutputFile;
	TArray<FString> Rows;
	double StageStarted = 0.0;
	float Clock = 0.f;
	int32 SettleFrames = 0;

	/** Per-manoeuvre peaks for the summary. */
	FString Phase;
	double PeakBetaDeg = 0.0;
	double PeakYawDegPerS = 0.0;
	double PeakSpeedKph = 0.0;
	void ClosePhase();
};
