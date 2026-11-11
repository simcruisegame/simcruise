// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/SimCruiseDriverShaping.h"
#include "Gameplay/SimCruiseHaptics.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "SimCruisePlayerController.generated.h"

class USimCruiseInputConfig;
class ASimCruiseHUD;
class ASimCruisePlayerCameraManager;

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
	ASimCruisePlayerCameraManager* GetSimCamera() const;

	void OnToggleTransmission();
	void OnShiftUp();
	void OnShiftDown();
	void OnStartEngine();
	void OnCycleCamera();
	void OnCycleHud();
	void OnToggleAtlas();
	void OnFindPlace();
	void OnRecover();
	void OnToggleSound();

public:
	/** Engine sound on (M toggles it). */
	bool IsSoundOn() const { return bSoundOn; }

protected:
	void OnToggleLights();
	void OnCycleLens();
	void OnToggleStats();

	UPROPERTY(Transient)
	TObjectPtr<USimCruiseInputConfig> InputConfig;

	FSimCruiseDriverShaper Shaper;
	FSimCruiseHapticsModel Haptics;
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
