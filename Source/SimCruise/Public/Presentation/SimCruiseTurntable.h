// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "SimCruiseTurntable.generated.h"

class ACameraActor;
class ASimCruiseCarPresentationActor;
class UStaticMeshComponent;

/** One turntable view: a photographer's position around the car, or the driver's seat. */
struct FSimCruiseTurntableView
{
	FString Name;
	/** Camera azimuth around the car, degrees from the nose toward the right side (Unreal +Y). */
	double AzimuthDeg = 0.0;
	double CameraHeightM = 1.2;
	double HorizontalFovDeg = 50.0;
	/** Look at the front-left hub instead of the car centre, from this distance (m), when > 0. */
	double WheelCloseupM = 0.0;
	bool bDriver = false;
	/** Road-wheel steer angle shown in this view (degrees, + left). */
	double SteerDeg = 0.0;
	bool bBrake = false;
};

/**
 * Turntable capture: lights a neutral outdoor stage (physical sun and sky,
 * real-time sky light, asphalt ground) and photographs each car from eight
 * angles plus the driver's view, a wheel close-up and steered/braking views,
 * at the highest scalability. Screenshots and a report land in the output
 * folder (default Saved/Screenshots/Turntable).
 *
 * Run: UnrealEditor.exe SimCruise.uproject /Engine/Maps/Entry?game=/Script/SimCruise.SimCruiseTurntableGameMode
 *      -game -windowed -ResX=1600 -ResY=900 -SimCruiseTurntable=all [-SimCruiseTurntableOut=DIR]
 * or the console command SimCruise.Turntable [ids|all] [dir] in any world.
 */
UCLASS()
class SIMCRUISE_API ASimCruiseTurntable : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseTurntable();

	void Configure(const TArray<FString>& InCarIds, const FString& InOutputDir, bool bInQuitWhenDone);

	static TArray<FSimCruiseTurntableView> DefaultViews();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BuildStage();
	void StartCar();
	void PoseView();
	void Finish();

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY(Transient)
	TObjectPtr<ASimCruiseCarPresentationActor> Car;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> StageActors;

	TArray<FString> CarIds;
	TArray<FSimCruiseTurntableView> Views;
	FString OutputDir;
	bool bQuitWhenDone = false;
	int32 CarIndex = -1;
	int32 ViewIndex = 0;
	int32 FramesInView = 0;
	bool bShotRequested = false;
	FString PendingShot;
	double FrameTimeSum = 0.0;
	int32 FrameTimeCount = 0;
	FString Report;
	bool bDone = false;
};

/** Game mode for the turntable map: no pawn, starts the turntable from the command line. */
UCLASS()
class SIMCRUISE_API ASimCruiseTurntableGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimCruiseTurntableGameMode();
	virtual void StartPlay() override;
};
