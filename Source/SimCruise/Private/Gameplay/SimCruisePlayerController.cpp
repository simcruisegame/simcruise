// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/SimCruiseAtlasUI.h"
#include "Gameplay/SimCruiseSeatUI.h"
#include "Gameplay/SimCruiseTuningUI.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePreferences.h"
#include "SimCruiseGameInstance.h"
#include "World/SimCruiseWorldStreamer.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruiseInputConfig.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseShell, Log, All);

namespace
{
	float SimCruiseSettingValue(const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetFloat() : 1.f;
	}
}

static TAutoConsoleVariable<float> CVarRumbleStrength(
	TEXT("simcruise.Rumble.Strength"), 1.f,
	TEXT("Pad rumble strength 0..1 (0 turns it off). Rumble reads tyre slip, ABS and road texture; it adds no force."));

static TAutoConsoleVariable<float> CVarWheelMaxTorque(
	TEXT("simcruise.Wheel.MaxTorqueNm"), 2.5f,
	TEXT("The steering wheel's peak force-feedback torque, N m (2.2-2.5 for Logitech G29/G923, 5-25 for direct-drive bases): the rim torque that maps to full force."));

static TAutoConsoleVariable<int32> CVarWheelForceSign(
	TEXT("simcruise.Wheel.ForceSign"), 1,
	TEXT("1 or -1: flips the force-feedback direction for a wheel whose driver reports force the other way round (untested hardware)."));

static TAutoConsoleVariable<float> CVarWheelForce(
	TEXT("simcruise.Wheel.Force"), 0.6f,
	TEXT("USB steering wheel force-feedback strength 0..1 (0 turns it off)."));

ASimCruisePlayerController::ASimCruisePlayerController()
{
	PlayerCameraManagerClass = ASimCruisePlayerCameraManager::StaticClass();
	bShowMouseCursor = false;
	bForceFeedbackEnabled = true;
}

void ASimCruisePlayerController::BeginPlay()
{
	Super::BeginPlay();
	// The GameInput plugin is loaded for racing wheels only (Config/Windows/WindowsInput.ini turns
	// its pad processing off), so pads, and their rumble, stay on XInput.
	RumbleBackend = ESimCruiseRumbleBackend::XInput;
	if (IsLocalController())
	{
		SetInputMode(FInputModeGameOnly());
		if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseShellSelfTest")))
		{
			SimCruiseShellSelfTest(1);
		}
		if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseShellCapture")))
		{
			SimCruiseShellCapture(1);
		}
	}
}

void ASimCruisePlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseOverlays();
	StopRumble();
	Super::EndPlay(EndPlayReason);
}

void ASimCruisePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputConfig)
	{
		InputConfig = USimCruiseInputConfig::Create(this);
	}
	if (ULocalPlayer* Local = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Local))
		{
			Subsystem->AddMappingContext(InputConfig->DrivingContext, 0);
		}
	}
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(InputComponent);
	if (!Input)
	{
		UE_LOG(LogSimCruiseShell, Error, TEXT("SimCruise needs Enhanced Input (DefaultInputComponentClass)."));
		return;
	}
	Input->BindAction(InputConfig->ToggleTransmission, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnToggleTransmission);
	Input->BindAction(InputConfig->ShiftUp, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnShiftUp);
	Input->BindAction(InputConfig->ShiftDown, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnShiftDown);
	Input->BindAction(InputConfig->StartEngine, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnStartEngine);
	Input->BindAction(InputConfig->CycleCamera, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnCycleCamera);
	Input->BindAction(InputConfig->CycleHud, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnCycleHud);
	Input->BindAction(InputConfig->ToggleAtlas, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnToggleAtlas);
	Input->BindAction(InputConfig->Tuning, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnTuning);
	Input->BindAction(InputConfig->FindPlace, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnFindPlace);
	Input->BindAction(InputConfig->Recover, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnRecover);
	Input->BindActionValueLambda(InputConfig->MinimapIn, ETriggerEvent::Started, [this](const FInputActionValue&) { if (ASimCruiseHUD* Hud = GetSimHUD()) Hud->ZoomMinimap(-1); });
	Input->BindActionValueLambda(InputConfig->MinimapOut, ETriggerEvent::Started, [this](const FInputActionValue&) { if (ASimCruiseHUD* Hud = GetSimHUD()) Hud->ZoomMinimap(1); });
	Input->BindAction(InputConfig->ToggleSound, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnToggleSound);
	Input->BindAction(InputConfig->ToggleLights, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnToggleLights);
	Input->BindAction(InputConfig->CycleLens, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnCycleLens);
	Input->BindAction(InputConfig->ToggleStats, ETriggerEvent::Started, this, &ASimCruisePlayerController::OnToggleStats);
}

void ASimCruisePlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	Shaper.Reset();
	Haptics.Quiet();
	if (InPawn)
	{
		// The command written this frame must reach the vehicle before it steps.
		InPawn->AddTickPrerequisiteActor(this);
	}
	// The driver's own setup for this car (checks and captures drive the real car).
	if (ASimCruiseVehicle* Tunable = Cast<ASimCruiseVehicle>(InPawn))
	{
		if (!FParse::Param(FCommandLine::Get(), TEXT("SimCruiseRealCar")) && !FParse::Param(FCommandLine::Get(), TEXT("SimCruiseDriveTest"))
			&& !FParse::Param(FCommandLine::Get(), TEXT("SimCruiseDriveCapture")) && !GIsAutomationTesting)
		{
			Tunable->LoadSavedTuning();
		}
	}
	// Where the car's own menus are, once a session (the 2.0.0 playtest could not find them).
	if (!bMenuHintShown && Cast<ASimCruiseVehicle>(InPawn))
	{
		bMenuHintShown = true;
		ShowMessage(TEXT("Menu: map and settings  ·  D-pad Right: tune the car  ·  Menu, then D-pad to Seat & view"), 9.f);
	}
	if (ISimCruiseVehicleControl* Vehicle = GetVehicle())
	{
		Setup = Vehicle->GetVehicleSetup();
		// The remembered gearbox (DrivingPreferences.gd "manual").
		if (SimCruisePreferences::GetBool(TEXT("manual"), false))
		{
			Vehicle->SetManualTransmission(true);
		}
	}
	if (ASimCruisePlayerCameraManager* Camera = GetSimCamera())
	{
		Camera->SnapNextFrame();
	}
}

void ASimCruisePlayerController::OnUnPossess()
{
	// The released pawn no longer waits for this controller's command.
	if (APawn* Previous = GetPawn())
	{
		Previous->RemoveTickPrerequisiteActor(this);
	}
	StopRumble();
	Super::OnUnPossess();
}

ISimCruiseVehicleControl* ASimCruisePlayerController::GetVehicle() const
{
	return Cast<ISimCruiseVehicleControl>(GetPawn());
}

ASimCruiseHUD* ASimCruisePlayerController::GetSimHUD() const
{
	return Cast<ASimCruiseHUD>(MyHUD);
}

ASimCruisePlayerCameraManager* ASimCruisePlayerController::GetSimCamera() const
{
	return Cast<ASimCruisePlayerCameraManager>(PlayerCameraManager);
}

void ASimCruisePlayerController::ShowMessage(const FString& Text, float Seconds)
{
	UE_LOG(LogSimCruiseShell, Display, TEXT("%s"), *Text);
	if (ASimCruiseHUD* Hud = GetSimHUD())
	{
		Hud->ShowMessage(Text, Seconds);
	}
	else
	{
		// The HUD arrives after BeginPlay; the start hint waits for it.
		PendingMessage = Text;
		PendingMessageSeconds = Seconds;
	}
}

FSimCruiseRawControls ASimCruisePlayerController::ReadRawControls() const
{
	FSimCruiseRawControls Raw;
	const UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(PlayerInput);
	if (!Input || !InputConfig)
	{
		return Raw;
	}
	auto Axis = [Input](const UInputAction* Action) { return static_cast<double>(Input->GetActionValue(Action).Get<float>()); };
	auto Held = [Input](const UInputAction* Action) { return Input->GetActionValue(Action).Get<bool>(); };
	Raw.PadSteer = Axis(InputConfig->SteerPad);
	Raw.KeySteer = FMath::Clamp(Axis(InputConfig->SteerKey), -1.0, 1.0);
	Raw.PadThrottle = FMath::Clamp(Axis(InputConfig->ThrottlePad), 0.0, 1.0);
	Raw.PadBrake = FMath::Clamp(Axis(InputConfig->BrakePad), 0.0, 1.0);
	Raw.bKeyThrottle = Held(InputConfig->ThrottleKey);
	Raw.bKeyBrake = Held(InputConfig->BrakeKey);
	Raw.bFullServiceBrake = Held(InputConfig->FullBrake);
	// Parked with the seat menu open: held on the brakes.
	if (SeatPanel.IsValid())
	{
		Raw.bFullServiceBrake = true;
		Raw.bParkingBrake = true;
	}
	Raw.bParkingBrake = Held(InputConfig->ParkingBrake);
	Raw.bClutch = Held(InputConfig->Clutch);
	return Raw;
}

void ASimCruisePlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!bPreferencesApplied)
	{
		ASimCruiseHUD* Hud = GetSimHUD();
		ASimCruisePlayerCameraManager* Camera = GetSimCamera();
		if (Hud && Camera)
		{
			// The remembered HUD level, lens and sound (DrivingPreferences.gd).
			bPreferencesApplied = true;
			bSoundOn = SimCruisePreferences::GetBool(TEXT("sound"), bSoundOn);
			Hud->SetLevel(static_cast<ESimCruiseHudLevel>(SimCruisePreferences::GetInt(TEXT("hud_level"), static_cast<int32>(Hud->GetLevel()), 0, 2)));
			const int32 Lens = SimCruisePreferences::GetInt(TEXT("lens_v3"), static_cast<int32>(Camera->GetLens()), 0, 2);
			using FPref = TPair<const TCHAR*, const TCHAR*>;
			for (const FPref& Pref : { FPref(TEXT("head_motion"), TEXT("simcruise.Camera.HeadMotion")), FPref(TEXT("rumble"), TEXT("simcruise.Rumble.Strength")),
				FPref(TEXT("pad_steer_speed"), TEXT("simcruise.Pad.SteerSpeed")), FPref(TEXT("pad_steer_linearity"), TEXT("simcruise.Pad.SteerLinearity")),
				FPref(TEXT("pad_steer_dead_zone"), TEXT("simcruise.Pad.SteerDeadZone")), FPref(TEXT("trigger_gamma"), TEXT("simcruise.TriggerGamma")) })
			{
				// Stored as hundredths (the throttle curve's 1.8 as 180).
				const int32 Percent = SimCruisePreferences::GetInt(Pref.Key, -1, -1, 300);
				IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Pref.Value);
				if (Percent >= 0 && Var) Var->Set(Percent / 100.f, ECVF_SetByCode);
			}
			for (int32 i = 0; i < 3 && static_cast<int32>(Camera->GetLens()) != Lens; ++i) Camera->CycleLens();
			const int32 TrafficCars = SimCruisePreferences::GetInt(TEXT("traffic_cars"), -1, -1, 60);
			if (TrafficCars >= 0) if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Traffic.Cars"))) Var->Set(TrafficCars, ECVF_SetByCode);
			// The screen the true-FOV lens is computed for: diagonal in tenths of an inch, eye distance in cm.
			const int32 Tenths = SimCruisePreferences::GetInt(TEXT("screen_tenths_inch"), -1, -1, 1000);
			const int32 Distance = SimCruisePreferences::GetInt(TEXT("view_distance_cm"), -1, -1, 400);
			if (Tenths > 0) if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Lens.ScreenInches"))) Var->Set(Tenths / 10.f, ECVF_SetByCode);
			if (Distance > 0) if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Lens.ViewDistanceCm"))) Var->Set(static_cast<float>(Distance), ECVF_SetByCode);
		}
	}
	if (!PendingMessage.IsEmpty())
	{
		if (ASimCruiseHUD* Hud = GetSimHUD())
		{
			Hud->ShowMessage(PendingMessage, PendingMessageSeconds);
			PendingMessage.Reset();
		}
	}

	ISimCruiseVehicleControl* Vehicle = GetVehicle();
	if (!Vehicle)
	{
		StopRumble();
		TickSelfTest(DeltaTime);
		return;
	}
	Vehicle->GetVehicleTelemetry(Telemetry);
	// A setup saved on the move went on at the stop: take its steering ratio and limits.
	if (bTuningWaits)
	{
		const ASimCruiseVehicle* Tuned = Cast<ASimCruiseVehicle>(GetPawn());
		if (!Tuned || !Tuned->HasPendingTuning())
		{
			bTuningWaits = false;
			Setup = Vehicle->GetVehicleSetup();
			if (Tuned)
			{
				ShowMessage(TEXT("New setup on"), 2.f);
			}
		}
	}
	// Free look on the right stick (its click stays the gearbox switch): a small dead zone, then
	// the head turns toward it; nothing while a menu has the pad.
	if (ASimCruisePlayerCameraManager* LookCamera = GetSimCamera())
	{
		FVector2D Stick(GetInputAnalogKeyState(EKeys::Gamepad_RightX), GetInputAnalogKeyState(EKeys::Gamepad_RightY));
		if (IsOverlayOpen() || Stick.Size() < 0.18) Stick = FVector2D::ZeroVector;
		LookCamera->SetLookInput(Stick);
	}
	// This car's seat and view, as the driver left them.
	if (const ASimCruiseVehicle* Seated = Cast<ASimCruiseVehicle>(GetPawn()))
	{
		if (Seated->GetRealCar().Id != SeatCarId)
		{
			SeatCarId = Seated->GetRealCar().Id;
			if (ASimCruisePlayerCameraManager* Camera = GetSimCamera())
			{
				Camera->Seat = FSimCruiseSeat::Load(SeatCarId);
			}
		}
	}
	FSimCruiseRawControls Raw = ReadRawControls();
	UpdateWheel(Raw, DeltaTime);
	if (FMath::Abs(Raw.PadSteer) > 0.0 || Raw.PadThrottle > 0.0 || Raw.PadBrake > 0.0)
	{
		bUsingGamepad = true;
	}
	else if (Raw.KeySteer != 0.0 || Raw.bKeyThrottle || Raw.bKeyBrake)
	{
		bUsingGamepad = false;
	}
	LastCommand = Shaper.Update(Setup, Telemetry, Raw, DeltaTime);
	Vehicle->ApplyDriverCommand(LastCommand);
	UpdateRumble(DeltaTime);
	TickSelfTest(DeltaTime);
	TickCapture(DeltaTime);
}

void ASimCruisePlayerController::UpdateWheel(FSimCruiseRawControls& Raw, float DeltaTime)
{
	FSimCruiseWheelReading Wheel;
	if (!WheelDevice.Poll(Wheel))
	{
		return;
	}
	if (!bWheelSeen)
	{
		bWheelSeen = true;
		ShowMessage(FString::Printf(TEXT("Steering wheel: %s"), *WheelDevice.GetName()), 5.f);
	}
	// The rim angle through the car's real steering ratio (the cockpit wheel's), 1:1.
	double Ratio = 15.0;
	if (const APawn* Car = GetPawn())
	{
		if (const USimCruiseCarPresentationComponent* Model = Car->FindComponentByClass<USimCruiseCarPresentationComponent>())
		{
			Ratio = FMath::Max(Model->GetSteeringRatio(), 1.0);
		}
	}
	const double RimRad = FMath::DegreesToRadians(Wheel.Wheel * Wheel.HalfRotationDeg);
	Raw.bWheel = true;
	Raw.WheelRackRad = RimRad / Ratio;
	Raw.WheelThrottle = Wheel.Throttle;
	Raw.WheelBrake = Wheel.Brake;
	Raw.bWheelClutch = Wheel.bHasClutch;
	Raw.WheelClutch = Wheel.Clutch;
	Raw.bParkingBrake = Raw.bParkingBrake || Wheel.Handbrake > 0.5;
	if (Wheel.bNextGear) OnShiftUp();
	if (Wheel.bPreviousGear) OnShiftDown();
	bUsingGamepad = false;

	// Force feedback: what the column carries in the real car. The tyres' aligning torque about
	// the steering axes reaches the rim divided by the steering ratio (+ turns the wheels right),
	// so the rim goes light as the fronts pass their peak and slide. A light spring holds the rim
	// at rest and the column damps it; both stay small beside the tyre torque.
	const float Strength = FMath::Clamp(CVarWheelForce.GetValueOnGameThread(), 0.f, 1.f);
	if (IsPaused() || Strength <= 0.f)
	{
		WheelDevice.Stop();
		return;
	}
	const double RimTorqueNm = Telemetry.RackAligningTorqueNm / Ratio;
	const double MaxTorqueNm = FMath::Max(static_cast<double>(CVarWheelMaxTorque.GetValueOnGameThread()), 0.1);
	const double Sign = CVarWheelForceSign.GetValueOnGameThread() < 0 ? -1.0 : 1.0;
	const double Constant = FMath::Clamp(Sign * RimTorqueNm / MaxTorqueNm, -1.0, 1.0) * Strength;
	const double Speed = FMath::Abs(Telemetry.BodyVelocityMps.X);
	const double Spring = 0.06 * Strength;
	const double Damper = (0.18 - 0.08 * FMath::Clamp(Speed / 25.0, 0.0, 1.0)) * Strength;
	WheelDevice.SetForces(Spring, Damper, Constant);
}

void ASimCruisePlayerController::UpdateRumble(float DeltaTime)
{
	const float Strength = FMath::Clamp(CVarRumbleStrength.GetValueOnGameThread(), 0.f, 1.f);
	if (IsPaused() || Strength <= 0.f)
	{
		Haptics.Quiet();
		StopRumble();
		return;
	}
	LastRumble = Haptics.Sample(Telemetry, LastCommand.Throttle, LastCommand.Brake, DeltaTime);
	const FForceFeedbackValues Values = SimCruiseHaptics::ToChannels(LastRumble, RumbleBackend);
	const float Channel[4] = {Values.LeftLarge, Values.LeftSmall, Values.RightLarge, Values.RightSmall};
	for (int32 I = 0; I < 4; ++I)
	{
		const float Intensity = Channel[I] * Strength;
		const bool LL = I == 0, LS = I == 1, RL = I == 2, RS = I == 3;
		if (Intensity > 0.001f)
		{
			RumbleHandles[I] = RumbleHandles[I] == 0
				? PlayDynamicForceFeedback(Intensity, -1.f, LL, LS, RL, RS, EDynamicForceFeedbackAction::Start)
				: PlayDynamicForceFeedback(Intensity, -1.f, LL, LS, RL, RS, EDynamicForceFeedbackAction::Update, RumbleHandles[I]);
		}
		else if (RumbleHandles[I] != 0)
		{
			PlayDynamicForceFeedback(0.f, 0.f, LL, LS, RL, RS, EDynamicForceFeedbackAction::Stop, RumbleHandles[I]);
			RumbleHandles[I] = 0;
		}
	}
}

void ASimCruisePlayerController::StopRumble()
{
	for (int32 I = 0; I < 4; ++I)
	{
		if (RumbleHandles[I] != 0)
		{
			PlayDynamicForceFeedback(0.f, 0.f, I == 0, I == 1, I == 2, I == 3, EDynamicForceFeedbackAction::Stop, RumbleHandles[I]);
			RumbleHandles[I] = 0;
		}
	}
	LastRumble = FSimCruiseRumble();
}

void ASimCruisePlayerController::OnToggleTransmission()
{
	if (ISimCruiseVehicleControl* Vehicle = GetVehicle())
	{
		const bool bManual = !Telemetry.bManualTransmission;
		Vehicle->SetManualTransmission(bManual);
		// Read back now: the atlas toggles it while the game is paused.
		Vehicle->GetVehicleTelemetry(Telemetry);
		SimCruisePreferences::SetBool(TEXT("manual"), bManual);
		ShowMessage(bManual ? TEXT("Manual · clutch + shift to select first gear") : TEXT("Automatic shifting"), 4.f);
	}
}

void ASimCruisePlayerController::OnShiftUp()
{
	if (ISimCruiseVehicleControl* Vehicle = GetVehicle())
	{
		const FString Reply = Vehicle->RequestGearChange(1);
		if (!Reply.IsEmpty()) { ShowMessage(Reply, 2.f); }
	}
}

void ASimCruisePlayerController::OnShiftDown()
{
	if (ISimCruiseVehicleControl* Vehicle = GetVehicle())
	{
		const FString Reply = Vehicle->RequestGearChange(-1);
		if (!Reply.IsEmpty()) { ShowMessage(Reply, 2.f); }
	}
}

void ASimCruisePlayerController::OnStartEngine()
{
	if (ISimCruiseVehicleControl* Vehicle = GetVehicle())
	{
		Vehicle->RequestEngineStart();
	}
}

void ASimCruisePlayerController::OnCycleCamera()
{
	if (ASimCruisePlayerCameraManager* Camera = GetSimCamera())
	{
		Camera->CycleView();
		ShowMessage(ASimCruisePlayerCameraManager::ViewName(Camera->GetView()), 1.5f);
	}
}

void ASimCruisePlayerController::OnCycleHud()
{
	if (ASimCruiseHUD* Hud = GetSimHUD())
	{
		Hud->CycleLevel();
		SimCruisePreferences::SetInt(TEXT("hud_level"), static_cast<int32>(Hud->GetLevel()));
	}
}

void ASimCruisePlayerController::OnToggleAtlas()
{
	if (IsOverlayOpen())
	{
		CloseOverlays();
		return;
	}
	OpenRoadAtlas();
}

void ASimCruisePlayerController::OnTuning()
{
	if (IsOverlayOpen())
	{
		// The menu itself handles its keys; from another screen the key just closes it.
		CloseOverlays();
		return;
	}
	// Open anywhere (the car waits while you choose); a setup saved on the move goes on at the next stop.
	OpenTuning();
}

void ASimCruisePlayerController::OpenTuning()
{
	ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(GetPawn());
	if (!Car)
	{
		return;
	}
	CloseOverlays();
	TuningPanel = SNew(SSimCruiseTuningMenu)
		.Vehicle(Car)
		.OnClose_Lambda([this]()
		{
			const SSimCruiseTuningMenu::ETuningResult Result = TuningPanel.IsValid() ? TuningPanel->GetResult() : SSimCruiseTuningMenu::ETuningResult::Cancelled;
			CloseOverlays();
			// The rebuilt car reports its new ratio and limits.
			if (ISimCruiseVehicleControl* Vehicle = GetVehicle())
			{
				Setup = Vehicle->GetVehicleSetup();
			}
			const ASimCruiseVehicle* Tuned = Cast<ASimCruiseVehicle>(GetPawn());
			if (Result == SSimCruiseTuningMenu::ETuningResult::Saved && Tuned && Tuned->HasPendingTuning())
			{
				bTuningWaits = true;
				ShowMessage(TEXT("Setup saved: it goes on when you stop"), 3.f);
			}
			else if (Result == SSimCruiseTuningMenu::ETuningResult::Saved)
			{
				ShowMessage(TEXT("Setup saved for this car"), 2.f);
			}
			else if (Result == SSimCruiseTuningMenu::ETuningResult::SaveFailed)
			{
				ShowMessage(TEXT("Setup applied, but it could not be saved"), 3.f);
			}
			else
			{
				ShowMessage(TEXT("Setup unchanged"), 1.5f);
			}
		});
	ShowOverlay(TuningPanel.ToSharedRef(), TuningPanel);
}

void ASimCruisePlayerController::OpenSeatMenu()
{
	ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(GetPawn());
	ASimCruisePlayerCameraManager* Camera = GetSimCamera();
	if (!Car || !Camera)
	{
		return;
	}
	// A seat is set parked: the menu keeps the world running and the car on its brakes.
	if (FMath::Abs(Telemetry.BodyVelocityMps.X) > 1.0)
	{
		CloseOverlays();
		ShowMessage(TEXT("Stop the car to set your seat"), 2.f);
		return;
	}
	CloseOverlays();
	// The seat is set from the seat: the driver's view while the menu is open.
	const ESimCruiseView Before = Camera->GetView();
	Camera->SetView(ESimCruiseView::Driver);
	SeatPanel = SNew(SSimCruiseSeatMenu)
		.Camera(Camera)
		.CarId(Car->GetRealCar().Id)
		.CarName(Car->GetRealCar().Name)
		.OnClose_Lambda([this, Before]()
		{
			const bool bSaved = SeatPanel.IsValid() && SeatPanel->WasSaved();
			CloseOverlays();
			if (ASimCruisePlayerCameraManager* Cam = GetSimCamera())
			{
				Cam->SetView(Before);
			}
			ShowMessage(bSaved ? TEXT("Seat and view saved for this car") : TEXT("Seat and view unchanged"), 2.f);
		});
	ShowOverlay(SeatPanel.ToSharedRef(), SeatPanel, false);
}

void ASimCruisePlayerController::OnFindPlace()
{
	if (IsOverlayOpen())
	{
		CloseOverlays();
		return;
	}
	OpenPlaceSearch(false);
}

FSimCruiseAtlasView ASimCruisePlayerController::AtlasView() const
{
	FSimCruiseAtlasView View;
	const ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
	if (!Mode)
	{
		return View;
	}
	if (ASimCruiseWorldStreamer* Stream = Mode->GetStreamer())
	{
		View.Frame = Stream->GetFrame();
		View.Streamer = Stream;
	}
	View.Title = Mode->GetPlace().Title;
	if (const APawn* Car = GetPawn())
	{
		View.CarCm = Car->GetActorLocation();
		View.CarYawDeg = Car->GetActorRotation().Yaw;
		View.bHasCar = true;
	}
	else
	{
		View.CarCm = View.Frame.IsValid() ? View.Frame.LonLatToWorldCm(Mode->GetPlace().Lon, Mode->GetPlace().Lat) : FVector::ZeroVector;
	}
	const USimCruiseGameInstance* GameInstance = GetGameInstance<USimCruiseGameInstance>();
	const FSimCruiseCarDefinition* CarDef = GameInstance ? GameInstance->FindCarDefinition(Mode->GetCarId()) : nullptr;
	View.CarName = CarDef ? CarDef->Name : Mode->GetCarId();
	View.Transmission = Telemetry.bManualTransmission ? TEXT("Manual") : TEXT("Automatic");
	const ASimCruiseHUD* Hud = GetSimHUD();
	static const TCHAR* HudNames[] = { TEXT("Off"), TEXT("Glance"), TEXT("Full") };
	View.Settings = {
		FString::Printf(TEXT("Sound · %s"), bSoundOn ? TEXT("On") : TEXT("Off")),
		FString::Printf(TEXT("HUD · %s"), Hud ? HudNames[FMath::Clamp(static_cast<int32>(Hud->GetLevel()), 0, 2)] : TEXT("Glance")),
		FString::Printf(TEXT("Head motion · %d%%"), FMath::RoundToInt(100.f * SimCruiseSettingValue(TEXT("simcruise.Camera.HeadMotion")))),
		FString::Printf(TEXT("Rumble · %d%%"), FMath::RoundToInt(100.f * SimCruiseSettingValue(TEXT("simcruise.Rumble.Strength")))),
		FString::Printf(TEXT("View · %s"), GetSimCamera() && GetSimCamera()->GetLens() == ESimCruiseLens::Screen ? TEXT("True FOV")
			: GetSimCamera() && GetSimCamera()->GetLens() == ESimCruiseLens::Wide ? TEXT("Wide") : TEXT("Natural")),
		FString::Printf(TEXT("Screen · %.1f in"), SimCruiseSettingValue(TEXT("simcruise.Lens.ScreenInches"))),
		FString::Printf(TEXT("Eye to screen · %.0f cm"), SimCruiseSettingValue(TEXT("simcruise.Lens.ViewDistanceCm"))),
		FString::Printf(TEXT("Stick steering speed · %d%%"), FMath::RoundToInt(100.f * SimCruiseSettingValue(TEXT("simcruise.Pad.SteerSpeed")))),
		FString::Printf(TEXT("Stick response · %s"), SimCruiseSettingValue(TEXT("simcruise.Pad.SteerLinearity")) > 0.75f ? TEXT("Linear")
			: SimCruiseSettingValue(TEXT("simcruise.Pad.SteerLinearity")) > 0.25f ? TEXT("Between") : TEXT("Fine centre")),
		// The total: the input's fixed 12 % plus the player's extra over the rest of the travel.
		FString::Printf(TEXT("Stick dead zone · %d%%"), FMath::RoundToInt(100.f * (0.12f + 0.88f * SimCruiseSettingValue(TEXT("simcruise.Pad.SteerDeadZone"))))),
		FString::Printf(TEXT("Throttle trigger · %s"), SimCruiseSettingValue(TEXT("simcruise.TriggerGamma")) < 1.2f ? TEXT("Linear")
			: SimCruiseSettingValue(TEXT("simcruise.TriggerGamma")) < 1.6f ? TEXT("Gentle") : SimCruiseSettingValue(TEXT("simcruise.TriggerGamma")) < 2.1f ? TEXT("Progressive")
			: TEXT("Very progressive")),
		TEXT("Tune car..."),
		TEXT("Seat & view..."),
		FString::Printf(TEXT("Traffic · %s"), SimCruiseSettingValue(TEXT("simcruise.Traffic.Cars")) < 0.5f ? TEXT("Off")
			: SimCruiseSettingValue(TEXT("simcruise.Traffic.Cars")) < 12.f ? TEXT("Light") : TEXT("Normal")),
	};
	return View;
}

void ASimCruisePlayerController::ShowOverlay(const TSharedRef<SWidget>& Widget, const TSharedPtr<SWidget>& Focus, bool bPause)
{
	if (!GEngine || !GEngine->GameViewport)
	{
		return;
	}
	if (OverlayWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(OverlayWidget.ToSharedRef());
	}
	OverlayWidget = Widget;
	GEngine->GameViewport->AddViewportWidgetContent(Widget, 50);
	// The car waits while you choose (Drive.gd froze it with a menu open); pad and keys go to the screen.
	// The seat menu keeps the world running (a paused frame smears in the temporal filters) and holds
	// the parked car on its brakes instead.
	SetPause(bPause);
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Focus.IsValid() ? Focus : TSharedPtr<SWidget>(Widget));
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	bShowMouseCursor = true;
	StopRumble();
	Haptics.Quiet();
}

void ASimCruisePlayerController::OpenPlaceSearch(bool bGamepad)
{
	CloseOverlays();
	SearchPanel = SNew(SSimCruisePlaceSearch)
		.OnChoose_Lambda([this](const FSimCruisePlace& Place) { ChoosePlace(Place); })
		.OnSurprise_Lambda([this]() { return SurpriseMe(); })
		.OnClose_Lambda([this]() { CloseOverlays(); });
	ShowOverlay(SearchPanel.ToSharedRef(), SearchPanel->FocusTarget(bGamepad));
}

void ASimCruisePlayerController::OpenRoadAtlas()
{
	CloseOverlays();
	AtlasPanel = SNew(SSimCruiseRoadAtlas)
		.GetView_Lambda([this]() { return AtlasView(); })
		.OnChoose_Lambda([this](const FSimCruisePlace& Place) { ChoosePlace(Place); })
		.OnSurprise_Lambda([this]() { return SurpriseMe(); })
		.OnFindPlace_Lambda([this]() { OpenPlaceSearch(true); })
		.OnCycleCar_Lambda([this]()
		{
			if (ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr)
			{
				Mode->CycleCar(this);
			}
		})
		.OnToggleTransmission_Lambda([this]() { OnToggleTransmission(); })
		.OnSetting_Lambda([this](int32 Index) { ChangeSetting(Index); })
		.OnClose_Lambda([this]() { CloseOverlays(); });
	ShowOverlay(AtlasPanel.ToSharedRef(), AtlasPanel);
}

void ASimCruisePlayerController::ChangeSetting(int32 Index)
{
	// The atlas's cockpit settings, remembered (Hud.gd, DrivingPreferences.gd).
	auto Step = [](const TCHAR* Name, std::initializer_list<float> Ladder, const TCHAR* Key)
	{
		const float Now = SimCruiseSettingValue(Name);
		float Next = *Ladder.begin();
		for (auto It = Ladder.begin(); It != Ladder.end(); ++It)
		{
			if (FMath::IsNearlyEqual(*It, Now, 0.01f)) { Next = (It + 1 != Ladder.end()) ? *(It + 1) : *Ladder.begin(); break; }
		}
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) Var->Set(Next, ECVF_SetByCode);
		SimCruisePreferences::SetInt(Key, FMath::RoundToInt(Next * 100.f));
	};
	switch (Index)
	{
	case 0: OnToggleSound(); break;
	case 1: OnCycleHud(); break;
	case 2: Step(TEXT("simcruise.Camera.HeadMotion"), { 1.f, 0.5f, 0.f }, TEXT("head_motion")); break;
	case 3: Step(TEXT("simcruise.Rumble.Strength"), { 1.f, 0.6f, 0.3f, 0.f }, TEXT("rumble")); break;
	case 4: OnCycleLens(); break;
	case 5:
	{
		// Common laptop and monitor diagonals.
		static const float Sizes[] = { 13.3f, 14.f, 15.6f, 17.3f, 24.f, 27.f, 32.f, 43.f, 55.f, 65.f };
		const float Now = SimCruiseSettingValue(TEXT("simcruise.Lens.ScreenInches"));
		float Next = Sizes[0];
		for (int32 i = 0; i < UE_ARRAY_COUNT(Sizes); ++i) if (Sizes[i] > Now + 0.05f) { Next = Sizes[i]; break; }
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Lens.ScreenInches"))) Var->Set(Next, ECVF_SetByCode);
		SimCruisePreferences::SetInt(TEXT("screen_tenths_inch"), FMath::RoundToInt(Next * 10.f));
		break;
	}
	case 6:
	{
		static const float Distances[] = { 40.f, 50.f, 60.f, 70.f, 80.f, 100.f, 150.f, 200.f, 250.f };
		const float Now = SimCruiseSettingValue(TEXT("simcruise.Lens.ViewDistanceCm"));
		float Next = Distances[0];
		for (int32 i = 0; i < UE_ARRAY_COUNT(Distances); ++i) if (Distances[i] > Now + 0.5f) { Next = Distances[i]; break; }
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Lens.ViewDistanceCm"))) Var->Set(Next, ECVF_SetByCode);
		SimCruisePreferences::SetInt(TEXT("view_distance_cm"), FMath::RoundToInt(Next));
		break;
	}
	case 7: Step(TEXT("simcruise.Pad.SteerSpeed"), { 1.f, 1.25f, 1.5f, 0.6f, 0.8f }, TEXT("pad_steer_speed")); break;
	case 8: Step(TEXT("simcruise.Pad.SteerLinearity"), { 0.f, 0.5f, 1.f }, TEXT("pad_steer_linearity")); break;
	case 9: Step(TEXT("simcruise.Pad.SteerDeadZone"), { 0.f, 0.05f, 0.1f, 0.15f }, TEXT("pad_steer_dead_zone")); break;
	case 10: Step(TEXT("simcruise.TriggerGamma"), { 1.8f, 2.4f, 1.f, 1.4f }, TEXT("trigger_gamma")); break;
	// The atlas closes and the menu opens in its place.
	case 11: OpenTuning(); break;
	case 12: OpenSeatMenu(); break;
	case 13:
	{
		// Normal (20 cars about you), light (8) or none; remembered as the car count.
		const float Now = SimCruiseSettingValue(TEXT("simcruise.Traffic.Cars"));
		const int32 Next = Now > 12.f ? 8 : Now > 0.5f ? 0 : 20;
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Traffic.Cars"))) Var->Set(Next, ECVF_SetByCode);
		SimCruisePreferences::SetInt(TEXT("traffic_cars"), Next);
		break;
	}
	default: break;
	}
}

void ASimCruisePlayerController::CloseOverlays()
{
	if (OverlayWidget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(OverlayWidget.ToSharedRef());
	}
	const bool bWasOpen = OverlayWidget.IsValid();
	OverlayWidget.Reset();
	SearchPanel.Reset();
	AtlasPanel.Reset();
	TuningPanel.Reset();
	SeatPanel.Reset();
	if (bWasOpen)
	{
		SetPause(false);
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
		Shaper.Reset();
	}
}

void ASimCruisePlayerController::ChoosePlace(const FSimCruisePlace& Place)
{
	CloseOverlays();
	if (ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr)
	{
		Mode->TravelTo(Place);
	}
}

bool ASimCruisePlayerController::SurpriseMe()
{
	ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
	if (!Mode)
	{
		return false;
	}
	CloseOverlays();
	return Mode->SurpriseMe();
}

void ASimCruisePlayerController::OnRecover()
{
	if (ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr)
	{
		Mode->RecoverVehicle(this);
		Shaper.Reset();
		Haptics.Quiet();
		if (ASimCruisePlayerCameraManager* Camera = GetSimCamera())
		{
			Camera->SnapNextFrame();
		}
	}
}

void ASimCruisePlayerController::OnToggleSound()
{
	bSoundOn = !bSoundOn;
	SimCruisePreferences::SetBool(TEXT("sound"), bSoundOn);
	ShowMessage(bSoundOn ? TEXT("Sound on") : TEXT("Sound off"), 2.f);
}

void ASimCruisePlayerController::OnToggleLights()
{
	bLightsOn = !bLightsOn;
	if (ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(GetPawn()))
	{
		// Off returns the lamps to following the sun.
		Car->bLampsOn = bLightsOn;
	}
	ShowMessage(bLightsOn ? TEXT("Lights on") : TEXT("Lights off"), 2.f);
}

void ASimCruisePlayerController::OnCycleLens()
{
	if (ASimCruisePlayerCameraManager* Camera = GetSimCamera())
	{
		Camera->CycleLens();
		SimCruisePreferences::SetInt(TEXT("lens_v3"), static_cast<int32>(Camera->GetLens()));
		ShowMessage(FString::Printf(TEXT("Lens · %s"), SimCruiseLens::Label(Camera->GetLens())), 2.f);
	}
}

void ASimCruisePlayerController::OnToggleStats()
{
	bStatsShown = !bStatsShown;
	ConsoleCommand(TEXT("stat fps"));
	ConsoleCommand(TEXT("stat unit"));
}

// --- Self-test -------------------------------------------------------------

void ASimCruisePlayerController::SimCruiseShellSelfTest(int32 Quit)
{
	SelfTestStage = 0;
	SelfTestClock = 0.f;
	bSelfTestQuit = Quit != 0;
	SelfTestFailure.Reset();
	UE_LOG(LogSimCruiseShell, Display, TEXT("SimCruise shell self-test started"));
}

void ASimCruisePlayerController::InjectKey(const FKey& Key, bool bPressed)
{
	InputKey(FInputKeyEventArgs::CreateSimulated(Key, bPressed ? IE_Pressed : IE_Released, bPressed ? 1.f : 0.f));
}

void ASimCruisePlayerController::InjectAxis(const FKey& Key, float Value)
{
	InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Axis, Value));
}

void ASimCruisePlayerController::TickSelfTest(float DeltaTime)
{
	if (SelfTestStage < 0)
	{
		return;
	}
	SelfTestClock += DeltaTime;
	const double Rack = Shaper.GetRackAngleRad();
	ASimCruisePlayerCameraManager* Camera = GetSimCamera();
	ASimCruiseHUD* Hud = GetSimHUD();
	auto Fail = [this](const FString& Why) { SelfTestFailure = Why; SelfTestStage = 100; };
	auto Next = [this]() { ++SelfTestStage; SelfTestClock = 0.f; };

	switch (SelfTestStage)
	{
	case 0: // settle; the vehicle must exist (a drive on the real place places it once the road is built)
		if (SelfTestClock > 0.5f && HasVehicle())
		{
			Next();
		}
		else if (SelfTestClock > 300.f)
		{
			Fail(TEXT("no ISimCruiseVehicleControl pawn possessed"));
		}
		break;
	case 1: // left stick, a third to the left
		InjectAxis(EKeys::Gamepad_LeftX, -0.30f);
		if (SelfTestClock > 0.5f)
		{
			if (!(Rack < 0.0) || !Shaper.IsUsingThumbstick()) { Fail(FString::Printf(TEXT("left stick routing: rack %.4f"), Rack)); break; }
			Next();
		}
		break;
	case 2:
		InjectAxis(EKeys::Gamepad_LeftX, 0.30f);
		if (SelfTestClock > 0.6f)
		{
			if (!(Rack > 0.0)) { Fail(FString::Printf(TEXT("right stick direction: rack %.4f"), Rack)); break; }
			Next();
		}
		break;
	case 3:
		InjectAxis(EKeys::Gamepad_LeftX, 0.f);
		if (SelfTestClock > 0.6f)
		{
			if (FMath::Abs(Rack) > 1e-4) { Fail(FString::Printf(TEXT("stick did not centre: rack %.5f"), Rack)); break; }
			InjectKey(EKeys::A, true);
			Next();
		}
		break;
	case 4: // A held: full left lock at standstill
		if (SelfTestClock > 1.6f)
		{
			const double Range = SimCruiseShaping::AvailableSteeringRad(Setup, Telemetry.BodyVelocityMps.X);
			if (Rack > -0.95 * Range) { Fail(FString::Printf(TEXT("keyboard full lock: rack %.4f of %.4f"), Rack, Range)); break; }
			InjectKey(EKeys::A, false);
			Next();
		}
		break;
	case 5:
		if (SelfTestClock > 1.0f)
		{
			if (FMath::Abs(Rack) > 1e-4) { Fail(FString::Printf(TEXT("key release did not centre: rack %.5f"), Rack)); break; }
			SelfTestViewBefore = Camera ? static_cast<int32>(Camera->GetView()) : -1;
			SelfTestHudBefore = Hud ? static_cast<int32>(Hud->GetLevel()) : -1;
			InjectKey(EKeys::Gamepad_FaceButton_Top, true);
			InjectKey(EKeys::Gamepad_FaceButton_Left, true);
			Next();
		}
		break;
	case 6: // Y cycles the camera, X the HUD
		if (SelfTestClock > 0.05f && SelfTestClock - DeltaTime <= 0.05f)
		{
			InjectKey(EKeys::Gamepad_FaceButton_Top, false);
			InjectKey(EKeys::Gamepad_FaceButton_Left, false);
		}
		if (SelfTestClock > 0.2f)
		{
			const int32 ViewNow = Camera ? static_cast<int32>(Camera->GetView()) : -1;
			const int32 HudNow = Hud ? static_cast<int32>(Hud->GetLevel()) : -1;
			if (ViewNow != (SelfTestViewBefore + 1) % SimCruiseViewCount) { Fail(TEXT("Y did not cycle the camera")); break; }
			if (HudNow != (SelfTestHudBefore + 1) % 3) { Fail(TEXT("X did not cycle the HUD")); break; }
			InjectKey(EKeys::C, true);
			InjectKey(EKeys::F2, true);
			Next();
		}
		break;
	case 7: // C and F2 do the same from the keyboard
		if (SelfTestClock > 0.05f && SelfTestClock - DeltaTime <= 0.05f)
		{
			InjectKey(EKeys::C, false);
			InjectKey(EKeys::F2, false);
		}
		if (SelfTestClock > 0.2f)
		{
			const int32 ViewNow = Camera ? static_cast<int32>(Camera->GetView()) : -1;
			const int32 HudNow = Hud ? static_cast<int32>(Hud->GetLevel()) : -1;
			if (ViewNow != (SelfTestViewBefore + 2) % SimCruiseViewCount) { Fail(TEXT("C did not cycle the camera")); break; }
			if (HudNow != (SelfTestHudBefore + 2) % 3) { Fail(TEXT("F2 did not cycle the HUD")); break; }
			Next();
		}
		break;
	case 8: // right trigger: full throttle, linear (one axis sample per frame)
		if (SelfTestClock > 0.25f)
		{
			if (LastCommand.Throttle < 0.99f) { Fail(FString::Printf(TEXT("right trigger throttle %.3f"), LastCommand.Throttle)); break; }
			Next();
		}
		InjectAxis(EKeys::Gamepad_RightTriggerAxis, SelfTestStage == 8 ? 1.f : 0.f);
		break;
	case 9: // trigger released: the pedal returns at once
		InjectAxis(EKeys::Gamepad_RightTriggerAxis, 0.f);
		if (SelfTestClock > 0.2f)
		{
			if (LastCommand.Throttle > 0.f) { Fail(FString::Printf(TEXT("right trigger release %.3f"), LastCommand.Throttle)); break; }
			InjectKey(EKeys::W, true);
			Next();
		}
		break;
	case 10: // W: the pedal ramps like a foot, 3/s
		if (SelfTestClock >= 0.30f)
		{
			if (LastCommand.Throttle < 0.75f || LastCommand.Throttle > 0.99f) { Fail(FString::Printf(TEXT("W ramp after 0.3 s: %.3f"), LastCommand.Throttle)); break; }
			InjectKey(EKeys::W, false);
			Next();
		}
		break;
	case 11: // left trigger half way: brake past the dead zone
		if (SelfTestClock > 0.25f)
		{
			const float Expected = (0.5f - 0.04f) / 0.96f;
			if (FMath::Abs(LastCommand.Brake - Expected) > 0.03f) { Fail(FString::Printf(TEXT("left trigger brake %.3f, expected %.3f"), LastCommand.Brake, Expected)); break; }
			Next();
		}
		InjectAxis(EKeys::Gamepad_LeftTriggerAxis, SelfTestStage == 11 ? 0.5f : 0.f);
		break;
	case 12:
		InjectAxis(EKeys::Gamepad_LeftTriggerAxis, 0.f);
		if (SelfTestClock > 0.2f)
		{
			if (Camera) { Camera->SetView(ESimCruiseView::Driver); }
			if (Hud) { Hud->SetLevel(ESimCruiseHudLevel::Glance); }
			SelfTestStage = 100;
		}
		break;
	default:
		break;
	}

	if (SelfTestStage == 100)
	{
		const bool bPassed = SelfTestFailure.IsEmpty();
		UE_LOG(LogSimCruiseShell, Display, TEXT("SIMCRUISE SHELL SELF-TEST %s %s"), bPassed ? TEXT("PASSED") : TEXT("FAILED"),
			bPassed ? TEXT("stick both ways, exact centre, key full lock, Y/X/C/F2, RT linear, W ramp, LT dead zone") : *SelfTestFailure);
		SelfTestStage = -1;
		if (bSelfTestQuit)
		{
			// A graceful exit always returns 0 on Windows (the code given to
			// PostQuitMessage is dropped), so a failure exits forcibly with 1,
			// as the automation command line does, after flushing the log.
			if (bPassed)
			{
				FPlatformMisc::RequestExit(false, TEXT("SimCruiseShellSelfTest"));
			}
			else
			{
				FPlatformMisc::RequestExitWithStatus(true, 1, TEXT("SimCruiseShellSelfTest"));
			}
		}
	}
}

// --- Documentation capture ---------------------------------------------------

void ASimCruisePlayerController::SimCruiseShellCapture(int32 Quit)
{
	CaptureClock = 0.f;
	CaptureShot = 0;
	CaptureTraceTick = -1;
	bCaptureQuit = Quit != 0;
	UE_LOG(LogSimCruiseShell, Display, TEXT("SimCruise shell capture started"));
}

void ASimCruisePlayerController::TickCapture(float DeltaTime)
{
	if (CaptureClock < 0.f || !HasVehicle())
	{
		// The timeline starts when the car is on the road.
		return;
	}
	CaptureClock += DeltaTime;
	const float T = CaptureClock;
	ASimCruisePlayerCameraManager* Camera = GetSimCamera();
	ASimCruiseHUD* Hud = GetSimHUD();

	// Timeline (s after Settle): 0-6 full throttle; 6-11 three-quarter throttle
	// with the stick 60 % right; 11-11.3 lift, stick half right and a 0.3 s
	// pull of the parking brake (A) to step the rear out; 11.3-12.5 answer
	// the slide by aiming the front wheels along the front axle's travel
	// beta_f (stick = beta_f / (|beta_f| + surplus); the countersteer
	// allowance lets the rack follow past the speed cap); 12.5-16 hard
	// braking until stopped. The 10 Hz slide trace records what happened.
	const float Settle = 10.f;
	const float S = T - Settle;
	float Throttle = 0.f;
	float Steer = 0.f;
	float Brake = 0.f;
	const double BetaFront = SimCruiseShaping::FrontAxleSlipAngleRad(Setup, Telemetry.BodyVelocityMps.X, Telemetry.BodyVelocityMps.Y, Telemetry.BodyAngularVelocityRadPerS.Z);
	if (S > 0.f && S <= 6.f) { Throttle = 1.f; }
	if (S > 6.f && S <= 11.f) { Throttle = 0.75f; Steer = 0.6f; }
	if (S > 11.f && S <= 11.3f) { Steer = 0.5f; }
	if (S > 11.3f && S <= 12.5f) { Steer = static_cast<float>(BetaFront / (FMath::Abs(BetaFront) + Setup.SteeringSlipLimitSurplusRad)); }
	// A 10 Hz trace of the slide and its catch, for the record.
	const int32 TraceTick = FMath::FloorToInt(S * 10.f);
	if (S > 10.9f && S <= 13.5f && TraceTick != CaptureTraceTick)
	{
		CaptureTraceTick = TraceTick;
		UE_LOG(LogSimCruiseShell, Display, TEXT("Slide trace t %.1f s: %.1f mph, stick %.2f, rack %.2f deg (range L %.2f R %.2f), front-axle slip %.1f deg, yaw rate %.1f deg/s"),
			S, Telemetry.BodyVelocityMps.X * 2.236936, Steer, FMath::RadiansToDegrees(Shaper.GetRackAngleRad()),
			FMath::RadiansToDegrees(Shaper.GetRangeLeftRad()), FMath::RadiansToDegrees(Shaper.GetRangeRightRad()),
			FMath::RadiansToDegrees(BetaFront), FMath::RadiansToDegrees(Telemetry.BodyAngularVelocityRadPerS.Z));
	}
	// Stop whichever way the car is rolling: in automatic, LT brakes forward
	// motion and RT brakes backward motion (Powertrain.drive semantics).
	if (S > 12.5f && S <= 16.f && Telemetry.BodyVelocityMps.X > 0.5) { Brake = 1.f; }
	if (S > 12.5f && S <= 16.f && Telemetry.BodyVelocityMps.X < -0.5) { Throttle = 1.f; }
	InjectAxis(EKeys::Gamepad_RightTriggerAxis, Throttle);
	InjectAxis(EKeys::Gamepad_LeftTriggerAxis, Brake);
	InjectAxis(EKeys::Gamepad_LeftX, Steer);
	const bool bParkingBrake = S > 11.f && S <= 11.3f;
	if (bParkingBrake != bCaptureParkingBrake)
	{
		InjectKey(EKeys::Gamepad_FaceButton_Right, bParkingBrake);
		bCaptureParkingBrake = bParkingBrake;
	}

	struct FShot { float Time; ESimCruiseView View; ESimCruiseHudLevel Level; const TCHAR* Name; };
	static const FShot Shots[] = {
		{Settle - 1.0f, ESimCruiseView::Driver, ESimCruiseHudLevel::Glance, TEXT("shell-driver-parked")},
		{Settle + 5.5f, ESimCruiseView::Driver, ESimCruiseHudLevel::Glance, TEXT("shell-driver-accelerating")},
		{Settle + 9.5f, ESimCruiseView::Chase, ESimCruiseHudLevel::Glance, TEXT("shell-chase-cornering")},
		{Settle + 11.6f, ESimCruiseView::Chase, ESimCruiseHudLevel::Full, TEXT("shell-chase-sliding")},
		{Settle + 13.5f, ESimCruiseView::Driver, ESimCruiseHudLevel::Full, TEXT("shell-driver-braking")},
		{Settle + 17.5f, ESimCruiseView::Scenic, ESimCruiseHudLevel::Off, TEXT("shell-scenic-stopped")},
	};
	constexpr int32 ShotCount = UE_ARRAY_COUNT(Shots);
	if (CaptureShot < ShotCount)
	{
		const FShot& Shot = Shots[CaptureShot];
		// Switch the view a little early so the camera's lags settle.
		if (T > Shot.Time - 1.5f && Camera && Camera->GetView() != Shot.View) { Camera->SetView(Shot.View); }
		if (T > Shot.Time - 1.5f && Hud && Hud->GetLevel() != Shot.Level) { Hud->SetLevel(Shot.Level); }
		if (T >= Shot.Time)
		{
			FScreenshotRequest::RequestScreenshot(FString(Shot.Name), true, false);
			UE_LOG(LogSimCruiseShell, Display, TEXT("Capture %s at %.1f mph, rack %.2f deg (range L %.2f R %.2f), body slip %.1f deg, front-axle slip %.1f deg, chase swing %.1f deg, rumble low %.2f high %.2f LT %.2f RT %.2f"), Shot.Name,
				Telemetry.BodyVelocityMps.X * 2.236936, FMath::RadiansToDegrees(Shaper.GetRackAngleRad()),
				FMath::RadiansToDegrees(Shaper.GetRangeLeftRad()), FMath::RadiansToDegrees(Shaper.GetRangeRightRad()),
				FMath::RadiansToDegrees(FMath::Atan2(Telemetry.BodyVelocityMps.Y, FMath::Abs(Telemetry.BodyVelocityMps.X) + 0.01)),
				FMath::RadiansToDegrees(BetaFront),
				Camera ? FMath::RadiansToDegrees(Camera->GetChaseFrame().GetSwingRad()) : 0.0,
				LastRumble.LowFrequency, LastRumble.HighFrequency, LastRumble.LeftTrigger, LastRumble.RightTrigger);
			++CaptureShot;
		}
	}
	else if (T > Settle + 19.f)
	{
		CaptureClock = -1.f;
		UE_LOG(LogSimCruiseShell, Display, TEXT("SimCruise shell capture finished"));
		if (bCaptureQuit)
		{
			FPlatformMisc::RequestExit(false);
		}
	}
}
