// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Vehicle/CarDefinition.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicleSim.h"

namespace
{
	constexpr EAutomationTestFlags VehicleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleUnitsScaleTest, "SimCruise.Vehicle.Units.Scale", VehicleTestFlags)
bool FSimCruiseVehicleUnitsScaleTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseUnits;
	TestEqual(TEXT("1 m is 100 cm"), ToUnrealLength(1.0), 100.0);
	TestEqual(TEXT("250 cm is 2.5 m"), ToSILength(250.0), 2.5);
	TestEqual(TEXT("velocity m/s -> cm/s"), ToUnrealVelocity(FVector3d(1.0, -2.0, 3.0)), FVector3d(100.0, -200.0, 300.0));
	// F = m a: 1 N accelerates 1 kg by 1 m/s^2 = 100 cm/s^2, so 1 N is 100 kg cm/s^2.
	TestEqual(TEXT("1 N is 100 kg cm/s^2"), ToUnrealForce(1.0), 100.0);
	TestEqual(TEXT("force round trip"), ToSIForce(ToUnrealForce(1234.5)), 1234.5);
	// tau = I alpha: 1 N m on 1 kg m^2 is 1 rad/s^2; 1 kg m^2 is 1e4 kg cm^2, so 1 N m is 1e4 kg cm^2/s^2.
	TestEqual(TEXT("1 N m is 1e4 kg cm^2/s^2"), ToUnrealTorque(1.0), 1.0e4);
	TestEqual(TEXT("1 kg m^2 is 1e4 kg cm^2"), ToUnrealInertia(1.0), 1.0e4);
	TestEqual(TEXT("torque and inertia scale together (rad/s^2 is unitless in length)"),
		ToUnrealTorque(37.0) / ToUnrealInertia(2.0), 37.0 / 2.0);
	TestEqual(TEXT("force and mass give cm/s^2"), ToUnrealForce(980.665) / 100.0, 980.665);
	TestEqual(TEXT("180 deg is pi"), ToRadians(180.0), UE_DOUBLE_PI);
	TestEqual(TEXT("pi is 180 deg"), ToDegrees(UE_DOUBLE_PI), 180.0);
	TestEqual(TEXT("60 mph is 26.8224 m/s"), MphToMps(60.0), 26.8224, 1.0e-12);
	TestEqual(TEXT("70 mph is 31.2928 m/s"), MphToMps(70.0), 31.2928, 1.0e-12);
	TestEqual(TEXT("1 m is 3.28084 ft"), MetresToFeet(1.0), 3.280839895, 1.0e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleUnitsAxesTest, "SimCruise.Vehicle.Units.Axes", VehicleTestFlags)
bool FSimCruiseVehicleUnitsAxesTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseUnits;
	// Godot: X right, Y up, -Z forward. Unreal: X forward, Y right, Z up.
	TestEqual(TEXT("Godot forward (-Z) is Unreal +X"), GodotToUnrealAxes(FVector3d(0, 0, -1)), FVector3d(1, 0, 0));
	TestEqual(TEXT("Godot right (+X) is Unreal +Y"), GodotToUnrealAxes(FVector3d(1, 0, 0)), FVector3d(0, 1, 0));
	TestEqual(TEXT("Godot up (+Y) is Unreal +Z"), GodotToUnrealAxes(FVector3d(0, 1, 0)), FVector3d(0, 0, 1));
	const FVector3d Sample(0.3, -1.7, 2.9);
	TestEqual(TEXT("axes round trip"), UnrealToGodotAxes(GodotToUnrealAxes(Sample)), Sample);
	// Moments: Godot (pitch about X, yaw about Y, roll about Z) -> Unreal (roll, pitch, yaw).
	TestEqual(TEXT("NB inertia in Unreal axes"), GodotInertiaToUnrealAxes(FVector3d(1130, 1260, 370)), FVector3d(370, 1130, 1260));

	// The vehicle geometry matches Godot's mounts and centre of mass once mapped.
	FSimCruiseCarDefinition Car;
	FSimCruiseVehicleSim Sim;
	Sim.Initialize(Car);
	const double HalfWheelbase = Car.WheelbaseM * 0.5;
	const FVector3d GodotMounts[4] = {
		FVector3d(-Car.FrontTrackM * 0.5, Car.SuspensionMountHeightM, -HalfWheelbase),
		FVector3d(Car.FrontTrackM * 0.5, Car.SuspensionMountHeightM, -HalfWheelbase),
		FVector3d(-Car.RearTrackM * 0.5, Car.SuspensionMountHeightM, HalfWheelbase),
		FVector3d(Car.RearTrackM * 0.5, Car.SuspensionMountHeightM, HalfWheelbase) };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestEqual(FString::Printf(TEXT("mount %d"), Index),
			Sim.GetMountLocalM(Index) - Sim.GetReferenceOriginLocalM(), GodotToUnrealAxes(GodotMounts[Index]), 1.0e-9f);
	}
	const FVector3d GodotCentreOfMass(0.0, Car.CenterOfGravityHeightM, (0.5 - Car.FrontWeightFraction) * Car.WheelbaseM);
	TestEqual(TEXT("centre of mass"), Sim.GetCenterOfMassLocalM() - Sim.GetReferenceOriginLocalM(),
		GodotToUnrealAxes(GodotCentreOfMass), 1.0e-9f);
	TestTrue(TEXT("front left mount is forward and left"), Sim.GetMountLocalM(0).X > 0.0 && Sim.GetMountLocalM(0).Y < 0.0);
	return true;
}

#endif
