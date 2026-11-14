// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseCockpitTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseCockpitRimFitTest, "SimCruise.Gameplay.Cockpit.RimFit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseCockpitRimFitTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseCockpitRules;
	// A rim of radius 18 cm in the Y-Z plane (the column is X), its centre 0.3 cm off the axis, a
	// flat bottom (the straight part below -15 cm), and three spokes from the hub: the fit must
	// keep the round tube only.
	TArray<FVector> Points;
	const FVector C(0.0, 0.3, 0.0);
	for (int32 I = 0; I < 360; ++I)
	{
		const double A = FMath::DegreesToRadians(double(I));
		FVector P = C + FVector(0.0, 18.0 * FMath::Cos(A), 18.0 * FMath::Sin(A));
		if (P.Z < -15.0) P.Z = -15.0;
		Points.Add(P);
	}
	for (int32 S = 0; S < 3; ++S)
	{
		const double A = FMath::DegreesToRadians(90.0 + 120.0 * S);
		for (double R = 13.0; R < 16.0; R += 0.5) Points.Add(C + FVector(-2.0, R * FMath::Cos(A), R * FMath::Sin(A)));
	}
	FVector N, Centre;
	double Radius = 0.0;
	int32 Kept = 0;
	TestTrue(TEXT("the rim fits"), FitRimCircle(Points, N, Centre, Radius, Kept));
	TestNearlyEqual(TEXT("radius"), Radius, 18.0, 0.3);
	TestNearlyEqual(TEXT("centre off the axis"), FVector2D(Centre.Y, Centre.Z).Size(), 0.3, 0.1);
	const double TiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Abs(N.X), 0.0, 1.0)));
	TestTrue(TEXT("square to the column"), TiltDeg < 0.5);
	TestTrue(TEXT("spokes and the flat bottom are dropped"), Kept < Points.Num() - 18);
	return true;
}

#endif
