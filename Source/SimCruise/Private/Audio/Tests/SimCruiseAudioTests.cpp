// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Audio/SimCruiseInlineFourSynth.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseAudioTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;
	constexpr int32 Rate = FSimCruiseInlineFourSynth::SampleRate;

	/** Renders Seconds in 512-sample buffers (as the mixer pulls them). */
	TArray<float> Render(FSimCruiseInlineFourSynth& Synth, const FSimCruiseInlineFourSynth::FInputs& In, double Seconds)
	{
		TArray<float> Out;
		Out.SetNumZeroed(FMath::RoundToInt(Seconds * Rate));
		for (int32 At = 0; At < Out.Num(); At += 512)
		{
			Synth.Render(Out.GetData() + At, FMath::Min(512, Out.Num() - At), In);
		}
		return Out;
	}

	/** Amplitude of one frequency over the samples (Goertzel). */
	double Magnitude(const TArray<float>& X, double Hz)
	{
		const double W = UE_DOUBLE_TWO_PI * Hz / Rate, C = 2.0 * FMath::Cos(W);
		double S1 = 0.0, S2 = 0.0;
		for (const float V : X)
		{
			const double S = V + C * S1 - S2;
			S2 = S1;
			S1 = S;
		}
		return FMath::Sqrt(FMath::Max(S1 * S1 + S2 * S2 - C * S1 * S2, 0.0)) / X.Num();
	}

	double Rms(const TArray<float>& X, int32 From = 0, int32 To = -1)
	{
		To = To < 0 ? X.Num() : To;
		double Sum = 0.0;
		for (int32 I = From; I < To; ++I) Sum += X[I] * X[I];
		return FMath::Sqrt(Sum / FMath::Max(To - From, 1));
	}

	/** Energy above Hz relative to the total (first difference as a crude high-pass). */
	double Brightness(const TArray<float>& X)
	{
		double Hi = 0.0, All = 0.0;
		for (int32 I = 1; I < X.Num(); ++I)
		{
			Hi += FMath::Square(X[I] - X[I - 1]);
			All += FMath::Square(X[I]);
		}
		return Hi / FMath::Max(All, 1e-12);
	}
}

using namespace SimCruiseAudioTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseInlineFourTest, "SimCruise.Audio.InlineFour", Flags)
bool FSimCruiseInlineFourTest::RunTest(const FString& Parameters)
{
	// Idle at 1260 rpm, light load, from the driver's seat.
	FSimCruiseInlineFourSynth::FInputs Idle;
	Idle.Rpm = 1260.0;
	Idle.Combustion = 0.12;
	Idle.ShaftLoad = 0.05;
	Idle.bCabin = true;
	FSimCruiseInlineFourSynth A, B;
	const TArray<float> X = Render(A, Idle, 2.0), Y = Render(B, Idle, 2.0);
	TestTrue(TEXT("the same inputs give the same samples"), X == Y);
	float Peak = 0.f;
	for (const float V : X) Peak = FMath::Max(Peak, FMath::Abs(V));
	TestTrue(FString::Printf(TEXT("output within +-0.94 (peak %.3f)"), Peak), Peak <= 0.94f);
	TestTrue(FString::Printf(TEXT("idle is audible (RMS %.4f)"), Rms(X)), Rms(X) > 0.01);

	// Two firing events per crank revolution: the fundamental is rpm / 30 = 42 Hz.
	TArray<float> Steady(X.GetData() + Rate / 2, X.Num() - Rate / 2);
	const double F0 = Idle.Rpm / 30.0;
	const double M0 = Magnitude(Steady, F0), MHalf = Magnitude(Steady, F0 * 0.5), MOff = Magnitude(Steady, F0 * 1.5);
	AddInfo(FString::Printf(TEXT("idle: %.1f Hz %.5f, half order %.5f, order 1.5 %.5f"), F0, M0, MHalf, MOff));
	TestTrue(TEXT("the firing frequency dominates its neighbours (no half orders: four even firings in 720 degrees)"), M0 > 4.0 * MHalf && M0 > 4.0 * MOff);
	for (int32 Order = 2; Order <= 6; ++Order)
	{
		TestTrue(FString::Printf(TEXT("idle in the cabin: order %d below the fundamental"), Order), Magnitude(Steady, F0 * Order) < M0);
	}

	// The port against the Godot voice's own idle orders (lab/vehicle/engine-sound/acoustics.json
	// "model": orders 1-10 relative to the strongest, floored at -45 dB): within 2 dB in the
	// median. Its distance from the recorded interior of an idling NB (Freesound 523351) is
	// reported: the Godot analysis (Welch windows) put it at 3.54 dB.
	{
		const double Interior[10] = { 0.0, -23.1, -24.6, -23.5, -20.8, -29.7, -31.2, -29.5, -27.6, -30.9 };
		const double Godot[10] = { 0.0, -15.2, -19.9, -21.6, -24.8, -26.8, -29.5, -32.6, -34.6, -36.2 };
		TArray<double> PortErrors;
		double Db[10], Strongest = 0.0;
		for (int32 N = 0; N < 10; ++N) Strongest = FMath::Max(Strongest, Magnitude(Steady, F0 * (N + 1)));
		TArray<double> Errors;
		FString Line;
		for (int32 N = 0; N < 10; ++N)
		{
			Db[N] = FMath::Max(20.0 * FMath::LogX(10.0, FMath::Max(Magnitude(Steady, F0 * (N + 1)), 1e-12) / Strongest), -45.0);
			Errors.Add(FMath::Abs(Db[N] - Interior[N]));
			PortErrors.Add(FMath::Abs(Db[N] - Godot[N]));
			Line += FString::Printf(TEXT(" %.1f"), Db[N]);
		}
		Errors.Sort();
		PortErrors.Sort();
		const double Median = 0.5 * (Errors[4] + Errors[5]), PortMedian = 0.5 * (PortErrors[4] + PortErrors[5]);
		AddInfo(FString::Printf(TEXT("idle cabin orders 1-10 (dB):%s; median error: %.2f dB against the Godot voice, %.2f dB against the recorded interior"), *Line, PortMedian, Median));
		TestTrue(TEXT("idle cabin harmonics within 2 dB (median) of the Godot voice"), PortMedian < 2.0);
	}

	// The cabin rolls the exhaust off; outside it is brighter.
	FSimCruiseInlineFourSynth::FInputs Pull;
	Pull.Rpm = 4000.0;
	Pull.Combustion = 0.9;
	Pull.ShaftLoad = 0.8;
	Pull.Pedal = 1.0;
	Pull.bCabin = true;
	FSimCruiseInlineFourSynth Cabin, Outside;
	const TArray<float> InCabin = Render(Cabin, Pull, 1.0);
	Pull.bCabin = false;
	const TArray<float> Out = Render(Outside, Pull, 1.0);
	AddInfo(FString::Printf(TEXT("4000 rpm full load: cabin brightness %.4f, outside %.4f; RMS cabin %.3f, outside %.3f"),
		Brightness(InCabin), Brightness(Out), Rms(InCabin), Rms(Out)));
	TestTrue(TEXT("outside is brighter than the cabin"), Brightness(Out) > 1.5 * Brightness(InCabin));
	TestTrue(TEXT("full load is louder than idle"), Rms(InCabin) > 1.5 * Rms(X));

	// A stall fades to silence within half a second.
	FSimCruiseInlineFourSynth Stall;
	Render(Stall, Idle, 0.5);
	FSimCruiseInlineFourSynth::FInputs Dead = Idle;
	Dead.bStalled = true;
	Dead.Rpm = 0.0;
	const TArray<float> Fade = Render(Stall, Dead, 1.0);
	TestTrue(FString::Printf(TEXT("silent 0.5 s after a stall (RMS %.6f)"), Rms(Fade, Rate / 2)), Rms(Fade, Rate / 2) < 1e-3);

	// On the limiter with the pedal down the fuel cut stutters at 14 Hz.
	FSimCruiseInlineFourSynth::FInputs Limiter;
	Limiter.Rpm = 7000.0;
	Limiter.Combustion = 1.0;
	Limiter.ShaftLoad = 0.5;
	Limiter.Pedal = 1.0;
	FSimCruiseInlineFourSynth Lim;
	const TArray<float> L = Render(Lim, Limiter, 1.0);
	const int32 Window = Rate / 56; // a quarter of a 14 Hz cycle
	double Lo = 1e9, Hi = 0.0;
	for (int32 At = Rate / 4; At + Window <= L.Num(); At += Window)
	{
		const double R = Rms(L, At, At + Window);
		Lo = FMath::Min(Lo, R);
		Hi = FMath::Max(Hi, R);
	}
	AddInfo(FString::Printf(TEXT("limiter: window RMS %.4f..%.4f"), Lo, Hi));
	TestTrue(TEXT("the limiter cuts and fires (loudest quarter-cycle over twice the quietest)"), Hi > 2.0 * Lo);
	return true;
}

#endif
