// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseRoute.h"

namespace SimCruiseRouteTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/**
	 * Two legs on a flat map where "longitude, latitude" are metres east and south:
	 * east 300 m along Ocean Drive, right (south) 200 m onto 5th Street to Lummus Park,
	 * then on south 400 m to Collins Park.
	 */
	const TCHAR* TwoLegs = TEXT(R"({"status":"ok","length_m":900,"duration_s":120,"legs":[
		{"length_m":500,"duration_s":60,"points":[[0,0],[100,0],[200,0],[300,0],[300,100],[300,200]],
		 "names":[[0,"Ocean Drive"],[3,"5th Street"]],
		 "steps":[{"index":0,"at_m":0,"kind":"depart","angle":0,"name":"Ocean Drive"},
		          {"index":3,"at_m":300,"kind":"right","angle":90,"name":"5th Street"},
		          {"index":5,"at_m":500,"kind":"arrive","angle":0,"name":""}]},
		{"length_m":400,"duration_s":60,"points":[[300,200],[300,400],[300,600]],
		 "names":[[0,"5th Street"]],
		 "steps":[{"index":0,"at_m":0,"kind":"depart","angle":0,"name":"5th Street"},
		          {"index":2,"at_m":400,"kind":"arrive","angle":0,"name":""}]}]})");

	TArray<FSimCruiseRouteStop> Stops()
	{
		return { {TEXT("You are here"), 0.0, 0.0}, {TEXT("Lummus Park"), 300.0, 200.0}, {TEXT("Collins Park"), 300.0, 600.0} };
	}

	FSimCruiseLonLatToM Flat()
	{
		return FSimCruiseLonLatToM::CreateLambda([](double Lon, double Lat) { return FVector2D(Lon, Lat); });
	}

	bool Load(FSimCruiseRoute& Route, FAutomationTestBase& Test)
	{
		FString Error;
		const bool bOk = Route.ParseHelperString(TwoLegs, Stops(), &Error);
		Test.TestTrue(FString::Printf(TEXT("parses (%s)"), *Error), bOk);
		Route.Project(Flat());
		return bOk;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRouteParseTest, "SimCruise.Route.Parse", SimCruiseRouteTest::Flags)
bool FSimCruiseRouteParseTest::RunTest(const FString& Parameters)
{
	FSimCruiseRoute Route;
	if (!SimCruiseRouteTest::Load(Route, *this)) return false;
	TestEqual(TEXT("two legs"), Route.GetLegs().Num(), 2);
	TestEqual(TEXT("leg 1 points"), Route.GetLegs()[0].LonLat.Num(), 6);
	TestEqual(TEXT("street of segment 3"), Route.GetLegs()[0].Streets[3], FString(TEXT("5th Street")));
	TestEqual(TEXT("street of segment 0"), Route.GetLegs()[0].Streets[0], FString(TEXT("Ocean Drive")));
	TestEqual(TEXT("the turn"), Route.GetLegs()[0].Steps[1].Turn, ESimCruiseTurn::Right);
	TestEqual(TEXT("the arrival names the stop"), Route.GetLegs()[0].Steps[2].Street, FString(TEXT("Lummus Park")));
	TestEqual(TEXT("total length"), Route.TotalLengthM(), 900.0, 0.01);
	TestEqual(TEXT("total time"), Route.TotalDurationS(), 120.0, 0.01);

	FSimCruiseRoute Bad;
	FString Error;
	TestFalse(TEXT("a no-route answer is refused"), Bad.ParseHelperString(TEXT(R"({"status":"no-route","leg":1,"reason":"stop 2 is not near a road"})"), SimCruiseRouteTest::Stops(), &Error));
	TestTrue(TEXT("with the helper's reason"), Error.Contains(TEXT("not near a road")));
	TestFalse(TEXT("stops must match the legs"), Bad.ParseHelperString(SimCruiseRouteTest::TwoLegs, { {TEXT("A"), 0, 0}, {TEXT("B"), 1, 1} }, &Error));
	TestFalse(TEXT("not JSON"), Bad.ParseHelperString(TEXT("oops"), SimCruiseRouteTest::Stops(), &Error));
	TestFalse(TEXT("no route is active"), Bad.IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRouteProgressTest, "SimCruise.Route.Progress", SimCruiseRouteTest::Flags)
bool FSimCruiseRouteProgressTest::RunTest(const FString& Parameters)
{
	FSimCruiseRoute Route;
	if (!SimCruiseRouteTest::Load(Route, *this)) return false;
	// 120 m along Ocean Drive, 2 m right of the line (y is south: right of east).
	const FSimCruiseRouteProgress& P = Route.Update(FVector2D(120.0, 2.0), 0.1);
	TestEqual(TEXT("along"), P.AlongM, 120.0, 0.01);
	TestEqual(TEXT("right of the line is +"), P.LateralM, 2.0, 0.01);
	TestEqual(TEXT("next step is the right turn"), P.NextStep, 1);
	TestEqual(TEXT("distance to the turn"), P.ToNextStepM, 180.0, 0.01);
	TestEqual(TEXT("leg remaining"), P.LegRemainingM, 380.0, 0.01);
	TestEqual(TEXT("time remaining scales with the leg"), P.LegRemainingS, 60.0 * 380.0 / 500.0, 0.01);
	TestNull(TEXT("no Then when the step after the turn is more than 100 m on"), Route.GetThenStep(100.0));
	TestNotNull(TEXT("Then within 500 m"), Route.GetThenStep(500.0));

	// Left of the line on 5th Street (heading south, right is west: -x).
	const FSimCruiseRouteProgress& Q = Route.Update(FVector2D(303.0, 150.0), 0.1);
	TestEqual(TEXT("along on 5th Street"), Q.AlongM, 450.0, 0.01);
	TestEqual(TEXT("east of a southbound line is left"), Q.LateralM, -3.0, 0.01);
	TestEqual(TEXT("next is the arrival"), Route.GetStep(Q.NextStep)->Turn, ESimCruiseTurn::Arrive);

	FVector2D M, Dir;
	TestTrue(TEXT("point at"), Route.PointAt(350.0, M, Dir));
	TestEqual(TEXT("point at 350 m"), M, FVector2D(300.0, 50.0));
	TestEqual(TEXT("direction south"), Dir, FVector2D(0.0, 1.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRouteOffRouteTest, "SimCruise.Route.OffRoute", SimCruiseRouteTest::Flags)
bool FSimCruiseRouteOffRouteTest::RunTest(const FString& Parameters)
{
	FSimCruiseRoute Route;
	if (!SimCruiseRouteTest::Load(Route, *this)) return false;
	// 25 m away never counts; 40 m away counts only after 3 s.
	for (int32 i = 0; i < 100; ++i) Route.Update(FVector2D(100.0, 25.0), 0.1);
	TestFalse(TEXT("25 m off the line is still on the route"), Route.GetProgress().bOffRoute);
	for (int32 i = 0; i < 29; ++i) Route.Update(FVector2D(100.0, 40.0), 0.1);
	TestFalse(TEXT("2.9 s off is not yet off route"), Route.GetProgress().bOffRoute);
	Route.Update(FVector2D(100.0, 40.0), 0.11);
	Route.Update(FVector2D(100.0, 40.0), 0.01);
	TestTrue(TEXT("over 3 s at 40 m is off route"), Route.GetProgress().bOffRoute);
	// Back on the line resets the timer.
	Route.Update(FVector2D(100.0, 0.0), 0.1);
	TestFalse(TEXT("back on the route"), Route.GetProgress().bOffRoute);
	Route.Update(FVector2D(100.0, 40.0), 2.0);
	TestFalse(TEXT("the timer starts again"), Route.GetProgress().bOffRoute);
	TArray<FSimCruiseRouteStop> Rest = Route.RemainingStops();
	TestEqual(TEXT("a new route goes to both stops"), Rest.Num(), 2);
	TestEqual(TEXT("first the next stop"), Rest[0].Title, FString(TEXT("Lummus Park")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRouteArrivalTest, "SimCruise.Route.Arrival", SimCruiseRouteTest::Flags)
bool FSimCruiseRouteArrivalTest::RunTest(const FString& Parameters)
{
	FSimCruiseRoute Route;
	if (!SimCruiseRouteTest::Load(Route, *this)) return false;
	Route.Update(FVector2D(300.0, 150.0), 0.1);
	TestEqual(TEXT("first leg"), Route.GetActiveLeg(), 0);
	Route.Update(FVector2D(301.0, 190.0), 0.1);
	TestEqual(TEXT("at the stop the next leg starts"), Route.GetActiveLeg(), 1);
	TestEqual(TEXT("arrival message"), Route.GetArrivalMessage(), FString(TEXT("You are at Lummus Park")));
	TestEqual(TEXT("the next stop"), Route.GetNextStop()->Title, FString(TEXT("Collins Park")));
	Route.Update(FVector2D(300.0, 250.0), 3.9);
	TestFalse(TEXT("the message lasts 4 s"), Route.GetArrivalMessage().IsEmpty());
	Route.Update(FVector2D(300.0, 260.0), 0.2);
	TestTrue(TEXT("then it goes"), Route.GetArrivalMessage().IsEmpty());
	TestEqual(TEXT("progress on the second leg"), Route.GetProgress().AlongM, 60.0, 0.01);
	Route.Update(FVector2D(300.0, 590.0), 0.1);
	TestTrue(TEXT("the last stop finishes the route"), Route.IsFinished());
	TestFalse(TEXT("no route after the end"), Route.IsActive());
	TestEqual(TEXT("last arrival"), Route.GetArrivalMessage(), FString(TEXT("You are at Collins Park")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRouteTextTest, "SimCruise.Route.Text", SimCruiseRouteTest::Flags)
bool FSimCruiseRouteTextTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseRouteText;
	TestEqual(TEXT("0.3 mi"), Distance(0.3 * 1609.344), FString(TEXT("0.3 mi")));
	TestEqual(TEXT("feet under 0.1 mi"), Distance(100.0), FString(TEXT("330 ft")));
	TestEqual(TEXT("50 ft steps from 500 ft"), Distance(155.0), FString(TEXT("500 ft")));
	TestEqual(TEXT("whole miles from 10"), Distance(12.4 * 1609.344), FString(TEXT("12 mi")));
	TestEqual(TEXT("never 0 ft"), Distance(0.5), FString(TEXT("10 ft")));
	TestEqual(TEXT("right onto"), Instruction(ESimCruiseTurn::Right, TEXT("5th Street")), FString(TEXT("Right onto 5th Street")));
	TestEqual(TEXT("no name"), Instruction(ESimCruiseTurn::SlightLeft, FString()), FString(TEXT("Turn slight left")));
	TestEqual(TEXT("continue"), Instruction(ESimCruiseTurn::Continue, TEXT("Collins Avenue")), FString(TEXT("Continue on Collins Avenue")));
	TestEqual(TEXT("parse kinds"), ParseTurn(TEXT("sharp left")), ESimCruiseTurn::SharpLeft);
	TestEqual(TEXT("minutes"), Duration(9 * 60 + 10), FString(TEXT("9 min")));
	TestEqual(TEXT("hours"), Duration(72 * 60), FString(TEXT("1 h 12 min")));
	TestEqual(TEXT("clock"), Clock(FDateTime(2026, 11, 12, 18, 51, 0)), FString(TEXT("6:51 PM")));
	TestEqual(TEXT("noon"), Clock(FDateTime(2026, 11, 12, 12, 5, 0)), FString(TEXT("12:05 PM")));
	return true;
}

#endif
