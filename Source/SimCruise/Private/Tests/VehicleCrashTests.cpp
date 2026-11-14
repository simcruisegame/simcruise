// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

/**
 * Release gate, row F of docs/release/driving-checklist.md: a car that hits a
 * wall, a tree, a guard rail or a stopped car must not launch into the air,
 * pass through the obstacle or produce a non-finite state, and the reset must
 * put it back on its wheels so that it drives on.
 */
namespace
{
	constexpr EAutomationTestFlags CrashTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	/** A static box obstacle; centre and half size in metres, yaw in degrees. */
	AActor* SpawnObstacle(UWorld* World, const FVector3d& CentreM, const FVector3d& HalfSizeM, double YawDeg)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("CrashObstacle"));
		Box->SetBoxExtent(SimCruiseUnits::ToUnrealLength(HalfSizeM), false);
		Box->SetMobility(EComponentMobility::Static);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Actor->SetRootComponent(Box);
		Box->SetWorldLocationAndRotation(SimCruiseUnits::ToUnrealLength(CentreM), FRotator(0.0, YawDeg, 0.0));
		Box->RegisterComponent();
		return Actor;
	}

	struct FCrashCase
	{
		const TCHAR* Name;
		double ImpactKph;
		/** Builds the obstacle in front of a car that starts at the origin facing +X; returns the X (m) the car's centre must not pass. */
		TFunction<double(UWorld*, const FSimCruiseCarDefinition&)> Build;
	};

	struct FCrashResult
	{
		double ImpactKph = 0.0;
		double PeakRiseM = 0.0;
		double PeakUpMps = 0.0;
		double MaxX = -1.0e9;
		double PeakTiltDeg = 0.0;
		bool bFinite = true;
	};

	bool IsFiniteState(const ASimCruiseVehicle* Vehicle, const FSimCruiseVehicleTelemetry& T)
	{
		return !Vehicle->GetActorLocation().ContainsNaN() && !Vehicle->GetActorRotation().ContainsNaN() && FMath::IsFinite(T.ForwardSpeedMps) &&
			FMath::IsFinite(T.EngineRpm) && !T.BodyVelocityLocalMps.ContainsNaN() && !T.BodyAngularVelocityLocalRadPerSecond.ContainsNaN();
	}

	bool RunCrashTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const double HalfLengthM = Car.CollisionBoxSizeM.Z * 0.5;
		const TArray<FCrashCase> Cases = {
			// A concrete wall square across the road at 60 km/h.
			{ TEXT("wall"), 60.0, [](UWorld* World, const FSimCruiseCarDefinition&)
				{
					SpawnObstacle(World, FVector3d(80.0, 0.0, 1.5), FVector3d(0.5, 10.0, 1.5), 0.0);
					return 79.5;
				} },
			// A tree trunk (0.4 m) hit off centre, under the left headlamp, at 50 km/h.
			{ TEXT("tree"), 50.0, [](UWorld* World, const FSimCruiseCarDefinition&)
				{
					SpawnObstacle(World, FVector3d(80.0, -0.5, 3.0), FVector3d(0.2, 0.2, 3.0), 0.0);
					return 79.8;
				} },
			// A guard rail (0.75 m high) crossed at 15 degrees at 80 km/h.
			{ TEXT("guardrail"), 80.0, [](UWorld* World, const FSimCruiseCarDefinition&)
				{
					// The rail runs at 15 deg to the car's path and crosses it at X = 100 m.
					SpawnObstacle(World, FVector3d(100.0, 0.0, 0.375), FVector3d(60.0, 0.1, 0.375), 15.0);
					return 1.0e9; // judged by the lateral check below
				} },
		};

		for (const FCrashCase& Case : Cases)
		{
			FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS, 3000.0, false);
			if (!Test.TestTrue(TEXT("bench world"), Bench.IsValid()))
			{
				return false;
			}
			const double LimitX = Case.Build(Bench.GetWorld(), Car);
			ASimCruiseVehicle* Vehicle = Bench.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.05), nullptr, true);
			if (!Test.TestNotNull(TEXT("vehicle"), Vehicle))
			{
				return false;
			}
			USimCruiseVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement();
			auto Telemetry = [Movement]() { return Movement->GetTelemetry(); };
			Bench.RunUntil([&]() { return Telemetry().SimTimeS > 1.0; }, 5.0);
			const double RestZ = Vehicle->GetActorLocation().Z / 100.0;

			// Accelerate to the impact speed, then hold it with a light pedal.
			FCrashResult R;
			Vehicle->SetThrottle(1.0f);
			bool bAtSpeed = false;
			double PrevZ = RestZ;
			double PrevT = Telemetry().SimTimeS;
			double AfterImpactS = -1.0;
			double Y0 = Vehicle->GetActorLocation().Y / 100.0;
			bool bCrossedRail = false;
			Bench.RunUntil([&]()
			{
				const FSimCruiseVehicleTelemetry T = Telemetry();
				if (!IsFiniteState(Vehicle, T))
				{
					R.bFinite = false;
					return true;
				}
				if (!bAtSpeed && T.SpeedKph >= Case.ImpactKph)
				{
					bAtSpeed = true;
					Vehicle->SetThrottle(0.0f);
				}
				const FVector P = Vehicle->GetActorLocation() / 100.0;
				const double Dt = T.SimTimeS - PrevT;
				if (Dt > 1.0e-4)
				{
					R.PeakUpMps = FMath::Max(R.PeakUpMps, (P.Z - PrevZ) / Dt);
					PrevZ = P.Z;
					PrevT = T.SimTimeS;
				}
				R.PeakRiseM = FMath::Max(R.PeakRiseM, P.Z - RestZ);
				R.MaxX = FMath::Max(R.MaxX, P.X);
				const FRotator Rot = Vehicle->GetActorRotation();
				R.PeakTiltDeg = FMath::Max(R.PeakTiltDeg, FMath::Max(FMath::Abs(Rot.Roll), FMath::Abs(Rot.Pitch)));
				if (Case.Name == FString(TEXT("guardrail")))
				{
					// The rail line: points (100, 0) + s (cos 15, sin 15); signed distance from it.
					// The car starts on the side away from the rail's left normal (positive), so
					// passing through the rail makes the distance negative.
					const double S = FMath::DegreesToRadians(15.0);
					const double Side = -(P.X - 100.0) * FMath::Sin(S) + (P.Y - Y0) * FMath::Cos(S);
					bCrossedRail |= Side < -0.25;
				}
				// The impact: a sudden loss of speed after the car is at speed.
				if (bAtSpeed && AfterImpactS < 0.0 && T.SpeedKph < Case.ImpactKph * 0.8)
				{
					AfterImpactS = T.SimTimeS;
				}
				if (AfterImpactS < 0.0)
				{
					R.ImpactKph = T.SpeedKph;
				}
				return AfterImpactS > 0.0 && T.SimTimeS - AfterImpactS > 3.0;
			}, 60.0);

			const FString Line = FString::Printf(TEXT("CRASH %-11s %-9s impact %.0f km/h, peak rise %.2f m, peak up speed %.2f m/s, peak tilt %.0f deg, max x %.2f m (limit %.2f), finite %d%s"),
				CarId, Case.Name, R.ImpactKph, R.PeakRiseM, R.PeakUpMps, R.PeakTiltDeg, R.MaxX, LimitX < 1.0e8 ? LimitX - HalfLengthM : -1.0,
				R.bFinite ? 1 : 0, bCrossedRail ? TEXT(", CROSSED THE RAIL") : TEXT(""));
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			Test.TestTrue(FString::Printf(TEXT("%s: the state stays finite"), Case.Name), R.bFinite);
			Test.TestTrue(FString::Printf(TEXT("%s: the car reached the obstacle"), Case.Name), AfterImpactS > 0.0);
			Test.TestTrue(FString::Printf(TEXT("%s: no launch (rise %.2f m)"), Case.Name, R.PeakRiseM), R.PeakRiseM < 0.6);
			Test.TestTrue(FString::Printf(TEXT("%s: no launch (up speed %.2f m/s)"), Case.Name, R.PeakUpMps), R.PeakUpMps < 3.0);
			Test.TestTrue(FString::Printf(TEXT("%s: the car does not roll over (tilt %.0f deg)"), Case.Name, R.PeakTiltDeg), R.PeakTiltDeg < 60.0);
			if (LimitX < 1.0e8)
			{
				Test.TestTrue(FString::Printf(TEXT("%s: no tunnelling (centre x %.2f m, face at %.2f m)"), Case.Name, R.MaxX, LimitX), R.MaxX < LimitX);
			}
			else
			{
				Test.TestFalse(TEXT("guardrail: the car does not pass through the rail"), bCrossedRail);
			}

			// Recover: the reset puts the car on its wheels and it drives on.
			Vehicle->SetThrottle(0.0f);
			Vehicle->ResetAt(FVector(0.0, 2000.0, 0.0), 0.0f);
			const double ResetAt = Telemetry().SimTimeS;
			Bench.RunUntil([&]() { return Telemetry().SimTimeS > ResetAt + 1.0; }, 5.0);
			Test.TestEqual(FString::Printf(TEXT("%s: four wheels down after the reset"), Case.Name), Telemetry().WheelContactCount, 4);
			Vehicle->SetThrottle(0.5f);
			Bench.RunUntil([&]() { return Telemetry().SimTimeS > ResetAt + 4.0; }, 10.0);
			const FSimCruiseVehicleTelemetry After = Telemetry();
			Test.TestTrue(FString::Printf(TEXT("%s: drives on after the reset (%.1f m/s)"), Case.Name, After.ForwardSpeedMps), After.ForwardSpeedMps > 3.0);
			Test.TestTrue(FString::Printf(TEXT("%s: finite after the reset"), Case.Name), IsFiniteState(Vehicle, After));
		}

		// A stopped car of the same model, hit square at 50 km/h: neither car launches or passes through the other.
		{
			FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS, 3000.0, false);
			ASimCruiseVehicle* Vehicle = Bench.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.05), nullptr, true);
			ASimCruiseVehicle* Parked = Bench.SpawnVehicle(Car, FVector3d(80.0, 0.0, 0.05), nullptr, true);
			if (!Test.TestNotNull(TEXT("vehicle"), Vehicle) || !Test.TestNotNull(TEXT("stopped car"), Parked))
			{
				return false;
			}
			USimCruiseVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement();
			auto Telemetry = [Movement]() { return Movement->GetTelemetry(); };
			Bench.RunUntil([&]() { return Telemetry().SimTimeS > 1.0; }, 5.0);
			const double RestZ = Vehicle->GetActorLocation().Z / 100.0;
			const double ParkedRestZ = Parked->GetActorLocation().Z / 100.0;
			Parked->SetBrake(1.0f);
			Vehicle->SetThrottle(1.0f);
			bool bAtSpeed = false;
			double Impact = -1.0, ImpactKph = 0.0, PeakRise = 0.0, MinGap = 1.0e9;
			bool bFinite = true;
			bool bThrough = false;
			Bench.RunUntil([&]()
			{
				const FSimCruiseVehicleTelemetry T = Telemetry();
				if (!IsFiniteState(Vehicle, T) || !IsFiniteState(Parked, Parked->GetVehicleMovement()->GetTelemetry()))
				{
					bFinite = false;
					return true;
				}
				if (!bAtSpeed && T.SpeedKph >= 50.0)
				{
					bAtSpeed = true;
					Vehicle->SetThrottle(0.0f);
				}
				if (bAtSpeed && Impact < 0.0 && T.SpeedKph < 40.0)
				{
					Impact = T.SimTimeS;
				}
				if (Impact < 0.0)
				{
					ImpactKph = T.SpeedKph;
				}
				// Passing through: the striking car's centre ends up ahead of the stopped car's.
				bThrough |= Vehicle->GetActorLocation().X > Parked->GetActorLocation().X;
				PeakRise = FMath::Max(PeakRise, FMath::Max(Vehicle->GetActorLocation().Z / 100.0 - RestZ, Parked->GetActorLocation().Z / 100.0 - ParkedRestZ));
				MinGap = FMath::Min(MinGap, (Parked->GetActorLocation().X - Vehicle->GetActorLocation().X) / 100.0);
				return Impact > 0.0 && T.SimTimeS - Impact > 3.0;
			}, 60.0);
			const FString Line = FString::Printf(TEXT("CRASH %-11s car       impact %.0f km/h, peak rise %.2f m, closest centres %.2f m (car length %.2f m), finite %d%s"),
				CarId, ImpactKph, PeakRise, MinGap, HalfLengthM * 2.0, bFinite ? 1 : 0, bThrough ? TEXT(", PASSED THROUGH") : TEXT(""));
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			Test.TestTrue(TEXT("car: the state stays finite"), bFinite);
			Test.TestTrue(TEXT("car: the cars met"), Impact > 0.0);
			Test.TestTrue(FString::Printf(TEXT("car: no launch (rise %.2f m)"), PeakRise), PeakRise < 0.6);
			Test.TestFalse(FString::Printf(TEXT("car: no pass-through (closest centres %.2f m)"), MinGap), bThrough);
		}
		return true;
	}
}

#define SIMCRUISE_VEHICLE_CRASH(CarIdent) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleCrash_##CarIdent, "SimCruise.Vehicle.Crash." #CarIdent, CrashTestFlags) \
	bool FSimCruiseVehicleCrash_##CarIdent::RunTest(const FString& Parameters) { return RunCrashTest(*this, TEXT(#CarIdent)); }

SIMCRUISE_VEHICLE_CRASH(nb2001)
SIMCRUISE_VEHICLE_CRASH(nb2001_lowered)
SIMCRUISE_VEHICLE_CRASH(mustang65)
SIMCRUISE_VEHICLE_CRASH(meridian_z)

#endif
