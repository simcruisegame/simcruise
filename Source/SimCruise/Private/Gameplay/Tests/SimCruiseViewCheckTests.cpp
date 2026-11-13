// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseViewCheck.h"

namespace SimCruiseViewCheckTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	void Set(FSimCruiseViewStats& S, ESimCruiseViewHit Hit, float Percent) { S.Percent[static_cast<int32>(Hit)] = Percent; }

	/** A street: road ahead, buildings to the side, a textured image. */
	FSimCruiseViewStats Street()
	{
		FSimCruiseViewStats S;
		S.RaysBelowHorizon = 600;
		Set(S, ESimCruiseViewHit::Road, 45.f);
		Set(S, ESimCruiseViewHit::Building, 20.f);
		Set(S, ESimCruiseViewHit::Ground, 20.f);
		Set(S, ESimCruiseViewHit::Tree, 10.f);
		Set(S, ESimCruiseViewHit::Empty, 5.f);
		S.NearestRoadM = 4.f;
		S.BuildingCells = 30;
		S.bPlaceHasRoads = true;
		S.bPlaceHasBuildings = true;
		S.bHasImage = true;
		S.Edges = 8.f;
		S.LumaStdDev = 40.f;
		S.HueBins = 9;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseViewCheckJudgeTest, "SimCruise.Gameplay.ViewCheck.Judge", SimCruiseViewCheckTest::Flags)
bool FSimCruiseViewCheckJudgeTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseViewCheckTest;
	TestEqual(TEXT("a street passes"), SimCruiseViewCheckRules::Judge(Street()), FString());

	// The drive robot's pad: every ray below the horizon on an unknown slab, no road, no building.
	FSimCruiseViewStats Pad = Street();
	for (float& P : Pad.Percent) { P = 0.f; }
	Set(Pad, ESimCruiseViewHit::Other, 100.f);
	Pad.NearestRoadM = -1.f;
	Pad.BuildingCells = 0;
	const FString Reasons = SimCruiseViewCheckRules::Judge(Pad);
	TestTrue(TEXT("the pad fails on the world share"), Reasons.Contains(TEXT("world-")));
	TestTrue(TEXT("the pad fails on the road"), Reasons.Contains(TEXT("no-road")));
	TestTrue(TEXT("the pad fails on buildings"), Reasons.Contains(TEXT("no-building")));

	FSimCruiseViewStats Countryside = Street();
	Countryside.bPlaceHasBuildings = false;
	Countryside.BuildingCells = 0;
	TestEqual(TEXT("no buildings are asked for where the map has none"), SimCruiseViewCheckRules::Judge(Countryside), FString());

	FSimCruiseViewStats Flat = Street();
	Flat.Edges = 0.5f;
	TestTrue(TEXT("a flat image fails"), SimCruiseViewCheckRules::Judge(Flat).Contains(TEXT("flat-image")));

	FSimCruiseViewStats NoShot = Street();
	NoShot.bHasImage = false;
	TestTrue(TEXT("a missing screenshot fails"), SimCruiseViewCheckRules::Judge(NoShot).Contains(TEXT("no-screenshot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseViewCheckImageTest, "SimCruise.Gameplay.ViewCheck.Image", SimCruiseViewCheckTest::Flags)
bool FSimCruiseViewCheckImageTest::RunTest(const FString& Parameters)
{
	constexpr int32 W = 64, H = 40;
	TArray<FColor> Grey;
	Grey.Init(FColor(128, 128, 128), W * H);
	FSimCruiseViewStats Flat;
	SimCruiseViewCheckRules::MeasureImage(W, H, Grey, Flat);
	TestTrue(TEXT("measured"), Flat.bHasImage);
	TestEqual(TEXT("a grey image has no edges"), Flat.Edges, 0.f);
	TestEqual(TEXT("a grey image has no spread"), Flat.LumaStdDev, 0.f);
	TestEqual(TEXT("a grey image has no hue"), Flat.HueBins, 0);

	// Black and white columns, and red, green and blue rows in the lower part.
	TArray<FColor> Busy;
	Busy.SetNum(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			static const FColor Rgb[3] = { FColor(200, 30, 30), FColor(30, 200, 30), FColor(30, 30, 200) };
			Busy[Y * W + X] = (X % 2 == 0) ? Rgb[Y % 3] : FColor::Black;
		}
	}
	FSimCruiseViewStats Edgy;
	SimCruiseViewCheckRules::MeasureImage(W, H, Busy, Edgy);
	TestTrue(TEXT("stripes have strong edges"), Edgy.Edges > 20.f);
	TestEqual(TEXT("three hues"), Edgy.HueBins, 3);

	TestEqual(TEXT("slug"), SimCruiseViewCheckRules::Slug(TEXT("Ocean Drive, Miami Beach")), FString(TEXT("ocean-drive-miami-beach")));
	TestEqual(TEXT("empty slug"), SimCruiseViewCheckRules::Slug(TEXT("  ")), FString(TEXT("place")));
	return true;
}

#endif
