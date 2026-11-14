// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/TaskGraphInterfaces.h"
#include "Components/BoxComponent.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruiseRoute.h"
#include "Gameplay/SimCruiseSpeedUnits.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

/**
 * The HUD's speed and rpm readouts: the unit conversion and rounding, and the
 * numbers shown while a robot drives the lowered Miata on the flat bench pad,
 * compared with the car body's own speed and the engine's own rpm.
 */
namespace SimCruiseHudReadoutTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	void RunSim(FSimCruiseVehicleBenchWorld& Bench, USimCruiseVehicleMovementComponent* Movement, double Seconds)
	{
		const double Until = Movement->GetTelemetry().SimTimeS + Seconds;
		Bench.RunUntil([Movement, Until]()
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			return Movement->GetTelemetry().SimTimeS >= Until;
		}, Seconds * 4.0 + 2.0);
	}

	/** The body's true ground speed along its nose, m/s, from the physics body itself. */
	double TrueSpeedMps(const ASimCruiseVehicle* Vehicle)
	{
		const FVector VelocityCmS = Vehicle->GetBody()->GetPhysicsLinearVelocity();
		return FVector::DotProduct(VelocityCmS, Vehicle->GetActorForwardVector()) / 100.0;
	}
}

using namespace SimCruiseHudReadoutTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHudUnitsTest, "SimCruise.Hud.Units", SimCruiseHudReadoutTest::Flags)
bool FSimCruiseHudUnitsTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseSpeedUnits;
	const ESimCruiseSpeedUnits Mph = ESimCruiseSpeedUnits::Mph;
	const ESimCruiseSpeedUnits Kmh = ESimCruiseSpeedUnits::Kmh;

	// Conversion: 60 mph is exactly 26.8224 m/s; 100 km/h is 27.78 m/s.
	TestTrue(TEXT("26.8224 m/s is 60 mph"), FMath::IsNearlyEqual(FromMps(26.8224, Mph), 60.0, 1e-9));
	TestTrue(TEXT("26.8224 m/s is 96.56 km/h"), FMath::IsNearlyEqual(FromMps(26.8224, Kmh), 96.56064, 1e-9));
	TestTrue(TEXT("1 m/s is 3.6 km/h"), FMath::IsNearlyEqual(FromMps(1.0, Kmh), 3.6, 1e-12));
	TestEqual(TEXT("60 mph shows 60"), Shown(26.8224, Mph), 60);
	TestEqual(TEXT("60 mph shows 97 km/h"), Shown(26.8224, Kmh), 97);
	TestEqual(TEXT("100 km/h shows 100"), Shown(100.0 / 3.6, Kmh), 100);
	TestEqual(TEXT("100 km/h shows 62 mph"), Shown(100.0 / 3.6, Mph), 62);

	// Rounding to the nearest whole number, half up.
	TestEqual(TEXT("29.4 mph shows 29"), Shown(SimCruiseUnits::MphToMps(29.4), Mph), 29);
	TestEqual(TEXT("29.6 mph shows 30"), Shown(SimCruiseUnits::MphToMps(29.6), Mph), 30);
	TestEqual(TEXT("48.5 km/h shows 49"), Shown(48.5 / 3.6, Kmh), 49);

	// Never negative: reverse shows its size; a crawl and a stop show 0; a bad number shows 0.
	TestEqual(TEXT("reverse at 10 m/s shows 22 mph"), Shown(-10.0, Mph), 22);
	TestEqual(TEXT("reverse at 10 m/s shows 36 km/h"), Shown(-10.0, Kmh), 36);
	TestEqual(TEXT("a stop shows 0"), Shown(0.0, Mph), 0);
	TestEqual(TEXT("creeping back at 0.1 m/s shows 0"), Shown(-0.1, Kmh), 0);
	TestEqual(TEXT("NaN shows 0"), Shown(std::numeric_limits<double>::quiet_NaN(), Mph), 0);

	// Rpm to the nearest 50, never negative.
	TestEqual(TEXT("874 rpm shows 850"), ShownRpm(874.0), 850);
	TestEqual(TEXT("875 rpm shows 900"), ShownRpm(875.0), 900);
	TestEqual(TEXT("6999 rpm shows 7000"), ShownRpm(6999.0), 7000);
	TestEqual(TEXT("stalled shows 0"), ShownRpm(0.0), 0);
	TestEqual(TEXT("a negative rpm shows 0"), ShownRpm(-30.0), 0);

	TestEqual(TEXT("mph label"), FString(Label(Mph)), FString(TEXT("mph")));
	TestEqual(TEXT("km/h label"), FString(Label(Kmh)), FString(TEXT("km/h")));
	TestTrue(TEXT("the menu switches mph to km/h and back"), Next(Mph) == Kmh && Next(Kmh) == Mph);

	// The HUD readout from one frame's telemetry.
	FSimCruiseDriveTelemetry T;
	T.BodyVelocityMps = FVector(-13.4112, 0.8, 0.0);
	T.EngineSpeedRpm = 2210.0;
	const FSimCruiseHudReadout ReverseMph = FSimCruiseHudReadout::From(T, Mph);
	TestEqual(TEXT("reverse at 30 mph shows 30 (sideways slide not counted)"), ReverseMph.Speed, 30);
	TestEqual(TEXT("readout rpm"), ReverseMph.Rpm, 2200);
	const FSimCruiseHudReadout ReverseKmh = FSimCruiseHudReadout::From(T, Kmh);
	TestEqual(TEXT("the same in km/h"), ReverseKmh.Speed, 48);
	TestEqual(TEXT("the mph copy stays mph"), ReverseKmh.Mph, 30);
	TestTrue(TEXT("units carried"), ReverseKmh.Units == Kmh);

	// GPS distances in both units.
	FString Number, Unit;
	SimCruiseRouteText::DistancePartsIn(1609.344, Mph, Number, Unit);
	TestEqual(TEXT("1 mile"), Number + TEXT(" ") + Unit, FString(TEXT("1.0 mi")));
	SimCruiseRouteText::DistancePartsIn(100.0, Mph, Number, Unit);
	TestEqual(TEXT("100 m in feet"), Number + TEXT(" ") + Unit, FString(TEXT("330 ft")));
	SimCruiseRouteText::DistancePartsIn(1609.344, Kmh, Number, Unit);
	TestEqual(TEXT("1 mile in km"), Number + TEXT(" ") + Unit, FString(TEXT("1.6 km")));
	SimCruiseRouteText::DistancePartsIn(333.0, Kmh, Number, Unit);
	TestEqual(TEXT("333 m"), Number + TEXT(" ") + Unit, FString(TEXT("330 m")));
	SimCruiseRouteText::DistancePartsIn(720.0, Kmh, Number, Unit);
	TestEqual(TEXT("720 m to 50 m"), Number + TEXT(" ") + Unit, FString(TEXT("700 m")));
	SimCruiseRouteText::DistancePartsIn(960.0, Kmh, Number, Unit);
	TestEqual(TEXT("960 m as km"), Number + TEXT(" ") + Unit, FString(TEXT("1.0 km")));
	SimCruiseRouteText::DistancePartsIn(23400.0, Kmh, Number, Unit);
	TestEqual(TEXT("23.4 km"), Number + TEXT(" ") + Unit, FString(TEXT("23 km")));
	SimCruiseRouteText::DistancePartsIn(2.0, Kmh, Number, Unit);
	TestEqual(TEXT("at the turn"), Number + TEXT(" ") + Unit, FString(TEXT("10 m")));

	// The driver's setting: mph by default (checks have no preferences), km/h after the switch.
	const ESimCruiseSpeedUnits Before = Get();
	Set(Mph);
	FString Default;
	SimCruiseRouteText::DistanceParts(1609.344, Default, Unit);
	TestEqual(TEXT("GPS follows mph"), Unit, FString(TEXT("mi")));
	Set(Kmh);
	SimCruiseRouteText::DistanceParts(1609.344, Default, Unit);
	TestEqual(TEXT("GPS follows km/h"), Unit, FString(TEXT("km")));
	Set(Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHudReadoutDriveTest, "SimCruise.Hud.ReadoutDrive", SimCruiseHudReadoutTest::Flags)
bool FSimCruiseHudReadoutDriveTest::RunTest(const FString& Parameters)
{
	// the lowered Miata on the flat bench pad, driven by a robot through the shell's control interface:
	// the same FSimCruiseDriveTelemetry the player controller hands the HUD every frame.
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(*this, TEXT("nb2001_lowered"), Car))
	{
		return false;
	}
	FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS, 3000.0, false);
	ASimCruiseVehicle* Vehicle = Bench.SpawnVehicle(Car, FVector3d(-2500.0, 0.0, 0.03), nullptr, true);
	if (!TestNotNull(TEXT("vehicle"), Vehicle))
	{
		return false;
	}
	USimCruiseVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement();
	ISimCruiseVehicleControl* Control = Cast<ISimCruiseVehicleControl>(Vehicle);
	if (!TestNotNull(TEXT("the vehicle is the shell's ISimCruiseVehicleControl"), Control))
	{
		return false;
	}
	const double TyreRadiusM = Car.TireFreeRadiusM();

	// One HUD frame: what the HUD shows in both units against the body's speed and the engine's rpm.
	auto Check = [&](const TCHAR* Where, bool bMoving)
	{
		FSimCruiseDriveTelemetry T;
		Control->GetVehicleTelemetry(T);
		const double TrueMps = TrueSpeedMps(Vehicle);
		const double TrueMph = FMath::Abs(SimCruiseSpeedUnits::FromMps(TrueMps, ESimCruiseSpeedUnits::Mph));
		const double TrueKmh = FMath::Abs(SimCruiseSpeedUnits::FromMps(TrueMps, ESimCruiseSpeedUnits::Kmh));
		const double EngineRpm = Movement->GetTelemetry().EngineRpm;
		const double WheelRpm = FMath::Abs(TrueMps) / FMath::Max(TyreRadiusM, 0.1) * 60.0 / UE_DOUBLE_TWO_PI;
		const FSimCruiseHudReadout InMph = FSimCruiseHudReadout::From(T, ESimCruiseSpeedUnits::Mph);
		const FSimCruiseHudReadout InKmh = FSimCruiseHudReadout::From(T, ESimCruiseSpeedUnits::Kmh);
		AddInfo(FString::Printf(TEXT("HUDREADOUT %s: shown %d mph / %d km/h, true %.2f mph / %.2f km/h; shown %d rpm, engine %.0f rpm, wheel %.0f rpm, gear %d"),
			Where, InMph.Speed, InKmh.Speed, TrueMph, TrueKmh, InMph.Rpm, EngineRpm, WheelRpm, T.Gear));
		TestTrue(FString::Printf(TEXT("%s: shown mph within 1 of the true speed"), Where), FMath::Abs(InMph.Speed - TrueMph) <= 1.0);
		TestTrue(FString::Printf(TEXT("%s: shown km/h within 1 of the true speed"), Where), FMath::Abs(InKmh.Speed - TrueKmh) <= 1.0);
		TestTrue(FString::Printf(TEXT("%s: never negative"), Where), InMph.Speed >= 0 && InKmh.Speed >= 0);
		TestTrue(FString::Printf(TEXT("%s: shown rpm within 50 of Telemetry.EngineSpeedRpm"), Where), FMath::Abs(InMph.Rpm - T.EngineSpeedRpm) <= 50.0);
		TestTrue(FString::Printf(TEXT("%s: the telemetry rpm is the engine's own"), Where), FMath::IsNearlyEqual(T.EngineSpeedRpm, EngineRpm, 25.0));
		if (bMoving)
		{
			TestTrue(FString::Printf(TEXT("%s: the rpm is the engine's, not a wheel's"), Where), FMath::Abs(InMph.Rpm - WheelRpm) > 300.0);
		}
		return TrueMph;
	};

	// At a stop with the engine running: 0 and idle (not 0 rpm).
	RunSim(Bench, Movement, 1.5);
	{
		FSimCruiseDriveTelemetry T;
		Control->GetVehicleTelemetry(T);
		const FSimCruiseHudReadout Idle = FSimCruiseHudReadout::From(T, ESimCruiseSpeedUnits::Mph);
		Check(TEXT("idle"), false);
		TestEqual(TEXT("at a stop the HUD shows 0"), Idle.Speed, 0);
		TestEqual(TEXT("at a stop the HUD shows 0 km/h"), FSimCruiseHudReadout::From(T, ESimCruiseSpeedUnits::Kmh).Speed, 0);
		TestTrue(FString::Printf(TEXT("idle rpm %d is above 500"), Idle.Rpm), Idle.Rpm > 500);
	}

	// Full throttle until 30 mph, then on to 60 mph (automatic, straight ahead).
	FSimCruiseDriverCommand Command;
	Command.Throttle = 1.f;
	const double Targets[] = { 30.0, 60.0 };
	for (const double Target : Targets)
	{
		double TrueMph = 0.0;
		for (int32 Step = 0; Step < 400; ++Step)
		{
			Control->ApplyDriverCommand(Command);
			RunSim(Bench, Movement, 0.05);
			TrueMph = FMath::Abs(SimCruiseSpeedUnits::FromMps(TrueSpeedMps(Vehicle), ESimCruiseSpeedUnits::Mph));
			if (TrueMph >= Target) break;
		}
		TestTrue(FString::Printf(TEXT("the Miata reached %.0f mph (%.1f)"), Target, TrueMph), TrueMph >= Target);
		Check(*FString::Printf(TEXT("%.0f mph"), Target), true);
	}

	// Lift and brake to a stop: back to 0 and idle. The pedal comes off above walking pace (a held
	// brake at a crawl selects reverse in automatic) and the parking brake holds the car.
	Command.Throttle = 0.f;
	Command.Brake = 1.f;
	for (int32 Step = 0; Step < 400 && FMath::Abs(TrueSpeedMps(Vehicle)) > 1.0; ++Step)
	{
		Control->ApplyDriverCommand(Command);
		RunSim(Bench, Movement, 0.05);
	}
	Command.Brake = 0.f;
	Command.ParkingBrake = 1.f;
	Control->ApplyDriverCommand(Command);
	RunSim(Bench, Movement, 2.0);
	{
		FSimCruiseDriveTelemetry T;
		Control->GetVehicleTelemetry(T);
		const FSimCruiseHudReadout Stopped = FSimCruiseHudReadout::From(T, ESimCruiseSpeedUnits::Mph);
		Check(TEXT("stopped again"), false);
		TestEqual(TEXT("stopped again: the HUD shows 0"), Stopped.Speed, 0);
		TestTrue(FString::Printf(TEXT("stopped again: idle rpm %d is above 500"), Stopped.Rpm), Stopped.Rpm > 500);
	}
	return true;
}

#endif
