// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"
#include "SimCruiseInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

/** One binding of the driving controls (docs/controls.md). The table is the
 *  single source for the mapping context, the automation test and the docs. */
struct FSimCruiseBindingSpec
{
	FName Action;
	FKey Key;
	/** Axial dead zone lower threshold (0 = none); the remaining travel is rescaled to 0..1. */
	float DeadZone = 0.f;
	/** Negate the value (A and Left arrow steer left). */
	bool bNegate = false;
};

/**
 * Enhanced Input objects built in code, so the driving controls need no
 * binary .uasset and merge as text. Analog channels (left stick, triggers)
 * are separate actions from their keyboard equivalents because the shell
 * shapes them differently: a thumb gets the speed-aware stick map, a key gets
 * a hand's rack rate and a foot's pedal ramp.
 */
UCLASS()
class SIMCRUISE_API USimCruiseInputConfig : public UObject
{
	GENERATED_BODY()

public:
	/** Build every action and the driving mapping context. */
	static USimCruiseInputConfig* Create(UObject* Outer);

	/** The Godot-compatible bindings, in docs/controls.md order. */
	static const TArray<FSimCruiseBindingSpec>& Bindings();

	UInputAction* FindAction(FName Name) const;

	UPROPERTY() TObjectPtr<UInputMappingContext> DrivingContext;

	// Analog, Axis1D.
	UPROPERTY() TObjectPtr<UInputAction> SteerPad;
	UPROPERTY() TObjectPtr<UInputAction> SteerKey;
	UPROPERTY() TObjectPtr<UInputAction> ThrottlePad;
	UPROPERTY() TObjectPtr<UInputAction> BrakePad;
	// Held, Boolean.
	UPROPERTY() TObjectPtr<UInputAction> ThrottleKey;
	UPROPERTY() TObjectPtr<UInputAction> BrakeKey;
	UPROPERTY() TObjectPtr<UInputAction> FullBrake;
	UPROPERTY() TObjectPtr<UInputAction> ParkingBrake;
	UPROPERTY() TObjectPtr<UInputAction> Clutch;
	// Pressed once, Boolean.
	UPROPERTY() TObjectPtr<UInputAction> ToggleTransmission;
	UPROPERTY() TObjectPtr<UInputAction> ShiftUp;
	UPROPERTY() TObjectPtr<UInputAction> ShiftDown;
	UPROPERTY() TObjectPtr<UInputAction> StartEngine;
	UPROPERTY() TObjectPtr<UInputAction> CycleCamera;
	UPROPERTY() TObjectPtr<UInputAction> CycleHud;
	UPROPERTY() TObjectPtr<UInputAction> ToggleAtlas;
	UPROPERTY() TObjectPtr<UInputAction> FindPlace;
	UPROPERTY() TObjectPtr<UInputAction> Recover;
	UPROPERTY() TObjectPtr<UInputAction> ToggleSound;
	UPROPERTY() TObjectPtr<UInputAction> ToggleLights;
	UPROPERTY() TObjectPtr<UInputAction> CycleLens;
	UPROPERTY() TObjectPtr<UInputAction> ToggleStats;

private:
	UPROPERTY() TMap<FName, TObjectPtr<UInputAction>> ActionsByName;
};
