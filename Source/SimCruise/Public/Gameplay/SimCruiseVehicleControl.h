// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SimCruiseVehicleControl.generated.h"

/**
 * The narrow seam between the driving shell (input, cameras, HUD, rumble) and
 * whatever simulates the car. The shell only reads telemetry and writes the
 * driver's controls; it never applies a force, so the vehicle model and every
 * calibration band stay owned by the vehicle stream.
 *
 * Conventions, all SI and in the pawn's own body frame (Unreal axes):
 *   +X forward, +Y right, +Z up; yaw rate is positive turning right.
 *   Rack angle is the mean road-wheel steer angle of the front axle, radians,
 *   positive steering to the right (the stick's +X).
 *   Wheel index: 0 front left, 1 front right, 2 rear left, 3 rear right
 *   (the same order as game/Powertrain.gd MOUNTS).
 * Locations handed to the camera (driver eye) are in the pawn's local space
 * in centimetres, because that is where Unreal places a camera.
 */

/** What the driver does with the controls this frame. Pedals are already
 *  shaped by the shell (key ramps, trigger dead zone); the rack angle is the
 *  shell's steering-shaper output. The vehicle applies them as a real car's
 *  controls would: no extra filtering, no stability aid beyond the car's own. */
struct FSimCruiseDriverCommand
{
	/** Accelerator pedal, 0..1. */
	float Throttle = 0.f;
	/** Brake pedal, 0..1. In automatic mode the vehicle decides, as Godot's
	 *  Powertrain.drive does, whether a held brake below ~0.45 m/s selects reverse. */
	float Brake = 0.f;
	/** Clutch pedal, 0 engaged (foot off) .. 1 fully pressed. */
	float Clutch = 0.f;
	/** Parking brake on the rear wheels, 0..1. */
	float ParkingBrake = 0.f;
	/** Space bar: full service brake regardless of the pedal. */
	bool bFullServiceBrake = false;
	/** Requested mean front road-wheel angle, radians, positive right. */
	double RackAngleRad = 0.0;
};

/** One tyre as the shell needs to see it (rumble, telemetry HUD). */
struct FSimCruiseWheelTelemetry
{
	bool bContact = false;
	bool bDriven = false;
	/** True while ABS is releasing this wheel's brake (Powertrain abs_release < 0.95). */
	bool bAbsActive = false;
	double NormalLoadN = 0.0;
	/** Longitudinal slip ratio kappa (signed) and the ratio where the curve peaks. */
	double SlipRatio = 0.0;
	double PeakSlipRatio = 0.1;
	/** Relaxed lateral slip angle (rad, signed) and the angle where the lateral
	 *  force first reaches 98 % of its maximum (see SimCruiseHaptics::PlateauSlipAngleRad). */
	double SlipAngleRad = 0.0;
	double PeakSlipAngleRad = 0.0;
	/** Suspension compression from full droop, metres (drives head vibration). */
	double SuspensionTravelM = 0.0;
	/** Surface grip multiplier under the tyre: about 1 paved, 0.7 gravel or verge. */
	double SurfaceGrip = 1.0;
};

/** Per-frame state of the car. Missing data keeps its default, so a partial
 *  vehicle still drives the shell (no rumble from data it does not report). */
struct FSimCruiseVehicleTelemetry
{
	/** Body-frame velocity, m/s, and angular velocity, rad/s (X roll, Y pitch, Z yaw). */
	FVector BodyVelocityMps = FVector::ZeroVector;
	FVector BodyAngularVelocityRadPerS = FVector::ZeroVector;
	double EngineSpeedRpm = 0.0;
	/** -1 reverse, 0 neutral, 1.. forward gears. */
	int32 Gear = 0;
	bool bManualTransmission = false;
	bool bEngineStalled = false;
	bool bElectric = false;
	bool bServiceBrakeOn = false;
	/** Rack angle actually applied this tick, radians, positive right. */
	double RackAngleRad = 0.0;
	/** Throttle times combustion load, 0..1 (the faint engine buzz). */
	double EngineLoad = 0.0;
	/** Road roughness under the car, 0 smooth .. 1 kerb/pothole (RoadNoise.roughness). */
	double RoadRoughness = 0.0;
	FSimCruiseWheelTelemetry Wheels[4];
};

/** Fixed facts about the mounted car that the shell shapes its controls and
 *  cameras around. All come from the car definition (game/cars/*.json). */
struct FSimCruiseVehicleSetup
{
	FString CarId;
	FString DisplayName;
	double WheelbaseM = 2.266;
	/** Distance from the centre of mass forward to the front axle, metres. */
	double CenterOfMassToFrontAxleM = 1.10;
	double ParkingRackAngleRad = FMath::DegreesToRadians(36.6);
	double SteeringAssistLateralG = 0.95;
	double SteeringSlipLimitSurplusRad = FMath::DegreesToRadians(5.0);
	/** Steering-wheel turns per road-wheel turn; 0 when unsourced. */
	double SteeringRatio = 0.0;
	/** Driver's eye in the pawn's local space, cm, and the direction of a
	 *  relaxed gaze down the road (local, unit length). */
	FVector DriverEyeLocalCm = FVector(-50.3, -34.7, 100.9);
	FVector DriverSightLocalDirection = FVector(1.0, 0.0, -0.0267).GetSafeNormal();
	double IdleRpm = 850.0;
	double UpshiftRpm = 6700.0;
	double RedlineRpm = 7000.0;
	int32 ForwardGearCount = 5;
	/** Published skidpad (lateral) and braking (from the 70-0 mph distance) limits, g. */
	double SkidpadG = 0.89;
	double BrakingG = 0.95;
};

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USimCruiseVehicleControl : public UInterface
{
	GENERATED_BODY()
};

/** Implemented by the vehicle pawn. Seven calls, no Blueprint surface. */
class SIMCRUISE_API ISimCruiseVehicleControl
{
	GENERATED_BODY()

public:
	virtual FSimCruiseVehicleSetup GetVehicleSetup() const = 0;
	virtual void GetVehicleTelemetry(FSimCruiseVehicleTelemetry& OutTelemetry) const = 0;
	/** Called once per rendered frame before physics; the vehicle holds the
	 *  command for every physics substep until the next call. */
	virtual void ApplyDriverCommand(const FSimCruiseDriverCommand& Command) = 0;
	/** Manual gearbox: +1 up, -1 down. Returns a refusal or status line to show,
	 *  empty when the change needs no message (Godot Powertrain.change_gear). */
	virtual FString RequestGearChange(int32 Step) = 0;
	virtual void SetManualTransmission(bool bManual) = 0;
	virtual void RequestEngineStart() = 0;
	/** Teleport upright and at rest (recover-to-road, restarts). */
	virtual void ResetVehicle(const FTransform& WorldTransform) = 0;
};

/** Converts game/cars/*.json coordinates (Godot: +X right, +Y up, -Z forward,
 *  metres) into pawn-local Unreal centimetres (+X forward, +Y right, +Z up). */
inline FVector SimCruiseGodotToLocalCm(const FVector3d& GodotMetres)
{
	return FVector(-GodotMetres.Z * 100.0, GodotMetres.X * 100.0, GodotMetres.Y * 100.0);
}
