// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseReview.h"

namespace SimCruiseReviewTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
}

/** The review's steering and gauge measures: the drawn ratio, lock to lock, and when a needle reads wrong. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseReviewRulesTest, "SimCruise.Gameplay.Review.Rules", SimCruiseReviewTest::Flags)
bool FSimCruiseReviewRulesTest::RunTest(const FString& Parameters)
{
	// A 15:1 rack drawn both ways, with small angles near the centre left out.
	TArray<FVector2D> Samples;
	for (double Wheel = -30.0; Wheel <= 30.0; Wheel += 1.0)
	{
		Samples.Add(FVector2D(15.0 * Wheel, Wheel));
	}
	Samples.Add(FVector2D(40.0, 0.5));
	TestTrue(TEXT("ratio 15"), FMath::IsNearlyEqual(SimCruiseReviewRules::SteeringRatio(Samples), 15.0, 1e-6));
	TestEqual(TEXT("no turned samples: 0"), SimCruiseReviewRules::SteeringRatio({ FVector2D(10.0, 1.0) }), 0.0);
	// An NB Miata: 15:1 at about 36.6 deg of road-wheel lock is about 3.05 turns lock to lock.
	TestTrue(TEXT("lock to lock"), FMath::IsNearlyEqual(SimCruiseReviewRules::LockToLockTurns(36.6, 15.0), 3.05, 0.01));

	TestFalse(TEXT("1 mph off at 60 is right"), SimCruiseReviewRules::GaugeIsWrong(61.0, 60.0));
	TestTrue(TEXT("5 mph off at 60 is wrong"), SimCruiseReviewRules::GaugeIsWrong(65.0, 60.0));
	TestFalse(TEXT("1.5 off at 10 is inside the floor"), SimCruiseReviewRules::GaugeIsWrong(11.5, 10.0));
	TestTrue(TEXT("a needle at rest while driving is wrong"), SimCruiseReviewRules::GaugeIsWrong(0.0, 30.0));
	return true;
}

#endif
