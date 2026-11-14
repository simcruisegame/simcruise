// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Köppen-Geiger climate class at a place, from the bundled global class grid
 * data/climate/koppen-1991-2020.png: Beck et al. (2023), "High-resolution (1 km)
 * Köppen-Geiger maps for 1901-2099 based on constrained CMIP6 projections",
 * Scientific Data 10, 724, doi:10.6084/m9.figshare.21789074, CC BY 4.0. The 1 km
 * 1991-2020 map is cut down by majority class (land cells only) to the grid in the
 * file (lab/koppen/make_grid.py); one 8-bit grey pixel per cell, the value is
 * Beck's class number (1 Af ... 30 EF, 0 sea or no data). Row 0 is 90 N, column 0
 * is 180 W. When the file is missing every query answers 0 and callers keep the
 * old latitude band.
 *
 * Thread-safe: the grid is decoded once (on the first query, on whichever thread
 * asks) into run lengths per row (about 2 MB for the whole world).
 */
namespace SimCruiseClimate
{
	/** Beck's class number at a point (0 when unknown). A sea cell takes the nearest land class within 3 cells. */
	SIMCRUISE_API uint8 KoppenAt(double LatDeg, double LonDeg);

	/** "Af", "Cfa" ... for a class number, empty for 0 or out of range. */
	SIMCRUISE_API const TCHAR* KoppenCode(uint8 Class);

	/** The class number of "Cfa" (0 when unknown). */
	SIMCRUISE_API uint8 KoppenFromCode(const FString& Code);

	/**
	 * The place-graph climate fact of a class: Af/Am/Aw climate:tropical, B* climate:arid,
	 * Cs* climate:mediterranean, Cfa/Cwa climate:humid_subtropical, Cfb/Cfc/Cwb/Cwc
	 * climate:oceanic, Dxa/Dxb climate:humid_continental, Dxc/Dxd climate:subarctic,
	 * ET/EF climate:polar. NAME_None for 0.
	 */
	SIMCRUISE_API FName ClimateFact(uint8 Class);

	/** The old rough band (|lat| < 23.5 tropical, < 35 humid subtropical, < 55 humid continental, else subarctic). */
	SIMCRUISE_API FName LatitudeBandFact(double LatDeg);

	/** True when the grid file was found and decoded. */
	SIMCRUISE_API bool HasGrid();
	SIMCRUISE_API FString GridPath();
	/** Where the class comes from, for "why" strings and the data-sources page. */
	SIMCRUISE_API const TCHAR* Source();

	/** For tests: decode a grid from PNG bytes instead of the file (replaces the loaded one). */
	SIMCRUISE_API bool LoadGridForTest(const TArray<uint8>& Png);
	/** For tests: forget the test grid; the next query loads the file again. */
	SIMCRUISE_API void ResetGrid();

	/**
	 * The season at a place and month (1-12): season:winter/spring/summer/autumn outside the
	 * tropics (flipped in the south); for tropical classes season:wet or season:dry (Af wet
	 * all year; Am/Aw wet May-October north of the equator, November-April south).
	 */
	SIMCRUISE_API FName Season(uint8 Class, FName ClimateFallback, double LatDeg, int32 Month);

	/**
	 * Share of dry, dormant grass (0 green ... 1 straw) for a class and month: the grass
	 * field material's GrassDry. Estimates from the seasonal cycle of each class (warm-season
	 * lawns of Cfa go dormant tan in winter; Cs dries in summer; deserts stay dry).
	 */
	SIMCRUISE_API float GrassDry(uint8 Class, FName ClimateFallback, double LatDeg, int32 Month);
}
