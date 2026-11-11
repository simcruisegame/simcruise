// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/DateTime.h"

/**
 * Where a sky body stands for an observer on the ground.
 *
 * Azimuth is measured clockwise from true north (90 = east), elevation above the
 * astronomical horizon. ApparentElevationDeg includes standard atmospheric
 * refraction; TrueElevationDeg does not.
 */
struct SIMCRUISE_API FSimCruiseSkyPosition
{
	double AzimuthDeg = 0.0;
	double ApparentElevationDeg = 0.0;
	double TrueElevationDeg = 0.0;
	double DeclinationDeg = 0.0;
	double RightAscensionDeg = 0.0;
	double HourAngleDeg = 0.0;
	/** Sun: equation of time in minutes (apparent minus mean solar time). Zero for the moon. */
	double EquationOfTimeMinutes = 0.0;
	/** Sun: astronomical units. Moon: kilometres. */
	double Distance = 0.0;
	/** Moon only: illuminated fraction of the disc, 0 (new) to 1 (full). */
	double IlluminatedFraction = 0.0;
	/** Moon only: Sun-Moon-Earth phase angle in degrees, 0 at full moon. */
	double PhaseAngleDeg = 0.0;
};

/**
 * Sun and moon positions from latitude, longitude and a UTC instant.
 *
 * Sun: NOAA Global Monitoring Laboratory solar calculator (the spreadsheet
 * algorithm after Meeus, "Astronomical Algorithms"), about 0.01 degree for years
 * 1800-2100. https://gml.noaa.gov/grad/solcalc/calcdetails.html
 *
 * Moon: Meeus chapter 47 with the largest periodic terms (about 0.1-0.3 degree),
 * phase from chapter 48. Enough to place moonlight and its strength.
 *
 * World frame (shared with the map stream, see docs/unreal-architecture.md):
 * +X east, +Y south, +Z up, the left-handed east-south-up frame Cesium for
 * Unreal uses. North is -Y.
 */
class SIMCRUISE_API FSimCruiseSolarPosition
{
public:
	/** Julian day (UT) of a UTC instant. */
	static double JulianDay(const FDateTime& Utc);

	/** The sun for an observer at LatitudeDeg (north positive), LongitudeDeg (east positive). */
	static FSimCruiseSkyPosition Sun(double LatitudeDeg, double LongitudeDeg, const FDateTime& Utc);

	/** The moon for the same observer, with topocentric parallax and its phase. */
	static FSimCruiseSkyPosition Moon(double LatitudeDeg, double LongitudeDeg, const FDateTime& Utc);

	/** NOAA's refraction correction, degrees to add to the true elevation (standard atmosphere). */
	static double RefractionDeg(double TrueElevationDeg);

	/**
	 * Unit vector from the observer toward a body at this azimuth and elevation in the
	 * world frame (+X east, +Y south, +Z up). NorthYawDeg rotates the map if the world
	 * north is not -Y (positive turns north toward +X, clockwise seen from above).
	 */
	static FVector ToWorldDirection(double AzimuthDeg, double ElevationDeg, double NorthYawDeg = 0.0);

	/** Inverse of ToWorldDirection: azimuth (deg from north, clockwise) and elevation (deg). */
	static void FromWorldDirection(const FVector& Direction, double& OutAzimuthDeg, double& OutElevationDeg, double NorthYawDeg = 0.0);

	/**
	 * The Godot client's fractional-year approximation (NOAA "General Solar Position
	 * Calculations", game/SurveyDaylight.gd solar_for_day). Kept as the calibration
	 * reference the tests compare against; about 0.3 degree.
	 */
	static FSimCruiseSkyPosition SunFractionalYear(double LatitudeDeg, double LongitudeDeg, const FDateTime& Utc);

	/** UTC instant of solar noon for this place on this UTC calendar day. */
	static FDateTime SolarNoonUtc(double LongitudeDeg, const FDateTime& DayUtc);
};
