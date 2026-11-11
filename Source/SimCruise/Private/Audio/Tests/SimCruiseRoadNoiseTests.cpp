// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Audio/SimCruiseRoadNoiseSynth.h"

#if WITH_DEV_AUTOMATION_TESTS

// godot-final checks/RoadNoiseCheck.gd, ported: headroom, levels rising with speed, the
// tyre layer's onset, loudness and squeal band, no tyre sound on gravel, the road
// unchanged while no tyre slips, and the cabin filter.
namespace SimCruiseRoadNoiseTests
{
	using FRoad = FSimCruiseRoadNoiseSynth;
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	struct FStats { double Peak = 0.0, Rms = 0.0; bool bFinite = true; };

	/** One second, in the 367/368-frame chunks the Godot check used; stereo. */
	template <typename RenderFn>
	FStats Measure(RenderFn Render, int32 Frames = 22050)
	{
		FStats Out;
		TArray<float> Buffer;
		double Energy = 0.0;
		int32 Done = 0, Chunk = 0;
		while (Done < Frames)
		{
			const int32 N = FMath::Min(Chunk++ % 2 == 0 ? 368 : 367, Frames - Done);
			Buffer.SetNumZeroed(N * 2);
			Render(Buffer.GetData(), N);
			for (const float V : Buffer)
			{
				if (!FMath::IsFinite(V)) { Out.bFinite = false; continue; }
				Out.Peak = FMath::Max(Out.Peak, static_cast<double>(FMath::Abs(V)));
				Energy += V * V;
			}
			Done += N;
		}
		Out.Rms = FMath::Sqrt(Energy / (2.0 * Frames));
		return Out;
	}

	double Db(double Rms) { return 20.0 * FMath::LogX(10.0, FMath::Max(Rms, 1e-9)); }

	/** Amplitude of one frequency (Goertzel). */
	double Magnitude(const TArray<float>& X, double Hz)
	{
		const double W = UE_DOUBLE_TWO_PI * Hz / FRoad::SampleRate, C = 2.0 * FMath::Cos(W);
		double S1 = 0.0, S2 = 0.0;
		for (const float V : X)
		{
			const double S = V + C * S1 - S2;
			S2 = S1;
			S1 = S;
		}
		return FMath::Sqrt(FMath::Max(S1 * S1 + S2 * S2 - C * S1 * S2, 0.0)) / X.Num();
	}

	FRoad::FInputs Road(double Speed, double Grip, double Bump, double Contact = 1.0)
	{
		FRoad::FInputs In;
		In.SpeedMps = Speed;
		In.Grip = Grip;
		In.Bump = Bump;
		In.Contact = Contact;
		return In;
	}

	void SetTyres(FRoad::FInputs& In, double S, double Slide, double Load = 1.0)
	{
		In.bTyres = true;
		for (FRoad::FTyre& T : In.Tyres) { T.S = S; T.Load = Load; T.Slide = Slide; }
	}
}

using namespace SimCruiseRoadNoiseTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadNoiseTest, "SimCruise.Audio.RoadNoise", SimCruiseRoadNoiseTests::Flags)
bool FSimCruiseRoadNoiseTest::RunTest(const FString& Parameters)
{
	auto Report = [this](const FString& Name, const FStats& S)
	{
		AddInfo(FString::Printf(TEXT("%-28s peak %.3f rms %.4f (%.1f dBFS)"), *Name, S.Peak, S.Rms, Db(S.Rms)));
		TestTrue(Name + TEXT(": finite samples"), S.bFinite);
		TestTrue(FString::Printf(TEXT("%s: headroom (peak %.3f under .95)"), *Name, S.Peak), S.Peak < .95);
	};

	// Tyre hiss on asphalt rises with speed; at 30 m/s it sits near -16 dBFS before its bus.
	{
		FRoad Noise;
		double Last = -1.0;
		for (const double Speed : { 0.0, 10.0, 20.0, 30.0, 45.0 })
		{
			const FStats S = Measure([&](float* B, int32 N) { Noise.RenderRoad(B, N, Road(Speed, 1.0, 0.0)); });
			Report(FString::Printf(TEXT("road asphalt %2.0f m/s"), Speed), S);
			TestTrue(FString::Printf(TEXT("asphalt louder at %.0f m/s"), Speed), S.Rms > Last);
			Last = S.Rms;
			if (Speed == 30.0)
			{
				TestTrue(FString::Printf(TEXT("asphalt at 30 m/s near -16 dBFS (%.1f)"), Db(S.Rms)), Db(S.Rms) > -19.0 && Db(S.Rms) < -13.0);
			}
		}
		Report(TEXT("road gravel 20 m/s"), Measure([&](float* B, int32 N) { Noise.RenderRoad(B, N, Road(20.0, .76, .2)); }));
		Report(TEXT("road verge 45 m/s bump 1"), Measure([&](float* B, int32 N) { Noise.RenderRoad(B, N, Road(45.0, .65, 1.0)); }));
		Report(TEXT("road airborne 30 m/s"), Measure([&](float* B, int32 N) { Noise.RenderRoad(B, N, Road(30.0, 1.0, 0.0, 0.0)); }));
	}

	// The tyre layer: silent below the scrub onset, at least -22 dBFS just past the plateau,
	// louder as slip grows; a squeal in the 600-1600 Hz band.
	{
		FRoad Noise;
		TArray<double> Levels;
		for (const double S : { .5, .8, 1.0, 1.1, 1.5, 2.0 })
		{
			FRoad::FInputs In;
			SetTyres(In, S, 3.0 + 6.0 * FMath::Max(S - 1.0, 0.0));
			const FStats St = Measure([&](float* B, int32 N) { Noise.RenderTyres(B, N, In.Tyres, 1.0); });
			Report(FString::Printf(TEXT("tyres s %.1f"), S), St);
			Levels.Add(St.Rms);
		}
		TestTrue(FString::Printf(TEXT("tyres silent at s 0.5 (%.1f dBFS)"), Db(Levels[0])), Db(Levels[0]) < -40.0);
		TestTrue(FString::Printf(TEXT("tyres at least -22 dBFS at s 1.1 (%.1f)"), Db(Levels[3])), Db(Levels[3]) >= -22.0);
		for (int32 I = 1; I < Levels.Num(); ++I)
		{
			TestTrue(FString::Printf(TEXT("tyres not quieter at the larger slip %d"), I), Levels[I] >= Levels[I - 1] * .97);
		}
		FRoad::FInputs Squeal;
		SetTyres(Squeal, 1.5, 8.0);
		TArray<float> Buf;
		Buf.SetNumZeroed(8192 * 2);
		Noise.RenderTyres(Buf.GetData(), 8192, Squeal.Tyres, 1.0);
		Noise.RenderTyres(Buf.GetData(), 8192, Squeal.Tyres, 1.0);
		TArray<float> Left;
		for (int32 I = 0; I < 8192; ++I) Left.Add(Buf[I * 2]);
		double Best = 0.0, BestHz = 0.0;
		for (double Hz = 100.0; Hz <= 4000.0; Hz += 25.0)
		{
			const double M = Magnitude(Left, Hz);
			if (M > Best) { Best = M; BestHz = Hz; }
		}
		AddInfo(FString::Printf(TEXT("squeal spectral peak %.0f Hz at s 1.5, 8 m/s sliding"), BestHz));
		TestTrue(TEXT("squeal peak within 600-1600 Hz"), BestHz >= 600.0 && BestHz <= 1600.0);
	}

	// No tyre sound on gravel; with no tyre slipping, the road is sample for sample the same.
	{
		FRoad WithTyres, Without;
		FRoad::FInputs Loose = Road(20.0, .76, .2);
		SetTyres(Loose, 2.0, 8.0);
		const FStats A = Measure([&](float* B, int32 N) { WithTyres.RenderRoad(B, N, Loose); });
		const FStats Bs = Measure([&](float* B, int32 N) { Without.RenderRoad(B, N, Road(20.0, .76, .2)); });
		TestTrue(FString::Printf(TEXT("no tyre layer on gravel (%.5f vs %.5f)"), A.Rms, Bs.Rms), FMath::Abs(A.Rms - Bs.Rms) < 1e-6);
		FRoad P, Q;
		FRoad::FInputs Gripping = Road(30.0, 1.0, .1);
		SetTyres(Gripping, .3, 1.0);
		bool bSame = true;
		TArray<float> X, Y;
		for (int32 Chunk = 0; Chunk < 12; ++Chunk)
		{
			X.SetNumZeroed(368 * 2);
			Y.SetNumZeroed(368 * 2);
			P.RenderRoad(X.GetData(), 368, Gripping);
			Q.RenderRoad(Y.GetData(), 368, Road(30.0, 1.0, .1));
			bSame &= X == Y;
		}
		TestTrue(TEXT("the road is unchanged while no tyre is past its scrub onset"), bSame);
	}

	// Wind rises with speed, limited below .85.
	{
		FRoad Noise;
		double Last = -1.0;
		for (const double Speed : { 0.0, 15.0, 30.0, 45.0 })
		{
			const FStats S = Measure([&](float* B, int32 N) { Noise.RenderWind(B, N, Speed); });
			Report(FString::Printf(TEXT("wind %2.0f m/s"), Speed), S);
			TestTrue(FString::Printf(TEXT("wind louder at %.0f m/s"), Speed), S.Rms > Last);
			Last = S.Rms;
		}
	}

	// The mix: the cabin takes the treble off, outside keeps it; road and wind together stay
	// within the Godot level plan (road .358 + wind .027 at their peaks).
	{
		FRoad Inside, Outside;
		FRoad::FInputs In = Road(30.0, 1.0, 0.0);
		In.bCabin = true;
		FRoad::FInputs Out = In;
		Out.bCabin = false;
		TArray<float> A, B;
		A.SetNumZeroed(22050 * 2);
		B.SetNumZeroed(22050 * 2);
		Inside.Render(A.GetData(), 22050, In);
		Outside.Render(B.GetData(), 22050, Out);
		TArray<float> AL, BL;
		for (int32 I = 0; I < 22050; ++I) { AL.Add(A[I * 2]); BL.Add(B[I * 2]); }
		const double Ratio = Magnitude(AL, 5000.0) / FMath::Max(Magnitude(BL, 5000.0), 1e-12);
		AddInfo(FString::Printf(TEXT("cabin / outside at 5 kHz: %.1f dB"), Db(Ratio)));
		TestTrue(TEXT("the cabin is at least 12 dB down at 5 kHz"), Db(Ratio) < -12.0);
		float Peak = 0.f;
		for (const float V : B) Peak = FMath::Max(Peak, FMath::Abs(V));
		TestTrue(FString::Printf(TEXT("the mixed road and wind stay under .40 (%.3f)"), Peak), Peak < .40f);
	}
	return true;
}

#endif
