// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseCameraModels.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLensTrueFovTest, "SimCruise.Gameplay.Lens.TrueFov",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseLensTrueFovTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseLens;
	const double Aspect = 16.0 / 9.0;
	// A 15.6 in 16:9 laptop seen from 60 cm: 34.5 x 19.4 cm, 2 atan(17.27 / 60) = 32.1 deg across.
	TestEqual(TEXT("15.6 in 16:9 picture height"), ScreenHeightCm(15.6, Aspect), 19.42, 0.02);
	TestEqual(TEXT("15.6 in at 60 cm subtends 32.1 deg"), TrueHorizontalFovDeg(15.6, Aspect, 60.0), 32.11, 0.05);
	// A 27 in monitor at 70 cm and a 55 in TV at 2 m.
	TestEqual(TEXT("27 in at 70 cm"), TrueHorizontalFovDeg(27.0, Aspect, 70.0), FMath::RadiansToDegrees(2.0 * FMath::Atan(0.5 * 27.0 * 2.54 * 16.0 / FMath::Sqrt(337.0) / 70.0)), 1e-9);
	TestEqual(TEXT("a 55 in TV at 2 m subtends 33.9 deg"), TrueHorizontalFovDeg(55.0, Aspect, 200.0), 33.86, 0.05);
	// The camera takes the vertical angle and widens it by the window's aspect: the two must agree.
	for (const double A : {16.0 / 9.0, 16.0 / 10.0, 21.0 / 9.0, 4.0 / 3.0})
	{
		const double Height = ScreenHeightCm(15.6, A);
		const double Across = HorizontalFromVerticalDeg(VerticalFovDeg(ESimCruiseLens::Screen, Height, 60.0), A);
		TestEqual(FString::Printf(TEXT("true-FOV lens matches the geometry at aspect %.2f"), A), Across, TrueHorizontalFovDeg(15.6, A, 60.0), 0.01);
	}
	// The chase camera keeps its wider view.
	TestTrue(TEXT("chase view is wider than the true FOV"), HorizontalFromVerticalDeg(ChaseVerticalDeg, Aspect) > 60.0);
	return true;
}

#endif
