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
 *
 * -SimCruiseRealismPose=name,lat,lon,heading,hfov[+name,...] instead photographs the
 * world from each pose (eye 1.65 m above the ground, compass heading in degrees,
 * horizontal field of view, the player car hidden) as realism-<name>.png, for
 * lab/realism/score.py to compare with a licensed photograph of the same spot; then it quits.
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
	enum class EStage : uint8 { WaitCar, Settle, Shots, Perf, Realism, Done };

	struct FRealismPose
	{
		FString Name;
		double LatDeg = 0.0;
		double LonDeg = 0.0;
		double HeadingDeg = 0.0;
		float HorizontalFovDeg = 70.f;
		/** Optional sixth field "road": stand on the nearest carriageway within 60 m, looking along it
		 * in the direction nearest the heading (for photographs whose geotag is a few tens of metres off). */
		bool bSnapToRoad = false;
		/** Optional seventh field: camera pitch in degrees (negative looks down). */
		float PitchDeg = 0.f;
	};

	ASimCruisePlayerController* GetPlayer() const;
	void Shoot(const FString& Name, bool bWithHud);
	void Finish();
	void TickRealism(float DeltaSeconds, ASimCruisePlayerController* Player, APawn* Pawn, bool bShadersBusy);

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> TopCamera;

	EStage Stage = EStage::WaitCar;
	FString OutputDir;
	double StageStarted = 0.0;
	float Clock = 0.f;
	/** When the menus' shots began (-SimCruiseShowMenus), 0 before. */
	float MenusAt = 0.f;
	int32 SettleFrames = 0;
	int32 NextShot = 0;

	/** -SimCruiseDrivePerf=high,ultra: frame-time windows per preset and view, parked at the start. */
	TArray<FString> PerfSteps;
	int32 PerfIndex = 0;
	bool bPerfSampling = false;
	bool bTopView = false;

	/** -SimCruiseRealismPose: the poses to photograph, the current one and its camera. */
	TArray<FRealismPose> RealismPoses;
	int32 RealismIndex = 0;
	int32 RealismFrames = 0;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> PoseCamera;
};
