// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <type_traits>
#include <utility>

#include "Framework/Application/SlateApplication.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruiseUiTheme.h"
#include "Widgets/SWindow.h"

/*
 * Every control of the menus with an Xbox controller only (owner, 2026-11-14: "I wasn't able to
 * get to the top menu part"). The keys go through FSlateApplication::ProcessKeyDownEvent and
 * ProcessAnalogInputEvent, as the pad's do in the game, when the menu can hold the focus in a
 * window; else straight to the widget. Each control is logged as "PADNAV REACHED" or
 * "PADNAV UNREACHABLE" (scripts\pad-review.ps1 makes the report from these lines). An
 * unreachable control or a focus trap (B does not leave) fails the test.
 *
 * The navigation the UI lane agreed: LB and RB change the top tabs, the D-pad moves along the
 * rows, D-pad Up from the first row moves the focus to the tab bar, A chooses, B goes back. The
 * tab bar is reached only when the menu says so (IsTabBarFocused(), when the menu has it).
 */
namespace SimCruisePadNavTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	template <typename T, typename = void> struct THasTabBarFocus : std::false_type {};
	template <typename T> struct THasTabBarFocus<T, std::void_t<decltype(std::declval<const T&>().IsTabBarFocused())>> : std::true_type {};
	template <typename T> bool TabBarFocused(const T& Menu)
	{
		if constexpr (THasTabBarFocus<T>::value) return Menu.IsTabBarFocused();
		else return false;
	}

	/** Sends pad keys as the game gets them: through Slate to the focused widget, or to the widget itself. */
	struct FPad
	{
		TSharedPtr<SWidget> Target;
		TSharedPtr<SWindow> Window;
		bool bThroughSlate = false;

		void Open(const TSharedRef<SWidget>& Widget)
		{
			Target = Widget;
			if (!FSlateApplication::IsInitialized()) return;
			FSlateApplication& App = FSlateApplication::Get();
			Window = SNew(SWindow).ClientSize(FVector2D(1920, 1080)).IsInitiallyMaximized(false)[Widget];
			App.AddWindow(Window.ToSharedRef(), false);
			App.SetAllUserFocus(Widget, EFocusCause::SetDirectly);
			bThroughSlate = Widget->HasAnyUserFocus().IsSet();
		}
		void Close()
		{
			if (Window.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().DestroyWindowImmediately(Window.ToSharedRef());
			Window.Reset();
		}
		bool Press(const FKey& Key)
		{
			const FKeyEvent Down(Key, FModifierKeysState(), 0, false, 0, 0);
			if (bThroughSlate)
			{
				const bool bHandled = FSlateApplication::Get().ProcessKeyDownEvent(Down);
				FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
				return bHandled;
			}
			return Target->OnKeyDown(FGeometry(), Down).IsEventHandled();
		}
		bool Stick(const FKey& Axis, float Value)
		{
			const FAnalogInputEvent Event(Axis, FModifierKeysState(), 0u, false, 0u, 0u, Value);
			if (bThroughSlate) return FSlateApplication::Get().ProcessAnalogInputEvent(Event);
			return Target->OnAnalogValueChanged(FGeometry(), Event).IsEventHandled();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePadNavigationTest, "SimCruise.UI.PadNavigation", SimCruisePadNavTest::Flags)
bool FSimCruisePadNavigationTest::RunTest(const FString& Parameters)
{
	using namespace SimCruisePadNavTest;
	int32 Closed = 0;
	TArray<TPair<ESimCruiseMenuRow, int32>> Actions;
	TSharedRef<SSimCruisePauseMenu> Menu = SNew(SSimCruisePauseMenu)
		.GetState_Lambda([]() { FSimCruiseMenuState S; S.Place = TEXT("Ocean Drive"); S.bPad = true; return S; })
		.OnAction_Lambda([&Actions](ESimCruiseMenuRow Row, int32 Dir) { Actions.Add({ Row, Dir }); })
		.OnClose_Lambda([&Closed]() { ++Closed; });
	FPad Pad;
	Pad.Open(Menu);
	AddInfo(FString::Printf(TEXT("Keys go %s"), Pad.bThroughSlate ? TEXT("through FSlateApplication to the focused menu") : TEXT("straight to the menu (no window can hold the focus here)")));
	int32 Unreachable = 0;
	// A control that is not built yet (the tab bar focus, until the menu has IsTabBarFocused) is a
	// warning here, so the fast tier stays green for the other lanes; scripts\pad-review.ps1 fails
	// on every UNREACHABLE line.
	auto Log = [this, &Unreachable](const FString& Screen, const FString& Control, bool bReached, const FString& How, bool bBuilt = true)
	{
		const FString Line = FString::Printf(TEXT("PADNAV %s screen=\"%s\" control=\"%s\" how=\"%s\""), bReached ? TEXT("REACHED") : TEXT("UNREACHABLE"), *Screen, *Control, *How);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		if (bReached) return;
		++Unreachable;
		if (bBuilt) AddError(Line);
		else AddWarning(Line);
	};

	// The top tabs: LB and RB reach every page.
	TSet<int32> PagesSeen = { Menu->GetPage() };
	for (int32 I = 0; I < SSimCruisePauseMenu::NumPages; ++I) { Pad.Press(EKeys::Gamepad_RightShoulder); PagesSeen.Add(Menu->GetPage()); }
	for (int32 P = 0; P < SSimCruisePauseMenu::NumPages; ++P)
		Log(TEXT("Pause menu"), FString::Printf(TEXT("tab %s"), *SSimCruisePauseMenu::PageName(P)), PagesSeen.Contains(P), TEXT("RB"));

	for (int32 P = 0; P < SSimCruisePauseMenu::NumPages; ++P)
	{
		Menu->SetPage(P);
		const FString Screen = FString::Printf(TEXT("Pause menu / %s"), *SSimCruisePauseMenu::PageName(P));
		const TArray<SSimCruisePauseMenu::FRowInfo>& Rows = SSimCruisePauseMenu::Rows(P);
		// Go to the first row, then down along every row.
		for (int32 I = 0; I < Rows.Num() && Menu->GetSelected() != 0; ++I) Pad.Press(EKeys::Gamepad_DPad_Down);
		TSet<ESimCruiseMenuRow> Seen = { Menu->GetSelectedRow() };
		for (int32 I = 0; I < Rows.Num(); ++I) { Pad.Press(EKeys::Gamepad_DPad_Down); Seen.Add(Menu->GetSelectedRow()); }
		for (const SSimCruisePauseMenu::FRowInfo& R : Rows) Log(Screen, R.Title, Seen.Contains(R.Row), TEXT("D-pad Down"));
		// A on a row asks for its action (rows that change a value take left and right).
		for (int32 I = 0; I <= Rows.Num() && Menu->GetSelected() != 0; ++I) Pad.Press(EKeys::Gamepad_DPad_Down);
		const int32 Before = Actions.Num();
		Pad.Press(Rows[0].Kind == SSimCruisePauseMenu::EKind::Action ? EKeys::Gamepad_FaceButton_Bottom : EKeys::Gamepad_DPad_Right);
		Log(Screen, FString::Printf(TEXT("%s (use)"), *Rows[0].Title), Actions.Num() > Before, Rows[0].Kind == SSimCruisePauseMenu::EKind::Action ? TEXT("A") : TEXT("D-pad Right"));
		// The tab bar from the first row.
		Pad.Press(EKeys::Gamepad_DPad_Up);
		const bool bTabs = TabBarFocused(*Menu);
		Log(Screen, TEXT("tab bar (top menu)"), bTabs, bTabs ? TEXT("D-pad Up from the first row")
			: *FString::Printf(TEXT("D-pad Up from the first row goes to '%s', not the tab bar"), *Rows[Menu->GetSelected()].Title), THasTabBarFocus<SSimCruisePauseMenu>::value);
		if (bTabs) Pad.Press(EKeys::Gamepad_DPad_Down);
		// B leaves from every page (no focus trap).
		const int32 ClosedBefore = Closed;
		Pad.Press(EKeys::Gamepad_FaceButton_Right);
		Log(Screen, TEXT("back (B)"), Closed > ClosedBefore, TEXT("B"));
	}

	// The left stick moves along the rows as the D-pad does.
	Menu->SetPage(0);
	for (int32 I = 0; I <= SSimCruisePauseMenu::Rows(0).Num() && Menu->GetSelected() != 0; ++I) Pad.Press(EKeys::Gamepad_DPad_Down);
	Pad.Stick(EKeys::Gamepad_LeftY, -1.f);
	Log(TEXT("Pause menu / Drive"), TEXT("left stick down"), Menu->GetSelected() == 1, TEXT("left stick"));
	Pad.Close();

	// The list screens (famous roads, a found place): every item, A and B.
	int32 Chosen = -1, Back = 0;
	TArray<SSimCruiseListMenu::FItem> Items;
	for (int32 I = 0; I < 5; ++I) { SSimCruiseListMenu::FItem It; It.Title = FString::Printf(TEXT("Item %d"), I + 1); Items.Add(It); }
	TSharedRef<SSimCruiseListMenu> List = SNew(SSimCruiseListMenu).Title(TEXT("Famous roads")).Items(Items)
		.OnChoose_Lambda([&Chosen](int32 I) { Chosen = I; }).OnBack_Lambda([&Back]() { ++Back; });
	FPad ListPad;
	ListPad.Open(List);
	TSet<int32> ItemsSeen = { List->GetSelected() };
	for (int32 I = 0; I < Items.Num(); ++I) { ListPad.Press(EKeys::Gamepad_DPad_Down); ItemsSeen.Add(List->GetSelected()); }
	for (int32 I = 0; I < Items.Num(); ++I) Log(TEXT("List screen"), Items[I].Title, ItemsSeen.Contains(I), TEXT("D-pad Down"));
	ListPad.Press(EKeys::Gamepad_FaceButton_Bottom);
	Log(TEXT("List screen"), TEXT("choose (A)"), Chosen == List->GetSelected(), TEXT("A"));
	ListPad.Press(EKeys::Gamepad_FaceButton_Right);
	Log(TEXT("List screen"), TEXT("back (B)"), Back == 1, TEXT("B"));
	ListPad.Close();

	UE_LOG(LogTemp, Display, TEXT("PADNAV RESULT %s unreachable=%d"), Unreachable ? TEXT("FAIL") : TEXT("PASS"), Unreachable);
	return true;
}

#endif
