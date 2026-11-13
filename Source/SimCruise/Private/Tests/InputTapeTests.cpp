// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseInputTape.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

namespace
{
	constexpr EAutomationTestFlags InputTapeFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	/** Six seconds at 60 frames a second: a launch, a turn and its catch, then the brakes. */
	FSimCruiseInputTape SyntheticTape()
	{
		FSimCruiseInputTape Tape;
		Tape.CarId = TEXT("nb1999");
		Tape.Note = TEXT("synthetic");
		const double Dt = 1.0 / 60.0;
		for (int32 Index = 1; Index <= 360; ++Index)
		{
			const double T = Index * Dt;
			FSimCruiseInputFrame F;
			F.TimeS = T;
			F.DeltaS = Dt;
			F.Raw.PadThrottle = T > 0.5 && T <= 3.5 ? 0.8 : 0.0;
			F.Raw.PadSteer = T > 2.0 && T <= 3.5 ? 0.4 : (T > 3.5 && T <= 4.0 ? -0.3 : 0.0);
			F.Raw.PadBrake = T > 4.0 ? 0.6 : 0.0;
			Tape.Frames.Add(F);
		}
		return Tape;
	}

	/** Replay a tape on the bench: each physics step takes the frame in force at its time. */
	bool ReplayOnBench(FAutomationTestBase& Test, const FSimCruiseInputTape& Tape, FSimCruiseReplayStats& OutStats)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, Tape.CarId, Car))
		{
			return false;
		}
		const double RearAxleM = Car.WheelbaseM * Car.FrontWeightFraction;
		FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS);
		int32 Cursor = 0;
		double SimTime = 0.0;
		bool bDone = false;
		ASimCruiseVehicle* Vehicle = Bench.SpawnVehicle(Car, FVector3d::ZeroVector,
			[&Tape, &OutStats, &Cursor, &SimTime, &bDone, RearAxleM](FSimCruiseVehicleDriverContext& Context)
			{
				SimTime = Context.SimTimeS;
				// Half a second to settle on the springs, then the tape.
				const double TapeTime = Context.SimTimeS - 0.5;
				if (TapeTime <= 0.0 || bDone)
				{
					return;
				}
				const FSimCruiseInputFrame* Frame = Tape.At(TapeTime, Cursor);
				if (!Frame)
				{
					bDone = true;
					return;
				}
				const FSimCruiseRawControls& Raw = Frame->Raw;
				Context.Controls.Throttle = FMath::Max(Raw.PadThrottle, Raw.bKeyThrottle ? 1.0 : 0.0);
				Context.Controls.Brake = FMath::Max(Raw.PadBrake, Raw.bKeyBrake ? 1.0 : 0.0);
				Context.Controls.Steer = FMath::Clamp(Raw.PadSteer + Raw.KeySteer, -1.0, 1.0);
				Context.Controls.bThumbstickSteer = Raw.PadSteer != 0.0;
				Context.Controls.bHandbrake = Raw.bParkingBrake;
				Context.Controls.bFullBrake = Raw.bFullServiceBrake;
				Context.Controls.Clutch = Raw.bClutch ? 1.0 : 0.0;
				const FVector3d Velocity = Context.Body.Rotation.UnrotateVector(Context.Body.LinearVelocityMps);
				const FVector3d Angular = Context.Body.Rotation.UnrotateVector(Context.Body.AngularVelocityRadPerSecond);
				OutStats.Add(TapeTime, Context.Body.PositionM, Velocity, Angular.Z, RearAxleM, 0);
			});
		if (!Test.TestNotNull(TEXT("bench car"), Vehicle))
		{
			return false;
		}
		Bench.RunUntil([&bDone]() { return bDone; }, Tape.GetDuration() + 2.0);
		return Test.TestTrue(TEXT("the replay ran to the end of the tape"), bDone);
	}
}

/**
 * Input replay: a synthetic recording goes through the tape file format and back unchanged,
 * and replaying it twice on the same car gives the same speed, yaw rate, slip and distance,
 * sample for sample.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseInputReplayTest, "SimCruise.Drive.InputReplay", InputTapeFlags)
bool FSimCruiseInputReplayTest::RunTest(const FString& Parameters)
{
	const FSimCruiseInputTape Written = SyntheticTape();
	FSimCruiseInputTape Tape;
	FString Error;
	if (!TestTrue(TEXT("the tape reads back"), FSimCruiseInputTape::FromCsv(Written.ToCsv(), Tape, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("car"), Tape.CarId, Written.CarId);
	TestEqual(TEXT("frames"), Tape.Frames.Num(), Written.Frames.Num());
	TestEqual(TEXT("same text after a round trip"), Tape.ToCsv(), Written.ToCsv());
	int32 Cursor = 0;
	const FSimCruiseInputFrame* Mid = Tape.At(3.0, Cursor);
	TestTrue(TEXT("sample and hold at 3 s"), Mid && FMath::IsNearlyEqual(Mid->Raw.PadSteer, 0.4) && FMath::IsNearlyEqual(Mid->Raw.PadThrottle, 0.8));
	TestNull(TEXT("nothing past the end"), Tape.At(Tape.GetDuration() + 0.1, Cursor));
	FSimCruiseInputTape Broken;
	TestFalse(TEXT("a file that is not a tape is refused"), FSimCruiseInputTape::FromCsv(TEXT("t,speed\n1,2\n"), Broken, Error));

	FSimCruiseReplayStats A;
	FSimCruiseReplayStats B;
	if (!ReplayOnBench(*this, Tape, A) || !ReplayOnBench(*this, Tape, B))
	{
		return false;
	}
	const FString Line = FString::Printf(TEXT("INPUTREPLAY %s  |  %s  (%d / %d samples)"), *A.Summary(), *B.Summary(), A.Rows.Num(), B.Rows.Num());
	UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
	AddInfo(Line);
	TestTrue(TEXT("the car drove (more than 20 m)"), A.DistanceM > 20.0);
	TestTrue(TEXT("the turn shows as yaw"), A.MaxYawDps > 2.0);
	TestEqual(TEXT("same summary"), A.Summary(), B.Summary());
	TestEqual(TEXT("same number of samples"), A.Rows.Num(), B.Rows.Num());
	int32 Mismatches = 0;
	for (int32 Index = 0; Index < FMath::Min(A.Rows.Num(), B.Rows.Num()); ++Index)
	{
		Mismatches += A.Rows[Index] != B.Rows[Index] ? 1 : 0;
	}
	TestEqual(TEXT("every sample identical"), Mismatches, 0);
	return true;
}

#endif
