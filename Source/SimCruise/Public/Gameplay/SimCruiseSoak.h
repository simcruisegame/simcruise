// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseSoak.generated.h"

class ASimCruisePlayerController;

/**
 * -SimCruiseSoak=<minutes>: a long unattended drive for release checks (default 20 minutes).
 *
 * Through the real input stack (InjectAxis/InjectKey) it holds about 45 km/h
 * with a slow weave of the stick, so the car meets kerbs, walls, trees and
 * traffic in the streets; when the car is stuck for 6 s it presses the recover
 * key. It changes the camera every 45 s, the car every 4 minutes and travels to
 * the next preset place every 6 minutes (-SimCruiseSoakNoTravel to stay). Every
 * 30 s it logs one "SOAK" line (time, speed, frame rate, memory, recoveries);
 * a non-finite car state is an Error. At the end it logs "SOAK DONE" and quits
 * (unless -SimCruiseKeepOpen).
 */
UCLASS()
class SIMCRUISE_API ASimCruiseSoak : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseSoak();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	double Minutes = 20.0;
	double Started = 0.0;
	double Clock = 0.0;
	double LastReport = 0.0;
	double LastCamera = 0.0;
	double LastCar = 0.0;
	double LastTravel = 0.0;
	double StuckSeconds = 0.0;
	double RecoverHeld = -1.0;
	double CameraHeld = -1.0;
	int32 Recovers = 0;
	int32 Cameras = 0;
	int32 Cars = 0;
	int32 Travels = 0;
	int32 NonFinite = 0;
	int32 Frames = 0;
	double FrameSeconds = 0.0;
	double WorstFrame = 0.0;
	bool bDriving = false;
	bool bDone = false;
	bool bTravel = true;

	ASimCruisePlayerController* GetPlayer() const;
	void Finish();
};
