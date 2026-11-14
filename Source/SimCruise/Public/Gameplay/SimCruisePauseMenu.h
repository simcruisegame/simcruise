// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruiseUiTheme.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SWidgetSwitcher;

/**
 * The pause menu (Menu or Tab; docs/ui/ui-spec.md, mockups 1, 3, 4): four
 * pages, Drive, Your car, Seat & view and Settings, changed with LB and RB
 * (Q and E) from anywhere in the menu. Up and down choose a row, A (Enter)
 * chooses it, left and right change a value, B (Esc) or Menu goes back to the
 * drive. Up from the first row goes onto the tab row: left and right move along
 * it, A or Down opens that page. Every page change lands on the page's first
 * row. The rows only ask: the player controller does each action
 * (ASimCruisePlayerController::MenuAction). The pad's place is Slate's focus:
 * the selected row or tab has it, so a mouse resting on the menu cannot move it.
 */
enum class ESimCruiseMenuRow : uint8
{
	// Drive
	Continue, WhereTo, PlanRoute, FamousRoads, Traffic, Atlas,
	// Your car
	Car, Transmission, Tune, Lights,
	// Seat & view
	Lens, HeadMotion, ScreenSize, EyeDistance, Seat,
	// Settings
	Sound, HudLevel, SpeedUnits, Rumble, StickSpeed, StickResponse, StickDeadZone, Trigger,
	// Not a row: X on the Drive page while a route is on.
	EndRoute,
	Count,
};

/** What a row shows now. */
struct FSimCruiseMenuValue
{
	FString Text;
	FString Unit;
	/** Slider rows: 0..1 along the track. */
	float Fraction = 0.f;
	/** Slider rows: where the stock value is, < 0 for none. */
	float Stock = -1.f;
	/** Segmented rows: the chosen option. */
	int32 Chosen = 0;
	/** A row whose feature is not in this build yet (it says so and does nothing). */
	bool bUnavailable = false;
};

/** The state the menu shows, asked for every frame. */
struct FSimCruiseMenuState
{
	FString Place;
	FString Where;
	TMap<ESimCruiseMenuRow, FSimCruiseMenuValue> Values;
	bool bRoute = false;
	FString RouteTitle;
	FString RouteSummary;
	/** The route's legs and stops in world metres for the card's small map, and the car. */
	TArray<TArray<FVector2D>> RouteLegs;
	TArray<FVector2D> RouteStops;
	FVector2D CarM = FVector2D::ZeroVector;
	bool bPad = true;
};

DECLARE_DELEGATE_RetVal(FSimCruiseMenuState, FSimCruiseGetMenuState);
/** A row was chosen (Dir 0) or changed (Dir -1, +1). */
DECLARE_DELEGATE_TwoParams(FSimCruiseOnMenuAction, ESimCruiseMenuRow, int32);
DECLARE_DELEGATE_OneParam(FSimCruiseOnMenuPage, int32);

class SIMCRUISE_API SSimCruisePauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruisePauseMenu) : _Page(0), _Row(0) {}
		SLATE_EVENT(FSimCruiseGetMenuState, GetState)
		SLATE_EVENT(FSimCruiseOnMenuAction, OnAction)
		SLATE_EVENT(FSimCruiseOnMenuPage, OnPage)
		SLATE_EVENT(FSimpleDelegate, OnClose)
		SLATE_ARGUMENT(int32, Page)
		/** The row the page opens on (coming back from a screen this menu opened). */
		SLATE_ARGUMENT(int32, Row)
	SLATE_END_ARGS()

	enum class EKind : uint8 { Action, Value, Slider, Segmented };
	struct FRowInfo
	{
		ESimCruiseMenuRow Row;
		EKind Kind;
		SimCruiseTheme::EIcon Icon;
		FString Title;
		FString Note;
		/** One plain sentence for the card on the right. */
		FString Help;
	};
	static constexpr int32 NumPages = 4;
	static const TArray<FRowInfo>& Rows(int32 Page);
	static FString PageName(int32 Page);

	void Construct(const FArguments& InArgs);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Pad navigation as the keys do it (tests and scripts). */
	void Navigate(SimCruiseTheme::ENav Nav);
	int32 GetPage() const { return Page; }
	int32 GetSelected() const { return Selected[Page]; }
	ESimCruiseMenuRow GetSelectedRow() const { return Rows(Page)[Selected[Page]].Row; }
	/** Open a page on its first row (LB, RB, a tab). */
	void SetPage(int32 InPage);
	const FSimCruiseMenuState& GetStateNow() const { return State; }
	/** Is the pad on the tab row (and on which tab)? */
	bool IsOnTabs() const { return bTabs; }
	/** Has the tab bar the pad's focus (SimCruise.UI.PadNavigation asks this)? */
	bool IsTabBarFocused() const { return bTabs; }
	int32 GetTabCursor() const { return TabCursor; }
	/** The widgets that take the pad's focus (tests). */
	TSharedPtr<SWidget> GetTabWidget(int32 InPage) const { return InPage >= 0 && InPage < NumPages ? TabWidgets[InPage] : nullptr; }
	TSharedPtr<SWidget> GetRowWidget(int32 InPage, int32 Index) const { return InPage >= 0 && InPage < NumPages && RowWidgets[InPage].IsValidIndex(Index) ? RowWidgets[InPage][Index] : nullptr; }
	/** The widget the pad is on now. */
	TSharedPtr<SWidget> GetFocusTarget() const;

private:
	TSharedRef<SWidget> BuildPage(int32 InPage);
	TSharedRef<SWidget> BuildTabs();
	TSharedRef<SWidget> BuildRouteCard();
	TSharedRef<SWidget> BuildHelpCard();
	TSharedRef<SWidget> BuildHints();
	FSimCruiseMenuValue ValueOf(ESimCruiseMenuRow Row) const;
	void Choose(int32 Dir);
	/** Move Slate's focus to the selected row or tab (again next frame if it is not on screen yet). */
	void SyncFocus();

	FSimCruiseGetMenuState GetState;
	FSimCruiseOnMenuAction OnAction;
	FSimCruiseOnMenuPage OnPage;
	FSimpleDelegate OnClose;
	FSimCruiseMenuState State;
	int32 Page = 0;
	int32 Selected[NumPages] = { 0, 0, 0, 0 };
	TSharedPtr<SWidgetSwitcher> Pages;
	TArray<TSharedPtr<SWidget>> RowWidgets[NumPages];
	TSharedPtr<SWidget> TabWidgets[NumPages];
	bool bTabs = false;
	int32 TabCursor = 0;
	bool bFocusDirty = true;
	bool bSyncing = false;
	SimCruiseTheme::FStickRepeat Stick;
};

/** A short list screen in the same look: famous roads, or what to do with a found place. */
class SIMCRUISE_API SSimCruiseListMenu : public SCompoundWidget
{
public:
	struct FItem
	{
		SimCruiseTheme::EIcon Icon = SimCruiseTheme::EIcon::Star;
		FString Title;
		FString Note;
		FString Value;
		bool bCourse = false;
	};
	DECLARE_DELEGATE_OneParam(FOnChoose, int32);

	SLATE_BEGIN_ARGS(SSimCruiseListMenu) : _bPad(true) {}
		SLATE_ARGUMENT(FString, Title)
		SLATE_ARGUMENT(FString, Help)
		SLATE_ARGUMENT(TArray<FItem>, Items)
		SLATE_ARGUMENT(bool, bPad)
		SLATE_EVENT(FOnChoose, OnChoose)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	void Navigate(SimCruiseTheme::ENav Nav);
	int32 GetSelected() const { return Selected; }
	int32 NumItems() const { return Items.Num(); }
	TSharedPtr<SWidget> GetRowWidget(int32 Index) const { return RowWidgets.IsValidIndex(Index) ? RowWidgets[Index] : nullptr; }

private:
	void SyncFocus();

	TArray<FItem> Items;
	TArray<TSharedPtr<SWidget>> RowWidgets;
	int32 Selected = 0;
	bool bPad = true;
	bool bFocusDirty = true;
	bool bSyncing = false;
	SimCruiseTheme::FStickRepeat Stick;
	FOnChoose OnChoose;
	FSimpleDelegate OnBack;
};

/** The dark gradient behind a menu: over the whole screen, or only its left part (the world stays clear). */
SIMCRUISE_API TSharedRef<SWidget> SimCruiseMenuBackdrop(TAttribute<float> LeftFraction, TAttribute<bool> bBlur);
