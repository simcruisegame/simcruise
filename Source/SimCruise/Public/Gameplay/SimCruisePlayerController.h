// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/SimCruiseDriverShaping.h"
#include "Gameplay/SimCruiseHaptics.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "Gameplay/SimCruiseWheelDevice.h"
#include "SimCruisePlayerController.generated.h"

class USimCruiseInputConfig;
class SSimCruisePlaceSearch;
class SSimCruiseRoadAtlas;
class SWidget;
struct FSimCruisePlace;
struct FSimCruiseAtlasView;
class ASimCruiseHUD;
class FSimCruiseInputTapeDriver;
class ASimCruisePlayerCameraManager;
class SSimCruisePauseMenu;
class SSimCruiseRoutePlanner;
struct FSimCruiseMenuState;
enum class ESimCruiseMenuRow : uint8;

/**
 * The driver. Reads the Enhanced Input actions built in code, shapes them into
 * pedal positions and a rack angle (FSimCruiseDriverShaper), hands them to the
 * possessed ISimCruiseVehicleControl pawn every frame, and drives the pad's
 * rumble from the tyres' state. Discrete controls map to Godot's: C/Y camera,
 * F2/L3 HUD, T/B gearbox, E,Q/RB,X shifts, R/View recover, Tab/Menu atlas.
 */
UCLASS()
class SIMCRUISE_API ASimCruisePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASimCruisePlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	bool HasVehicle() const { return GetVehicle() != nullptr; }
	ISimCruiseVehicleControl* GetVehicle() const;
	const FSimCruiseDriveTelemetry& GetTelemetry() const { return Telemetry; }
	const FSimCruiseVehicleSetup& GetCachedVehicleSetup() const { return Setup; }
	const FSimCruiseDriverCommand& GetLastCommand() const { return LastCommand; }
	const FSimCruiseDriverShaper& GetShaper() const { return Shaper; }
	const FSimCruiseRumble& GetLastRumble() const { return LastRumble; }
	/** Pad record and replay (-SimCruiseRecordInput, F9, -SimCruiseReplayInput=FILE). */
	const FSimCruiseInputTapeDriver* GetInputTape() const { return InputTape.Get(); }
	bool IsAbsActive() const { return Haptics.IsAbsActive(); }
	bool IsUsingGamepad() const { return bUsingGamepad; }
	USimCruiseInputConfig* GetInputConfig() const { return InputConfig; }
	ESimCruiseRumbleBackend GetRumbleBackend() const { return RumbleBackend; }

	void ShowMessage(const FString& Text, float Seconds = 3.f);

	/** Scripted end-to-end check of the input routing, like Godot's
	 *  checks/PadSteer.gd: injects stick, key and trigger events through the
	 *  real Enhanced Input stack and logs SIMCRUISE SHELL SELF-TEST PASSED/FAILED.
	 *  With Quit=1 the game exits afterwards: exit code 0 when it passes,
	 *  1 (a forced exit after the log is flushed) when it fails. */
	UFUNCTION(Exec)
	void SimCruiseShellSelfTest(int32 Quit = 0);

	/** Scripted drive for documentation captures: accelerate, corner, brake,
	 *  and screenshot each view and HUD level into Saved/Screenshots. */
	UFUNCTION(Exec)
	void SimCruiseShellCapture(int32 Quit = 0);

	/** The place search (/ key; X in the atlas): the game pauses while it is open. */
	void OpenPlaceSearch(bool bGamepad);
	/** The road atlas (Tab, Menu). */
	void OpenRoadAtlas();
	/** Close the search or the atlas and give the drive back. */
	void CloseOverlays();
	bool IsOverlayOpen() const { return OverlayWidget.IsValid(); }
	TSharedPtr<SSimCruiseRoadAtlas> GetRoadAtlas() const { return AtlasPanel; }
	TSharedPtr<SSimCruisePlaceSearch> GetPlaceSearch() const { return SearchPanel; }

	/** Simulated key and axis events through the real Enhanced Input stack (scripts and captures). */
	void InjectKey(const FKey& Key, bool bPressed);
	void InjectAxis(const FKey& Key, float Value);

private:
	FSimCruiseRawControls ReadRawControls() const;
	void UpdateRumble(float DeltaTime);
	void StopRumble();
	void TickSelfTest(float DeltaTime);
	void TickCapture(float DeltaTime);
	ASimCruiseHUD* GetSimHUD() const;
	void ShowOverlay(const TSharedRef<SWidget>& Widget, const TSharedPtr<SWidget>& Focus, bool bPause = true);
	void ChoosePlace(const FSimCruisePlace& Place);
	/** The atlas's cockpit settings: 0 sound, 1 HUD level, 2 head motion, 3 rumble strength. */
	void ChangeSetting(int32 Index);
	bool SurpriseMe();
	FSimCruiseAtlasView AtlasView() const;
	ASimCruisePlayerCameraManager* GetSimCamera() const;

public:
	void OnToggleTransmission();
private:
	void OnShiftUp();
	void OnShiftDown();
	void OnStartEngine();
	void OnCycleCamera();
	void OnCycleHud();
	void OnToggleAtlas();
	void OnFindPlace();
	void OnRecover();
	void OnToggleSound();
	/** D-pad Right / F4: the tuning menu, standing still. */
	void OnTuning();

public:
	/** Open the tuning menu for the car being driven (tests call it directly). */
	void OpenTuning();
	TSharedPtr<class SSimCruiseTuningMenu> GetTuningMenu() const { return TuningPanel; }
	/** Open the seat and view menu over the live driver's view. */
	void OpenSeatMenu();
	TSharedPtr<class SSimCruiseSeatMenu> GetSeatMenu() const { return SeatPanel; }

	/** The pause menu (Menu, Tab) and the GPS screens: SimCruisePlayerControllerMenu.cpp. */
	void OpenPauseMenu(int32 Page = 0);
	void OpenRoutePlanner();
	void OpenFamousRoads();
	void OpenWhereTo();
	TSharedPtr<SSimCruisePauseMenu> GetPauseMenu() const { return PausePanel; }
	TSharedPtr<SSimCruiseRoutePlanner> GetRoutePlanner() const { return PlannerPanel; }
	/** What a pause menu row does (Dir 0 choose, -1 / +1 change, 2 back to stock). */
	void MenuAction(ESimCruiseMenuRow Row, int32 Dir);
	FSimCruiseMenuState MenuState() const;

	/** Engine sound on (M toggles it). */
	bool IsSoundOn() const { return bSoundOn; }

protected:
	void OnToggleLights();
	void OnCycleLens();
	void OnToggleStats();

	UPROPERTY(Transient)
	TObjectPtr<USimCruiseInputConfig> InputConfig;

	FSimCruiseDriverShaper Shaper;
	TSharedPtr<FSimCruiseInputTapeDriver> InputTape;
	FSimCruiseHapticsModel Haptics;
	/** A USB racing wheel, read each frame (nothing happens without one). */
	FSimCruiseWheelDevice WheelDevice;
	/** Read the wheel into Raw and drive its force feedback from the last frame's telemetry. */
	void UpdateWheel(FSimCruiseRawControls& Raw, float DeltaTime);
	bool bWheelSeen = false;
	FSimCruiseVehicleSetup Setup;
	FSimCruiseDriveTelemetry Telemetry;
	FSimCruiseDriverCommand LastCommand;
	FSimCruiseRumble LastRumble;
	ESimCruiseRumbleBackend RumbleBackend = ESimCruiseRumbleBackend::XInput;
	uint64 RumbleHandles[4] = {0, 0, 0, 0};
	bool bUsingGamepad = false;
	bool bSoundOn = true;
	bool bLightsOn = false;
	bool bStatsShown = false;
	bool bPreferencesApplied = false;
	TSharedPtr<SWidget> OverlayWidget;
	TSharedPtr<SSimCruisePlaceSearch> SearchPanel;
	TSharedPtr<SSimCruiseRoadAtlas> AtlasPanel;
	TSharedPtr<class SSimCruiseTuningMenu> TuningPanel;
	TSharedPtr<class SSimCruiseSeatMenu> SeatPanel;
	/** A tuning saved on the move, waiting for the stop. */
	bool bTuningWaits = false;
	bool bMenuHintShown = false;
	/** The car whose seat the camera has (reloaded when the car changes). */
	FString SeatCarId;
	TSharedPtr<SSimCruisePauseMenu> PausePanel;
	TSharedPtr<SSimCruiseRoutePlanner> PlannerPanel;
	/** Famous roads, and what to do with a found place. */
	TSharedPtr<SWidget> ListPanel;
	int32 MenuPage = 0;
	FString PendingMessage;
	float PendingMessageSeconds = 0.f;

	// Self-test state.
	int32 SelfTestStage = -1;
	float SelfTestClock = 0.f;
	bool bSelfTestQuit = false;
	FString SelfTestFailure;
	int32 SelfTestViewBefore = 0;
	int32 SelfTestHudBefore = 0;

	// Capture script state.
	float CaptureClock = -1.f;
	int32 CaptureShot = 0;
	int32 CaptureTraceTick = -1;
	bool bCaptureQuit = false;
	bool bCaptureParkingBrake = false;
};
