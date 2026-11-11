// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Photometric daylight numbers in SI units (lux, cd/m2, metres) that the
 * environment uses to set light intensities, haze and the camera's exposure band.
 *
 * Unreal's lighting is physical when the directional light is given in lux and the
 * sky comes from SkyAtmosphere, so these are real-world values rather than tuned
 * game energies. Sources:
 * - Extraterrestrial illuminance: solar constant 1361 W/m2 (Kopp & Lean 2011) times
 *   the luminous efficacy of the extraterrestrial spectrum, 93.9 lm/W (Darula et al.
 *   2005), so 127.8 klx at 1 AU.
 * - Clear and cloudy sky illuminance on the ground: IES Lighting Handbook daylight
 *   model (direct normal 127.5 klx * exp(-c m), diffuse A + B sin(h)^C), used only to
 *   pick the auto-exposure band; the rendered light comes from the atmosphere itself.
 * - Exposure: ISO 2720 reflected-light calibration K = 12.5, EV100 = log2(L * 100 / K).
 * - Visibility: Koschmieder, 2 % contrast remains at the meteorological visibility,
 *   so extinction = 3.912 / V.
 */
struct SIMCRUISE_API FSimCruiseDaylightModel
{
	/** Solar illuminance above the atmosphere at this Earth-Sun distance (lux). */
	static double ExtraterrestrialSunLux(double SunDistanceAU = 1.0);

	/** Relative optical air mass (Kasten & Young 1989) for an apparent elevation in degrees. */
	static double AirMass(double ElevationDeg);

	/**
	 * Illuminance on a horizontal surface from sun and sky, lux, for a sun elevation and a
	 * cloud fraction 0 (clear) to 1 (overcast). Includes twilight down to -6 degrees.
	 */
	static double HorizontalIlluminanceLux(double SunElevationDeg, double CloudCover);

	/** The sky-only (diffuse) part of HorizontalIlluminanceLux. */
	static double DiffuseHorizontalLux(double SunElevationDeg, double CloudCover);

	/** Exposure value at ISO 100 that renders luminance L (cd/m2) at the meter's middle grey. */
	static double EV100FromLuminance(double LuminanceCdM2);

	/** EV100 a reflected-light meter reads off an 18 % grey horizontal surface under this sky. */
	static double ReferenceEV100(double SunElevationDeg, double CloudCover, double MoonIlluminanceLux = 0.0);

	/**
	 * The EV the camera is steered to for a reference EV. In daylight they are equal. Below
	 * EV 10 (sun under about 4 degrees) the eye no longer fully adapts and night looks dark,
	 * so the camera compensates only part of the loss: slope 0.55 below the knee. A full-moon
	 * night (reference about -4) is then shown about 6 EV darker than day, not as daylight.
	 */
	static double DisplayEV100(double ReferenceEV100);

	/** Moonlight on a surface facing the moon above the atmosphere, lux (about 0.3 lx at full moon). */
	static double MoonIlluminanceLux(double PhaseAngleDeg, double MoonDistanceKm);

	/** Total extinction coefficient (1/km) that gives this meteorological visibility. */
	static double ExtinctionPerKmForVisibility(double VisibilityKm);

	/** Visibility (km) for a total extinction coefficient (1/km). */
	static double VisibilityKmForExtinction(double ExtinctionPerKm);

	/** Rayleigh extinction of clean air at 550 nm near sea level, 1/km. */
	static constexpr double RayleighExtinctionPerKm550 = 0.0116;

	/**
	 * Aerosol (Mie) extinction near the ground for a visibility, 1/km: what is left of the
	 * Koschmieder extinction after clean air. Never below a clean-continental floor.
	 */
	static double AerosolExtinctionPerKm(double VisibilityKm);

	/** A typical clear-day visibility when no observation exists (aerosol optical depth about 0.1). */
	static constexpr double DefaultVisibilityKm = 40.0;
};
