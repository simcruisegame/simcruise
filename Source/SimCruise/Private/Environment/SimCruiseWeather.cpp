// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseWeather.h"

#include "Dom/JsonObject.h"
#include "Internationalization/Regex.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool MatchesPrecipitation(const FString& Wx, const TCHAR* Pattern)
	{
		const FRegexPattern Regex(Pattern);
		FRegexMatcher Matcher(Regex, Wx);
		return Matcher.FindNext();
	}
}

bool FSimCruiseWeather::ParseConditionsJson(const FString& Json, FSimCruiseWeatherConditions& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("conditions are not a JSON object");
		return false;
	}

	if (Root->Values.IsEmpty())
	{
		// No station within reach: keep the default sky.
		return true;
	}

	double Cloud = 0.0;
	if (!Root->TryGetNumberField(TEXT("cloud"), Cloud) || Cloud < 0.0 || Cloud > 1.0)
	{
		OutError = TEXT("conditions need a cloud fraction between 0 and 1");
		return false;
	}
	Out.CloudCover = static_cast<float>(Cloud);

	double Visibility = 0.0;
	if (Root->TryGetNumberField(TEXT("visibility_m"), Visibility) && Visibility > 0.0)
	{
		// METAR tops out at 10 SM ("10SM" means at least that); a reported cap is not a measured haze.
		Out.VisibilityM = Visibility >= 16000.0 ? FMath::Max(static_cast<float>(Visibility), 40000.0f) : static_cast<float>(Visibility);
	}

	bool bClear = false;
	Root->TryGetBoolField(TEXT("clear_sky"), bClear);
	Out.bClearSky = bClear;

	bool bWet = false;
	Root->TryGetBoolField(TEXT("wet"), bWet);
	FString Wx;
	if (Root->TryGetStringField(TEXT("wx"), Wx) && !Wx.IsEmpty())
	{
		Out.bRaining = MatchesPrecipitation(Wx, TEXT("(^|[\\s+-])(FZ)?(SH|TS)?(RA|DZ)"));
		Out.bSnowing = MatchesPrecipitation(Wx, TEXT("(^|[\\s+-])(SH|TS)?(SN|SG|PL|GR|GS)"));
	}
	else
	{
		// Without the raw string the helper's flag cannot tell rain from snow; treat it as rain.
		Out.bRaining = bWet;
		Out.bSnowing = false;
	}

	Root->TryGetStringField(TEXT("station"), Out.Station);
	Root->TryGetStringField(TEXT("description"), Out.Description);
	double Km = 0.0;
	if (Root->TryGetNumberField(TEXT("km"), Km))
	{
		Out.StationKm = static_cast<float>(Km);
	}
	Out.bObserved = true;
	return true;
}

float FSimCruiseWeather::StepRoadWetness(float Wetness, bool bRaining, float DeltaSeconds)
{
	const float Target = bRaining ? 1.0f : 0.0f;
	const float TimeConstant = bRaining ? 180.0f : 900.0f;
	const float Alpha = 1.0f - FMath::Exp(-FMath::Max(DeltaSeconds, 0.0f) / TimeConstant);
	return FMath::Clamp(FMath::Lerp(Wetness, Target, Alpha), 0.0f, 1.0f);
}

float FSimCruiseWeather::CloudMaterialCoverage(float CloudCover)
{
	// Epic's simple cloud material (m_SimpleVolumetricCloud) treats Cloud_GlobalCoverage as an
	// offset on its weather texture: -0.5 is a clear sky, 0.3 a solid deck (0.2 still let the sun through). The knots were
	// read off a sweep of lab captures (docs/unreal-architecture.md): CLR, FEW, SCT, BKN, OVC.
	static const FVector2f Knots[] = {{0.0f, -0.5f}, {0.19f, -0.25f}, {0.44f, -0.12f}, {0.78f, 0.0f}, {1.0f, 0.35f}};
	const float Cover = FMath::Clamp(CloudCover, 0.0f, 1.0f);
	for (int32 Index = 1; Index < UE_ARRAY_COUNT(Knots); ++Index)
	{
		if (Cover <= Knots[Index].X)
		{
			const float Alpha = (Cover - Knots[Index - 1].X) / (Knots[Index].X - Knots[Index - 1].X);
			return FMath::Lerp(Knots[Index - 1].Y, Knots[Index].Y, Alpha);
		}
	}
	return Knots[UE_ARRAY_COUNT(Knots) - 1].Y;
}
