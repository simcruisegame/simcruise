// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/TaskGraphInterfaces.h"
#include "Components/PrimitiveComponent.h"
#include "Gameplay/SimCruiseHotReload.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

namespace
{
	constexpr EAutomationTestFlags HotReloadFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	void RunPhysics(FSimCruiseVehicleBenchWorld& Bench, USimCruiseVehicleMovementComponent* Movement, double Seconds)
	{
		const double Until = Movement->GetTelemetry().SimTimeS + Seconds;
		Bench.RunUntil([Movement, Until]()
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			return Movement->GetTelemetry().SimTimeS >= Until;
		}, Seconds * 4.0 + 2.0);
	}

	/** Points the car loader's calibration overlays at a folder of the test's own, and back. */
	struct FScopedCalibrationDir
	{
		FString Before;
		FString Dir;
		FScopedCalibrationDir()
		{
			Before = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_CALIBRATION_DIR"));
			Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
				TEXT("HotReload-") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
			IFileManager::Get().MakeDirectory(*Dir, true);
			FPlatformMisc::SetEnvironmentVar(TEXT("SIMCRUISE_CALIBRATION_DIR"), *Dir);
		}
		~FScopedCalibrationDir()
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("SIMCRUISE_CALIBRATION_DIR"), *Before);
			IFileManager::Get().DeleteDirectory(*Dir, false, true);
		}
	};
}

/**
 * Hot reload end to end on a bench car: a calibration overlay saved while the car stands on the
 * pad is seen by the watcher once it has settled for a second, classified as that car's file,
 * and the reloaded mass is live on the car, its movement model and its Chaos body.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHotReloadCarTest, "SimCruise.Drive.HotReload", HotReloadFlags)
bool FSimCruiseHotReloadCarTest::RunTest(const FString& Parameters)
{
	const FScopedCalibrationDir Calibration;
	TestEqual(TEXT("the loader's overlay folder is the test's"), SimCruiseHotReload::CalibrationDir(), Calibration.Dir);

	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(*this, TEXT("nb1999"), Car))
	{
		return false;
	}
	FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS, 3000.0, false);
	ASimCruiseVehicle* Vehicle = Bench.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.03), nullptr, true);
	if (!TestNotNull(TEXT("vehicle"), Vehicle))
	{
		return false;
	}
	USimCruiseVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement();
	RunPhysics(Bench, Movement, 0.5);
	const double BaseMassKg = Vehicle->GetRealCar().MassKg;
	const double NewMassKg = BaseMassKg + 150.0;

	FSimCruiseFileWatcher Watcher;
	SimCruiseHotReload::WatchGameData(Watcher);
	Watcher.Baseline(0.0);
	TestEqual(TEXT("nothing changed yet"), Watcher.Poll(0.2).Num(), 0);

	const FString Overlay = FPaths::Combine(Calibration.Dir, TEXT("nb1999.json"));
	TestTrue(TEXT("overlay written"), FFileHelper::SaveStringToFile(
		FString::Printf(TEXT("{ \"_note\": \"hot reload test\", \"mass_kg\": %.1f }"), NewMassKg), *Overlay));
	TestEqual(TEXT("a fresh save waits until it settles"), Watcher.Poll(0.5).Num(), 0);
	const TArray<FString> Changed = Watcher.Poll(1.6);
	if (!TestEqual(TEXT("one settled change"), Changed.Num(), 1))
	{
		return false;
	}
	FString CarId;
	TestTrue(TEXT("an overlay feeds the car"), SimCruiseHotReload::Classify(Changed[0], CarId) == ESimCruiseHotReloadKind::Car);
	TestEqual(TEXT("for nb1999"), CarId, FString(TEXT("nb1999")));
	TestEqual(TEXT("reported once"), Watcher.Poll(3.0).Num(), 0);

	FString Error;
	if (!TestTrue(TEXT("reloaded in place"), SimCruiseHotReload::ReloadCarInPlace(Vehicle, Error)))
	{
		AddError(Error);
		return false;
	}
	RunPhysics(Bench, Movement, 1.0);
	TestEqual(TEXT("the car's definition has the new mass"), Vehicle->GetRealCar().MassKg, NewMassKg, 1e-6);
	TestEqual(TEXT("the movement model has the new mass"), Movement->GetCarDefinition().MassKg, NewMassKg, 1e-6);
	if (const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Vehicle->GetRootComponent()))
	{
		TestEqual(TEXT("the Chaos body has the new mass"), static_cast<double>(Body->GetMass()), NewMassKg, 0.5);
	}
	TestTrue(TEXT("the overlay is named on the car"), Vehicle->GetRealCar().CalibrationOverlayPath.Contains(TEXT("HotReload-")));
	TestTrue(TEXT("the car stands where it stood"), FVector2D(Vehicle->GetActorLocation()).Size() < 20.0);
	TestTrue(TEXT("at rest on its wheels"), Movement->GetTelemetry().SpeedMps < 0.1);

	// A broken save keeps the car as it was.
	TestTrue(TEXT("broken overlay written"), FFileHelper::SaveStringToFile(TEXT("{ \"mass_kg\": "), *Overlay));
	TestFalse(TEXT("a broken file does not reload"), SimCruiseHotReload::ReloadCarInPlace(Vehicle, Error));
	TestEqual(TEXT("the car keeps the last good mass"), Vehicle->GetRealCar().MassKg, NewMassKg, 1e-6);

	FString Ignored;
	TestTrue(TEXT("look.json is the look"), SimCruiseHotReload::Classify(SimCruiseHotReload::LookPath(), Ignored) == ESimCruiseHotReloadKind::Look);
	return true;
}

#endif
