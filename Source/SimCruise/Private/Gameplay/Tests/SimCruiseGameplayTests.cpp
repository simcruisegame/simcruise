// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/World.h"
#include "Gameplay/SimCruiseCameraModels.h"
#include "Gameplay/SimCruiseDriverShaping.h"
#include "Gameplay/SimCruiseHaptics.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruiseInputConfig.h"
#include "Gameplay/SimCruisePlaceholderVehicle.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

namespace SimCruiseGameplayTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** The 1999 roadster: FSimCruiseVehicleSetup defaults are the NB's. */
	FSimCruiseVehicleSetup Roadster() { return FSimCruiseVehicleSetup(); }

	FSimCruiseDriveTelemetry Moving(double Vx, double Vy = 0.0, double YawRate = 0.0)
	{
		FSimCruiseDriveTelemetry T;
		T.BodyVelocityMps = FVector(Vx, Vy, 0.0);
		T.BodyAngularVelocityRadPerS = FVector(0.0, 0.0, YawRate);
		return T;
	}

	/** Run the shaper for Seconds at 120 Hz with fixed raw controls. */
	double Hold(FSimCruiseDriverShaper& Shaper, const FSimCruiseDriveTelemetry& T, const FSimCruiseRawControls& Raw, double Seconds,
		FSimCruiseDriverCommand* OutCommand = nullptr, double* OutLargestStep = nullptr)
	{
		const FSimCruiseVehicleSetup Setup = Roadster();
		const int32 Steps = FMath::RoundToInt(Seconds * 120.0);
		FSimCruiseDriverCommand Command;
		double Largest = 0.0;
		for (int32 I = 0; I < Steps; ++I)
		{
			const double Before = Shaper.GetRackAngleRad();
			Command = Shaper.Update(Setup, T, Raw, 1.0 / 120.0);
			Largest = FMath::Max(Largest, FMath::Abs(Shaper.GetRackAngleRad() - Before));
		}
		if (OutCommand) { *OutCommand = Command; }
		if (OutLargestStep) { *OutLargestStep = Largest; }
		return Shaper.GetRackAngleRad();
	}

	const FEnhancedActionKeyMapping* FindMapping(const USimCruiseInputConfig* Config, const TCHAR* Action, const FKey& Key)
	{
		const UInputAction* Wanted = Config->FindAction(Action);
		for (const FEnhancedActionKeyMapping& Mapping : Config->DrivingContext->GetMappings())
		{
			if (Mapping.Action == Wanted && Mapping.Key == Key)
			{
				return &Mapping;
			}
		}
		return nullptr;
	}

	float ApplyModifiers(const FEnhancedActionKeyMapping& Mapping, float Raw)
	{
		FInputActionValue Value(Raw);
		for (const UInputModifier* Modifier : Mapping.Modifiers)
		{
			Value = Modifier->ModifyRaw(nullptr, Value, 1.f / 60.f);
		}
		return Value.Get<float>();
	}
}

using namespace SimCruiseGameplayTest;

// --- Input ------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseInputBindingsTest, "SimCruise.Gameplay.Input.BindingsMatchControlsDoc", Flags)
bool FSimCruiseInputBindingsTest::RunTest(const FString& Parameters)
{
	USimCruiseInputConfig* Config = USimCruiseInputConfig::Create(GetTransientPackage());
	TestNotNull(TEXT("mapping context"), Config->DrivingContext.Get());

	// docs/controls.md, keyboard and Xbox columns.
	struct FExpect { const TCHAR* Action; FKey Key; };
	const FExpect Expected[] = {
		{TEXT("ThrottlePad"), EKeys::Gamepad_RightTriggerAxis}, {TEXT("ThrottleKey"), EKeys::W}, {TEXT("ThrottleKey"), EKeys::Up},
		{TEXT("BrakePad"), EKeys::Gamepad_LeftTriggerAxis}, {TEXT("BrakeKey"), EKeys::S}, {TEXT("BrakeKey"), EKeys::Down},
		{TEXT("SteerPad"), EKeys::Gamepad_LeftX}, {TEXT("SteerKey"), EKeys::A}, {TEXT("SteerKey"), EKeys::D},
		{TEXT("SteerKey"), EKeys::Left}, {TEXT("SteerKey"), EKeys::Right},
		{TEXT("FullBrake"), EKeys::SpaceBar},
		{TEXT("ParkingBrake"), EKeys::H}, {TEXT("ParkingBrake"), EKeys::Gamepad_FaceButton_Right},
		{TEXT("ToggleTransmission"), EKeys::T}, {TEXT("ToggleTransmission"), EKeys::Gamepad_RightThumbstick},
		{TEXT("Clutch"), EKeys::LeftShift}, {TEXT("Clutch"), EKeys::Gamepad_FaceButton_Bottom},
		{TEXT("ShiftUp"), EKeys::E}, {TEXT("ShiftUp"), EKeys::Gamepad_RightShoulder},
		{TEXT("ShiftDown"), EKeys::Q}, {TEXT("ShiftDown"), EKeys::Gamepad_LeftShoulder},
		{TEXT("StartEngine"), EKeys::I}, {TEXT("StartEngine"), EKeys::Gamepad_DPad_Left},
		{TEXT("CycleCamera"), EKeys::C}, {TEXT("CycleCamera"), EKeys::Gamepad_FaceButton_Top},
		{TEXT("CycleHud"), EKeys::F2}, {TEXT("CycleHud"), EKeys::Gamepad_FaceButton_Left},
		{TEXT("ToggleAtlas"), EKeys::Tab}, {TEXT("ToggleAtlas"), EKeys::Gamepad_Special_Right},
		{TEXT("FindPlace"), EKeys::Slash},
		{TEXT("Recover"), EKeys::R}, {TEXT("Recover"), EKeys::Gamepad_Special_Left},
		{TEXT("ToggleSound"), EKeys::M}, {TEXT("ToggleLights"), EKeys::L}, {TEXT("CycleLens"), EKeys::F6}, {TEXT("ToggleStats"), EKeys::F3},
	};
	for (const FExpect& E : Expected)
	{
		TestNotNull(FString::Printf(TEXT("%s on %s"), E.Action, *E.Key.ToString()), FindMapping(Config, E.Action, E.Key));
	}
	TestEqual(TEXT("one mapping per table row"), Config->DrivingContext->GetMappings().Num(), USimCruiseInputConfig::Bindings().Num());

	// No key drives two actions: a press must mean one thing.
	TMap<FKey, int32> Uses;
	for (const FEnhancedActionKeyMapping& Mapping : Config->DrivingContext->GetMappings())
	{
		Uses.FindOrAdd(Mapping.Key)++;
	}
	for (const TPair<FKey, int32>& Pair : Uses)
	{
		TestEqual(FString::Printf(TEXT("%s bound once"), *Pair.Key.ToString()), Pair.Value, 1);
	}
	TestTrue(TEXT("A and D cancel"), Config->SteerKey->AccumulationBehavior == EInputActionAccumulationBehavior::Cumulative);
	TestTrue(TEXT("stick is an axis"), Config->SteerPad->ValueType == EInputActionValueType::Axis1D);
	TestTrue(TEXT("trigger is an axis"), Config->ThrottlePad->ValueType == EInputActionValueType::Axis1D);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseInputModifierTest, "SimCruise.Gameplay.Input.DeadZonesAndDirections", Flags)
bool FSimCruiseInputModifierTest::RunTest(const FString& Parameters)
{
	USimCruiseInputConfig* Config = USimCruiseInputConfig::Create(GetTransientPackage());
	const FEnhancedActionKeyMapping* Stick = FindMapping(Config, TEXT("SteerPad"), EKeys::Gamepad_LeftX);
	const FEnhancedActionKeyMapping* Trigger = FindMapping(Config, TEXT("ThrottlePad"), EKeys::Gamepad_RightTriggerAxis);
	const FEnhancedActionKeyMapping* KeyA = FindMapping(Config, TEXT("SteerKey"), EKeys::A);
	const FEnhancedActionKeyMapping* KeyD = FindMapping(Config, TEXT("SteerKey"), EKeys::D);
	if (!Stick || !Trigger || !KeyA || !KeyD)
	{
		AddError(TEXT("missing mappings"));
		return false;
	}
	// Godot's 0.12 stick dead zone, rescaled so full travel still reaches 1.
	TestEqual(TEXT("stick inside dead zone"), ApplyModifiers(*Stick, 0.11f), 0.f);
	TestNearlyEqual(TEXT("stick half travel"), ApplyModifiers(*Stick, 0.56f), 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("stick left full"), ApplyModifiers(*Stick, -1.f), -1.f, 1e-4f);
	TestNearlyEqual(TEXT("stick right full"), ApplyModifiers(*Stick, 1.f), 1.f, 1e-4f);
	// Triggers: 0.04 dead zone, linear above it.
	TestEqual(TEXT("trigger rest"), ApplyModifiers(*Trigger, 0.03f), 0.f);
	TestNearlyEqual(TEXT("trigger linear"), ApplyModifiers(*Trigger, 0.52f), 0.5f, 1e-4f);
	TestNearlyEqual(TEXT("trigger full"), ApplyModifiers(*Trigger, 1.f), 1.f, 1e-4f);
	// A steers left (negative), D right.
	TestEqual(TEXT("A is left"), ApplyModifiers(*KeyA, 1.f), -1.f);
	TestEqual(TEXT("D is right"), ApplyModifiers(*KeyD, 1.f), 1.f);
	return true;
}

// --- Steering shaping -------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSteeringRangeTest, "SimCruise.Gameplay.Steering.RangeMatchesGodot", Flags)
bool FSimCruiseSteeringRangeTest::RunTest(const FString& Parameters)
{
	const FSimCruiseVehicleSetup Setup = Roadster();
	// Powertrain.available_steering: atan(2.266 * 0.95 g / 400) + 5 deg = 8.02 deg at 20 m/s.
	TestNearlyEqual(TEXT("NB at 20 m/s"), FMath::RadiansToDegrees(SimCruiseShaping::AvailableSteeringRad(Setup, 20.0)), 8.02, 0.03);
	TestNearlyEqual(TEXT("parking lock"), FMath::RadiansToDegrees(SimCruiseShaping::AvailableSteeringRad(Setup, 0.0)), 36.6, 1e-6);
	TestTrue(TEXT("range closes with speed"), SimCruiseShaping::AvailableSteeringRad(Setup, 30.0) < SimCruiseShaping::AvailableSteeringRad(Setup, 15.0));
	TestNearlyEqual(TEXT("reverse is symmetric"), SimCruiseShaping::AvailableSteeringRad(Setup, -7.0), SimCruiseShaping::AvailableSteeringRad(Setup, 7.0), 1e-12);
	// Stick centre goes to lane corrections, the ends keep full range.
	const double Small = SimCruiseShaping::ThumbstickCommand(Setup, 0.1, 30.0);
	TestTrue(TEXT("stick centre is gentle at speed"), Small < 0.1 && Small > 0.0);
	TestNearlyEqual(TEXT("full stick is full range"), SimCruiseShaping::ThumbstickCommand(Setup, 1.0, 30.0), 1.0, 1e-12);
	TestNearlyEqual(TEXT("parking: stick is linear"), SimCruiseShaping::ThumbstickCommand(Setup, 0.3, 0.0), 0.3, 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSteeringCentreTest, "SimCruise.Gameplay.Steering.StickAndKeysCentreExactly", Flags)
bool FSimCruiseSteeringCentreTest::RunTest(const FString& Parameters)
{
	FSimCruiseDriverShaper Shaper;
	FSimCruiseRawControls Raw;
	Raw.PadSteer = -0.3;
	TestTrue(TEXT("stick left steers left"), Hold(Shaper, Moving(20.0), Raw, 0.5) < 0.0);
	TestTrue(TEXT("uses the stick map"), Shaper.IsUsingThumbstick());
	Raw.PadSteer = 0.3;
	TestTrue(TEXT("stick right steers right"), Hold(Shaper, Moving(20.0), Raw, 0.6) > 0.0);
	Raw.PadSteer = 0.0;
	TestEqual(TEXT("released stick returns to exactly zero"), Hold(Shaper, Moving(20.0), Raw, 0.5), 0.0);

	Raw.KeySteer = -1.0;
	const double Lock = Hold(Shaper, Moving(0.0), Raw, 1.0);
	TestNearlyEqual(TEXT("A held at rest reaches the parking lock"), Lock, -FMath::DegreesToRadians(36.6), 1e-6);
	Raw.KeySteer = 0.0;
	TestEqual(TEXT("released key returns to exactly zero"), Hold(Shaper, Moving(0.0), Raw, 1.0), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSteeringKeyRateTest, "SimCruise.Gameplay.Steering.KeyTapAtHighwaySpeed", Flags)
bool FSimCruiseSteeringKeyRateTest::RunTest(const FString& Parameters)
{
	// Powertrain.gd: a half-second tap at 60 mph reaches about 6 deg, not the lock.
	FSimCruiseDriverShaper Shaper;
	FSimCruiseRawControls Raw;
	Raw.KeySteer = 1.0;
	const double Rack = FMath::RadiansToDegrees(Hold(Shaper, Moving(26.8), Raw, 0.5));
	TestNearlyEqual(TEXT("0.5 s tap at 60 mph"), Rack, 6.0, 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseCountersteerTest, "SimCruise.Gameplay.Steering.CountersteerAllowance", Flags)
bool FSimCruiseCountersteerTest::RunTest(const FString& Parameters)
{
	const FSimCruiseVehicleSetup Setup = Roadster();
	// A right-hand corner whose rear has stepped out to the left: the car moves
	// left of where its nose points, the front axle 15 deg left of the nose.
	const double Beta = FMath::DegreesToRadians(-15.0);
	const FSimCruiseDriveTelemetry Slide = Moving(20.0, 20.0 * FMath::Tan(Beta), 0.0);
	TestNearlyEqual(TEXT("beta_f"), SimCruiseShaping::FrontAxleSlipAngleRad(Setup, 20.0, Slide.BodyVelocityMps.Y, 0.0), Beta, 1e-9);

	FSimCruiseDriverShaper Shaper;
	FSimCruiseRawControls Raw;
	Raw.PadSteer = -1.0;
	FSimCruiseDriverCommand Command;
	const double Counter = Hold(Shaper, Slide, Raw, 0.6, &Command);
	// |beta_f| + 5 deg surplus = 20 deg, reachable at the 40 deg/s countersteer rate.
	TestNearlyEqual(TEXT("full opposite lock follows the slide"), FMath::RadiansToDegrees(Counter), -20.0, 0.01);
	TestNearlyEqual(TEXT("range toward the slide"), FMath::RadiansToDegrees(Shaper.GetRangeLeftRad()), 20.0, 1e-6);
	TestNearlyEqual(TEXT("range away from it is unchanged"), FMath::RadiansToDegrees(Shaper.GetRangeRightRad()), 8.02, 0.03);

	FSimCruiseDriverShaper Other;
	Raw.PadSteer = 1.0;
	const double Into = Hold(Other, Slide, Raw, 1.0);
	TestNearlyEqual(TEXT("steering into the slide keeps the normal cap"), FMath::RadiansToDegrees(Into), 8.02, 0.03);

	// The slide ends: the rack unwinds at the return rate, never jumps.
	double LargestStep = 0.0;
	Raw.PadSteer = -1.0;
	const double After = Hold(Shaper, Moving(20.0), Raw, 1.0, nullptr, &LargestStep);
	TestNearlyEqual(TEXT("back to the normal cap"), FMath::RadiansToDegrees(After), -8.02, 0.03);
	const double ReturnPerStep = SimCruiseShaping::PadReturnRatePerS * SimCruiseShaping::AvailableSteeringRad(Setup, 20.0) / 120.0;
	TestTrue(TEXT("closing allowance is slewed"), LargestStep <= ReturnPerStep + 1e-9);

	// No allowance below 5 m/s or in reverse, where the angle is noise.
	TestEqual(TEXT("slow"), SimCruiseShaping::FrontAxleSlipAngleRad(Setup, 4.0, -3.0, 0.0), 0.0);
	TestEqual(TEXT("reverse"), SimCruiseShaping::FrontAxleSlipAngleRad(Setup, -10.0, -3.0, 0.0), 0.0);
	// A yaw rate alone moves the front axle sideways: r * a.
	TestNearlyEqual(TEXT("yaw contributes"), SimCruiseShaping::FrontAxleSlipAngleRad(Setup, 20.0, 0.0, 1.0), FMath::Atan2(Setup.CenterOfMassToFrontAxleM, 20.0), 1e-12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePedalTest, "SimCruise.Gameplay.Steering.PedalRamps", Flags)
bool FSimCruisePedalTest::RunTest(const FString& Parameters)
{
	FSimCruiseDriverShaper Shaper;
	FSimCruiseRawControls Raw;
	FSimCruiseDriverCommand Command;
	Raw.bKeyBrake = true;
	Raw.bKeyThrottle = true;
	Hold(Shaper, Moving(20.0), Raw, 0.3, &Command);
	TestNearlyEqual(TEXT("0.3 s brake tap reaches 0.75"), Command.Brake, 0.75f, 0.01f);
	TestNearlyEqual(TEXT("0.3 s throttle tap reaches 0.9"), Command.Throttle, 0.9f, 0.01f);
	Raw.bKeyBrake = false;
	Raw.bKeyThrottle = false;
	Hold(Shaper, Moving(20.0), Raw, 0.125, &Command);
	TestEqual(TEXT("released within 1/8 s"), Command.Brake, 0.f);

	FSimCruiseDriverShaper Pad;
	FSimCruiseRawControls Trigger;
	Trigger.PadThrottle = 0.37;
	Trigger.PadBrake = 0.81;
	Hold(Pad, Moving(10.0), Trigger, 1.0 / 120.0, &Command);
	TestNearlyEqual(TEXT("trigger throttle follows the pedal curve"), Command.Throttle,
		static_cast<float>(FMath::Pow(0.37, SimCruiseShaping::DefaultTriggerGamma)), 1e-6f);
	TestNearlyEqual(TEXT("half trigger is light part throttle"), static_cast<float>(SimCruiseShaping::TriggerThrottle(0.5, SimCruiseShaping::DefaultTriggerGamma)), 0.287f, 0.002f);
	TestEqual(TEXT("full trigger is full throttle"), SimCruiseShaping::TriggerThrottle(1.0, SimCruiseShaping::DefaultTriggerGamma), 1.0);
	TestNearlyEqual(TEXT("trigger brake passes straight through"), Command.Brake, 0.81f, 1e-6f);

	Trigger.bClutch = true;
	Hold(Pad, Moving(10.0), Trigger, 0.1, &Command);
	TestEqual(TEXT("clutch pressed in 0.1 s"), Command.Clutch, 1.f);
	Trigger.bClutch = false;
	Hold(Pad, Moving(10.0), Trigger, 0.2, &Command);
	TestNearlyEqual(TEXT("clutch released slowly (2.8/s)"), Command.Clutch, 0.44f, 0.01f);
	return true;
}

// --- Haptics ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHapticsTest, "SimCruise.Gameplay.Haptics.SlipRumble", Flags)
bool FSimCruiseHapticsTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseHaptics;
	const FSimCruiseCarDefinition Car;
	TestNearlyEqual(TEXT("NB plateau angle (Haptics.gd: 10.6 deg)"), FMath::RadiansToDegrees(PlateauSlipAngleRad(Car)), 10.6, 0.15);

	TestEqual(TEXT("no feel below the onset"), SlipFeel(0.75), 0.0);
	TestNearlyEqual(TEXT("limit share at the plateau end"), SlipFeel(1.15), 0.65, 1e-12);
	TestNearlyEqual(TEXT("full deep in a slide"), SlipFeel(2.5), 1.0, 1e-12);
	double Previous = 0.0;
	bool bMonotonic = true;
	for (int32 I = 0; I <= 400; ++I)
	{
		const double F = SlipFeel(I * 0.01);
		bMonotonic &= F + 1e-12 >= Previous;
		Previous = F;
	}
	TestTrue(TEXT("never falls as slip grows"), bMonotonic);

	TestEqual(TEXT("ABS pulse on"), AbsPulse(true, 0.01), AbsOn);
	TestEqual(TEXT("ABS pulse off half a cycle later (8 Hz)"), AbsPulse(true, 0.07), 0.0);
	TestEqual(TEXT("no pulse without ABS"), AbsPulse(false, 0.01), 0.0);

	// Coasting straight on a smooth road: silence.
	FSimCruiseDriveTelemetry Coast = Moving(25.0);
	for (FSimCruiseDriveWheelTelemetry& Wheel : Coast.Wheels)
	{
		Wheel.bContact = true;
		Wheel.NormalLoadN = 2500.0;
		Wheel.PeakSlipAngleRad = PlateauSlipAngleRad(Car);
	}
	FSimCruiseHapticsModel Model;
	TestTrue(TEXT("coasting is quiet"), Model.Sample(Coast, 0.0, 0.0, 1.0 / 60.0).IsQuiet());

	// Understeer: front tyres past the plateau buzz the high-frequency motor.
	FSimCruiseDriveTelemetry Push = Coast;
	Push.Wheels[0].SlipAngleRad = Push.Wheels[1].SlipAngleRad = 1.5 * PlateauSlipAngleRad(Car);
	const FSimCruiseRumble Under = Model.Sample(Push, 0.0, 0.0, 1.0 / 60.0);
	TestTrue(TEXT("understeer is the weak motor"), Under.HighFrequency > 0.6f && Under.LowFrequency == 0.f);

	// Oversteer and wheelspin: the rear, low-frequency motor and the right trigger.
	FSimCruiseDriveTelemetry Spin = Coast;
	Spin.Wheels[2].bDriven = Spin.Wheels[3].bDriven = true;
	Spin.Wheels[2].SlipRatio = Spin.Wheels[3].SlipRatio = 0.25;
	const FSimCruiseRumble Over = Model.Sample(Spin, 1.0, 0.0, 1.0 / 60.0);
	TestTrue(TEXT("wheelspin is the strong motor"), Over.LowFrequency > 0.6f && Over.HighFrequency == 0.f);
	TestTrue(TEXT("wheelspin in the throttle trigger"), Over.RightTrigger > 0.4f && Over.LeftTrigger == 0.f);

	// Parked after a slide: a relaxed slip angle left on the tyres is not
	// scrub, but wheelspin from standstill is felt.
	FSimCruiseDriveTelemetry Parked = Coast;
	Parked.BodyVelocityMps = FVector(0.05, -0.05, 0.0);
	for (FSimCruiseDriveWheelTelemetry& Wheel : Parked.Wheels) { Wheel.SlipAngleRad = 0.6; }
	FSimCruiseHapticsModel ParkedModel;
	TestTrue(TEXT("a stopped car does not scrub"), ParkedModel.Sample(Parked, 0.0, 0.0, 1.0 / 60.0).IsQuiet());
	FSimCruiseDriveTelemetry Launch = Parked;
	for (FSimCruiseDriveWheelTelemetry& Wheel : Launch.Wheels) { Wheel.SlipAngleRad = 0.0; }
	Launch.Wheels[2].bDriven = Launch.Wheels[3].bDriven = true;
	Launch.Wheels[2].SlipRatio = Launch.Wheels[3].SlipRatio = 0.5;
	TestTrue(TEXT("wheelspin from standstill is felt"), ParkedModel.Sample(Launch, 1.0, 0.0, 1.0 / 60.0).LowFrequency > 0.6f);

	// ABS acting: the pulse, in the strong motor and the brake trigger.
	FSimCruiseDriveTelemetry Abs = Coast;
	Abs.bServiceBrakeOn = true;
	for (FSimCruiseDriveWheelTelemetry& Wheel : Abs.Wheels) { Wheel.bAbsActive = true; Wheel.SlipRatio = -0.1; }
	FSimCruiseHapticsModel AbsModel;
	const FSimCruiseRumble Pulse = AbsModel.Sample(Abs, 0.0, 1.0, 0.01);
	TestTrue(TEXT("ABS pulse felt"), AbsModel.IsAbsActive() && Pulse.LowFrequency >= 0.6f && Pulse.LeftTrigger >= 0.6f);

	// Backends: XInput drives max(Large) left and max(Small) right; GameInput has four motors.
	FSimCruiseRumble R;
	R.LowFrequency = 0.8f; R.HighFrequency = 0.3f; R.LeftTrigger = 0.5f; R.RightTrigger = 0.2f;
	const FForceFeedbackValues X = ToChannels(R, ESimCruiseRumbleBackend::XInput);
	TestTrue(TEXT("XInput low motor"), FMath::Max(X.LeftLarge, X.RightLarge) == 0.8f);
	TestTrue(TEXT("XInput high motor"), FMath::Max(X.LeftSmall, X.RightSmall) == 0.3f);
	const FForceFeedbackValues GI = ToChannels(R, ESimCruiseRumbleBackend::GameInput);
	TestTrue(TEXT("GameInput four motors"), GI.LeftLarge == 0.8f && GI.RightLarge == 0.3f && GI.LeftSmall == 0.5f && GI.RightSmall == 0.2f);
	return true;
}

// --- Cameras ----------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHeadModelTest, "SimCruise.Gameplay.Camera.HeadModel", Flags)
bool FSimCruiseHeadModelTest::RunTest(const FString& Parameters)
{
	const FSimCruiseVehicleSetup Setup = Roadster();
	const double G = SimCruiseShaping::StandardGravity;
	const double Dt = 1.0 / 60.0;

	// Steady 0.5 g left turn: the head leans out to the right, 1.5 cm.
	{
		FSimCruiseHeadModel Head;
		FVector Velocity(20.0, 0.0, 0.0);
		for (int32 I = 0; I < 180; ++I)
		{
			Velocity += FVector(0.0, -0.5 * G, 0.0) * Dt;
			Head.Update(FTransform::Identity, Velocity, Setup, Moving(20.0), Dt);
		}
		TestNearlyEqual(TEXT("3 cm per g lateral"), Head.GetHeadOffsetM().Y, 0.015, 0.0005);
	}
	// 0.8 g braking: the head moves 1.6 cm forward and the gaze dips 1.2 deg.
	{
		FSimCruiseHeadModel Head;
		FVector Velocity(30.0, 0.0, 0.0);
		for (int32 I = 0; I < 120; ++I)
		{
			Velocity += FVector(-0.8 * G, 0.0, 0.0) * Dt;
			Head.Update(FTransform::Identity, Velocity, Setup, Moving(Velocity.X), Dt);
		}
		TestNearlyEqual(TEXT("2 cm per g longitudinal"), Head.GetHeadOffsetM().X, 0.016, 0.0005);
		TestNearlyEqual(TEXT("brake dip"), Head.GetBrakePitchDeg(), 1.2, 0.02);
	}
	// Horizon: a 10 deg body roll shows as 7 deg.
	{
		FSimCruiseHeadModel Head;
		Head.HeadMotion = 0.0;
		const FTransform Rolled(FRotator(0.0, 0.0, 10.0));
		const FSimCruiseEyePose Pose = Head.Update(Rolled, FVector::ZeroVector, Setup, Moving(0.0), Dt);
		TestNearlyEqual(TEXT("70 % of the roll"), Pose.ToRotator().Roll, 7.0, 0.05);
		TestTrue(TEXT("head motion 0 welds the eye"), Pose.Location.Equals(Rolled.TransformPosition(Setup.DriverEyeLocalCm), 1e-6));
	}
	// Lens: natural 45 deg vertical is 72.9 deg across a 16:9 screen.
	TestNearlyEqual(TEXT("natural lens"), SimCruiseLens::HorizontalFromVerticalDeg(SimCruiseLens::VerticalFovDeg(ESimCruiseLens::Natural, 0, 0), 16.0 / 9.0), 72.74, 0.05);
	TestNearlyEqual(TEXT("screen lens, 15.6 in at 60 cm"), SimCruiseLens::VerticalFovDeg(ESimCruiseLens::Screen, 19.4, 60.0), 18.37, 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseChaseFrameTest, "SimCruise.Gameplay.Camera.ChaseShowsSlip", Flags)
bool FSimCruiseChaseFrameTest::RunTest(const FString& Parameters)
{
	bool bHold = false;
	const double Beta = FMath::DegreesToRadians(15.0);
	const FVector Drift(20.0, 20.0 * FMath::Tan(Beta), 0.0);
	TestNearlyEqual(TEXT("swing is 0.4 beta"), FSimCruiseChaseFrame::TargetSwingRad(Drift, bHold), 0.4 * Beta, 1e-9);
	TestNearlyEqual(TEXT("beta clamps at 35 deg"), FSimCruiseChaseFrame::TargetSwingRad(FVector(10.0, 30.0, 0.0), bHold), 0.4 * FMath::DegreesToRadians(35.0), 1e-9);
	TestEqual(TEXT("reversing never swings"), FSimCruiseChaseFrame::TargetSwingRad(FVector(-5.0, 0.5, 0.0), bHold), 0.0);
	TestFalse(TEXT("reversing does not hold"), bHold);
	FSimCruiseChaseFrame::TargetSwingRad(FVector(-1.0, 8.0, 0.0), bHold);
	TestTrue(TEXT("a spin holds the last swing"), bHold);
	TestEqual(TEXT("walking pace: no swing"), FSimCruiseChaseFrame::TargetSwingRad(FVector(1.5, 1.0, 0.0), bHold), 0.0);

	FSimCruiseChaseFrame Frame;
	FVector Location;
	FVector LookAt;
	Frame.Update(ESimCruiseView::Chase, FTransform::Identity, FVector(20.0, 0.0, 0.0), 1.0 / 60.0, true, Location, LookAt);
	TestNearlyEqual(TEXT("chase behind"), Location.X, -580.0, 1e-6);
	TestNearlyEqual(TEXT("chase height"), Location.Z, 194.0, 1e-6);
	for (int32 I = 0; I < 180; ++I)
	{
		Frame.Update(ESimCruiseView::Chase, FTransform::Identity, Drift, 1.0 / 60.0, false, Location, LookAt);
	}
	TestNearlyEqual(TEXT("steady drift: camera yaw - body yaw = 0.4 beta"), Frame.GetSwingRad(), 0.4 * Beta, 1e-4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHeadFrameRateTest, "SimCruise.Gameplay.Camera.HeadModelFrameRate", Flags)
bool FSimCruiseHeadFrameRateTest::RunTest(const FString& Parameters)
{
	// The same 0.5 g turn must lean the head the same at 30, 60, 144 and
	// 360 fps: the felt acceleration is differentiated over the real frame.
	const FSimCruiseVehicleSetup Setup = Roadster();
	const double G = SimCruiseShaping::StandardGravity;
	for (const double Fps : {30.0, 60.0, 144.0, 360.0})
	{
		FSimCruiseHeadModel Head;
		FVector Velocity(20.0, 0.0, 0.0);
		const double Dt = 1.0 / Fps;
		const int32 Frames = FMath::RoundToInt(3.0 * Fps);
		for (int32 I = 0; I < Frames; ++I)
		{
			Velocity += FVector(0.0, -0.5 * G, 0.0) * Dt;
			Head.Update(FTransform::Identity, Velocity, Setup, Moving(20.0), Dt);
		}
		TestNearlyEqual(FString::Printf(TEXT("0.5 g lean at %.0f fps"), Fps), Head.GetHeadOffsetM().Y, 0.015, 0.0005);
		TestNearlyEqual(FString::Printf(TEXT("felt 0.5 g at %.0f fps"), Fps), Head.GetFeltSpecificForce().Y / G, -0.5, 0.01);
	}
	FSimCruiseGMeter Meter;
	FVector Velocity(20.0, 0.0, 0.0);
	for (int32 I = 0; I < 3 * 360; ++I)
	{
		Velocity += FVector(-0.8 * G, 0.0, 0.0) / 360.0;
		Meter.Update(FTransform::Identity, Velocity, 1.0 / 360.0);
	}
	TestNearlyEqual(TEXT("g-meter reads 0.8 g braking at 360 fps"), Meter.Get().X, -0.8, 0.01);
	const FVector2D Held = Meter.Get();
	Meter.Update(FTransform::Identity, Velocity, 0.0);
	TestTrue(TEXT("g-meter holds while paused"), Meter.Get().Equals(Held, 1e-9));
	return true;
}

/**
 * Release bug #4: the asynchronous 360 Hz physics step does not line up with the
 * frames, so a 60 fps frame sees 5, 6 or 7 new steps. Differentiating the velocity
 * over the frame time made the g-meter and the head model jitter by about 17 %;
 * over the physics time they read a steady value.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseCameraPhysicsTimeTest, "SimCruise.Gameplay.Camera.PhysicsTimeNoJitter", Flags)
bool FSimCruiseCameraPhysicsTimeTest::RunTest(const FString& Parameters)
{
	constexpr double G = 9.80665;
	constexpr double StepS = 1.0 / 360.0;
	FSimCruiseGMeter Meter;
	FVector Velocity(20.0, 0.0, 0.0);
	double SimTime = 1.0;
	static const int32 Pattern[] = {6, 5, 7, 6, 6, 5, 7, 0, 12};
	double MinG = 10.0, MaxG = -10.0;
	for (int32 I = 0; I < 360; ++I)
	{
		const int32 Steps = Pattern[I % UE_ARRAY_COUNT(Pattern)];
		Velocity += FVector(-0.8 * G, 0.0, 0.0) * Steps * StepS;
		SimTime += Steps * StepS;
		const FVector2D Reading = Meter.Update(FTransform::Identity, Velocity, 1.0 / 60.0, SimTime);
		if (I > 120)
		{
			MinG = FMath::Min(MinG, Reading.X);
			MaxG = FMath::Max(MaxG, Reading.X);
		}
	}
	AddInfo(FString::Printf(TEXT("g-meter with uneven physics steps per frame: %.4f .. %.4f g"), MinG, MaxG));
	TestNearlyEqual(TEXT("g-meter reads 0.8 g braking"), (MinG + MaxG) * 0.5, -0.8, 0.01);
	TestTrue(TEXT("g-meter steady (spread under 0.01 g)"), MaxG - MinG < 0.01);
	return true;
}

// --- Shell and vehicle together ---------------------------------------------

namespace SimCruiseGameplayTest
{
	struct FSlideResult
	{
		double PeakBodySlipDeg = 0.0;
		double FinalBodySlipDeg = 0.0;
		double PeakRackBeyondCapDeg = 0.0;
		double SteadyYawRate = 0.0;
		double SteadyLateralSpeed = 0.0;
		bool bSpun = false;
	};

	/** How the scripted driver answers a slide with the stick, toward the
	 *  front axle's travel beta_f. Align: aim the wheels along beta_f
	 *  (stick = beta_f / (|beta_f| + surplus)). Snap: full lock from 6 deg. */
	enum class ECatch : uint8 { Align, Snap };

	/** Drive the placeholder through the real shaper at 120 Hz frames: reach
	 *  20 m/s, turn right, flick the parking brake 0.15 s, then catch the
	 *  slide. bCapped clamps the rack to the speed cap, the pre-allowance
	 *  behaviour. */
	FSlideResult DriveSlide(UWorld* World, const FSimCruiseCarDefinition& Car, bool bCapped, ECatch Catch)
	{
		FSlideResult Result;
		ASimCruisePlaceholderVehicle* Vehicle = World->SpawnActor<ASimCruisePlaceholderVehicle>();
		Vehicle->SetCarDefinition(Car);
		Vehicle->ResetVehicle(FTransform::Identity);
		const FSimCruiseVehicleSetup Setup = Vehicle->GetVehicleSetup();
		FSimCruiseDriverShaper Shaper;
		FSimCruiseDriveTelemetry T;
		constexpr double Frame = 1.0 / 120.0;
		auto Step = [&](const FSimCruiseRawControls& Raw)
		{
			Vehicle->GetVehicleTelemetry(T);
			FSimCruiseDriverCommand Command = Shaper.Update(Setup, T, Raw, Frame);
			const double Cap = SimCruiseShaping::AvailableSteeringRad(Setup, T.BodyVelocityMps.X);
			if (bCapped)
			{
				Command.RackAngleRad = FMath::Clamp(Command.RackAngleRad, -Cap, Cap);
			}
			Result.PeakRackBeyondCapDeg = FMath::Max(Result.PeakRackBeyondCapDeg, FMath::RadiansToDegrees(FMath::Abs(Command.RackAngleRad) - Cap));
			Vehicle->ApplyDriverCommand(Command);
			Vehicle->StepPhysics(Frame * 0.5);
			Vehicle->StepPhysics(Frame * 0.5);
		};
		FSimCruiseRawControls Raw;
		Raw.PadThrottle = 1.0;
		for (int32 I = 0; I < 20 * 120 && Vehicle->GetForwardSpeedMps() < 20.0; ++I) { Step(Raw); }
		Raw.PadThrottle = 0.15;
		Raw.PadSteer = 0.5;
		for (int32 I = 0; I < 120; ++I) { Step(Raw); }
		Result.SteadyYawRate = Vehicle->GetYawRateRadPerS();
		Result.SteadyLateralSpeed = Vehicle->GetLateralSpeedMps();
		Raw.PadThrottle = 0.0;
		Raw.bParkingBrake = true;
		for (int32 I = 0; I < 18; ++I) { Step(Raw); }
		Raw.bParkingBrake = false;
		for (int32 I = 0; I < 3 * 120; ++I)
		{
			const double BetaFront = SimCruiseShaping::FrontAxleSlipAngleRad(Setup, Vehicle->GetForwardSpeedMps(), Vehicle->GetLateralSpeedMps(), Vehicle->GetYawRateRadPerS());
			Raw.PadSteer = Catch == ECatch::Snap
				? FMath::Clamp(BetaFront / FMath::DegreesToRadians(6.0), -1.0, 1.0)
				: BetaFront / (FMath::Abs(BetaFront) + Setup.SteeringSlipLimitSurplusRad);
			Step(Raw);
			const double BodySlip = FMath::RadiansToDegrees(FMath::Atan2(Vehicle->GetLateralSpeedMps(), FMath::Abs(Vehicle->GetForwardSpeedMps())));
			Result.PeakBodySlipDeg = FMath::Max(Result.PeakBodySlipDeg, FMath::Abs(BodySlip));
			Result.bSpun |= Vehicle->GetForwardSpeedMps() < 0.0 || FMath::Abs(BodySlip) > 90.0;
		}
		Result.FinalBodySlipDeg = FMath::RadiansToDegrees(FMath::Atan2(Vehicle->GetLateralSpeedMps(), FMath::Abs(Vehicle->GetForwardSpeedMps())));
		Vehicle->Destroy();
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSlideCatchTest, "SimCruise.Gameplay.Loop.PlaceholderConventionsAndSlideCatch", Flags)
bool FSimCruiseSlideCatchTest::RunTest(const FString& Parameters)
{
	FSimCruiseCarDefinition Car;
	FString Error;
	if (!FSimCruiseCarDefinitionLoader::LoadById(TEXT("nb1999"), Car, Error))
	{
		AddWarning(FString::Printf(TEXT("nb1999.json not loaded, using the built-in NB defaults: %s"), *Error));
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SimCruiseSlideCatch"));
	const FSlideResult Free = DriveSlide(World, Car, false, ECatch::Align);
	const FSlideResult Capped = DriveSlide(World, Car, true, ECatch::Align);
	const FSlideResult Snap = DriveSlide(World, Car, false, ECatch::Snap);
	const FSlideResult SnapCapped = DriveSlide(World, Car, true, ECatch::Snap);
	World->DestroyWorld(false);

	// Conventions across the seam: the stick right turns the car right (yaw
	// rate positive in Unreal body axes); the shell and the vehicle agree.
	TestTrue(TEXT("stick right yaws right"), Free.SteadyYawRate > 0.05);
	AddInfo(FString::Printf(TEXT("Steady right turn at 20 m/s: yaw rate %.3f rad/s, lateral speed %.3f m/s"), Free.SteadyYawRate, Free.SteadyLateralSpeed));
	auto Report = [this](const TCHAR* Name, const FSlideResult& R)
	{
		AddInfo(FString::Printf(TEXT("%s: peak body slip %.1f deg, final %.1f deg, rack up to %.1f deg past the speed cap, spun %s"),
			Name, R.PeakBodySlipDeg, R.FinalBodySlipDeg, R.PeakRackBeyondCapDeg, R.bSpun ? TEXT("yes") : TEXT("no")));
	};
	Report(TEXT("Align the wheels with the travel, allowance"), Free);
	Report(TEXT("Align the wheels with the travel, rack held to the speed cap"), Capped);
	Report(TEXT("Snap to full lock, allowance"), Snap);
	Report(TEXT("Snap to full lock, rack held to the speed cap"), SnapCapped);
	// The allowance must actually open toward the slide in the loop.
	// A gentle catch never needs it; a snap to full lock opens it (and, on
	// this uncalibrated placeholder, overcorrects into a spin the capped rack
	// prevents: the plan 04 SlideCatch gate belongs to the real vehicle).
	TestTrue(TEXT("a full-lock countersteer goes past the speed cap"), Snap.PeakRackBeyondCapDeg > 1.0);
	TestTrue(TEXT("the capped runs stay within the cap"), Capped.PeakRackBeyondCapDeg < 1e-6 && SnapCapped.PeakRackBeyondCapDeg < 1e-6);
	return true;
}

// --- HUD --------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHudRulesTest, "SimCruise.Gameplay.Hud.Levels", Flags)
bool FSimCruiseHudRulesTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseHudRules;
	TestTrue(TEXT("Off -> Glance"), Next(ESimCruiseHudLevel::Off) == ESimCruiseHudLevel::Glance);
	TestTrue(TEXT("Glance -> Full"), Next(ESimCruiseHudLevel::Glance) == ESimCruiseHudLevel::Full);
	TestTrue(TEXT("Full -> Off"), Next(ESimCruiseHudLevel::Full) == ESimCruiseHudLevel::Off);
	TestEqual(TEXT("reverse"), GearText(-1, false), FString(TEXT("R")));
	TestEqual(TEXT("neutral"), GearText(0, true), FString(TEXT("N")));
	TestEqual(TEXT("automatic third"), GearText(3, false), FString(TEXT("D3")));
	TestEqual(TEXT("manual third"), GearText(3, true), FString(TEXT("3")));
	float Alpha = 1.f;
	for (int32 I = 0; I < 120; ++I) { Alpha = GlanceAlpha(Alpha, false, 1.f / 60.f); }
	TestNearlyEqual(TEXT("glance fades while cruising"), Alpha, GlanceFaded, 0.02f);
	for (int32 I = 0; I < 120; ++I) { Alpha = GlanceAlpha(Alpha, true, 1.f / 60.f); }
	TestNearlyEqual(TEXT("and wakes"), Alpha, 1.f, 0.02f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
