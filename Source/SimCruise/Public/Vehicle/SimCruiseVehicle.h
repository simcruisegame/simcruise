// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/SimCruiseTuning.h"
#include "Vehicle/SimCruiseVehicleTelemetry.h"
#include "SimCruiseVehicle.generated.h"

class UBoxComponent;
class USimCruiseEngineSoundComponent;
class USimCruiseRoadNoiseComponent;
class USimCruiseVehicleMovementComponent;
class USpotLightComponent;

/**
 * A drivable SimCruise car: a simulating Chaos box (the car's collision box
 * from its definition) as the root body, driven by
 * USimCruiseVehicleMovementComponent. Presentation (meshes, wheels, cameras,
 * audio, rumble) attaches to this pawn and reads GetVehicleTelemetry().
 *
 * The root is the collision box centre; the Godot car origin (ground at ride
 * height) is CollisionShapeCenterYM below it. Spawn with SpawnActorDeferred,
 * call SetCarDefinition or set CarId, then finish spawning.
 *
 * It is the driving shell's vehicle (ISimCruiseVehicleControl: the shell's
 * shaped pedals and rack angle in, telemetry out) and feeds the car model
 * (ISimCruiseWheelStateProvider: wheel steer, camber, spin, travel, lamps).
 * The visible car is optional (AttachCarModel), so physics benches stay light.
 */
UCLASS()
class SIMCRUISE_API ASimCruiseVehicle : public APawn, public ISimCruiseVehicleControl, public ISimCruiseWheelStateProvider
{
	GENERATED_BODY()

public:
	ASimCruiseVehicle();

	/** Car file id under data/cars, loaded at BeginPlay when no definition was set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimCruise|Vehicle")
	FString CarId = TEXT("nb1999");

	/** Install a car before spawning finishes (or later: resizes the body and resets). */
	void SetCarDefinition(const FSimCruiseCarDefinition& Car);

	/** The car as its file describes it, before any tuning. */
	const FSimCruiseCarDefinition& GetRealCar() const { return RealCar; }
	/** The real car's setup in tuning units (the "reset" values). */
	FSimCruiseTuning GetStockTuning() const;
	const FSimCruiseTuning& GetTuning() const { return Tuning; }
	/** Rebuild the car from the real one with this tuning (clamped to safe ranges); bSave writes
	 *  it to Saved/SimCruise/tuning-<id>.json (false when that write fails). Resets the
	 *  drivetrain: apply standing still. */
	bool ApplyTuning(const FSimCruiseTuning& InTuning, bool bSave);
	/** Apply the driver's saved tuning for this car, if there is one (the player's car only). */
	bool LoadSavedTuning();

	/**
	 * Build the car's downloaded model on this pawn (blocking the first time a
	 * car is built; later from the cache), posed every frame from the physics.
	 * Returns false (and keeps driving, invisible) when the model is missing.
	 */
	bool AttachCarModel();

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle")
	USimCruiseVehicleMovementComponent* GetVehicleMovement() const { return VehicleMovement; }

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle")
	UBoxComponent* GetBody() const { return Body; }

	UFUNCTION(BlueprintPure, Category = "SimCruise|Vehicle")
	USimCruiseCarPresentationComponent* GetCarModel() const { return CarModel; }

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

	/** Head and tail lamps on (else they follow the sun: on from 1 deg below the horizon). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Vehicle")
	bool bLampsOn = false;

	// ISimCruiseVehicleControl (game thread; see SimCruiseVehicleControl.h).
	virtual FSimCruiseVehicleSetup GetVehicleSetup() const override;
	virtual void GetVehicleTelemetry(FSimCruiseDriveTelemetry& OutTelemetry) const override;
	virtual void ApplyDriverCommand(const FSimCruiseDriverCommand& Command) override;
	virtual FString RequestGearChange(int32 Step) override;
	virtual void SetManualTransmission(bool bManual) override;
	virtual void RequestEngineStart() override;
	/** WorldTransform is a ground pose (the car origin on the road), as the shell's recover passes it. */
	virtual void ResetVehicle(const FTransform& WorldTransform) override;

	// ISimCruiseWheelStateProvider
	virtual bool GetCarVisualState(FSimCruiseCarVisualState& OutState) const override;

	/** Map the vehicle's telemetry to the shell's view of it (exposed for tests). */
	static void ToDriveTelemetry(const FSimCruiseCarDefinition& Car, const FSimCruiseVehicleTelemetry& In, FSimCruiseDriveTelemetry& Out);
	/** Map the vehicle's telemetry to the car model's pose (exposed for tests). */
	static void ToVisualState(const FSimCruiseCarDefinition& Car, const FSimCruiseVehicleTelemetry& In, FSimCruiseCarVisualState& Out);

	/**
	 * Low-beam photometry (a halogen H4 reflector lamp of the late 1990s):
	 * peak intensity per lamp in candela, aimed this far below level, with
	 * the bright core and the cone's edge as half angles, colour 3200 K.
	 */
	static constexpr double LowBeamPeakCandela = 15000.0;
	static constexpr double LowBeamAimDownDeg = 1.5;
	/** The cone's full-intensity core: the beam's own shape comes from the light function (SimCruiseLowBeamPattern), so the cone must not dim it a second time. */
	static constexpr double LowBeamCoreHalfAngleDeg = 30.0;
	/** A real low beam spreads about 40 deg to each side at low intensity; a round cone this wide also spills upward (no cut-off yet). */
	static constexpr double LowBeamEdgeHalfAngleDeg = 32.0;
	/**
	 * What the driver's eye adapts to at night in its own low beams: the luminance of the lit
	 * road ahead (dry asphalt under low beams reads about 0.4-2 cd/m2), not a card facing the
	 * lamps, which metered about 2 cd/m2 and left the 0.1 cd/m2 road black.
	 */
	static constexpr double NightAdaptationLuminanceCdM2 = 0.5;
	/** That luminance as the illuminance on a middle-grey (18 %) Lambertian surface, lux. */
	static constexpr double NightAdaptationLux = NightAdaptationLuminanceCdM2 * UE_DOUBLE_PI / 0.18;
	/** How far the beam reaches before Unreal cuts the light off, metres (the 15000 cd peak gives 0.24 lx there). */
	static constexpr double LowBeamRangeM = 250.0;

	/** The beam lights (empty until AttachCarModel, or when the model has no head lamps). */
	const TArray<TObjectPtr<USpotLightComponent>>& GetHeadLampLights() const { return HeadLampLights; }
	/** Head and tail lamps lit this frame. */
	bool AreLampsLit() const { return AreLampsWanted(); }

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ApplyBodyShape();
	void ApplyBodyShape(const FSimCruiseCarDefinition& Car);
	bool AreLampsWanted() const;

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|Vehicle")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|Vehicle")
	TObjectPtr<USimCruiseVehicleMovementComponent> VehicleMovement;

	UPROPERTY(Transient)
	TObjectPtr<USimCruiseCarPresentationComponent> CarModel;

	/** The engine's voice (the driven car only, while sound is on and an audio device exists). */
	UPROPERTY(Transient)
	TObjectPtr<USimCruiseEngineSoundComponent> EngineSound;

	/** Tyres, road and wind (the driven car only, while sound is on). */
	UPROPERTY(Transient)
	TObjectPtr<USimCruiseRoadNoiseComponent> RoadNoise;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USpotLightComponent>> HeadLampLights;

	/** Beam-pattern light functions taken off by simcruise.Lamps.Pattern 0, to put back. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<USpotLightComponent>, TObjectPtr<UMaterialInterface>> BeamPatterns;

	void UpdateHeadLamps();
	bool bHeadLampsShown = false;

	bool bHasDefinition = false;
	FSimCruiseCarDefinition RealCar;
	FSimCruiseTuning Tuning;
	bool bTuned = false;
	bool bManualRequested = false;
	FString LastShownGearMessage;
	bool bMirrorsLive = false;
	void UpdateEngineSound(const struct FSimCruiseVehicleTelemetry& Telemetry, bool bInSeat, float DeltaSeconds);
	void UpdateRoadNoise(const struct FSimCruiseVehicleTelemetry& Telemetry, bool bInSeat, bool bWant, float DeltaSeconds);
	/** Road roughness for the road noise (RoadNoise.gd update_roughness). */
	double Travel[4] = {};
	bool bTravelKnown = false;
	double TravelSlow = 0.0, Roughness = 0.0;
};
