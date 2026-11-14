// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePadReview.h"

#include <type_traits>
#include <utility>

#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/SimCruiseAtlasUI.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruisePadReview, Log, All);

namespace
{
	/** The menu says when the pad is on its tab row (once the menu has IsTabBarFocused). */
	template <typename T, typename = void> struct THasTabBar : std::false_type {};
	template <typename T> struct THasTabBar<T, std::void_t<decltype(std::declval<const T&>().IsTabBarFocused())>> : std::true_type {};
	template <typename T> bool TabBarFocused(const T& Menu)
	{
		if constexpr (THasTabBar<T>::value) return Menu.IsTabBarFocused();
		else return false;
	}

	/** The walk starts this long after the car is placed (the loading screen has gone). */
	constexpr double AfterPlacedS = 5.0;
	constexpr double GiveUpAfterS = 300.0;
}

ASimCruisePadReview::ASimCruisePadReview()
{
	PrimaryActorTick.bCanEverTick = true;
	// The menus pause the game; the walk goes on.
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

void ASimCruisePadReview::BeginPlay()
{
	Super::BeginPlay();
	if (!FParse::Value(FCommandLine::Get(), TEXT("SimCruisePadReview="), Dir)) Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PadReview"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	StartS = FPlatformTime::Seconds();
	UE_LOG(LogSimCruisePadReview, Display, TEXT("Pad review: menus with pad buttons through Slate, frames -> %s"), *Dir);
}

ASimCruisePlayerController* ASimCruisePadReview::Player() const
{
	return GetWorld() ? Cast<ASimCruisePlayerController>(GetWorld()->GetFirstPlayerController()) : nullptr;
}

void ASimCruisePadReview::Press(const FKey& Key)
{
	if (!FSlateApplication::IsInitialized()) return;
	FSlateApplication& App = FSlateApplication::Get();
	const bool bHandled = App.ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	App.ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
	LogFocus(FString::Printf(TEXT("%s handled=%d"), *Key.ToString(), bHandled ? 1 : 0));
}

void ASimCruisePadReview::Stick(const FKey& Axis, float Value)
{
	if (!FSlateApplication::IsInitialized()) return;
	FSlateApplication& App = FSlateApplication::Get();
	const bool bHandled = App.ProcessAnalogInputEvent(FAnalogInputEvent(Axis, FModifierKeysState(), 0u, false, 0u, 0u, Value));
	LogFocus(FString::Printf(TEXT("%s=%.1f handled=%d"), *Axis.ToString(), Value, bHandled ? 1 : 0));
}

void ASimCruisePadReview::Shot(const FString& Name)
{
	// With the UI: the menus are what this test looks at.
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, Name + TEXT(".png")), true, false);
}

void ASimCruisePadReview::LogFocus(const FString& Step)
{
	TSharedPtr<SWidget> Focus = FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetUserFocusedWidget(0) : nullptr;
	FString Where = TEXT("-");
	if (ASimCruisePlayerController* PC = Player())
	{
		if (TSharedPtr<SSimCruisePauseMenu> Menu = PC->GetPauseMenu()) Where = FString::Printf(TEXT("page=%d row=%d"), Menu->GetPage(), Menu->GetSelected());
	}
	UE_LOG(LogSimCruisePadReview, Display, TEXT("PADWALK %s focus=%s tag=%s %s"), *Step,
		Focus.IsValid() ? *Focus->GetTypeAsString() : TEXT("none"), Focus.IsValid() ? *Focus->GetTag().ToString() : TEXT("-"), *Where);
}

void ASimCruisePadReview::Reach(const FString& Screen, const FString& Control, bool bReached, const FString& How)
{
	if (!bReached) ++Unreachable;
	UE_LOG(LogSimCruisePadReview, Display, TEXT("PADNAV %s screen=\"%s\" control=\"%s\" how=\"%s\""), bReached ? TEXT("REACHED") : TEXT("UNREACHABLE"), *Screen, *Control, *How);
}

void ASimCruisePadReview::Plan()
{
	auto Add = [this](const FString& Name, TFunction<void()> Do, float Wait = 0.25f) { Steps.Add({ Name, MoveTemp(Do), Wait }); };
	// Pause menu: every tab with RB, every row with the D-pad, the tab bar with D-pad Up, B.
	Add(TEXT("open pause menu"), [this]() { if (ASimCruisePlayerController* PC = Player()) PC->OpenPauseMenu(0); LogFocus(TEXT("opened")); }, 1.5f);
	for (int32 P = 0; P < SSimCruisePauseMenu::NumPages; ++P)
	{
		const FString Screen = FString::Printf(TEXT("Pause menu / %s"), *SSimCruisePauseMenu::PageName(P));
		if (P > 0)
		{
			Add(TEXT("RB"), [this]() { Press(EKeys::Gamepad_RightShoulder); }, 0.5f);
			Add(TEXT("tab check"), [this, P]()
			{
				TSharedPtr<SSimCruisePauseMenu> M = Player() ? Player()->GetPauseMenu() : nullptr;
				Reach(TEXT("Pause menu"), FString::Printf(TEXT("tab %s"), *SSimCruisePauseMenu::PageName(P)), M.IsValid() && M->GetPage() == P, TEXT("RB"));
			}, 0.f);
		}
		Add(TEXT("shot"), [this, P]() { Shot(FString::Printf(TEXT("pad-pause-%d"), P)); }, 0.6f);
		TSharedRef<TSet<int32>> Seen = MakeShared<TSet<int32>>();
		Add(TEXT("row start"), [this, Seen]() { if (TSharedPtr<SSimCruisePauseMenu> M = Player() ? Player()->GetPauseMenu() : nullptr) Seen->Add(M->GetSelected()); }, 0.f);
		const int32 N = SSimCruisePauseMenu::Rows(P).Num();
		for (int32 I = 0; I < N; ++I)
		{
			Add(TEXT("D-pad Down"), [this, Seen]()
			{
				Press(EKeys::Gamepad_DPad_Down);
				if (TSharedPtr<SSimCruisePauseMenu> M = Player() ? Player()->GetPauseMenu() : nullptr) Seen->Add(M->GetSelected());
			});
		}
		Add(TEXT("rows"), [this, Seen, P, Screen]()
		{
			const TArray<SSimCruisePauseMenu::FRowInfo>& Rows = SSimCruisePauseMenu::Rows(P);
			for (int32 I = 0; I < Rows.Num(); ++I) Reach(Screen, Rows[I].Title, Seen->Contains(I), TEXT("D-pad Down"));
		}, 0.f);
		// The tab bar: D-pad Up from the first row (the rows wrap, so a full lap ends on the first row).
		Add(TEXT("D-pad Up"), [this, Screen]()
		{
			TSharedPtr<SSimCruisePauseMenu> M = Player() ? Player()->GetPauseMenu() : nullptr;
			const bool bFromFirst = M.IsValid() && M->GetSelected() == 0;
			Press(EKeys::Gamepad_DPad_Up);
			TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetUserFocusedWidget(0);
			const bool bTabs = M.IsValid() && (TabBarFocused(*M) || (Focus.IsValid() && Focus->GetTag().ToString().StartsWith(TEXT("Tab."))));
			Reach(Screen, TEXT("tab bar (top menu)"), bTabs, bFromFirst ? TEXT("D-pad Up from the first row") : TEXT("D-pad Up (the D-pad did not reach the first row)"));
			if (bTabs) Press(EKeys::Gamepad_DPad_Down);
		}, 0.4f);
	}
	// The left stick moves along the rows too.
	TSharedRef<int32> RowBefore = MakeShared<int32>(-1);
	Add(TEXT("stick"), [this, RowBefore]()
	{
		if (TSharedPtr<SSimCruisePauseMenu> M = Player() ? Player()->GetPauseMenu() : nullptr) *RowBefore = M->GetSelected();
		Stick(EKeys::Gamepad_LeftY, -1.f);
	}, 0.4f);
	Add(TEXT("stick check"), [this, RowBefore]()
	{
		Stick(EKeys::Gamepad_LeftY, 0.f);
		TSharedPtr<SSimCruisePauseMenu> M = Player() ? Player()->GetPauseMenu() : nullptr;
		Reach(TEXT("Pause menu"), TEXT("left stick down"), M.IsValid() && M->GetSelected() != *RowBefore, TEXT("left stick"));
	}, 0.f);
	Add(TEXT("B"), [this]() { Press(EKeys::Gamepad_FaceButton_Right); }, 0.8f);
	Add(TEXT("B check"), [this]() { Reach(TEXT("Pause menu"), TEXT("back (B)"), Player() && !Player()->IsOverlayOpen(), TEXT("B")); }, 0.f);

	// "Where to?" and the road atlas: a frame before and after the D-pad (the script compares them).
	Add(TEXT("open where to"), [this]() { if (ASimCruisePlayerController* PC = Player()) PC->OpenWhereTo(); LogFocus(TEXT("where-to opened")); }, 2.f);
	Add(TEXT("shot"), [this]() { Shot(TEXT("pad-whereto-0")); }, 0.6f);
	Add(TEXT("D-pad Down"), [this]() { Press(EKeys::Gamepad_DPad_Down); });
	Add(TEXT("D-pad Down"), [this]() { Press(EKeys::Gamepad_DPad_Down); }, 0.6f);
	Add(TEXT("shot"), [this]() { Shot(TEXT("pad-whereto-1")); }, 0.6f);
	// The left stick (Slate's spatial navigation on this screen: its order may differ from the D-pad's; a warning only).
	Add(TEXT("stick"), [this]() { Stick(EKeys::Gamepad_LeftY, -1.f); }, 0.3f);
	Add(TEXT("stick release"), [this]() { Stick(EKeys::Gamepad_LeftY, 0.f); }, 0.6f);
	Add(TEXT("shot"), [this]() { Shot(TEXT("pad-whereto-2")); }, 0.6f);
	// B goes back one level at a time (a setting, then the screen): up to three presses to leave it.
	for (int32 B = 0; B < 3; ++B)
	{
		Add(TEXT("B"), [this]()
		{
			TSharedPtr<SWidget> S = Player() ? StaticCastSharedPtr<SWidget>(Player()->GetPlaceSearch()) : nullptr;
			if (S.IsValid() && Player()->IsOverlayOpen() && (S->HasUserFocus(0).IsSet() || S->HasFocusedDescendants())) Press(EKeys::Gamepad_FaceButton_Right);
		}, 0.8f);
	}
	Add(TEXT("B check"), [this]()
	{
		TSharedPtr<SWidget> S = Player() ? StaticCastSharedPtr<SWidget>(Player()->GetPlaceSearch()) : nullptr;
		const bool bLeft = !S.IsValid() || !(S->HasUserFocus(0).IsSet() || S->HasFocusedDescendants()) || !Player()->IsOverlayOpen();
		Reach(TEXT("Where to?"), TEXT("back (B)"), bLeft, TEXT("B leaves the screen"));
	}, 0.f);
	Add(TEXT("open atlas"), [this]() { if (ASimCruisePlayerController* PC = Player()) PC->OpenRoadAtlas(); LogFocus(TEXT("atlas opened")); }, 3.f);
	Add(TEXT("shot"), [this]() { Shot(TEXT("pad-atlas-0")); }, 0.6f);
	Add(TEXT("D-pad Down"), [this]() { Press(EKeys::Gamepad_DPad_Down); });
	Add(TEXT("D-pad Right"), [this]() { Press(EKeys::Gamepad_DPad_Right); });
	Add(TEXT("D-pad Right"), [this]() { Press(EKeys::Gamepad_DPad_Right); }, 0.8f);
	Add(TEXT("shot"), [this]() { Shot(TEXT("pad-atlas-1")); }, 0.6f);
	// The left stick (Slate's spatial navigation on this screen: its order may differ from the D-pad's; a warning only).
	Add(TEXT("stick"), [this]() { Stick(EKeys::Gamepad_LeftY, -1.f); }, 0.3f);
	Add(TEXT("stick release"), [this]() { Stick(EKeys::Gamepad_LeftY, 0.f); }, 0.6f);
	Add(TEXT("shot"), [this]() { Shot(TEXT("pad-atlas-2")); }, 0.6f);
	// B goes back one level at a time (a setting, then the screen): up to three presses to leave it.
	for (int32 B = 0; B < 3; ++B)
	{
		Add(TEXT("B"), [this]()
		{
			TSharedPtr<SWidget> S = Player() ? StaticCastSharedPtr<SWidget>(Player()->GetRoadAtlas()) : nullptr;
			if (S.IsValid() && Player()->IsOverlayOpen() && (S->HasUserFocus(0).IsSet() || S->HasFocusedDescendants())) Press(EKeys::Gamepad_FaceButton_Right);
		}, 0.8f);
	}
	Add(TEXT("B check"), [this]()
	{
		TSharedPtr<SWidget> S = Player() ? StaticCastSharedPtr<SWidget>(Player()->GetRoadAtlas()) : nullptr;
		const bool bLeft = !S.IsValid() || !(S->HasUserFocus(0).IsSet() || S->HasFocusedDescendants()) || !Player()->IsOverlayOpen();
		Reach(TEXT("Road atlas"), TEXT("back (B)"), bLeft, TEXT("B leaves the screen"));
	}, 0.f);
	Add(TEXT("end"), [this]()
	{
		UE_LOG(LogSimCruisePadReview, Display, TEXT("PADNAV RESULT %s unreachable=%d (in game)"), Unreachable ? TEXT("FAIL") : TEXT("PASS"), Unreachable);
		if (ASimCruisePlayerController* PC = Player()) PC->ConsoleCommand(TEXT("quit"));
	}, 1.f);
}

void ASimCruisePadReview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds() - StartS;
	if (Now > GiveUpAfterS)
	{
		UE_LOG(LogSimCruisePadReview, Display, TEXT("PADNAV RESULT FAIL unreachable=%d (in game, gave up after %.0f s at step %d)"), Unreachable, GiveUpAfterS, Next);
		if (ASimCruisePlayerController* PC = Player()) PC->ConsoleCommand(TEXT("quit"));
		SetActorTickEnabled(false);
		return;
	}
	if (!bPlanned)
	{
		const ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
		if (!Player() || !Mode || !Mode->IsDriveReady()) { PlacedAtS = -1.0; return; }
		if (PlacedAtS < 0.0) PlacedAtS = Now;
		if (Now - PlacedAtS < AfterPlacedS) return;
		Plan();
		bPlanned = true;
		NextAtS = Now;
	}
	if (Next >= Steps.Num() || Now < NextAtS) return;
	FStep& S = Steps[Next++];
	S.Do();
	NextAtS = Now + S.Wait;
}
