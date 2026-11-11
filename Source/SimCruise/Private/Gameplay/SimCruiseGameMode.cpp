// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseGameMode.h"

#include "Engine/World.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseDriveCapture.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruisePlaceholderVehicle.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SimCruiseGameInstance.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseDrive, Log, All);

namespace
{
	/** How far from the start point the start road may be. */
	constexpr double StartSearchRadiusM = 900.0;
	/** How far a recover looks for a road. */
	constexpr double RecoverSearchRadiusM = 250.0;
	/** The car is placed this far above the road and settles onto its springs. */
	constexpr double SpawnLiftCm = 3.0;

	bool ParseLatLon(const FString& Text, double& OutLat, double& OutLon)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() != 2 || !Parts[0].IsNumeric() || !Parts[1].IsNumeric())
		{
			return false;
		}
		OutLat = FCString::Atod(*Parts[0]);
		OutLon = FCString::Atod(*Parts[1]);
		return true;
	}
}

ASimCruiseGameMode::ASimCruiseGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
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
		UE_LOG(LogSimCruiseDrive, Warning, TEXT("SimCruise VehiclePawnClass %s is not a pawn implementing ISimCruiseVehicleControl; using the placeholder."),
			*VehiclePawnClass.ToString());
	}
	return Super::GetDefaultPawnClassForController_Implementation(InController);
}

ASimCruiseWorldStreamer* ASimCruiseGameMode::GetStreamer() const
{
	return Streamer.Get();
}

void ASimCruiseGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	// Decided here, not in StartPlay: the local player logs in (PostLogin ->
	// RestartPlayer) while the map loads, before StartPlay, and a world drive
	// must hold that player until the road under the car exists.
	const TCHAR* Cmd = FCommandLine::Get();
	CarId = StartCarId;
	FParse::Value(Cmd, TEXT("SimCruiseCar="), CarId);
	// The lighting lab builds its own street, and -SimCruiseNoWorld drives the loaded map.
	bWorldDrive = GetWorld() && GetWorld()->IsGameWorld()
		&& !FParse::Param(Cmd, TEXT("SimCruiseNoWorld")) && !FParse::Param(Cmd, TEXT("SimCruiseLab"));
}

void ASimCruiseGameMode::StartPlay()
{
	if (bWorldDrive)
	{
		BeginWorldDrive();
	}
	Super::StartPlay();
	if (!bWorldDrive && !PendingPlayers.IsEmpty())
	{
		// The world could not start: the held players drive the loaded map instead.
		TArray<TWeakObjectPtr<AController>> Held = MoveTemp(PendingPlayers);
		for (const TWeakObjectPtr<AController>& Weak : Held)
		{
			if (AController* Player = Weak.Get())
			{
				Super::RestartPlayer(Player);
			}
		}
	}
}

void ASimCruiseGameMode::BeginWorldDrive()
{
	UWorld* World = GetWorld();
	USimCruiseWorldSubsystem* Worlds = World->GetSubsystem<USimCruiseWorldSubsystem>();
	if (!Worlds)
	{
		bWorldDrive = false;
		return;
	}
	const TCHAR* Cmd = FCommandLine::Get();
	double Lat = USimCruiseWorldSubsystem::BundledLat;
	double Lon = USimCruiseWorldSubsystem::BundledLon;
	FString Title = TEXT("Ocean Drive, Miami Beach");
	StartRoadName = StartRoad;
	FString Start;
	if (FParse::Value(Cmd, TEXT("SimCruiseStart="), Start) && ParseLatLon(Start, Lat, Lon))
	{
		Title = FString::Printf(TEXT("%.5f, %.5f"), Lat, Lon);
		StartRoadName.Reset();
	}
	FParse::Value(Cmd, TEXT("SimCruiseStartRoad="), StartRoadName);

	// The world subsystem may already have started this place from the command line.
	ASimCruiseWorldStreamer* Existing = Worlds->GetStreamer();
	Streamer = Existing ? Existing : Worlds->StartDrive(Lon, Lat, Title);
	if (!Streamer.IsValid())
	{
		UE_LOG(LogSimCruiseDrive, Error, TEXT("Could not start the world stream; driving the loaded map."));
		bWorldDrive = false;
		return;
	}
	StartCm = Streamer->GetFrame().LonLatToWorldCm(Lon, Lat);
	Streamer->SetFocus(StartCm, FVector::ZeroVector);
	Streamer->OnRebased.AddUObject(this, &ASimCruiseGameMode::HandleRebased);
	WaitStartedSeconds = FPlatformTime::Seconds();

	// The sun and sky follow the place (unless the command line placed the observer).
	double CommandLineLat = 0.0;
	if (!FParse::Value(Cmd, TEXT("SimCruiseLat="), CommandLineLat))
	{
		if (USimCruiseEnvironmentSubsystem* Environments = USimCruiseEnvironmentSubsystem::Get(World))
		{
			if (ASimCruiseEnvironment* Environment = Environments->GetEnvironment())
			{
				Environment->SetPlace(Lat, Lon, Environment->ElevationM);
			}
		}
	}

	if (FParse::Param(Cmd, TEXT("SimCruiseDriveCapture")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ASimCruiseDriveCapture>(ASimCruiseDriveCapture::StaticClass(), FTransform::Identity, Params);
	}
	UE_LOG(LogSimCruiseDrive, Display, TEXT("Drive at %s in %s: waiting for the tiles around the start"), *Title, *CarId);
}

void ASimCruiseGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!bWorldDrive || !NewPlayer)
	{
		Super::RestartPlayer(NewPlayer);
		return;
	}
	if (bCarPlaced && NewPlayer->GetPawn())
	{
		return;
	}
	// The car waits until the road under it exists.
	PendingPlayers.AddUnique(NewPlayer);
	if (bCarPlaced)
	{
		TryPlacePendingPlayers();
	}
}

void ASimCruiseGameMode::UpdateLoadingMessage()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastMessageSeconds < 2.0)
	{
		return;
	}
	LastMessageSeconds = Now;
	for (const TWeakObjectPtr<AController>& Weak : PendingPlayers)
	{
		if (ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(Weak.Get()))
		{
			const int32 Tiles = Streamer.IsValid() ? Streamer->NumTiles() : 0;
			Player->ShowMessage(FString::Printf(TEXT("Building %s · %d of 9 tiles"),
				Streamer.IsValid() ? *Streamer->GetTitle() : TEXT("the place"), FMath::Min(Tiles, 9)), 2.5f);
		}
	}
}

bool ASimCruiseGameMode::TryPlacePendingPlayers()
{
	PendingPlayers.RemoveAll([](const TWeakObjectPtr<AController>& Weak) { return !Weak.IsValid(); });
	if (PendingPlayers.IsEmpty())
	{
		return true;
	}
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream)
	{
		return false;
	}
	if (!bCarPlaced)
	{
		Stream->SetFocus(StartCm, FVector::ZeroVector);
		const bool bReady = Stream->AreTilesReadyAround(StartCm, 1);
		const bool bTimedOut = FPlatformTime::Seconds() - WaitStartedSeconds > StartTimeoutSeconds;
		if (!bReady && !bTimedOut)
		{
			UpdateLoadingMessage();
			return false;
		}
		if (!bReady)
		{
			UE_LOG(LogSimCruiseDrive, Warning, TEXT("The start tiles were not all ready after %.0f s; placing the car on what is there."), StartTimeoutSeconds);
		}
	}

	FVector Ground = StartCm;
	FRotator Heading = FRotator::ZeroRotator;
	const bool bOnRoad = (!StartRoadName.IsEmpty() && Stream->FindRoadPose(StartRoadName, StartCm, StartSearchRadiusM, Ground, Heading))
		|| Stream->FindRoadPose(FString(), StartCm, StartSearchRadiusM, Ground, Heading);
	if (!bOnRoad)
	{
		double Z = 0.0;
		if (Stream->SurfaceHeightAt(StartCm, Z))
		{
			Ground.Z = Z;
		}
		UE_LOG(LogSimCruiseDrive, Warning, TEXT("No road within %.0f m of the start; the car starts on the ground."), StartSearchRadiusM);
	}
	for (const TWeakObjectPtr<AController>& Weak : PendingPlayers)
	{
		if (AController* Player = Weak.Get())
		{
			if (APawn* Pawn = SpawnVehicleAt(Player, Ground, Heading))
			{
				UE_LOG(LogSimCruiseDrive, Display, TEXT("Car placed%s at %s heading %.1f deg after %.1f s"),
					bOnRoad ? *FString::Printf(TEXT(" on %s"), StartRoadName.IsEmpty() ? TEXT("the nearest road") : *StartRoadName) : TEXT(""),
					*Ground.ToString(), Heading.Yaw, FPlatformTime::Seconds() - WaitStartedSeconds);
				if (ASimCruisePlayerController* SimPlayer = Cast<ASimCruisePlayerController>(Player))
				{
					SimPlayer->ShowMessage(TEXT("RT or W to accelerate  ·  Y or C camera  ·  L3 or F2 HUD"), 6.f);
				}
				(void)Pawn;
			}
		}
	}
	PendingPlayers.Reset();
	bCarPlaced = true;
	return true;
}

APawn* ASimCruiseGameMode::SpawnVehicleAt(AController* Player, const FVector& GroundCm, const FRotator& Heading)
{
	UWorld* World = GetWorld();
	UClass* PawnClass = GetDefaultPawnClassForController(Player);
	APawn* Pawn = nullptr;
	if (PawnClass && PawnClass->IsChildOf(ASimCruiseVehicle::StaticClass()))
	{
		FSimCruiseCarDefinition Car;
		const USimCruiseGameInstance* GameInstance = GetGameInstance<USimCruiseGameInstance>();
		const FSimCruiseCarDefinition* Known = GameInstance ? GameInstance->FindCarDefinition(CarId) : nullptr;
		FString Error;
		if (Known)
		{
			Car = *Known;
		}
		else if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Car, Error))
		{
			UE_LOG(LogSimCruiseDrive, Error, TEXT("Car %s: %s; using nb1999"), *CarId, *Error);
			FSimCruiseCarDefinitionLoader::LoadById(TEXT("nb1999"), Car, Error);
		}
		ASimCruiseVehicle* Vehicle = World->SpawnActorDeferred<ASimCruiseVehicle>(PawnClass, FTransform(Heading, GroundCm), nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Vehicle)
		{
			return nullptr;
		}
		Vehicle->SetCarDefinition(Car);
		Vehicle->FinishSpawning(Vehicle->BodyTransformForGroundPose(GroundCm + FVector(0.0, 0.0, SpawnLiftCm), static_cast<float>(Heading.Yaw)));
		Vehicle->AttachCarModel();
		Pawn = Vehicle;
	}
	else if (PawnClass)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Pawn = World->SpawnActor<APawn>(PawnClass, FTransform(Heading, GroundCm + FVector(0.0, 0.0, 5.0)), Params);
	}
	if (Pawn)
	{
		Player->SetControlRotation(Heading);
		Player->Possess(Pawn);
		FinishRestartPlayer(Player, Heading);
	}
	return Pawn;
}

void ASimCruiseGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bWorldDrive)
	{
		return;
	}
	if (!PendingPlayers.IsEmpty())
	{
		TryPlacePendingPlayers();
		return;
	}
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream)
	{
		return;
	}
	// The world streams around the first player's car.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* Player = It->Get())
		{
			if (const APawn* Pawn = Player->GetPawn())
			{
				Stream->SetFocus(Pawn->GetActorLocation(), Pawn->GetVelocity());
				break;
			}
		}
	}
}

void ASimCruiseGameMode::HandleRebased(const FVector& OffsetCm)
{
	StartCm += OffsetCm;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		if (APawn* Pawn = Player ? Player->GetPawn() : nullptr)
		{
			// Teleport keeps the body's velocity: the car drives on across the shift.
			Pawn->AddActorWorldOffset(OffsetCm, false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (ASimCruisePlayerCameraManager* Camera = Player ? Cast<ASimCruisePlayerCameraManager>(Player->PlayerCameraManager) : nullptr)
		{
			Camera->SnapNextFrame();
		}
	}
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
	FRotator Heading(0.0, Pawn->GetActorRotation().Yaw, 0.0);
	bool bOnRoad = false;
	if (ASimCruiseWorldStreamer* Stream = Streamer.Get())
	{
		FVector RoadGround;
		FRotator RoadHeading;
		if (Stream->FindRoadPose(FString(), Location, RecoverSearchRadiusM, RoadGround, RoadHeading))
		{
			Ground = RoadGround;
			Heading = RoadHeading;
			bOnRoad = true;
		}
	}
	if (!bOnRoad)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruiseRecover), false, Pawn);
		// Probe from just above the car (3 m) first: under an overpass or a canopy
		// a high start would land the car on the deck above. Only a car that has
		// fallen below the surface needs the probe from high above.
		if (GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 300.0), Location - FVector(0.0, 0.0, 20000.0), ECC_Visibility, Params)
			|| GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.0, 0.0, 20000.0), Location - FVector(0.0, 0.0, 20000.0), ECC_Visibility, Params))
		{
			Ground = Hit.ImpactPoint;
		}
	}
	Vehicle->ResetVehicle(FTransform(Heading, Ground + FVector(0.0, 0.0, SpawnLiftCm)));
	if (ASimCruisePlayerController* Controller = Cast<ASimCruisePlayerController>(Player))
	{
		Controller->ShowMessage(bOnRoad ? TEXT("Back on the road") : TEXT("Recovered upright"), 3.f);
	}
}
