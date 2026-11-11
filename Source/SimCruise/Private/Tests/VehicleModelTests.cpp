// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/VehicleBenchWorld.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicleDynamics.h"
#include "Vehicle/SimCruiseVehicleSim.h"

namespace
{
	constexpr EAutomationTestFlags VehicleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;
	const TCHAR* const BenchCars[] = { TEXT("nb1999"), TEXT("mustang65"), TEXT("meridian_z") };

	/** A pure-model step on an analytic flat road at Z = 0 (no engine objects). */
	struct FFlatRoad
	{
		bool operator()(const FVector3d& StartM, const FVector3d& EndM, FSimCruiseGroundHit& OutHit) const
		{
			if (StartM.Z < 0.0 || EndM.Z > 0.0)
			{
				return false;
			}
			const double Alpha = StartM.Z / (StartM.Z - EndM.Z);
			OutHit.PointM = FMath::Lerp(StartM, EndM, Alpha);
			OutHit.NormalM = FVector3d::UnitZ();
			return true;
		}
	};

	FSimCruiseBodyState BodyAtRideHeight(const FSimCruiseVehicleSim& Sim, const FVector3d& VelocityMps)
	{
		FSimCruiseBodyState Body;
		Body.PositionM = -Sim.GetReferenceOriginLocalM();
		Body.CenterOfMassM = Body.PositionM + Sim.GetCenterOfMassLocalM();
		Body.LinearVelocityMps = VelocityMps;
		return Body;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleModelSuspensionPreloadTest, "SimCruise.Vehicle.Model.SuspensionPreload", VehicleTestFlags)
bool FSimCruiseVehicleModelSuspensionPreloadTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* CarId : BenchCars)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(*this, CarId, Car))
		{
			continue;
		}
		for (int32 Axle = 0; Axle < 2; ++Axle)
		{
			const double Share = Axle == 0 ? Car.FrontWeightFraction : 1.0 - Car.FrontWeightFraction;
			const double Corner = Car.MassKg * Share * 0.5 * SimCruiseDynamics::GravityMps2;
			TestEqual(FString::Printf(TEXT("%s axle %d carries its corner weight at static length"), CarId, Axle),
				SimCruiseDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM, 0.0), Corner, 1.0e-6);
			// Compression adds the wheel rate; rebound (+ velocity) removes damper force.
			const double Rate = SimCruiseDynamics::GetWheelSpringRateNPerM(Car, Axle);
			TestEqual(FString::Printf(TEXT("%s axle %d spring rate"), CarId, Axle),
				SimCruiseDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM - 0.01, 0.0), Corner + 0.01 * Rate, 1.0e-6);
			TestTrue(FString::Printf(TEXT("%s axle %d damper resists rebound"), CarId, Axle),
				SimCruiseDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM, 0.2) < Corner);
			TestTrue(FString::Printf(TEXT("%s axle %d damper resists compression"), CarId, Axle),
				SimCruiseDynamics::ComputeSuspensionLoadN(Car, Axle, Car.StaticSuspensionLengthM, -0.2) > Corner);
		}
	}
	// The NB's rates as CarSuspension.gd computes them.
	FSimCruiseCarDefinition Nb;
	TestEqual(TEXT("NB front wheel rate"), SimCruiseDynamics::GetWheelSpringRateNPerM(Nb, 0), 168.0 * 175.126835 * 0.68 * 0.68, 1.0e-6);
	TestTrue(TEXT("NB front bar is stiffer than the rear"), SimCruiseDynamics::GetAntiRollBarRateNPerM(Nb, 0) > SimCruiseDynamics::GetAntiRollBarRateNPerM(Nb, 1));
	// CarSuspension.gd roll_bar_rate, written out: the 11 mm rear bar's second
	// moment (7.2e-10 m^4) once fell under a small-number guard and read 0.
	for (int32 Axle = 0; Axle < 2; ++Axle)
	{
		const double D = Nb.AntiRollBarDiameterMByAxle[Axle];
		const double Arm = Nb.AntiRollBarArmLengthMByAxle[Axle];
		const double Moment = UE_DOUBLE_PI * D * D * D * D / 64.0;
		const double Twist = Arm * Arm * Nb.AntiRollBarTorsionLengthMByAxle[Axle] / (79.0e9 * 2.0 * Moment);
		const double Bending = 2.0 * Arm * Arm * Arm / (3.0 * 205.0e9 * Moment);
		const double Expected = 0.82 * FMath::Square(Nb.AntiRollBarLeverRatioByAxle[Axle]) / (Twist + Bending);
		TestEqual(FString::Printf(TEXT("NB anti-roll bar rate, axle %d"), Axle), SimCruiseDynamics::GetAntiRollBarRateNPerM(Nb, Axle), Expected, 1.0e-6);
	}
	TestTrue(TEXT("NB rear bar is not zero"), SimCruiseDynamics::GetAntiRollBarRateNPerM(Nb, 1) > 2000.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleModelTyreTest, "SimCruise.Vehicle.Model.Tyre", VehicleTestFlags)
bool FSimCruiseVehicleModelTyreTest::RunTest(const FString& Parameters)
{
	const FSimCruiseCarDefinition Car;
	const double Fz = Car.ReferenceCornerLoadN;
	const double Mu = SimCruiseDynamics::ComputeFrictionCoefficient(Car, 0, Fz, 1.0);
	TestEqual(TEXT("reference load gets the peak friction"), Mu, Car.PeakLongitudinalFrictionCoefficient, 1.0e-12);
	TestTrue(TEXT("load sensitivity: more load, less friction"), SimCruiseDynamics::ComputeFrictionCoefficient(Car, 0, Fz * 1.5, 1.0) < Mu);
	// Sliding to the right (positive slip angle) gives a force to the left.
	TestTrue(TEXT("lateral force opposes slip"), SimCruiseDynamics::ComputeLateralForceN(Car, Fz, Mu, 0.05, 0.0, 20.0) < 0.0);
	TestTrue(TEXT("camber thrust toward the lean"), SimCruiseDynamics::ComputeLateralForceN(Car, Fz, Mu, 0.0, 0.02, 20.0) > 0.0);
	const double Saturated = SimCruiseDynamics::ComputeLateralForceN(Car, Fz, Mu, 0.6, 0.0, 20.0);
	TestTrue(TEXT("lateral force saturates near mu Fz times the lateral scale"),
		FMath::Abs(Saturated) <= Fz * Mu * Car.LateralGripScale + 1.0 && FMath::Abs(Saturated) > 0.8 * Fz * Mu * Car.LateralGripScale);

	// Relaxation: a step in slip angle builds over the relaxation length.
	double Rolling = 0.0;
	const double First = SimCruiseDynamics::ComputeRelaxedSlipAngleRad(Car, 20.0, 1.0, 1.0 / 360.0, Fz, Rolling);
	const double Target = FMath::Atan2(1.0, 20.0);
	TestTrue(TEXT("relaxed slip lags the target"), First > 0.0 && First < Target);
	for (int32 Step = 0; Step < 360; ++Step)
	{
		SimCruiseDynamics::ComputeRelaxedSlipAngleRad(Car, 20.0, 1.0, 1.0 / 360.0, Fz, Rolling);
	}
	TestEqual(TEXT("relaxed slip reaches the target after 20 m"), Rolling, Target, 1.0e-9);

	// Longitudinal: a driven wheel spinning faster than the road pushes forward; a locked wheel slides at mu Fz.
	FSimCruiseLongitudinalSubstepInput In;
	In.NormalLoadN = Fz;
	In.FrictionCoefficient = Mu;
	In.ForwardSpeedMps = 10.0;
	In.TireRadiusM = Car.TireFreeRadiusM();
	In.WheelInertiaKgM2 = Car.WheelRotationalInertiaKgM2;
	In.SubstepS = 1.0 / 360.0;
	In.StartAngularSpeedRadPerSecond = 11.0 / In.TireRadiusM;
	In.SpinAfterTorqueRadPerSecond = In.StartAngularSpeedRadPerSecond;
	const FSimCruiseLongitudinalSubstepOutput Drive = SimCruiseDynamics::SolveLongitudinalSubstep(Car, In);
	TestTrue(TEXT("drive slip pushes forward"), Drive.LongitudinalForceN > 0.0 && Drive.SlipRatio > 0.0);
	TestTrue(TEXT("the friction circle caps the force"), Drive.LongitudinalForceN <= Fz * Mu + 1.0e-6);
	In.StartAngularSpeedRadPerSecond = 0.0;
	In.SpinAfterTorqueRadPerSecond = 0.0;
	In.BrakeTorqueNm = 2000.0;
	const FSimCruiseLongitudinalSubstepOutput Locked = SimCruiseDynamics::SolveLongitudinalSubstep(Car, In);
	TestEqual(TEXT("a locked wheel slides at mu Fz"), Locked.LongitudinalForceN, -Fz * Mu, 1.0e-6);
	TestEqual(TEXT("a locked wheel stays locked under the brake"), Locked.EndAngularSpeedRadPerSecond, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleModelDrivetrainTest, "SimCruise.Vehicle.Model.Drivetrain", VehicleTestFlags)
bool FSimCruiseVehicleModelDrivetrainTest::RunTest(const FString& Parameters)
{
	FSimCruiseCarDefinition Car;
	TestEqual(TEXT("torque curve point"), SimCruiseDynamics::SampleEngineTorqueNm(Car, 5000.0), 161.0, 1.0e-9);
	TestEqual(TEXT("torque curve interpolates"), SimCruiseDynamics::SampleEngineTorqueNm(Car, 4575.0), 156.0, 1.0e-9);
	TestEqual(TEXT("below the curve holds the first point"), SimCruiseDynamics::SampleEngineTorqueNm(Car, 300.0), 94.0, 1.0e-9);
	TestEqual(TEXT("past the curve"), SimCruiseDynamics::SampleEngineTorqueNm(Car, 7500.0), Car.TorquePastCurveNm, 1.0e-9);

	// Torsen-style bias 2: the slower wheel can take up to twice the faster's torque.
	const double Equal[4] = { 0.0, 0.0, 30.0, 30.0 };
	double Out[4];
	SimCruiseDynamics::SplitDriveTorqueToWheels(Car, 1000.0, Equal, 1.0 / 360.0, true, Out);
	TestEqual(TEXT("equal speeds split equally"), Out[2], 500.0, 1.0e-9);
	const double Spinning[4] = { 0.0, 0.0, 30.0, 60.0 };
	SimCruiseDynamics::SplitDriveTorqueToWheels(Car, 1000.0, Spinning, 1.0 / 360.0, true, Out);
	TestEqual(TEXT("a spinning right wheel sends torque left, up to the bias"), Out[2] / Out[3], 2.0, 1.0e-9);
	TestEqual(TEXT("the split conserves torque"), Out[2] + Out[3], 1000.0, 1.0e-9);
	Car.RearDifferentialBiasRatio = 1.0;
	SimCruiseDynamics::SplitDriveTorqueToWheels(Car, 1000.0, Spinning, 1.0 / 360.0, true, Out);
	TestEqual(TEXT("an open differential splits equally"), Out[2], Out[3], 1.0e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleModelReverseInhibitTest, "SimCruise.Vehicle.Model.ReverseInhibit", VehicleTestFlags)
bool FSimCruiseVehicleModelReverseInhibitTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* CarId : BenchCars)
	{
		FSimCruiseCarDefinition Car;
		if (!SimCruiseLoadBenchCar(*this, CarId, Car))
		{
			continue;
		}
		FSimCruiseVehicleSim Sim;
		Sim.Initialize(Car);
		Sim.bSteeringAssist = false;
		Sim.ResetState();
		const FFlatRoad Road;
		FSimCruiseBodyForces Forces;
		// Rolling forward at 5 m/s, every brake pedal from the lightest touch up:
		// the automatic must never select reverse.
		for (const double Pedal : { 0.001, 0.005, 0.0099, 0.01, 0.02, 0.3, 1.0 })
		{
			Sim.ResetState();
			FSimCruiseVehicleControls Controls;
			Controls.Brake = Pedal;
			Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(5.0, 0.0, 0.0)), Road, Forces);
			TestFalse(FString::Printf(TEXT("%s: brake %.4f at 5 m/s keeps drive"), CarId, Pedal), Sim.State.bInReverse);
		}
		// At 0.44 m/s a pedal selects reverse, as a real selector would near standstill.
		FSimCruiseVehicleControls Controls;
		Controls.Brake = 0.5;
		Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(0.44, 0.0, 0.0)), Road, Forces);
		TestTrue(FString::Printf(TEXT("%s: brake at 0.44 m/s selects reverse"), CarId), Sim.State.bInReverse);
		// Rolling backwards, the accelerator brakes until 0.45 m/s.
		Controls = FSimCruiseVehicleControls();
		Controls.Throttle = 0.5;
		Sim.Step(1.0 / 360.0, 1, Controls, BodyAtRideHeight(Sim, FVector3d(-3.0, 0.0, 0.0)), Road, Forces);
		TestTrue(FString::Printf(TEXT("%s: accelerator rolling backwards brakes"), CarId), Sim.State.bServiceBrake && Sim.State.bInReverse);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleModelGearboxTest, "SimCruise.Vehicle.Model.Gearbox", VehicleTestFlags)
bool FSimCruiseVehicleModelGearboxTest::RunTest(const FString& Parameters)
{
	FSimCruiseVehicleSim Sim;
	Sim.Initialize(FSimCruiseCarDefinition());
	Sim.SetManual(true, 0.0);
	TestEqual(TEXT("manual from rest selects neutral"), Sim.GetGearText(), FString(TEXT("N")));
	TestEqual(TEXT("no change without the clutch"), Sim.ChangeGear(1, 0.0), FString(TEXT("Hold the clutch to change gear")));
	Sim.State.ClutchPedal = 1.0;
	TestEqual(TEXT("first with the clutch"), Sim.ChangeGear(1, 0.0), FString());
	TestEqual(TEXT("in first"), Sim.GetGearText(), FString(TEXT("1")));
	Sim.ChangeGear(-1, 0.0);
	TestEqual(TEXT("reverse refused while rolling forward"), Sim.ChangeGear(-1, 3.0), FString(TEXT("Stop before changing direction")));
	TestEqual(TEXT("reverse at rest"), Sim.ChangeGear(-1, 0.0), FString());
	TestEqual(TEXT("in reverse"), Sim.GetGearText(), FString(TEXT("R")));
	Sim.ChangeGear(1, 0.0);
	Sim.ChangeGear(1, 0.0);
	TestEqual(TEXT("over-revving downshift refused"), Sim.ChangeGear(0, 0.0), FString());
	Sim.SelectRatio(5);
	TestEqual(TEXT("first refused at 40 m/s"), Sim.ChangeGear(-4, 40.0), FString(TEXT("Speed is too high for that gear")));
	Sim.SetManual(false, 20.0);
	TestFalse(TEXT("automatic again"), Sim.State.bManual);
	return true;
}

#endif
