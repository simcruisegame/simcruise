// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseDaylightModel.h"
#include "Knowledge/SimCruisePlacePhotos.h"
#include "Live/SimCruiseLiveData.h"
#include "Live/SimCruiseSceneParser.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseLiveTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	// Recorded 2026-11-11 22:3x UTC for Ocean Drive, Miami Beach (25.78, -80.13).
	const TCHAR* WeatherKmh = TEXT(R"({"latitude":25.814812,"longitude":-80.12985,"utc_offset_seconds":-18000,"timezone":"America/New_York",
		"current_units":{"time":"iso8601","wind_speed_10m":"km/h"},
		"current":{"time":"2026-11-11T17:30","interval":900,"temperature_2m":26.6,"relative_humidity_2m":72,"precipitation":0.00,"rain":0.00,
		"weather_code":2,"cloud_cover":53,"cloud_cover_low":57,"cloud_cover_mid":19,"cloud_cover_high":0,"visibility":22800.00,
		"wind_speed_10m":18.7,"wind_direction_10m":168,"wind_gusts_10m":23.8,"is_day":1,"surface_pressure":1013.9}})");

	const TCHAR* Air = TEXT(R"({"latitude":25.8,"longitude":-80.1,"utc_offset_seconds":0,"current":{"time":"2026-11-11T22:00","interval":3600,
		"pm2_5":6.3,"pm10":10.4,"aerosol_optical_depth":0.09,"dust":0.0,"us_aqi":41,"uv_index":0.10}})");

	const TCHAR* Marine = TEXT(R"({"latitude":25.791664,"longitude":-80.12499,"utc_offset_seconds":0,"current":{"time":"2026-11-11T22:30","interval":900,
		"wave_height":0.34,"wave_direction":114,"wave_period":4.45,"sea_surface_temperature":27.9,"sea_level_height_msl":-0.20}})");

	const TCHAR* MarineInland = TEXT(R"({"latitude":39.7,"longitude":-104.9,"utc_offset_seconds":0,"current":{"time":"2026-11-11T22:30","interval":900,
		"wave_height":null,"wave_direction":null}})");

	const TCHAR* Tide = TEXT(R"({"metadata":{"id":"8723214","name":"Virginia Key","lat":"25.7314","lon":"-80.1618"},
		"data":[{"t":"2026-11-11 22:30", "v":"0.532", "s":"0.008", "f":"0,0,0,0", "q":"p"}]})");

	const TCHAR* TideError = TEXT(R"({"error":{"message":"No data was found. This product may not be offered at this station at the requested time."}})");

	const TCHAR* Aircraft = TEXT(R"({"ac":[
		{"hex":"782144","type":"adsb_icao","flight":"CSG2569 ","r":"B-226C","t":"B77L","alt_baro":3625,"alt_geom":3775,"gs":230.8,"track":57.21,
		 "baro_rate":-960,"category":"A5","lat":25.728195,"lon":-80.569051,"seen_pos":0.168},
		{"hex":"a841f5","flight":"FDX251  ","t":"MD11","alt_baro":34975,"gs":535.4,"track":119.05,"geom_rate":-32,"category":"A5",
		 "lat":25.937836,"lon":-80.553237,"seen_pos":0.0},
		{"hex":"a1b2c3","flight":"TAXI1","alt_baro":"ground","gs":12,"track":90,"lat":25.79,"lon":-80.29,"seen_pos":1.0},
		{"hex":"ffffff","flight":"STALE","alt_baro":5000,"gs":150,"track":0,"lat":25.7,"lon":-80.2,"seen_pos":120.0},
		{"hex":"000001","flight":"NOPOS","alt_baro":5000}
	]})");

	const TCHAR* Metars = TEXT(R"([
		{"icaoId":"KMIA","obsTime":1794436260,"lat":25.788,"lon":-80.317,"visib":"10+","cover":"SCT","wxString":null,"rawOb":"KMIA 112251Z 15009KT 10SM SCT025 27/21 A2994"},
		{"icaoId":"KFLL","obsTime":1794436260,"lat":26.072,"lon":-80.15,"visib":3,"cover":"BKN","wxString":"-RA BR"},
		{"icaoId":"KTMB","obsTime":1794436260,"lat":25.648,"lon":-80.433,"visib":"10+","cover":"XYZ"}
	])");
}

using namespace SimCruiseLiveTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLiveWeatherTest, "SimCruise.Live.Weather", Flags)
bool FSimCruiseLiveWeatherTest::RunTest(const FString&)
{
	FSimCruiseLiveWeather W;
	FString Error;
	TestTrue(TEXT("parses"), FSimCruiseLiveData::ParseWeather(WeatherKmh, W, Error));
	TestEqual(TEXT("local 17:30 at UTC-5 is 22:30 UTC"), W.TimeUtc, FDateTime(2026, 11, 11, 22, 30, 0));
	TestNearlyEqual(TEXT("cloud 53 % -> 0.53"), W.CloudCover, 0.53f, 1.0e-4f);
	TestNearlyEqual(TEXT("km/h converted: 18.7 km/h = 5.19 m/s"), W.WindSpeedMs, 18.7f / 3.6f, 1.0e-3f);
	TestEqual(TEXT("wind from SSE"), FSimCruiseLiveData::Compass(W.WindFromDeg), FString(TEXT("SSE")));
	TestEqual(TEXT("WMO 2"), FSimCruiseLiveData::DescribeWmo(W.WmoCode), FString(TEXT("partly cloudy")));
	TestFalse(TEXT("a missing current block fails"), FSimCruiseLiveData::ParseWeather(TEXT("{\"error\":true,\"reason\":\"Latitude must be in range\"}"), W, Error));
	TestEqual(TEXT("the API's reason is the error"), Error, FString(TEXT("Latitude must be in range")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLiveFuseTest, "SimCruise.Live.Fuse", Flags)
bool FSimCruiseLiveFuseTest::RunTest(const FString&)
{
	FSimCruiseLiveWeather W;
	FSimCruiseLiveAir A;
	FString Error;
	FSimCruiseLiveData::ParseWeather(WeatherKmh, W, Error);
	TestTrue(TEXT("air parses"), FSimCruiseLiveData::ParseAir(Air, A, Error));

	const FSimCruiseWeatherConditions ModelOnly = FSimCruiseLiveData::Fuse(&W, nullptr, &A, FSimCruiseWeatherConditions());
	TestNearlyEqual(TEXT("model cloud"), ModelOnly.CloudCover, 0.53f, 1.0e-4f);
	TestTrue(TEXT("the model's 22.8 km clear-air cap is not haze"), ModelOnly.VisibilityM >= 40000.0f);
	TestFalse(TEXT("no rain"), ModelOnly.bRaining);
	TestNearlyEqual(TEXT("AOD carried"), ModelOnly.AerosolOpticalDepth, 0.09f, 1.0e-4f);
	TestTrue(TEXT("observed"), ModelOnly.bObserved);

	TArray<FSimCruiseMetar> Rows;
	TestTrue(TEXT("METARs parse"), FSimCruiseLiveData::ParseMetars(Metars, Rows, Error));
	TestEqual(TEXT("an unknown cover code is skipped"), Rows.Num(), 2);
	const FDateTime Now = FDateTime::FromUnixTimestamp(1794436260 + 600);
	// Ocean Drive: KMIA is 18.7 km west, KFLL 32 km north. KMIA is nearer.
	const FSimCruiseMetar* Near = FSimCruiseLiveData::NearestMetar(Rows, 25.78, -80.13, Now);
	TestTrue(TEXT("a station within 40 km"), Near != nullptr);
	if (Near)
	{
		TestEqual(TEXT("KMIA is nearest"), Near->Station, FString(TEXT("KMIA")));
		TestTrue(TEXT("10+ statute miles"), Near->VisibilityM > 16000.0f);
		const FSimCruiseWeatherConditions Fused = FSimCruiseLiveData::Fuse(&W, Near, &A, FSimCruiseWeatherConditions());
		TestNearlyEqual(TEXT("the observer's SCT wins over the model"), Fused.CloudCover, 0.44f, 1.0e-4f);
		TestEqual(TEXT("station named"), Fused.Station, FString(TEXT("KMIA")));
	}
	TestTrue(TEXT("a report two hours old is too old"), FSimCruiseLiveData::NearestMetar(Rows, 25.78, -80.13, Now + FTimespan::FromHours(2.0)) == nullptr);

	// Fort Lauderdale beach: KFLL reports light rain and mist.
	const FSimCruiseMetar* Fll = FSimCruiseLiveData::NearestMetar(Rows, 26.10, -80.10, Now);
	TestTrue(TEXT("KFLL near Fort Lauderdale"), Fll && Fll->Station == TEXT("KFLL"));
	if (Fll)
	{
		const FSimCruiseWeatherConditions Wet = FSimCruiseLiveData::Fuse(&W, Fll, &A, FSimCruiseWeatherConditions());
		TestTrue(TEXT("-RA is rain"), Wet.bRaining);
		TestNearlyEqual(TEXT("3 SM visibility"), Wet.VisibilityM, 3.0f * 1609.344f, 1.0f);
	}

	// WMO codes.
	TestTrue(TEXT("61 rain"), FSimCruiseLiveData::IsRainCode(61));
	TestTrue(TEXT("95 thunderstorm wets"), FSimCruiseLiveData::IsRainCode(95));
	TestTrue(TEXT("73 snow"), FSimCruiseLiveData::IsSnowCode(73));
	TestFalse(TEXT("3 overcast is dry"), FSimCruiseLiveData::IsRainCode(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLiveSeaTest, "SimCruise.Live.Sea", Flags)
bool FSimCruiseLiveSeaTest::RunTest(const FString&)
{
	FSimCruiseLiveSea Sea;
	FString Error;
	TestTrue(TEXT("marine parses"), FSimCruiseLiveData::ParseMarine(Marine, Sea, Error));
	TestNearlyEqual(TEXT("waves"), Sea.WaveHeightM, 0.34f, 1.0e-4f);
	TestNearlyEqual(TEXT("sea level"), Sea.SeaLevelM, -0.20f, 1.0e-4f);
	FSimCruiseLiveSea Inland;
	TestFalse(TEXT("inland nulls are no sea"), FSimCruiseLiveData::ParseMarine(MarineInland, Inland, Error));

	float Level = 0.0f;
	FDateTime When;
	TestTrue(TEXT("tide parses"), FSimCruiseLiveData::ParseTide(Tide, Level, When, Error));
	TestNearlyEqual(TEXT("water level"), Level, 0.532f, 1.0e-4f);
	TestEqual(TEXT("time"), When, FDateTime(2026, 11, 11, 22, 30, 0));
	TestFalse(TEXT("NOAA error"), FSimCruiseLiveData::ParseTide(TideError, Level, When, Error));
	TestTrue(TEXT("NOAA message kept"), Error.StartsWith(TEXT("No data")));

	TArray<FSimCruiseTideStation> Stations = {{TEXT("8723214"), TEXT("Virginia Key"), 25.7314, -80.1618}, {TEXT("8724580"), TEXT("Key West"), 24.5508, -81.8081}};
	const FSimCruiseTideStation* Near = FSimCruiseLiveData::NearestTideStation(Stations, 25.78, -80.13);
	TestTrue(TEXT("Virginia Key within 30 km of Ocean Drive"), Near && Near->Id == TEXT("8723214"));
	TestTrue(TEXT("no gauge near Denver"), FSimCruiseLiveData::NearestTideStation(Stations, 39.74, -104.99) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLiveAircraftTest, "SimCruise.Live.Aircraft", Flags)
bool FSimCruiseLiveAircraftTest::RunTest(const FString&)
{
	TArray<FSimCruiseLiveAircraft> List;
	FString Error;
	TestTrue(TEXT("parses"), FSimCruiseLiveData::ParseAircraft(Aircraft, List, Error));
	TestEqual(TEXT("stale and position-less rows dropped, the ground one kept"), List.Num(), 3);
	if (List.Num() < 3)
	{
		return true;
	}
	TestEqual(TEXT("callsign trimmed"), List[0].Flight, FString(TEXT("CSG2569")));
	TestNearlyEqual(TEXT("GNSS height preferred: 3775 ft"), List[0].AltitudeM, 3775.0f * 0.3048f, 0.1f);
	TestNearlyEqual(TEXT("knots to m/s"), List[0].GroundSpeedMs, 230.8f * 0.514444f, 0.01f);
	TestNearlyEqual(TEXT("descending 960 ft/min"), List[0].VerticalRateMs, -960.0f * 0.3048f / 60.0f, 0.01f);
	TestNearlyEqual(TEXT("barometric when no GNSS"), List[1].AltitudeM, 34975.0f * 0.3048f, 0.1f);
	TestTrue(TEXT("ground"), List[2].bOnGround);

	// Dead reckoning: 100 s due east at 100 m/s at the equator is 10 km = 0.0899 degrees.
	FSimCruiseLiveAircraft East;
	East.Lat = 0.0; East.Lon = 0.0; East.TrackDeg = 90.0f; East.GroundSpeedMs = 100.0f; East.AltitudeM = 1000.0f; East.VerticalRateMs = 5.0f;
	double Lat = 0.0, Lon = 0.0;
	float Alt = 0.0f;
	FSimCruiseLiveData::Extrapolate(East, 100.0, Lat, Lon, Alt);
	TestNearlyEqual(TEXT("10 km east"), Lon, 10000.0 / 6371008.8 * 180.0 / PI, 1.0e-6);
	TestNearlyEqual(TEXT("no drift north"), Lat, 0.0, 1.0e-9);
	TestNearlyEqual(TEXT("climbed 500 m"), Alt, 1500.0f, 1.0e-3f);
	TestNearlyEqual(TEXT("distance agrees"), FSimCruiseLiveData::DistanceKm(0, 0, Lat, Lon), 10.0, 1.0e-6);

	// Curvature: 20 km away the ground is 27.3 m below the tangent plane (31.4 m geometric, 13 % refraction).
	TestNearlyEqual(TEXT("curvature drop at 20 km"), FSimCruiseLiveData::CurvatureDropM(20000.0), 27.3, 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLiveHazeTest, "SimCruise.Live.Haze", Flags)
bool FSimCruiseLiveHazeTest::RunTest(const FString&)
{
	// AOD 0.09 over a 0.7 km marine layer: 0.129 /km at the ground, a 30 km Koschmieder visibility
	// with Rayleigh added: a clean maritime day, as Miami's AQI 41 says.
	const double Sigma = FSimCruiseDaylightModel::AerosolForOpticalDepth(0.09, 0.7);
	TestNearlyEqual(TEXT("sigma = tau / H"), Sigma, 0.09 / 0.7, 1.0e-9);
	const double VisibilityKm = 3.912 / (Sigma + FSimCruiseDaylightModel::RayleighExtinctionPerKm550);
	TestTrue(FString::Printf(TEXT("visibility %.1f km between 20 and 40"), VisibilityKm), VisibilityKm > 20.0 && VisibilityKm < 40.0);
	TestNearlyEqual(TEXT("a clean floor"), FSimCruiseDaylightModel::AerosolForOpticalDepth(0.0, 0.7), 0.014, 1.0e-9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLiveTrafficTest, "SimCruise.Live.Traffic", Flags)
bool FSimCruiseLiveTrafficTest::RunTest(const FString&)
{
	// Recorded FHWA HPMS 2024 (Florida) answer near Ocean Drive: two counted sections.
	const TCHAR* Hpms = TEXT(R"({"features":[
		{"attributes":{"through_lanes":2,"speed_limit":30,"iri":null,"route_id":"87060000","routename":null,"aadt":13400,"aadt_single_unit":300,"aadt_combination":null,"f_system":3},
		 "geometry":{"paths":[[[-80.1310,25.7750],[-80.1300,25.7850]]]}},
		{"attributes":{"through_lanes":2,"speed_limit":null,"route_id":"87000465","aadt":5600,"f_system":5},
		 "geometry":{"paths":[[[-80.140943,25.781741],[-80.132545,25.781952]]]}},
		{"attributes":{"through_lanes":2,"route_id":"zero","aadt":0,"f_system":7},"geometry":{"paths":[[[-80.13,25.78],[-80.12,25.78]]]}}
	]})");
	TArray<FSimCruiseRoadSection> Roads;
	FString Error;
	TestTrue(TEXT("parses"), FSimCruiseLiveData::ParseRoads(Hpms, Roads, Error));
	TestEqual(TEXT("uncounted section dropped"), Roads.Num(), 2);
	double Metres = 0.0;
	const FSimCruiseRoadSection* Near = FSimCruiseLiveData::NearestRoad(Roads, 25.7800, -80.13045, 40.0, Metres);
	TestTrue(TEXT("the counted section under the car"), Near && Near->Aadt == 13400);
	TestTrue(TEXT("nothing counted 300 m out at sea"), FSimCruiseLiveData::NearestRoad(Roads, 25.7800, -80.1270, 40.0, Metres) == nullptr);

	// The hour curves sum to one day.
	float Weekday = 0.0f, Weekend = 0.0f;
	for (int32 H = 0; H < 24; ++H) { Weekday += FSimCruiseLiveData::HourShare(H, false); Weekend += FSimCruiseLiveData::HourShare(H, true); }
	TestNearlyEqual(TEXT("weekday sums to 1"), Weekday, 1.0f, 1.0e-4f);
	TestNearlyEqual(TEXT("weekend sums to 1"), Weekend, 1.0f, 1.0e-4f);
	TestTrue(TEXT("5 pm busier than 3 am"), FSimCruiseLiveData::HourShare(17, false) > 10.0f * FSimCruiseLiveData::HourShare(3, false));

	// Wednesday 11 Nov 2026, 17:00 local on 13 400 a day: about 1 000 an hour; 2 lanes at 30 mph.
	FSimCruiseRoadSection Ocean = Roads[0];
	const float Flow = FSimCruiseLiveData::FlowPerHour(Ocean, FDateTime(2026, 11, 11, 17, 0, 0), false, false);
	TestTrue(FString::Printf(TEXT("evening peak %.0f veh/h"), Flow), Flow > 900.0f && Flow < 1150.0f);
	const float Night = FSimCruiseLiveData::FlowPerHour(Ocean, FDateTime(2026, 11, 11, 3, 0, 0), false, false);
	TestTrue(FString::Printf(TEXT("3 am %.0f veh/h"), Night), Night < 100.0f);
	TestTrue(TEXT("rain thins the traffic"), FSimCruiseLiveData::FlowPerHour(Ocean, FDateTime(2026, 11, 11, 17, 0, 0), true, false) < Flow);
	const float Density = FSimCruiseLiveData::DensityPerKmLane(Ocean, Flow);
	TestNearlyEqual(TEXT("k = q / (lanes v)"), Density, Flow / (2.0f * 30.0f * 1.609344f), 1.0e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLivePhotosTest, "SimCruise.Live.Photos", Flags)
bool FSimCruiseLivePhotosTest::RunTest(const FString&)
{
	const TCHAR* Commons = TEXT(R"({"batchcomplete":true,"query":{"pages":[
		{"pageid":1,"title":"File:Breakwater Hotel dusk.jpg","coordinates":[{"lat":25.779981,"lon":-80.13049}],
		 "imageinfo":[{"width":4000,"height":3000,"thumburl":"https://upload.wikimedia.org/a.jpg","descriptionurl":"https://commons.wikimedia.org/wiki/File:A.jpg",
		 "extmetadata":{"LicenseShortName":{"value":"CC BY-SA 3.0"},"Artist":{"value":"<a href=\"x\">Jane Doe</a>"},"DateTimeOriginal":{"value":"2011-10-01"}}}]},
		{"pageid":2,"title":"File:Logo.png","imageinfo":[{"thumburl":"https://upload.wikimedia.org/b.png","extmetadata":{"LicenseShortName":{"value":"Fair use"}}}]}
	]}})");
	TArray<FSimCruisePlacePhoto> Photos;
	FString Error;
	TestTrue(TEXT("parses"), FSimCruiseLiveData::ParsePhotos(Commons, Photos, Error));
	TestEqual(TEXT("only the freely licensed photo"), Photos.Num(), 1);
	if (Photos.Num() == 1)
	{
		TestEqual(TEXT("artist without HTML"), Photos[0].Artist, FString(TEXT("Jane Doe")));
		TestEqual(TEXT("licence"), Photos[0].License, FString(TEXT("CC BY-SA 3.0")));
		TestNearlyEqual(TEXT("place"), Photos[0].Lat, 25.779981, 1.0e-9);
	}
	TestTrue(TEXT("no photos is fine"), FSimCruiseLiveData::ParsePhotos(TEXT("{\"batchcomplete\":true}"), Photos, Error) && Photos.IsEmpty());

	FString State;
	TestTrue(TEXT("Census state"), FSimCruiseLiveData::ParseState(TEXT(R"({"result":{"geographies":{"States":[{"STUSAB":"FL","NAME":"Florida"}]}}})"), State, Error) && State == TEXT("FL"));
	TestFalse(TEXT("outside the US"), FSimCruiseLiveData::ParseState(TEXT(R"({"result":{"geographies":{"States":[]}}})"), State, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLiveWallMaskTest, "SimCruise.Live.WallMask", Flags)
bool FSimCruiseLiveWallMaskTest::RunTest(const FString&)
{
	// A pink Deco wall on the left, a blue car in front of it, sky and a green awning: the colour
	// rule would see the awning and the car; the parser's labels keep only the wall.
	const int32 W = 320, H = 240, GW = 40, GH = 30;
	TArray<FColor> Px;
	Px.SetNum(W * H);
	TArray<uint8> Labels;
	Labels.SetNum(GW * GH);
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			FColor C(236, 170, 190); // the wall
			ESimCruiseSceneClass L = ESimCruiseSceneClass::Building;
			if (y < H / 5) { C = FColor(200, 215, 235); L = ESimCruiseSceneClass::Sky; }
			else if (y > H * 3 / 4) { C = FColor(90, 90, 95); L = ESimCruiseSceneClass::Terrain; }
			else if (y > H / 2 && x < W / 2) { C = FColor(30, 60, 170); L = ESimCruiseSceneClass::Car; }
			Px[y * W + x] = C;
			Labels[(y * GH / H) * GW + (x * GW / W)] = static_cast<uint8>(L);
		}
	}
	FColor Wall;
	float Share = 0.0f;
	bool bNormalised = false;
	TestTrue(TEXT("a wall is found"), SimCruisePlacePhotos::WallColourMasked(Px, W, H, Labels, GW, GH, Wall, Share, bNormalised));
	TestFalse(TEXT("no pavement here, so no albedo scaling"), bNormalised);
	TestTrue(FString::Printf(TEXT("the pink stucco, not the car (%d %d %d)"), Wall.R, Wall.G, Wall.B),
		FMath::Abs(Wall.R - 236) < 12 && FMath::Abs(Wall.G - 170) < 12 && FMath::Abs(Wall.B - 190) < 12);
	TestTrue(TEXT("one colour"), Share > 0.9f);

	// A bright grey sidewalk (sRGB 200, 58 % linear) in the same light: the photograph is
	// over-exposed for a 0.30 concrete, so the wall comes down by 0.30 / 0.58 (about 0.52):
	// (236, 170, 190) -> about (176, 126, 141).
	for (int32 y = H * 3 / 4 + 1; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			Px[y * W + x] = FColor(200, 200, 200);
			Labels[(y * GH / H) * GW + (x * GW / W)] = static_cast<uint8>(ESimCruiseSceneClass::Sidewalk);
		}
	}
	TestTrue(TEXT("still a wall"), SimCruisePlacePhotos::WallColourMasked(Px, W, H, Labels, GW, GH, Wall, Share, bNormalised, true));
	TestTrue(TEXT("normalised on the sidewalk"), bNormalised);
	TestTrue(FString::Printf(TEXT("albedo from the grey card (%d %d %d)"), Wall.R, Wall.G, Wall.B), FMath::Abs(Wall.R - 176) <= 6 && FMath::Abs(Wall.G - 126) <= 6 && FMath::Abs(Wall.B - 141) <= 6);

	// No building in the frame: nothing.
	for (uint8& L : Labels) { L = static_cast<uint8>(ESimCruiseSceneClass::Vegetation); }
	TestFalse(TEXT("no wall pixels, no colour"), SimCruisePlacePhotos::WallColourMasked(Px, W, H, Labels, GW, GH, Wall, Share, bNormalised));

	// Label lookup maps the photograph onto the grid.
	TestEqual(TEXT("top-left cell"), FSimCruiseSceneParser::ClassAt(Labels, GW, GH, 0, 0, W, H), ESimCruiseSceneClass::Vegetation);
	return true;
}

#endif
