// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseHotReload.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruisePreferences.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SimCruiseGameInstance.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseHotReload, Log, All);

// --- FSimCruiseFileWatcher ---------------------------------------------------

void FSimCruiseFileWatcher::WatchFolder(const FString& Folder, const FString& Wildcard)
{
	Folders.AddUnique(TPair<FString, FString>(FPaths::ConvertRelativePathToFull(Folder), Wildcard));
}

void FSimCruiseFileWatcher::WatchFile(const FString& Path)
{
	Files.AddUnique(FPaths::ConvertRelativePathToFull(Path));
}

void FSimCruiseFileWatcher::Scan(TMap<FString, TPair<FDateTime, int64>>& Out) const
{
	IFileManager& FileManager = IFileManager::Get();
	auto Add = [&Out, &FileManager](const FString& Path)
	{
		const FFileStatData Stat = FileManager.GetStatData(*Path);
		if (Stat.bIsValid && !Stat.bIsDirectory)
		{
			Out.Add(Path, TPair<FDateTime, int64>(Stat.ModificationTime, Stat.FileSize));
		}
	};
	for (const TPair<FString, FString>& Folder : Folders)
	{
		TArray<FString> Names;
		FileManager.FindFiles(Names, *FPaths::Combine(Folder.Key, Folder.Value), true, false);
		for (const FString& Name : Names)
		{
			Add(FPaths::Combine(Folder.Key, Name));
		}
	}
	for (const FString& File : Files)
	{
		Add(File);
	}
}

void FSimCruiseFileWatcher::Baseline(double NowSeconds)
{
	TMap<FString, TPair<FDateTime, int64>> Now;
	Scan(Now);
	States.Reset();
	for (const TPair<FString, TPair<FDateTime, int64>>& Entry : Now)
	{
		FFileState& State = States.Add(Entry.Key);
		State.Stamp = Entry.Value.Key;
		State.Size = Entry.Value.Value;
		State.ChangedAt = NowSeconds;
	}
}

TArray<FString> FSimCruiseFileWatcher::Poll(double NowSeconds)
{
	TMap<FString, TPair<FDateTime, int64>> Now;
	Scan(Now);
	for (const TPair<FString, TPair<FDateTime, int64>>& Entry : Now)
	{
		FFileState* State = States.Find(Entry.Key);
		if (!State)
		{
			// A new file is a change too (a calibration overlay saved for the first time).
			State = &States.Add(Entry.Key);
			State->bPending = true;
			State->ChangedAt = NowSeconds;
		}
		else if (State->Stamp != Entry.Value.Key || State->Size != Entry.Value.Value)
		{
			State->bPending = true;
			State->ChangedAt = NowSeconds;
		}
		State->Stamp = Entry.Value.Key;
		State->Size = Entry.Value.Value;
	}
	// A deleted file is forgotten; saving it again reports it.
	for (auto It = States.CreateIterator(); It; ++It)
	{
		if (!Now.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
	TArray<FString> Settled;
	for (TPair<FString, FFileState>& Entry : States)
	{
		if (Entry.Value.bPending && NowSeconds - Entry.Value.ChangedAt >= SettleSeconds)
		{
			Entry.Value.bPending = false;
			Settled.Add(Entry.Key);
		}
	}
	Settled.Sort();
	return Settled;
}

// --- SimCruiseHotReload ------------------------------------------------------

namespace SimCruiseHotReload
{
	FString CalibrationDir()
	{
		// The same folder FSimCruiseCarDefinitionLoader::LoadById reads.
		FString Dir;
#if !UE_BUILD_SHIPPING
		Dir = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_CALIBRATION_DIR"));
#endif
		return Dir.IsEmpty() ? FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Vehicle"), TEXT("Calibration")) : Dir;
	}

	FString LookPath()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SimCruise"), TEXT("look.json"));
	}

	namespace
	{
		bool SamePath(const FString& A, const FString& B)
		{
			FString FullA = FPaths::ConvertRelativePathToFull(A);
			FString FullB = FPaths::ConvertRelativePathToFull(B);
			FPaths::NormalizeFilename(FullA);
			FPaths::NormalizeFilename(FullB);
			return FullA.Equals(FullB, ESearchCase::IgnoreCase);
		}
	}

	ESimCruiseHotReloadKind Classify(const FString& Path, FString& OutCarId)
	{
		OutCarId.Reset();
		if (SamePath(Path, SimCruisePresentationProfiles::ProfilePath()))
		{
			return ESimCruiseHotReloadKind::Presentation;
		}
		if (SamePath(Path, SimCruisePreferences::Path()))
		{
			return ESimCruiseHotReloadKind::Preferences;
		}
		if (SamePath(Path, LookPath()))
		{
			return ESimCruiseHotReloadKind::Look;
		}
		const FString Folder = FPaths::GetPath(Path);
		if (FPaths::GetExtension(Path).Equals(TEXT("json"), ESearchCase::IgnoreCase)
			&& (SamePath(Folder, FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("cars"))) || SamePath(Folder, CalibrationDir())))
		{
			OutCarId = FPaths::GetBaseFilename(Path);
			return ESimCruiseHotReloadKind::Car;
		}
		return ESimCruiseHotReloadKind::Unknown;
	}

	void WatchGameData(FSimCruiseFileWatcher& Watcher)
	{
		Watcher.WatchFolder(FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("cars")), TEXT("*.json"));
		Watcher.WatchFolder(CalibrationDir(), TEXT("*.json"));
		Watcher.WatchFile(SimCruisePresentationProfiles::ProfilePath());
		Watcher.WatchFile(SimCruisePreferences::Path());
		Watcher.WatchFile(LookPath());
	}

	bool ReloadCarInPlace(ASimCruiseVehicle* Vehicle, FString& OutError)
	{
		if (!Vehicle || !Vehicle->GetVehicleMovement())
		{
			OutError = TEXT("no car");
			return false;
		}
		FSimCruiseCarDefinition Car;
		if (!FSimCruiseCarDefinitionLoader::LoadById(Vehicle->GetRealCar().Id, Car, OutError))
		{
			return false;
		}
		// Where the car stands, under the old body box, then the new car on that spot at rest.
		const double OldBoxCentreCm = SimCruiseUnits::ToUnrealLength(Vehicle->GetVehicleMovement()->GetCarDefinition().CollisionShapeCenterYM);
		const FVector Ground = Vehicle->GetActorLocation() - Vehicle->GetActorQuat().RotateVector(FVector(0.0, 0.0, OldBoxCentreCm));
		const float Yaw = static_cast<float>(Vehicle->GetActorRotation().Yaw);
		Vehicle->SetCarDefinition(Car);
		Vehicle->ResetAt(Ground, Yaw);
		return true;
	}

	bool ApplyLookFile(UWorld* World, const FString& Path, FString& OutError)
	{
		USimCruiseEnvironmentSubsystem* Subsystem = USimCruiseEnvironmentSubsystem::Get(World);
		ASimCruiseEnvironment* Env = Subsystem ? Subsystem->GetEnvironment() : nullptr;
		if (!Env)
		{
			OutError = TEXT("no environment in this world");
			return false;
		}
		FString Text;
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			OutError = TEXT("not a JSON object");
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : Root->Values)
		{
			double Value = 0.0;
			if (!Field.Key.StartsWith(TEXT("_")) && Field.Value.IsValid() && Field.Value->TryGetNumber(Value))
			{
				Env->SetLookParameter(FName(*Field.Key), static_cast<float>(Value));
			}
		}
		return true;
	}
}

// --- USimCruiseHotReloadSubsystem --------------------------------------------

bool USimCruiseHotReloadSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	if (GIsAutomationTesting || FParse::Param(FCommandLine::Get(), TEXT("SimCruiseNoHotReload")) || !Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld();
#endif
}

void USimCruiseHotReloadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	SimCruiseHotReload::WatchGameData(Watcher);
	Watcher.Baseline(FPlatformTime::Seconds());
	bStarted = true;
	UE_LOG(LogSimCruiseHotReload, Display, TEXT("Hot reload on: car files, calibration, CarPresentation.json, preferences.json, %s"), *SimCruiseHotReload::LookPath());
}

TStatId USimCruiseHotReloadSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USimCruiseHotReloadSubsystem, STATGROUP_Tickables);
}

void USimCruiseHotReloadSubsystem::Tick(float DeltaTime)
{
	if (!bStarted)
	{
		return;
	}
	// A look.json kept from an earlier session is the look the developer left: put it on once
	// the environment exists.
	if (!bLookApplied && FPaths::FileExists(SimCruiseHotReload::LookPath()))
	{
		const USimCruiseEnvironmentSubsystem* Env = USimCruiseEnvironmentSubsystem::Get(GetWorld());
		if (Env && Env->GetEnvironment())
		{
			bLookApplied = true;
			FString Error;
			if (SimCruiseHotReload::ApplyLookFile(GetWorld(), SimCruiseHotReload::LookPath(), Error))
			{
				UE_LOG(LogSimCruiseHotReload, Display, TEXT("Look from %s"), *SimCruiseHotReload::LookPath());
			}
		}
	}
	const double Now = FPlatformTime::Seconds();
	if (Now < NextPoll)
	{
		return;
	}
	NextPoll = Now + 0.25;
	for (const FString& Path : Watcher.Poll(Now))
	{
		ApplyChange(Path);
	}
}

APlayerController* USimCruiseHotReloadSubsystem::GetPlayer() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetFirstPlayerController() : nullptr;
}

void USimCruiseHotReloadSubsystem::Show(const FString& Text) const
{
	if (ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(GetPlayer()))
	{
		Player->ShowMessage(Text, 3.f);
	}
	else
	{
		UE_LOG(LogSimCruiseHotReload, Display, TEXT("%s"), *Text);
	}
}

void USimCruiseHotReloadSubsystem::ApplyChange(const FString& Path)
{
	FString CarId;
	switch (SimCruiseHotReload::Classify(Path, CarId))
	{
	case ESimCruiseHotReloadKind::Car:
		ReloadCar(CarId, Path);
		break;
	case ESimCruiseHotReloadKind::Presentation:
		RespawnPlayerCar(Path);
		break;
	case ESimCruiseHotReloadKind::Preferences:
		ApplyPreferences(Path);
		break;
	case ESimCruiseHotReloadKind::Look:
	{
		FString Error;
		Show(SimCruiseHotReload::ApplyLookFile(GetWorld(), Path, Error)
			? FString::Printf(TEXT("Reloaded %s"), *FPaths::GetCleanFilename(Path))
			: FString::Printf(TEXT("Reload failed: %s: %s"), *FPaths::GetCleanFilename(Path), *Error));
		break;
	}
	default:
		break;
	}
}

void USimCruiseHotReloadSubsystem::ReloadCar(const FString& CarId, const FString& Path)
{
	const FString File = FPaths::GetCleanFilename(Path);
	FString Error;
	USimCruiseGameInstance* GameInstance = GetWorld() ? GetWorld()->GetGameInstance<USimCruiseGameInstance>() : nullptr;
	if (GameInstance && !GameInstance->ReloadCarDefinition(CarId, Error))
	{
		// The roster keeps the last good definition; the car drives on.
		Show(FString::Printf(TEXT("Reload failed: %s: %s"), *File, *Error));
		return;
	}
	APlayerController* Player = GetPlayer();
	ASimCruiseVehicle* Vehicle = Player ? Cast<ASimCruiseVehicle>(Player->GetPawn()) : nullptr;
	if (!Vehicle || Vehicle->GetRealCar().Id != CarId)
	{
		// Another car's file: the roster has it for the next spawn.
		UE_LOG(LogSimCruiseHotReload, Display, TEXT("Reloaded %s (not the car being driven)"), *File);
		return;
	}
	if (ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>())
	{
		Mode->RespawnCar(Player);
	}
	else if (!SimCruiseHotReload::ReloadCarInPlace(Vehicle, Error))
	{
		Show(FString::Printf(TEXT("Reload failed: %s: %s"), *File, *Error));
		return;
	}
	Show(FString::Printf(TEXT("Reloaded %s"), *File));
}

void USimCruiseHotReloadSubsystem::RespawnPlayerCar(const FString& Path)
{
	const FString File = FPaths::GetCleanFilename(Path);
	APlayerController* Player = GetPlayer();
	const ASimCruiseVehicle* Vehicle = Player ? Cast<ASimCruiseVehicle>(Player->GetPawn()) : nullptr;
	if (!Vehicle)
	{
		return;
	}
	// A broken file would leave the car without a body: check it first.
	FSimCruiseCarPresentationProfile Profile;
	FString Error;
	if (!SimCruisePresentationProfiles::Load(Vehicle->GetRealCar().Id, Profile, Error))
	{
		Show(FString::Printf(TEXT("Reload failed: %s: %s"), *File, *Error));
		return;
	}
	if (ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>())
	{
		Mode->RespawnCar(Player);
		Show(FString::Printf(TEXT("Reloaded %s"), *File));
	}
}

void USimCruiseHotReloadSubsystem::ApplyPreferences(const FString& Path)
{
	// The game writes preferences.json itself (menus); say something only when the seat or the
	// lens the driver sees changes.
	ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(GetPlayer());
	ASimCruisePlayerCameraManager* Camera = Player ? Cast<ASimCruisePlayerCameraManager>(Player->PlayerCameraManager) : nullptr;
	const ASimCruiseVehicle* Vehicle = Player ? Cast<ASimCruiseVehicle>(Player->GetPawn()) : nullptr;
	if (!Camera || !SimCruisePreferences::IsEnabled())
	{
		return;
	}
	bool bChanged = false;
	if (Vehicle)
	{
		const FSimCruiseSeat Seat = FSimCruiseSeat::Load(Vehicle->GetRealCar().Id);
		bChanged |= Seat.UpCm != Camera->Seat.UpCm || Seat.ForwardCm != Camera->Seat.ForwardCm || Seat.PitchDeg != Camera->Seat.PitchDeg || Seat.FovDeg != Camera->Seat.FovDeg;
		Camera->Seat = Seat;
	}
	const int32 Lens = SimCruisePreferences::GetInt(TEXT("lens_v3"), static_cast<int32>(Camera->GetLens()), 0, 2);
	for (int32 I = 0; I < 3 && static_cast<int32>(Camera->GetLens()) != Lens; ++I)
	{
		Camera->CycleLens();
		bChanged = true;
	}
	// The screen the true-FOV lens is computed for, as PlayerTick reads it at the start.
	auto SetVar = [&bChanged](const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name); Var && Var->GetFloat() != Value)
		{
			Var->Set(Value, ECVF_SetByCode);
			bChanged = true;
		}
	};
	const int32 Tenths = SimCruisePreferences::GetInt(TEXT("screen_tenths_inch"), -1, -1, 1000);
	const int32 Distance = SimCruisePreferences::GetInt(TEXT("view_distance_cm"), -1, -1, 400);
	if (Tenths > 0) SetVar(TEXT("simcruise.Lens.ScreenInches"), Tenths / 10.f);
	if (Distance > 0) SetVar(TEXT("simcruise.Lens.ViewDistanceCm"), static_cast<float>(Distance));
	if (bChanged)
	{
		Show(FString::Printf(TEXT("Reloaded %s"), *FPaths::GetCleanFilename(Path)));
	}
}
