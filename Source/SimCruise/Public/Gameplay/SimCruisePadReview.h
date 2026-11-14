// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruisePadReview.generated.h"

/**
 * -SimCruisePadReview=<folder>: the menus in the real game with pad buttons only (scripts\pad-review.ps1).
 * The buttons go in as key events through FSlateApplication (ProcessKeyDownEvent and
 * ProcessKeyUpEvent, user 0, and the left stick through ProcessAnalogInputEvent), as an Xbox
 * pad's do, so Slate's focus, navigation and bubbling run as for the player. After each press it
 * logs who has the pad's focus ("PADWALK ...") and each control as "PADNAV REACHED" or
 * "PADNAV UNREACHABLE". Frames before and after the D-pad on the road atlas and "Where to?"
 * (pad-*.png) let the script see whether the D-pad changed the screen. It quits when done, and
 * after 300 s whatever happened.
 */
UCLASS(NotPlaceable)
class SIMCRUISE_API ASimCruisePadReview : public AActor
{
	GENERATED_BODY()

public:
	ASimCruisePadReview();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FStep
	{
		FString Name;
		TFunction<void()> Do;
		float Wait = 0.25f;
	};
	void Plan();
	void Press(const FKey& Key);
	void Stick(const FKey& Axis, float Value);
	void Shot(const FString& Name);
	void LogFocus(const FString& Step);
	void Reach(const FString& Screen, const FString& Control, bool bReached, const FString& How);
	class ASimCruisePlayerController* Player() const;

	FString Dir;
	TArray<FStep> Steps;
	int32 Next = 0;
	double StartS = 0.0;
	double NextAtS = 0.0;
	double PlacedAtS = -1.0;
	int32 Unreachable = 0;
	bool bPlanned = false;
};
