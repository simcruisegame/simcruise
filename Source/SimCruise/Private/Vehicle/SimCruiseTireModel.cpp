// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicleDynamics.h"

#include "Math/UnrealMathUtility.h"

#include <cmath>

namespace SimCruiseDynamics
{
	double SmoothStep(double Edge0, double Edge1, double Value)
	{
		if (FMath::IsNearlyEqual(Edge0, Edge1))
		{
			return Edge0 <= Edge1 ? (Value <= Edge0 ? 0.0 : 1.0) : (Value <= Edge1 ? 1.0 : 0.0);
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

	double ComputeRelaxedSlipAngleRad(
		const FSimCruiseCarDefinition& Car,
		double ForwardSpeedMps,
		double LateralSpeedMps,
		double DeltaTimeS,
		double NormalLoadN,
		double& InOutRollingSlipAngleRad)
	{
		const double SpeedMps = FMath::Abs(ForwardSpeedMps);
		const double TargetRad = FMath::Atan2(LateralSpeedMps, FMath::Max(1.8, SpeedMps));
		// Below walking speed the regularised contact also handles static
		// friction; blend into the rolling-tyre model as travel builds. A lightly
		// loaded tyre builds side force over a shorter distance.
		const double LoadScale = NormalLoadN > 0.0
			? FMath::Pow(FMath::Clamp(NormalLoadN / FMath::Max(Car.ReferenceCornerLoadN, UE_DOUBLE_SMALL_NUMBER), 0.25, 2.5),
				Car.RelaxationLoadExponent)
			: 1.0;
		const double LengthM = Car.TireRelaxationLengthM * LoadScale * SmoothStep(0.8, 3.5, SpeedMps);
		if (LengthM < 0.0001)
		{
			InOutRollingSlipAngleRad = TargetRad;
			return TargetRad;
		}
		const double Travelled = SpeedMps * DeltaTimeS / LengthM;
		const double Fraction = 1.0 - FMath::Exp(-Travelled);
		const double Change = TargetRad - InOutRollingSlipAngleRad;
		InOutRollingSlipAngleRad += Change * Fraction;
		// Interval mean of d(alpha)/ds = (target - alpha)/length, so the impulse
		// over the interval is not advanced in time.
		return TargetRad - Change * Fraction / Travelled;
	}

	double ComputeFrictionCoefficient(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		double NormalLoadN,
		double SurfaceGripMultiplier)
	{
		const double BaseMu = FMath::Clamp(
			Car.PeakLongitudinalFrictionCoefficient *
				(1.0 - Car.TireLoadSensitivityFraction *
					(NormalLoadN / FMath::Max(Car.ReferenceCornerLoadN, UE_DOUBLE_SMALL_NUMBER) - 1.0)),
			Car.MinimumFrictionCoefficient,
			Car.MaximumFrictionCoefficient);
		const double AxleGrip = Car.AxleGripMultiplier.IsValidIndex(AxleIndex) ? Car.AxleGripMultiplier[AxleIndex] : 1.0;
		return BaseMu * SurfaceGripMultiplier * AxleGrip;
	}

	double ComputeLateralForceN(
		const FSimCruiseCarDefinition& Car,
		double NormalLoadN,
		double FrictionCoefficient,
		double SlipAngleRad,
		double InclinationRad,
		double ForwardSpeedMps)
	{
		const double Falloff = 1.0 - Car.SlipAngleFalloffFraction *
			SmoothStep(Car.SlipAngleFalloffStartRad, Car.SlipAngleFalloffEndRad, FMath::Abs(SlipAngleRad));
		double LateralN = -NormalLoadN * FrictionCoefficient * Car.LateralGripScale *
			std::tanh(SlipAngleRad * Car.SlipAngleStiffnessPerRad) * Falloff;
		LateralN += NormalLoadN * Car.CamberThrustCoefficientPerRad * InclinationRad *
			FMath::Min(1.0, FMath::Abs(ForwardSpeedMps) / 3.0);
		return LateralN;
	}

	FSimCruiseLongitudinalSubstepOutput SolveLongitudinalSubstep(
		const FSimCruiseCarDefinition& Car,
		const FSimCruiseLongitudinalSubstepInput& Input)
	{
		FSimCruiseLongitudinalSubstepOutput Out;
		const double R = Input.TireRadiusM;
		const double I = FMath::Max(Input.WheelInertiaKgM2, UE_DOUBLE_SMALL_NUMBER);
		const double Ds = Input.SubstepS;
		const double V = Input.ForwardSpeedMps;
		const double Fz = Input.NormalLoadN;
		const double Mu = Input.FrictionCoefficient;
		const double LateralScale = FMath::Max(Car.LateralGripScale, UE_DOUBLE_SMALL_NUMBER);
		const double Spin = Input.SpinAfterTorqueRadPerSecond;

		// Slip-ratio curve: the friction circle holds its full size up to the
		// peak slip and shrinks toward the sliding value as the wheel spins or
		// locks, so a spinning tyre also loses side grip (combined slip, via the
		// budget). The slip is the wheel's as it stands at the end of the last
		// substep, where brake and contact impulses balance.
		const double Kappa = (Input.StartAngularSpeedRadPerSecond * R - V) / FMath::Max(FMath::Abs(V), 5.0);
		Out.SlipRatio = Kappa;
		Out.TractionControlSlip = (Input.StartAngularSpeedRadPerSecond * R - V) / FMath::Max(FMath::Abs(V), 1.5);
		// The force cannot fall before it has peaked: a grippy tyre on this
		// linear stiffness reaches its full grip at mu/stiffness.
		const double LongitudinalStiffness = FMath::Max(Car.LongitudinalSlipStiffness, UE_DOUBLE_SMALL_NUMBER);
		Out.PeakSlipRatio = FMath::Max(Car.PeakSlipRatio, Mu / LongitudinalStiffness);
		const double MaxForceN = Fz * Mu * (1.0 - Car.SlipRatioFalloffFraction *
			SmoothStep(Out.PeakSlipRatio, Out.PeakSlipRatio * 3.5, FMath::Abs(Kappa)));

		// Wheel inertia and contact force solved together for stable rolling.
		const double EffectiveStiffness = (Fz * LongitudinalStiffness) / FMath::Max(1.5, FMath::Abs(V));
		double Fx = EffectiveStiffness * (Spin * R - V) / (1.0 + EffectiveStiffness * R * R * Ds / I);
		double Fy = Input.LateralForceN;
		const double Budget = FMath::Sqrt(Fx * Fx + (Fy / LateralScale) * (Fy / LateralScale));
		if (Budget > MaxForceN && Budget > 0.01)
		{
			Fx *= MaxForceN / Budget;
			Fy *= MaxForceN / Budget;
		}
		Out.GripUseFraction = Fx / FMath::Max(Fz * Mu, 1.0);

		// As a braked tyre stops rotating the tread slides with the contact
		// patch velocity; blend in over the last 20 % before lock.
		if (Input.BrakeTorqueNm > 0.0)
		{
			const double RollingFraction = FMath::Abs(Spin * R) / FMath::Max(0.10, FMath::Abs(V));
			const double LockWeight = (1.0 - SmoothStep(0.0, 0.20, RollingFraction)) *
				SmoothStep(1.5, 3.0, FMath::Sqrt(V * V + Input.LateralSpeedMps * Input.LateralSpeedMps));
			if (LockWeight > 0.0)
			{
				const double SlideX = Spin * R - V;
				const double SlideY = -Input.LateralSpeedMps;
				const double Ellipse = FMath::Sqrt(SlideX * SlideX + (SlideY / LateralScale) * (SlideY / LateralScale));
				const double Scale = MaxForceN / FMath::Max(Ellipse, 0.001);
				Fx = FMath::Lerp(Fx, SlideX * Scale, LockWeight);
				Fy = FMath::Lerp(Fy, SlideY * Scale, LockWeight);
			}
		}

		double EndSpin = Spin - Fx * R * Ds / I;
		if (Input.BrakeTorqueNm > 0.0 && FMath::Abs(Spin) < 1.0e-5 && FMath::Abs(Fx * R) <= Input.BrakeTorqueNm)
		{
			EndSpin = 0.0;
		}
		Out.LongitudinalForceN = Fx;
		Out.LateralForceN = Fy;
		Out.EndAngularSpeedRadPerSecond = EndSpin;
		return Out;
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
		const double WheelInertiaKgM2 = FMath::Max(
			Input.WheelInertiaKgM2 > 0.0 ? Input.WheelInertiaKgM2 : Car.WheelRotationalInertiaKgM2,
			UE_DOUBLE_SMALL_NUMBER);
		const double Spin = MoveToward(
			Input.WheelAngularSpeedRadPerSecond + Input.DriveTorqueNm * DeltaTimeS / WheelInertiaKgM2,
			0.0,
			FMath::Max(Input.BrakeTorqueNm, 0.0) * DeltaTimeS / WheelInertiaKgM2);
		Result.EndWheelAngularSpeedRadPerSecond = Spin;
		if (Input.NormalLoadN <= 0.0 || DeltaTimeS <= 0.0 || TireRadiusM <= 0.0)
		{
			InOutRollingSlipAngleRad = 0.0;
			return Result;
		}

		const double SlipAngle = ComputeRelaxedSlipAngleRad(
			Car, Input.ForwardSpeedMps, Input.LateralSpeedMps, DeltaTimeS, Input.NormalLoadN, InOutRollingSlipAngleRad);
		Result.RelaxedSlipAngleRad = SlipAngle;
		const double Mu = ComputeFrictionCoefficient(Car, AxleIndex, Input.NormalLoadN, FMath::Max(Input.SurfaceGripMultiplier, 0.0));
		const double Lateral = ComputeLateralForceN(
			Car, Input.NormalLoadN, Mu, SlipAngle, -(Input.CamberAngleRad + Input.RoadLeanAngleRad), Input.ForwardSpeedMps);

		FSimCruiseLongitudinalSubstepInput Sub;
		Sub.NormalLoadN = Input.NormalLoadN;
		Sub.FrictionCoefficient = Mu;
		Sub.ForwardSpeedMps = Input.ForwardSpeedMps;
		Sub.LateralSpeedMps = Input.LateralSpeedMps;
		Sub.LateralForceN = Lateral;
		Sub.StartAngularSpeedRadPerSecond = Input.WheelAngularSpeedRadPerSecond;
		Sub.SpinAfterTorqueRadPerSecond = Spin;
		Sub.BrakeTorqueNm = FMath::Max(Input.BrakeTorqueNm, 0.0);
		Sub.TireRadiusM = TireRadiusM;
		Sub.WheelInertiaKgM2 = WheelInertiaKgM2;
		Sub.SubstepS = DeltaTimeS;
		const FSimCruiseLongitudinalSubstepOutput Out = SolveLongitudinalSubstep(Car, Sub);

		Result.LongitudinalForceN = Out.LongitudinalForceN;
		Result.LateralForceN = Out.LateralForceN;
		Result.EndWheelAngularSpeedRadPerSecond = Out.EndAngularSpeedRadPerSecond;
		Result.LongitudinalSlipRatio = Out.SlipRatio;
		Result.PeakSlipRatio = Out.PeakSlipRatio;
		Result.GripUseFraction = Out.GripUseFraction;
		return Result;
	}
}
