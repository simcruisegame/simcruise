// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseSolarPosition.h"

namespace SimCruiseSolar
{
	constexpr double DegToRad = UE_DOUBLE_PI / 180.0;
	constexpr double RadToDeg = 180.0 / UE_DOUBLE_PI;
	constexpr double AstronomicalUnitKm = 149597870.7;
	constexpr double EarthEquatorialRadiusKm = 6378.14;

	double Wrap360(double Degrees)
	{
		double Wrapped = FMath::Fmod(Degrees, 360.0);
		return Wrapped < 0.0 ? Wrapped + 360.0 : Wrapped;
	}

	double SinD(double Degrees) { return FMath::Sin(Degrees * DegToRad); }
	double CosD(double Degrees) { return FMath::Cos(Degrees * DegToRad); }
	double TanD(double Degrees) { return FMath::Tan(Degrees * DegToRad); }

	/** Azimuth clockwise from north and true elevation for an hour angle and declination. */
	void HorizontalFromEquatorial(double LatitudeDeg, double HourAngleDeg, double DeclinationDeg, double& OutAzimuthDeg, double& OutElevationDeg)
	{
		const double SinElevation = SinD(LatitudeDeg) * SinD(DeclinationDeg) + CosD(LatitudeDeg) * CosD(DeclinationDeg) * CosD(HourAngleDeg);
		OutElevationDeg = FMath::Asin(FMath::Clamp(SinElevation, -1.0, 1.0)) * RadToDeg;
		// Meeus 13.5 measures azimuth from the south; adding 180 turns it into a bearing from north.
		const double AzimuthFromSouth = FMath::Atan2(SinD(HourAngleDeg), CosD(HourAngleDeg) * SinD(LatitudeDeg) - TanD(DeclinationDeg) * CosD(LatitudeDeg)) * RadToDeg;
		OutAzimuthDeg = Wrap360(AzimuthFromSouth + 180.0);
	}

	/** The sun's apparent ecliptic longitude, obliquity and distance for Julian century T (NOAA sheet). */
	struct FSunEcliptic
	{
		double GeomMeanLongitude = 0.0;
		double MeanAnomaly = 0.0;
		double Eccentricity = 0.0;
		double ApparentLongitude = 0.0;
		double ObliquityCorrected = 0.0;
		double RadiusAU = 0.0;
	};

	FSunEcliptic SunEcliptic(double T)
	{
		FSunEcliptic Out;
		Out.GeomMeanLongitude = Wrap360(280.46646 + T * (36000.76983 + T * 0.0003032));
		Out.MeanAnomaly = 357.52911 + T * (35999.05029 - 0.0001537 * T);
		Out.Eccentricity = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
		const double M = Out.MeanAnomaly;
		const double Center = SinD(M) * (1.914602 - T * (0.004817 + 0.000014 * T))
			+ SinD(2.0 * M) * (0.019993 - 0.000101 * T)
			+ SinD(3.0 * M) * 0.000289;
		const double TrueLongitude = Out.GeomMeanLongitude + Center;
		const double TrueAnomaly = M + Center;
		const double E = Out.Eccentricity;
		Out.RadiusAU = (1.000001018 * (1.0 - E * E)) / (1.0 + E * CosD(TrueAnomaly));
		const double Omega = 125.04 - 1934.136 * T;
		Out.ApparentLongitude = TrueLongitude - 0.00569 - 0.00478 * SinD(Omega);
		const double MeanObliquity = 23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
		Out.ObliquityCorrected = MeanObliquity + 0.00256 * CosD(Omega);
		return Out;
	}

	double EquationOfTimeMinutes(const FSunEcliptic& Sun)
	{
		const double Y = FMath::Square(TanD(Sun.ObliquityCorrected / 2.0));
		const double L0 = Sun.GeomMeanLongitude;
		const double M = Sun.MeanAnomaly;
		const double E = Sun.Eccentricity;
		const double Radians = Y * SinD(2.0 * L0) - 2.0 * E * SinD(M) + 4.0 * E * Y * SinD(M) * CosD(2.0 * L0)
			- 0.5 * Y * Y * SinD(4.0 * L0) - 1.25 * E * E * SinD(2.0 * M);
		return 4.0 * Radians * RadToDeg;
	}

	double JulianCentury(double JulianDay)
	{
		return (JulianDay - 2451545.0) / 36525.0;
	}

	double MinutesOfDay(const FDateTime& Utc)
	{
		return Utc.GetTimeOfDay().GetTotalMinutes();
	}

	/** One row of Meeus tables 47.A/47.B: multiples of D, M, M', F and the coefficient(s). */
	struct FLunarTerm
	{
		int8 D;
		int8 M;
		int8 Mp;
		int8 F;
		int32 SumL; // 1e-6 degree (47.A) or Sigma b (47.B)
		int32 SumR; // 1e-3 km (47.A only)
	};

	// The largest terms of Meeus table 47.A (longitude and distance).
	const FLunarTerm LongitudeDistanceTerms[] = {
		{0, 0, 1, 0, 6288774, -20905355}, {2, 0, -1, 0, 1274027, -3699111}, {2, 0, 0, 0, 658314, -2955968},
		{0, 0, 2, 0, 213618, -569925}, {0, 1, 0, 0, -185116, 48888}, {0, 0, 0, 2, -114332, -3149},
		{2, 0, -2, 0, 58793, 246158}, {2, -1, -1, 0, 57066, -152138}, {2, 0, 1, 0, 53322, -170733},
		{2, -1, 0, 0, 45758, -204586}, {0, 1, -1, 0, -40923, -129620}, {1, 0, 0, 0, -34720, 108743},
		{0, 1, 1, 0, -30383, 104755}, {2, 0, 0, -2, 15327, 10321}, {0, 0, 1, 2, -12528, 0},
		{0, 0, 1, -2, 10980, 79661}, {4, 0, -1, 0, 10675, -34782}, {0, 0, 3, 0, 10034, -23210},
		{4, 0, -2, 0, 8548, -21636}, {2, 1, -1, 0, -7888, 24208}, {2, 1, 0, 0, -6766, 30824},
		{1, 0, -1, 0, -5163, -8379}, {1, 1, 0, 0, 4987, -16675}, {2, -1, 1, 0, 4036, -12831},
		{2, 0, 2, 0, 3994, -10445}, {4, 0, 0, 0, 3861, -11650}, {2, 0, -3, 0, 3665, 14403},
		{0, 1, -2, 0, -2689, -7003}, {2, 0, -1, 2, -2602, 0}, {2, -1, -2, 0, 2390, 10056},
		{1, 0, 1, 0, -2348, 6322}, {2, -2, 0, 0, 2236, -9884},
	};

	// The largest terms of Meeus table 47.B (latitude); SumR unused.
	const FLunarTerm LatitudeTerms[] = {
		{0, 0, 0, 1, 5128122, 0}, {0, 0, 1, 1, 280602, 0}, {0, 0, 1, -1, 277693, 0}, {2, 0, 0, -1, 173237, 0},
		{2, 0, -1, 1, 55413, 0}, {2, 0, -1, -1, 46271, 0}, {2, 0, 0, 1, 32573, 0}, {0, 0, 2, 1, 17198, 0},
		{2, 0, 1, -1, 9266, 0}, {0, 0, 2, -1, 8822, 0}, {2, -1, 0, -1, 8216, 0}, {2, 0, -2, -1, 4324, 0},
		{2, 0, 1, 1, 4200, 0}, {2, 1, 0, -1, -3359, 0}, {2, -1, -1, 1, 2463, 0}, {2, -1, 0, 1, 2211, 0},
		{2, -1, -1, -1, 2065, 0}, {0, 1, -1, -1, -1870, 0}, {4, 0, -1, -1, 1828, 0}, {0, 1, 0, 1, -1794, 0},
		{0, 0, 0, 3, -1749, 0}, {0, 1, -1, 1, -1565, 0}, {1, 0, 0, 1, -1491, 0}, {0, 1, 1, 1, -1475, 0},
		{0, 1, 1, -1, -1410, 0}, {0, 1, 0, -1, -1344, 0}, {1, 0, 0, -1, -1335, 0}, {0, 0, 3, 1, 1107, 0},
	};
}

double FSimCruiseSolarPosition::JulianDay(const FDateTime& Utc)
{
	return Utc.GetJulianDay();
}

double FSimCruiseSolarPosition::RefractionDeg(double TrueElevationDeg)
{
	using namespace SimCruiseSolar;
	const double E = TrueElevationDeg;
	double ArcSeconds = 0.0;
	if (E > 85.0)
	{
		ArcSeconds = 0.0;
	}
	else if (E > 5.0)
	{
		const double T = TanD(E);
		ArcSeconds = 58.1 / T - 0.07 / (T * T * T) + 0.000086 / FMath::Pow(T, 5.0);
	}
	else if (E > -0.575)
	{
		ArcSeconds = 1735.0 + E * (-518.2 + E * (103.4 + E * (-12.79 + E * 0.711)));
	}
	else
	{
		ArcSeconds = -20.772 / TanD(E);
	}
	return ArcSeconds / 3600.0;
}

FSimCruiseSkyPosition FSimCruiseSolarPosition::Sun(double LatitudeDeg, double LongitudeDeg, const FDateTime& Utc)
{
	using namespace SimCruiseSolar;
	const double T = JulianCentury(JulianDay(Utc));
	const FSunEcliptic Ecl = SunEcliptic(T);

	FSimCruiseSkyPosition Out;
	Out.Distance = Ecl.RadiusAU;
	Out.DeclinationDeg = FMath::Asin(SinD(Ecl.ObliquityCorrected) * SinD(Ecl.ApparentLongitude)) * RadToDeg;
	Out.RightAscensionDeg = Wrap360(FMath::Atan2(CosD(Ecl.ObliquityCorrected) * SinD(Ecl.ApparentLongitude), CosD(Ecl.ApparentLongitude)) * RadToDeg);
	Out.EquationOfTimeMinutes = EquationOfTimeMinutes(Ecl);

	// True solar time in minutes; UTC is used directly, so no time-zone term.
	const double TrueSolarMinutes = FMath::Fmod(MinutesOfDay(Utc) + Out.EquationOfTimeMinutes + 4.0 * LongitudeDeg + 1440.0 * 4.0, 1440.0);
	Out.HourAngleDeg = TrueSolarMinutes / 4.0 < 0.0 ? TrueSolarMinutes / 4.0 + 180.0 : TrueSolarMinutes / 4.0 - 180.0;

	HorizontalFromEquatorial(LatitudeDeg, Out.HourAngleDeg, Out.DeclinationDeg, Out.AzimuthDeg, Out.TrueElevationDeg);
	Out.ApparentElevationDeg = Out.TrueElevationDeg + RefractionDeg(Out.TrueElevationDeg);
	return Out;
}

FSimCruiseSkyPosition FSimCruiseSolarPosition::Moon(double LatitudeDeg, double LongitudeDeg, const FDateTime& Utc)
{
	using namespace SimCruiseSolar;
	const double JD = JulianDay(Utc);
	const double T = JulianCentury(JD);
	const double T2 = T * T;
	const double T3 = T2 * T;
	const double T4 = T3 * T;

	// Meeus 47.1-47.7: mean elements (degrees).
	const double Lp = Wrap360(218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0);
	const double D = Wrap360(297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0);
	const double M = Wrap360(357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000.0);
	const double Mp = Wrap360(134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0);
	const double F = Wrap360(93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0);
	const double A1 = Wrap360(119.75 + 131.849 * T);
	const double A2 = Wrap360(53.09 + 479264.290 * T);
	const double A3 = Wrap360(313.45 + 481266.484 * T);
	const double E = 1.0 - 0.002516 * T - 0.0000074 * T2;

	double SumL = 0.0;
	double SumR = 0.0;
	for (const FLunarTerm& Term : LongitudeDistanceTerms)
	{
		const double Argument = Term.D * D + Term.M * M + Term.Mp * Mp + Term.F * F;
		const double Eccentricity = FMath::Abs(Term.M) == 1 ? E : (FMath::Abs(Term.M) == 2 ? E * E : 1.0);
		SumL += Term.SumL * Eccentricity * SinD(Argument);
		SumR += Term.SumR * Eccentricity * CosD(Argument);
	}
	double SumB = 0.0;
	for (const FLunarTerm& Term : LatitudeTerms)
	{
		const double Argument = Term.D * D + Term.M * M + Term.Mp * Mp + Term.F * F;
		const double Eccentricity = FMath::Abs(Term.M) == 1 ? E : (FMath::Abs(Term.M) == 2 ? E * E : 1.0);
		SumB += Term.SumL * Eccentricity * SinD(Argument);
	}
	SumL += 3958.0 * SinD(A1) + 1962.0 * SinD(Lp - F) + 318.0 * SinD(A2);
	SumB += -2235.0 * SinD(Lp) + 382.0 * SinD(A3) + 175.0 * SinD(A1 - F) + 175.0 * SinD(A1 + F)
		+ 127.0 * SinD(Lp - Mp) - 115.0 * SinD(Lp + Mp);

	const double Omega = 125.04452 - 1934.136261 * T;
	const double NutationLongitude = -0.004778 * SinD(Omega); // the dominant nutation term, degrees
	const double Longitude = Wrap360(Lp + SumL / 1.0e6 + NutationLongitude);
	const double Latitude = SumB / 1.0e6;
	const double DistanceKm = 385000.56 + SumR / 1000.0;

	const FSunEcliptic SunEcl = SunEcliptic(T);
	const double Obliquity = SunEcl.ObliquityCorrected;

	FSimCruiseSkyPosition Out;
	Out.Distance = DistanceKm;
	Out.RightAscensionDeg = Wrap360(FMath::Atan2(SinD(Longitude) * CosD(Obliquity) - TanD(Latitude) * SinD(Obliquity), CosD(Longitude)) * RadToDeg);
	Out.DeclinationDeg = FMath::Asin(SinD(Latitude) * CosD(Obliquity) + CosD(Latitude) * SinD(Obliquity) * SinD(Longitude)) * RadToDeg;

	// Meeus 12.4: Greenwich mean sidereal time; the local value adds east longitude.
	const double Gmst = Wrap360(280.46061837 + 360.98564736629 * (JD - 2451545.0) + 0.000387933 * T2 - T3 / 38710000.0);
	double HourAngle = Wrap360(Gmst + LongitudeDeg - Out.RightAscensionDeg);
	if (HourAngle > 180.0)
	{
		HourAngle -= 360.0;
	}
	Out.HourAngleDeg = HourAngle;

	double GeocentricElevation = 0.0;
	HorizontalFromEquatorial(LatitudeDeg, Out.HourAngleDeg, Out.DeclinationDeg, Out.AzimuthDeg, GeocentricElevation);
	// Topocentric parallax lowers the moon by up to about 1 degree near the horizon.
	const double Parallax = FMath::Asin(EarthEquatorialRadiusKm / DistanceKm) * RadToDeg;
	Out.TrueElevationDeg = GeocentricElevation - Parallax * CosD(GeocentricElevation);
	Out.ApparentElevationDeg = Out.TrueElevationDeg + RefractionDeg(Out.TrueElevationDeg);

	// Meeus 48.2-48.3: phase angle from the elongation, using the sun's apparent longitude.
	const double SunDistanceKm = SunEcl.RadiusAU * AstronomicalUnitKm;
	const double CosElongation = CosD(Latitude) * CosD(Longitude - SunEcl.ApparentLongitude);
	const double Elongation = FMath::Acos(FMath::Clamp(CosElongation, -1.0, 1.0));
	const double PhaseAngle = FMath::Atan2(SunDistanceKm * FMath::Sin(Elongation), DistanceKm - SunDistanceKm * FMath::Cos(Elongation));
	Out.PhaseAngleDeg = PhaseAngle * RadToDeg;
	Out.IlluminatedFraction = (1.0 + FMath::Cos(PhaseAngle)) / 2.0;
	return Out;
}

FVector FSimCruiseSolarPosition::ToWorldDirection(double AzimuthDeg, double ElevationDeg, double NorthYawDeg)
{
	using namespace SimCruiseSolar;
	const double Heading = AzimuthDeg + NorthYawDeg;
	const double Horizontal = CosD(ElevationDeg);
	// East is +X, north is -Y, up is +Z.
	return FVector(Horizontal * SinD(Heading), -Horizontal * CosD(Heading), SinD(ElevationDeg)).GetSafeNormal();
}

void FSimCruiseSolarPosition::FromWorldDirection(const FVector& Direction, double& OutAzimuthDeg, double& OutElevationDeg, double NorthYawDeg)
{
	using namespace SimCruiseSolar;
	const FVector Unit = Direction.GetSafeNormal();
	OutElevationDeg = FMath::Asin(FMath::Clamp(Unit.Z, -1.0, 1.0)) * RadToDeg;
	OutAzimuthDeg = Wrap360(FMath::Atan2(Unit.X, -Unit.Y) * RadToDeg - NorthYawDeg);
}

FSimCruiseSkyPosition FSimCruiseSolarPosition::SunFractionalYear(double LatitudeDeg, double LongitudeDeg, const FDateTime& Utc)
{
	using namespace SimCruiseSolar;
	const int32 Year = Utc.GetYear();
	const double Days = FDateTime::IsLeapYear(Year) ? 366.0 : 365.0;
	const double Day = Utc.GetDayOfYear();
	const double UtcHour = Utc.GetTimeOfDay().GetTotalHours();

	const double Gamma = 2.0 * UE_DOUBLE_PI / Days * (Day - 1.0 + (UtcHour - 12.0) / 24.0);
	const double C1 = FMath::Cos(Gamma);
	const double S1 = FMath::Sin(Gamma);
	const double C2 = FMath::Cos(2.0 * Gamma);
	const double S2 = FMath::Sin(2.0 * Gamma);
	const double Minutes = 229.18 * (0.000075 + 0.001868 * C1 - 0.032077 * S1 - 0.014615 * C2 - 0.040849 * S2);
	const double Declination = 0.006918 - 0.399912 * C1 + 0.070257 * S1 - 0.006758 * C2 + 0.000907 * S2
		- 0.002697 * FMath::Cos(3.0 * Gamma) + 0.00148 * FMath::Sin(3.0 * Gamma);
	const double HourAngleDeg = (UtcHour * 60.0 + Minutes + 4.0 * LongitudeDeg) / 4.0 - 180.0;

	FSimCruiseSkyPosition Out;
	Out.DeclinationDeg = Declination * RadToDeg;
	Out.EquationOfTimeMinutes = Minutes;
	Out.HourAngleDeg = HourAngleDeg;
	HorizontalFromEquatorial(LatitudeDeg, HourAngleDeg, Out.DeclinationDeg, Out.AzimuthDeg, Out.TrueElevationDeg);
	Out.ApparentElevationDeg = Out.TrueElevationDeg;
	Out.Distance = 1.0;
	return Out;
}

FDateTime FSimCruiseSolarPosition::SolarNoonUtc(double LongitudeDeg, const FDateTime& DayUtc)
{
	const FDateTime Midnight = DayUtc.GetDate();
	// Two passes: the equation of time is evaluated at the first estimate of noon.
	double NoonMinutes = 720.0 - 4.0 * LongitudeDeg;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FDateTime Estimate = Midnight + FTimespan::FromMinutes(NoonMinutes);
		const double EquationOfTime = Sun(0.0, LongitudeDeg, Estimate).EquationOfTimeMinutes;
		NoonMinutes = 720.0 - 4.0 * LongitudeDeg - EquationOfTime;
	}
	return Midnight + FTimespan::FromMinutes(NoonMinutes);
}
