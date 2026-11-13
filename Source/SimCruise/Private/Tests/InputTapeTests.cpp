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
	TestFalse(TEXT("a tape without a place has none"), Tape.bHasPlace);

	// The place and the world frame go round the file too (a title with a comma and an equals sign).
	FSimCruiseInputTape Placed = SyntheticTape();
	Placed.bHasLonLat = true;
	Placed.StartLonDeg = -83.93273;
	Placed.StartLatDeg = 35.47928;
	Placed.bHasPlace = true;
	Placed.PlaceTitle = TEXT("Tail of the Dragon (US-129), Deals Gap = TN");
	Placed.PlaceStreet = TEXT("Calderwood Highway");
	Placed.PlaceLonDeg = -83.9327265;
	Placed.PlaceLatDeg = 35.4792763;
	Placed.FrameOriginTile = FIntPoint(17488, 25851);
	Placed.FrameTileSizeM = 497.96;
	FSimCruiseInputTape PlacedBack;
	if (TestTrue(TEXT("a tape with a place reads back"), FSimCruiseInputTape::FromCsv(Placed.ToCsv(), PlacedBack, Error)))
	{
		TestTrue(TEXT("place"), PlacedBack.bHasPlace);
		TestEqual(TEXT("place title"), PlacedBack.PlaceTitle, Placed.PlaceTitle);
		TestEqual(TEXT("place street"), PlacedBack.PlaceStreet, Placed.PlaceStreet);
		TestTrue(TEXT("place lon/lat"), FMath::IsNearlyEqual(PlacedBack.PlaceLonDeg, Placed.PlaceLonDeg, 1e-7) && FMath::IsNearlyEqual(PlacedBack.PlaceLatDeg, Placed.PlaceLatDeg, 1e-7));
		TestEqual(TEXT("frame origin tile"), PlacedBack.FrameOriginTile, Placed.FrameOriginTile);
		TestTrue(TEXT("frame tile size"), FMath::IsNearlyEqual(PlacedBack.FrameTileSizeM, 497.96, 1e-4));
		TestEqual(TEXT("same text after a round trip"), PlacedBack.ToCsv(), Placed.ToCsv());
	}

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

/** The physics-step file reads back bit for bit: a replay step by step gives the model the same numbers. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseInputStepsTest, "SimCruise.Drive.InputSteps", InputTapeFlags)
bool FSimCruiseInputStepsTest::RunTest(const FString& Parameters)
{
	FSimCruiseInputSteps Steps;
	Steps.StepS = 1.0 / 360.0;
	for (int32 Index = 0; Index < 500; ++Index)
	{
		FSimCruiseInputSteps::FRow& Row = Steps.Rows.AddDefaulted_GetRef();
		Row.Controls.Throttle = FMath::Pow(FMath::Abs(FMath::Sin(Index * 0.0137)), 1.8);
		Row.Controls.Brake = Index % 7 == 0 ? 1.0 / 3.0 : 0.0;
		Row.Controls.Steer = FMath::Sin(Index * 0.031) * 0.7;
		Row.Controls.Clutch = Index % 50 < 10 ? 1.0 : 0.0;
		Row.Controls.RackAngleRad = -0.123456789012345678 * FMath::Cos(Index * 0.01);
		Row.Controls.bThumbstickSteer = Index % 2 == 0;
		Row.Controls.bHandbrake = Index % 3 == 0;
		Row.Controls.bFullBrake = Index % 5 == 0;
		Row.Controls.bDirectRack = Index % 11 != 0;
		Row.Gear = Index % 7 - 1;
		Row.bManual = Index > 250;
	}
	FSimCruiseInputSteps Back;
	FString Error;
	if (!TestTrue(TEXT("the steps read back"), FSimCruiseInputSteps::FromCsv(Steps.ToCsv(), Back, Error)))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("step length"), Back.StepS, Steps.StepS);
	if (!TestEqual(TEXT("rows"), Back.Rows.Num(), Steps.Rows.Num()))
	{
		return false;
	}
	int32 Mismatches = 0;
	for (int32 Index = 0; Index < Steps.Rows.Num(); ++Index)
	{
		const FSimCruiseInputSteps::FRow& A = Steps.Rows[Index];
		const FSimCruiseInputSteps::FRow& B = Back.Rows[Index];
		const bool bSame = A.Controls.Throttle == B.Controls.Throttle && A.Controls.Brake == B.Controls.Brake &&
			A.Controls.Steer == B.Controls.Steer && A.Controls.Clutch == B.Controls.Clutch && A.Controls.RackAngleRad == B.Controls.RackAngleRad &&
			A.Controls.bThumbstickSteer == B.Controls.bThumbstickSteer && A.Controls.bHandbrake == B.Controls.bHandbrake &&
			A.Controls.bFullBrake == B.Controls.bFullBrake && A.Controls.bDirectRack == B.Controls.bDirectRack && A.Gear == B.Gear && A.bManual == B.bManual;
		Mismatches += bSame ? 0 : 1;
	}
	TestEqual(TEXT("every step bit for bit"), Mismatches, 0);
	TestEqual(TEXT("the steps file sits beside the tape"), FSimCruiseInputSteps::PathFor(TEXT("D:/x/input-1.csv")), FString(TEXT("D:/x/input-1-steps.csv")));
	FSimCruiseInputSteps Broken;
	TestFalse(TEXT("a file that is not a steps file is refused"), FSimCruiseInputSteps::FromCsv(TEXT("a,b\n1,2\n"), Broken, Error));
	return true;
}

#endif
