// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruiseUiTheme.h"

namespace SimCruisePauseMenuTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	FReply Press(SWidget& Widget, const FKey& Key)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		return Widget.OnKeyDown(FGeometry(), Event);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePauseMenuPadTest, "SimCruise.UI.PauseMenuPad", SimCruisePauseMenuTest::Flags)
bool FSimCruisePauseMenuPadTest::RunTest(const FString& Parameters)
{
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the menu test needs one"));
		return true;
	}
	using SimCruisePauseMenuTest::Press;
	TArray<TPair<ESimCruiseMenuRow, int32>> Actions;
	TArray<int32> PagesSeen;
	int32 Closed = 0;
	bool bRoute = false;
	TSharedRef<SSimCruisePauseMenu> Menu = SNew(SSimCruisePauseMenu)
		.GetState_Lambda([&bRoute]() { FSimCruiseMenuState S; S.Place = TEXT("Ocean Drive"); S.bRoute = bRoute; return S; })
		.OnAction_Lambda([&Actions](ESimCruiseMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); })
		.OnPage_Lambda([&PagesSeen](int32 Page) { PagesSeen.Add(Page); })
		.OnClose_Lambda([&Closed]() { ++Closed; });

	TestEqual(TEXT("opens on Drive"), Menu->GetPage(), 0);
	TestEqual(TEXT("on Continue driving"), Menu->GetSelectedRow(), ESimCruiseMenuRow::Continue);
	TestTrue(TEXT("D-pad down is handled"), Press(*Menu, EKeys::Gamepad_DPad_Down).IsEventHandled());
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("two down: Plan a route"), Menu->GetSelectedRow(), ESimCruiseMenuRow::PlanRoute);
	Press(*Menu, EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("A chooses Plan a route"), Actions.Num() == 1 && Actions[0].Key == ESimCruiseMenuRow::PlanRoute && Actions[0].Value == 0);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	Press(*Menu, EKeys::Gamepad_DPad_Up);
	TestEqual(TEXT("up from the top wraps to the last row"), Menu->GetSelectedRow(), ESimCruiseMenuRow::Atlas);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	TestEqual(TEXT("right on an action row does nothing"), Actions.Num(), 1);
	Press(*Menu, EKeys::Gamepad_FaceButton_Left);
	TestEqual(TEXT("X without a route does nothing"), Actions.Num(), 1);
	bRoute = true;
	Menu->Tick(FGeometry(), 0.0, 0.016f);
	Press(*Menu, EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("X with a route ends it"), Actions.Num() == 2 && Actions[1].Key == ESimCruiseMenuRow::EndRoute);

	Press(*Menu, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("RB: Your car"), Menu->GetPage(), 1);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("the gearbox row"), Menu->GetSelectedRow(), ESimCruiseMenuRow::Transmission);
	Press(*Menu, EKeys::Gamepad_DPad_Right);
	TestTrue(TEXT("right changes the gearbox"), Actions.Last().Key == ESimCruiseMenuRow::Transmission && Actions.Last().Value == 1);
	Press(*Menu, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("RB: Seat & view"), Menu->GetPage(), 2);
	Press(*Menu, EKeys::Gamepad_DPad_Down);
	Press(*Menu, EKeys::Gamepad_DPad_Left);
	TestTrue(TEXT("left lowers head motion"), Actions.Last().Key == ESimCruiseMenuRow::HeadMotion && Actions.Last().Value == -1);
	Press(*Menu, EKeys::Gamepad_FaceButton_Top);
	TestTrue(TEXT("Y: back to stock"), Actions.Last().Key == ESimCruiseMenuRow::HeadMotion && Actions.Last().Value == 2);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	Press(*Menu, EKeys::Gamepad_LeftShoulder);
	TestEqual(TEXT("LB from Drive wraps to Settings"), Menu->GetPage(), 3);
	TestEqual(TEXT("the page change is reported (Seat & view unpauses the world)"), PagesSeen.Contains(2), true);
	TestEqual(TEXT("each page keeps its row"), (Menu->SetPage(1), Menu->GetSelectedRow()), ESimCruiseMenuRow::Transmission);

	// The keyboard does the same.
	Press(*Menu, EKeys::Q);
	TestEqual(TEXT("Q: previous page"), Menu->GetPage(), 0);
	TestEqual(TEXT("the Drive page kept its row"), Menu->GetSelectedRow(), ESimCruiseMenuRow::Atlas);
	Press(*Menu, EKeys::Down);
	Press(*Menu, EKeys::Down);
	Press(*Menu, EKeys::Enter);
	TestEqual(TEXT("Enter chooses Where to?"), Actions.Last().Key, ESimCruiseMenuRow::WhereTo);
	TestFalse(TEXT("a letter that means nothing is not handled"), Press(*Menu, EKeys::J).IsEventHandled());

	Press(*Menu, EKeys::Gamepad_FaceButton_Right);
	TestEqual(TEXT("B closes"), Closed, 1);
	Press(*Menu, EKeys::Gamepad_Special_Right);
	TestEqual(TEXT("Menu closes"), Closed, 2);
	Press(*Menu, EKeys::Escape);
	TestEqual(TEXT("Esc closes"), Closed, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseThemeTest, "SimCruise.UI.Theme", SimCruisePauseMenuTest::Flags)
bool FSimCruiseThemeTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseTheme;
	TestTrue(TEXT("the Overpass files are in data/fonts/overpass"), HasOverpass());
	TestEqual(TEXT("guide green"), Guide().ToFColor(true), FColor(0x0A, 0x5C, 0x3A));
	TestEqual(TEXT("course magenta"), Course().ToFColor(true), FColor(0xE2, 0x3A, 0x9C));
	TestEqual(TEXT("a font for 34 px is 25.5 pt"), Font(EWeight::ExtraBold, 34.f).Size, 25.5f);
	TestEqual(TEXT("LB is the previous page"), NavOf(EKeys::Gamepad_LeftShoulder), ENav::PrevPage);
	TestEqual(TEXT("A accepts"), NavOf(EKeys::Gamepad_FaceButton_Bottom), ENav::Accept);
	TestEqual(TEXT("RT zooms in"), NavOf(EKeys::Gamepad_RightTrigger), ENav::ZoomIn);
	for (int32 i = 0; i <= static_cast<int32>(ESimCruiseTurn::Arrive); ++i)
	{
		TArray<TArray<FVector2D>> Strokes;
		TurnStrokes(static_cast<ESimCruiseTurn>(i), Strokes);
		bool bInside = Strokes.Num() > 0;
		for (const TArray<FVector2D>& S : Strokes) for (const FVector2D& P : S) bInside &= P.X >= -0.01 && P.X <= 1.01 && P.Y >= -0.01 && P.Y <= 1.01;
		TestTrue(FString::Printf(TEXT("turn arrow %d is inside its box"), i), bInside);
	}
	return true;
}

#endif
