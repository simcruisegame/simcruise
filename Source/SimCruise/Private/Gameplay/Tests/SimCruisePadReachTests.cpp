// Copyright SimCruise contributors. All Rights Reserved.

// SimCruise.UI.PadReachAll: every control on every menu screen is reached and used with an
// Xbox pad alone. The screens sit in a virtual Slate window and the pad's buttons go in as
// key events through FSlateApplication (ProcessKeyDownEvent / ProcessKeyUpEvent, user 0),
// the way XInput sends them, so Slate's own focus, bubbling and buttons run. After every
// press the test asks Slate which widget has the pad's focus.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Gameplay/SimCruiseAtlasUI.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruiseSeatUI.h"
#include "Gameplay/SimCruiseTuningUI.h"
#include "Gameplay/SimCruiseUiTheme.h"
#include "Widgets/SWindow.h"

namespace SimCruisePadReachTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/**
	 * A screen in a hidden 1920 x 1080 Slate window, driven by pad buttons. Each press goes in
	 * through FSlateApplication like XInput's; where the headless editor does not route it to
	 * the window (no OS window behind it), the same event goes along the focused widget's path
	 * as Slate does it: OnPreviewKeyDown from the window down, then OnKeyDown back up.
	 */
	/** Does FSlateApplication route pad keys to the test's window here (the first press finds out)? */
	static bool GSlateRoutes = true;

	struct FPadScreen
	{
		TSharedRef<SWindow> Window;
		TSharedPtr<SWidget> Screen;
		int32 Routed = 0;
		int32 Walked = 0;

		explicit FPadScreen(const TSharedRef<SWidget>& InScreen)
			: Window(SNew(SWindow).ClientSize(FVector2D(1920.0, 1080.0)).IsInitiallyMaximized(false).FocusWhenFirstShown(false))
			, Screen(InScreen)
		{
			Window->SetContent(InScreen);
			FSlateApplication::Get().AddWindow(Window, false);
			Window->SlatePrepass(1.f);
			SimCruiseTheme::FocusPad(InScreen);
			Tick();
		}

		~FPadScreen()
		{
			FSlateApplication::Get().ClearAllUserFocus(EFocusCause::Cleared);
			FSlateApplication::Get().DestroyWindowImmediately(Window);
		}

		/** What the screen does each frame (its Tick puts lost focus back). */
		void Tick()
		{
			Window->SlatePrepass(1.f);
			if (Screen.IsValid()) Screen->Tick(Screen->GetCachedGeometry(), 0.0, 0.016f);
		}

		/** Does Slate's own focus path for user 0 run through the screen (so its routing reaches it)? */
		bool Routable() const
		{
			const TSharedPtr<FSlateUser> User = FSlateApplication::Get().GetUser(0);
			return User.IsValid() && Screen.IsValid() && User->GetWeakFocusPath().ToWidgetPath().ContainsWidget(Screen.Get());
		}

		/** The focused widget and its parents, the focused one first. */
		static TArray<TSharedPtr<SWidget>> FocusChain()
		{
			TArray<TSharedPtr<SWidget>> Chain;
			for (TSharedPtr<SWidget> W = Focus(); W.IsValid(); W = W->GetParentWidget()) Chain.Add(W);
			return Chain;
		}

		static bool Walk(const FKeyEvent& Event, bool bDown)
		{
			const TArray<TSharedPtr<SWidget>> Chain = FocusChain();
			if (bDown)
			{
				for (int32 i = Chain.Num() - 1; i >= 0; --i) if (Chain[i]->OnPreviewKeyDown(Chain[i]->GetCachedGeometry(), Event).IsEventHandled()) return true;
			}
			for (const TSharedPtr<SWidget>& W : Chain)
			{
				const FReply Reply = bDown ? W->OnKeyDown(W->GetCachedGeometry(), Event) : W->OnKeyUp(W->GetCachedGeometry(), Event);
				if (Reply.IsEventHandled()) return true;
			}
			return false;
		}

		void Press(const FKey& Key)
		{
			FSlateApplication& App = FSlateApplication::Get();
			const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
			if (GSlateRoutes && Routable())
			{
				++Routed;
				App.ProcessKeyDownEvent(Event);
				App.ProcessKeyUpEvent(Event);
			}
			else
			{
				++Walked;
				Walk(Event, true);
				Walk(Event, false);
			}
			Tick();
		}

		/** The left stick pushed (Value) and let go, as XInput's analog events. */
		void Stick(const FKey& Axis, float Value)
		{
			FSlateApplication& App = FSlateApplication::Get();
			for (const float V : { Value, 0.f })
			{
				const FAnalogInputEvent Event(Axis, FModifierKeysState(), 0, false, 0, 0, V);
				if (GSlateRoutes && Routable()) { App.ProcessAnalogInputEvent(Event); continue; }
				for (const TSharedPtr<SWidget>& W : FocusChain()) if (W->OnAnalogValueChanged(W->GetCachedGeometry(), Event).IsEventHandled()) break;
			}
			Tick();
		}

		static TSharedPtr<SWidget> Focus() { return FSlateApplication::Get().GetUserFocusedWidget(0); }

		/** Is the pad's focus on this widget (or inside it, as in a text box)? */
		static bool On(const TSharedPtr<SWidget>& Widget)
		{
			const TSharedPtr<SWidget> F = Focus();
			return Widget.IsValid() && F.IsValid() && (F == Widget || Widget->HasFocusedDescendants());
		}
	};

	FString Name(const TSharedPtr<SWidget>& W)
	{
		return W.IsValid() ? (W->GetTag().IsNone() ? W->GetTypeAsString() : W->GetTag().ToString()) : FString(TEXT("nothing"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePadReachAllTest, "SimCruise.UI.PadReachAll", SimCruisePadReachTest::Flags)
bool FSimCruisePadReachAllTest::RunTest(const FString& Parameters)
{
	using namespace SimCruisePadReachTest;
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the pad test needs one"));
		return true;
	}

	// ------------------------------------------------------------------ pause menu
	{
		TArray<TPair<ESimCruiseMenuRow, int32>> Actions;
		int32 Closed = 0;
		TSharedRef<SSimCruisePauseMenu> Menu = SNew(SSimCruisePauseMenu)
			.GetState_Lambda([]() { FSimCruiseMenuState S; S.Place = TEXT("Ocean Drive"); return S; })
			.OnAction_Lambda([&Actions](ESimCruiseMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); })
			.OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Menu);
		AddInfo(FString::Printf(TEXT("pause menu: the focus after opening is on %s"), *Name(Pad.Focus())));
		GSlateRoutes = true;
		Pad.Press(EKeys::Gamepad_DPad_Down);
		AddInfo(FString::Printf(TEXT("probe: routable=%d, after one Down through Slate the row is %d, the focus on %s"), Pad.Routable() ? 1 : 0, Menu->GetSelected(), *Name(Pad.Focus())));
		if (Menu->GetSelected() != 1)
		{
			// The headless editor did not route it to the window: send each press along the focus path as Slate does.
			GSlateRoutes = false;
			AddInfo(TEXT("FSlateApplication does not route pad keys to a hidden window in this editor: presses go along the focus path (preview down, then up)"));
			Pad.Press(EKeys::Gamepad_DPad_Down);
		}
		const bool bRouted = Menu->GetSelected() == 1;
		Pad.Press(EKeys::Gamepad_DPad_Up);
		if (!TestTrue(FString::Printf(TEXT("a pad key reaches the menu (the focus is on %s)"), *Name(Pad.Focus())), bRouted)) return false;
		TestTrue(FString::Printf(TEXT("pause menu opens with the pad on Continue driving (on %s)"), *Name(Pad.Focus())), Pad.On(Menu->GetRowWidget(0, 0)));

		// The top row: Up from the first row, then along every tab.
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(FString::Printf(TEXT("Up from the first row reaches the Drive tab (on %s)"), *Name(Pad.Focus())), Menu->IsOnTabs() && Pad.On(Menu->GetTabWidget(0)));
		for (int32 T = 1; T < SSimCruisePauseMenu::NumPages; ++T)
		{
			Pad.Press(EKeys::Gamepad_DPad_Right);
			TestTrue(FString::Printf(TEXT("Right reaches the %s tab (on %s)"), *SSimCruisePauseMenu::PageName(T), *Name(Pad.Focus())), Pad.On(Menu->GetTabWidget(T)));
		}
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Right past the last tab comes round to Drive"), Pad.On(Menu->GetTabWidget(0)));
		Pad.Press(EKeys::Gamepad_DPad_Left);
		TestTrue(TEXT("Left from Drive goes to Settings"), Pad.On(Menu->GetTabWidget(3)));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(TEXT("Up on the tab row stays on it (no trap, no jump)"), Pad.On(Menu->GetTabWidget(3)));

		// Every tab opens with A (and Down), on its first row; every row is reached and used.
		for (int32 P = 0; P < SSimCruisePauseMenu::NumPages; ++P)
		{
			if (!Menu->IsOnTabs()) Pad.Press(EKeys::Gamepad_DPad_Up);
			for (int32 Guard = 0; Menu->GetTabCursor() != P && Guard < 8; ++Guard) Pad.Press(EKeys::Gamepad_DPad_Right);
			Pad.Press(P % 2 ? EKeys::Gamepad_DPad_Down : EKeys::Gamepad_FaceButton_Bottom);
			TestEqual(FString::Printf(TEXT("%s opens from its tab"), *SSimCruisePauseMenu::PageName(P)), Menu->GetPage(), P);
			const TArray<SSimCruisePauseMenu::FRowInfo>& List = SSimCruisePauseMenu::Rows(P);
			for (int32 i = 0; i < List.Num(); ++i)
			{
				if (i > 0) Pad.Press(EKeys::Gamepad_DPad_Down);
				const FString Row = List[i].Title;
				TestTrue(FString::Printf(TEXT("%s: the pad reaches %s (on %s)"), *SSimCruisePauseMenu::PageName(P), *Row, *Name(Pad.Focus())), Pad.On(Menu->GetRowWidget(P, i)));
				const int32 Before = Actions.Num();
				if (List[i].Kind == SSimCruisePauseMenu::EKind::Slider || List[i].Kind == SSimCruisePauseMenu::EKind::Segmented)
				{
					Pad.Press(EKeys::Gamepad_DPad_Right);
					Pad.Press(EKeys::Gamepad_DPad_Left);
					TestTrue(FString::Printf(TEXT("%s changes with D-pad Right and Left"), *Row), Actions.Num() == Before + 2 && Actions.Last().Key == List[i].Row && Actions.Last().Value == -1);
				}
				else if (List[i].Row != ESimCruiseMenuRow::Continue)
				{
					Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
					TestTrue(FString::Printf(TEXT("A uses %s"), *Row), Actions.Num() == Before + 1 && Actions.Last().Key == List[i].Row && Actions.Last().Value == 0);
				}
				TestTrue(FString::Printf(TEXT("the pad is still on %s after using it"), *Row), Pad.On(Menu->GetRowWidget(P, i)));
			}
			Pad.Press(EKeys::Gamepad_DPad_Down);
			TestTrue(FString::Printf(TEXT("%s: Down from the last row comes round to the first"), *SSimCruisePauseMenu::PageName(P)), Pad.On(Menu->GetRowWidget(P, 0)));
		}

		// LB and RB from anywhere: a new page, on its first row.
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_RightShoulder);
		TestTrue(TEXT("RB from a row: Drive, on its first row"), Menu->GetPage() == 0 && Pad.On(Menu->GetRowWidget(0, 0)));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_LeftShoulder);
		TestTrue(TEXT("LB from the tab row: Settings, on its first row"), Menu->GetPage() == 3 && !Menu->IsOnTabs() && Pad.On(Menu->GetRowWidget(3, 0)));

		// The stick moves one row per push.
		Pad.Stick(EKeys::Gamepad_LeftY, -1.f);
		TestTrue(TEXT("one stick push down is one row"), Pad.On(Menu->GetRowWidget(3, 1)));
		Pad.Stick(EKeys::Gamepad_LeftY, 1.f);
		TestTrue(TEXT("one stick push up is one row"), Pad.On(Menu->GetRowWidget(3, 0)));

		// A mouse resting on a row while the menu changes under it does not take the pad's place.
		const TSharedPtr<SWidget> Resting = Menu->GetRowWidget(3, 5);
		Resting->OnMouseEnter(Resting->GetCachedGeometry(), FPointerEvent(0, FVector2D(600.0, 700.0), FVector2D(600.0, 700.0), TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		TestTrue(TEXT("a still mouse does not move the selection"), Pad.On(Menu->GetRowWidget(3, 0)) && Menu->GetSelected() == 0);
		Resting->OnMouseEnter(Resting->GetCachedGeometry(), FPointerEvent(0, FVector2D(600.0, 700.0), FVector2D(590.0, 690.0), TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
		TestTrue(TEXT("a moving mouse does"), Menu->GetSelected() == 5 && Pad.On(Resting));

		// Lost focus comes back on the next frame.
		FSlateApplication::Get().ClearAllUserFocus(EFocusCause::Cleared);
		Pad.Tick();
		TestTrue(TEXT("lost focus comes back to the menu"), Pad.On(Menu->GetRowWidget(3, 5)));

		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("B closes the menu"), Closed, 1);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("B on the tab row closes the menu too"), Closed, 2);
		Pad.Press(EKeys::Gamepad_Special_Right);
		TestEqual(TEXT("Menu closes the menu"), Closed, 3);
		AddInfo(FString::Printf(TEXT("pause menu: %d presses routed by FSlateApplication, %d walked along the focus path"), Pad.Routed, Pad.Walked));
	}

	// ------------------------------------------------------------------ list menu (famous roads, a found place)
	{
		TArray<SSimCruiseListMenu::FItem> Items;
		for (const TCHAR* T : { TEXT("Pacific Coast Highway"), TEXT("Overseas Highway"), TEXT("Blue Ridge Parkway") })
		{
			SSimCruiseListMenu::FItem I;
			I.Title = T;
			Items.Add(I);
		}
		int32 Chosen = -1, Back = 0;
		TSharedRef<SSimCruiseListMenu> List = SNew(SSimCruiseListMenu).Title(TEXT("Famous roads")).Items(Items)
			.OnChoose_Lambda([&Chosen](int32 i) { Chosen = i; })
			.OnBack_Lambda([&Back]() { ++Back; });
		FPadScreen Pad(List);
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (i > 0) Pad.Press(EKeys::Gamepad_DPad_Down);
			TestTrue(FString::Printf(TEXT("list: the pad reaches %s (on %s)"), *Items[i].Title, *Name(Pad.Focus())), Pad.On(List->GetRowWidget(i)));
			Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
			TestEqual(FString::Printf(TEXT("list: A chooses %s"), *Items[i].Title), Chosen, i);
		}
		Pad.Stick(EKeys::Gamepad_LeftY, -1.f);
		TestTrue(TEXT("list: the stick comes round to the first item"), Pad.On(List->GetRowWidget(0)));
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("list: B goes back"), Back, 1);
	}

	// ------------------------------------------------------------------ road atlas (its top buttons)
	{
		TArray<int32> Settings;
		int32 Closed = 0, Found = 0, Cars = 0, Gears = 0;
		TSharedRef<SSimCruiseRoadAtlas> Atlas = SNew(SSimCruiseRoadAtlas)
			.OnSetting_Lambda([&Settings](int32 i) { Settings.Add(i); })
			.OnFindPlace_Lambda([&Found]() { ++Found; })
			.OnCycleCar_Lambda([&Cars]() { ++Cars; })
			.OnToggleTransmission_Lambda([&Gears]() { ++Gears; })
			.OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Atlas);
		TArray<TSharedPtr<SWidget>> All;
		for (const TArray<TSharedPtr<SWidget>>& Row : Atlas->GetPadRows()) All.Append(Row);
		TestEqual(TEXT("atlas: 23 buttons on the top panel"), All.Num(), 23);
		TestTrue(TEXT("atlas: opens on the map"), Pad.On(Atlas));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TSet<SWidget*> Reached;
		for (int32 i = 0; i < All.Num(); ++i)
		{
			TestTrue(FString::Printf(TEXT("atlas: D-pad Right reaches %s (on %s)"), *Name(All[i]), *Name(Pad.Focus())), Pad.On(All[i]));
			Reached.Add(Pad.Focus().Get());
			Pad.Press(EKeys::Gamepad_DPad_Right);
		}
		TestEqual(TEXT("atlas: every top button reached"), Reached.Num(), All.Num());
		TestTrue(TEXT("atlas: Right past the last button comes round to the first"), Pad.On(All[0]));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		TestTrue(TEXT("atlas: Down goes to the second row"), Pad.On(Atlas->GetPadRows()[1][0]));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(TEXT("atlas: Up comes back to the top row"), Pad.On(Atlas->GetPadRows()[0][0]));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(TEXT("atlas: Up on the top row stays"), Pad.On(Atlas->GetPadRows()[0][0]));
		// A presses the button the pad is on: a setting, Find a place, the car and the gearbox.
		for (int32 i = 0; i < 4; ++i) Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestTrue(FString::Printf(TEXT("atlas: A presses a Your car setting (%d)"), Settings.Num() ? Settings.Last() : -1), Settings.Num() == 1 && Settings[0] >= 11);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses a Settings button"), Settings.Num(), 2);
		Pad.Press(EKeys::Gamepad_DPad_Up);
		for (int32 Guard = 0; !Pad.On(Atlas->GetPadRows()[1][0]) && Guard < 30; ++Guard) Pad.Press(EKeys::Gamepad_DPad_Left);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses Find a place"), Found, 1);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses the car button"), Cars, 1);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A presses the gearbox button"), Gears, 1);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestTrue(TEXT("atlas: B goes from the buttons back to the map"), Pad.On(Atlas) && Closed == 0);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("atlas: B on the map closes it"), Closed, 1);
		Pad.Press(EKeys::Gamepad_DPad_Left);
		TestTrue(TEXT("atlas: Left from the map reaches the last top-row button"), Pad.On(Atlas->GetPadRows()[0].Last()));
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestEqual(TEXT("atlas: A on Back to drive closes it"), Closed, 2);
	}

	// ------------------------------------------------------------------ Where to? (pad keyboard)
	{
		int32 Closed = 0;
		TSharedRef<SSimCruisePlaceSearch> Search = SNew(SSimCruisePlaceSearch)
			.OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Search);
		Search->FocusFirst(true);
		Pad.Tick();
		const TArray<TSharedPtr<SWidget>> Places = Search->GetChoiceButtons();
		TestTrue(TEXT("Where to?: places to choose"), Places.Num() > 0);
		TestTrue(FString::Printf(TEXT("Where to?: opens on the first place for a pad (on %s)"), *Name(Pad.Focus())), Places.Num() > 0 && Pad.On(Places[0]));
		for (int32 i = 1; i < Places.Num(); ++i)
		{
			Pad.Press(EKeys::Gamepad_DPad_Down);
			TestTrue(FString::Printf(TEXT("Where to?: Down reaches %s"), *Name(Places[i])), Pad.On(Places[i]));
		}
		Pad.Press(EKeys::Gamepad_DPad_Down);
		const TArray<TSharedPtr<SWidget>> Tools = Search->GetToolButtons();
		TestTrue(TEXT("Where to?: Down from the last place reaches Back to drive"), Tools.Num() == 3 && Pad.On(Tools[2]));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		TestTrue(TEXT("Where to?: Down on the last control stays (no trap)"), Pad.On(Tools[2]));
		for (int32 i = 0; i < Places.Num() + 1; ++i) Pad.Press(EKeys::Gamepad_DPad_Up);
		TestTrue(FString::Printf(TEXT("Where to?: Up reaches the search box (on %s)"), *Name(Pad.Focus())), Pad.On(Search->GetField()));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Where to?: Right reaches Search"), Pad.On(Tools[0]));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Where to?: Right reaches Surprise me"), Pad.On(Tools[1]));
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Where to?: Right comes round to the search box"), Pad.On(Search->GetField()));

		// A on the box: the pad keyboard; every key is reached; typing works.
		Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		TestTrue(TEXT("Where to?: A on the box opens the pad keyboard"), Search->IsPadKeyboardOpen());
		const TArray<TArray<TSharedPtr<SWidget>>>& Keys = Search->GetKeyRows();
		TestTrue(FString::Printf(TEXT("Where to?: the pad is on q (on %s)"), *Name(Pad.Focus())), Keys.Num() == 5 && Pad.On(Keys[1][0]));
		Pad.Press(EKeys::Gamepad_DPad_Up);
		int32 KeysReached = 0, KeysAll = 0;
		for (int32 R = 0; R < Keys.Num(); ++R)
		{
			for (int32 C = 0; C < Keys[R].Num(); ++C)
			{
				++KeysAll;
				if (Pad.On(Keys[R][C])) ++KeysReached;
				else AddError(FString::Printf(TEXT("Where to?: key %s not reached (on %s)"), *Name(Keys[R][C]), *Name(Pad.Focus())));
				if (C + 1 < Keys[R].Num()) Pad.Press(EKeys::Gamepad_DPad_Right);
			}
			if (R + 1 < Keys.Num())
			{
				Pad.Press(EKeys::Gamepad_DPad_Down);
				for (int32 Guard = 0; !Pad.On(Keys[R + 1][0]) && Guard < 12; ++Guard) Pad.Press(EKeys::Gamepad_DPad_Left);
			}
		}
		TestTrue(FString::Printf(TEXT("Where to?: every pad keyboard key reached (%d)"), KeysReached), KeysReached >= 44);
		// Type "mia": m is row 3, i row 1, a row 2.
		auto TypeAt = [&](int32 R, int32 C)
		{
			SimCruiseTheme::FocusPad(Keys[R][C]);
			Pad.Press(EKeys::Gamepad_FaceButton_Bottom);
		};
		Pad.Press(EKeys::Gamepad_FaceButton_Top);
		TestEqual(TEXT("Where to?: Y types a space"), Search->GetQuery(), FString(TEXT(" ")));
		Pad.Press(EKeys::Gamepad_FaceButton_Left);
		TypeAt(3, 6); TypeAt(1, 7); TypeAt(2, 0);
		TestEqual(TEXT("Where to?: A types the key the pad is on"), Search->GetQuery(), FString(TEXT("mia")));
		Pad.Press(EKeys::Gamepad_FaceButton_Left);
		TestEqual(TEXT("Where to?: X deletes"), Search->GetQuery(), FString(TEXT("mi")));
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestTrue(TEXT("Where to?: B closes the pad keyboard, back on the box, text kept"), !Search->IsPadKeyboardOpen() && Pad.On(Search->GetField()) && Search->GetQuery() == TEXT("mi") && Closed == 0);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestEqual(TEXT("Where to?: B closes the screen"), Closed, 1);
	}

	// ------------------------------------------------------------------ Tune car and Seat & view
	{
		int32 Closed = 0;
		TSharedRef<SSimCruiseTuningMenu> Tune = SNew(SSimCruiseTuningMenu).OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Tune);
		TestTrue(TEXT("Tune car has the pad's focus"), Pad.On(Tune));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_DPad_Right);
		TestTrue(TEXT("Tune car keeps the focus while rows change"), Pad.On(Tune) && Closed == 0);
		Pad.Press(EKeys::Gamepad_Special_Right);
		TestTrue(TEXT("Menu cancels Tune car"), Closed == 1 && Tune->GetResult() == SSimCruiseTuningMenu::ETuningResult::Cancelled);
	}
	{
		int32 Closed = 0;
		TSharedRef<SSimCruiseSeatMenu> Seat = SNew(SSimCruiseSeatMenu).OnClose_Lambda([&Closed]() { ++Closed; });
		FPadScreen Pad(Seat);
		TestTrue(TEXT("Seat & view has the pad's focus"), Pad.On(Seat));
		Pad.Press(EKeys::Gamepad_DPad_Down);
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		TestTrue(TEXT("B cancels Seat & view"), Closed == 1 && !Seat->WasSaved());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePadHintsTest, "SimCruise.UI.PadHints", SimCruisePadReachTest::Flags)
bool FSimCruisePadHintsTest::RunTest(const FString& Parameters)
{
	// The button hints follow the last device used, on every screen: a pad key sent through
	// FSlateApplication shows the pad's buttons, a keyboard key the keyboard's keys.
	if (!FSlateApplication::IsInitialized())
	{
		AddWarning(TEXT("No Slate application: the hints test needs one"));
		return true;
	}
	TSharedRef<SSimCruisePauseMenu> Menu = SNew(SSimCruisePauseMenu)
		.GetState_Lambda([]() { FSimCruiseMenuState S; S.bPad = false; return S; });
	FSlateApplication& App = FSlateApplication::Get();
	auto Tap = [&App](const FKey& Key)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
		App.ProcessKeyDownEvent(Event);
		App.ProcessKeyUpEvent(Event);
	};
	// Keys nothing in the editor is bound to.
	Tap(EKeys::ScrollLock);
	TestFalse(TEXT("after a keyboard key the pause menu shows the keyboard's keys"), Menu->ShowsPadHints());
	Tap(EKeys::Gamepad_LeftThumbstick);
	TestTrue(TEXT("after one pad button the pause menu shows the pad's buttons"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.2f));
	Tap(EKeys::ScrollLock);
	TestFalse(TEXT("a keyboard key switches back"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.2f));
	TestFalse(TEXT("a stick resting near the middle does not switch"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.9f));
	TestTrue(TEXT("a stick pushed switches to the pad"), Menu->ShowsPadHints());
	App.ProcessAnalogInputEvent(FAnalogInputEvent(EKeys::Gamepad_LeftY, FModifierKeysState(), 0, false, 0, 0, 0.f));
	Tap(EKeys::ScrollLock);
	return true;
}

#endif
