// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruisePreflight.h"

namespace SimCruisePreflightTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** A good drive: 60 s, 800 m, on the road, clean pictures. */
	FSimCruisePreflightStats Good()
	{
		FSimCruisePreflightStats S;
		S.DriveSeconds = 60.f;
		S.DistanceM = 800.f;
		S.MeanSpeedMps = 13.f;
		S.Frames = 590;
		S.SkySamples = 4000;
		S.SkyWhitePercent = 3.f;
		S.bViewCheckRan = true;
		S.bViewCheckPassed = true;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightJudgeTest, "SimCruise.Gameplay.Preflight.Judge", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightJudgeTest::RunTest(const FString& Parameters)
{
	using namespace SimCruisePreflightTest;
	TestEqual(TEXT("a good drive passes"), SimCruisePreflightRules::Judge(Good()), FString());

	auto Fails = [this](const TCHAR* What, const FSimCruisePreflightStats& S, const TCHAR* Reason)
	{
		TestTrue(What, SimCruisePreflightRules::Judge(S).Contains(Reason));
	};
	FSimCruisePreflightStats S = Good(); S.SkyWhitePercent = 60.f; Fails(TEXT("white sky"), S, TEXT("white-sky"));
	S = Good(); S.TilePopIns = 1; Fails(TEXT("a tile built in front of the car"), S, TEXT("tile-pop-in"));
	S = Good(); S.CarPopIns = 3; Fails(TEXT("cars appearing"), S, TEXT("car-pop-in"));
	S = Good(); S.RoadSteps = 1; S.MaxStepCm = 12.f; Fails(TEXT("a step in the road"), S, TEXT("road-steps"));
	S = Good(); S.RoadGaps = 1; Fails(TEXT("a gap in the road"), S, TEXT("road-gaps"));
	S = Good(); S.BorderJolts = 1; Fails(TEXT("a jolt at a tile border"), S, TEXT("jolts-at-tile-borders"));
	S = Good(); S.StuckTraffic = 2; Fails(TEXT("stuck traffic"), S, TEXT("stuck-traffic"));
	S = Good(); S.OffRoadSeconds = 5.f; Fails(TEXT("off the road"), S, TEXT("left-the-road"));
	S = Good(); S.bFellBack = true; Fails(TEXT("fell back to Miami Beach"), S, TEXT("fell-back"));
	S = Good(); S.bViewCheckPassed = false; S.ViewCheckReasons = TEXT("a,b"); Fails(TEXT("the view check failed"), S, TEXT("viewcheck-fail(a+b)"));
	S = Good(); S.DistanceM = 20.f; Fails(TEXT("the robot did not drive"), S, TEXT("robot-moved-only"));
	S = Good(); S.Frames = 40; Fails(TEXT("the recording failed"), S, TEXT("few-frames"));

	S = Good(); S.CarPopIns = 2; S.Jolts = 4; S.SkySamples = 10; S.SkyWhitePercent = 90.f;
	TestEqual(TEXT("two car pop-ins, jolts away from borders and too few sky samples pass"), SimCruisePreflightRules::Judge(S), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightStepsTest, "SimCruise.Gameplay.Preflight.Steps", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightStepsTest::RunTest(const FString& Parameters)
{
	// A 4 % grade with a crest, sampled every 0.4 m: no step.
	TArray<FVector2f> Smooth;
	for (int32 I = 0; I < 200; ++I)
	{
		const float X = I * 0.4f;
		Smooth.Add(FVector2f(X, 0.04f * X - 0.0005f * X * X));
	}
	float MaxCm = 0.f;
	TestEqual(TEXT("a smooth road has no step"), SimCruisePreflightRules::CountSteps(Smooth, MaxCm), 0);
	TestTrue(TEXT("and the largest jump is small"), MaxCm < 1.f);

	// The same road with a 12 cm step at 40 m.
	TArray<FVector2f> Stepped = Smooth;
	for (FVector2f& P : Stepped)
	{
		if (P.X >= 40.f) P.Y += 0.12f;
	}
	TestEqual(TEXT("one step"), SimCruisePreflightRules::CountSteps(Stepped, MaxCm), 1);
	TestTrue(TEXT("of about 12 cm"), FMath::Abs(MaxCm - 12.f) < 1.f);

	// Samples far apart are not compared.
	TArray<FVector2f> Sparse = { FVector2f(0.f, 0.f), FVector2f(5.f, 0.3f), FVector2f(10.f, 0.f) };
	TestEqual(TEXT("samples 5 m apart are not judged"), SimCruisePreflightRules::CountSteps(Sparse, MaxCm), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreflightFlashTest, "SimCruise.Gameplay.Preflight.Flashes", SimCruisePreflightTest::Flags)
bool FSimCruisePreflightFlashTest::RunTest(const FString& Parameters)
{
	TArray<float> Diffs;
	TArray<bool> Turning;
	for (int32 I = 0; I < 100; ++I)
	{
		Diffs.Add(4.f + (I % 3));
		Turning.Add(false);
	}
	float Median = 0.f;
	TestEqual(TEXT("an even drive has no flash"), SimCruisePreflightRules::CountFlashes(Diffs, Turning, Median), 0);
	TestEqual(TEXT("median"), Median, 5.f);
	Diffs[50] = 60.f;
	Diffs[70] = 60.f;
	Turning[70] = true;
	TestEqual(TEXT("one flash; the one while turning is not counted"), SimCruisePreflightRules::CountFlashes(Diffs, Turning, Median), 1);
	return true;
}

#endif
