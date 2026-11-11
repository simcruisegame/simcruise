// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimCruiseGameMode.generated.h"

class APlayerController;
class ASimCruiseWorldStreamer;

/**
 * The drivable loop. By default it is a drive on the real place: it starts
 * the world stream at the bundled Miami Beach start (Ocean Drive, offline),
 * waits until the tiles around the start are built and their collision is in
 * the physics scene, then puts the player's car in the right-hand lane of the
 * start road, facing the direction of travel, with the SimCruise controller,
 * camera manager and HUD. Each frame it hands the car's position to the world
 * stream and moves the car with the floating origin.
 *
 * The pawn class comes from config so the vehicle stream can plug its own in
 * without touching the shell:
 *   [/Script/SimCruise.SimCruiseGameMode]
 *   VehiclePawnClass=/Script/SimCruise.SimCruiseVehicle
 * Any pawn that implements ISimCruiseVehicleControl works; without one the
 * placeholder single-track car is used.
 *
 * Command line:
 *   -SimCruiseCar=<id>             car file under data/cars (default StartCarId)
 *   -SimCruiseStart=lat,lon        start elsewhere (needs the atlas helper); nearest road
 *   -SimCruiseStartRoad=<name>     the road to start on near the start point
 *   -SimCruiseNoWorld              drive the loaded map instead (its player start)
 *   -SimCruiseDriveCapture         documentation captures (see ASimCruiseDriveCapture)
 */
UCLASS(config = Game)
class SIMCRUISE_API ASimCruiseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimCruiseGameMode();

	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Put the player's car back upright at rest: on the nearest road when the
	 *  real place is streamed, otherwise on the ground below it. */
	void RecoverVehicle(APlayerController* Player);

	/** True while the real place streams around the car. */
	bool IsWorldDrive() const { return bWorldDrive; }
	ASimCruiseWorldStreamer* GetStreamer() const;
	/** The car is on the road and the player drives it. */
	bool IsDriveReady() const { return bCarPlaced; }

	UPROPERTY(config, EditAnywhere, Category = "SimCruise")
	FSoftClassPath VehiclePawnClass;

	/** The car a drive starts in (data/cars/<id>.json). */
	UPROPERTY(config, EditAnywhere, Category = "SimCruise")
	FString StartCarId = TEXT("nb1999");

	/** Road the bundled start puts the car on (Godot StartPool FALLBACK: Ocean Drive). */
	UPROPERTY(config, EditAnywhere, Category = "SimCruise")
	FString StartRoad = TEXT("Ocean Drive");

	/** Seconds to wait for the start tiles before placing the car on whatever is there. */
	UPROPERTY(config, EditAnywhere, Category = "SimCruise")
	float StartTimeoutSeconds = 180.f;

private:
	void BeginWorldDrive();
	bool TryPlacePendingPlayers();
	APawn* SpawnVehicleAt(AController* Player, const FVector& GroundCm, const FRotator& Heading);
	void HandleRebased(const FVector& OffsetCm);
	void UpdateLoadingMessage();

	bool bWorldDrive = false;
	bool bCarPlaced = false;
	FVector StartCm = FVector::ZeroVector;
	FString StartRoadName;
	FString CarId;
	double WaitStartedSeconds = 0.0;
	double LastMessageSeconds = -100.0;
	TArray<TWeakObjectPtr<AController>> PendingPlayers;
	TWeakObjectPtr<ASimCruiseWorldStreamer> Streamer;
};
