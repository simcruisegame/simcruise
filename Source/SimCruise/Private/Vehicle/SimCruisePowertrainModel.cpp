// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicleDynamics.h"

#include "Math/UnrealMathUtility.h"

namespace SimCruiseDynamics
{
	namespace
	{
		/** Torque-sensing limited slip between two wheels (bias 1 = open). */
		TPair<double, double> SplitAxleTorque(
			double AxleTorqueNm,
			double LeftWheelAngularSpeed,
			double RightWheelAngularSpeed,
			double WheelInertiaKgM2,
			double DeltaTimeS,
			bool bIsDriveTorque,
			double BiasRatio)
		{
			const double HalfTorqueNm = AxleTorqueNm * 0.5;
			if (!bIsDriveTorque || DeltaTimeS <= 0.0 || BiasRatio <= 1.0)
			{
				return TPair<double, double>(HalfTorqueNm, HalfTorqueNm);
			}

			const double TransferLimitNm = FMath::Abs(HalfTorqueNm) * (BiasRatio - 1.0) / (BiasRatio + 1.0);
			const double EqualizingTorqueNm = (RightWheelAngularSpeed - LeftWheelAngularSpeed) *
				WheelInertiaKgM2 / (2.0 * DeltaTimeS);
			const double TransferNm = FMath::Clamp(EqualizingTorqueNm, -TransferLimitNm, TransferLimitNm);
			return TPair<double, double>(HalfTorqueNm + TransferNm, HalfTorqueNm - TransferNm);
		}
	}

	double SampleEngineTorqueNm(const FSimCruiseCarDefinition& Car, double EngineSpeedRpm)
	{
		const TArray<FSimCruiseTorqueCurvePoint>& Curve = Car.EngineTorqueCurve;
		for (int32 Index = 1; Index < Curve.Num(); ++Index)
		{
			if (EngineSpeedRpm <= Curve[Index].EngineSpeedRpm)
			{
				const FSimCruiseTorqueCurvePoint& Lower = Curve[Index - 1];
				const FSimCruiseTorqueCurvePoint& Upper = Curve[Index];
				const double Range = Upper.EngineSpeedRpm - Lower.EngineSpeedRpm;
				const double Alpha = Range > UE_DOUBLE_SMALL_NUMBER
					? FMath::Clamp((EngineSpeedRpm - Lower.EngineSpeedRpm) / Range, 0.0, 1.0)
					: 0.0;
				return FMath::Lerp(Lower.EngineTorqueNm, Upper.EngineTorqueNm, Alpha);
			}
		}
		return Car.TorquePastCurveNm;
	}

	void SplitDriveTorqueToWheels(
		const FSimCruiseCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque,
		double OutWheelTorquesNm[4])
	{
		for (int32 Index = 0; Index < 4; ++Index)
		{
			OutWheelTorquesNm[Index] = 0.0;
		}
		if (WheelAngularSpeedsRadPerSecond == nullptr)
		{
			return;
		}
		const double* W = WheelAngularSpeedsRadPerSecond;
		const double I = Car.WheelRotationalInertiaKgM2;

		if (Car.DrivetrainLayout == TEXT("fwd"))
		{
			const TPair<double, double> Front = SplitAxleTorque(AxleTorqueNm, W[0], W[1], I, DeltaTimeS, bIsDriveTorque,
				Car.FrontDifferentialBiasRatio);
			OutWheelTorquesNm[0] = Front.Key;
			OutWheelTorquesNm[1] = Front.Value;
			return;
		}

		double RearTorqueNm = AxleTorqueNm;
		if (Car.DrivetrainLayout == TEXT("awd"))
		{
			double FrontTorqueNm = AxleTorqueNm * Car.AwdFrontTorqueShareFraction;
			RearTorqueNm = AxleTorqueNm * (1.0 - Car.AwdFrontTorqueShareFraction);
			if (bIsDriveTorque && DeltaTimeS > 0.0 && Car.CenterDifferentialBiasRatio > 1.0)
			{
				// Limited-slip centre: torque moves toward the slower axle within the bias.
				const double TransferLimitNm = FMath::Abs(AxleTorqueNm) * 0.5 *
					(Car.CenterDifferentialBiasRatio - 1.0) / (Car.CenterDifferentialBiasRatio + 1.0);
				const double AxleSpeedDifference = (W[2] + W[3] - W[0] - W[1]) * I / DeltaTimeS;
				const double TransferNm = FMath::Clamp(AxleSpeedDifference, -TransferLimitNm, TransferLimitNm);
				FrontTorqueNm += TransferNm;
				RearTorqueNm -= TransferNm;
			}

			const TPair<double, double> Front = SplitAxleTorque(FrontTorqueNm, W[0], W[1], I, DeltaTimeS, bIsDriveTorque,
				Car.FrontDifferentialBiasRatio);
			OutWheelTorquesNm[0] = Front.Key;
			OutWheelTorquesNm[1] = Front.Value;
		}

		const TPair<double, double> Rear = SplitAxleTorque(RearTorqueNm, W[2], W[3], I, DeltaTimeS, bIsDriveTorque,
			Car.RearDifferentialBiasRatio);
		OutWheelTorquesNm[2] = Rear.Key;
		OutWheelTorquesNm[3] = Rear.Value;
	}

	TArray<double> SplitDriveTorqueToWheels(
		const FSimCruiseCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque)
	{
		double Torques[4];
		SplitDriveTorqueToWheels(Car, AxleTorqueNm, WheelAngularSpeedsRadPerSecond, DeltaTimeS, bIsDriveTorque, Torques);
		return TArray<double>(Torques, 4);
	}
}
