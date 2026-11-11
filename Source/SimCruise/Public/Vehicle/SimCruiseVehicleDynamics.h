// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CarDefinition.h"

/** Per-contact inputs to the SI-unit tire model. Wheel torques are applied
 *  before tire forces, matching the existing Godot simulation's integration. */
struct FSimCruiseTireContactInput
{
	double NormalLoadN = 0.0;
	double ForwardSpeedMps = 0.0;
	double LateralSpeedMps = 0.0;
	double WheelAngularSpeedRadPerSecond = 0.0;
	double DriveTorqueNm = 0.0;
	double BrakeTorqueNm = 0.0;
	double TireRadiusM = 0.0;
	double WheelInertiaKgM2 = 0.0;
	double SurfaceGripMultiplier = 1.0;
	/** Godot's sign: positive leans the wheel top to the car's left. */
	double CamberAngleRad = 0.0;
	double RoadLeanAngleRad = 0.0;
	double DeltaTimeS = 0.0;
};

struct FSimCruiseTireContactOutput
{
	double LongitudinalForceN = 0.0;
	double LateralForceN = 0.0;
	double EndWheelAngularSpeedRadPerSecond = 0.0;
	double LongitudinalSlipRatio = 0.0;
	double PeakSlipRatio = 0.0;
	double RelaxedSlipAngleRad = 0.0;
	double GripUseFraction = 0.0;
};

/** One wheel-inertia substep of the longitudinal tyre law (Powertrain.gd's
 *  inner loop), after drive and brake torque have been applied to the spin. */
struct FSimCruiseLongitudinalSubstepInput
{
	double NormalLoadN = 0.0;
	/** Load-sensitive friction coefficient of this contact (surface and axle grip included). */
	double FrictionCoefficient = 0.0;
	double ForwardSpeedMps = 0.0;
	double LateralSpeedMps = 0.0;
	/** Relaxed lateral force from this tick's slip angle (N, + to the wheel's right). */
	double LateralForceN = 0.0;
	/** Wheel speed at the end of the previous substep (sets the slip ratio). */
	double StartAngularSpeedRadPerSecond = 0.0;
	/** Wheel speed after this substep's drive and brake torque. */
	double SpinAfterTorqueRadPerSecond = 0.0;
	double BrakeTorqueNm = 0.0;
	double TireRadiusM = 0.0;
	double WheelInertiaKgM2 = 0.0;
	double SubstepS = 0.0;
};

struct FSimCruiseLongitudinalSubstepOutput
{
	double LongitudinalForceN = 0.0;
	double LateralForceN = 0.0;
	double EndAngularSpeedRadPerSecond = 0.0;
	/** Slip ratio over max(|v|, 5 m/s), as the slip-ratio curve reads it. */
	double SlipRatio = 0.0;
	/** Slip ratio over max(|v|, 1.5 m/s), as traction control reads it. */
	double TractionControlSlip = 0.0;
	double PeakSlipRatio = 0.0;
	double GripUseFraction = 0.0;
};

/** Deterministic vehicle model helpers. All inputs and outputs use SI units;
 *  conversion to Unreal's centimeter-based force boundary belongs in the
 *  Chaos adapter that consumes these functions (SimCruiseUnits.h). */
namespace SimCruiseDynamics
{
	inline constexpr double GravityMps2 = 9.80665;

	/** Godot's smoothstep (equal edges step at the edge). */
	double SmoothStep(double Edge0, double Edge1, double Value);
	/** Godot's move_toward. */
	double MoveToward(double Current, double Target, double MaxDelta);

	double GetWheelSpringRateNPerM(const FSimCruiseCarDefinition& Car, int32 AxleIndex);
	double GetAntiRollBarRateNPerM(const FSimCruiseCarDefinition& Car, int32 AxleIndex);
	double ComputeSuspensionLoadN(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		double SuspensionTravelM,
		double SuspensionVelocityMps);

	/** Powertrain.gd contact_slip: the slip angle this interval sees through the
	 *  rolling relaxation length, advancing the rolling slip state. */
	double ComputeRelaxedSlipAngleRad(
		const FSimCruiseCarDefinition& Car,
		double ForwardSpeedMps,
		double LateralSpeedMps,
		double DeltaTimeS,
		double NormalLoadN,
		double& InOutRollingSlipAngleRad);

	/** Load-sensitive peak friction times surface and axle grip. */
	double ComputeFrictionCoefficient(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		double NormalLoadN,
		double SurfaceGripMultiplier);

	/** Saturating slip-angle curve plus camber thrust. InclinationRad is the
	 *  wheel's lean relative to the road, positive with the top toward the
	 *  wheel's right; the force is positive toward the wheel's right. */
	double ComputeLateralForceN(
		const FSimCruiseCarDefinition& Car,
		double NormalLoadN,
		double FrictionCoefficient,
		double SlipAngleRad,
		double InclinationRad,
		double ForwardSpeedMps);

	FSimCruiseLongitudinalSubstepOutput SolveLongitudinalSubstep(
		const FSimCruiseCarDefinition& Car,
		const FSimCruiseLongitudinalSubstepInput& Input);

	/** One complete contact evaluation (relaxation + one longitudinal substep). */
	FSimCruiseTireContactOutput SolveTireContact(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		const FSimCruiseTireContactInput& Input,
		double& InOutRollingSlipAngleRad);

	/** Full-load crank torque from the car's curve (N·m). */
	double SampleEngineTorqueNm(const FSimCruiseCarDefinition& Car, double EngineSpeedRpm);

	void SplitDriveTorqueToWheels(
		const FSimCruiseCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque,
		double OutWheelTorquesNm[4]);

	TArray<double> SplitDriveTorqueToWheels(
		const FSimCruiseCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque);
}
