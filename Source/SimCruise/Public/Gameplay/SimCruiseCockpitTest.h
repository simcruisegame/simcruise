// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseCockpitTest.generated.h"

class ASimCruisePlayerController;
class UStaticMeshComponent;
class USimCruiseCarPresentationComponent;

/** Cockpit limits and measures that need no world, kept apart for the tests. */
namespace SimCruiseCockpitRules
{
	/** A fixed part (dial face, needle pivot, body) may move this much between poses (cm, deg). */
	constexpr double MaxFixedMoveCm = 0.05;
	constexpr double MaxFixedTurnDeg = 0.1;
	/** The steering wheel: off-axis turn and hub motion, and the rim plane's tilt from the column. */
	constexpr double MaxWheelOffAxisDeg = 0.5;
	constexpr double MaxHubMoveCm = 0.1;
	constexpr double MaxRimTiltDeg = 2.0;
	/** The rim angle at full lock may differ from the car data's by this share. */
	constexpr double MaxLockError = 0.05;
	/** Cluster pixels that change by more than 25 of 255 between the centre and full lock (wheel hidden). */
	constexpr double MaxClusterChangedPercent = 1.0;

	/** The angle (deg) of a rotation's part that is not about Axis (both in the same frame). */
	SIMCRUISE_API double OffAxisDeg(const FQuat& Rotation, const FVector& Axis);
	/** The normal of the best plane through the points (least squares); zero with fewer than 3. */
	SIMCRUISE_API FVector PlaneNormal(const TArray<FVector>& Points);
}

/**
 * The cockpit steer test (-SimCruiseCockpitTest): with the car at rest in the driver's seat, it
 * turns the stick to the centre, half and full lock both ways, holds each 2 s, and at each pose
 * reads the world transforms of the steering wheel, the dial faces, the needles and the body, and
 * takes two frames: as the driver sees it, and with the steering wheel hidden. It FAILs when a
 * dial, a needle or the body moves or turns with the steering, when a gauge hangs on the wheel,
 * when the wheel turns about another axis than its column, its hub moves, its rim is tilted on the
 * column, the rim angle at full lock is not the car data's, or the cluster's pixels change between
 * the centre and full lock with the wheel hidden.
 *
 * Output (-SimCruiseCockpitOut=DIR, default <-SimCruisePreflightOut>/cockpit, else
 * Saved/Review/cockpit-<time>): <pose>.png, <pose>-nowheel.png, diff-<pose>.png (cluster change
 * x4, outside the cluster dark), cockpit.json, and one log line
 * COCKPIT PASS|FAIL car=.. lock=.. reasons=... With -SimCruisePreflight the robot drives after it;
 * alone, the game quits (-SimCruiseKeepOpen stays).
 */
UCLASS()
class SIMCRUISE_API ASimCruiseCockpitTest : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseCockpitTest();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool IsFinished() const { return Stage == EStage::Done; }

private:
	enum class EStage : uint8 { WaitReady, Settle, Hold, ShotWheel, ShotNoWheel, Done };

	struct FPartPose
	{
		FString Name;
		FTransform World;
		/** The mesh's local bounds centre, in the world. */
		FVector BoundsCentre = FVector::ZeroVector;
		FString Parent;
	};
	struct FPose
	{
		FString Name;
		float Stick = 0.f;
		double RimDeg = 0.0;
		double WheelDeg = 0.0;
		TArray<FPartPose> Parts;
		TArray<FColor> Pixels;
		TArray<FColor> PixelsNoWheel;
		int32 Width = 0;
		int32 Height = 0;
		FIntRect Cluster;
	};

	void Record(ASimCruisePlayerController& Player, FPose& Pose);
	void RequestShot();
	void OnShot(int32 Width, int32 Height, const TArray<FColor>& Pixels);
	void Judge();
	void SavePng(const FString& Name, int32 Width, int32 Height, const TArray<FColor>& Pixels) const;
	USimCruiseCarPresentationComponent* GetModel(ASimCruisePlayerController* Player) const;
	UStaticMeshComponent* FindPart(USimCruiseCarPresentationComponent* Model, const FString& Prefix) const;

	EStage Stage = EStage::WaitReady;
	double StageStarted = 0.0;
	double Started = 0.0;
	FString OutputDir;
	bool bQuitWhenDone = true;
	int32 PoseIndex = 0;
	TArray<FPose> Poses;
	bool bShotPending = false;
	FDelegateHandle ShotHandle;
	FString CarId;
	double FullLockRoadWheelDeg = 0.0;
	double Ratio = 0.0;
};
