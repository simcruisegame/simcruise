// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicleDynamics.h"

#include "Math/UnrealMathUtility.h"

#include <cmath>

namespace SimCruiseDynamics
{
	namespace
	{
		double SmoothStep(double Edge0, double Edge1, double Value)
		{
			if (Edge1 <= Edge0)
			{
				return Value >= Edge1 ? 1.0 : 0.0;
			}

			const double Alpha = FMath::Clamp((Value - Edge0) / (Edge1 - Edge0), 0.0, 1.0);
			return Alpha * Alpha * (3.0 - 2.0 * Alpha);
		}

		double MoveToward(double Current, double Target, double MaxDelta)
		{
			if (FMath::Abs(Target - Current) <= MaxDelta)
			{
				return Target;
			}
			return Current + FMath::Sign(Target - Current) * MaxDelta;
		}
	}

	FSimCruiseTireContactOutput SolveTireContact(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		const FSimCruiseTireContactInput& Input,
		double& InOutRollingSlipAngleRad)
	{
		FSimCruiseTireContactOutput Result;
		const double DeltaTimeS = FMath::Max(Input.DeltaTimeS, 0.0);
		const double TireRadiusM = Input.TireRadiusM > 0.0 ? Input.TireRadiusM : Car.TireFreeRadiusM();
		const double WheelInertiaKgM2 = Input.WheelInertiaKgM2 > 0.0
			? Input.WheelInertiaKgM2
			: Car.WheelRotationalInertiaKgM2;
		const double SafeInertia = FMath::Max(WheelInertiaKgM2, UE_DOUBLE_SMALL_NUMBER);
		const double SpeedMps = FMath::Abs(Input.ForwardSpeedMps);
		const double LateralScale = FMath::Max(Car.LateralGripScale, UE_DOUBLE_SMALL_NUMBER);
		const double SpinAfterDriveAndBrake = MoveToward(
			Input.WheelAngularSpeedRadPerSecond + Input.DriveTorqueNm * DeltaTimeS / SafeInertia,
			0.0,
			FMath::Max(Input.BrakeTorqueNm, 0.0) * DeltaTimeS / SafeInertia);

		Result.EndWheelAngularSpeedRadPerSecond = SpinAfterDriveAndBrake;
		if (Input.NormalLoadN <= 0.0 || DeltaTimeS <= 0.0 || TireRadiusM <= 0.0)
		{
			InOutRollingSlipAngleRad = 0.0;
			return Result;
		}

		const double TargetSlipAngleRad = FMath::Atan2(Input.LateralSpeedMps, FMath::Max(1.8, SpeedMps));
		const double LoadScale = FMath::Pow(
			FMath::Clamp(Input.NormalLoadN / FMath::Max(Car.ReferenceCornerLoadN, UE_DOUBLE_SMALL_NUMBER), 0.25, 2.5),
			Car.RelaxationLoadExponent);
		const double RelaxationLengthM = Car.TireRelaxationLengthM * LoadScale * SmoothStep(0.8, 3.5, SpeedMps);
		double SlipAngleForThisStep = TargetSlipAngleRad;
		if (RelaxationLengthM >= 0.0001)
		{
			const double TravelledFraction = SpeedMps * DeltaTimeS / RelaxationLengthM;
			const double Fraction = 1.0 - FMath::Exp(-TravelledFraction);
			const double SlipChange = TargetSlipAngleRad - InOutRollingSlipAngleRad;
			InOutRollingSlipAngleRad += SlipChange * Fraction;
			SlipAngleForThisStep = TargetSlipAngleRad - SlipChange * Fraction / TravelledFraction;
		}
		else
		{
			InOutRollingSlipAngleRad = TargetSlipAngleRad;
		}
		Result.RelaxedSlipAngleRad = SlipAngleForThisStep;

		const double BaseMu = FMath::Clamp(
			Car.PeakLongitudinalFrictionCoefficient *
				(1.0 - Car.TireLoadSensitivityFraction *
					(Input.NormalLoadN / FMath::Max(Car.ReferenceCornerLoadN, UE_DOUBLE_SMALL_NUMBER) - 1.0)),
			Car.MinimumFrictionCoefficient,
			Car.MaximumFrictionCoefficient);
		const double AxleGrip = Car.AxleGripMultiplier.IsValidIndex(AxleIndex)
			? Car.AxleGripMultiplier[AxleIndex]
			: 1.0;
		const double Mu = BaseMu * FMath::Max(Input.SurfaceGripMultiplier, 0.0) * FMath::Max(AxleGrip, 0.0);
		const double SlipAngleMagnitude = FMath::Abs(SlipAngleForThisStep);
		const double SlipAngleFalloff = 1.0 - Car.SlipAngleFalloffFraction * SmoothStep(
			Car.SlipAngleFalloffStartRad,
			Car.SlipAngleFalloffEndRad,
			SlipAngleMagnitude);
		const double CamberThrust = Input.NormalLoadN * Car.CamberThrustCoefficientPerRad *
			(Input.CamberAngleRad + Input.RoadLeanAngleRad) * FMath::Min(1.0, SpeedMps / 3.0);
		const double LateralForceN = -Input.NormalLoadN * Mu * LateralScale *
			std::tanh(SlipAngleForThisStep * Car.SlipAngleStiffnessPerRad) * SlipAngleFalloff - CamberThrust;

		const double LongitudinalSpeedDenominator = FMath::Max(FMath::Abs(Input.ForwardSpeedMps), 5.0);
		const double SlipRatio = (SpinAfterDriveAndBrake * TireRadiusM - Input.ForwardSpeedMps) /
			LongitudinalSpeedDenominator;
		const double LongitudinalStiffness = FMath::Max(Car.LongitudinalSlipStiffness, UE_DOUBLE_SMALL_NUMBER);
		const double PeakSlipRatio = FMath::Max(Car.PeakSlipRatio, Mu / LongitudinalStiffness);
		const double LongitudinalFalloff = 1.0 - Car.SlipRatioFalloffFraction *
			SmoothStep(PeakSlipRatio, PeakSlipRatio * 3.5, FMath::Abs(SlipRatio));
		const double MaximumCombinedForceN = Input.NormalLoadN * Mu * FMath::Max(LongitudinalFalloff, 0.0);
		const double EffectiveLongitudinalStiffness = Input.NormalLoadN * LongitudinalStiffness /
			FMath::Max(1.5, FMath::Abs(Input.ForwardSpeedMps));
		const double LongitudinalForceN = EffectiveLongitudinalStiffness *
			(SpinAfterDriveAndBrake * TireRadiusM - Input.ForwardSpeedMps) /
			(1.0 + EffectiveLongitudinalStiffness * TireRadiusM * TireRadiusM * DeltaTimeS / SafeInertia);
		const double GripBudget = FMath::Sqrt(
			LongitudinalForceN * LongitudinalForceN +
			(LateralForceN / LateralScale) * (LateralForceN / LateralScale));
		double FxN = LongitudinalForceN;
		double FyN = LateralForceN;
		if (GripBudget > MaximumCombinedForceN && GripBudget > UE_DOUBLE_SMALL_NUMBER)
		{
			const double Scale = MaximumCombinedForceN / GripBudget;
			FxN *= Scale;
			FyN *= Scale;
		}

		if (Input.BrakeTorqueNm > 0.0)
		{
			const double RollingFraction = FMath::Abs(SpinAfterDriveAndBrake * TireRadiusM) /
				FMath::Max(0.10, FMath::Abs(Input.ForwardSpeedMps));
			const double LockWeight = (1.0 - SmoothStep(0.0, 0.20, RollingFraction)) *
				SmoothStep(1.5, 3.0, FMath::Sqrt(
					Input.ForwardSpeedMps * Input.ForwardSpeedMps +
					Input.LateralSpeedMps * Input.LateralSpeedMps));
			if (LockWeight > 0.0)
			{
				const double SlideX = SpinAfterDriveAndBrake * TireRadiusM - Input.ForwardSpeedMps;
				const double SlideY = -Input.LateralSpeedMps;
				const double SlideBudget = FMath::Sqrt(SlideX * SlideX + (SlideY / LateralScale) * (SlideY / LateralScale));
				if (SlideBudget > UE_DOUBLE_SMALL_NUMBER)
				{
					const double SlideScale = MaximumCombinedForceN / SlideBudget;
					FxN = FMath::Lerp(FxN, SlideX * SlideScale, LockWeight);
					FyN = FMath::Lerp(FyN, SlideY * SlideScale, LockWeight);
				}
			}
		}

		double EndSpin = SpinAfterDriveAndBrake - FxN * TireRadiusM * DeltaTimeS / SafeInertia;
		if (Input.BrakeTorqueNm > 0.0 && FMath::IsNearlyZero(SpinAfterDriveAndBrake, 1.0e-6) &&
			FMath::Abs(FxN * TireRadiusM) <= Input.BrakeTorqueNm)
		{
			EndSpin = 0.0;
		}

		Result.LongitudinalForceN = FxN;
		Result.LateralForceN = FyN;
		Result.EndWheelAngularSpeedRadPerSecond = EndSpin;
		Result.LongitudinalSlipRatio = SlipRatio;
		Result.PeakSlipRatio = PeakSlipRatio;
		Result.GripUseFraction = FxN / FMath::Max(Input.NormalLoadN * Mu, 1.0);
		return Result;
	}
}
