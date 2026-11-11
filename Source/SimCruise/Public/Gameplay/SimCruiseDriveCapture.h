// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseDriveCapture.generated.h"

class ACameraActor;
class ASimCruisePlayerController;

/**
 * Documentation captures of a real drive (-SimCruiseDriveCapture): once the
 * car is on the start road, the tiles around it are built and the shaders are
 * compiled, it photographs the parked car from the driver's seat, from the
 * chase camera and from straight above the road, then drives off through the
 * real input stack (right trigger) and photographs the moving car from the
 * seat and the chase camera, brakes to a stop and quits.
 *
 * Output: Saved/Screenshots/SimCruise/Drive/<shot>.png (-SimCruiseDriveCaptureOut=DIR
 * changes it) and one log line per shot with speed, gear and the sun.
 * -SimCruiseKeepOpen leaves the game running afterwards.
 */
UCLASS()
class SIMCRUISE_API ASimCruiseDriveCapture : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseDriveCapture();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EStage : uint8 { WaitCar, Settle, Shots, Done };

	ASimCruisePlayerController* GetPlayer() const;
	void Shoot(const FString& Name, bool bWithHud);
	void Finish();

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> TopCamera;

	EStage Stage = EStage::WaitCar;
	FString OutputDir;
	double StageStarted = 0.0;
	float Clock = 0.f;
	int32 SettleFrames = 0;
	int32 NextShot = 0;
	bool bTopView = false;
};
