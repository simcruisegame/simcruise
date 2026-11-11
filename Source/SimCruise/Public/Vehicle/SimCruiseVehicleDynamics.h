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

/** Deterministic vehicle model helpers. All inputs and outputs use SI units;
 *  conversion to Unreal's centimeter-based force boundary belongs in the
 *  Chaos adapter that consumes these functions. */
namespace SimCruiseDynamics
{
	double GetWheelSpringRateNPerM(const FSimCruiseCarDefinition& Car, int32 AxleIndex);
	double GetAntiRollBarRateNPerM(const FSimCruiseCarDefinition& Car, int32 AxleIndex);
	double ComputeSuspensionLoadN(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		double SuspensionTravelM,
		double SuspensionVelocityMps);

	FSimCruiseTireContactOutput SolveTireContact(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		const FSimCruiseTireContactInput& Input,
		double& InOutRollingSlipAngleRad);

	TArray<double> SplitDriveTorqueToWheels(
		const FSimCruiseCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque);
}
