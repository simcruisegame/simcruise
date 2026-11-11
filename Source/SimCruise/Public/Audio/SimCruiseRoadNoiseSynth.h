// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A two-pole low-pass (RBJ biquad) for one channel: the cabin with the soft top up loses
 * the outside's treble above 2.4 kHz (godot-final:game/RoadNoise.gd CABIN_CUTOFF_HZ).
 */
struct SIMCRUISE_API FSimCruiseCabinFilter
{
	void Set(double CutoffHz, double SampleRate, double Q = 0.7071);
	float Process(float X);

	double B0 = 1.0, B1 = 0.0, B2 = 0.0, A1 = 0.0, A2 = 0.0;
	double X1 = 0.0, X2 = 0.0, Y1 = 0.0, Y2 = 0.0;
};

/**
 * Procedural tyre, road and wind noise (godot-final:game/RoadNoise.gd with the
 * feel/slip-haptics-audio tyre layer and the audio/no-wind level, ported): no sample files.
 *
 * Road layer (stereo): tyre hiss whose amplitude rises with speed^1.5 through a band-pass
 * at 850 Hz + 9 Hz per m/s, gravel crunch as Poisson bursts (25 + 4.5 v stones a second),
 * a 60 Hz rumble off the pavement and over rough road, and each tyre's scrub (near its
 * limit), squeal (past it: a resonant 700 Hz + 25 Hz per m/s of sliding) and skid hiss,
 * all through a tanh limiter below 0.90. Wind: level ~ v^2, cutoff rising with speed,
 * slow buffeting, limited below 0.85.
 *
 * The mix is the Godot bus plan: road -8 dB, wind -30 dB (the owner's request of
 * 10 November 2026: wind only a faint hiss at highway speed), and in the driver's seat a
 * 2.4 kHz low-pass over both. Pure DSP: no engine, no device, any thread.
 */
class SIMCRUISE_API FSimCruiseRoadNoiseSynth
{
public:
	static constexpr int32 SampleRate = 22050;
	static constexpr double RoadBus = 0.39810717; // -8 dB
	static constexpr double WindBus = 0.031622777; // -30 dB
	static constexpr double CabinCutoffHz = 2400.0;

	struct FTyre
	{
		/** Normalised slip: 1 = the end of the full-grip plateau (SimCruiseHaptics::NormalizedSlip). */
		double S = 0.0;
		/** Normal load over the car's reference corner load. */
		double Load = 0.0;
		/** Contact-patch sliding speed, m/s. */
		double Slide = 0.0;
	};

	struct FInputs
	{
		double SpeedMps = 0.0;
		/** Mean surface grip under the tyres: 1 paved, about 0.76 gravel, 0.65 verge. */
		double Grip = 1.0;
		/** Road roughness 0..1 (high-passed suspension travel rate). */
		double Bump = 0.0;
		/** Fraction of wheels touching the ground. */
		double Contact = 1.0;
		/** FL, FR, RL, RR; without them the tyre layer is silent. */
		FTyre Tyres[4];
		bool bTyres = false;
		bool bCabin = true;
	};

	FSimCruiseRoadNoiseSynth();

	/** The mixed stereo output (interleaved L, R), bus gains and cabin filter applied. */
	void Render(float* OutInterleaved, int32 Frames, const FInputs& In);

	/** The road layer alone before its bus (interleaved L, R), as RoadNoise.render_road. */
	void RenderRoad(float* OutInterleaved, int32 Frames, const FInputs& In);
	/** The wind layer alone before its bus (interleaved L, R), as RoadNoise.render_wind. */
	void RenderWind(float* OutInterleaved, int32 Frames, double SpeedMps);

	/**
	 * The tyre layer alone (scrub, squeal, skid) written into Out (interleaved), before the
	 * road limiter; false (Out untouched) while it is silent. Paved: 1 sealed .. 0 loose.
	 */
	bool RenderTyres(float* OutInterleaved, int32 Frames, const FTyre* Tyres, double Paved);

private:

	int64 RoadRandom = 7919, WindRandom = 7919, TyreRandom = 104729;
	double Hiss = 0.0, Crunch = 0.0, Rumble = 0.0, HissCutoff = 900.0;
	double BandL = 0.0, BandLLow = 0.0, BandR = 0.0, BandRLow = 0.0;
	double CrunchEnv = 0.0, CrunchLow = 0.0, RumbleA = 0.0, RumbleB = 0.0;
	double Wind = 0.0, WindCutoff = 200.0, WindL = 0.0, WindR = 0.0, GustL = 0.0, GustR = 0.0;
	double ScrubHz = 500.0, SquealHz = 800.0;
	double ScrubLevel[2] = {}, SquealLevel[2] = {}, SkidLevel[2] = {};
	double ScrubLow[2] = {}, ScrubBand[2] = {}, SquealLow[2] = {}, SquealBand[2] = {}, SkidLow[2] = {};
	TArray<float> RoadScratch, WindScratch;
	FSimCruiseCabinFilter Cabin[2];
	bool bCabinFilterSet = false;
};
