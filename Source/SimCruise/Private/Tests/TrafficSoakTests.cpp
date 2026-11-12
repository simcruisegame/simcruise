// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "World/SimCruiseTraffic.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"

/**
 * Traffic deadlock soak: 40 cars drive the bundled Miami Beach streets for four minutes
 * of game time with no player in the way. Before the gridlock rules, a ring of cars at a
 * junction (each waiting for the next) stood for good within minutes, with a queue behind
 * it; with them, no car may stand longer than 40 s (a long red-free wait at a stop is
 * seconds, a queue behind a car that stands for good turns off after 25 s).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseTrafficSoakTest, "SimCruise.World.TrafficSoak",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseTrafficSoakTest::RunTest(const FString& Parameters)
{
	FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS, 0.5, false);
	if (!TestTrue(TEXT("bench world"), Bench.IsValid()))
	{
		return false;
	}
	ASimCruiseWorldStreamer* Streamer = Bench.GetWorld()->SpawnActor<ASimCruiseWorldStreamer>();
	Streamer->bUseHelper = false;
	Streamer->StartAt(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat, TEXT("traffic soak"));
	const FVector Start = Streamer->GetFrame().LonLatToWorldCm(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat);
	const double Began = FPlatformTime::Seconds();
	Bench.RunUntil([&]()
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.002f);
		return Streamer->AreTilesReadyAround(Start, 1) || FPlatformTime::Seconds() - Began > 240.0;
	}, 1.0e6);
	if (!TestTrue(TEXT("the start tiles stream"), Streamer->AreTilesReadyAround(Start, 1)))
	{
		return false;
	}

	IConsoleVariable* CarsVar = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Traffic.Cars"));
	IConsoleVariable* SoundVar = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Traffic.Sound"));
	const int32 OldCars = CarsVar ? CarsVar->GetInt() : 20;
	const int32 OldSound = SoundVar ? SoundVar->GetInt() : 1;
	if (CarsVar) CarsVar->Set(40, ECVF_SetByCode);
	if (SoundVar) SoundVar->Set(0, ECVF_SetByCode);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASimCruiseTraffic* Traffic = Bench.GetWorld()->SpawnActor<ASimCruiseTraffic>(Params);
	Traffic->Streamer = Streamer;
	Traffic->bTestFocus = true;
	Traffic->TestFocusCm = Start;

	int32 WorstStuck = 0;
	int32 MaxCars = 0;
	double Clock = 0.0;
	Bench.RunUntil([&]()
	{
		Clock += 1.0 / 60.0;
		MaxCars = FMath::Max(MaxCars, Traffic->NumCars());
		if (Clock > 60.0)
		{
			WorstStuck = FMath::Max(WorstStuck, Traffic->CountStuckCars(40.0));
		}
		return Clock >= 240.0;
	}, 260.0);

	if (CarsVar) CarsVar->Set(OldCars, ECVF_SetByCode);
	if (SoundVar) SoundVar->Set(OldSound, ECVF_SetByCode);
	AddInfo(FString::Printf(TEXT("TRAFFIC soak: up to %d cars over 240 s; most standing over 40 s at once: %d; %d left a standing queue"),
		MaxCars, WorstStuck, Traffic->GetQueueGiveUps()));
	TestTrue(TEXT("traffic runs (at least 20 cars)"), MaxCars >= 20);
	TestEqual(TEXT("no car stands for more than 40 s (no gridlock)"), WorstStuck, 0);
	return true;
}

#endif
