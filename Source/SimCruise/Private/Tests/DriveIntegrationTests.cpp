// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/TaskGraphInterfaces.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"

/**
 * The streams joined up: the driving shell's interface on the real vehicle,
 * the car model's pose from the vehicle's telemetry, and the car on the
 * streamed Ocean Drive tiles.
 */
namespace SimCruiseDriveTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	/** Run the bench for Seconds of physics time. */
	void WaitSim(FSimCruiseVehicleBenchWorld& Bench, USimCruiseVehicleMovementComponent* Movement, double Seconds)
	{
		const double Until = Movement->GetTelemetry().SimTimeS + Seconds;
		Bench.RunUntil([Movement, Until]()
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			return Movement->GetTelemetry().SimTimeS >= Until;
		}, Seconds * 4.0 + 2.0);
	}
}

using namespace SimCruiseDriveTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseDriveMappingTest, "SimCruise.Drive.Mapping", SimCruiseDriveTest::Flags)
bool FSimCruiseDriveMappingTest::RunTest(const FString& Parameters)
{
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(*this, TEXT("nb1999"), Car))
	{
		return false;
	}
	FSimCruiseVehicleTelemetry In;
	In.Wheels.SetNum(4);
	In.Gear = 2;
	In.bManual = true;
	In.bServiceBrake = true;
	In.SteeringAngleDeg = 5.0;
	In.BodyVelocityLocalMps = FVector(12.0, -0.5, 0.0);
	In.BodyAngularVelocityLocalRadPerSecond = FVector(0.0, 0.0, 0.3);
	for (int32 I = 0; I < 4; ++I)
	{
		FSimCruiseWheelTelemetry& W = In.Wheels[I];
		W.bContact = true;
		W.SteerAngleDeg = I < 2 ? 5.0 : 0.0;
		W.InclinationDeg = 2.0;
		W.SpinAngleDeg = 725.0;
		W.SlipAngleDeg = -3.0;
		W.SuspensionLengthCm = Car.StaticSuspensionLengthM * 100.0 - 1.5;
		W.AbsRelease = I == 0 ? 0.5 : 1.0;
		W.LoadN = 2500.0;
	}

	FSimCruiseCarVisualState Visual;
	ASimCruiseVehicle::ToVisualState(Car, In, Visual);
	TestTrue(TEXT("a right turn shows as a negative (rightward) model steer"), Visual.Wheels[0].SteerAngleRad < 0.f && FMath::IsNearlyEqual(Visual.Wheels[0].SteerAngleRad, -FMath::DegreesToRadians(5.f), 1e-5f));
	TestTrue(TEXT("lean to the right is negative camber on the left wheel"), Visual.Wheels[0].CamberRad < 0.f);
	TestTrue(TEXT("lean to the right is positive camber on the right wheel"), Visual.Wheels[1].CamberRad > 0.f);
	TestTrue(TEXT("spin wrapped to one turn"), FMath::IsNearlyEqual(Visual.Wheels[2].SpinAngleRad, FMath::DegreesToRadians(5.f), 1e-4f));
	TestTrue(TEXT("a strut 1.5 cm short of static ride is 1.5 cm of bump"), FMath::IsNearlyEqual(Visual.Wheels[3].SuspensionTravelCm, 1.5f, 1e-3f));
	TestTrue(TEXT("brake lamps follow the service brake"), Visual.bBrake);

	FSimCruiseDriveTelemetry Drive;
	ASimCruiseVehicle::ToDriveTelemetry(Car, In, Drive);
	TestEqual(TEXT("gear"), Drive.Gear, 2);
	TestTrue(TEXT("manual"), Drive.bManualTransmission);
	TestTrue(TEXT("body velocity in the body frame"), Drive.BodyVelocityMps.Equals(FVector(12.0, -0.5, 0.0)));
	TestTrue(TEXT("yaw rate positive turning right"), FMath::IsNearlyEqual(Drive.BodyAngularVelocityRadPerS.Z, 0.3));
	TestTrue(TEXT("rack angle in radians"), FMath::IsNearlyEqual(Drive.RackAngleRad, FMath::DegreesToRadians(5.0), 1e-9));
	TestTrue(TEXT("slip angle in radians, sign kept"), FMath::IsNearlyEqual(Drive.Wheels[0].SlipAngleRad, FMath::DegreesToRadians(-3.0), 1e-9));
	TestTrue(TEXT("ABS on the releasing wheel only"), Drive.Wheels[0].bAbsActive && !Drive.Wheels[1].bAbsActive);
	TestTrue(TEXT("rear wheels driven (the roadster)"), Drive.Wheels[2].bDriven && !Drive.Wheels[0].bDriven);
	TestTrue(TEXT("suspension travel from full droop"), FMath::IsNearlyEqual(Drive.Wheels[0].SuspensionTravelM,
		Car.MaximumSuspensionTravelM - (Car.StaticSuspensionLengthM - 0.015), 1e-6));
	TestTrue(TEXT("the tyre's plateau slip angle is known"), Drive.Wheels[0].PeakSlipAngleRad > 0.02);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseDriveShellControlTest, "SimCruise.Drive.ShellControl", SimCruiseDriveTest::Flags)
bool FSimCruiseDriveShellControlTest::RunTest(const FString& Parameters)
{
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
	ISimCruiseVehicleControl* Control = Cast<ISimCruiseVehicleControl>(Vehicle);
	if (!TestNotNull(TEXT("the vehicle is the shell's ISimCruiseVehicleControl"), Control))
	{
		return false;
	}

	// The driver's eye sits in the cabin: inside the body's footprint, left of centre, above the body centre.
	const FSimCruiseVehicleSetup Setup = Control->GetVehicleSetup();
	const FVector Extent = Vehicle->GetBody()->GetUnscaledBoxExtent();
	AddInfo(FString::Printf(TEXT("DRIVE eye %s (body half size %s)"), *Setup.DriverEyeLocalCm.ToString(), *Extent.ToString()));
	TestTrue(TEXT("eye within the body's length and width"), FMath::Abs(Setup.DriverEyeLocalCm.X) < Extent.X && FMath::Abs(Setup.DriverEyeLocalCm.Y) < Extent.Y);
	TestTrue(TEXT("left-hand drive: the eye is left of centre"), Setup.DriverEyeLocalCm.Y < -20.0);
	TestTrue(TEXT("eye above the body centre"), Setup.DriverEyeLocalCm.Z > 20.0);
	TestEqual(TEXT("car id"), Setup.CarId, FString(TEXT("nb1999")));

	WaitSim(Bench, Movement, 1.0);
	FSimCruiseDriveTelemetry T;
	Control->GetVehicleTelemetry(T);
	int32 Contacts = 0;
	for (const FSimCruiseDriveWheelTelemetry& W : T.Wheels) { Contacts += W.bContact ? 1 : 0; }
	TestEqual(TEXT("four tyres on the ground"), Contacts, 4);
	TestTrue(TEXT("parked"), FMath::Abs(T.BodyVelocityMps.X) < 0.05);

	// The shell's shaped command: throttle, then a rack angle applied as given.
	FSimCruiseDriverCommand Command;
	Command.Throttle = 1.f;
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		Control->ApplyDriverCommand(Command);
		WaitSim(Bench, Movement, 1.0);
	}
	Control->GetVehicleTelemetry(T);
	AddInfo(FString::Printf(TEXT("DRIVE after 4 s of throttle through the shell interface: %.1f m/s, gear %d"), T.BodyVelocityMps.X, T.Gear));
	TestTrue(TEXT("the shell's throttle drives the car"), T.BodyVelocityMps.X > 15.0);
	TestTrue(TEXT("telemetry agrees with the vehicle's own speed"), FMath::IsNearlyEqual(T.BodyVelocityMps.X, Movement->GetTelemetry().ForwardSpeedMps, 0.2));

	Command.Throttle = 0.3f;
	Command.RackAngleRad = FMath::DegreesToRadians(1.5);
	Control->ApplyDriverCommand(Command);
	WaitSim(Bench, Movement, 1.0);
	Control->GetVehicleTelemetry(T);
	TestTrue(FString::Printf(TEXT("the rack takes the shaped angle as given (%.3f deg)"), FMath::RadiansToDegrees(T.RackAngleRad)),
		FMath::IsNearlyEqual(T.RackAngleRad, FMath::DegreesToRadians(1.5), 1e-4));
	TestTrue(TEXT("the car yaws right"), T.BodyAngularVelocityRadPerS.Z > 0.02);

	// Past the lock the rack stops at the car's parking lock.
	Command.RackAngleRad = FMath::DegreesToRadians(90.0);
	Command.Throttle = 0.f;
	Command.Brake = 1.f;
	Control->ApplyDriverCommand(Command);
	WaitSim(Bench, Movement, 0.5);
	Control->GetVehicleTelemetry(T);
	TestTrue(TEXT("rack limited to the lock"), FMath::IsNearlyEqual(FMath::RadiansToDegrees(T.RackAngleRad), Car.ParkingRackAngleDeg, 0.01));
	TestTrue(TEXT("brake lamps"), T.bServiceBrakeOn);

	// Recover: a ground pose puts the car on its wheels there.
	Command = FSimCruiseDriverCommand();
	Control->ApplyDriverCommand(Command);
	Control->ResetVehicle(FTransform(FRotator(0.0, 45.0, 0.0), FVector(2000.0, 0.0, 0.0)));
	WaitSim(Bench, Movement, 1.0);
	const double ExpectedZ = SimCruiseUnits::ToUnrealLength(Car.CollisionShapeCenterYM);
	TestTrue(FString::Printf(TEXT("reset body height %.1f cm over the ground (expected about %.1f)"), Vehicle->GetActorLocation().Z, ExpectedZ),
		FMath::Abs(Vehicle->GetActorLocation().Z - ExpectedZ) < 3.0);
	TestEqual(TEXT("reset heading"), Vehicle->GetActorRotation().Yaw, 45.0, 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseDriveOceanDriveTest, "SimCruise.Drive.OceanDrive", SimCruiseDriveTest::Flags)
bool FSimCruiseDriveOceanDriveTest::RunTest(const FString& Parameters)
{
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(*this, TEXT("nb1999"), Car))
	{
		return false;
	}
	// A tiny bench pad at the origin; the car drives on the streamed tiles.
	FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS, 0.5, false);
	if (!TestTrue(TEXT("bench world"), Bench.IsValid()))
	{
		return false;
	}
	ASimCruiseWorldStreamer* Streamer = Bench.GetWorld()->SpawnActor<ASimCruiseWorldStreamer>();
	Streamer->bUseHelper = false;
	Streamer->StartAt(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat, TEXT("test"));
	const FVector Start = Streamer->GetFrame().LonLatToWorldCm(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat);
	const double Began = FPlatformTime::Seconds();
	Bench.RunUntil([&]()
	{
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		FPlatformProcess::Sleep(0.002f);
		return Streamer->AreTilesReadyAround(Start, 1) || FPlatformTime::Seconds() - Began > 240.0;
	}, 1.0e6);
	AddInfo(FString::Printf(TEXT("DRIVE the 3 x 3 start ring was ready in %.1f s"), FPlatformTime::Seconds() - Began));
	if (!TestTrue(TEXT("the start tiles stream with collision"), Streamer->AreTilesReadyAround(Start, 1)))
	{
		return false;
	}

	FVector Road;
	FRotator Heading;
	if (!TestTrue(TEXT("Ocean Drive near the start"), Streamer->FindRoadPose(TEXT("Ocean Drive"), Start, 900.0, Road, Heading)))
	{
		return false;
	}
	ASimCruiseVehicle* Vehicle = Bench.SpawnVehicle(Car, SimCruiseUnits::ToSILength(FVector3d(Road)) + FVector3d(0.0, 0.0, 0.03), nullptr, true);
	if (!TestNotNull(TEXT("vehicle"), Vehicle))
	{
		return false;
	}
	USimCruiseVehicleMovementComponent* Movement = Vehicle->GetVehicleMovement();
	Movement->ResetVehicle(Vehicle->BodyTransformForGroundPose(Road + FVector(0.0, 0.0, 3.0), static_cast<float>(Heading.Yaw)));
	ISimCruiseVehicleControl* Control = Cast<ISimCruiseVehicleControl>(Vehicle);

	WaitSim(Bench, Movement, 2.0);
	FSimCruiseVehicleTelemetry T = Movement->GetTelemetry();
	const double BodyOverRoadCm = Vehicle->GetActorLocation().Z - Road.Z;
	AddInfo(FString::Printf(TEXT("DRIVE parked on Ocean Drive: %d wheels down, body centre %.1f cm over the road (static %.1f), %.2f m/s"),
		T.WheelContactCount, BodyOverRoadCm, Car.CollisionShapeCenterYM * 100.0, T.SpeedMps));
	TestEqual(TEXT("four wheels on the streamed road"), T.WheelContactCount, 4);
	TestTrue(TEXT("resting at ride height on the road (within 4 cm)"), FMath::Abs(BodyOverRoadCm - Car.CollisionShapeCenterYM * 100.0) < 4.0);
	TestTrue(TEXT("at rest"), T.SpeedMps < 0.1);

	// Drive off along the lane through the shell's interface.
	const FVector From = Vehicle->GetActorLocation();
	const FVector Ahead = Heading.Vector();
	FSimCruiseDriverCommand Command;
	Command.Throttle = 0.4f;
	int32 MinContacts = 4;
	for (int32 Step = 0; Step < 25; ++Step)
	{
		Control->ApplyDriverCommand(Command);
		WaitSim(Bench, Movement, 0.2);
		MinContacts = FMath::Min(MinContacts, Movement->GetTelemetry().WheelContactCount);
	}
	T = Movement->GetTelemetry();
	const FVector Moved = Vehicle->GetActorLocation() - From;
	const double Along = FVector::DotProduct(Moved, Ahead) / 100.0;
	const double Across = FVector::CrossProduct(Ahead, Moved).Z / 100.0;
	AddInfo(FString::Printf(TEXT("DRIVE 5 s at 40 %% throttle on Ocean Drive: %.1f m along, %.2f m across, %.1f m/s, fewest wheels down %d"),
		Along, Across, T.ForwardSpeedMps, MinContacts));
	TestTrue(TEXT("drives forward along the lane"), Along > 15.0);
	TestTrue(TEXT("stays in its lane with the wheel straight (under 0.6 m drift)"), FMath::Abs(Across) < 0.6);
	TestTrue(TEXT("tyres stay on the road surface"), MinContacts >= 3);

	double SurfaceZ = 0.0;
	TestTrue(TEXT("still over a streamed tile"), Streamer->SurfaceHeightAt(Vehicle->GetActorLocation(), SurfaceZ));
	TestTrue(FString::Printf(TEXT("still on the surface (body %.1f cm over it)"), Vehicle->GetActorLocation().Z - SurfaceZ),
		FMath::Abs(Vehicle->GetActorLocation().Z - SurfaceZ - Car.CollisionShapeCenterYM * 100.0) < 10.0);
	return true;
}

#endif
