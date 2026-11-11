// Copyright SimCruise contributors. All Rights Reserved.

#include "Audio/SimCruiseInlineFourSynth.h"

namespace
{
	double SmoothStep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
}

FSimCruiseInlineFourSynth::FSimCruiseInlineFourSynth()
{
	// Each smooth, band-limited cycle is one evenly spaced exhaust event. A short pressure
	// pulse keeps the upper orders under load; gentle running and the cabin pickup
	// emphasise the fundamental; turbulent air is separate.
	static const double Light[] = { .82, .14, .085, .067, .045, .037, .028, .020, .014, .012, .007, .005 };
	static const double Load[] = { .64, .31, .19, .13, .10, .072, .073, .054, .055, .046, .035, .031, .025, .019, .012, .007 };
	constexpr int32 NumLight = UE_ARRAY_COUNT(Light), NumLoad = UE_ARRAY_COUNT(Load);
	for (int32 I = 0; I <= Size; ++I)
	{
		const double Theta = UE_DOUBLE_TWO_PI * static_cast<double>(I) / Size;
		double Quiet = 0.0, Loud = 0.0, Air = 0.0;
		for (int32 N = 0; N < NumLoad; ++N)
		{
			Loud += Load[N] * FMath::Sin(Theta * (N + 1) - .12 * N);
			if (N < NumLight) Quiet += Light[N] * FMath::Sin(Theta * (N + 1) - .15 * N);
		}
		for (int32 N = 0; N < 7; ++N) Air += FMath::Sin((Theta - .78) * (N + 1)) * .21 / (1.0 + .36 * N * N);
		ExhaustLight.Add(static_cast<float>(Quiet));
		ExhaustLoad.Add(static_cast<float>(Loud));
		Intake.Add(static_cast<float>(Air));
		PulseWindow.Add(static_cast<float>(.28 + .72 * FMath::Pow(.5 + .5 * FMath::Cos(Theta - .65), 2.0)));
	}
}

void FSimCruiseInlineFourSynth::Render(float* Out, int32 Frames, const FInputs& In)
{
	if (Frames < 1)
	{
		return;
	}
	if (In.bStalled && Gain < .00003)
	{
		FMemory::Memzero(Out, sizeof(float) * Frames);
		return;
	}
	const double Rpm = In.Rpm;
	const double TargetHz = FMath::Clamp(Rpm, 0.0, 7200.0) / 30.0;
	// The engine speed follows the physics continuously: rev limiter, shifts and stalls.
	if (!bFired)
	{
		FiringHz = TargetHz;
		bFired = true;
	}
	const double HzStep = (TargetHz - FiringHz) / Frames;
	const double TargetCharge = In.bStalled ? 0.0 : FMath::Clamp(In.Combustion, 0.0, 1.0);
	const double TargetThrottle = FMath::Clamp(In.Pedal, 0.0, 1.0);
	const double TargetPull = FMath::Clamp(In.ShaftLoad, 0.0, 1.0);
	const double TargetOverrun = FMath::Clamp(-In.ShaftLoad, 0.0, 1.0) * SmoothStep(1150.0, 2300.0, Rpm) * (1.0 - TargetCharge);
	const double TargetGain = In.bStalled ? 0.0 : 1.0;
	const double Smoothing = 1.0 - FMath::Exp(-1.0 / (SampleRate * .025));
	const double Filter = In.bCabin ? .20 : .52;
	const double BoomLevel = In.bCabin ? .45 : 0.0;
	const double ExhaustPresence = In.bCabin ? 1.0 : 1.12;
	// The powertrain cuts torque over the limiter; with the pedal still down the sound
	// alternates between fuel cut and firing at LimiterHz.
	const bool bLimiting = Rpm > LimiterRpm && TargetThrottle > .35 && !In.bStalled;
	const double CutSmoothing = 1.0 - FMath::Exp(-1.0 / (SampleRate * .0025));
	// Overrun pops: likely right after the throttle closes, fading over about 1.2 s.
	if (TargetOverrun > .25 && Rpm > PopRpm) OverrunAge += static_cast<double>(Frames) / SampleRate;
	else OverrunAge = 0.0;
	double PopChance = 0.0;
	if (OverrunAge > 0.0) PopChance = .10 * TargetOverrun * SmoothStep(PopRpm, 4200.0, Rpm) * FMath::Exp(-OverrunAge / 1.2);
	const double PopLevel = In.bCabin ? .30 : .55;
	const double PopDecay = FMath::Exp(-1.0 / (SampleRate * .012));
	for (int32 I = 0; I < Frames; ++I)
	{
		Throttle += (TargetThrottle - Throttle) * Smoothing;
		Charge += (TargetCharge - Charge) * Smoothing;
		Pull += (TargetPull - Pull) * Smoothing;
		Overrun += (TargetOverrun - Overrun) * Smoothing;
		Gain += (TargetGain - Gain) * Smoothing;
		FiringHz += HzStep;
		Phase += FiringHz * Size / SampleRate;
		if (Phase >= Size)
		{
			Phase -= Size;
			// A small zero-mean cycle-to-cycle variation, strongest under light load.
			PulseGain = 1.0 + (static_cast<double>(RandomState) / 1073741824.0 - 1.0) * (.035 - .018 * Charge);
			if (PopChance > 0.0 && static_cast<double>(RandomState) / 2147483647.0 < PopChance)
			{
				PopEnv = PopLevel * (.6 + .4 * static_cast<double>(RandomState & 1023) / 1023.0);
			}
		}
		double CutTarget = 1.0;
		if (bLimiting)
		{
			LimiterPhase += LimiterHz / SampleRate;
			if (LimiterPhase >= 1.0) LimiterPhase -= 1.0;
			if (LimiterPhase < .42) CutTarget = .12;
		}
		CutGain += (CutTarget - CutGain) * CutSmoothing;
		const int32 K = static_cast<int32>(Phase) & Mask;
		const double T = Phase - FMath::FloorToDouble(Phase);
		const double Quiet = FMath::Lerp(ExhaustLight[K], ExhaustLight[K + 1], T);
		const double Loaded = FMath::Lerp(ExhaustLoad[K], ExhaustLoad[K + 1], T);
		const double Air = FMath::Lerp(Intake[K], Intake[K + 1], T);
		RandomState = (RandomState * 1664525 + 1013904223) & 0x7fffffff;
		const double White = static_cast<double>(RandomState) / 1073741824.0 - 1.0;
		NoiseLow += .32 * (White - NoiseLow);
		NoiseSlow += .04 * (NoiseLow - NoiseSlow);
		const double Breath = (NoiseLow - NoiseSlow) * PulseWindow[K];
		const double Speed = FMath::Clamp(FiringHz / 215.0, 0.0, 1.0);
		const double Pulse = FMath::Lerp(Quiet, Loaded, Charge) * PulseGain * CutGain;
		const double ExhaustLevel = (.17 + .34 * Charge + .14 * Pull + .08 * Speed - .045 * Overrun) * ExhaustPresence;
		const double InletLevel = (.02 + .42 * Throttle * Charge) * (In.bCabin ? .50 : 1.0) * CutGain;
		const double Airflow = (.036 + .13 * Charge + .08 * Overrun) * (.55 + Speed) + .05 * (1.0 - CutGain);
		PopEnv *= PopDecay;
		PopLow += .35 * (White * PopEnv - PopLow);
		const double Exhaust = Pulse * ExhaustLevel;
		Boom += .034 * (Exhaust - Boom);
		const double Pressure = Exhaust + Boom * BoomLevel + Air * InletLevel + Breath * Airflow + PopLow;
		OutputLow += Filter * (Pressure - OutputLow);
		Out[I] = static_cast<float>(FMath::Clamp(OutputLow * Gain, -.94, .94));
	}
}
