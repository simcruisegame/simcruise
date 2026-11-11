// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SimCruiseGameMode.generated.h"

class APlayerController;

/**
 * The drivable loop: spawns the vehicle pawn for the player, with the
 * SimCruise controller, camera manager and HUD. The pawn class comes from
 * config so the vehicle stream can plug its own in without touching the shell:
 *   [/Script/SimCruise.SimCruiseGameMode]
 *   VehiclePawnClass=/Script/SimCruise.SomeVehiclePawn
 * Any pawn that implements ISimCruiseVehicleControl works; without one the
 * placeholder single-track car is used.
 */
UCLASS(config = Game)
class SIMCRUISE_API ASimCruiseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASimCruiseGameMode();

	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

	/** Put the player's car back upright at rest on the ground below it,
	 *  facing the same way. Road-aware recovery arrives with the road stream. */
	void RecoverVehicle(APlayerController* Player);

	UPROPERTY(config, EditAnywhere, Category = "SimCruise")
	FSoftClassPath VehiclePawnClass;
};
