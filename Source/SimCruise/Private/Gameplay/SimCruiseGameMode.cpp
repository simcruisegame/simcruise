// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseGameMode.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruisePlaceholderVehicle.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruiseVehicleControl.h"

ASimCruiseGameMode::ASimCruiseGameMode()
{
	DefaultPawnClass = ASimCruisePlaceholderVehicle::StaticClass();
	PlayerControllerClass = ASimCruisePlayerController::StaticClass();
	HUDClass = ASimCruiseHUD::StaticClass();
}

UClass* ASimCruiseGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (VehiclePawnClass.IsValid())
	{
		UClass* Loaded = VehiclePawnClass.TryLoadClass<APawn>();
		if (Loaded && Loaded->ImplementsInterface(USimCruiseVehicleControl::StaticClass()))
		{
			return Loaded;
		}
		UE_LOG(LogTemp, Warning, TEXT("SimCruise VehiclePawnClass %s is not a pawn implementing ISimCruiseVehicleControl; using the placeholder."),
			*VehiclePawnClass.ToString());
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

void ASimCruiseGameMode::RecoverVehicle(APlayerController* Player)
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	ISimCruiseVehicleControl* Vehicle = Cast<ISimCruiseVehicleControl>(Pawn);
	if (!Vehicle)
	{
		return;
	}
	const FVector Location = Pawn->GetActorLocation();
	FVector Ground = Location;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruiseRecover), false, Pawn);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 2000.0), Location - FVector(0.0, 0.0, 20000.0), ECC_Visibility, Params))
	{
		Ground = Hit.ImpactPoint;
	}
	const FRotator Heading(0.0, Pawn->GetActorRotation().Yaw, 0.0);
	Vehicle->ResetVehicle(FTransform(Heading, Ground + FVector(0.0, 0.0, 5.0)));
	if (ASimCruisePlayerController* Controller = Cast<ASimCruisePlayerController>(Player))
	{
		Controller->ShowMessage(TEXT("Recovered upright · road recovery arrives with the road stream"), 3.f);
	}
}
