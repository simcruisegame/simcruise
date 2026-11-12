// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Vehicle/CarDefinition.h"

/** Results of godot-final:game/checks/CarTrack.gd's three tests. */
struct FSimCruiseCarTrackResult
{
	double ZeroToSixtyS = TNumericLimits<double>::Max();
	/** 0-60 mph timed as US magazines do: the clock starts after the first foot of travel (a drag strip's rollout). */
	double ZeroToSixtyRolloutS = TNumericLimits<double>::Max();
	double SeventyToZeroFt = 0.0;
	double SkidpadG = 0.0;
	bool bCompleted = false;
	bool bTimedOut = false;
	/** The car stopped harder than tyres can (over 5 g): it hit something on the pad. */
	bool bCollided = false;
	double SimSeconds = 0.0;
	int64 PhysicsSteps = 0;
	double MeanStepMicroseconds = 0.0;
	double WallSeconds = 0.0;
	/** Final body and wheel state, for determinism checks. */
	TArray<double> Fingerprint;
};

/**
 * Run CarTrack (0-60 mph from rest at full throttle in automatic, 70-0 mph
 * with the full service brake, ramp-steer skidpad at the 300 ft circle's
 * target speed) at the given fixed physics step.
 * MaxWheelSubstepS splits each physics step for the wheel/driveline integration.
 */
FSimCruiseCarTrackResult SimCruiseRunCarTrack(const FSimCruiseCarDefinition& Car, double FixedStepS,
	double MaxWheelSubstepS = 1.0 / 360.0, bool bAccelerationOnly = false);

/** One case of godot-final:game/checks/LiftOff.gd. */
struct FSimCruiseLiftOffCase
{
	double TargetG = 0.3;
	bool bBrake = false;
	bool bSteady = false;
	double SteadyLateralG = 0.0;
	int32 SteadyGear = 0;
	double SteadyBetaDeg = 0.0;
	/** Mean front road-wheel angle holding the steady circle (deg). */
	double SteadyRackDeg = 0.0;
	double YawRatio1s = 0.0;
	double YawOvershoot = 0.0;
	double Beta1sDeg = 0.0;
	double PeakBetaDeg = 0.0;
	double SettleS = 0.0;
	double DecelG = 0.0;
	bool bSpun = false;
	bool bReverseAtSpeed = false;
};

struct FSimCruiseLiftOffResult
{
	TArray<FSimCruiseLiftOffCase> Cases;
	bool bCompleted = false;
};

/** Run LiftOff on a RadiusM circle at each lateral level, lift and trail brake. */
FSimCruiseLiftOffResult SimCruiseRunLiftOff(const FSimCruiseCarDefinition& Car, double FixedStepS,
	const TArray<double>& LevelsG, double RadiusM = 40.0);

/** Static load balance after settling on the pad. */
struct FSimCruiseStaticLoadResult
{
	bool bCompleted = false;
	double LoadN[4] = { 0.0, 0.0, 0.0, 0.0 };
	double SuspensionLengthM[4] = { 0.0, 0.0, 0.0, 0.0 };
	double TotalLoadN = 0.0;
	double FrontFraction = 0.0;
	double PitchDeg = 0.0;
	double RollDeg = 0.0;
	double ResidualSpeedMps = 0.0;
	double GravityMps2 = 0.0;
};

FSimCruiseStaticLoadResult SimCruiseRunStaticLoad(const FSimCruiseCarDefinition& Car, double FixedStepS, double SettleS = 4.0);

/** The physics rate SimCruise ships with (matches Config/DefaultEngine.ini). */
inline constexpr double SimCruiseShippingFixedStepS = 1.0 / 360.0;

#endif
