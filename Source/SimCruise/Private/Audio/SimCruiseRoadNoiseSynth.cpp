// Copyright SimCruise contributors. All Rights Reserved.

#include "Audio/SimCruiseRoadNoiseSynth.h"

namespace
{
	double SmoothStep(double A, double B, double X)
	{
		const double T = FMath::Clamp((X - A) / (B - A), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** The Godot LCG: next state and white noise in -1..1. */
	double Next(int64& State)
	{
		State = (State * 1664525 + 1013904223) & 0x7fffffff;
		return static_cast<double>(State) / 1073741824.0 - 1.0;
	}

	// Layer gains (RoadNoise.gd, set by checks/RoadNoiseCheck.gd).
	constexpr double HissGain = .72, CrunchGain = .55, RumbleGain = 9.0, WindGain = 2.7;
	constexpr double PavedGrip = .92, GravelGrip = .70;
	constexpr double ScrubFromS = .6, SquealFullS = 1.4, SkidFullS = 2.5;
	constexpr double SquealBaseHz = 700.0, SquealHzPerMps = 25.0;
	constexpr double ScrubGain = 1.0, SquealGain = 2.0, SkidGain = .3;
	constexpr double TyreLooseGrip = .80, TyrePavedGrip = .95;
}

void FSimCruiseCabinFilter::Set(double CutoffHz, double Rate, double Q)
{
	const double W0 = UE_DOUBLE_TWO_PI * CutoffHz / Rate, C = FMath::Cos(W0), Alpha = FMath::Sin(W0) / (2.0 * Q);
	const double A0 = 1.0 + Alpha;
	B0 = (1.0 - C) * 0.5 / A0;
	B1 = (1.0 - C) / A0;
	B2 = B0;
	A1 = -2.0 * C / A0;
	A2 = (1.0 - Alpha) / A0;
}

float FSimCruiseCabinFilter::Process(float X)
{
	const double Y = B0 * X + B1 * X1 + B2 * X2 - A1 * Y1 - A2 * Y2;
	X2 = X1;
	X1 = X;
	Y2 = Y1;
	Y1 = Y;
	return static_cast<float>(Y);
}

FSimCruiseRoadNoiseSynth::FSimCruiseRoadNoiseSynth() = default;

void FSimCruiseRoadNoiseSynth::Render(float* Out, int32 Frames, const FInputs& In)
{
	if (Frames < 1) return;
	if (!bCabinFilterSet)
	{
		Cabin[0].Set(CabinCutoffHz, SampleRate);
		Cabin[1].Set(CabinCutoffHz, SampleRate);
		bCabinFilterSet = true;
	}
	RoadScratch.SetNumUninitialized(Frames * 2, EAllowShrinking::No);
	WindScratch.SetNumUninitialized(Frames * 2, EAllowShrinking::No);
	RenderRoad(RoadScratch.GetData(), Frames, In);
	RenderWind(WindScratch.GetData(), Frames, In.SpeedMps);
	for (int32 I = 0; I < Frames * 2; ++I)
	{
		const float Mixed = static_cast<float>(RoadScratch[I] * RoadBus + WindScratch[I] * WindBus);
		// The filter runs always (so switching view does not click on stale state).
		const float Filtered = Cabin[I & 1].Process(Mixed);
		Out[I] = In.bCabin ? Filtered : Mixed;
	}
}

void FSimCruiseRoadNoiseSynth::RenderRoad(float* Out, int32 Frames, const FInputs& In)
{
	if (Frames < 1) return;
	FMemory::Memzero(Out, sizeof(float) * Frames * 2);
	const double Speed = FMath::Abs(In.SpeedMps);
	const double Grip = In.Grip, Contact = In.Contact;
	const double Paved = SmoothStep(GravelGrip, PavedGrip, Grip);
	const bool bTyre = In.bTyres && RenderTyres(Out, Frames, In.Tyres, SmoothStep(TyreLooseGrip, TyrePavedGrip, Grip) * Contact);
	const double Loose = 1.0 - Paved;
	const double Offroad = 1.0 - SmoothStep(.62, GravelGrip, Grip);
	const double Pace = FMath::Clamp(Speed / 35.0, 0.0, 1.3);
	// Pass-by tyre noise rises about 30 dB per decade of speed: amplitude ~ v^1.5.
	const double HissTarget = (.04 + .96 * FMath::Pow(Pace, 1.5)) * (1.0 - .45 * Loose) * Contact;
	const double CrunchTarget = SmoothStep(.5, 8.0, Speed) * (.35 + .65 * FMath::Clamp(Speed / 22.0, 0.0, 1.0)) * Loose * Contact;
	const double RumbleTarget = FMath::Clamp(.28 * Offroad * SmoothStep(.5, 6.0, Speed) + 1.2 * In.Bump, 0.0, 1.0) * Contact;
	const double CutoffTarget = (850.0 + 9.0 * Speed) * (1.0 - .35 * Loose);
	HissCutoff += (CutoffTarget - HissCutoff) * .3;
	const double F = 2.0 * FMath::Sin(UE_DOUBLE_PI * HissCutoff / SampleRate);
	const double Smoothing = 1.0 - FMath::Exp(-1.0 / (SampleRate * .04));
	const double CrunchDecay = FMath::Exp(-1.0 / (SampleRate * .0055));
	const int64 EventThreshold = static_cast<int64>((25.0 + 4.5 * Speed) / SampleRate * 2147483647.0);
	for (int32 I = 0; I < Frames; ++I)
	{
		Hiss += (HissTarget - Hiss) * Smoothing;
		Crunch += (CrunchTarget - Crunch) * Smoothing;
		Rumble += (RumbleTarget - Rumble) * Smoothing;
		const double Wl = Next(RoadRandom);
		const double Wr = Next(RoadRandom);
		// Independent left and right white noise through a state-variable band-pass (Q about 1.1).
		BandLLow += F * BandL; BandL += F * (Wl - BandLLow - .9 * BandL);
		BandRLow += F * BandR; BandR += F * (Wr - BandRLow - .9 * BandR);
		// Gravel: Poisson bursts of low-passed noise.
		Next(RoadRandom);
		if (RoadRandom < EventThreshold)
		{
			CrunchEnv = FMath::Max(CrunchEnv, .35 + .65 * static_cast<double>(RoadRandom) / static_cast<double>(FMath::Max<int64>(EventThreshold, 1)));
		}
		CrunchEnv *= CrunchDecay;
		const double Grain = Next(RoadRandom) * CrunchEnv;
		CrunchLow += .33 * (Grain - CrunchLow);
		// Rumble: white noise through two one-pole low-passes at 60 Hz.
		RumbleA += .0171 * (Next(RoadRandom) - RumbleA);
		RumbleB += .0171 * (RumbleA - RumbleB);
		const double Shared = CrunchLow * Crunch * CrunchGain + RumbleB * Rumble * RumbleGain;
		double L = BandL * Hiss * HissGain + Shared;
		double R = BandR * Hiss * HissGain + Shared;
		if (bTyre)
		{
			L += Out[I * 2];
			R += Out[I * 2 + 1];
		}
		Out[I * 2] = static_cast<float>(.9 * FMath::Tanh(L / .9));
		Out[I * 2 + 1] = static_cast<float>(.9 * FMath::Tanh(R / .9));
	}
}

bool FSimCruiseRoadNoiseSynth::RenderTyres(float* Out, int32 Frames, const FTyre* Tyres, double Paved)
{
	double SideScrub[2] = {}, SideSqueal[2] = {}, SideSkid[2] = {};
	double HzSum = 0.0, HzWeight = 0.0, SlideSum = 0.0, SlideWeight = 0.0;
	for (int32 W = 0; W < 4; ++W)
	{
		const double S = Tyres[W].S, Load = FMath::Clamp(Tyres[W].Load, 0.0, 2.0), Slide = Tyres[W].Slide;
		const double A = SmoothStep(ScrubFromS, 1.0, S) * Load * SmoothStep(.3, 2.0, Slide) * Paved;
		if (A <= 0.0) continue;
		const double Tone = SmoothStep(1.0, SquealFullS, S);
		const double Skid = SmoothStep(SquealFullS, SkidFullS, S);
		SideScrub[W % 2] += A * (1.0 - .6 * Tone) * .5;
		SideSqueal[W % 2] += A * Tone * .5;
		SideSkid[W % 2] += A * Skid * .5;
		HzSum += A * Tone * (SquealBaseHz + SquealHzPerMps * Slide);
		HzWeight += A * Tone;
		SlideSum += A * Slide;
		SlideWeight += A;
	}
	// Each side's tyres in their channel, half their level in the other.
	const double TargetScrub[2] = { (SideScrub[0] + .5 * SideScrub[1]) / 1.5, (SideScrub[1] + .5 * SideScrub[0]) / 1.5 };
	const double TargetSqueal[2] = { (SideSqueal[0] + .5 * SideSqueal[1]) / 1.5, (SideSqueal[1] + .5 * SideSqueal[0]) / 1.5 };
	const double TargetSkid[2] = { (SideSkid[0] + .5 * SideSkid[1]) / 1.5, (SideSkid[1] + .5 * SideSkid[0]) / 1.5 };
	double Loudest = 0.0, Ringing = 0.0;
	for (int32 C = 0; C < 2; ++C)
	{
		Loudest = FMath::Max(Loudest, FMath::Max3(TargetScrub[C], TargetSqueal[C], TargetSkid[C]));
		Ringing = FMath::Max(Ringing, FMath::Max3(ScrubLevel[C], SquealLevel[C], SkidLevel[C]));
	}
	if (Loudest <= 0.0 && Ringing < 1e-4)
	{
		return false;
	}
	if (HzWeight > 0.0) SquealHz += (FMath::Clamp(HzSum / HzWeight, 600.0, 1600.0) - SquealHz) * .3;
	if (SlideWeight > 0.0) ScrubHz += (480.0 + 12.0 * FMath::Clamp(SlideSum / SlideWeight, 0.0, 40.0) - ScrubHz) * .3;
	const double Fs = 2.0 * FMath::Sin(UE_DOUBLE_PI * ScrubHz / SampleRate), Qs = 1.0 / 1.4;
	const double Fq = 2.0 * FMath::Sin(UE_DOUBLE_PI * SquealHz / SampleRate), Qq = 1.0 / 8.0;
	// A noise-excited band-pass passes energy in proportion to its bandwidth (frequency at fixed Q).
	const double GainSqueal = FMath::Sqrt(1000.0 / SquealHz) * SquealGain;
	const double Hp = FMath::Exp(-UE_DOUBLE_TWO_PI * 1000.0 / SampleRate);
	const double Smoothing = 1.0 - FMath::Exp(-1.0 / (SampleRate * .015));
	for (int32 C = 0; C < 2; ++C)
	{
		int64 State = TyreRandom;
		for (int32 I = 0; I < Frames; ++I)
		{
			ScrubLevel[C] += (TargetScrub[C] - ScrubLevel[C]) * Smoothing;
			SquealLevel[C] += (TargetSqueal[C] - SquealLevel[C]) * Smoothing;
			SkidLevel[C] += (TargetSkid[C] - SkidLevel[C]) * Smoothing;
			const double N = Next(State);
			ScrubLow[C] += Fs * ScrubBand[C]; ScrubBand[C] += Fs * (Qs * N - ScrubLow[C] - Qs * ScrubBand[C]);
			SquealLow[C] += Fq * SquealBand[C]; SquealBand[C] += Fq * (Qq * N - SquealLow[C] - Qq * SquealBand[C]);
			SkidLow[C] = N + (SkidLow[C] - N) * Hp;
			Out[I * 2 + C] = static_cast<float>(ScrubBand[C] * ScrubLevel[C] * ScrubGain + SquealBand[C] * SquealLevel[C] * GainSqueal + (N - SkidLow[C]) * SkidLevel[C] * SkidGain);
		}
		TyreRandom = State;
	}
	return true;
}

void FSimCruiseRoadNoiseSynth::RenderWind(float* Out, int32 Frames, double SpeedMps)
{
	if (Frames < 1) return;
	const double Speed = FMath::Abs(SpeedMps);
	const double LevelTarget = FMath::Clamp(Speed * Speed / 1600.0, 0.0, 1.4);
	WindCutoff += ((160.0 + 7.0 * Speed) - WindCutoff) * .3;
	const double A = 1.0 - FMath::Exp(-UE_DOUBLE_TWO_PI * WindCutoff / SampleRate);
	const double Slow = UE_DOUBLE_TWO_PI * 1.2 / SampleRate;
	const double Smoothing = 1.0 - FMath::Exp(-1.0 / (SampleRate * .04));
	for (int32 I = 0; I < Frames; ++I)
	{
		Wind += (LevelTarget - Wind) * Smoothing;
		const double Wl = Next(WindRandom);
		const double Wr = Next(WindRandom);
		GustL += Slow * (Wl - GustL); GustR += Slow * (Wr - GustR);
		WindL += A * (Wl - WindL); WindR += A * (Wr - WindR);
		const double L = WindL * Wind * WindGain * (1.0 + FMath::Clamp(GustL * 40.0, -.35, .35));
		const double R = WindR * Wind * WindGain * (1.0 + FMath::Clamp(GustR * 40.0, -.35, .35));
		Out[I * 2] = static_cast<float>(.85 * FMath::Tanh(L / .85));
		Out[I * 2 + 1] = static_cast<float>(.85 * FMath::Tanh(R / .85));
	}
}
