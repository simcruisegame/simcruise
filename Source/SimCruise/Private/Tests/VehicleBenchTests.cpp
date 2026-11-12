// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseUnits.h"

namespace
{
	constexpr EAutomationTestFlags VehicleBenchFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	// docs/cars.md bands.
	constexpr double ZeroToSixtyBandS = 0.15;
	constexpr double SeventyToZeroBandFt = 5.0;
	constexpr double SkidpadBandG = 0.02;

	/** A car file's calibration_open note excuses only the measurement it names. */
	bool IsOpenItem(const FSimCruiseCarDefinition& Car, const TCHAR* Measurement)
	{
		return !Car.CalibrationOpen.IsEmpty() && Car.CalibrationOpen.Contains(Measurement, ESearchCase::IgnoreCase);
	}

	FString Seconds(double Value)
	{
		return Value >= TNumericLimits<double>::Max() ? FString(TEXT("never")) : FString::Printf(TEXT("%.2f s"), Value);
	}

	bool RunCarTrackTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseCarTrackResult R = SimCruiseRunCarTrack(Car, SimCruiseShippingFixedStepS);
		if (!Test.TestTrue(TEXT("bench completed"), R.bCompleted))
		{
			return false;
		}
		Test.TestFalse(TEXT("the car hit nothing on the bench pad"), R.bCollided);
		const bool bAccel = FMath::Abs(R.ZeroToSixtyS - Car.TargetZeroToSixtySeconds) <= ZeroToSixtyBandS;
		const bool bBrake = FMath::Abs(R.SeventyToZeroFt - Car.TargetSeventyToZeroFeet) <= SeventyToZeroBandFt;
		const bool bSkid = FMath::Abs(R.SkidpadG - Car.TargetSkidpadG) <= SkidpadBandG;
		const FString Line = FString::Printf(
			TEXT("CARTRACK %-11s 0-60 %s (target %.2f)  70-0 %.1f ft (target %.1f)  skidpad %.3f g (target %.2f)  %s  [%lld steps at %.0f Hz, %.1f us/step vehicle, %.1f s sim in %.1f s wall]"),
			CarId, *Seconds(R.ZeroToSixtyS), Car.TargetZeroToSixtySeconds, R.SeventyToZeroFt, Car.TargetSeventyToZeroFeet,
			R.SkidpadG, Car.TargetSkidpadG, (bAccel && bBrake && bSkid) ? TEXT("PASS") : TEXT("OFF"),
			R.PhysicsSteps, 1.0 / SimCruiseShippingFixedStepS, R.MeanStepMicroseconds, R.SimSeconds, R.WallSeconds);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		if (!Car.CalibrationOverlayChanges.IsEmpty())
		{
			const FString Overlay = FString::Printf(TEXT("CALIBRATION %s overlay %s: %s (%s)"), CarId, *Car.CalibrationOverlayPath,
				*Car.CalibrationOverlayChanges, *Car.CalibrationOverlayNote);
			UE_LOG(LogTemp, Display, TEXT("%s"), *Overlay);
			Test.AddInfo(Overlay);
		}
		auto Check = [&Test, &Car](bool bInBand, const TCHAR* Measurement, const FString& What)
		{
			if (bInBand)
			{
				return;
			}
			if (IsOpenItem(Car, Measurement))
			{
				Test.AddInfo(FString::Printf(TEXT("%s outside its band, stated open item: %s"), *What, *Car.CalibrationOpen));
			}
			else
			{
				Test.AddError(FString::Printf(TEXT("%s outside its band"), *What));
			}
		};
		Check(bAccel, TEXT("0-60"), FString::Printf(TEXT("0-60 mph %s vs %.2f +/- %.2f s"), *Seconds(R.ZeroToSixtyS), Car.TargetZeroToSixtySeconds, ZeroToSixtyBandS));
		Check(bBrake, TEXT("70-0"), FString::Printf(TEXT("70-0 mph %.1f ft vs %.1f +/- %.0f ft"), R.SeventyToZeroFt, Car.TargetSeventyToZeroFeet, SeventyToZeroBandFt));
		Check(bSkid, TEXT("skidpad"), FString::Printf(TEXT("skidpad %.3f g vs %.2f +/- %.2f g"), R.SkidpadG, Car.TargetSkidpadG, SkidpadBandG));
		return true;
	}

	bool RunLiftOffTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseLiftOffResult R = SimCruiseRunLiftOff(Car, SimCruiseShippingFixedStepS, { 0.3, 0.5, 0.7 });
		if (!Test.TestTrue(TEXT("bench completed"), R.bCompleted))
		{
			return false;
		}
		constexpr double GateG = 0.5;
		for (const FSimCruiseLiftOffCase& Case : R.Cases)
		{
			const bool bGated = Case.TargetG <= GateG + 0.001;
			const FString Line = Case.bSteady
				? FString::Printf(TEXT("LIFTOFF %-11s %.1f g %-5s steady %.2f g (gear %d, beta %+.1f)  yaw_ratio_1s %.2f  yaw_overshoot %+.2f  beta_1s %+.1f  peak_beta %+.1f  settle_s %.2f  decel_g %.2f  %s%s%s"),
					CarId, Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift"), Case.SteadyLateralG, Case.SteadyGear, Case.SteadyBetaDeg,
					Case.YawRatio1s, Case.YawOvershoot, Case.Beta1sDeg, Case.PeakBetaDeg, Case.SettleS, Case.DecelG,
					Case.bSpun ? TEXT("SPIN") : TEXT("ok"), bGated ? TEXT("") : TEXT(" (reported)"),
					Case.bReverseAtSpeed ? TEXT(" REVERSE SELECTED AT SPEED") : TEXT(""))
				: FString::Printf(TEXT("LIFTOFF %-11s %.1f g %-5s no steady state"), CarId, Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift"));
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			if (bGated && !Case.bSteady)
			{
				Test.AddError(FString::Printf(TEXT("%.1f g %s: the circle driver found no steady state"), Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift")));
			}
			if (bGated && Case.bSpun)
			{
				Test.AddError(FString::Printf(TEXT("%.1f g %s: spun (peak sideslip %.1f deg)"), Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift"), Case.PeakBetaDeg));
			}
			if (Case.bReverseAtSpeed)
			{
				Test.AddError(FString::Printf(TEXT("%.1f g %s: the automatic selected reverse while rolling forward"), Case.TargetG, Case.bBrake ? TEXT("brake") : TEXT("lift")));
			}
		}
		// Understeer gradient, ISO 4138's constant-radius method: on the fixed circle the steer
		// angle the car needs grows with lateral acceleration by K deg/g (0 = neutral). Road cars
		// understeer; surveys put the average at about 2.6 deg/g for European and 4.4 for
		// American cars (Crolla's handling course notes).
		const FSimCruiseLiftOffCase* Low = nullptr;
		const FSimCruiseLiftOffCase* Mid = nullptr;
		for (const FSimCruiseLiftOffCase& Case : R.Cases)
		{
			if (!Case.bSteady || Case.bBrake) continue;
			if (FMath::IsNearlyEqual(Case.TargetG, 0.3, 0.01)) Low = &Case;
			if (FMath::IsNearlyEqual(Case.TargetG, 0.5, 0.01)) Mid = &Case;
		}
		if (Low && Mid && Mid->SteadyLateralG - Low->SteadyLateralG > 0.05)
		{
			const double K = (Mid->SteadyRackDeg - Low->SteadyRackDeg) / (Mid->SteadyLateralG - Low->SteadyLateralG);
			const FString Line = FString::Printf(TEXT("UNDERSTEER %-11s %.2f deg/g (road-wheel steer %.2f deg at %.2f g, %.2f deg at %.2f g)"),
				CarId, K, Low->SteadyRackDeg, Low->SteadyLateralG, Mid->SteadyRackDeg, Mid->SteadyLateralG);
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.AddInfo(Line);
			Test.TestTrue(TEXT("understeers below the limit (0 to 8 deg/g, ISO 4138 constant radius)"), K > 0.0 && K < 8.0);
		}
		return true;
	}

	bool RunStaticLoadTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(Test, CarId, Car))
		{
			return false;
		}
		const FSimCruiseStaticLoadResult R = SimCruiseRunStaticLoad(Car, SimCruiseShippingFixedStepS);
		if (!Test.TestTrue(TEXT("settled"), R.bCompleted))
		{
			return false;
		}
		const double Weight = Car.MassKg * R.GravityMps2;
		const FString Line = FString::Printf(
			TEXT("STATICLOAD %-11s loads FL %.0f FR %.0f RL %.0f RR %.0f N  total %.1f of %.1f N  front %.4f (file %.4f)  ride FL %.1f RL %.1f mm (static %.1f)  pitch %+.3f roll %+.3f deg  residual %.4f m/s"),
			CarId, R.LoadN[0], R.LoadN[1], R.LoadN[2], R.LoadN[3], R.TotalLoadN, Weight, R.FrontFraction, Car.FrontWeightFraction,
			R.SuspensionLengthM[0] * 1000.0, R.SuspensionLengthM[2] * 1000.0, Car.StaticSuspensionLengthM * 1000.0, R.PitchDeg, R.RollDeg, R.ResidualSpeedMps);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		Test.TestEqual(TEXT("the tyres carry the car's weight"), R.TotalLoadN, Weight, Weight * 0.002);
		Test.TestEqual(TEXT("front share matches the file"), R.FrontFraction, Car.FrontWeightFraction, 0.002);
		Test.TestEqual(TEXT("left and right front agree"), R.LoadN[0], R.LoadN[1], Weight * 0.001);
		Test.TestEqual(TEXT("left and right rear agree"), R.LoadN[2], R.LoadN[3], Weight * 0.001);
		// Ride height: preload makes the static length exact at 9.80665 m/s^2; Unreal's 9.8 sits it a fraction of a mm higher.
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Test.TestEqual(FString::Printf(TEXT("wheel %d sits at the static suspension length"), Index),
				R.SuspensionLengthM[Index], Car.StaticSuspensionLengthM, 0.002);
		}
		Test.TestTrue(TEXT("level (pitch, roll under 0.2 deg)"), FMath::Abs(R.PitchDeg) < 0.2 && FMath::Abs(R.RollDeg) < 0.2);
		Test.TestTrue(TEXT("at rest"), R.ResidualSpeedMps < 0.01);
		return true;
	}
}

#define SIMCRUISE_VEHICLE_BENCH(Kind, CarIdent) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicle##Kind##_##CarIdent, "SimCruise.Vehicle." #Kind "." #CarIdent, VehicleBenchFlags) \
	bool FSimCruiseVehicle##Kind##_##CarIdent::RunTest(const FString& Parameters) { return Run##Kind##Test(*this, TEXT(#CarIdent)); }

SIMCRUISE_VEHICLE_BENCH(CarTrack, nb1999)
SIMCRUISE_VEHICLE_BENCH(CarTrack, nb2001)
SIMCRUISE_VEHICLE_BENCH(CarTrack, nb2001_aaron)
SIMCRUISE_VEHICLE_BENCH(CarTrack, mustang65)
SIMCRUISE_VEHICLE_BENCH(CarTrack, meridian_z)
SIMCRUISE_VEHICLE_BENCH(CarTrack, model3)
SIMCRUISE_VEHICLE_BENCH(CarTrack, f150raptor18)
SIMCRUISE_VEHICLE_BENCH(CarTrack, grandcherokee13)
SIMCRUISE_VEHICLE_BENCH(CarTrack, odyssey18)
SIMCRUISE_VEHICLE_BENCH(CarTrack, crv25)
SIMCRUISE_VEHICLE_BENCH(LiftOff, nb1999)
SIMCRUISE_VEHICLE_BENCH(LiftOff, nb2001)
SIMCRUISE_VEHICLE_BENCH(LiftOff, nb2001_aaron)
SIMCRUISE_VEHICLE_BENCH(LiftOff, mustang65)
SIMCRUISE_VEHICLE_BENCH(LiftOff, meridian_z)
SIMCRUISE_VEHICLE_BENCH(LiftOff, model3)
SIMCRUISE_VEHICLE_BENCH(LiftOff, f150raptor18)
SIMCRUISE_VEHICLE_BENCH(LiftOff, grandcherokee13)
SIMCRUISE_VEHICLE_BENCH(LiftOff, odyssey18)
SIMCRUISE_VEHICLE_BENCH(LiftOff, crv25)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, nb1999)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, nb2001)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, nb2001_aaron)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, mustang65)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, meridian_z)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, model3)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, f150raptor18)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, grandcherokee13)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, odyssey18)
SIMCRUISE_VEHICLE_BENCH(StaticLoad, crv25)

#undef SIMCRUISE_VEHICLE_BENCH

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleDeterminismTest, "SimCruise.Vehicle.Determinism", VehicleBenchFlags)
bool FSimCruiseVehicleDeterminismTest::RunTest(const FString& Parameters)
{
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(*this, TEXT("nb1999"), Car))
	{
		return false;
	}
	// The same car, controls and fixed steps must give the same state, bit for
	// bit, through all three tests: the restart and the skidpad included, where
	// anything else in the bench world (streamed tiles once) shows up first.
	const FSimCruiseCarTrackResult A = SimCruiseRunCarTrack(Car, SimCruiseShippingFixedStepS);
	const FSimCruiseCarTrackResult B = SimCruiseRunCarTrack(Car, SimCruiseShippingFixedStepS);
	if (!TestTrue(TEXT("both runs completed"), A.bCompleted && B.bCompleted))
	{
		return false;
	}
	TestEqual(TEXT("0-60 identical"), A.ZeroToSixtyS, B.ZeroToSixtyS);
	TestEqual(TEXT("70-0 identical"), A.SeventyToZeroFt, B.SeventyToZeroFt);
	TestEqual(TEXT("skidpad identical"), A.SkidpadG, B.SkidpadG);
	TestEqual(TEXT("fingerprint size"), A.Fingerprint.Num(), B.Fingerprint.Num());
	int32 Mismatches = 0;
	for (int32 Index = 0; Index < FMath::Min(A.Fingerprint.Num(), B.Fingerprint.Num()); ++Index)
	{
		Mismatches += A.Fingerprint[Index] != B.Fingerprint[Index] ? 1 : 0;
	}
	const FString Line = FString::Printf(TEXT("DETERMINISM two CarTrack runs: 0-60 %.9f / %.9f s, skidpad %.9f / %.9f g, %d of %d state values differ"),
		A.ZeroToSixtyS, B.ZeroToSixtyS, A.SkidpadG, B.SkidpadG, Mismatches, A.Fingerprint.Num());
	UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
	AddInfo(Line);
	TestEqual(TEXT("state identical after the run"), Mismatches, 0);
	return true;
}

namespace
{
	bool RunConvergenceTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(Test, CarId, Car))
	{
		return false;
	}
	struct FRate { double Hz; double WheelHz; const TCHAR* Label; };
	const FRate Rates[] = {
		{ 60.0, 360.0, TEXT("60 Hz body, 6 wheel substeps (Godot)") },
		{ 120.0, 360.0, TEXT("120 Hz body, 3 wheel substeps") },
		{ 240.0, 480.0, TEXT("240 Hz body, 2 wheel substeps") },
		{ 360.0, 360.0, TEXT("360 Hz body and wheels (shipping)") },
		{ 720.0, 720.0, TEXT("720 Hz body and wheels") },
	};
	TArray<FSimCruiseCarTrackResult> Results;
	for (const FRate& Rate : Rates)
	{
		const FSimCruiseCarTrackResult R = SimCruiseRunCarTrack(Car, 1.0 / Rate.Hz, 1.0 / Rate.WheelHz);
		Results.Add(R);
		const FString Line = FString::Printf(TEXT("CONVERGENCE %-11s %-38s 0-60 %s  70-0 %.1f ft  skidpad %.3f g  %.1f us/step vehicle  %.2f ms of vehicle per simulated second"),
			CarId, Rate.Label, *Seconds(R.ZeroToSixtyS), R.SeventyToZeroFt, R.SkidpadG, R.MeanStepMicroseconds, R.MeanStepMicroseconds * Rate.Hz / 1000.0);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
		Test.AddInfo(Line);
		Test.TestTrue(FString::Printf(TEXT("%s completed"), Rate.Label), R.bCompleted);
		Test.TestFalse(FString::Printf(TEXT("%s hit nothing on the pad"), Rate.Label), R.bCollided);
	}
	// Halving the step from the shipping rate must move each result by less
	// than a fifth of its calibration band.
	const FSimCruiseCarTrackResult& Shipping = Results[3];
	const FSimCruiseCarTrackResult& Finer = Results[4];
	Test.TestTrue(TEXT("0-60 converged at 360 Hz"), FMath::Abs(Shipping.ZeroToSixtyS - Finer.ZeroToSixtyS) < ZeroToSixtyBandS / 5.0);
	Test.TestTrue(TEXT("70-0 converged at 360 Hz"), FMath::Abs(Shipping.SeventyToZeroFt - Finer.SeventyToZeroFt) < SeventyToZeroBandFt / 5.0);
	Test.TestTrue(TEXT("skidpad converged at 360 Hz"), FMath::Abs(Shipping.SkidpadG - Finer.SkidpadG) < SkidpadBandG / 5.0);
	return true;
	}
}

#define SIMCRUISE_VEHICLE_CONVERGENCE(CarIdent) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleConvergence_##CarIdent, "SimCruise.Vehicle.Convergence." #CarIdent, VehicleBenchFlags) \
	bool FSimCruiseVehicleConvergence_##CarIdent::RunTest(const FString& Parameters) { return RunConvergenceTest(*this, TEXT(#CarIdent)); }

SIMCRUISE_VEHICLE_CONVERGENCE(nb1999)
SIMCRUISE_VEHICLE_CONVERGENCE(mustang65)
SIMCRUISE_VEHICLE_CONVERGENCE(meridian_z)

#undef SIMCRUISE_VEHICLE_CONVERGENCE

#endif
