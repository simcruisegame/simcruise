// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseInputConfig.h"

#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

namespace
{
	// Godot Drive.gd dead zones: 0.04 on both triggers, 0.12 on the stick's X.
	constexpr float TriggerDeadZone = 0.04f;
	constexpr float StickDeadZone = 0.12f;

	struct FActionSpec
	{
		const TCHAR* Name;
		EInputActionValueType Type;
		TObjectPtr<UInputAction> USimCruiseInputConfig::* Member;
	};
}

const TArray<FSimCruiseBindingSpec>& USimCruiseInputConfig::Bindings()
{
	static const TArray<FSimCruiseBindingSpec> Table = {
		{TEXT("ThrottlePad"), EKeys::Gamepad_RightTriggerAxis, TriggerDeadZone},
		{TEXT("ThrottleKey"), EKeys::W},
		{TEXT("ThrottleKey"), EKeys::Up},
		{TEXT("BrakePad"), EKeys::Gamepad_LeftTriggerAxis, TriggerDeadZone},
		{TEXT("BrakeKey"), EKeys::S},
		{TEXT("BrakeKey"), EKeys::Down},
		{TEXT("SteerPad"), EKeys::Gamepad_LeftX, StickDeadZone},
		{TEXT("SteerKey"), EKeys::A, 0.f, true},
		{TEXT("SteerKey"), EKeys::Left, 0.f, true},
		{TEXT("SteerKey"), EKeys::D},
		{TEXT("SteerKey"), EKeys::Right},
		{TEXT("FullBrake"), EKeys::SpaceBar},
		{TEXT("ParkingBrake"), EKeys::H},
		// Release bug #10: the clutch was on LB, the same finger as the brake trigger, so a
		// downshift under braking needed one finger in two places. The right thumb now holds
		// the clutch (A) or the parking brake (B) while the bumpers change gear like paddles.
		{TEXT("ParkingBrake"), EKeys::Gamepad_FaceButton_Right},
		{TEXT("ToggleTransmission"), EKeys::T},
		// The right stick does nothing while driving, so its click is free and never pressed by accident.
		{TEXT("ToggleTransmission"), EKeys::Gamepad_RightThumbstick},
		{TEXT("Clutch"), EKeys::LeftShift},
		{TEXT("Clutch"), EKeys::RightShift},
		{TEXT("Clutch"), EKeys::Gamepad_FaceButton_Bottom},
		{TEXT("ShiftUp"), EKeys::E},
		{TEXT("ShiftUp"), EKeys::Gamepad_RightShoulder},
		{TEXT("ShiftDown"), EKeys::Q},
		{TEXT("ShiftDown"), EKeys::Gamepad_LeftShoulder},
		{TEXT("StartEngine"), EKeys::I},
		{TEXT("StartEngine"), EKeys::Gamepad_DPad_Left},
		{TEXT("CycleCamera"), EKeys::C},
		{TEXT("CycleCamera"), EKeys::Gamepad_FaceButton_Top},
		{TEXT("CycleHud"), EKeys::F2},
		// Not on L3: steering hard presses the stick by accident.
		{TEXT("CycleHud"), EKeys::Gamepad_FaceButton_Left},
		{TEXT("ToggleStats"), EKeys::F3},
		{TEXT("CycleLens"), EKeys::F6},
		{TEXT("ToggleSound"), EKeys::M},
		{TEXT("ToggleLights"), EKeys::L},
		{TEXT("ToggleAtlas"), EKeys::Tab},
		{TEXT("ToggleAtlas"), EKeys::Gamepad_Special_Right},
		{TEXT("FindPlace"), EKeys::Slash},
		{TEXT("Recover"), EKeys::R},
		{TEXT("Recover"), EKeys::Gamepad_Special_Left},
		// MapHUD.gd: the D-pad zooms the minimap.
		{TEXT("MinimapIn"), EKeys::Gamepad_DPad_Up},
		{TEXT("MinimapIn"), EKeys::Equals},
		{TEXT("MinimapOut"), EKeys::Gamepad_DPad_Down},
		{TEXT("MinimapOut"), EKeys::Hyphen},
		{TEXT("Tuning"), EKeys::Gamepad_DPad_Right},
		{TEXT("Tuning"), EKeys::F4},
	};
	return Table;
}

USimCruiseInputConfig* USimCruiseInputConfig::Create(UObject* Outer)
{
	USimCruiseInputConfig* Config = NewObject<USimCruiseInputConfig>(Outer, TEXT("SimCruiseInputConfig"));

	static const FActionSpec Actions[] = {
		{TEXT("SteerPad"), EInputActionValueType::Axis1D, &USimCruiseInputConfig::SteerPad},
		{TEXT("SteerKey"), EInputActionValueType::Axis1D, &USimCruiseInputConfig::SteerKey},
		{TEXT("ThrottlePad"), EInputActionValueType::Axis1D, &USimCruiseInputConfig::ThrottlePad},
		{TEXT("BrakePad"), EInputActionValueType::Axis1D, &USimCruiseInputConfig::BrakePad},
		{TEXT("ThrottleKey"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ThrottleKey},
		{TEXT("BrakeKey"), EInputActionValueType::Boolean, &USimCruiseInputConfig::BrakeKey},
		{TEXT("FullBrake"), EInputActionValueType::Boolean, &USimCruiseInputConfig::FullBrake},
		{TEXT("ParkingBrake"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ParkingBrake},
		{TEXT("Clutch"), EInputActionValueType::Boolean, &USimCruiseInputConfig::Clutch},
		{TEXT("ToggleTransmission"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ToggleTransmission},
		{TEXT("ShiftUp"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ShiftUp},
		{TEXT("ShiftDown"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ShiftDown},
		{TEXT("StartEngine"), EInputActionValueType::Boolean, &USimCruiseInputConfig::StartEngine},
		{TEXT("CycleCamera"), EInputActionValueType::Boolean, &USimCruiseInputConfig::CycleCamera},
		{TEXT("CycleHud"), EInputActionValueType::Boolean, &USimCruiseInputConfig::CycleHud},
		{TEXT("ToggleAtlas"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ToggleAtlas},
		{TEXT("FindPlace"), EInputActionValueType::Boolean, &USimCruiseInputConfig::FindPlace},
		{TEXT("Recover"), EInputActionValueType::Boolean, &USimCruiseInputConfig::Recover},
		{TEXT("ToggleSound"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ToggleSound},
		{TEXT("ToggleLights"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ToggleLights},
		{TEXT("CycleLens"), EInputActionValueType::Boolean, &USimCruiseInputConfig::CycleLens},
		{TEXT("ToggleStats"), EInputActionValueType::Boolean, &USimCruiseInputConfig::ToggleStats},
		{TEXT("MinimapIn"), EInputActionValueType::Boolean, &USimCruiseInputConfig::MinimapIn},
		{TEXT("MinimapOut"), EInputActionValueType::Boolean, &USimCruiseInputConfig::MinimapOut},
		{TEXT("Tuning"), EInputActionValueType::Boolean, &USimCruiseInputConfig::Tuning},
	};

	for (const FActionSpec& Spec : Actions)
	{
		UInputAction* Action = NewObject<UInputAction>(Config, FName(FString(TEXT("IA_")) + Spec.Name));
		Action->ValueType = Spec.Type;
		Config->*(Spec.Member) = Action;
		Config->ActionsByName.Add(FName(Spec.Name), Action);
	}
	// A and D held together cancel, as Godot's left - right strengths did
	// (the shaper clamps A + Left arrow back to -1).
	Config->SteerKey->AccumulationBehavior = EInputActionAccumulationBehavior::Cumulative;

	Config->DrivingContext = NewObject<UInputMappingContext>(Config, TEXT("IMC_Driving"));
	for (const FSimCruiseBindingSpec& Binding : Bindings())
	{
		UInputAction* Action = Config->FindAction(Binding.Action);
		check(Action);
		FEnhancedActionKeyMapping& Mapping = Config->DrivingContext->MapKey(Action, Binding.Key);
		if (Binding.DeadZone > 0.f)
		{
			UInputModifierDeadZone* DeadZone = NewObject<UInputModifierDeadZone>(Config->DrivingContext);
			DeadZone->LowerThreshold = Binding.DeadZone;
			DeadZone->UpperThreshold = 1.f;
			DeadZone->Type = EDeadZoneType::Axial;
			Mapping.Modifiers.Add(DeadZone);
		}
		if (Binding.bNegate)
		{
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(Config->DrivingContext);
			Negate->bX = true;
			Negate->bY = false;
			Negate->bZ = false;
			Mapping.Modifiers.Add(Negate);
		}
	}
	return Config;
}

UInputAction* USimCruiseInputConfig::FindAction(FName Name) const
{
	const TObjectPtr<UInputAction>* Found = ActionsByName.Find(Name);
	return Found ? Found->Get() : nullptr;
}
