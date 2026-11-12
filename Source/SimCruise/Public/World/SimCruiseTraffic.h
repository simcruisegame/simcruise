// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "SimCruiseTraffic.generated.h"

class ASimCruiseWorldStreamer;
class UBoxComponent;
class USimCruiseEngineSoundComponent;
struct FSimCruiseRibbon;

/** Pure traffic rules, testable without a world. */
namespace SimCruiseTrafficModel
{
	/**
	 * The Intelligent Driver Model (Treiber, Hennecke and Helbing 2000): acceleration
	 * (m/s^2) at speed V toward desired speed V0, with a leader GapM ahead (bumper to
	 * bumper) closing at ClosingMps (positive when approaching). No leader: GapM < 0.
	 */
	SIMCRUISE_API double IdmAcceleration(double V, double V0, double GapM, double ClosingMps);

	/** US urban speed limit (m/s) a road class is usually posted at. */
	SIMCRUISE_API double SpeedLimitMps(const FString& Class);

	/**
	 * Lateral offset (m, + toward the station's R) of the rightmost travel lane for a car
	 * driving the ribbon in direction Dir (+1 along S, -1 against), at station I: the
	 * lane the markings draw, inside any parking lane on the driver's right.
	 */
	SIMCRUISE_API double LaneOffsetM(const FSimCruiseRibbon& Ribbon, int32 I, int32 Dir, int32 Lane = 0);

	/** How many travel lanes a car driving the ribbon in Dir has at station I (at least 1). */
	SIMCRUISE_API int32 LaneCount(const FSimCruiseRibbon& Ribbon, int32 I, int32 Dir);

	/**
	 * The next place along the ribbon (m, station S) where a car driving in Dir from S
	 * chooses its way: the centre of the next run of stations another carriageway crosses
	 * (FSimCruiseRibbonSide::Cut) beyond 2 m ahead and not within 15 m of SkipNearS (the
	 * junction just passed), or the ribbon's end (bEnd true).
	 */
	SIMCRUISE_API double NextDecisionS(const FSimCruiseRibbon& Ribbon, int32 Dir, double S, double SkipNearS, bool& bEnd);
}

/**
 * One car of the moving traffic: a car body (the parked cars' models and colours) moved
 * kinematically along a lane by ASimCruiseTraffic, wheels rolling and steering, brake
 * lamps when it brakes, lamps at night, and a box the player's car can hit.
 */
UCLASS(NotPlaceable, Transient)
class SIMCRUISE_API ASimCruiseTrafficCar : public AActor, public ISimCruiseWheelStateProvider
{
	GENERATED_BODY()

public:
	ASimCruiseTrafficCar();

	bool Setup(const FString& InCarId, const FLinearColor& Paint);
	virtual bool GetCarVisualState(FSimCruiseCarVisualState& OutState) const override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USimCruiseCarPresentationComponent> Presentation;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Collision;
	/** The engine heard from outside, in place (only while the car is near the player). */
	UPROPERTY(Transient)
	TObjectPtr<USimCruiseEngineSoundComponent> Voice;
	FString CarId;
	double Accel = 0.0;

	// Path: a ribbon of a tile, driven in Dir from station position S (m along the ribbon).
	FIntPoint Tile = FIntPoint::ZeroValue;
	int32 Ribbon = INDEX_NONE;
	int32 Dir = 1;
	double S = 0.0;
	double Speed = 0.0;
	double DesiredSpeed = 10.0;
	double LengthM = 4.5;
	double TyreRadiusCm = 30.0;
	/** World offset and heading error left by a change of ribbon, eased out over about 2 s. */
	FVector BlendCm = FVector::ZeroVector;
	double BlendYawDeg = 0.0;
	double SteerRad = 0.0;
	double Spin = 0.0;
	/** The front-left wheel's drawn roll at the last traffic report (checks that the wheels visibly roll). */
	double ReportedSpinRad = 0.0;
	bool bBraking = false;
	bool bLamps = false;
	/** The successor chosen ahead of the ribbon's end (tile, ribbon, dir, entry S); Ribbon INDEX_NONE until chosen. */
	FIntPoint NextTile = FIntPoint::ZeroValue;
	int32 NextRibbon = INDEX_NONE;
	int32 NextDir = 1;
	double NextS = 0.0;
	double NextTurnDeg = 0.0;
	bool bNextChosen = false;
	uint32 Seed = 1;
	/**
	 * The junction at this ribbon's end: 0 none or a priority road, 1 a stop ahead (a
	 * minor approach, or an all-way stop between equal minor streets), 2 stopped and
	 * waiting for a gap, 3 clear to go.
	 */
	int32 StopState = 0;
	FVector JunctionCm = FVector::ZeroVector;
	double StopBackM = 6.0;
	double WaitS = 0.0;
	/** Travel lane, counted from the right (0); a change eases sideways over about 3 s. */
	int32 Lane = 0;
	FVector LaneShiftCm = FVector::ZeroVector;
	double LaneChangeCooldownS = 0.0;
	/** What set the gap last frame: 0 nothing, 1 a car in the lane, 2 the player, 3 a crossing car, 4 a stop line, 5 giving way on a left turn. */
	int32 Why = 0;
	bool bYielding = false;
	/** Seconds at a standstill (below 0.3 m/s), for the deadlock report and the gridlock breaker. */
	double StandS = 0.0;
	/** Where the thing this car waited for last frame stood (the car or player ahead or across; zero for a stop line or a yield). */
	FVector BlockerCm = FVector::ZeroVector;
	/** Where the chosen way on starts (m along the ribbon), whether that is the ribbon's end, and whether it is straight on. */
	double DecisionS = 0.0;
	bool bDecisionAtEnd = true;
	bool bNextStraight = false;
	/** The last junction passed straight through (not chosen again). */
	double LastDecisionS = -1e9;
};

/**
 * The moving traffic round the player's car: keeps about MaxCars cars within the radius,
 * spawning them on public roads out of the driver's sight and removing them far away.
 * Each follows its lane (the lane geometry the markings use), continues onto the next
 * ribbon at its end (straight on across a tile seam, or a turn at a junction, slowing for
 * it), keeps its distance from the car ahead and from the player (IDM), and drives the
 * class's usual US speed limit, give or take a tenth.
 */
UCLASS(NotPlaceable, Transient)
class SIMCRUISE_API ASimCruiseTraffic : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseTraffic();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	TWeakObjectPtr<ASimCruiseWorldStreamer> Streamer;

	int32 NumCars() const { return Cars.Num(); }
	/** Tests: drive the traffic round this point with no player (bTestFocus). */
	bool bTestFocus = false;
	FVector TestFocusCm = FVector::ZeroVector;

	/** Cars that have stood still longer than Seconds (the deadlock soak test reads it). */
	int32 CountStuckCars(double Seconds = 20.0) const;
	/** Cars that left a queue behind a car standing for good (you parked in the lane). */
	int32 GetQueueGiveUps() const { return QueueGiveUps; }

	/** Remove every car (a jump to another place: their roads are gone). */
	void ClearCars();

private:
	struct FPose
	{
		FVector LocationCm = FVector::ZeroVector;
		FVector2d Heading = FVector2d(1.0, 0.0);
		bool bValid = false;
	};

	const FSimCruiseRibbon* GetRibbon(const FIntPoint& Tile, int32 Ribbon) const;
	FPose PoseAt(const FIntPoint& Tile, int32 Ribbon, int32 Dir, double S, int32 Lane = 0) const;
	/** Whether a car could move into Lane here: nobody within 15 m behind or 25 m ahead in it. */
	bool LaneClear(const ASimCruiseTrafficCar& Car, int32 Lane, const FVector& PlayerCm) const;
	bool Spawn(const FVector& FocusCm, const FVector& PlayerFwd, const FVector& CameraCm, const FVector& CameraFwd, bool bAnywhere);
	/** Picks the way on at DecisionS (a junction along the ribbon, or its end when bAtEnd). */
	bool ChooseAt(ASimCruiseTrafficCar& Car, double DecisionS, bool bAtEnd) const;
	void Advance(ASimCruiseTrafficCar& Car, double Dt, const FVector& PlayerCm, const FVector& PlayerVelCm);
	/** Starts, feeds or stops a car's engine voice by its distance from the listener. */
	void UpdateVoice(ASimCruiseTrafficCar& Car, double DistanceM, bool bSound);

	UPROPERTY(Transient)
	TArray<TObjectPtr<ASimCruiseTrafficCar>> Cars;
	uint32 Random = 0x5eed1234;
	double SinceStart = 0.0;
	double ReportClock = 5.0;
	int32 RibbonChanges = 0, Spawned = 0, StopsPassed = 0, LaneChanges = 0, LeftTurnYields = 0, QueueGiveUps = 0;
	/** Car-frames held at a red or late yellow signal (for the report). */
	int64 SignalHolds = 0;
	TWeakObjectPtr<const class ASimCruiseSignals> CachedSignals;
	double LastSignalLookup = -10.0;
};
