// Copyright SimCruise contributors. All Rights Reserved.

// -SimCruisePadWalk=<folder>: drives the menus with pad buttons only, as a real Xbox pad
// does (key events through FSlateApplication, so Slate focus, bubbling and the game's own
// Enhanced Input all run), logs where the pad focus is after every press
// ("PADWALK <step> ...") and saves a frame of each screen as pad-*.png in the folder.
// It always ends: after 240 s it quits whatever happened.

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruisePadWalk, Log, All);

namespace
{
	struct FPadStep
	{
		const TCHAR* Name;
		/** The button pressed (none for a pause or a shot only). */
		FKey Key;
		/** Saved as <Shot>.png after the press settles; empty for none. */
		const TCHAR* Shot;
		float Wait;
	};

	struct FPadWalk
	{
		FString Dir;
		double Clock = 0.0;
		double StepAt = 0.0;
		int32 Step = -1;
		bool bReady = false;
		bool bShotTaken = false;
		TArray<FPadStep> Steps;

		ASimCruisePlayerController* Controller() const
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (World && World->IsGameWorld())
				{
					if (ASimCruisePlayerController* PC = Cast<ASimCruisePlayerController>(World->GetFirstPlayerController())) return PC;
				}
			}
			return nullptr;
		}

		static void Press(const FKey& Key)
		{
			// Down and up, user 0: what XInput sends Slate for a tap of the pad's first controller.
			FSlateApplication& App = FSlateApplication::Get();
			App.ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
			App.ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), 0, false, 0, 0));
		}

		static FString Describe(const TSharedPtr<SWidget>& Widget)
		{
			if (!Widget.IsValid()) return TEXT("none");
			const FName Tag = Widget->GetTag();
			return Tag.IsNone() ? Widget->GetTypeAsString() : FString::Printf(TEXT("%s[%s]"), *Widget->GetTypeAsString(), *Tag.ToString());
		}

		void Log(const TCHAR* Name, ASimCruisePlayerController* PC) const
		{
			const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetUserFocusedWidget(0);
			FString Menu = TEXT("menu=closed");
			if (const TSharedPtr<SSimCruisePauseMenu> Pause = PC->GetPauseMenu())
			{
				Menu = FString::Printf(TEXT("menu=open page=%d row=%d tabs=%d tab=%d"), Pause->GetPage(), Pause->GetSelected(), Pause->IsOnTabs() ? 1 : 0, Pause->GetTabCursor());
			}
			UE_LOG(LogSimCruisePadWalk, Display, TEXT("PADWALK %s %s overlay=%d focus=%s"), Name, *Menu, PC->IsOverlayOpen() ? 1 : 0, *Describe(Focus));
		}

		void Shot(const TCHAR* Name) const
		{
			const FString Path = FPaths::Combine(Dir, FString(Name) + TEXT(".png"));
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogSimCruisePadWalk, Display, TEXT("PADSHOT %s"), *Path);
		}

		bool Tick(float DeltaTime)
		{
			Clock += DeltaTime;
			if (Clock > 240.0)
			{
				UE_LOG(LogSimCruisePadWalk, Warning, TEXT("PADWALK TIMED OUT at step %d"), Step);
				FPlatformMisc::RequestExit(false);
				return false;
			}
			if (Clock < StepAt) return true;
			ASimCruisePlayerController* PC = Controller();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const ASimCruiseGameMode* Mode = World ? World->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
			if (!PC || !Mode) return true;
			if (!bReady)
			{
				// The road is built and the car is on it; then let it settle.
				if (Mode->IsDriveReady() && PC->HasVehicle()) { bReady = true; StepAt = Clock + 10.0; }
				return true;
			}
			// The shot of the step before, now that its frame has drawn; the next press waits a moment,
			// so the screenshot (taken at the end of a frame) is of this step and not the next.
			if (!bShotTaken && Steps.IsValidIndex(Step) && Steps[Step].Shot && *Steps[Step].Shot)
			{
				Shot(Steps[Step].Shot);
				bShotTaken = true;
				StepAt = Clock + 0.5;
				return true;
			}
			bShotTaken = false;
			++Step;
			if (!Steps.IsValidIndex(Step))
			{
				PC->CloseOverlays();
				UE_LOG(LogSimCruisePadWalk, Display, TEXT("PADWALK DONE in %s"), *Dir);
				FPlatformMisc::RequestExit(false);
				return false;
			}
			const FPadStep& S = Steps[Step];
			if (S.Key.IsValid()) Press(S.Key);
			Log(S.Name, PC);
			StepAt = Clock + S.Wait;
			return true;
		}
	};

	TArray<FPadStep> WalkSteps()
	{
		const float W = 0.6f;
		return {
			{ TEXT("open-menu"), EKeys::Gamepad_Special_Right, nullptr, 1.5f },
			{ TEXT("up-onto-tabs"), EKeys::Gamepad_DPad_Up, TEXT("pad-tab-bar"), 1.0f },
			{ TEXT("right-your-car"), EKeys::Gamepad_DPad_Right, TEXT("pad-tab-bar-your-car"), 1.0f },
			{ TEXT("a-open"), EKeys::Gamepad_FaceButton_Bottom, TEXT("pad-your-car-first-row"), 1.0f },
			{ TEXT("rb"), EKeys::Gamepad_RightShoulder, TEXT("pad-seat-and-view-first-row"), 1.0f },
			{ TEXT("b-close"), EKeys::Gamepad_FaceButton_Right, nullptr, 1.0f },
		};
	}

	struct FPadWalkStarter
	{
		FPadWalkStarter()
		{
			FCoreDelegates::OnPostEngineInit.AddLambda([]()
			{
				FString Dir;
				if (!FParse::Value(FCommandLine::Get(), TEXT("SimCruisePadWalk="), Dir)) return;
				TSharedRef<FPadWalk> Walk = MakeShared<FPadWalk>();
				Walk->Dir = FPaths::ConvertRelativePathToFull(Dir);
				Walk->Steps = WalkSteps();
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Walk](float Dt) { return Walk->Tick(Dt); }));
			});
		}
	};
	FPadWalkStarter Starter;
}
