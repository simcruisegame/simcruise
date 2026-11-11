// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "SimCruiseTraffic.generated.h"

class ASimCruiseWorldStreamer;
class UBoxComponent;
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
	SIMCRUISE_API double LaneOffsetM(const FSimCruiseRibbon& Ribbon, int32 I, int32 Dir);
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

	bool Setup(const FString& CarId, const FLinearColor& Paint);
	virtual bool GetCarVisualState(FSimCruiseCarVisualState& OutState) const override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USimCruiseCarPresentationComponent> Presentation;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Collision;

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

private:
	struct FPose
	{
		FVector LocationCm = FVector::ZeroVector;
		FVector2d Heading = FVector2d(1.0, 0.0);
		bool bValid = false;
	};

	const FSimCruiseRibbon* GetRibbon(const FIntPoint& Tile, int32 Ribbon) const;
	FPose PoseAt(const FIntPoint& Tile, int32 Ribbon, int32 Dir, double S) const;
	bool Spawn(const FVector& FocusCm, const FVector& CameraCm, const FVector& CameraFwd, bool bAnywhere);
	bool ChooseNext(ASimCruiseTrafficCar& Car) const;
	void Advance(ASimCruiseTrafficCar& Car, double Dt, const FVector& PlayerCm, const FVector& PlayerVelCm);

	UPROPERTY(Transient)
	TArray<TObjectPtr<ASimCruiseTrafficCar>> Cars;
	uint32 Random = 0x5eed1234;
	double SinceStart = 0.0;
	double ReportClock = 5.0;
	int32 RibbonChanges = 0, Spawned = 0;
};
