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

	bool UsesTmeasy(const FSimCruiseCarDefinition& Car)
	{
		return Car.TyreModel == TEXT("tmeasy");
	}

	void GetTmeasyCurves(const FSimCruiseCarDefinition& Car, double NormalLoadN, double GripScale,
		FTmeasyCurve& OutX, FTmeasyCurve& OutY)
	{
		const double Pn = FMath::Max(Car.ReferenceCornerLoadN, 1.0);
		// Chrono clamps the load it interpolates with; past about 2.5 pn the quadratic turns over.
		const double Q = FMath::Clamp(NormalLoadN / Pn, 0.0, 2.5);
		// InterpQ: through 0 at no load, w1 at pn and w2 at 2 pn (absolute forces).
		auto InterpQ = [Q](double W1, double W2) { return Q * (2.0 * W1 - 0.5 * W2 - (W1 - 0.5 * W2) * Q); };
		auto InterpL = [Q](double W1, double W2) { return W1 + (W2 - W1) * (Q - 1.0); };
		auto Force = [&](double PerLoad, double Ratio2) { return InterpQ(PerLoad * Pn, PerLoad * Ratio2 * 2.0 * Pn); };
		const double G = FMath::Max(GripScale, 0.0);
		OutX.Stiffness = Force(Car.TmeasyStiffnessX, Car.TmeasyStiffnessX2Ratio);
		OutX.PeakForce = Force(Car.TmeasyPeakMuX, Car.TmeasyPeakMuX2Ratio) * G;
		OutX.PeakSlip = Car.TmeasyPeakSlipX * G;
		OutX.SlideForce = Force(Car.TmeasySlideMuX, Car.TmeasySlideMuX2Ratio) * G;
		OutX.SlideSlip = Car.TmeasySlideSlipX * G;
		OutY.Stiffness = Force(Car.TmeasyStiffnessY, Car.TmeasyStiffnessY2Ratio);
		OutY.PeakForce = Force(Car.TmeasyPeakMuY, Car.TmeasyPeakMuY2Ratio) * G;
		OutY.PeakSlip = FMath::Max(InterpL(Car.TmeasyPeakSlipY, Car.TmeasyPeakSlipY * Car.TmeasyPeakSlipY2Ratio), 0.02) * G;
		OutY.SlideForce = Force(Car.TmeasySlideMuY, Car.TmeasySlideMuY2Ratio) * G;
		OutY.SlideSlip = FMath::Max(InterpL(Car.TmeasySlideSlipY, Car.TmeasySlideSlipY * Car.TmeasySlideSlipY2Ratio), 0.1) * G;
	}

	double TmeasyForce(double S, const FTmeasyCurve& C)
	{
		S = FMath::Abs(S);
		const double Fm = C.PeakForce, Sm = FMath::Max(C.PeakSlip, 1.0e-4), Fs = C.SlideForce, Ss = FMath::Max(C.SlideSlip, Sm + 1.0e-4);
		if (Fm <= 0.0)
		{
			return 0.0;
		}
		// The curve must reach its peak: the initial slope is at least the secant to it, twice over.
		const double Df0 = FMath::Max(2.0 * Fm / Sm, C.Stiffness);
		if (S >= Ss)
		{
			return Fs;
		}
		if (S < Sm)
		{
			// Adhesion: a rational curve from the initial slope to a zero slope at the peak.
			const double P = Df0 * Sm / Fm - 2.0;
			const double Sn = S / Sm;
			return Df0 * Sm * Sn / (1.0 + (Sn + P) * Sn);
		}
		// Peak to full slide: two parabolas meeting at S*, as Chrono's tmxy_combined.
		const double A = FMath::Square(Fm / Sm) / (Df0 * Sm);
		const double SStar = Sm + (Fm - Fs) / (A * (Ss - Sm));
		if (SStar <= Ss)
		{
			if (S <= SStar)
			{
				return Fm - A * FMath::Square(S - Sm);
			}
			const double B = A * (SStar - Sm) / (Ss - SStar);
			return Fs + B * FMath::Square(Ss - S);
		}
		const double Sn = (S - Sm) / (Ss - Sm);
		return Fm - (Fm - Fs) * Sn * Sn * (3.0 - 2.0 * Sn);
	}

	double ContactPatchLengthM(const FSimCruiseCarDefinition& Car, double NormalLoadN)
	{
		const double Deflection = FMath::Max(NormalLoadN, 0.0) / FMath::Max(Car.TireVerticalStiffnessNPerM, 1.0);
		return 2.0 * FMath::Sqrt(Car.TireFreeRadiusM() * Deflection);
	}

	double TmeasyPneumaticTrailM(const FSimCruiseCarDefinition& Car, double NormalLoadN, double LateralSlip)
	{
		const double Q = FMath::Clamp(NormalLoadN / FMath::Max(Car.ReferenceCornerLoadN, 1.0), 0.0, 2.5);
		auto InterpL = [Q](double W1, double W2) { return W1 + (W2 - W1) * (Q - 1.0); };
		const double N0 = InterpL(Car.TmeasyTrailFraction, Car.TmeasyTrailFraction * Car.TmeasyTrailFraction2Ratio);
		const double S0 = FMath::Max(InterpL(Car.TmeasyTrailZeroSlip, Car.TmeasyTrailZeroSlip * Car.TmeasyTrailZeroSlip2Ratio), 0.01);
		const double SE = FMath::Max(InterpL(Car.TmeasyTrailEndSlip, Car.TmeasyTrailEndSlip * Car.TmeasyTrailEndSlip2Ratio), S0);
		const double W = S0 / SE;
		const double S = FMath::Abs(LateralSlip);
		double Lever = 0.0;
		if (S >= SE)
		{
			Lever = 0.0;
		}
		else if (S <= S0)
		{
			// Linear to zero, blended with an S-shaped fall (Chrono's nto1 and nto2).
			const double Sn = S / S0;
			Lever = (1.0 - W) * N0 * (1.0 - Sn) + W * N0 * (1.0 - (3.0 - 2.0 * Sn) * Sn * Sn);
		}
		else
		{
			// Past the zero the trail goes a little negative and returns to zero at full slide.
			const double Sn = (SE - S) / (SE - S0);
			Lever = -N0 * (1.0 - W) * (S - S0) / S0 * Sn * Sn;
		}
		return Lever * ContactPatchLengthM(Car, NormalLoadN);
	}

	double TmeasyCombined(const FSimCruiseCarDefinition& Car, double NormalLoadN, double GripScale,
		double SlipX, double SlipY, double& OutSecantPerSlip, double& OutPeakSlipX)
	{
		FTmeasyCurve X, Y;
		GetTmeasyCurves(Car, NormalLoadN, GripScale, X, Y);
		OutPeakSlipX = X.PeakSlip;
		const double Sc = FMath::Sqrt(SlipX * SlipX + SlipY * SlipY);
		if (Sc < 1.0e-6)
		{
			OutSecantPerSlip = FMath::Max(X.Stiffness, Y.Stiffness);
			return 0.0;
		}
		// Each parameter blended along the slip direction (Chrono ChTMeasyTire::Advance).
		const double Cx = SlipX / Sc, Cy = SlipY / Sc;
		FTmeasyCurve C;
		C.Stiffness = FMath::Sqrt(FMath::Square(X.Stiffness * Cx) + FMath::Square(Y.Stiffness * Cy));
		C.PeakForce = FMath::Sqrt(FMath::Square(X.PeakForce * Cx) + FMath::Square(Y.PeakForce * Cy));
		C.PeakSlip = FMath::Sqrt(FMath::Square(X.PeakSlip * Cx) + FMath::Square(Y.PeakSlip * Cy));
		C.SlideForce = FMath::Sqrt(FMath::Square(X.SlideForce * Cx) + FMath::Square(Y.SlideForce * Cy));
		C.SlideSlip = FMath::Sqrt(FMath::Square(X.SlideSlip * Cx) + FMath::Square(Y.SlideSlip * Cy));
		const double F = TmeasyForce(Sc, C);
		OutSecantPerSlip = F / Sc;
		return F;
	}

	static FSimCruiseLongitudinalSubstepOutput SolveTmeasySubstep(
		const FSimCruiseCarDefinition& Car,
		const FSimCruiseLongitudinalSubstepInput& Input)
	{
		FSimCruiseLongitudinalSubstepOutput Out;
		const double R = Input.TireRadiusM;
		const double I = FMath::Max(Input.WheelInertiaKgM2, UE_DOUBLE_SMALL_NUMBER);
		const double Ds = Input.SubstepS;
		const double V = Input.ForwardSpeedMps;
		const double Fz = Input.NormalLoadN;
		const double Spin = Input.SpinAfterTorqueRadPerSecond;
		// ABS and traction control keep their own slip measures.
		Out.SlipRatio = (Input.StartAngularSpeedRadPerSecond * R - V) / FMath::Max(FMath::Abs(V), 5.0);
		Out.TractionControlSlip = (Input.StartAngularSpeedRadPerSecond * R - V) / FMath::Max(FMath::Abs(V), 1.5);
		// The force law's slip: over the road speed, floored at 1.5 m/s as the implicit solve is.
		const double Transport = FMath::Max(FMath::Abs(V), 1.5);
		const double SlipX = (Input.StartAngularSpeedRadPerSecond * R - V) / Transport;
		const double SlipY = Input.LateralSlip;
		double Secant = 0.0, PeakSlipX = 0.1;
		TmeasyCombined(Car, Fz, Input.GripScale, SlipX, SlipY, Secant, PeakSlipX);
		Out.PeakSlipRatio = PeakSlipX;
		// The wheel and the contact solved together on the secant stiffness (never negative),
		// so a spinning or locking wheel stays stable past the peak.
		double Ke = FMath::Max(Secant, 0.0) / Transport;
		double Fx = Ke * (Spin * R - V) / (1.0 + Ke * R * R * Ds / I);
		// A second pass at the substep's mean slip (start and this pass's end), so the curve's
		// stiffness does not depend on how long the substep is: a spinning wheel's force then
		// converges as the physics step shrinks. Under engine torque either way (drive, engine
		// braking, reverse) but not under the brake, whose benches converge with the first pass.
		if (Input.BrakeTorqueNm <= 0.0)
		{
			const double EndSpinGuess = Spin - Fx * R * Ds / I;
			const double MeanSlipX = (0.5 * (Input.StartAngularSpeedRadPerSecond + EndSpinGuess) * R - V) / Transport;
			double MeanSecant = Secant;
			TmeasyCombined(Car, Fz, Input.GripScale, MeanSlipX, SlipY, MeanSecant, PeakSlipX);
			Secant = MeanSecant;
			Ke = FMath::Max(Secant, 0.0) / Transport;
			Fx = Ke * (Spin * R - V) / (1.0 + Ke * R * R * Ds / I);
		}
		// A wheel the brake holds locked does not turn back up: the road force on it is the
		// full slip force while the brake out-torques it.
		const double LockedFx = Ke * -V;
		if (Input.BrakeTorqueNm > 0.0 && FMath::Abs(Spin) < 1.0e-5 && FMath::Abs(LockedFx * R) <= Input.BrakeTorqueNm)
		{
			Fx = LockedFx;
		}
		const double Fy = -Secant * SlipY + Input.LateralForceN;
		Out.GripUseFraction = Fx / FMath::Max(Fz * Car.TmeasyPeakMuX * Input.GripScale, 1.0);
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

	FSimCruiseLongitudinalSubstepOutput SolveLongitudinalSubstep(
		const FSimCruiseCarDefinition& Car,
		const FSimCruiseLongitudinalSubstepInput& Input)
	{
		if (UsesTmeasy(Car))
		{
			return SolveTmeasySubstep(Car, Input);
		}
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
		if (UsesTmeasy(Car))
		{
			Sub.LateralSlip = FMath::Tan(FMath::Clamp(SlipAngle, -1.4, 1.4));
			Sub.GripScale = FMath::Max(Input.SurfaceGripMultiplier, 0.0) *
				(Car.AxleGripMultiplier.IsValidIndex(AxleIndex) ? Car.AxleGripMultiplier[AxleIndex] : 1.0);
			Sub.LateralForceN = ComputeLateralForceN(Car, Input.NormalLoadN, Mu, 0.0,
				-(Input.CamberAngleRad + Input.RoadLeanAngleRad), Input.ForwardSpeedMps);
		}
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
