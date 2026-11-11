// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTraffic.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseTrafficTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	/** A straight ribbon along +X, HalfW wide, with curbs (built-up) on both sides. */
	FSimCruiseRibbon Straight(const FString& Class, double HalfW, int8 Travel)
	{
		FSimCruiseRibbon R;
		R.Class = Class;
		for (int32 I = 0; I < 3; ++I)
		{
			FSimCruiseRibbonStation St;
			St.C = FVector2d(I * 20.0, 0.0);
			St.T = FVector2d(1.0, 0.0);
			St.R = FVector2d(0.0, 1.0);
			St.S = I * 20.0;
			St.HalfW = HalfW;
			St.Travel = Travel;
			R.St.Add(St);
			for (FSimCruiseRibbonSide& Side : R.Sides)
			{
				Side.Edge.Add(ESimCruiseRoadEdge::Kerb);
				Side.SidewalkM.Add(2.0f);
				Side.Cut.Add(0);
			}
		}
		return R;
	}

	/** Follows a leader at constant speed for Seconds; returns the final gap (m). */
	double Follow(double LeaderMps, double Seconds, double& OutSpeed)
	{
		double V = 0.0, Gap = 30.0;
		for (double T = 0.0; T < Seconds; T += 0.02)
		{
			const double A = SimCruiseTrafficModel::IdmAcceleration(V, 15.6, Gap, V - LeaderMps);
			V = FMath::Max(0.0, V + A * 0.02);
			Gap += (LeaderMps - V) * 0.02;
		}
		OutSpeed = V;
		return Gap;
	}
}

using namespace SimCruiseTrafficTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseTrafficModelTest, "SimCruise.World.Traffic", SimCruiseTrafficTests::Flags)
bool FSimCruiseTrafficModelTest::RunTest(const FString& Parameters)
{
	// Free road: pulls away at 1.4 m/s^2 and settles at the desired speed.
	TestNearlyEqual(TEXT("free road from rest accelerates at 1.4 m/s^2"), SimCruiseTrafficModel::IdmAcceleration(0.0, 15.6, -1.0, 0.0), 1.4, 1e-6);
	TestNearlyEqual(TEXT("no acceleration at the desired speed"), SimCruiseTrafficModel::IdmAcceleration(15.6, 15.6, -1.0, 0.0), 0.0, 1e-6);
	// A stopped car 20 m ahead at 15 m/s: brake hard.
	TestTrue(TEXT("brakes for a stopped car ahead"), SimCruiseTrafficModel::IdmAcceleration(15.0, 15.6, 20.0, 15.0) < -4.0);

	// Behind a leader at 10 m/s it settles at that speed, a little over 2 m + 1.4 s behind.
	double V = 0.0;
	const double Gap = Follow(10.0, 60.0, V);
	AddInfo(FString::Printf(TEXT("following at 10 m/s: %.2f m/s, gap %.1f m"), V, Gap));
	TestTrue(TEXT("matches the leader's speed"), FMath::Abs(V - 10.0) < 0.1);
	TestTrue(FString::Printf(TEXT("keeps about a 1.4 s gap (%.1f m)"), Gap), Gap > 2.0 + 14.0 && Gap < 2.0 + 14.0 + 8.0);
	// Behind a stopped car it stops, at least 2 m short, never through it.
	const double Stop = Follow(0.0, 40.0, V);
	TestTrue(FString::Printf(TEXT("stops behind a stopped car (%.2f m/s, gap %.2f m)"), V, Stop), V < 0.05 && Stop > 1.5);

	// Lanes: a two-way residential street keeps traffic on the right, each way.
	const FSimCruiseRibbon Two = Straight(TEXT("residential"), 6.0, 0);
	const double Along = SimCruiseTrafficModel::LaneOffsetM(Two, 1, 1), Against = SimCruiseTrafficModel::LaneOffsetM(Two, 1, -1);
	AddInfo(FString::Printf(TEXT("two-way 12 m street: %.2f m along, %.2f m against"), Along, Against));
	TestTrue(TEXT("along the street: right of the centre line"), Along > 0.5);
	TestTrue(TEXT("against it: the other side"), Against < -0.5);
	TestNearlyEqual(TEXT("the two directions mirror each other"), Along, -Against, 1e-6);
	// One way, 10 m with parking both sides: one lane, in the middle.
	const FSimCruiseRibbon One = Straight(TEXT("secondary"), 5.0, -1);
	const double OneWay = SimCruiseTrafficModel::LaneOffsetM(One, 1, -1);
	TestTrue(FString::Printf(TEXT("one way, one lane: centred (%.2f)"), OneWay), FMath::Abs(OneWay) < 0.3);
	// One way, 14 m: two lanes; driving against S the right-hand one lies toward -R.
	const FSimCruiseRibbon Wide = Straight(TEXT("secondary"), 7.0, -1);
	const double WideWay = SimCruiseTrafficModel::LaneOffsetM(Wide, 1, -1);
	TestTrue(FString::Printf(TEXT("one way, two lanes: the right lane toward -R (%.2f)"), WideWay), WideWay < -0.5 && WideWay > -7.0);

	TestNearlyEqual(TEXT("residential streets at 25 mph"), SimCruiseTrafficModel::SpeedLimitMps(TEXT("residential")), 11.2, 0.05);
	TestNearlyEqual(TEXT("motorways at 65 mph"), SimCruiseTrafficModel::SpeedLimitMps(TEXT("motorway")), 29.1, 0.05);
	return true;
}

#endif
