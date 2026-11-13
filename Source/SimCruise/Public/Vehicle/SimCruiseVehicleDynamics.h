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
	/** TMeasy only: lateral slip tan(relaxed slip angle), + contact sliding to the wheel's right,
	 *  and surface times axle grip. LateralForceN then carries only the camber thrust. */
	double LateralSlip = 0.0;
	double GripScale = 1.0;
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
	/**
	 * Engine load (the share of full-throttle torque, 0..1) for an accelerator pedal at an engine
	 * speed. The throttle plate's flow saturates against what the engine draws: at low speed a
	 * small opening already fills the cylinders, at high speed the response is nearly linear.
	 * Load = (1 - e^(-k p)) / (1 - e^(-k)), k = ThrottleProgression x 3000 / rpm (0 = linear).
	 */
	double EngineLoad(const FSimCruiseCarDefinition& Car, double Pedal, double EngineRpm);
	/** The rear brakes' share of the pedal (0..1) after the proportioning valve. */
	double RearBrakePedal(const FSimCruiseCarDefinition& Car, double Pedal);
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

	/** One TMeasy direction at a load: initial stiffness (N per unit slip), peak force and
	 *  slip, full-slide force and slip. */
	struct FTmeasyCurve
	{
		double Stiffness = 0.0;
		double PeakForce = 0.0;
		double PeakSlip = 0.1;
		double SlideForce = 0.0;
		double SlideSlip = 0.5;
	};

	bool UsesTmeasy(const FSimCruiseCarDefinition& Car);

	/** The car's TMeasy curves at this load (interpolated between the nominal load and
	 *  twice it as Chrono's ChTMeasyTire does: forces quadratically, slips linearly), the
	 *  forces and slips scaled by GripScale. */
	void GetTmeasyCurves(const FSimCruiseCarDefinition& Car, double NormalLoadN, double GripScale,
		FTmeasyCurve& OutX, FTmeasyCurve& OutY);

	/** Contact patch length (m) of a tyre under this load: 2 sqrt(R d), d = load / vertical stiffness. */
	double ContactPatchLengthM(const FSimCruiseCarDefinition& Car, double NormalLoadN);

	/** Pneumatic trail (m, + behind the contact centre) at this load and lateral slip tan(alpha):
	 *  TMeasy's lever over the patch length, falling through zero past the peak (Chrono AlignmentTorque). */
	double TmeasyPneumaticTrailM(const FSimCruiseCarDefinition& Car, double NormalLoadN, double LateralSlip);

	/** TMeasy's force over combined slip s along one curve (Rill; Chrono tmxy_combined). */
	double TmeasyForce(double Slip, const FTmeasyCurve& Curve);

	/** Combined TMeasy force F along the slip direction (SlipX, SlipY), and the secant F/s. */
	double TmeasyCombined(const FSimCruiseCarDefinition& Car, double NormalLoadN, double GripScale,
		double SlipX, double SlipY, double& OutSecantPerSlip, double& OutPeakSlipX);

	FSimCruiseLongitudinalSubstepOutput SolveLongitudinalSubstep(
		const FSimCruiseCarDefinition& Car,
		const FSimCruiseLongitudinalSubstepInput& Input);

	/** One complete contact evaluation (relaxation + one longitudinal substep). */
	FSimCruiseTireContactOutput SolveTireContact(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		const FSimCruiseTireContactInput& Input,
		double& InOutRollingSlipAngleRad);

	/** Engine speeds at which the automatic changes gear for one pedal position. */
	struct FShiftPoints
	{
		double UpshiftRpm = 0.0;
		double DownshiftRpm = 0.0;
	};

	/**
	 * The automatic's shift map, as a real transmission controller's: a light
	 * pedal changes up early (about 2400 rpm in the roadster) and lets the
	 * engine lug down before changing down; a deeper pedal holds each gear
	 * longer; the floor runs to upshift_rpm and kicks down a gear whenever the
	 * lower gear fits under it. PedalFraction is 0..1.
	 */
	FShiftPoints GetAutomaticShiftPoints(const FSimCruiseCarDefinition& Car, double PedalFraction);

	/**
	 * The gear the automatic wants next (0-based). WheelEngineOmega is the
	 * engine speed the driven wheels imply in the current gear, so a slipping
	 * launch or a locked wheel never triggers a change; no upshift while braking.
	 * The downshift must land below 92 % of the upshift point, so it never hunts.
	 * LateralG holds the gear through a corner, as real controllers do (a shift's
	 * torque step mid-corner unsettles the car): above 0.3 g it changes only to
	 * save the engine from the fuel cut or from lugging below the light-pedal point.
	 */
	int32 SelectAutomaticGear(const FSimCruiseCarDefinition& Car, int32 Gear, double EngineRpm,
		double WheelEngineOmegaRadPerSecond, double PedalFraction, bool bBraking, double LateralG = 0.0);

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
