// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Templates/Function.h"
#include "Tests/AutomationCommon.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

class ASimCruiseVehicle;
class UWorld;

/**
 * A throwaway game world for vehicle benches: a flat static box whose top is
 * the plane Z = 0, the Chaos solver ticking asynchronously at a chosen fixed
 * step on a single thread (deterministic), and a manual tick loop.
 */
class FSimCruiseVehicleBenchWorld
{
public:
	explicit FSimCruiseVehicleBenchWorld(double FixedStepS, double GroundHalfSizeM = 3000.0, bool bSingleThreaded = true);
	~FSimCruiseVehicleBenchWorld();

	bool IsValid() const { return World != nullptr; }
	UWorld* GetWorld() const { return World; }
	double GetFixedStepS() const { return FixedStepS; }

	/** Spawn a car with its Godot origin at GroundLocationM (metres) facing +X; benches use Direct steering. */
	ASimCruiseVehicle* SpawnVehicle(const FSimCruiseCarDefinition& Car, const FVector3d& GroundLocationM,
		TFunction<void(FSimCruiseVehicleDriverContext&)> Driver, bool bSteeringAssist = false,
		double MaxWheelSubstepS = 1.0 / 360.0);

	/** Tick the world until Done() returns true or MaxSimSeconds of physics have elapsed. */
	bool RunUntil(TFunctionRef<bool()> Done, double MaxSimSeconds);

	/** Body (root) location in metres for a car origin at GroundLocationM. */
	static FVector3d BodyLocationForGround(const FSimCruiseCarDefinition& Car, const FVector3d& GroundLocationM);

private:
	FTestWorldWrapper Wrapper;
	UWorld* World = nullptr;
	double FixedStepS = 1.0 / 360.0;
};

/** Load a canonical car or fail the test. */
bool SimCruiseLoadBenchCar(FAutomationTestBase& Test, const FString& CarId, FSimCruiseCarDefinition& OutCar);

#endif
