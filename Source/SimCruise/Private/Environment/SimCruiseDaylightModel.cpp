// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseDaylightModel.h"

namespace
{
	constexpr double SolarConstantWm2 = 1361.0;
	constexpr double ExtraterrestrialEfficacyLmW = 93.9;
	constexpr double ReflectedMeterK = 12.5;
	constexpr double MiddleGreyAlbedo = 0.18;
	constexpr double FullMoonLuxAtMeanDistance = 0.32;
	constexpr double MeanMoonDistanceKm = 384400.0;
	constexpr double NightSkyFloorLux = 0.001; // airglow and starlight without the moon

	/** IES daylight parameters blended over the cloud fraction: clear, partly cloudy, overcast. */
	struct FIesSky
	{
		double DirectExtinction; // c in exp(-c m); overcast has no direct beam
		double A;                // klux
		double B;                // klux
		double C;
		double DirectFraction;   // how much of the beam survives (0 under overcast)
	};

	FIesSky IesSkyFor(double CloudCover)
	{
		const double Cloud = FMath::Clamp(CloudCover, 0.0, 1.0);
		const FIesSky Clear{0.21, 0.8, 15.5, 0.5, 1.0};
		const FIesSky Partly{0.80, 0.3, 45.0, 1.0, 1.0};
		const FIesSky Overcast{0.80, 0.3, 21.0, 1.0, 0.0};
		auto Blend = [](const FIesSky& X, const FIesSky& Y, double Alpha)
		{
			return FIesSky{
				FMath::Lerp(X.DirectExtinction, Y.DirectExtinction, Alpha),
				FMath::Lerp(X.A, Y.A, Alpha),
				FMath::Lerp(X.B, Y.B, Alpha),
				FMath::Lerp(X.C, Y.C, Alpha),
				FMath::Lerp(X.DirectFraction, Y.DirectFraction, Alpha)};
		};
		return Cloud < 0.5 ? Blend(Clear, Partly, Cloud / 0.5) : Blend(Partly, Overcast, (Cloud - 0.5) / 0.5);
	}

	double DiffuseDaylightLux(double ElevationDeg, const FIesSky& Sky)
	{
		const double SinH = FMath::Max(FMath::Sin(FMath::DegreesToRadians(ElevationDeg)), 0.0);
		return 1000.0 * (Sky.A + Sky.B * FMath::Pow(SinH, Sky.C));
	}

	/** Log-linear twilight below the horizon, matching the daylight value at 0 degrees. */
	double TwilightScale(double ElevationDeg)
	{
		if (ElevationDeg >= 0.0)
		{
			return 1.0;
		}
		// About 800 lx at sunset to 3.4 lx at the end of civil twilight (-6) and 0.008 lx at -12.
		if (ElevationDeg >= -6.0)
		{
			return FMath::Pow(10.0, 0.395 * ElevationDeg);
		}
		const double CivilEnd = FMath::Pow(10.0, 0.395 * -6.0);
		return CivilEnd * FMath::Pow(10.0, 0.44 * (ElevationDeg + 6.0));
	}
}

double FSimCruiseDaylightModel::ExtraterrestrialSunLux(double SunDistanceAU)
{
	const double Distance = SunDistanceAU > 0.5 ? SunDistanceAU : 1.0;
	return SolarConstantWm2 * ExtraterrestrialEfficacyLmW / (Distance * Distance);
}

double FSimCruiseDaylightModel::AirMass(double ElevationDeg)
{
	const double H = FMath::Max(ElevationDeg, 0.0);
	return 1.0 / (FMath::Sin(FMath::DegreesToRadians(H)) + 0.50572 * FMath::Pow(H + 6.07995, -1.6364));
}

double FSimCruiseDaylightModel::DiffuseHorizontalLux(double SunElevationDeg, double CloudCover)
{
	const FIesSky Sky = IesSkyFor(CloudCover);
	if (SunElevationDeg >= 0.0)
	{
		return DiffuseDaylightLux(SunElevationDeg, Sky);
	}
	return FMath::Max(DiffuseDaylightLux(0.0, Sky) * TwilightScale(SunElevationDeg), NightSkyFloorLux);
}

double FSimCruiseDaylightModel::HorizontalIlluminanceLux(double SunElevationDeg, double CloudCover)
{
	const FIesSky Sky = IesSkyFor(CloudCover);
	double Direct = 0.0;
	if (SunElevationDeg > 0.0)
	{
		const double Normal = 127500.0 * FMath::Exp(-Sky.DirectExtinction * AirMass(SunElevationDeg));
		Direct = Sky.DirectFraction * Normal * FMath::Sin(FMath::DegreesToRadians(SunElevationDeg));
	}
	return Direct + DiffuseHorizontalLux(SunElevationDeg, CloudCover);
}

double FSimCruiseDaylightModel::EV100FromLuminance(double LuminanceCdM2)
{
	return FMath::Log2(FMath::Max(LuminanceCdM2, 1.0e-6) * 100.0 / ReflectedMeterK);
}

double FSimCruiseDaylightModel::ReferenceEV100(double SunElevationDeg, double CloudCover, double MoonIlluminanceLux)
{
	const double Illuminance = HorizontalIlluminanceLux(SunElevationDeg, CloudCover) + FMath::Max(MoonIlluminanceLux, 0.0);
	return EV100FromLuminance(MiddleGreyAlbedo * Illuminance / UE_DOUBLE_PI);
}

double FSimCruiseDaylightModel::DisplayEV100(double ReferenceEV100)
{
	constexpr double Knee = 10.0;
	constexpr double NightSlope = 0.55;
	return ReferenceEV100 >= Knee ? ReferenceEV100 : Knee + (ReferenceEV100 - Knee) * NightSlope;
}

double FSimCruiseDaylightModel::MoonIlluminanceLux(double PhaseAngleDeg, double MoonDistanceKm)
{
	// Allen's lunar phase law: magnitude fades 0.026 per degree plus a quartic term.
	const double Alpha = FMath::Abs(PhaseAngleDeg);
	const double DeltaMagnitude = 0.026 * Alpha + 4.0e-9 * FMath::Pow(Alpha, 4.0);
	const double Distance = MoonDistanceKm > 1000.0 ? MoonDistanceKm : MeanMoonDistanceKm;
	return FullMoonLuxAtMeanDistance * FMath::Pow(10.0, -0.4 * DeltaMagnitude) * FMath::Square(MeanMoonDistanceKm / Distance);
}

double FSimCruiseDaylightModel::ExtinctionPerKmForVisibility(double VisibilityKm)
{
	return 3.912 / FMath::Max(VisibilityKm, 0.01);
}

double FSimCruiseDaylightModel::VisibilityKmForExtinction(double ExtinctionPerKm)
{
	return 3.912 / FMath::Max(ExtinctionPerKm, 1.0e-6);
}

double FSimCruiseDaylightModel::AerosolExtinctionPerKm(double VisibilityKm)
{
	// A clean continental aerosol floor (about 150 km visibility with clean air).
	return FMath::Max(ExtinctionPerKmForVisibility(VisibilityKm) - RayleighExtinctionPerKm550, 0.014);
}

double FSimCruiseDaylightModel::AerosolForOpticalDepth(double OpticalDepth, double ScaleHeightKm)
{
	return FMath::Max(FMath::Max(OpticalDepth, 0.0) / FMath::Max(ScaleHeightKm, 0.1), 0.014);
}
