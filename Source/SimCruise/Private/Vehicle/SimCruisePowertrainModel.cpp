// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicleDynamics.h"

#include "Math/UnrealMathUtility.h"

namespace SimCruiseDynamics
{
	namespace
	{
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

	TArray<double> SplitDriveTorqueToWheels(
		const FSimCruiseCarDefinition& Car,
		double AxleTorqueNm,
		const double WheelAngularSpeedsRadPerSecond[4],
		double DeltaTimeS,
		bool bIsDriveTorque)
	{
		TArray<double> WheelTorquesNm;
		WheelTorquesNm.Init(0.0, 4);
		if (WheelAngularSpeedsRadPerSecond == nullptr)
		{
			return WheelTorquesNm;
		}

		if (Car.DrivetrainLayout == TEXT("fwd"))
		{
			const TPair<double, double> Front = SplitAxleTorque(
				AxleTorqueNm,
				WheelAngularSpeedsRadPerSecond[0],
				WheelAngularSpeedsRadPerSecond[1],
				Car.WheelRotationalInertiaKgM2,
				DeltaTimeS,
				bIsDriveTorque,
				Car.FrontDifferentialBiasRatio);
			WheelTorquesNm[0] = Front.Key;
			WheelTorquesNm[1] = Front.Value;
			return WheelTorquesNm;
		}

		double RearTorqueNm = AxleTorqueNm;
		if (Car.DrivetrainLayout == TEXT("awd"))
		{
			double FrontTorqueNm = AxleTorqueNm * Car.AwdFrontTorqueShareFraction;
			RearTorqueNm = AxleTorqueNm * (1.0 - Car.AwdFrontTorqueShareFraction);
			if (bIsDriveTorque && DeltaTimeS > 0.0 && Car.CenterDifferentialBiasRatio > 1.0)
			{
				const double TransferLimitNm = FMath::Abs(AxleTorqueNm) * 0.5 *
					(Car.CenterDifferentialBiasRatio - 1.0) / (Car.CenterDifferentialBiasRatio + 1.0);
				const double AxleSpeedDifference =
					(WheelAngularSpeedsRadPerSecond[2] + WheelAngularSpeedsRadPerSecond[3] -
					 WheelAngularSpeedsRadPerSecond[0] - WheelAngularSpeedsRadPerSecond[1]) *
					Car.WheelRotationalInertiaKgM2 / DeltaTimeS;
				const double TransferNm = FMath::Clamp(AxleSpeedDifference, -TransferLimitNm, TransferLimitNm);
				FrontTorqueNm += TransferNm;
				RearTorqueNm -= TransferNm;
			}

			const TPair<double, double> Front = SplitAxleTorque(
				FrontTorqueNm,
				WheelAngularSpeedsRadPerSecond[0],
				WheelAngularSpeedsRadPerSecond[1],
				Car.WheelRotationalInertiaKgM2,
				DeltaTimeS,
				bIsDriveTorque,
				Car.FrontDifferentialBiasRatio);
			WheelTorquesNm[0] = Front.Key;
			WheelTorquesNm[1] = Front.Value;
		}

		const TPair<double, double> Rear = SplitAxleTorque(
			RearTorqueNm,
			WheelAngularSpeedsRadPerSecond[2],
			WheelAngularSpeedsRadPerSecond[3],
			Car.WheelRotationalInertiaKgM2,
			DeltaTimeS,
			bIsDriveTorque,
			Car.RearDifferentialBiasRatio);
		WheelTorquesNm[2] = Rear.Key;
		WheelTorquesNm[3] = Rear.Value;
		return WheelTorquesNm;
	}
}
