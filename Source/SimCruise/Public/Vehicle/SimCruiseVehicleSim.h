// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Vehicle/CarDefinition.h"

/**
 * SimCruise vehicle dynamics, independent of any engine object.
 *
 * A faithful port of godot-final:game/Powertrain.gd (contacts, springs, dampers, anti-roll
 * bars, relaxed slip angles, combined-slip slip-ratio tyres, engine, clutch,
 * gearbox, differentials, brakes with ABS, traction control, aero and rolling
 * resistance) and godot-final:game/CarSuspension.gd. Everything is SI, in Unreal's axes:
 * X forward, Y right, Z up. A positive steer turns right.
 *
 * One Step() is one rigid-body interval: contact geometry, loads and slip
 * angles are evaluated once, then wheel spin, driveline and longitudinal tyre
 * force are integrated over Substeps equal sub-intervals, and the mean force
 * is returned for the interval. Godot ran this at 60 Hz with 6 substeps; the
 * Chaos adapter runs it on the physics thread at a fixed rate (see
 * docs/unreal-architecture.md, "Vehicle physics core").
 */

/** Driver inputs, already in pedal/stick fractions. */
struct FSimCruiseVehicleControls
{
	/** Accelerator, 0..1. In automatic it also selects drive near standstill. */
	double Throttle = 0.0;
	/** Brake pedal, 0..1. In automatic, below 0.45 m/s it selects reverse and drives. */
	double Brake = 0.0;
	/** Steering request, -1..1, positive right. A thumbstick is shaped by the vehicle. */
	double Steer = 0.0;
	/** Clutch pedal, 0..1 (manual gearbox only). */
	double Clutch = 0.0;
	/** The steering request comes from a thumbstick (Godot's pad steering). */
	bool bThumbstickSteer = false;
	/** Rear parking brake (lever). */
	bool bHandbrake = false;
	/** Full service brake held (Godot's "handbrake" action on Space). */
	bool bFullBrake = false;
	/**
	 * The driving shell has already shaped the steering (rates, speed cap,
	 * countersteer allowance) into a rack angle: apply RackAngleRad as it is,
	 * limited only by the car's lock, instead of Steer.
	 */
	bool bDirectRack = false;
	/** Mean front road-wheel angle, radians, positive right (with bDirectRack). */
	double RackAngleRad = 0.0;
};

/** Rigid-body state at the start of an interval (SI, world frame, Unreal axes). */
struct FSimCruiseBodyState
{
	/** Body (root component) origin. */
	FVector3d PositionM = FVector3d::ZeroVector;
	FQuat4d Rotation = FQuat4d::Identity;
	FVector3d CenterOfMassM = FVector3d::ZeroVector;
	/** Velocity of the centre of mass. */
	FVector3d LinearVelocityMps = FVector3d::ZeroVector;
	FVector3d AngularVelocityRadPerSecond = FVector3d::ZeroVector;
};

/** What a suspension ray found. */
struct FSimCruiseGroundHit
{
	FVector3d PointM = FVector3d::ZeroVector;
	FVector3d NormalM = FVector3d::UnitZ();
	/** Grip of this surface relative to dry asphalt. */
	double SurfaceGrip = 1.0;
	/** Road roughness under the tyre (m, + raises the road) and its rate (m/s). */
	double RoadProfileM = 0.0;
	double RoadProfileRateMps = 0.0;
};

/** Segment query against drivable collision: true and a hit when the segment Start->End meets ground. */
using FSimCruiseGroundQuery = TFunctionRef<bool(const FVector3d& StartM, const FVector3d& EndM, FSimCruiseGroundHit& OutHit)>;

/** Wheel order everywhere: 0 front left, 1 front right, 2 rear left, 3 rear right. */
struct FSimCruiseWheelState
{
	double AngularSpeedRadPerSecond = 0.0;
	/** Rolling (relaxed) slip-angle state. */
	double RollingSlipAngleRad = 0.0;
	/** Slip angle the last interval's lateral force used. */
	double SlipAngleRad = 0.0;
	double SlipRatio = 0.0;
	double TractionControlSlip = 0.0;
	double GripUse = 0.0;
	double AbsRelease = 1.0;
	double PeakSlip = 0.1;
	/** Normal (spring, damper, bump stop and anti-roll bar) load. */
	double LoadN = 0.0;
	double SuspensionLengthM = 0.0;
	/** Road-wheel steer angle, positive right (toe included). */
	double SteerAngleRad = 0.0;
	/** Wheel lean, positive with its top to the car's right (camber and caster). */
	double InclinationRad = 0.0;
	double FrictionCoefficient = 0.0;
	double SurfaceGrip = 1.0;
	double ForwardSpeedMps = 0.0;
	double LateralSpeedMps = 0.0;
	/** Interval-mean tyre forces in the wheel's ground frame. */
	double LongitudinalForceN = 0.0;
	double LateralForceN = 0.0;
	/** Torque about the steering axis from the tyre (pneumatic plus caster trail), + steering the wheel to the right. */
	double AligningTorqueNm = 0.0;
	/** Accumulated rotation for presentation. */
	double SpinAngleRad = 0.0;
	bool bContact = false;
	FVector3d ContactPointM = FVector3d::ZeroVector;
	FVector3d ContactNormal = FVector3d::UnitZ();
};

struct FSimCruiseVehicleSimState
{
	FSimCruiseWheelState Wheels[4];
	double EngineRpm = 850.0;
	double EngineOmegaRadPerSecond = 89.0;
	/** Forward gear index (0 = first). */
	int32 Gear = 0;
	bool bInReverse = false;
	bool bManual = false;
	/** -1 reverse, 0 neutral, 1.. forward (manual selection). */
	int32 SelectedGear = 1;
	double ClutchPedal = 0.0;
	bool bStalled = false;
	/** Launch control: armed (brake and full accelerator at a standstill) and launching. */
	bool bLaunchArmed = false;
	bool bLaunching = false;
	double TcCut = 0.0;
	double TcIntegral = 0.0;
	double TcSlip = 0.0;
	double TcSlipFiltered = 0.0;
	bool bClutchSaturated = false;
	double ShiftTimeS = 0.0;
	/** Steering input fraction (-1..1, + right) and the rack angle it gives. */
	double SteeringInput = 0.0;
	double SteeringRad = 0.0;
	bool bServiceBrake = false;
	double BrakeAmount = 0.0;
	double PedalThrottle = 0.0;
	double ForwardSpeedMps = 0.0;
	double ClutchTorqueNm = 0.0;
	/** Both front tyres' aligning torque about the steering axes (N m, + steering right). */
	double RackAligningTorqueNm = 0.0;
	int32 WheelContactCount = 0;
	bool bRoofDown = false;
	double FlippedSeconds = 0.0;
	double StuckSeconds = 0.0;
	double StrandedSeconds = 0.0;
	double SoundThrottle = 0.0;
	double SoundCombustion = 0.0;
	double SoundShaftLoad = 0.0;
};

/** Mean force over the interval and its torque about the centre of mass (SI, world). */
struct FSimCruiseBodyForces
{
	FVector3d ForceN = FVector3d::ZeroVector;
	FVector3d TorqueNm = FVector3d::ZeroVector;
	FVector3d WheelPointM[4] = { FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector };
	FVector3d WheelForceN[4] = { FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector };
	bool bWheelActive[4] = { false, false, false, false };
};

class SIMCRUISE_API FSimCruiseVehicleSim
{
public:
	FSimCruiseVehicleSim();

	/** Install a car. Resets the state as Powertrain.gd's use_spec does. */
	void Initialize(const FSimCruiseCarDefinition& InCar);
	const FSimCruiseCarDefinition& GetCar() const { return Car; }

	/** Powertrain.gd restart(): wheels stopped, idle, first (or neutral in manual). */
	void ResetState();

	/** Advance one interval and return the forces to apply over it. */
	void Step(double DeltaTimeS, int32 Substeps, const FSimCruiseVehicleControls& Controls,
		const FSimCruiseBodyState& Body, FSimCruiseGroundQuery Ground, FSimCruiseBodyForces& OutForces);

	// Gearbox (Powertrain.gd set_manual, change_gear, start_engine, select_ratio).
	void SetManual(bool bEnabled, double BodySpeedMps);
	/** Manual only: returns "" on success, or why the change was refused. */
	FString ChangeGear(int32 GearStep, double ForwardSpeedMps);
	void StartEngine();
	void SelectRatio(int32 Ratio);
	FString GetGearText() const;
	bool IsElectric() const;

	// Geometry in the body (root component) frame, Unreal axes, metres.
	FVector3d GetMountLocalM(int32 WheelIndex) const { return MountsLocalM[WheelIndex]; }
	FVector3d GetCenterOfMassLocalM() const { return CenterOfMassLocalM; }
	/** Godot's car origin: the ground plane at static ride height. */
	FVector3d GetReferenceOriginLocalM() const { return ReferenceOriginLocalM; }
	/** Principal moments about the centre of mass, Unreal axes (roll, pitch, yaw). */
	FVector3d GetPrincipalInertiaKgM2() const;
	double GetTireRadiusM() const { return TireRadiusM; }
	double GetMaximumSuspensionLengthM() const { return RestLengthM; }

	// Steering map (Powertrain.gd).
	double AvailableSteeringRad(double SpeedMps, bool bAssisted) const;
	double KeyRackRateRadPerSecond(double SpeedMps) const;
	double ThumbstickCommand(double Stick, double SpeedMps) const;
	static double AdvanceSteering(double Current, double Target, double DeltaTimeS, double ApplyRate, double ReturnRate);

	/** Mean driven-wheel speed as the propeller shaft sees it. */
	double DrivenOmega() const;

	/** Everything that evolves; benches and telemetry read it directly. */
	FSimCruiseVehicleSimState State;

	/** Assisted steering (a person on keys or a stick); Direct for benches and real wheels. */
	bool bSteeringAssist = true;

	/** The air the car drives through: density (kg/m3; ISA sea level by default) and the wind
	 *  at the car's height (m/s, world axes). Drag acts on the car's speed through the air. */
	double AirDensityKgPerM3 = 1.225;
	FVector3d WindMps = FVector3d::ZeroVector;

private:
	double MotorTorqueNm(double Throttle, double Ratio, double SignDirection, double SpeedMps);

	FSimCruiseCarDefinition Car;
	FVector3d MountsLocalM[4];
	FVector3d CenterOfMassLocalM = FVector3d::ZeroVector;
	FVector3d ReferenceOriginLocalM = FVector3d::ZeroVector;
	double TireRadiusM = 0.3;
	double WheelInertiaKgM2 = 1.0;
	double RestLengthM = 0.3;
	double AntiRollRateNPerM[2] = { 0.0, 0.0 };
	double ParkingRackRad = 0.0;
	double RoadReferenceRad = 0.0;
	double LimitSlipSurplusRad = 0.0;
	double KeyRackRateParkingRadPerSecond = 0.0;
};
