// Copyright SimCruise contributors. All Rights Reserved.

#include "Live/SimCruiseLiveData.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SimCruiseLivePrivate
{
	constexpr double EarthRadiusM = 6371008.8;
	constexpr double FeetToMetres = 0.3048;
	constexpr double KnotsToMs = 0.514444;
	constexpr double KmhToMs = 1.0 / 3.6;

	bool ReadObject(const FString& Json, TSharedPtr<FJsonObject>& Out)
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.IsValid();
	}

	float Number(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float Default = 0.0f)
	{
		double Value = 0.0;
		return Object->TryGetNumberField(Field, Value) ? static_cast<float>(Value) : Default;
	}

	/** ISO 8601 without seconds ("2026-11-11T17:30"), which FDateTime::ParseIso8601 refuses. */
	bool ParseMinutes(FString Text, FDateTime& Out)
	{
		Text.ReplaceInline(TEXT(" "), TEXT("T"));
		if (Text.Len() == 16)
		{
			Text += TEXT(":00");
		}
		return FDateTime::ParseIso8601(*Text, Out);
	}

	/** Open-Meteo times are local ISO without a zone ("2026-11-11T17:30") plus utc_offset_seconds. */
	FDateTime OpenMeteoTime(const TSharedPtr<FJsonObject>& Root, const TSharedPtr<FJsonObject>& Current)
	{
		FString Text;
		FDateTime Local;
		if (!Current->TryGetStringField(TEXT("time"), Text) || !ParseMinutes(Text, Local))
		{
			return FDateTime();
		}
		double Offset = 0.0;
		Root->TryGetNumberField(TEXT("utc_offset_seconds"), Offset);
		return Local - FTimespan::FromSeconds(Offset);
	}

	bool Current(const FString& Json, TSharedPtr<FJsonObject>& Root, TSharedPtr<FJsonObject>& Out, FString& OutError)
	{
		if (!ReadObject(Json, Root))
		{
			OutError = TEXT("not a JSON object");
			return false;
		}
		const TSharedPtr<FJsonObject>* Block = nullptr;
		if (!Root->TryGetObjectField(TEXT("current"), Block) || !Block || !(*Block).IsValid())
		{
			FString Reason;
			OutError = Root->TryGetStringField(TEXT("reason"), Reason) ? Reason : TEXT("no \"current\" block");
			return false;
		}
		Out = *Block;
		return true;
	}

	double Wrap360(double Degrees)
	{
		Degrees = FMath::Fmod(Degrees, 360.0);
		return Degrees < 0.0 ? Degrees + 360.0 : Degrees;
	}
}

using namespace SimCruiseLivePrivate;

FString FSimCruiseLiveData::WeatherUrl(double Lat, double Lon)
{
	return FString::Printf(TEXT("https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,relative_humidity_2m,")
		TEXT("precipitation,weather_code,cloud_cover,cloud_cover_low,cloud_cover_mid,cloud_cover_high,visibility,wind_speed_10m,")
		TEXT("wind_direction_10m,wind_gusts_10m,is_day,surface_pressure&wind_speed_unit=ms&timezone=auto"), Lat, Lon);
}

FString FSimCruiseLiveData::AirUrl(double Lat, double Lon)
{
	return FString::Printf(TEXT("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%.4f&longitude=%.4f")
		TEXT("&current=pm2_5,pm10,aerosol_optical_depth,dust,us_aqi,uv_index&timezone=GMT"), Lat, Lon);
}

FString FSimCruiseLiveData::MarineUrl(double Lat, double Lon)
{
	return FString::Printf(TEXT("https://marine-api.open-meteo.com/v1/marine?latitude=%.4f&longitude=%.4f")
		TEXT("&current=wave_height,wave_direction,wave_period,sea_surface_temperature,sea_level_height_msl&timezone=GMT"), Lat, Lon);
}

FString FSimCruiseLiveData::MetarUrl(double Lat, double Lon)
{
	// The same box as helper/conditions.mjs, on the half-degree grid so nearby places share a cache.
	const double TileLat = FMath::RoundToDouble(Lat * 2.0) / 2.0;
	const double TileLon = FMath::RoundToDouble(Lon * 2.0) / 2.0;
	return FString::Printf(TEXT("https://aviationweather.gov/api/data/metar?format=json&hours=2&bbox=%.2f,%.2f,%.2f,%.2f"),
		TileLat - 0.65, FMath::Max(-180.0, TileLon - 0.75), TileLat + 0.65, FMath::Min(180.0, TileLon + 0.75));
}

FString FSimCruiseLiveData::TideStationsUrl()
{
	return TEXT("https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=waterlevels");
}

FString FSimCruiseLiveData::TideUrl(const FString& StationId)
{
	return FString::Printf(TEXT("https://api.tidesandcurrents.noaa.gov/api/prod/datagetter?date=latest&station=%s&product=water_level")
		TEXT("&datum=MSL&units=metric&time_zone=gmt&format=json&application=SimCruise"), *StationId);
}

FString FSimCruiseLiveData::AircraftUrl(double Lat, double Lon, int32 RadiusNm)
{
	return FString::Printf(TEXT("https://api.adsb.lol/v2/point/%.4f/%.4f/%d"), Lat, Lon, FMath::Clamp(RadiusNm, 1, 250));
}

FString FSimCruiseLiveData::StateUrl(double Lat, double Lon)
{
	return FString::Printf(TEXT("https://geocoding.geo.census.gov/geocoder/geographies/coordinates?x=%.5f&y=%.5f")
		TEXT("&benchmark=Public_AR_Current&vintage=Current_Current&layers=States&format=json"), Lon, Lat);
}

FString FSimCruiseLiveData::RoadsUrl(const FString& StateCode, double Lat, double Lon, double HalfKm)
{
	const double DLat = HalfKm / 111.32, DLon = HalfKm / (111.32 * FMath::Max(FMath::Cos(FMath::DegreesToRadians(Lat)), 0.05));
	return FString::Printf(TEXT("https://geo.dot.gov/server/rest/services/Hosted/HPMS_FULL_%s_2024/FeatureServer/0/query?geometry=%.5f,%.5f,%.5f,%.5f")
		TEXT("&geometryType=esriGeometryEnvelope&inSR=4326&outSR=4326&spatialRel=esriSpatialRelIntersects")
		TEXT("&outFields=route_id,routename,aadt,aadt_single_unit,aadt_combination,through_lanes,speed_limit,f_system,iri")
		TEXT("&where=aadt%%3E0&returnGeometry=true&geometryPrecision=6&resultRecordCount=2000&f=json"),
		*StateCode.ToUpper(), Lon - DLon, Lat - DLat, Lon + DLon, Lat + DLat);
}

FString FSimCruiseLiveData::PhotosUrl(double Lat, double Lon, int32 RadiusM)
{
	return FString::Printf(TEXT("https://commons.wikimedia.org/w/api.php?action=query&generator=geosearch&ggscoord=%.5f%%7C%.5f&ggsradius=%d")
		TEXT("&ggsnamespace=6&ggslimit=50&prop=imageinfo%%7Ccoordinates&iiprop=url%%7Cextmetadata%%7Csize&iiurlwidth=1024")
		TEXT("&iiextmetadatafilter=LicenseShortName%%7CArtist%%7CDateTimeOriginal&format=json&formatversion=2"), Lat, Lon, FMath::Clamp(RadiusM, 10, 10000));
}

bool FSimCruiseLiveData::ParseState(const FString& Json, FString& OutCode, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Result = nullptr;
	const TSharedPtr<FJsonObject>* Geographies = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* States = nullptr;
	if (!ReadObject(Json, Root) || !Root->TryGetObjectField(TEXT("result"), Result) || !(*Result)->TryGetObjectField(TEXT("geographies"), Geographies)
		|| !(*Geographies)->TryGetArrayField(TEXT("States"), States))
	{
		OutError = TEXT("no States in the Census answer");
		return false;
	}
	const TSharedPtr<FJsonObject>* State = nullptr;
	if (States->IsEmpty() || !(*States)[0]->TryGetObject(State) || !(*State)->TryGetStringField(TEXT("STUSAB"), OutCode))
	{
		OutError = TEXT("outside the United States");
		return false;
	}
	return true;
}

bool FSimCruiseLiveData::ParseRoads(const FString& Json, TArray<FSimCruiseRoadSection>& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
	if (!ReadObject(Json, Root) || !Root->TryGetArrayField(TEXT("features"), Features))
	{
		const TSharedPtr<FJsonObject>* Error = nullptr;
		FString Message;
		OutError = Root.IsValid() && Root->TryGetObjectField(TEXT("error"), Error) && (*Error)->TryGetStringField(TEXT("message"), Message) ? Message : TEXT("no features");
		return false;
	}
	Out.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Features)
	{
		const TSharedPtr<FJsonObject>* F = nullptr;
		const TSharedPtr<FJsonObject>* A = nullptr;
		const TSharedPtr<FJsonObject>* G = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Paths = nullptr;
		if (!Value->TryGetObject(F) || !(*F)->TryGetObjectField(TEXT("attributes"), A) || !(*F)->TryGetObjectField(TEXT("geometry"), G)
			|| !(*G)->TryGetArrayField(TEXT("paths"), Paths))
		{
			continue;
		}
		FSimCruiseRoadSection Road;
		(*A)->TryGetStringField(TEXT("route_id"), Road.RouteId);
		(*A)->TryGetStringField(TEXT("routename"), Road.Name);
		Road.Aadt = static_cast<int32>(Number(*A, TEXT("aadt")));
		Road.Trucks = static_cast<int32>(Number(*A, TEXT("aadt_single_unit")) + Number(*A, TEXT("aadt_combination")));
		Road.Lanes = static_cast<int32>(Number(*A, TEXT("through_lanes")));
		Road.SpeedLimitMph = static_cast<int32>(Number(*A, TEXT("speed_limit")));
		Road.FunctionalClass = static_cast<int32>(Number(*A, TEXT("f_system")));
		Road.IriInPerMile = Number(*A, TEXT("iri"));
		if (Road.Aadt <= 0)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& Path : *Paths)
		{
			for (const TSharedPtr<FJsonValue>& Point : Path->AsArray())
			{
				const TArray<TSharedPtr<FJsonValue>>& XY = Point->AsArray();
				if (XY.Num() >= 2)
				{
					Road.LonLat.Add(FVector2d(XY[0]->AsNumber(), XY[1]->AsNumber()));
				}
			}
		}
		if (Road.LonLat.Num() >= 2)
		{
			Out.Add(MoveTemp(Road));
		}
	}
	return true;
}

namespace SimCruiseLivePrivate
{
	FString MetaText(const TSharedPtr<FJsonObject>& Ext, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* Field = nullptr;
		FString Value;
		if (Ext.IsValid() && Ext->TryGetObjectField(Key, Field))
		{
			(*Field)->TryGetStringField(TEXT("value"), Value);
		}
		// Artist and the like are HTML: keep the text.
		FString Plain;
		bool bInTag = false;
		for (const TCHAR C : Value)
		{
			if (C == TEXT('<')) { bInTag = true; continue; }
			if (C == TEXT('>')) { bInTag = false; continue; }
			if (!bInTag) { Plain.AppendChar(C); }
		}
		return Plain.TrimStartAndEnd();
	}
}

bool FSimCruiseLiveData::ParsePhotos(const FString& Json, TArray<FSimCruisePlacePhoto>& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadObject(Json, Root))
	{
		OutError = TEXT("not a JSON object");
		return false;
	}
	Out.Reset();
	const TSharedPtr<FJsonObject>* Query = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Pages = nullptr;
	if (!Root->TryGetObjectField(TEXT("query"), Query) || !(*Query)->TryGetArrayField(TEXT("pages"), Pages))
	{
		return true; // no photographs here
	}
	for (const TSharedPtr<FJsonValue>& Value : *Pages)
	{
		const TSharedPtr<FJsonObject>* Page = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Infos = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Coords = nullptr;
		if (!Value->TryGetObject(Page) || !(*Page)->TryGetArrayField(TEXT("imageinfo"), Infos) || Infos->IsEmpty())
		{
			continue;
		}
		FSimCruisePlacePhoto Photo;
		(*Page)->TryGetStringField(TEXT("title"), Photo.Title);
		if ((*Page)->TryGetArrayField(TEXT("coordinates"), Coords) && !Coords->IsEmpty())
		{
			const TSharedPtr<FJsonObject> C = (*Coords)[0]->AsObject();
			C->TryGetNumberField(TEXT("lat"), Photo.Lat);
			C->TryGetNumberField(TEXT("lon"), Photo.Lon);
		}
		const TSharedPtr<FJsonObject> Info = (*Infos)[0]->AsObject();
		Info->TryGetStringField(TEXT("thumburl"), Photo.ThumbUrl);
		Info->TryGetStringField(TEXT("descriptionurl"), Photo.PageUrl);
		Photo.Width = static_cast<int32>(Number(Info, TEXT("width")));
		Photo.Height = static_cast<int32>(Number(Info, TEXT("height")));
		const TSharedPtr<FJsonObject>* Ext = nullptr;
		if (Info->TryGetObjectField(TEXT("extmetadata"), Ext))
		{
			Photo.License = MetaText(*Ext, TEXT("LicenseShortName"));
			Photo.Artist = MetaText(*Ext, TEXT("Artist"));
			Photo.Taken = MetaText(*Ext, TEXT("DateTimeOriginal"));
		}
		// Only what the licence lets the game use with credit.
		const FString L = Photo.License.ToLower();
		const bool bFree = L.Contains(TEXT("cc by")) || L.Contains(TEXT("cc-by")) || L.Contains(TEXT("cc0")) || L.Contains(TEXT("public domain"));
		if (bFree && !Photo.ThumbUrl.IsEmpty())
		{
			Out.Add(MoveTemp(Photo));
		}
	}
	return true;
}

float FSimCruiseLiveData::HourShare(int32 LocalHour, bool bWeekend)
{
	// Percent of the day's traffic by hour; typical urban patterns (FHWA TMG).
	static const float Weekday[24] = {0.9f, 0.6f, 0.5f, 0.5f, 0.8f, 2.0f, 4.6f, 7.1f, 7.0f, 5.4f, 4.9f, 5.2f,
		5.6f, 5.6f, 5.9f, 6.6f, 7.4f, 7.7f, 5.9f, 4.3f, 3.5f, 3.0f, 2.3f, 1.3f};
	static const float Weekend[24] = {1.8f, 1.3f, 1.0f, 0.7f, 0.6f, 0.9f, 1.6f, 2.5f, 3.6f, 4.8f, 5.8f, 6.5f,
		6.9f, 7.0f, 7.0f, 6.9f, 6.7f, 6.4f, 5.8f, 5.0f, 4.3f, 3.7f, 3.0f, 2.2f};
	const int32 H = ((LocalHour % 24) + 24) % 24;
	const float* Table = bWeekend ? Weekend : Weekday;
	float Sum = 0.0f;
	for (int32 I = 0; I < 24; ++I)
	{
		Sum += Table[I];
	}
	return Table[H] / Sum;
}

float FSimCruiseLiveData::DayFactor(int32 DayOfWeek)
{
	static const float Factor[7] = {0.82f, 0.98f, 1.00f, 1.02f, 1.04f, 1.10f, 0.96f};
	return Factor[((DayOfWeek % 7) + 7) % 7];
}

float FSimCruiseLiveData::FlowPerHour(const FSimCruiseRoadSection& Road, const FDateTime& Local, bool bRain, bool bSnow)
{
	// FDateTime counts Monday as 0; the factors count Sunday as 0.
	const int32 Dow = (static_cast<int32>(Local.GetDayOfWeek()) + 1) % 7;
	const bool bWeekend = Dow == 0 || Dow == 6;
	const float Now = HourShare(Local.GetHour(), bWeekend);
	const float Next = HourShare(Local.GetHour() + 1, bWeekend);
	// Blend toward the next hour so the count does not jump on the hour.
	const float Share = FMath::Lerp(Now, Next, Local.GetMinute() / 60.0f);
	return Road.Aadt * DayFactor(Dow) * Share * (bSnow ? 0.85f : bRain ? 0.95f : 1.0f);
}

float FSimCruiseLiveData::DensityPerKmLane(const FSimCruiseRoadSection& Road, float FlowPerHour)
{
	const float SpeedKmh = (Road.SpeedLimitMph > 0 ? Road.SpeedLimitMph : 30) * 1.609344f;
	return FlowPerHour / (FMath::Max(Road.Lanes, 1) * SpeedKmh);
}

const FSimCruiseRoadSection* FSimCruiseLiveData::NearestRoad(const TArray<FSimCruiseRoadSection>& Roads, double Lat, double Lon, double MaxM, double& OutM)
{
	// Local equirectangular metres are exact enough over a few hundred metres.
	const double MPerDegLat = 111320.0, MPerDegLon = 111320.0 * FMath::Cos(FMath::DegreesToRadians(Lat));
	const FSimCruiseRoadSection* Best = nullptr;
	OutM = MaxM;
	for (const FSimCruiseRoadSection& Road : Roads)
	{
		for (int32 I = 1; I < Road.LonLat.Num(); ++I)
		{
			const FVector2d A((Road.LonLat[I - 1].X - Lon) * MPerDegLon, (Road.LonLat[I - 1].Y - Lat) * MPerDegLat);
			const FVector2d B((Road.LonLat[I].X - Lon) * MPerDegLon, (Road.LonLat[I].Y - Lat) * MPerDegLat);
			const FVector2d AB = B - A;
			const double T = FMath::Clamp(FVector2d::DotProduct(-A, AB) / FMath::Max(AB.SizeSquared(), 1e-9), 0.0, 1.0);
			const double D = (A + AB * T).Size();
			if (D < OutM)
			{
				OutM = D;
				Best = &Road;
			}
		}
	}
	return Best;
}

bool FSimCruiseLiveData::ParseWeather(const FString& Json, FSimCruiseLiveWeather& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root, Cur;
	if (!Current(Json, Root, Cur, OutError))
	{
		return false;
	}
	double Cloud = 0.0;
	if (!Cur->TryGetNumberField(TEXT("cloud_cover"), Cloud))
	{
		OutError = TEXT("no cloud_cover");
		return false;
	}
	Out = FSimCruiseLiveWeather();
	Out.TimeUtc = OpenMeteoTime(Root, Cur);
	Out.TemperatureC = Number(Cur, TEXT("temperature_2m"));
	Out.HumidityPct = Number(Cur, TEXT("relative_humidity_2m"));
	Out.PrecipitationMm = Number(Cur, TEXT("precipitation"));
	Out.WmoCode = static_cast<int32>(Number(Cur, TEXT("weather_code")));
	Out.CloudCover = FMath::Clamp(static_cast<float>(Cloud) / 100.0f, 0.0f, 1.0f);
	Out.CloudLow = FMath::Clamp(Number(Cur, TEXT("cloud_cover_low")) / 100.0f, 0.0f, 1.0f);
	Out.CloudMid = FMath::Clamp(Number(Cur, TEXT("cloud_cover_mid")) / 100.0f, 0.0f, 1.0f);
	Out.CloudHigh = FMath::Clamp(Number(Cur, TEXT("cloud_cover_high")) / 100.0f, 0.0f, 1.0f);
	Out.VisibilityM = Number(Cur, TEXT("visibility"), 24000.0f);

	// The URL asks for m/s; a feed in the default km/h says so in current_units.
	float WindScale = 1.0f;
	const TSharedPtr<FJsonObject>* Units = nullptr;
	FString WindUnit;
	if (Root->TryGetObjectField(TEXT("current_units"), Units) && Units && (*Units)->TryGetStringField(TEXT("wind_speed_10m"), WindUnit)
		&& WindUnit.Contains(TEXT("km/h")))
	{
		WindScale = static_cast<float>(KmhToMs);
	}
	Out.WindSpeedMs = Number(Cur, TEXT("wind_speed_10m")) * WindScale;
	Out.WindGustMs = Number(Cur, TEXT("wind_gusts_10m")) * WindScale;
	Out.WindFromDeg = Number(Cur, TEXT("wind_direction_10m"));
	Out.PressureHPa = Number(Cur, TEXT("surface_pressure"));
	Out.bIsDay = Number(Cur, TEXT("is_day"), 1.0f) > 0.5f;
	double Offset = 0.0;
	Root->TryGetNumberField(TEXT("utc_offset_seconds"), Offset);
	Out.UtcOffsetSeconds = static_cast<int32>(Offset);
	Out.bValid = true;
	return true;
}

bool FSimCruiseLiveData::ParseAir(const FString& Json, FSimCruiseLiveAir& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root, Cur;
	if (!Current(Json, Root, Cur, OutError))
	{
		return false;
	}
	Out = FSimCruiseLiveAir();
	Out.Pm25 = Number(Cur, TEXT("pm2_5"));
	Out.Pm10 = Number(Cur, TEXT("pm10"));
	Out.AerosolOpticalDepth = Number(Cur, TEXT("aerosol_optical_depth"), -1.0f);
	Out.DustUgM3 = Number(Cur, TEXT("dust"));
	Out.UsAqi = Number(Cur, TEXT("us_aqi"));
	Out.UvIndex = Number(Cur, TEXT("uv_index"));
	Out.bValid = true;
	return true;
}

bool FSimCruiseLiveData::ParseMarine(const FString& Json, FSimCruiseLiveSea& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root, Cur;
	if (!Current(Json, Root, Cur, OutError))
	{
		return false;
	}
	double Wave = 0.0;
	if (!Cur->TryGetNumberField(TEXT("wave_height"), Wave))
	{
		// Inland points come back with nulls: no sea here.
		OutError = TEXT("no sea at this place");
		return false;
	}
	Out.WaveHeightM = static_cast<float>(Wave);
	Out.WavePeriodS = Number(Cur, TEXT("wave_period"));
	Out.WaveFromDeg = Number(Cur, TEXT("wave_direction"));
	Out.SeaSurfaceTempC = Number(Cur, TEXT("sea_surface_temperature"));
	Out.SeaLevelM = Number(Cur, TEXT("sea_level_height_msl"));
	Out.bValid = true;
	return true;
}

bool FSimCruiseLiveData::ParseMetars(const FString& Json, TArray<FSimCruiseMetar>& Out, FString& OutError)
{
	TArray<TSharedPtr<FJsonValue>> Rows;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Rows))
	{
		OutError = TEXT("METAR feed is not a JSON array");
		return false;
	}
	static const TMap<FString, float> Cover = {{TEXT("CLR"), 0.0f}, {TEXT("SKC"), 0.0f}, {TEXT("NSC"), 0.0f}, {TEXT("CAVOK"), 0.0f},
		{TEXT("FEW"), 0.19f}, {TEXT("SCT"), 0.44f}, {TEXT("BKN"), 0.78f}, {TEXT("OVC"), 1.0f}, {TEXT("OVX"), 1.0f}, {TEXT("VV"), 1.0f}};
	Out.Reset();
	for (const TSharedPtr<FJsonValue>& Value : Rows)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& R = *Row;
		FSimCruiseMetar M;
		double ObsTime = 0.0;
		FString CoverCode;
		if (!R->TryGetNumberField(TEXT("lat"), M.Lat) || !R->TryGetNumberField(TEXT("lon"), M.Lon) || !R->TryGetNumberField(TEXT("obsTime"), ObsTime)
			|| !R->TryGetStringField(TEXT("cover"), CoverCode) || !Cover.Contains(CoverCode))
		{
			continue;
		}
		M.TimeUtc = FDateTime::FromUnixTimestamp(static_cast<int64>(ObsTime));
		M.CloudCover = Cover[CoverCode];
		R->TryGetStringField(TEXT("icaoId"), M.Station);
		R->TryGetStringField(TEXT("wxString"), M.WxString);
		R->TryGetStringField(TEXT("rawOb"), M.Raw);
		// visib is a number of statute miles or a string such as "10+".
		FString Visib;
		double VisibNumber = 0.0;
		if (R->TryGetNumberField(TEXT("visib"), VisibNumber))
		{
			M.VisibilityM = static_cast<float>(VisibNumber * 1609.344);
		}
		else if (R->TryGetStringField(TEXT("visib"), Visib) && !Visib.IsEmpty())
		{
			M.VisibilityM = static_cast<float>(FCString::Atod(*Visib.Replace(TEXT("+"), TEXT(""))) * 1609.344);
			if (M.VisibilityM <= 0.0f)
			{
				M.VisibilityM = 16093.0f;
			}
		}
		Out.Add(MoveTemp(M));
	}
	return true;
}

bool FSimCruiseLiveData::ParseTideStations(const FString& Json, TArray<FSimCruiseTideStation>& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* Stations = nullptr;
	if (!ReadObject(Json, Root) || !Root->TryGetArrayField(TEXT("stations"), Stations) || !Stations)
	{
		OutError = TEXT("no stations array");
		return false;
	}
	Out.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *Stations)
	{
		const TSharedPtr<FJsonObject>* S = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(S) || !S)
		{
			continue;
		}
		FSimCruiseTideStation Station;
		if ((*S)->TryGetStringField(TEXT("id"), Station.Id) && (*S)->TryGetNumberField(TEXT("lat"), Station.Lat)
			&& (*S)->TryGetNumberField(TEXT("lng"), Station.Lon))
		{
			(*S)->TryGetStringField(TEXT("name"), Station.Name);
			Out.Add(MoveTemp(Station));
		}
	}
	return Out.Num() > 0;
}

bool FSimCruiseLiveData::ParseTide(const FString& Json, float& OutLevelM, FDateTime& OutUtc, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	if (!ReadObject(Json, Root))
	{
		OutError = TEXT("not a JSON object");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
	if (!Root->TryGetArrayField(TEXT("data"), Data) || !Data || Data->IsEmpty())
	{
		const TSharedPtr<FJsonObject>* Error = nullptr;
		FString Message;
		OutError = Root->TryGetObjectField(TEXT("error"), Error) && Error && (*Error)->TryGetStringField(TEXT("message"), Message) ? Message : TEXT("no data");
		return false;
	}
	const TSharedPtr<FJsonObject>* Last = nullptr;
	FString Value, Time;
	if (!Data->Last()->TryGetObject(Last) || !Last || !(*Last)->TryGetStringField(TEXT("v"), Value) || Value.IsEmpty())
	{
		OutError = TEXT("no water level value");
		return false;
	}
	OutLevelM = FCString::Atof(*Value);
	// "2026-11-11 22:30" in GMT.
	if ((*Last)->TryGetStringField(TEXT("t"), Time))
	{
		ParseMinutes(Time, OutUtc);
	}
	return true;
}

bool FSimCruiseLiveData::ParseAircraft(const FString& Json, TArray<FSimCruiseLiveAircraft>& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!ReadObject(Json, Root) || !Root->TryGetArrayField(TEXT("ac"), List) || !List)
	{
		OutError = TEXT("no \"ac\" array");
		return false;
	}
	Out.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Row) || !Row)
		{
			continue;
		}
		const TSharedPtr<FJsonObject>& R = *Row;
		FSimCruiseLiveAircraft A;
		if (!R->TryGetNumberField(TEXT("lat"), A.Lat) || !R->TryGetNumberField(TEXT("lon"), A.Lon) || !R->TryGetStringField(TEXT("hex"), A.Hex))
		{
			continue;
		}
		R->TryGetStringField(TEXT("flight"), A.Flight);
		A.Flight.TrimStartAndEndInline();
		R->TryGetStringField(TEXT("t"), A.Type);
		R->TryGetStringField(TEXT("r"), A.Registration);
		R->TryGetStringField(TEXT("category"), A.Category);

		// alt_baro is feet or the string "ground"; alt_geom (GNSS) is feet above the WGS84 ellipsoid.
		FString BaroText;
		double Baro = 0.0, Geom = 0.0;
		const bool bBaro = R->TryGetNumberField(TEXT("alt_baro"), Baro);
		if (!bBaro && R->TryGetStringField(TEXT("alt_baro"), BaroText) && BaroText == TEXT("ground"))
		{
			A.bOnGround = true;
		}
		if (R->TryGetNumberField(TEXT("alt_geom"), Geom))
		{
			A.AltitudeM = static_cast<float>(Geom * FeetToMetres);
		}
		else if (bBaro)
		{
			A.AltitudeM = static_cast<float>(Baro * FeetToMetres);
		}
		else if (!A.bOnGround)
		{
			continue; // no height: cannot place it
		}

		double Gs = 0.0, Track = 0.0, Rate = 0.0, Seen = 0.0;
		if (R->TryGetNumberField(TEXT("gs"), Gs)) { A.GroundSpeedMs = static_cast<float>(Gs * KnotsToMs); }
		if (R->TryGetNumberField(TEXT("track"), Track) || R->TryGetNumberField(TEXT("true_heading"), Track)) { A.TrackDeg = static_cast<float>(Track); }
		if (R->TryGetNumberField(TEXT("geom_rate"), Rate) || R->TryGetNumberField(TEXT("baro_rate"), Rate))
		{
			A.VerticalRateMs = static_cast<float>(Rate * FeetToMetres / 60.0);
		}
		if (R->TryGetNumberField(TEXT("seen_pos"), Seen)) { A.PositionAgeS = static_cast<float>(Seen); }
		// A position older than a minute is a ghost.
		if (A.PositionAgeS > 60.0f)
		{
			continue;
		}
		Out.Add(MoveTemp(A));
	}
	return true;
}

bool FSimCruiseLiveData::IsRainCode(int32 Wmo)
{
	return (Wmo >= 51 && Wmo <= 67) || (Wmo >= 80 && Wmo <= 82) || (Wmo >= 95 && Wmo <= 99);
}

bool FSimCruiseLiveData::IsSnowCode(int32 Wmo)
{
	return (Wmo >= 71 && Wmo <= 77) || Wmo == 85 || Wmo == 86;
}

bool FSimCruiseLiveData::IsFogCode(int32 Wmo)
{
	return Wmo == 45 || Wmo == 48;
}

FString FSimCruiseLiveData::DescribeWmo(int32 Wmo)
{
	switch (Wmo)
	{
	case 0: return TEXT("clear");
	case 1: return TEXT("mainly clear");
	case 2: return TEXT("partly cloudy");
	case 3: return TEXT("overcast");
	case 45: case 48: return TEXT("fog");
	case 51: case 53: case 55: return TEXT("drizzle");
	case 56: case 57: return TEXT("freezing drizzle");
	case 61: case 63: return TEXT("rain");
	case 65: return TEXT("heavy rain");
	case 66: case 67: return TEXT("freezing rain");
	case 71: case 73: case 75: case 77: return TEXT("snow");
	case 80: case 81: return TEXT("rain showers");
	case 82: return TEXT("violent rain showers");
	case 85: case 86: return TEXT("snow showers");
	case 95: return TEXT("thunderstorm");
	case 96: case 99: return TEXT("thunderstorm with hail");
	default: return TEXT("weather");
	}
}

const FSimCruiseMetar* FSimCruiseLiveData::NearestMetar(const TArray<FSimCruiseMetar>& Metars, double Lat, double Lon, const FDateTime& NowUtc,
	double MaxKm, double MaxAgeMinutes)
{
	const FSimCruiseMetar* Best = nullptr;
	double BestKm = MaxKm;
	for (const FSimCruiseMetar& M : Metars)
	{
		const double AgeMinutes = FMath::Abs((NowUtc - M.TimeUtc).GetTotalMinutes());
		const double Km = DistanceKm(Lat, Lon, M.Lat, M.Lon);
		if (AgeMinutes > MaxAgeMinutes || Km > BestKm)
		{
			continue;
		}
		// The same station reports more than once in two hours: the newest wins at equal distance.
		if (Best && FMath::IsNearlyEqual(Km, BestKm, 0.01) && M.TimeUtc <= Best->TimeUtc)
		{
			continue;
		}
		Best = &M;
		BestKm = Km;
	}
	return Best;
}

const FSimCruiseTideStation* FSimCruiseLiveData::NearestTideStation(const TArray<FSimCruiseTideStation>& Stations, double Lat, double Lon, double MaxKm)
{
	const FSimCruiseTideStation* Best = nullptr;
	double BestKm = MaxKm;
	for (const FSimCruiseTideStation& S : Stations)
	{
		const double Km = DistanceKm(Lat, Lon, S.Lat, S.Lon);
		if (Km <= BestKm)
		{
			Best = &S;
			BestKm = Km;
		}
	}
	return Best;
}

FSimCruiseWeatherConditions FSimCruiseLiveData::Fuse(const FSimCruiseLiveWeather* Weather, const FSimCruiseMetar* Metar, const FSimCruiseLiveAir* Air,
	const FSimCruiseWeatherConditions& Base)
{
	FSimCruiseWeatherConditions Out = Base;
	if (Weather && Weather->bValid)
	{
		Out.CloudCover = Weather->CloudCover;
		// The model's visibility caps near 24 km in clear air; that cap is not a measured haze.
		Out.VisibilityM = Weather->VisibilityM >= 20000.0f ? FMath::Max(Weather->VisibilityM, 40000.0f) : FMath::Max(Weather->VisibilityM, 50.0f);
		Out.bRaining = IsRainCode(Weather->WmoCode) || (Weather->PrecipitationMm > 0.05f && !IsSnowCode(Weather->WmoCode));
		Out.bSnowing = IsSnowCode(Weather->WmoCode);
		Out.WindSpeedMs = Weather->WindSpeedMs;
		Out.WindFromDeg = Weather->WindFromDeg;
		Out.TemperatureC = Weather->TemperatureC;
		Out.Description = DescribeWmo(Weather->WmoCode);
		Out.Station = TEXT("Open-Meteo");
		Out.StationKm = 0.0f;
		Out.bClearSky = Weather->CloudCover < 0.05f && !Out.bRaining && Out.VisibilityM >= 10000.0f;
		Out.bObserved = true;
	}
	if (Metar)
	{
		// An observer at the airport: what they see wins over the model where they measure it.
		Out.CloudCover = Metar->CloudCover;
		Out.VisibilityM = Metar->VisibilityM >= 16000.0f ? FMath::Max(Out.VisibilityM, 40000.0f) : FMath::Max(Metar->VisibilityM, 50.0f);
		// The same present-weather rules as the helper's conditions (FSimCruiseWeather).
		Out.bRaining = FSimCruiseWeather::IsRainWx(Metar->WxString);
		Out.bSnowing = FSimCruiseWeather::IsSnowWx(Metar->WxString);
		Out.bClearSky = Metar->CloudCover <= 0.0f && Metar->VisibilityM >= 10000.0f && Metar->WxString.TrimStartAndEnd().IsEmpty() && Metar->Km <= 40.0f;
		Out.Station = Metar->Station;
		Out.StationKm = Metar->Km;
		if (!Metar->WxString.IsEmpty())
		{
			Out.Description = Metar->WxString;
		}
		Out.bObserved = true;
	}
	if (Air && Air->bValid && Air->AerosolOpticalDepth > 0.0f)
	{
		Out.AerosolOpticalDepth = Air->AerosolOpticalDepth;
	}
	return Out;
}

void FSimCruiseLiveData::Extrapolate(const FSimCruiseLiveAircraft& Aircraft, double DeltaSeconds, double& OutLat, double& OutLon, float& OutAltitudeM)
{
	const double Distance = static_cast<double>(Aircraft.GroundSpeedMs) * DeltaSeconds;
	const double Delta = Distance / EarthRadiusM;
	const double Track = FMath::DegreesToRadians(static_cast<double>(Aircraft.TrackDeg));
	const double Lat1 = FMath::DegreesToRadians(Aircraft.Lat);
	const double Lon1 = FMath::DegreesToRadians(Aircraft.Lon);
	const double Lat2 = FMath::Asin(FMath::Sin(Lat1) * FMath::Cos(Delta) + FMath::Cos(Lat1) * FMath::Sin(Delta) * FMath::Cos(Track));
	const double Lon2 = Lon1 + FMath::Atan2(FMath::Sin(Track) * FMath::Sin(Delta) * FMath::Cos(Lat1), FMath::Cos(Delta) - FMath::Sin(Lat1) * FMath::Sin(Lat2));
	OutLat = FMath::RadiansToDegrees(Lat2);
	OutLon = FMath::RadiansToDegrees(Lon2);
	if (OutLon > 180.0) { OutLon -= 360.0; }
	if (OutLon < -180.0) { OutLon += 360.0; }
	OutAltitudeM = Aircraft.bOnGround ? Aircraft.AltitudeM : FMath::Max(0.0f, Aircraft.AltitudeM + Aircraft.VerticalRateMs * static_cast<float>(DeltaSeconds));
}

double FSimCruiseLiveData::DistanceKm(double LatA, double LonA, double LatB, double LonB)
{
	const double P1 = FMath::DegreesToRadians(LatA), P2 = FMath::DegreesToRadians(LatB);
	const double DP = P2 - P1, DL = FMath::DegreesToRadians(LonB - LonA);
	const double H = FMath::Square(FMath::Sin(DP / 2.0)) + FMath::Cos(P1) * FMath::Cos(P2) * FMath::Square(FMath::Sin(DL / 2.0));
	return EarthRadiusM / 1000.0 * 2.0 * FMath::Asin(FMath::Sqrt(FMath::Min(1.0, H)));
}

double FSimCruiseLiveData::CurvatureDropM(double DistanceM)
{
	// Standard terrestrial refraction bends sight lines down by k = 0.13 of the Earth's curvature.
	constexpr double K = 0.13;
	return DistanceM * DistanceM * (1.0 - K) / (2.0 * EarthRadiusM);
}

FString FSimCruiseLiveData::Compass(float Degrees)
{
	static const TCHAR* Names[] = {TEXT("N"), TEXT("NNE"), TEXT("NE"), TEXT("ENE"), TEXT("E"), TEXT("ESE"), TEXT("SE"), TEXT("SSE"),
		TEXT("S"), TEXT("SSW"), TEXT("SW"), TEXT("WSW"), TEXT("W"), TEXT("WNW"), TEXT("NW"), TEXT("NNW")};
	const int32 Index = FMath::RoundToInt(Wrap360(Degrees) / 22.5) % 16;
	return Names[Index];
}

FString FSimCruiseLiveData::Summary(const FSimCruiseLiveWeather* Weather, const FSimCruiseMetar* Metar, const FSimCruiseLiveAir* Air,
	const FSimCruiseLiveSea* Sea, int32 AircraftCount)
{
	TArray<FString> Parts;
	if (Weather && Weather->bValid)
	{
		Parts.Add(FString::Printf(TEXT("%.0f C, %s, cloud %.0f%%, wind %.0f km/h from %s"), Weather->TemperatureC, *DescribeWmo(Weather->WmoCode),
			Weather->CloudCover * 100.0f, Weather->WindSpeedMs * 3.6f, *Compass(Weather->WindFromDeg)));
	}
	if (Metar)
	{
		Parts.Add(FString::Printf(TEXT("%s %s"), *Metar->Station, Metar->WxString.IsEmpty() ? TEXT("observed") : *Metar->WxString));
	}
	if (Air && Air->bValid)
	{
		Parts.Add(FString::Printf(TEXT("AQI %.0f, haze AOD %.2f"), Air->UsAqi, Air->AerosolOpticalDepth));
	}
	if (Sea && Sea->bValid)
	{
		Parts.Add(Sea->bTideObserved
			? FString::Printf(TEXT("tide %+.2f m (%s), waves %.1f m"), Sea->TideM, *Sea->TideStation, Sea->WaveHeightM)
			: FString::Printf(TEXT("sea level %+.2f m, waves %.1f m"), Sea->SeaLevelM, Sea->WaveHeightM));
	}
	if (AircraftCount > 0)
	{
		Parts.Add(FString::Printf(TEXT("%d aircraft overhead"), AircraftCount));
	}
	return Parts.IsEmpty() ? FString(TEXT("no live data yet")) : FString::Join(Parts, TEXT("  |  "));
}
