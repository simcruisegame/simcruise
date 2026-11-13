// Copyright SimCruise contributors. All Rights Reserved.

#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Environment/SimCruiseDaylightModel.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseGraphics.h"
#include "Environment/SimCruisePerfSampler.h"
#include "Environment/SimCruiseSolarPosition.h"
#include "Environment/SimCruiseWeather.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseEnvironmentTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	FDateTime Utc(int32 Year, int32 Month, int32 Day, int32 Hour, int32 Minute, int32 Second)
	{
		return FDateTime(Year, Month, Day, Hour, Minute, Second);
	}

	double AngleDifference(double A, double B)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B));
	}
}

using namespace SimCruiseEnvironmentTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSolarSpaReferenceTest, "SimCruise.Environment.Solar.SpaReference", Flags)
bool FSimCruiseSolarSpaReferenceTest::RunTest(const FString&)
{
	// NREL SPA paper (Reda & Andreas 2008, table A5.1): 2003-10-17 12:30:30 MST (UTC-7),
	// 39.742476 N 105.1786 W: topocentric zenith 50.11162, azimuth 194.34024 (with refraction).
	const FSimCruiseSkyPosition Sun = FSimCruiseSolarPosition::Sun(39.742476, -105.1786, Utc(2003, 10, 17, 19, 30, 30));
	TestTrue(FString::Printf(TEXT("zenith %.5f vs 50.11162"), 90.0 - Sun.ApparentElevationDeg), FMath::Abs((90.0 - Sun.ApparentElevationDeg) - 50.11162) < 0.03);
	TestTrue(FString::Printf(TEXT("azimuth %.5f vs 194.34024"), Sun.AzimuthDeg), AngleDifference(Sun.AzimuthDeg, 194.34024) < 0.03);
	// SPA: equation of time 14.641503 minutes, declination -9.31434.
	TestTrue(FString::Printf(TEXT("equation of time %.3f vs 14.6415"), Sun.EquationOfTimeMinutes), FMath::Abs(Sun.EquationOfTimeMinutes - 14.641503) < 0.05);
	TestTrue(FString::Printf(TEXT("declination %.4f vs -9.31434"), Sun.DeclinationDeg), FMath::Abs(Sun.DeclinationDeg - -9.31434) < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSolarGodotAgreementTest, "SimCruise.Environment.Solar.GodotAgreement", Flags)
bool FSimCruiseSolarGodotAgreementTest::RunTest(const FString&)
{
	// The Godot client's taste test: Panoramax 4b6d216b, Lummus Park, 2025-04-24 14:34:52 UTC,
	// sun azimuth 101, elevation 49 (lab/taste/2026-11-10/README.md).
	const FDateTime When = Utc(2025, 4, 24, 14, 34, 52);
	const FSimCruiseSkyPosition Noaa = FSimCruiseSolarPosition::Sun(25.7770425, -80.1309641, When);
	const FSimCruiseSkyPosition Godot = FSimCruiseSolarPosition::SunFractionalYear(25.7770425, -80.1309641, When);
	TestTrue(FString::Printf(TEXT("azimuth %.2f near 101"), Noaa.AzimuthDeg), AngleDifference(Noaa.AzimuthDeg, 101.0) < 1.0);
	TestTrue(FString::Printf(TEXT("elevation %.2f near 49"), Noaa.TrueElevationDeg), FMath::Abs(Noaa.TrueElevationDeg - 49.0) < 1.0);
	AddInfo(FString::Printf(TEXT("NOAA az %.3f el %.3f decl %.4f eot %.3f ha %.3f | Godot az %.3f el %.3f decl %.4f eot %.3f ha %.3f"),
		Noaa.AzimuthDeg, Noaa.TrueElevationDeg, Noaa.DeclinationDeg, Noaa.EquationOfTimeMinutes, Noaa.HourAngleDeg,
		Godot.AzimuthDeg, Godot.TrueElevationDeg, Godot.DeclinationDeg, Godot.EquationOfTimeMinutes, Godot.HourAngleDeg));
	// Spencer's Fourier declination (the Godot formula) runs 0.45 degree low on this date
	// (12.63 against 13.08; the almanac gives about 13.07), so the two agree to under a degree.
	TestTrue(TEXT("Godot fractional-year sun agrees within 0.75 degree (azimuth)"), AngleDifference(Noaa.AzimuthDeg, Godot.AzimuthDeg) < 0.75);
	TestTrue(TEXT("Godot fractional-year sun agrees within 0.75 degree (elevation)"), FMath::Abs(Noaa.TrueElevationDeg - Godot.TrueElevationDeg) < 0.75);
	TestTrue(TEXT("NOAA declination matches the almanac (13.07)"), FMath::Abs(Noaa.DeclinationDeg - 13.07) < 0.05);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSolarCalendarTest, "SimCruise.Environment.Solar.Calendar", Flags)
bool FSimCruiseSolarCalendarTest::RunTest(const FString&)
{
	// June solstice 2026 (21 June 08:24 UTC): declination at its maximum, 23.44 degrees.
	const FSimCruiseSkyPosition Solstice = FSimCruiseSolarPosition::Sun(0.0, 0.0, Utc(2026, 6, 21, 8, 24, 0));
	TestTrue(FString::Printf(TEXT("solstice declination %.3f"), Solstice.DeclinationDeg), FMath::Abs(Solstice.DeclinationDeg - 23.436) < 0.02);

	// At local solar noon on the Tropic of Cancer the sun is overhead.
	const FDateTime Noon = FSimCruiseSolarPosition::SolarNoonUtc(-80.13, Utc(2026, 6, 21, 0, 0, 0));
	const FSimCruiseSkyPosition Overhead = FSimCruiseSolarPosition::Sun(23.436, -80.13, Noon);
	TestTrue(FString::Printf(TEXT("noon elevation on the tropic %.3f"), Overhead.TrueElevationDeg), Overhead.TrueElevationDeg > 89.8);

	// Equation of time: about +16.4 min on 3 November and -14.2 min on 11 February.
	const double November = FSimCruiseSolarPosition::Sun(0.0, 0.0, Utc(2026, 11, 3, 12, 0, 0)).EquationOfTimeMinutes;
	const double February = FSimCruiseSolarPosition::Sun(0.0, 0.0, Utc(2026, 2, 11, 12, 0, 0)).EquationOfTimeMinutes;
	TestTrue(FString::Printf(TEXT("equation of time 3 Nov %.2f"), November), FMath::Abs(November - 16.4) < 0.3);
	TestTrue(FString::Printf(TEXT("equation of time 11 Feb %.2f"), February), FMath::Abs(February - -14.2) < 0.3);

	// Solar noon is the highest sun of the day.
	const FDateTime MiamiNoon = FSimCruiseSolarPosition::SolarNoonUtc(-80.13, Utc(2025, 4, 24, 0, 0, 0));
	const double AtNoon = FSimCruiseSolarPosition::Sun(25.78, -80.13, MiamiNoon).TrueElevationDeg;
	TestTrue(TEXT("Miami solar noon near 17:19 UTC"), FMath::Abs((MiamiNoon.GetTimeOfDay().GetTotalMinutes()) - (17 * 60 + 19)) < 3.0);
	TestTrue(TEXT("elevation peaks at solar noon"),
		AtNoon > FSimCruiseSolarPosition::Sun(25.78, -80.13, MiamiNoon - FTimespan::FromMinutes(10)).TrueElevationDeg &&
		AtNoon > FSimCruiseSolarPosition::Sun(25.78, -80.13, MiamiNoon + FTimespan::FromMinutes(10)).TrueElevationDeg);
	TestTrue(FString::Printf(TEXT("Miami April noon elevation %.2f"), AtNoon), FMath::Abs(AtNoon - (90.0 - 25.78 + 13.0)) < 0.6);

	// 2 a.m. in Miami: the sun is far below the horizon, north of the meridian.
	const FSimCruiseSkyPosition Night = FSimCruiseSolarPosition::Sun(25.78, -80.13, Utc(2025, 4, 24, 6, 0, 0));
	TestTrue(FString::Printf(TEXT("night elevation %.1f"), Night.TrueElevationDeg), Night.TrueElevationDeg < -40.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSolarRefractionTest, "SimCruise.Environment.Solar.Refraction", Flags)
bool FSimCruiseSolarRefractionTest::RunTest(const FString&)
{
	// About 34 arcminutes at the horizon, under 1 arcminute above 45 degrees, none overhead.
	TestTrue(TEXT("horizon refraction"), FMath::Abs(FSimCruiseSolarPosition::RefractionDeg(0.0) - 0.482) < 0.02);
	TestTrue(TEXT("45 degree refraction"), FSimCruiseSolarPosition::RefractionDeg(45.0) < 1.0 / 60.0 + 1e-4);
	TestEqual(TEXT("zenith refraction"), FSimCruiseSolarPosition::RefractionDeg(89.0), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSolarWorldFrameTest, "SimCruise.Environment.Solar.WorldFrame", Flags)
bool FSimCruiseSolarWorldFrameTest::RunTest(const FString&)
{
	// +X east, +Y south, +Z up.
	TestTrue(TEXT("east"), FSimCruiseSolarPosition::ToWorldDirection(90.0, 0.0).Equals(FVector(1, 0, 0), 1e-6));
	TestTrue(TEXT("north"), FSimCruiseSolarPosition::ToWorldDirection(0.0, 0.0).Equals(FVector(0, -1, 0), 1e-6));
	TestTrue(TEXT("south"), FSimCruiseSolarPosition::ToWorldDirection(180.0, 0.0).Equals(FVector(0, 1, 0), 1e-6));
	TestTrue(TEXT("zenith"), FSimCruiseSolarPosition::ToWorldDirection(33.0, 90.0).Equals(FVector(0, 0, 1), 1e-6));
	TestTrue(TEXT("north yaw turns north toward east"), FSimCruiseSolarPosition::ToWorldDirection(0.0, 0.0, 90.0).Equals(FVector(1, 0, 0), 1e-6));

	double Azimuth = 0.0;
	double Elevation = 0.0;
	FSimCruiseSolarPosition::FromWorldDirection(FSimCruiseSolarPosition::ToWorldDirection(247.5, 18.25, 12.0), Azimuth, Elevation, 12.0);
	TestTrue(TEXT("round trip azimuth"), FMath::Abs(Azimuth - 247.5) < 1e-6);
	TestTrue(TEXT("round trip elevation"), FMath::Abs(Elevation - 18.25) < 1e-6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseMoonMeeusTest, "SimCruise.Environment.Moon.Meeus", Flags)
bool FSimCruiseMoonMeeusTest::RunTest(const FString&)
{
	// Meeus example 47.a: 1992 April 12, 0h TD: apparent RA 134.688470, Dec 13.768368, 368409.7 km.
	// Example 48.a: illuminated fraction 0.6786.
	const FSimCruiseSkyPosition Moon = FSimCruiseSolarPosition::Moon(0.0, 0.0, Utc(1992, 4, 12, 0, 0, 0));
	TestTrue(FString::Printf(TEXT("right ascension %.4f"), Moon.RightAscensionDeg), AngleDifference(Moon.RightAscensionDeg, 134.688470) < 0.1);
	TestTrue(FString::Printf(TEXT("declination %.4f"), Moon.DeclinationDeg), FMath::Abs(Moon.DeclinationDeg - 13.768368) < 0.05);
	TestTrue(FString::Printf(TEXT("distance %.1f km"), Moon.Distance), FMath::Abs(Moon.Distance - 368409.7) < 100.0);
	TestTrue(FString::Printf(TEXT("illuminated fraction %.4f"), Moon.IlluminatedFraction), FMath::Abs(Moon.IlluminatedFraction - 0.6786) < 0.01);

	// Full moon of 13 March 2025 (06:55 UTC): nearly fully lit.
	const FSimCruiseSkyPosition Full = FSimCruiseSolarPosition::Moon(25.78, -80.13, Utc(2025, 3, 14, 6, 55, 0));
	TestTrue(FString::Printf(TEXT("full moon fraction %.3f"), Full.IlluminatedFraction), Full.IlluminatedFraction > 0.98);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseDaylightIlluminanceTest, "SimCruise.Environment.Daylight.Illuminance", Flags)
bool FSimCruiseDaylightIlluminanceTest::RunTest(const FString&)
{
	TestTrue(TEXT("extraterrestrial sun about 128 klx"), FMath::Abs(FSimCruiseDaylightModel::ExtraterrestrialSunLux() - 127800.0) < 500.0);
	TestTrue(TEXT("perihelion is brighter"), FSimCruiseDaylightModel::ExtraterrestrialSunLux(0.983) > FSimCruiseDaylightModel::ExtraterrestrialSunLux(1.017));

	// Clear tropical noon: about 100-120 klx on the ground; "sunny 16" is EV100 15.
	const double Noon = FSimCruiseDaylightModel::HorizontalIlluminanceLux(77.0, 0.0);
	TestTrue(FString::Printf(TEXT("clear noon %.0f lx"), Noon), Noon > 95000.0 && Noon < 125000.0);
	const double NoonEV = FSimCruiseDaylightModel::ReferenceEV100(77.0, 0.0);
	TestTrue(FString::Printf(TEXT("clear noon EV100 %.2f"), NoonEV), NoonEV > 15.0 && NoonEV < 16.0);

	// Overcast noon is about two stops darker; a 20 degree sun about one.
	const double Overcast = FSimCruiseDaylightModel::ReferenceEV100(77.0, 1.0);
	TestTrue(FString::Printf(TEXT("overcast noon EV100 %.2f"), Overcast), Overcast > 12.5 && Overcast < 14.5);
	const double Low = FSimCruiseDaylightModel::ReferenceEV100(20.0, 0.0);
	TestTrue(FString::Printf(TEXT("late afternoon EV100 %.2f"), Low), Low > 13.5 && Low < 15.0);

	// Twilight falls monotonically; civil dusk (-6) is a few lux; full moonlight a quarter lux.
	double Previous = FSimCruiseDaylightModel::HorizontalIlluminanceLux(10.0, 0.0);
	for (double Elevation = 9.0; Elevation >= -18.0; Elevation -= 1.0)
	{
		const double Now = FSimCruiseDaylightModel::HorizontalIlluminanceLux(Elevation, 0.0);
		TestTrue(FString::Printf(TEXT("twilight falls at %.0f"), Elevation), Now <= Previous);
		Previous = Now;
	}
	const double Dusk = FSimCruiseDaylightModel::HorizontalIlluminanceLux(-6.0, 0.0);
	TestTrue(FString::Printf(TEXT("civil dusk %.2f lx"), Dusk), Dusk > 1.5 && Dusk < 8.0);
	const double FullMoon = FSimCruiseDaylightModel::MoonIlluminanceLux(0.0, 384400.0);
	TestTrue(FString::Printf(TEXT("full moon %.3f lx"), FullMoon), FullMoon > 0.2 && FullMoon < 0.4);
	const double Quarter = FSimCruiseDaylightModel::MoonIlluminanceLux(90.0, 384400.0);
	TestTrue(FString::Printf(TEXT("quarter moon %.3f lx is under a tenth of full"), Quarter), Quarter < 0.1 * FullMoon * 1.2);
	const double MoonNight = FSimCruiseDaylightModel::ReferenceEV100(-30.0, 0.0, FullMoon);
	TestTrue(FString::Printf(TEXT("full-moon night EV100 %.2f"), MoonNight), MoonNight > -4.0 && MoonNight < -2.0);

	// Daylight is exposed as metered; night only partly compensated (shown darker than day).
	TestEqual(TEXT("daylight display EV"), FSimCruiseDaylightModel::DisplayEV100(14.4), 14.4);
	TestTrue(TEXT("night is shown about 6 EV darker than day"), FMath::Abs((FSimCruiseDaylightModel::DisplayEV100(MoonNight) - MoonNight) - 6.0) < 0.5);
	TestTrue(TEXT("display EV is continuous at the knee"), FMath::Abs(FSimCruiseDaylightModel::DisplayEV100(9.999) - 10.0) < 0.01);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseDaylightVisibilityTest, "SimCruise.Environment.Daylight.Visibility", Flags)
bool FSimCruiseDaylightVisibilityTest::RunTest(const FString&)
{
	TestTrue(TEXT("Koschmieder 10 km"), FMath::Abs(FSimCruiseDaylightModel::ExtinctionPerKmForVisibility(10.0) - 0.3912) < 1e-9);
	TestTrue(TEXT("inverse"), FMath::Abs(FSimCruiseDaylightModel::VisibilityKmForExtinction(0.3912) - 10.0) < 1e-9);
	// Clean air alone sees far; haze adds aerosol on top of it.
	TestTrue(TEXT("aerosol for 28 km"), FMath::Abs(FSimCruiseDaylightModel::AerosolExtinctionPerKm(28.0) - (3.912 / 28.0 - 0.0116)) < 1e-9);
	TestTrue(TEXT("aerosol floor"), FSimCruiseDaylightModel::AerosolExtinctionPerKm(1000.0) >= 0.014);
	TestTrue(TEXT("EV of 1 cd/m2"), FMath::Abs(FSimCruiseDaylightModel::EV100FromLuminance(1.0) - 3.0) < 1e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseWeatherConditionsTest, "SimCruise.Environment.Weather.Conditions", Flags)
bool FSimCruiseWeatherConditionsTest::RunTest(const FString&)
{
	FSimCruiseWeatherConditions Out;
	FString Error;
	// The shape helper/conditions.mjs writes.
	const FString Json = TEXT("{\"clear_sky\":false,\"wet\":true,\"station\":\"KMIA\",\"at\":\"2026-11-10T18:53:00.000Z\",\"km\":13.2,\"cloud\":0.78,\"visibility_m\":4828,\"description\":\"broken clouds\",\"wx\":\"-RA BR\"}");
	TestTrue(TEXT("parses"), FSimCruiseWeather::ParseConditionsJson(Json, Out, Error));
	TestEqual(TEXT("cloud"), Out.CloudCover, 0.78f);
	TestEqual(TEXT("visibility"), Out.VisibilityM, 4828.0f);
	TestTrue(TEXT("rain"), Out.bRaining);
	TestFalse(TEXT("not snow"), Out.bSnowing);
	TestEqual(TEXT("station"), Out.Station, FString(TEXT("KMIA")));
	TestTrue(TEXT("observed"), Out.bObserved);

	FSimCruiseWeatherConditions Snow;
	TestTrue(TEXT("snow parses"), FSimCruiseWeather::ParseConditionsJson(TEXT("{\"cloud\":1,\"visibility_m\":16093.44,\"wet\":true,\"wx\":\"SN\"}"), Snow, Error));
	TestTrue(TEXT("snow is not rain"), Snow.bSnowing && !Snow.bRaining);
	TestTrue(TEXT("a 10SM report is not taken as measured haze"), Snow.VisibilityM >= 40000.0f);

	FSimCruiseWeatherConditions Empty;
	TestTrue(TEXT("no station"), FSimCruiseWeather::ParseConditionsJson(TEXT("{}"), Empty, Error));
	TestFalse(TEXT("default sky is not observed"), Empty.bObserved);
	TestEqual(TEXT("default cloud"), Empty.CloudCover, 0.12f);

	FSimCruiseWeatherConditions Bad;
	TestFalse(TEXT("cloud out of range"), FSimCruiseWeather::ParseConditionsJson(TEXT("{\"cloud\":3}"), Bad, Error));
	TestFalse(TEXT("not json"), FSimCruiseWeather::ParseConditionsJson(TEXT("rain"), Bad, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseWeatherWetnessTest, "SimCruise.Environment.Weather.RoadWetness", Flags)
bool FSimCruiseWeatherWetnessTest::RunTest(const FString&)
{
	const float AfterRain = FSimCruiseWeather::StepRoadWetness(0.0f, true, 180.0f);
	TestTrue(FString::Printf(TEXT("three minutes of rain %.3f"), AfterRain), FMath::Abs(AfterRain - (1.0f - FMath::Exp(-1.0f))) < 1e-4f);
	const float Drying = FSimCruiseWeather::StepRoadWetness(1.0f, false, 900.0f);
	TestTrue(FString::Printf(TEXT("fifteen minutes dry %.3f"), Drying), FMath::Abs(Drying - FMath::Exp(-1.0f)) < 1e-4f);
	// Stepping in small pieces gives the same result as one big step.
	float Stepped = 0.0f;
	for (int32 Index = 0; Index < 1800; ++Index)
	{
		Stepped = FSimCruiseWeather::StepRoadWetness(Stepped, true, 0.1f);
	}
	TestTrue(TEXT("frame-rate independent"), FMath::Abs(Stepped - AfterRain) < 1e-3f);
	TestTrue(TEXT("coverage grows with cloud"), FSimCruiseWeather::CloudMaterialCoverage(0.78f) > FSimCruiseWeather::CloudMaterialCoverage(0.19f));
	TestEqual(TEXT("clear sky coverage"), FSimCruiseWeather::CloudMaterialCoverage(0.0f), -0.5f);
	TestEqual(TEXT("overcast coverage"), FSimCruiseWeather::CloudMaterialCoverage(1.0f), 0.35f);
	TestTrue(TEXT("scattered between few and broken"), FMath::IsNearlyEqual(FSimCruiseWeather::CloudMaterialCoverage(0.44f), -0.12f, 1e-5f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseGraphicsAutoPickTest, "SimCruise.Environment.Graphics.AutoPick", Flags)
bool FSimCruiseGraphicsAutoPickTest::RunTest(const FString&)
{
	using P = ESimCruiseGraphicsPreset;
	TestEqual(TEXT("software adapter"), FSimCruiseGraphics::AutoPick(TEXT("Microsoft Basic Render Driver"), false, 0), P::Low);
	TestEqual(TEXT("integrated Intel"), FSimCruiseGraphics::AutoPick(TEXT("Intel(R) UHD Graphics"), true, 128), P::Low);
	TestEqual(TEXT("desktop RTX 4080"), FSimCruiseGraphics::AutoPick(TEXT("NVIDIA GeForce RTX 4080"), false, 16376), P::Ultra);
	TestEqual(TEXT("laptop RTX 4080 one tier down"), FSimCruiseGraphics::AutoPick(TEXT("NVIDIA GeForce RTX 4080 Laptop GPU"), false, 12282), P::High);
	TestEqual(TEXT("desktop RTX 2070 Super"), FSimCruiseGraphics::AutoPick(TEXT("NVIDIA GeForce RTX 2070 SUPER"), false, 8192), P::Ultra);
	TestEqual(TEXT("the owner's RTX 2070 Super Max-Q (High misses 60 fps on Ocean Drive)"), FSimCruiseGraphics::AutoPick(TEXT("NVIDIA GeForce RTX 2070 Super with Max-Q Design"), false, 8031), P::Medium);
	TestEqual(TEXT("laptop RTX 2060"), FSimCruiseGraphics::AutoPick(TEXT("NVIDIA GeForce RTX 2060 Laptop GPU"), false, 6144), P::Medium);
	TestEqual(TEXT("6 GB desktop cards step down from Ultra"), FSimCruiseGraphics::AutoPick(TEXT("NVIDIA GeForce RTX 3060"), false, 6144), P::High);
	TestEqual(TEXT("unknown adapter without benchmark"), FSimCruiseGraphics::AutoPick(TEXT("Some Future GPU"), false, 16000, false), P::Medium);
	TestEqual(TEXT("4 GB cards stay off High"), FSimCruiseGraphics::AutoPick(TEXT("NVIDIA GeForce RTX 3070"), false, 4096), P::Medium);

	ESimCruiseGraphicsPreset Parsed = P::Low;
	for (uint8 Index = 0; Index <= static_cast<uint8>(P::Ultra); ++Index)
	{
		const P Preset = static_cast<P>(Index);
		TestTrue(TEXT("parse round trip"), FSimCruiseGraphics::Parse(FSimCruiseGraphics::Name(Preset).ToUpper(), Parsed) && Parsed == Preset);
		TestTrue(TEXT("presets get sharper"), Index == 0 || FSimCruiseGraphics::Spec(Preset).ScreenPercentage > FSimCruiseGraphics::Spec(static_cast<P>(Index - 1)).ScreenPercentage);
	}
	TestFalse(TEXT("unknown preset"), FSimCruiseGraphics::Parse(TEXT("insane"), Parsed));
	TestEqual(TEXT("percentile"), FSimCruisePerfStats::Percentile({4.0, 1.0, 3.0, 2.0}, 50.0), 2.0);
	TestEqual(TEXT("p95 of 20"), FSimCruisePerfStats::Percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}, 95.0), 19.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSkyActorTest, "SimCruise.Environment.Sky.Actor", Flags)
bool FSimCruiseSkyActorTest::RunTest(const FString&)
{
	// A throwaway game world with a level sun of its own, as a streamed map would bring.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SimCruiseSkyActorTest"));
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);

	ADirectionalLight* LevelSun = World->SpawnActor<ADirectionalLight>();
	ASimCruiseEnvironment* Env = World->SpawnActor<ASimCruiseEnvironment>();
	if (TestNotNull(TEXT("level sun"), LevelSun) && TestNotNull(TEXT("environment"), Env))
	{
		// Clear noon on the taste-test day at Lummus Park.
		Env->SetPlace(25.7770425, -80.1309641, 2.0);
		Env->SetUtcTime(FDateTime(2025, 4, 24, 17, 19, 0));
		const FSimCruiseSkyPosition Expected = FSimCruiseSolarPosition::Sun(25.7770425, -80.1309641, FDateTime(2025, 4, 24, 17, 19, 0));
		const FVector ToSun = FSimCruiseSolarPosition::ToWorldDirection(Expected.AzimuthDeg, Expected.ApparentElevationDeg);
		TestTrue(TEXT("sun direction follows the ephemeris"), Env->GetSunDirection().Equals(ToSun, 1e-6));
		// A directional light shines along its forward axis, away from the sun.
		TestTrue(TEXT("sun light shines from the sun"), Env->Sun->GetForwardVector().Equals(-ToSun, 1e-4));
		TestTrue(FString::Printf(TEXT("sun light is extraterrestrial lux (%.0f)"), Env->Sun->Intensity), FMath::Abs(Env->Sun->Intensity - FSimCruiseDaylightModel::ExtraterrestrialSunLux(Expected.Distance)) < 1.0);
		TestTrue(TEXT("sun casts shadows at noon"), Env->Sun->CastShadows);
		TestFalse(TEXT("moon casts none while the sun is up"), Env->MoonLight->CastShadows);
		TestTrue(FString::Printf(TEXT("noon reference EV100 %.2f"), Env->GetReferenceEV100()), Env->GetReferenceEV100() > 15.0f && Env->GetReferenceEV100() < 16.0f);

		// The level's own sun is turned off, the environment's stays on; a level-scoped pass does the same.
		Env->TakeOverLevelLighting(World->PersistentLevel);
		TestFalse(TEXT("level sun turned off"), LevelSun->GetLightComponent()->IsVisible());
		TestTrue(TEXT("environment sun kept"), Env->Sun->IsVisible());

		// Night: the sun is switched off below -18 degrees; the moon takes the shadows when up.
		Env->SetUtcTime(FDateTime(2025, 5, 13, 3, 0, 0));
		TestFalse(TEXT("sun off at night"), Env->Sun->IsVisible());
		TestFalse(TEXT("sun casts no shadows at night"), Env->Sun->CastShadows);
		TestTrue(TEXT("full moon up casts the shadows"), Env->MoonLight->CastShadows && Env->MoonLight->IsVisible());
		TestTrue(FString::Printf(TEXT("moonlight %.3f lx"), Env->MoonLight->Intensity), Env->MoonLight->Intensity > 0.2f && Env->MoonLight->Intensity < 0.4f);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
