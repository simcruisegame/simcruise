// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseGameMode.h"

#include "Engine/World.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseDriveCapture.h"
#include "Gameplay/SimCruiseDriveTest.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruisePlaceholderVehicle.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruisePreferences.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "SimCruiseGameInstance.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Misc/App.h"
#include "World/SimCruiseRegionalTerrain.h"
#include "World/SimCruiseTrees.h"
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

	/** The bundled offline place (its tiles ship in data/tiles). */
	bool IsBundledPlace(const FSimCruisePlace& Place)
	{
		const FSimCruisePlace Bundled = SimCruisePlaces::Fallback();
		return FMath::Abs(Place.Lat - Bundled.Lat) < 1e-4 && FMath::Abs(Place.Lon - Bundled.Lon) < 1e-4;
	}

	/** A normal drive starts on a random pool road; checks, captures and previews start on the bundled streets. */
	bool WantsRandomStart(const TCHAR* Cmd, bool bConfigDefault)
	{
		if (FParse::Param(Cmd, TEXT("SimCruiseRandomStart"))) return true;
		static const TCHAR* BundledFlags[] = {
			TEXT("SimCruiseBundledStart"), TEXT("SimCruiseDriveCapture"), TEXT("SimCruiseDriveTest"), TEXT("SimCruiseShellSelfTest"),
			TEXT("SimCruiseShellCapture"), TEXT("SimCruiseLab"),
		};
		for (const TCHAR* Flag : BundledFlags)
		{
			if (FParse::Param(Cmd, Flag)) return false;
		}
		FString Ignored;
		if (FParse::Value(Cmd, TEXT("SimCruisePreview="), Ignored) || FParse::Value(Cmd, TEXT("SimCruiseDrivePerf="), Ignored)) return false;
		return bConfigDefault && !GIsAutomationTesting;
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
	// The car driven last (DrivingPreferences.gd "car"), when it still exists.
	const FString Remembered = SimCruisePreferences::GetString(TEXT("car"), FString());
	const USimCruiseGameInstance* Garage = GetGameInstance<USimCruiseGameInstance>();
	if (!Remembered.IsEmpty() && Garage && Garage->HasCarDefinition(Remembered))
	{
		CarId = Remembered;
	}
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

	// Where this drive begins (Drive.gd plan_start): a chosen point, else a seeded random
	// start-pool road in normal play, else the bundled place, which checks and captures use
	// so their runs repeat.
	int32 Seed = 0;
	if (FParse::Value(Cmd, TEXT("SimCruiseStartSeed="), Seed)) StartRng.Initialize(Seed);
	else StartRng.GenerateNewSeed();
	FSimCruisePlace First = SimCruisePlaces::Fallback();
	First.Street = StartRoad;
	FString Mode = TEXT("bundled");
	FString Start;
	double Lat = 0.0, Lon = 0.0;
	if (FParse::Value(Cmd, TEXT("SimCruiseStart="), Start, /*bShouldStopOnSeparator=*/ false) && ParseLatLon(Start, Lat, Lon))
	{
		First = FSimCruisePlace::Make(Lon, Lat, FString::Printf(TEXT("%.5f, %.5f"), Lat, Lon));
		Mode = TEXT("chosen");
	}
	else if (WantsRandomStart(Cmd, bRandomStart))
	{
		StartPool = SimCruisePlaces::LoadStartPool();
		StartChain = SimCruisePlaces::Sequence(StartPool, StartRng, 1 + SimCruisePlaces::Retries);
		if (!StartChain.IsEmpty())
		{
			First = StartChain[0];
			StartChain.RemoveAt(0);
			bStartLaunch = true;
			Mode = TEXT("random");
		}
	}
	FString Road;
	if (FParse::Value(Cmd, TEXT("SimCruiseStartRoad="), Road))
	{
		First.Street = Road;
	}
	// Checks parse this line (Drive.gd: "START <mode> seed=<n> <title>").
	UE_LOG(LogSimCruiseDrive, Display, TEXT("START %s seed=%d %s"), *Mode, StartRng.GetInitialSeed(), *First.Title);

	// The world subsystem may already have started a place from the command line.
	ASimCruiseWorldStreamer* Existing = Worlds->GetStreamer();
	const bool bSamePlace = Existing && Existing->GetFrame().IsValid()
		&& Existing->GetFrame().OriginTile == FSimCruiseWorldFrame::ForPlace(First.Lon, First.Lat).OriginTile;
	Streamer = Existing ? Existing : Worlds->StartDrive(First.Lon, First.Lat, First.Title);
	if (!Streamer.IsValid())
	{
		UE_LOG(LogSimCruiseDrive, Error, TEXT("Could not start the world stream; driving the loaded map."));
		bWorldDrive = false;
		return;
	}
	Streamer->OnRebased.AddUObject(this, &ASimCruiseGameMode::HandleRebased);
	BeginTravel(First, Existing != nullptr && !bSamePlace);

	// The land to the horizon round wherever the car is (needs the helper; drawn runs only).
	if (FApp::CanEverRender() && !FParse::Param(Cmd, TEXT("SimCruiseNoLandscape")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ASimCruiseRegionalTerrain* Land = World->SpawnActor<ASimCruiseRegionalTerrain>(ASimCruiseRegionalTerrain::StaticClass(), FTransform::Identity, Params))
		{
			Land->Streamer = Streamer;
		}
	}
	// Trees anywhere, from the canopy height map (needs the helper; drawn runs only).
	if (FApp::CanEverRender() && !FParse::Param(Cmd, TEXT("SimCruiseNoTrees")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ASimCruiseTrees* Trees = World->SpawnActor<ASimCruiseTrees>(ASimCruiseTrees::StaticClass(), FTransform::Identity, Params))
		{
			Trees->Streamer = Streamer;
		}
	}

	if (FParse::Param(Cmd, TEXT("SimCruiseDriveCapture")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ASimCruiseDriveCapture>(ASimCruiseDriveCapture::StaticClass(), FTransform::Identity, Params);
	}
	if (FParse::Param(Cmd, TEXT("SimCruiseDriveTest")) || FParse::Param(Cmd, TEXT("SimCruiseDriveLog")))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		World->SpawnActor<ASimCruiseDriveTest>(ASimCruiseDriveTest::StaticClass(), FTransform::Identity, Params);
	}
	UE_LOG(LogSimCruiseDrive, Display, TEXT("Drive at %s in %s: waiting for the tiles around the start"), *First.Title, *CarId);
}

void ASimCruiseGameMode::BeginTravel(const FSimCruisePlace& Place, bool bRestartWorld)
{
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream)
	{
		return;
	}
	CurrentPlace = Place;
	StartRoadName = Place.Street;
	bCarPlaced = false;
	if (bRestartWorld)
	{
		Stream->StartAt(Place.Lon, Place.Lat, Place.Title);
	}
	StartCm = Stream->GetFrame().LonLatToWorldCm(Place.Lon, Place.Lat);
	Stream->SetFocus(StartCm, FVector::ZeroVector);
	WaitStartedSeconds = FPlatformTime::Seconds();
	LastMessageSeconds = -100.0;

	// The sun and sky follow the place (unless the command line placed the observer).
	double CommandLineLat = 0.0;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SimCruiseLat="), CommandLineLat))
	{
		if (USimCruiseEnvironmentSubsystem* Environments = USimCruiseEnvironmentSubsystem::Get(GetWorld()))
		{
			if (ASimCruiseEnvironment* Environment = Environments->GetEnvironment())
			{
				Environment->SetPlace(Place.Lat, Place.Lon, Environment->ElevationM);
			}
		}
	}
}

void ASimCruiseGameMode::HoldPlayers()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Player = It->Get();
		if (!Player) continue;
		if (APawn* Pawn = Player->GetPawn())
		{
			Player->UnPossess();
			Pawn->Destroy();
		}
		PendingPlayers.AddUnique(Player);
	}
	bCarPlaced = false;
}

void ASimCruiseGameMode::TravelTo(const FSimCruisePlace& Place)
{
	if (!bWorldDrive || !Streamer.IsValid() || !Place.IsValid())
	{
		return;
	}
	// A chosen place ends any start sequence (Drive.gd choose_place).
	StartChain.Reset();
	bStartLaunch = false;
	StartNote.Reset();
	HoldPlayers();
	UE_LOG(LogSimCruiseDrive, Display, TEXT("Travel to %s (%.5f, %.5f)%s"), *Place.Title, Place.Lat, Place.Lon,
		Place.Street.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" on %s"), *Place.Street));
	BeginTravel(Place, true);
	UpdateLoadingMessage();
}

bool ASimCruiseGameMode::SurpriseMe()
{
	if (StartPool.IsEmpty())
	{
		StartPool = SimCruisePlaces::LoadStartPool();
	}
	TArray<FSimCruisePlace> Picks = SimCruisePlaces::Sequence(StartPool, StartRng, 1 + SimCruisePlaces::Retries, CurrentPlace.Title);
	if (Picks.IsEmpty())
	{
		return false;
	}
	const FSimCruisePlace First = Picks[0];
	Picks.RemoveAt(0);
	TravelTo(First);
	StartChain = MoveTemp(Picks);
	return true;
}

void ASimCruiseGameMode::TravelFailed(const FString& Reason)
{
	UE_LOG(LogSimCruiseDrive, Warning, TEXT("%s did not load: %s"), *CurrentPlace.Title, *Reason);
	if (!StartChain.IsEmpty())
	{
		const FSimCruisePlace Next = StartChain[0];
		StartChain.RemoveAt(0);
		BeginTravel(Next, true);
		return;
	}
	StartNote = bStartLaunch
		? FString(TEXT("No random road loaded · starting in bundled Miami Beach (works offline)"))
		: FString::Printf(TEXT("%s did not load · back in bundled Miami Beach (works offline)"), *CurrentPlace.Title);
	bStartLaunch = false;
	UE_LOG(LogSimCruiseDrive, Display, TEXT("START fallback after: %s"), *Reason);
	FSimCruisePlace Fallback = SimCruisePlaces::Fallback();
	Fallback.Street = StartRoad;
	BeginTravel(Fallback, true);
}

bool ASimCruiseGameMode::WorldToLonLat(const FVector& WorldCm, double& OutLon, double& OutLat) const
{
	const ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream || !Stream->GetFrame().IsValid())
	{
		return false;
	}
	Stream->GetFrame().WorldCmToLonLat(WorldCm, OutLon, OutLat);
	return true;
}

void ASimCruiseGameMode::CycleCar(APlayerController* Player)
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const USimCruiseGameInstance* GameInstance = GetGameInstance<USimCruiseGameInstance>();
	if (!Pawn || !GameInstance || !bCarPlaced)
	{
		return;
	}
	TArray<FString> Ids = GameInstance->GetAvailableCarIds();
	Ids.Sort();
	if (Ids.Num() < 2)
	{
		return;
	}
	const int32 Index = Ids.IndexOfByKey(CarId);
	CarId = Ids[(Index + 1) % Ids.Num()];
	SimCruisePreferences::SetString(TEXT("car"), CarId);
	// The new car starts at rest where the old one stood, facing the same way.
	FVector Ground = Pawn->GetActorLocation();
	const FRotator Heading(0.0, Pawn->GetActorRotation().Yaw, 0.0);
	double Z = 0.0;
	if (ASimCruiseWorldStreamer* Stream = Streamer.Get(); Stream && Stream->SurfaceHeightAt(Ground, Z))
	{
		Ground.Z = Z;
	}
	Player->UnPossess();
	Pawn->Destroy();
	SpawnVehicleAt(Player, Ground, Heading);
	if (ASimCruisePlayerController* SimPlayer = Cast<ASimCruisePlayerController>(Player))
	{
		const FSimCruiseCarDefinition* Car = GameInstance->FindCarDefinition(CarId);
		SimPlayer->ShowMessage(FString::Printf(TEXT("Car · %s"), Car ? *Car->Name : *CarId), 3.f);
	}
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
	const bool bFirstPlacement = !bCarPlaced;
	if (bFirstPlacement)
	{
		Stream->SetFocus(StartCm, FVector::ZeroVector);
		const bool bReady = Stream->AreTilesReadyAround(StartCm, 1);
		const double Waited = FPlatformTime::Seconds() - WaitStartedSeconds;
		const bool bTimedOut = Waited > StartTimeoutSeconds;
		// Tiles that cannot load (offline, no helper, nothing there): the next place, else the
		// bundled one (Drive.gd start_next). The bundled place itself loads from disk.
		const int32 Failed = Stream->NumTilesFailed();
		if (!bReady && (Failed >= 2 || (Failed >= 1 && Waited > 30.0)) && !IsBundledPlace(CurrentPlace))
		{
			TravelFailed(FString::Printf(TEXT("%d tiles failed to load"), Failed));
			return false;
		}
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

	// On the place's street (a start-pool point is a node on it), facing its bearing on a two-way
	// road; else the nearest public road. A pool point takes the road under it only within 30 m
	// (Drive.gd: named street within 125 m, loose fallback main road within 30 m).
	const double PreferredYaw = CurrentPlace.bHasBearing ? SimCruisePlaces::BearingToYawDeg(CurrentPlace.BearingDeg) : TNumericLimits<double>::Max();
	const double NamedRadiusM = CurrentPlace.bLoose ? 125.0 : StartSearchRadiusM;
	const double AnyRadiusM = CurrentPlace.bLoose ? 30.0 : StartSearchRadiusM;
	FVector Ground = StartCm;
	FRotator Heading = CurrentPlace.bHasBearing ? FRotator(0.0, PreferredYaw, 0.0) : FRotator::ZeroRotator;
	const bool bNamed = !StartRoadName.IsEmpty() && Stream->FindRoadPoseFacing(StartRoadName, StartCm, NamedRadiusM, PreferredYaw, Ground, Heading);
	if (!bNamed && !StartRoadName.IsEmpty())
	{
		UE_LOG(LogSimCruiseDrive, Warning, TEXT("No room to start on %s within %.0f m of the start; starting on the nearest road."), *StartRoadName, NamedRadiusM);
	}
	const bool bOnRoad = bNamed || Stream->FindRoadPoseFacing(FString(), StartCm, AnyRadiusM, PreferredYaw, Ground, Heading);
	if (!bOnRoad)
	{
		if (bFirstPlacement && (!StartChain.IsEmpty() || bStartLaunch))
		{
			TravelFailed(TEXT("no road near the start point"));
			return false;
		}
		double Z = 0.0;
		if (Stream->SurfaceHeightAt(StartCm, Z))
		{
			Ground.Z = Z;
		}
		UE_LOG(LogSimCruiseDrive, Warning, TEXT("No road within %.0f m of the start; the car starts on the ground."), AnyRadiusM);
	}
	for (const TWeakObjectPtr<AController>& Weak : PendingPlayers)
	{
		if (AController* Player = Weak.Get())
		{
			if (APawn* Pawn = SpawnVehicleAt(Player, Ground, Heading))
			{
				UE_LOG(LogSimCruiseDrive, Display, TEXT("Car placed%s at %s heading %.1f deg after %.1f s"),
					bOnRoad ? *FString::Printf(TEXT(" on %s"), bNamed ? *StartRoadName : TEXT("the nearest road")) : TEXT(""),
					*Ground.ToString(), Heading.Yaw, FPlatformTime::Seconds() - WaitStartedSeconds);
				if (ASimCruisePlayerController* SimPlayer = Cast<ASimCruisePlayerController>(Player))
				{
					SimPlayer->ShowMessage(!StartNote.IsEmpty() ? StartNote
						: FString::Printf(TEXT("%s  ·  RT or W to accelerate  ·  / or Menu to go somewhere else"), *CurrentPlace.Title), 6.f);
				}
				(void)Pawn;
			}
		}
	}
	PendingPlayers.Reset();
	bCarPlaced = true;
	StartChain.Reset();
	bStartLaunch = false;
	StartNote.Reset();

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
