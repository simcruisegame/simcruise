// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/SimCruiseVehicleTelemetry.h"
#include "SimCruiseVehicle.generated.h"

class UBoxComponent;
class USimCruiseVehicleMovementComponent;

/**
 * A drivable SimCruise car: a simulating Chaos box (the car's collision box
 * from its definition) as the root body, driven by
 * USimCruiseVehicleMovementComponent. Presentation (meshes, wheels, cameras,
 * audio, rumble) attaches to this pawn and reads GetVehicleTelemetry().
 *
 * The root is the collision box centre; the Godot car origin (ground at ride
 * height) is CollisionShapeCenterYM below it. Spawn with SpawnActorDeferred,
 * call SetCarDefinition or set CarId, then finish spawning.
 */
UCLASS()
class SIMCRUISE_API ASimCruiseVehicle : public APawn
{
	GENERATED_BODY()

public:
	ASimCruiseVehicle();

	/** Car file id under game/cars, loaded at BeginPlay when no definition was set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimCruise|Vehicle")
	FString CarId = TEXT("nb1999");

	/** Install a car before spawning finishes (or later: resizes the body and resets). */
	void SetCarDefinition(const FSimCruiseCarDefinition& Car);

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle")
	USimCruiseVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle")
	UBoxComponent* GetBody() const { return Body; }

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle|Telemetry")
	FSimCruiseVehicleTelemetry GetVehicleTelemetry() const;

	// Driving-shell interface (forwards to the movement component).
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetThrottle(float Value);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetBrake(float Value);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetSteer(float Value, bool bFromThumbstick = false);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetHandbrake(bool bEngaged);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void SetClutch(float Value);

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void ShiftUp();

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void ShiftDown();

	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle|Controls")
	void ToggleManual();

	/** Place the car with its wheels on the ground at Location (Godot origin) facing Yaw (deg), stopped. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Vehicle")
	void ResetAt(const FVector& GroundLocation, float YawDeg);

	/** World transform of the root body for a car origin at GroundLocation facing YawDeg. */
	FTransform BodyTransformForGroundPose(const FVector& GroundLocation, float YawDeg) const;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void ApplyBodyShape();

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|Vehicle")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|Vehicle")
	TObjectPtr<USimCruiseVehicleMovementComponent> VehicleMovement;

	bool bHasDefinition = false;
};
