// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/EngineTypes.h"
#include "Templates/Function.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/SimCruiseVehicleSim.h"
#include "Vehicle/SimCruiseVehicleTelemetry.h"
#include "SimCruiseVehicleMovementComponent.generated.h"

class UPrimitiveComponent;
struct FSimCruiseVehiclePhysicsState;
class FSimCruiseVehicleSimCallback;

/**
 * What a physics-thread driver (a bench, a replay, an autopilot) sees and may
 * change before each fixed step. Runs on the physics thread with the vehicle
 * locked; never touch UObjects from it.
 */
struct FSimCruiseVehicleDriverContext
{
	FSimCruiseVehicleSim& Sim;
	/** Body state for this step; edit it and set bBodyStateChanged to teleport or set velocities. */
	FSimCruiseBodyState& Body;
	FSimCruiseVehicleControls& Controls;
	double DeltaTimeS = 0.0;
	/** Physics time at the start of this step. */
	double SimTimeS = 0.0;
	int64 StepIndex = 0;
	bool bBodyStateChanged = false;
};

/**
 * Drives a Chaos rigid body as a car. The SimCruise vehicle model
 * (FSimCruiseVehicleSim) runs on the physics thread in a Chaos sim callback
 * at the solver's fixed asynchronous step; forces and torque go straight to
 * the physics particle each step. Controls set here reach the physics thread
 * on its next step; telemetry is the last completed step.
 *
 * The owner's root component must be a simulating primitive (ASimCruiseVehicle
 * builds one from the car's collision box).
 */
UCLASS(ClassGroup = (SimCruise), meta = (BlueprintSpawnableComponent))
class SIMCRUISE_API USimCruiseVehicleMovementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimCruiseVehicleMovementComponent();

	/** Install a car (before BeginPlay, or later to swap cars and reset). */
	void SetCarDefinition(const FSimCruiseCarDefinition& Car);
	const FSimCruiseCarDefinition& GetCarDefinition() const;

	/** Load data/cars/<CarId>.json and install it. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle")
	bool LoadCarById(const FString& CarId);

	// Driver controls (game thread).
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetThrottle(float Value);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetBrake(float Value);

	/** -1..1, positive right. bFromThumbstick applies the stick's centre shaping and rates. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetSteer(float Value, bool bFromThumbstick = false);

	/** Replace every driver control at once (the driving shell, once per frame). */
	void SetDriverControls(const FSimCruiseVehicleControls& Controls);

	/** Rear parking brake. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetHandbrake(bool bEngaged);

	/** Full service brake held regardless of the pedal. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetFullBrake(bool bEngaged);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetClutch(float Value);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void ShiftUp();

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void ShiftDown();

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void ToggleManual();

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetManualTransmission(bool bManual);

	/** Restart a stalled engine (manual: in neutral or with the clutch down). */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void RequestEngineStart();

	/** Water on the road (0 dry .. 1 soaked): the tyres' grip falls with it, by surface. */
	void SetRoadWetness(float Wetness);

	/** The air the car drives through: density (kg/m3) and wind at its height (m/s, world axes). */
	void SetAir(double DensityKgPerM3, const FVector& WindMps);

	/** Assisted steering for keys and sticks; off for a steering wheel or a bench servo. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetSteeringAssist(bool bAssist);

	/** Teleport the body, stop it, and restart the drivetrain (Powertrain.gd restart). */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle")
	void ResetVehicle(const FTransform& BodyTransform);

	// Telemetry (game thread copy of the last physics step).
	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle|Telemetry")
	FSimCruiseVehicleTelemetry GetTelemetry() const;

	void CopyTelemetry(FSimCruiseVehicleTelemetry& OutTelemetry) const;

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle|Telemetry")
	float GetForwardSpeedMps() const;

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle|Telemetry")
	float GetEngineRpm() const;

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle|Telemetry")
	FString GetGearText() const;

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle|Telemetry")
	float GetSteeringAngleDeg() const;

	/**
	 * Run a function on the physics thread before every vehicle step (benches,
	 * replays). Pass an empty function to remove it.
	 */
	void SetPhysicsThreadDriver(TFunction<void(FSimCruiseVehicleDriverContext&)> Driver);

	/** Run a function on the physics thread with the vehicle locked (reads between steps). */
	void ReadPhysicsState(TFunctionRef<void(const FSimCruiseVehicleSim&)> Reader) const;

	/** The longest wheel/driveline sub-interval; the solver's fixed step is split to stay under it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimCruise|Vehicle|Physics")
	double MaxWheelSubstepSeconds = 1.0 / 360.0;

	/** Channel the suspension rays trace on. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimCruise|Vehicle|Physics")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	/** Assisted steering at BeginPlay (Powertrain.gd: a car a person drives). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimCruise|Vehicle")
	bool bSteeringAssist = true;

	/** Start in the manual gearbox. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimCruise|Vehicle")
	bool bStartManual = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ConfigureBody();
	UPrimitiveComponent* GetBodyComponent() const;

	/**
	 * The root body's Chaos particle is replaced whenever its physics state is
	 * recreated (collision switched off and on, a re-register): forget the old
	 * particle before the physics thread can touch it, and configure the new one.
	 */
	UFUNCTION()
	void HandleBodyPhysicsStateChanged(UPrimitiveComponent* ChangedComponent, EComponentPhysicsStateChange StateChange);

	TWeakObjectPtr<UPrimitiveComponent> BoundBody;

	TSharedPtr<FSimCruiseVehiclePhysicsState, ESPMode::ThreadSafe> PhysicsState;
	FSimCruiseVehicleSimCallback* SimCallback = nullptr;
};
