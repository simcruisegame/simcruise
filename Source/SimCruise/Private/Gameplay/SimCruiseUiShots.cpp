// Copyright SimCruise contributors. All Rights Reserved.

// -SimCruiseUiShots=<folder>: screenshots of the 2.0.2 screens for the docs and the
// release notes. It waits for the Ocean Drive start, starts a route, and takes the
// HUD with the route, the pause menu, the route planner with two stops, Tune car and
// Seat & view, then quits. It always ends: after 300 s it quits whatever happened.

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruiseRouteSubsystem.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseUiShots, Log, All);

namespace
{
	struct FUiShots
	{
		FString Dir;
		double Clock = 0.0;
		double StepAt = 0.0;
		int32 Step = 0;

		static FSimCruiseRouteStop Stop(const TCHAR* Title, double Lon, double Lat)
		{
			FSimCruiseRouteStop S;
			S.Title = Title;
			S.Lon = Lon;
			S.Lat = Lat;
			return S;
		}

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

		void Shot(const TCHAR* Name)
		{
			const FString Path = FPaths::Combine(Dir, Name);
			FScreenshotRequest::RequestScreenshot(Path, true, false);
			UE_LOG(LogSimCruiseUiShots, Display, TEXT("UISHOT %s"), *Path);
		}

		void Next(double Wait) { ++Step; StepAt = Clock + Wait; }

		bool Tick(float DeltaTime)
		{
			Clock += DeltaTime;
			if (Clock > 300.0)
			{
				UE_LOG(LogSimCruiseUiShots, Warning, TEXT("UISHOTS TIMED OUT at step %d"), Step);
				FPlatformMisc::RequestExit(false);
				return false;
			}
			if (Clock < StepAt) return true;
			ASimCruisePlayerController* PC = Controller();
			UWorld* World = PC ? PC->GetWorld() : nullptr;
			const ASimCruiseGameMode* Mode = World ? World->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
			USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(World);
			if (!PC || !Mode || !Gps) return true;
			switch (Step)
			{
			case 0: // the road is built and the car is on it
				if (Mode->IsDriveReady() && PC->HasVehicle()) Next(12.0);
				break;
			case 1:
				Gps->StartRoute({ Stop(TEXT("5th Street"), -80.1336, 25.7739), Stop(TEXT("Collins Park"), -80.1290, 25.7930) });
				Next(1.0);
				break;
			case 2: // wait for the route, then let the HUD fade it in
				if (Gps->HasRoute()) Next(3.0);
				else if (!Gps->IsBusy() && Clock - StepAt > 40.0) { UE_LOG(LogSimCruiseUiShots, Warning, TEXT("No route: %s"), *Gps->GetStatus()); Next(0.0); }
				break;
			case 3: Shot(TEXT("hud-route-ocean-drive.png")); Next(2.0); break;
			case 4: PC->OpenPauseMenu(0); Next(2.5); break;
			case 5: Shot(TEXT("pause-menu-drive.png")); Next(1.0); break;
			case 6:
				Gps->DraftStops = { Stop(TEXT("Lummus Park"), -80.1300, 25.7810), Stop(TEXT("Collins Park"), -80.1290, 25.7930) };
				PC->OpenRoutePlanner();
				Gps->RequestPreview(Gps->DraftStops);
				Next(10.0);
				break;
			case 7: Shot(TEXT("route-planner-2-stops.png")); Next(1.0); break;
			case 8: PC->OpenTuning(); Next(2.5); break;
			case 9: Shot(TEXT("tune-car.png")); Next(1.0); break;
			case 10: PC->OpenPauseMenu(2); Next(3.0); break;
			case 11: Shot(TEXT("seat-and-view.png")); Next(2.0); break;
			case 12:
				PC->CloseOverlays();
				UE_LOG(LogSimCruiseUiShots, Display, TEXT("UISHOTS DONE in %s"), *Dir);
				FPlatformMisc::RequestExit(false);
				return false;
			default: break;
			}
			return true;
		}
	};

	struct FUiShotsStarter
	{
		FUiShotsStarter()
		{
			FCoreDelegates::OnPostEngineInit.AddLambda([]()
			{
				FString Dir;
				if (!FParse::Value(FCommandLine::Get(), TEXT("SimCruiseUiShots="), Dir)) return;
				TSharedRef<FUiShots> Shots = MakeShared<FUiShots>();
				Shots->Dir = FPaths::ConvertRelativePathToFull(Dir);
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Shots](float Dt) { return Shots->Tick(Dt); }));
			});
		}
	};
	FUiShotsStarter Starter;
}
