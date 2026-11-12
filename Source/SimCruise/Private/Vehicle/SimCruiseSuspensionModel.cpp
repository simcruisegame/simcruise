// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicleDynamics.h"

#include "Math/UnrealMathUtility.h"

namespace SimCruiseDynamics
{
	namespace
	{
		constexpr double PoundsPerInchToNewtonsPerMeter = 175.126835;
		constexpr double PoundsForceToNewtons = 4.4482216153;
		constexpr double SteelShearModulusPa = 79.0e9;
		constexpr double SteelYoungsModulusPa = 205.0e9;

		double GetAxleValue(const TArray<double>& Values, int32 AxleIndex, double Fallback = 0.0)
		{
			return Values.IsValidIndex(AxleIndex) ? Values[AxleIndex] : Fallback;
		}

		double GetDamperDynoLoadN(const FSimCruiseCarDefinition& Car, int32 AxleIndex, double WheelVelocityMps)
		{
			if (!Car.DamperDynoByAxle.IsValidIndex(AxleIndex))
			{
				return 0.0;
			}

			const TArray<FSimCruiseDamperDynoPoint>& Table = Car.DamperDynoByAxle[AxleIndex];
			if (Table.Num() < 2)
			{
				return 0.0;
			}

			const double LeverRatio = GetAxleValue(Car.SpringLeverRatioByAxle, AxleIndex, 1.0);
			const double ShaftSpeedInPerSecond = FMath::Abs(WheelVelocityMps) * LeverRatio / 0.0254;
			const bool bRebound = WheelVelocityMps > 0.0;
			auto GetForceLbf = [bRebound](const FSimCruiseDamperDynoPoint& Point)
			{
				return bRebound ? Point.ReboundForceLbf : Point.CompressionForceLbf;
			};

			double ForceLbf = GetForceLbf(Table.Last());
			for (int32 PointIndex = 1; PointIndex < Table.Num(); ++PointIndex)
			{
				const FSimCruiseDamperDynoPoint& Upper = Table[PointIndex];
				if (ShaftSpeedInPerSecond <= Upper.ShaftSpeedInPerSecond)
				{
					const FSimCruiseDamperDynoPoint& Lower = Table[PointIndex - 1];
					const double Range = Upper.ShaftSpeedInPerSecond - Lower.ShaftSpeedInPerSecond;
					const double Alpha = Range > UE_DOUBLE_SMALL_NUMBER
						? FMath::Clamp((ShaftSpeedInPerSecond - Lower.ShaftSpeedInPerSecond) / Range, 0.0, 1.0)
						: 0.0;
					ForceLbf = FMath::Lerp(GetForceLbf(Lower), GetForceLbf(Upper), Alpha);
					break;
				}
			}

			return -FMath::Sign(WheelVelocityMps) * LeverRatio * ForceLbf * PoundsForceToNewtons;
		}
	}

	double GetWheelSpringRateNPerM(const FSimCruiseCarDefinition& Car, int32 AxleIndex)
	{
		const double WheelRate = GetAxleValue(Car.WheelSpringRateNPerMByAxle, AxleIndex);
		if (WheelRate > 0.0)
		{
			return WheelRate;
		}

		const double CoilRate = GetAxleValue(Car.CoilSpringRateLbfPerInByAxle, AxleIndex);
		const double LeverRatio = GetAxleValue(Car.SpringLeverRatioByAxle, AxleIndex, 1.0);
		return CoilRate * PoundsPerInchToNewtonsPerMeter * LeverRatio * LeverRatio;
	}

	static double GetAntiRollBarRateUnscaled(const FSimCruiseCarDefinition& Car, int32 AxleIndex);

	double GetAntiRollBarRateNPerM(const FSimCruiseCarDefinition& Car, int32 AxleIndex)
	{
		return GetAntiRollBarRateUnscaled(Car, AxleIndex) * FMath::Max(GetAxleValue(Car.AntiRollBarScaleByAxle, AxleIndex, 1.0), 0.0);
	}

	static double GetAntiRollBarRateUnscaled(const FSimCruiseCarDefinition& Car, int32 AxleIndex)
	{
		const double ExplicitRate = GetAxleValue(Car.AntiRollBarRateNmPerMByAxle, AxleIndex);
		if (ExplicitRate > 0.0)
		{
			return ExplicitRate;
		}

		const double DiameterM = GetAxleValue(Car.AntiRollBarDiameterMByAxle, AxleIndex);
		if (DiameterM <= 0.0)
		{
			return 0.0;
		}

		const double TorsionLengthM = GetAxleValue(Car.AntiRollBarTorsionLengthMByAxle, AxleIndex);
		const double ArmM = GetAxleValue(Car.AntiRollBarArmLengthMByAxle, AxleIndex);
		const double LeverRatio = GetAxleValue(Car.AntiRollBarLeverRatioByAxle, AxleIndex, 1.0);
		const double SecondMomentM4 = UE_DOUBLE_PI * FMath::Pow(DiameterM, 4.0) / 64.0;
		if (SecondMomentM4 <= 0.0)
		{
			return 0.0;
		}

		const double TorsionalCompliance = ArmM * ArmM * TorsionLengthM /
			(SteelShearModulusPa * 2.0 * SecondMomentM4);
		const double BendingCompliance = 2.0 * FMath::Pow(ArmM, 3.0) /
			(3.0 * SteelYoungsModulusPa * SecondMomentM4);
		const double TotalCompliance = TorsionalCompliance + BendingCompliance;
		return TotalCompliance > 0.0
			? 0.82 * LeverRatio * LeverRatio / TotalCompliance
			: 0.0;
	}

	double ComputeSuspensionLoadN(
		const FSimCruiseCarDefinition& Car,
		int32 AxleIndex,
		double SuspensionTravelM,
		double SuspensionVelocityMps)
	{
		if (AxleIndex < 0 || AxleIndex > 1)
		{
			return 0.0;
		}

		const double AxleWeight = AxleIndex == 0
			? Car.FrontWeightFraction
			: 1.0 - Car.FrontWeightFraction;
		const double CornerMassKg = Car.MassKg * AxleWeight * 0.5;
		const double SpringRate = GetWheelSpringRateNPerM(Car, AxleIndex);
		const double TravelM = FMath::Clamp(
			SuspensionTravelM,
			Car.MinimumSuspensionTravelM,
			Car.MaximumSuspensionTravelM);
		const double CompressionM = Car.StaticSuspensionLengthM - TravelM;
		const double BumpCompressionM = FMath::Max(CompressionM - Car.BumpStopGapM, 0.0);
		double LoadN = CornerMassKg * GravityMps2 + CompressionM * SpringRate +
			BumpCompressionM * BumpCompressionM * Car.BumpStopStiffnessNPerM2;

		if (Car.DamperModeByAxle.IsValidIndex(AxleIndex) && Car.DamperModeByAxle[AxleIndex] == TEXT("dyno"))
		{
			LoadN += GetDamperDynoLoadN(Car, AxleIndex, SuspensionVelocityMps) * Car.DamperScale;
		}
		else
		{
			const double UnsprungMassKg = Car.UnsprungMassKgPerCorner;
			const double EffectiveCornerMassKg = FMath::Max(CornerMassKg - UnsprungMassKg, 0.0);
			const double CriticalDampingNsPerM = 2.0 * FMath::Sqrt(SpringRate * EffectiveCornerMassKg);
			const double DampingRatio = SuspensionVelocityMps > 0.0
				? GetAxleValue(Car.DamperReboundCriticalFractionByAxle, AxleIndex)
				: GetAxleValue(Car.DamperCompressionCriticalFractionByAxle, AxleIndex);
			const double DamperKneeMps = GetAxleValue(Car.DamperKneeSpeedMPerSecondByAxle, AxleIndex);
			const double DamperKneeSlope = GetAxleValue(Car.DamperKneeSlopeByAxle, AxleIndex, 1.0);
			const double DamperSpeedMps = FMath::Abs(SuspensionVelocityMps);
			const double EffectiveDamperSpeedMps = FMath::Min(DamperSpeedMps, DamperKneeMps) +
				FMath::Max(DamperSpeedMps - DamperKneeMps, 0.0) * DamperKneeSlope;
			LoadN -= FMath::Sign(SuspensionVelocityMps) * CriticalDampingNsPerM * DampingRatio * EffectiveDamperSpeedMps * Car.DamperScale;
		}

		return FMath::Clamp(LoadN, 0.0, Car.MaximumTireContactLoadN);
	}
}
