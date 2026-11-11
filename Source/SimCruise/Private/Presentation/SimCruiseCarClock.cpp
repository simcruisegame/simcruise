// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarClock.h"

#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#endif

namespace
{
	// UV: the mesh's texture coordinates. BoxMin, BoxMax: the digits' box in the texture (u, v);
	// FlipV: 1 when the mesh shows the texture upside down (as the NB's does). Minutes after
	// midnight in Minutes; Backlight in nits. The box is cleared to the LCD's background (read
	// at Clear) and the time drawn as H:MM or HH:MM in dark segments, right-aligned like the
	// real clock.
	const TCHAR* ClockCode = TEXT(R"HLSL(
float3 bg = Texture2DSample(Tex, TexSampler, Clear.xy).rgb;
float3 col = Texture2DSample(Tex, TexSampler, UV).rgb;
float2 q = (UV - BoxMin.xy) / (BoxMax.xy - BoxMin.xy);
if (all(q >= 0.0) && all(q <= 1.0))
{
	col = bg;
	if (FlipV > 0.5) q.y = 1.0 - q.y;
	int m = (int)round(Minutes);
	int h = (m / 60) % 12; if (h == 0) h = 12;
	int mm = m % 60;
	int digits[4] = { h / 10, h % 10, mm / 10, mm % 10 };
	// Cells across the box: tens of hours (narrow), hours, colon, tens of minutes, minutes.
	const float x0[5] = { 0.00, 0.14, 0.40, 0.50, 0.76 };
	const float x1[5] = { 0.10, 0.36, 0.46, 0.72, 0.98 };
	const int masks[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
	float ink = 0.0;
	for (int c = 0; c < 5; c++)
	{
		if (q.x < x0[c] || q.x > x1[c]) continue;
		float2 p = float2((q.x - x0[c]) / (x1[c] - x0[c]), (q.y - 0.12) / 0.76);
		if (p.y < 0.0 || p.y > 1.0) continue;
		if (c == 2)
		{
			ink = max(ink, step(length((p - float2(0.5, 0.32)) * float2(1.0, 2.2)), 0.22));
			ink = max(ink, step(length((p - float2(0.5, 0.68)) * float2(1.0, 2.2)), 0.22));
			continue;
		}
		int d = c < 2 ? digits[c] : digits[c - 1];
		if (c == 0) { if (d == 0) continue; p.x = 0.6 + p.x * 0.4; }
		int mask = masks[d];
		const float t = 0.09, tv = 0.13;
		// a top, b top-right, c bottom-right, d bottom, e bottom-left, f top-left, g middle
		float s = 0.0;
		s += ((mask >> 0) & 1) * step(abs(p.y - 0.05), t) * step(abs(p.x - 0.5), 0.32);
		s += ((mask >> 1) & 1) * step(abs(p.x - 0.88), tv) * step(abs(p.y - 0.27), 0.2);
		s += ((mask >> 2) & 1) * step(abs(p.x - 0.88), tv) * step(abs(p.y - 0.73), 0.2);
		s += ((mask >> 3) & 1) * step(abs(p.y - 0.95), t) * step(abs(p.x - 0.5), 0.32);
		s += ((mask >> 4) & 1) * step(abs(p.x - 0.12), tv) * step(abs(p.y - 0.73), 0.2);
		s += ((mask >> 5) & 1) * step(abs(p.x - 0.12), tv) * step(abs(p.y - 0.27), 0.2);
		s += ((mask >> 6) & 1) * step(abs(p.y - 0.5), t) * step(abs(p.x - 0.5), 0.32);
		ink = max(ink, saturate(s));
	}
	col = lerp(col, Ink.rgb, ink);
}
Emis = col * Backlight;
return col;
)HLSL");
}

namespace SimCruiseCarClock
{
	int32 LocalMinutes(const FDateTime& Utc, double LatitudeDeg, double LongitudeDeg)
	{
		const bool bHawaii = LatitudeDeg > 18.5 && LatitudeDeg < 22.5 && LongitudeDeg < -154.0 && LongitudeDeg > -161.0;
		const bool bArizona = LatitudeDeg > 31.3 && LatitudeDeg < 37.0 && LongitudeDeg > -114.8 && LongitudeDeg < -109.05;
		const bool bUS = LatitudeDeg > 18.0 && LatitudeDeg < 72.0 && LongitudeDeg < -66.0 && LongitudeDeg > -180.0;
		int32 ZoneHours = FMath::RoundToInt(LongitudeDeg / 15.0);
		if (bHawaii) ZoneHours = -10;
		else if (bUS)
		{
			ZoneHours = LongitudeDeg > -85.5 ? -5 : LongitudeDeg > -101.0 ? -6 : LongitudeDeg > -114.5 ? -7 : LongitudeDeg > -130.0 ? -8 : -9;
			if (bArizona) ZoneHours = -7;
		}
		FDateTime Local = Utc + FTimespan::FromHours(ZoneHours);
		// US daylight saving: from the second Sunday of March to the first Sunday of November, 02:00 local.
		auto NthSunday = [](int32 Year, int32 Month, int32 N)
		{
			FDateTime Day(Year, Month, 1);
			while (Day.GetDayOfWeek() != EDayOfWeek::Sunday) Day += FTimespan::FromDays(1);
			return Day + FTimespan::FromDays(7 * (N - 1)) + FTimespan::FromHours(2);
		};
		const int32 Year = Local.GetYear();
		if (bUS && !bHawaii && !bArizona && Local >= NthSunday(Year, 3, 2) && Local < NthSunday(Year, 11, 1))
		{
			Local += FTimespan::FromHours(1);
		}
		return Local.GetHour() * 60 + Local.GetMinute();
	}

	FString Format12(int32 Minutes)
	{
		int32 H = (Minutes / 60) % 12;
		if (H == 0) H = 12;
		return FString::Printf(TEXT("%d:%02d"), H, Minutes % 60);
	}

	UMaterialInterface* Get()
	{
		static TStrongObjectPtr<UMaterialInterface> Cached;
		if (Cached.IsValid())
		{
			return Cached.Get();
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicle/Materials/M_SimCruiseCarClock.M_SimCruiseCarClock"), nullptr, LOAD_NoWarn | LOAD_Quiet);
#if WITH_EDITOR
		if (!Material && FApp::CanEverRender())
		{
			UMaterial* Built = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_SimCruiseCarClock"), RF_Transient);
			// The display is a section of a Nanite cabin part: a material without Nanite usage
			// sends the whole part to its coarse fallback mesh.
			Built->bUsedWithNanite = true;
			UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Built);
			Built->GetExpressionCollection().AddExpression(Custom);
			Custom->OutputType = CMOT_Float3;
			Custom->Description = TEXT("SimCruise dashboard clock");
			Custom->Code = ClockCode;
			Custom->Inputs.Reset();
			Custom->AdditionalOutputs.Add({ FName(TEXT("Emis")), CMOT_Float3 });
			auto Input = [&](const TCHAR* Name, UMaterialExpression* Expression)
			{
				FCustomInput In;
				In.InputName = Name;
				In.Input.Connect(0, Expression);
				Custom->Inputs.Add(In);
			};
			auto Add = [&](auto* Expression) { Built->GetExpressionCollection().AddExpression(Expression); return Expression; };
			auto Scalar = [&](const TCHAR* Name, float Value)
			{
				UMaterialExpressionScalarParameter* P = Add(NewObject<UMaterialExpressionScalarParameter>(Built));
				P->ParameterName = Name;
				P->DefaultValue = Value;
				return P;
			};
			auto Vector = [&](const TCHAR* Name, const FLinearColor& Value)
			{
				UMaterialExpressionVectorParameter* P = Add(NewObject<UMaterialExpressionVectorParameter>(Built));
				P->ParameterName = Name;
				P->DefaultValue = Value;
				return P;
			};
			UMaterialExpressionTextureObjectParameter* Tex = Add(NewObject<UMaterialExpressionTextureObjectParameter>(Built));
			Tex->ParameterName = TEXT("BaseTexture");
			Tex->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
			Input(TEXT("Tex"), Tex);
			Input(TEXT("UV"), Add(NewObject<UMaterialExpressionTextureCoordinate>(Built)));
			Input(TEXT("BoxMin"), Vector(TEXT("BoxMin"), FLinearColor(0.6f, 0.1f, 0.f, 0.f)));
			Input(TEXT("BoxMax"), Vector(TEXT("BoxMax"), FLinearColor(0.97f, 0.9f, 0.f, 0.f)));
			Input(TEXT("Clear"), Vector(TEXT("Clear"), FLinearColor(0.3f, 0.5f, 0.f, 0.f)));
			Input(TEXT("Ink"), Vector(TEXT("Ink"), FLinearColor(0.012f, 0.03f, 0.02f, 1.f)));
			Input(TEXT("FlipV"), Scalar(TEXT("FlipV"), 0.f));
			Input(TEXT("Minutes"), Scalar(TEXT("Minutes"), 615.f));
			Input(TEXT("Backlight"), Scalar(TEXT("Backlight"), 25.f));
			Custom->RebuildOutputs();
			Built->GetEditorOnlyData()->BaseColor.Connect(0, Custom);
			Built->GetEditorOnlyData()->EmissiveColor.Connect(1, Custom);
			Built->GetEditorOnlyData()->Roughness.Connect(0, Scalar(TEXT("Roughness"), 0.35f));
			Built->PreEditChange(nullptr);
			Built->PostEditChange();
			Material = Built;
		}
#endif
		if (Material)
		{
			Cached.Reset(Material);
		}
		return Material;
	}
}
