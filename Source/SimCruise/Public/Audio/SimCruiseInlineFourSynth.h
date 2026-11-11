// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The procedural voice of a four-stroke inline-four (godot-final:game/InlineFourSound.gd,
 * ported sample for sample): two evenly spaced firing events per crank revolution follow
 * the physics' engine speed; the exhaust's spectrum moves from a light-load to a loaded
 * harmonic table with delivered combustion torque, with induction and airflow noise on
 * top. The cabin pickup keeps the fundamental and a 120 Hz body boom and rolls the
 * exhaust off above about 780 Hz; the exterior pickup is brighter (about 2.6 kHz). Over
 * the rev limiter with the pedal down the fuel cut stutters at 14 Hz; closing the
 * throttle above 3000 rpm gives a decaying chance of mild exhaust pops. The envelopes are
 * estimates, not BP-4W microphone measurements (lab/vehicle/engine-sound).
 *
 * Pure DSP: no engine, no device, any thread. It only reads the physics' outputs and
 * never feeds anything back.
 */
class SIMCRUISE_API FSimCruiseInlineFourSynth
{
public:
	static constexpr int32 SampleRate = 22050;
	static constexpr double LimiterRpm = 6950.0;
	static constexpr double LimiterHz = 14.0;
	static constexpr double PopRpm = 3000.0;

	struct FInputs
	{
		double Rpm = 0.0;
		/** Delivered combustion torque, 0..1 of the engine's peak. */
		double Combustion = 0.0;
		/** Shaft load, -1..1: + driving the car, - overrun (the car drives the engine). */
		double ShaftLoad = 0.0;
		/** The accelerator pedal, 0..1. */
		double Pedal = 0.0;
		bool bStalled = false;
		/** The driver's seat (cabin pickup) rather than outside. */
		bool bCabin = true;
	};

	FSimCruiseInlineFourSynth();

	/**
	 * Turn the voice into a cross-plane V8's: eight firing events per 720 crank degrees
	 * (four a revolution), and the exhaust heard as each bank's pipe delivers it. A
	 * cross-plane crank fires each bank at uneven intervals (90, 180 and 270 degrees), so
	 * with one pipe nearer than the other (BankGains) the pulse train swells and sags once
	 * an engine cycle: the V8's burble at half and quarter orders. BankOfEvent: the bank
	 * (0 or 1) of each event in firing order.
	 */
	void ConfigureCrossPlaneV8(const TArray<int32>& BankOfEvent, double NearGain, double FarGain);
	bool IsV8() const { return bV8; }

	/** Mono samples at SampleRate, within +-0.94. Inputs are smoothed over 25 ms. */
	void Render(float* Out, int32 Frames, const FInputs& In);

private:
	static constexpr int32 Size = 1024;
	static constexpr int32 Mask = Size - 1;

	TArray<float> ExhaustLight, ExhaustLoad, Intake, PulseWindow;
	/** V8: the exhaust over a whole engine cycle (720 degrees), eight events with their banks' gains. */
	TArray<float> CycleLight, CycleLoad;
	bool bV8 = false;
	double Phase = 0.0, FiringHz = 0.0, Charge = 0.0, Pull = 0.0, Overrun = 0.0, Gain = 0.0;
	int64 RandomState = 38791;
	double NoiseLow = 0.0, NoiseSlow = 0.0, OutputLow = 0.0, Boom = 0.0, PulseGain = 1.0, Throttle = 0.0;
	bool bFired = false;
	double LimiterPhase = 0.0, CutGain = 1.0, OverrunAge = 0.0, PopEnv = 0.0, PopLow = 0.0;
};
