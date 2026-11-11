// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Presentation/SimCruiseCarClock.h"
#include "Presentation/SimCruiseLowBeamPattern.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileDetail.h"

namespace
{
	constexpr EAutomationTestFlags WorldDetailTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLowBeamPatternTest, "SimCruise.Presentation.LowBeamPattern", WorldDetailTestFlags)
bool FSimCruiseLowBeamPatternTest::RunTest(const FString& Parameters)
{
	using SimCruiseLowBeamPattern::Relative;
	// The hot spot sits just under the cut-off, slightly to the curb side.
	TestTrue(TEXT("hot spot near full"), Relative(-1.4, 2.5) > 0.95);
	// Oncoming side: a sharp horizontal cut-off 0.57 degrees down.
	TestTrue(TEXT("oncoming side lit under the cut-off"), Relative(-1.0, -5.0) > 0.5);
	TestTrue(TEXT("oncoming side dark above it (glare under 2 %)"), Relative(0.0, -5.0) < 0.02 && Relative(1.0, -5.0) < 0.02);
	// Curb side: the cut-off rises at 15 degrees, so signs at the right are lit.
	TestTrue(TEXT("curb side lit above the horizon past the kink"), Relative(0.5, 6.0) > 0.4);
	TestTrue(TEXT("under 1 % high above the road"), Relative(5.0, 0.0) < 0.01 && Relative(5.0, 10.0) < 0.01);
	// The foreground is dimmer than the throw, so the near road does not dazzle the driver.
	TestTrue(TEXT("foreground dimmer than the hot spot"), Relative(-15.0, 0.0) < 0.5 * Relative(-1.4, 2.5));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseParkingLayoutTest, "SimCruise.World.ParkingLayout", WorldDetailTestFlags)
bool FSimCruiseParkingLayoutTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseLanes;
	// Ocean Drive: a 10 m one-way tertiary street with built-up edges parks on both sides.
	const FParking Ocean = Parking(TEXT("tertiary"), 5.0, 1, true, true);
	TestEqual(TEXT("Ocean Drive parks left"), Ocean.Left, 2.4);
	TestEqual(TEXT("Ocean Drive parks right"), Ocean.Right, 2.4);
	// Its single lane is centred between the parking lanes.
	TestEqual(TEXT("one lane, centred"), RightLaneCentreM(TEXT("tertiary"), 5.0, 1, Ocean), 0.0, 1e-9);
	// Narrow one-way: one side, the driver's right first, else the other curb.
	const FParking NarrowRight = Parking(TEXT("residential"), 3.0, 1, true, true);
	TestTrue(TEXT("6 m one-way parks on the driver's right"), NarrowRight.Right == 2.4 && NarrowRight.Left == 0.0);
	const FParking NarrowLeft = Parking(TEXT("residential"), 3.0, 1, true, false);
	TestTrue(TEXT("6 m one-way with only a left curb parks there"), NarrowLeft.Left == 2.4 && NarrowLeft.Right == 0.0);
	// Two-way: both sides or none.
	TestEqual(TEXT("11 m two-way parks both sides"), Parking(TEXT("residential"), 5.5, 0, true, true).Left, 2.4);
	TestEqual(TEXT("9 m two-way has no room"), Parking(TEXT("residential"), 4.5, 0, true, true).Left, 0.0);
	// Arterials and rural roads never.
	TestEqual(TEXT("primary never"), Parking(TEXT("primary"), 8.0, 0, true, true).Right, 0.0);
	TestEqual(TEXT("rural never"), Parking(TEXT("residential"), 6.0, 0, false, false).Right, 0.0);
	// Without parking the lane layout is the old one.
	TestEqual(TEXT("unchanged without parking"), RightLaneCentreM(TEXT("residential"), 5.0, 0), RightLaneCentreM(TEXT("residential"), 5.0, 0, FParking()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRegionalDetailTest, "SimCruise.World.RegionalDetail", WorldDetailTestFlags)
bool FSimCruiseRegionalDetailTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseTileDetail;
	TestTrue(TEXT("Ocean Drive at 8th Street is Art Deco"), StyleForPlace(25.7780, -80.1312) == EBuildingStyle::MiamiBeachDeco);
	TestTrue(TEXT("Downtown Miami is not"), StyleForPlace(25.7743, -80.1937) == EBuildingStyle::Generic);
	TestTrue(TEXT("Telluride is not"), StyleForPlace(37.9374, -107.8119) == EBuildingStyle::Generic);
	TestTrue(TEXT("coconut palms in Miami Beach"), IsCoconutPalmRegion(25.78, -80.13));
	TestTrue(TEXT("and Key West"), IsCoconutPalmRegion(24.555, -81.78));
	TestTrue(TEXT("and Honolulu"), IsCoconutPalmRegion(21.28, -157.83));
	TestFalse(TEXT("not Orlando"), IsCoconutPalmRegion(28.54, -81.38));
	TestFalse(TEXT("not Oatman"), IsCoconutPalmRegion(35.04, -114.37));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseCarClockTest, "SimCruise.Presentation.CarClock", WorldDetailTestFlags)
bool FSimCruiseCarClockTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseCarClock;
	auto At = [](const TCHAR* Iso) { FDateTime T; FDateTime::ParseIso8601(Iso, T); return T; };
	TestEqual(TEXT("Miami Beach, November: EST"), Format12(LocalMinutes(At(TEXT("2026-11-11T16:00:00Z")), 25.78, -80.13)), FString(TEXT("11:00")));
	TestEqual(TEXT("Miami Beach, July: EDT"), Format12(LocalMinutes(At(TEXT("2026-07-01T16:00:00Z")), 25.78, -80.13)), FString(TEXT("12:00")));
	TestEqual(TEXT("Chicago, January: CST"), Format12(LocalMinutes(At(TEXT("2026-01-15T18:30:00Z")), 41.88, -87.63)), FString(TEXT("12:30")));
	TestEqual(TEXT("Phoenix keeps MST in summer"), Format12(LocalMinutes(At(TEXT("2026-07-01T19:05:00Z")), 33.45, -112.07)), FString(TEXT("12:05")));
	TestEqual(TEXT("Los Angeles, July: PDT"), Format12(LocalMinutes(At(TEXT("2026-07-01T19:05:00Z")), 34.05, -118.24)), FString(TEXT("12:05")));
	TestEqual(TEXT("Honolulu: HST, no DST"), Format12(LocalMinutes(At(TEXT("2026-07-01T22:00:00Z")), 21.31, -157.86)), FString(TEXT("12:00")));
	TestEqual(TEXT("Telluride in winter: MST"), Format12(LocalMinutes(At(TEXT("2026-02-01T07:00:00Z")), 37.94, -107.81)), FString(TEXT("12:00")));
	TestEqual(TEXT("DST starts on the second Sunday of March"), Format12(LocalMinutes(At(TEXT("2026-03-08T07:30:00Z")), 25.78, -80.13)), FString(TEXT("3:30")));
	TestEqual(TEXT("midnight shows 12"), Format12(0), FString(TEXT("12:00")));
	return true;
}

#endif
