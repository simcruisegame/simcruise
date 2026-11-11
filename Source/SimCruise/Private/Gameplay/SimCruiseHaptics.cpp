// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseHaptics.h"

#include "Vehicle/CarDefinition.h"

namespace SimCruiseHaptics
{
	double PlateauSlipAngleRad(const FSimCruiseCarDefinition& Car)
	{
		constexpr int32 Samples = 1001;
		double Force[Samples];
		double Best = 0.0;
		for (int32 N = 0; N < Samples; ++N)
		{
			const double Alpha = N * 0.001;
			Force[N] = FMath::Tanh(Alpha * Car.SlipAngleStiffnessPerRad)
				* (1.0 - Car.SlipAngleFalloffFraction * FMath::SmoothStep(Car.SlipAngleFalloffStartRad, Car.SlipAngleFalloffEndRad, Alpha));
			Best = FMath::Max(Best, Force[N]);
		}
		for (int32 N = 0; N < Samples; ++N)
		{
			if (Force[N] >= Plateau * Best)
			{
				return FMath::Max(N * 0.001, 0.001);
			}
		}
		return 1.0;
	}

	double NormalizedSlip(double Kappa, double PeakKappa, double Alpha, double PeakAlpha)
	{
		const double X = Kappa / FMath::Max(PeakKappa, 0.001);
		const double Y = Alpha / FMath::Max(PeakAlpha, 0.001);
		return FMath::Sqrt(X * X + Y * Y);
	}

	double SlipFeel(double S)
	{
		return LimitShare * FMath::SmoothStep(SlipOnset, SlipLimit, S) + (1.0 - LimitShare) * FMath::SmoothStep(SlipLimit, SlipSlide, S);
	}

	double AbsPulse(bool bActive, double TimeS)
	{
		return bActive && FMath::Fmod(TimeS * AbsHz, 1.0) < 0.5 ? AbsOn : 0.0;
	}

	FSimCruiseRumble Motors(double FrontSlip, double RearSlip, double Pulse, double Roughness, double Loose,
		double SpeedMps, double EngineLoad, bool bAbsOn)
	{
		double Weak = SlipFeel(FrontSlip);
		Weak += TextureGain * FMath::SmoothStep(TextureFrom, TextureTo, Roughness);
		Weak += LooseGain * FMath::Clamp(Loose, 0.0, 1.0) * FMath::SmoothStep(0.5, 6.0, FMath::Abs(SpeedMps));
		Weak += EngineGain * FMath::Clamp(EngineLoad, 0.0, 1.0);
		double Strong = SlipFeel(RearSlip) * (bAbsOn ? AbsSlipShare : 1.0) + Pulse;
		Strong += KerbGain * FMath::SmoothStep(KerbFrom, 1.0, Roughness);

		FSimCruiseRumble Out;
		Out.HighFrequency = static_cast<float>(FMath::Clamp(Weak, 0.0, 1.0));
		Out.LowFrequency = static_cast<float>(FMath::Clamp(Strong, 0.0, 1.0));
		return Out;
	}

	FForceFeedbackValues ToChannels(const FSimCruiseRumble& Rumble, ESimCruiseRumbleBackend Backend)
	{
		FForceFeedbackValues Values;
		if (Backend == ESimCruiseRumbleBackend::GameInput)
		{
			Values.LeftLarge = Rumble.LowFrequency;
			Values.RightLarge = Rumble.HighFrequency;
			Values.LeftSmall = Rumble.LeftTrigger;
			Values.RightSmall = Rumble.RightTrigger;
		}
		else
		{
			// XInputInterface.cpp: left motor = max(LeftLarge, RightLarge),
			// right motor = max(LeftSmall, RightSmall); no trigger motors.
			Values.LeftLarge = Rumble.LowFrequency;
			Values.LeftSmall = Rumble.HighFrequency;
			Values.RightLarge = 0.f;
			Values.RightSmall = 0.f;
		}
		return Values;
	}
}

FSimCruiseRumble FSimCruiseHapticsModel::Sample(const FSimCruiseDriveTelemetry& Telemetry, double Throttle, double Brake, double DeltaTimeS)
{
	using namespace SimCruiseHaptics;
	double FrontSlip = 0.0;
	double RearSlip = 0.0;
	double SpinSlip = 0.0;   // driven wheels turning faster than the road
	double LockSlip = 0.0;   // any wheel turning slower under braking
	bAbsActive = false;
	double Loose = 0.0;
	// A slip angle is only felt while the patch slides sideways, at about
	// v sin(alpha): below walking pace a relaxed angle left over from a slide
	// (or the noise of a near-zero velocity) is not scrub. Wheelspin is felt
	// from standstill, so the slip ratio is not gated.
	const double Speed = Telemetry.BodyVelocityMps.Size();
	const double LateralFeel = FMath::SmoothStep(LateralFeelFromMps, LateralFeelToMps, Speed);
	for (int32 I = 0; I < 4; ++I)
	{
		const FSimCruiseDriveWheelTelemetry& Wheel = Telemetry.Wheels[I];
		Loose += 1.0 - FMath::SmoothStep(0.70, 0.92, Wheel.SurfaceGrip);
		if (!Wheel.bContact || Wheel.NormalLoadN <= 0.0)
		{
			Slip[I] = 0.0;
			continue;
		}
		const double PeakAlpha = Wheel.PeakSlipAngleRad > 0.0 ? Wheel.PeakSlipAngleRad : DefaultPeakSlipAngleRad;
		Slip[I] = NormalizedSlip(Wheel.SlipRatio, Wheel.PeakSlipRatio, Wheel.SlipAngleRad * LateralFeel, PeakAlpha);
		const double Longitudinal = Wheel.SlipRatio / FMath::Max(Wheel.PeakSlipRatio, 0.001);
		if (I < 2)
		{
			FrontSlip = FMath::Max(FrontSlip, Slip[I]);
		}
		else
		{
			RearSlip = FMath::Max(RearSlip, Slip[I]);
		}
		if (Wheel.bDriven)
		{
			RearSlip = FMath::Max(RearSlip, FMath::Abs(Longitudinal));
			SpinSlip = FMath::Max(SpinSlip, Longitudinal);
		}
		LockSlip = FMath::Max(LockSlip, -Longitudinal);
		if (Telemetry.bServiceBrakeOn && Wheel.bAbsActive)
		{
			bAbsActive = true;
		}
	}
	PulseTime = bAbsActive ? PulseTime + DeltaTimeS : 0.0;
	Loose *= 0.25;

	const double Engine = Telemetry.bElectric ? 0.0 : FMath::Clamp(Telemetry.EngineLoad, 0.0, 1.0);
	const double Pulse = AbsPulse(bAbsActive, PulseTime);
	FSimCruiseRumble Out = Motors(FrontSlip, RearSlip, Pulse, Telemetry.RoadRoughness, Loose, Speed, Engine, bAbsActive);

	// Impulse triggers: the pedal under the foot that is losing the tyre.
	if (Throttle > 0.05)
	{
		Out.RightTrigger = static_cast<float>(FMath::Clamp(TriggerGain * SlipFeel(SpinSlip), 0.0, 1.0));
	}
	if (Brake > 0.05)
	{
		Out.LeftTrigger = static_cast<float>(FMath::Clamp(bAbsActive ? Pulse : TriggerGain * SlipFeel(LockSlip), 0.0, 1.0));
	}
	return Out;
}

void FSimCruiseHapticsModel::Quiet()
{
	for (double& S : Slip)
	{
		S = 0.0;
	}
	PulseTime = 0.0;
	bAbsActive = false;
}
