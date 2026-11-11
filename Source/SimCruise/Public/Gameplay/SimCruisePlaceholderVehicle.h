// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "Vehicle/CarDefinition.h"
#include "SimCruisePlaceholderVehicle.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/**
 * A stand-in car so the driving shell can be driven before the vehicle
 * stream's pawn lands. It is NOT the SimCruise vehicle model and is never used
 * for calibration: a planar single-track (bicycle) model with relaxed slip
 * angles, the car definition's tyre lateral law, a friction ellipse, static
 * load transfer, the engine curve, gearbox and brakes, riding the ground by
 * four line traces. It reports honest telemetry for what it computes, which is
 * enough to exercise steering shaping, cameras, HUD and rumble end to end.
 * Replace it by setting [/Script/SimCruise.SimCruiseGameMode] VehiclePawnClass.
 */
UCLASS()
class SIMCRUISE_API ASimCruisePlaceholderVehicle : public APawn, public ISimCruiseVehicleControl
{
	GENERATED_BODY()

public:
	ASimCruisePlaceholderVehicle();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual FVector GetVelocity() const override;

	/** Use this car's data (called before or after BeginPlay). */
	void SetCarDefinition(const FSimCruiseCarDefinition& InCar);

	// ISimCruiseVehicleControl
	virtual FSimCruiseVehicleSetup GetVehicleSetup() const override;
	virtual void GetVehicleTelemetry(FSimCruiseDriveTelemetry& OutTelemetry) const override;
	virtual void ApplyDriverCommand(const FSimCruiseDriverCommand& InCommand) override;
	virtual FString RequestGearChange(int32 Step) override;
	virtual void SetManualTransmission(bool bInManual) override;
	virtual void RequestEngineStart() override;
	virtual void ResetVehicle(const FTransform& WorldTransform) override;

	/** Advance the model by one fixed step (exposed for tests). */
	void StepPhysics(double DeltaTimeS);
	/** Planar state, SI (exposed for tests). */
	double GetForwardSpeedMps() const { return Vx; }
	double GetLateralSpeedMps() const { return Vy; }
	double GetYawRateRadPerS() const { return YawRate; }

private:
	void BuildVisuals();
	void FollowGround(bool bSnap);
	void UpdateVisuals(double DeltaTimeS);
	double EngineTorqueNm(double Rpm) const;
	double ComputeEngineRpm(double DriveFraction) const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> CabinMesh;
	UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> WheelMeshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Paints;

	FSimCruiseCarDefinition Car;
	FSimCruiseDriverCommand Command;
	double PeakSlipAngleRad = 0.18;

	// Planar state: body-frame velocity (m/s), yaw rate (rad/s, + right), yaw (rad).
	double Vx = 0.0;
	double Vy = 0.0;
	double YawRate = 0.0;
	double YawRad = 0.0;
	double LongitudinalAccel = 0.0;
	double LateralAccel = 0.0;
	double GroundPitchRad = 0.0;
	double GroundRollRad = 0.0;
	double GroundZ = 0.0;
	bool bGrounded = false;
	FVector PlanarLocationCm = FVector::ZeroVector;
	double PhysicsAccumulatorS = 0.0;

	// Per axle (0 front, 1 rear): relaxed slip angle, slip ratio, ABS release.
	double AxleSlipAngle[2] = {0.0, 0.0};
	double AxleSlipRatio[2] = {0.0, 0.0};
	double AxleLoadN[2] = {0.0, 0.0};
	bool bAxleAbs[2] = {false, false};
	double WheelTravelM[4] = {0.0, 0.0, 0.0, 0.0};
	double WheelSpinRad[4] = {0.0, 0.0, 0.0, 0.0};

	// Gearbox: 1..N forward, -1 reverse, 0 neutral.
	int32 Gear = 1;
	bool bManual = false;
	bool bReverse = false;
	bool bServiceBrake = false;
	double ShiftTimerS = 0.0;
	double EngineRpm = 850.0;
	double DriveFraction = 0.0;
};
