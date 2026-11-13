// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseDriveHud.h"

#include "CanvasItem.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruiseRouteSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Knowledge/SimCruisePlaceGraphSubsystem.h"
#include "Live/SimCruiseLiveWorldSubsystem.h"
#include "Rendering/SlateRenderer.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"

using namespace SimCruiseTheme;

static TAutoConsoleVariable<int32> CVarHudLook(
	TEXT("simcruise.Hud.Look"), 1,
	TEXT("1: the guide-sign HUD with the GPS (2.0.2). 0: the earlier HUD."));

namespace
{
	FLinearColor A(FLinearColor C, float Alpha) { C.A *= Alpha; return C; }

	FCanvasUVTri Tri(const FVector2D& P0, const FVector2D& P1, const FVector2D& P2, const FLinearColor& Color)
	{
		FCanvasUVTri T;
		T.V0_Pos = P0; T.V1_Pos = P1; T.V2_Pos = P2;
		T.V0_UV = T.V1_UV = T.V2_UV = FVector2D::ZeroVector;
		T.V0_Color = T.V1_Color = T.V2_Color = Color;
		return T;
	}

	/** Clip segment A-B to a circle; false when nothing is inside. */
	bool ClipToCircle(FVector2D& A, FVector2D& B, const FVector2D& C, double R)
	{
		const FVector2D D = B - A, F = A - C;
		const double a = D.SizeSquared(), b = 2.0 * FVector2D::DotProduct(F, D), c = F.SizeSquared() - R * R;
		if (a < 1e-9) return c <= 0.0;
		const double Disc = b * b - 4.0 * a * c;
		if (Disc <= 0.0) return false;
		const double Sq = FMath::Sqrt(Disc);
		const double T0 = FMath::Max((-b - Sq) / (2.0 * a), 0.0), T1 = FMath::Min((-b + Sq) / (2.0 * a), 1.0);
		if (T0 >= T1) return false;
		const FVector2D A0 = A;
		A = A0 + D * T0;
		B = A0 + D * T1;
		return true;
	}

	bool IsTurn(ESimCruiseTurn T)
	{
		return T != ESimCruiseTurn::Continue && T != ESimCruiseTurn::Depart && T != ESimCruiseTurn::Arrive;
	}

	bool IsRightTurn(ESimCruiseTurn T)
	{
		return T == ESimCruiseTurn::Right || T == ESimCruiseTurn::SlightRight || T == ESimCruiseTurn::SharpRight;
	}
}

bool FSimCruiseDriveHud::IsEnabled()
{
	return CVarHudLook.GetValueOnGameThread() != 0;
}

FDateTime FSimCruiseDriveHud::LocalTime(const UWorld* World, double Lon)
{
	FDateTime Utc = FDateTime::UtcNow();
	if (const USimCruiseEnvironmentSubsystem* Env = USimCruiseEnvironmentSubsystem::Get(World))
	{
		if (const ASimCruiseEnvironment* Sky = Env->GetEnvironment()) Utc = Sky->GetUtcTime();
	}
	int32 Offset = FMath::RoundToInt(Lon / 15.0) * 3600;
	if (const USimCruiseLiveWorldSubsystem* Live = USimCruiseLiveWorldSubsystem::Get(World))
	{
		if (Live->GetWeather().bValid) Offset = Live->GetWeather().UtcOffsetSeconds;
	}
	return Utc + FTimespan::FromSeconds(Offset);
}

// ------------------------------------------------------------------ canvas helpers

void FSimCruiseDriveHud::Rect(float X, float Y, float W, float H, const FLinearColor& Color)
{
	FCanvasTileItem Item(FVector2D(X, Y), FVector2D(W, H), Color);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void FSimCruiseDriveHud::RoundRect(float X, float Y, float W, float H, float R, const FLinearColor& Color)
{
	R = FMath::Min(R, FMath::Min(W, H) * 0.5f);
	TArray<FCanvasUVTri> Tris;
	auto Box = [&](float X0, float Y0, float X1, float Y1)
	{
		Tris.Add(Tri(FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), Color));
		Tris.Add(Tri(FVector2D(X0, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1), Color));
	};
	Box(X + R, Y, X + W - R, Y + H);
	Box(X, Y + R, X + R, Y + H - R);
	Box(X + W - R, Y + R, X + W, Y + H - R);
	const FVector2D Corners[4] = { FVector2D(X + R, Y + R), FVector2D(X + W - R, Y + R), FVector2D(X + W - R, Y + H - R), FVector2D(X + R, Y + H - R) };
	for (int32 k = 0; k < 4; ++k)
	{
		const double Start = UE_DOUBLE_PI + k * UE_DOUBLE_HALF_PI;
		for (int32 i = 0; i < 6; ++i)
		{
			const double T0 = Start + UE_DOUBLE_HALF_PI * i / 6.0, T1 = Start + UE_DOUBLE_HALF_PI * (i + 1) / 6.0;
			Tris.Add(Tri(Corners[k], Corners[k] + FVector2D(FMath::Cos(T0), FMath::Sin(T0)) * R, Corners[k] + FVector2D(FMath::Cos(T1), FMath::Sin(T1)) * R, Color));
		}
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void FSimCruiseDriveHud::Disc(const FVector2D& C, float R, const FLinearColor& Color, int32 Segments)
{
	TArray<FCanvasUVTri> Tris;
	for (int32 i = 0; i < Segments; ++i)
	{
		const double T0 = UE_DOUBLE_TWO_PI * i / Segments, T1 = UE_DOUBLE_TWO_PI * (i + 1) / Segments;
		Tris.Add(Tri(C, C + FVector2D(FMath::Cos(T0), FMath::Sin(T0)) * R, C + FVector2D(FMath::Cos(T1), FMath::Sin(T1)) * R, Color));
	}
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void FSimCruiseDriveHud::Quad(const FVector2D& Q0, const FVector2D& Q1, const FVector2D& Q2, const FVector2D& Q3, const FLinearColor& Color)
{
	TArray<FCanvasUVTri> Tris = { Tri(Q0, Q1, Q2, Color), Tri(Q0, Q2, Q3, Color) };
	FCanvasTriangleItem Item(Tris, GWhiteTexture);
	Item.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Item);
}

void FSimCruiseDriveHud::Line(const FVector2D& P0, const FVector2D& P1, const FLinearColor& Color, float Thickness)
{
	// A quad, so a thick line has square ends and no gaps at joins under round caps.
	const FVector2D D = (P1 - P0).GetSafeNormal();
	if (D.IsNearlyZero()) return;
	const FVector2D N(-D.Y * Thickness * 0.5, D.X * Thickness * 0.5);
	Quad(P0 + N, P1 + N, P1 - N, P0 - N, Color);
}

void FSimCruiseDriveHud::Strokes(const TArray<TArray<FVector2D>>& In, const FVector2D& Origin, float Size, const FLinearColor& Color, float Thickness)
{
	for (const TArray<FVector2D>& Stroke : In)
	{
		for (int32 i = 0; i + 1 < Stroke.Num(); ++i)
		{
			Line(Origin + Stroke[i] * Size, Origin + Stroke[i + 1] * Size, Color, Thickness);
		}
		// Round the joins.
		for (int32 i = 1; i + 1 < Stroke.Num(); ++i) Disc(Origin + Stroke[i] * Size, Thickness * 0.5f, Color, 10);
	}
}

namespace SimCruiseHudFonts
{
	UFont* Get(EWeight Weight)
	{
		static UFont* Fonts[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };
		UFont*& Slot = Fonts[static_cast<int32>(Weight)];
		if (!Slot)
		{
			Slot = NewObject<UFont>(GetTransientPackage());
			// Kept for the whole run: the HUD draws with it every frame.
			Slot->AddToRoot();
			Slot->FontCacheType = EFontCacheType::Runtime;
			Slot->LegacyFontSize = 24;
			if (HasOverpass())
			{
				Slot->CompositeFont = FCompositeFont(FName(TEXT("OverpassHud")), FontPath(Weight), EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			}
			else if (const UFont* Roboto = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")))
			{
				Slot->CompositeFont = Roboto->CompositeFont;
			}
		}
		return Slot;
	}
}

namespace
{
	/** The canvas scale for a text of Px pixels at 1080p already multiplied by the HUD scale (24 pt base, 0.75 pt a pixel). */
	float FontScale(float ScaledPx) { return ScaledPx * 0.75f / 24.f; }
}

FVector2D FSimCruiseDriveHud::Measure(const FString& Text, EWeight Weight, float Px) const
{
	if (Text.IsEmpty() || !Canvas) return FVector2D::ZeroVector;
	float W = 0.f, H = 0.f;
	const float K = FontScale(P(Px));
	Canvas->TextSize(SimCruiseHudFonts::Get(Weight), Text, W, H, K, K);
	return FVector2D(W, H);
}

float FSimCruiseDriveHud::TextAt(const FString& Text, EWeight Weight, float Px, float X, float Y, const FLinearColor& Color, int32 Align, bool bTabular, bool bShadow)
{
	if (Text.IsEmpty() || !Canvas) return 0.f;
	UFont* HudFont = SimCruiseHudFonts::Get(Weight);
	const float K = FontScale(P(Px));
	auto Item = [&](const FString& Part, float AtX)
	{
		FCanvasTextItem T(FVector2D(AtX, Y), FText::FromString(Part), HudFont, Color);
		T.Scale = FVector2D(K, K);
		if (bShadow) T.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.5f * Color.A), FVector2D(0.f, P(2.f)));
		Canvas->DrawItem(T);
	};
	if (!bTabular)
	{
		const float W = Measure(Text, Weight, Px).X;
		const float Left = Align == 0 ? X : Align == 1 ? X - W * 0.5f : X - W;
		Item(Text, Left);
		return W;
	}
	// One width for every digit, so the speed does not jitter as it changes.
	const float Digit = Measure(TEXT("0"), Weight, Px).X;
	float W = 0.f;
	for (const TCHAR Ch : Text) W += FChar::IsDigit(Ch) ? Digit : Measure(FString(1, &Ch), Weight, Px).X;
	float At = Align == 0 ? X : Align == 1 ? X - W * 0.5f : X - W;
	for (const TCHAR Ch : Text)
	{
		const FString One(1, &Ch);
		const float Own = Measure(One, Weight, Px).X;
		const float Cell = FChar::IsDigit(Ch) ? Digit : Own;
		Item(One, At + (Cell - Own) * 0.5f);
		At += Cell;
	}
	return W;
}

// ------------------------------------------------------------------ the frame

void FSimCruiseDriveHud::Draw(ASimCruiseHUD& Hud, float DeltaTimeS)
{
	Canvas = Hud.Canvas;
	if (!Canvas) return;
	Scale = Canvas->ClipY / 1080.f;
	Clock += DeltaTimeS;
	UWorld* World = Hud.GetWorld();
	const USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(World);
	const bool bRoute = Gps && Gps->HasRoute();
	RouteAlpha = bRoute ? FMath::Min(1.f, RouteAlpha + DeltaTimeS * 2.f) : 0.f;
	const bool bShow = Hud.bHasVehicle && Hud.Level != ESimCruiseHudLevel::Off;

	if (bShow)
	{
		float Alpha = 1.f;
		if (Hud.Level == ESimCruiseHudLevel::Glance)
		{
			const bool bMoving = Hud.Mph >= SimCruiseHudRules::SpeedWakeMph;
			const bool bAwake = Hud.WakeTime > 0.f || !bMoving || !Hud.Message.IsEmpty();
			Hud.GlanceAlphaValue = SimCruiseHudRules::GlanceAlpha(Hud.GlanceAlphaValue, bAwake, DeltaTimeS);
			Alpha = Hud.GlanceAlphaValue;
		}
		if (bRoute) DrawRoadGuides(Hud, *Gps);
		DrawSpeed(Hud, Alpha);
		// The map stays clear while a route is followed.
		DrawRoundMap(Hud, Gps, bRoute ? 1.f : Alpha);
	}

	// A menu covers the top of the screen: the turn sign waits.
	const ASimCruisePlayerController* Driver = Cast<ASimCruisePlayerController>(Hud.PlayerOwner);
	if (Driver && Driver->IsOverlayOpen()) return;
	float TopY = P(28.f);
	if (Gps && !Gps->GetRoute().GetArrivalMessage().IsEmpty())
	{
		DrawArrival(Gps->GetRoute().GetArrivalMessage());
		TopY += P(150.f);
	}
	else if (bRoute && Hud.Level != ESimCruiseHudLevel::Off)
	{
		DrawTurnSign(*Gps);
		TopY += P(150.f);
	}
	if (Gps && !Gps->GetStatus().IsEmpty())
	{
		DrawStatus(Gps->GetStatus(), TopY);
		TopY += P(58.f);
	}
	if (!Hud.Message.IsEmpty())
	{
		DrawMessage(Hud.Message, TopY);
	}
}

// ------------------------------------------------------------------ speed, gear, rev bar

void FSimCruiseDriveHud::DrawSpeed(ASimCruiseHUD& Hud, float Alpha)
{
	const bool bFull = Hud.Level == ESimCruiseHudLevel::Full;
	const float X = P(56.f);
	const float Bottom = Canvas->ClipY - P(48.f);
	const float CardW = P(bFull ? 720.f : 470.f), CardH = P(bFull ? 236.f : 200.f);
	const float CardY = Bottom - CardH;
	RoundRect(X - P(24.f), CardY, CardW, CardH, P(16.f), A(Glass(0.55f), Alpha));

	// The number: 132 px Overpass Black, three digit cells, right-aligned.
	const float NumberRight = X + Measure(TEXT("000"), EWeight::Black, 132.f).X;
	const float NumberY = CardY - P(6.f);
	TextAt(FString::FromInt(Hud.Mph), EWeight::Black, 132.f, NumberRight, NumberY, A(White(), Alpha), 2, true);
	const float SideX = NumberRight + P(18.f);
	TextAt(SimCruiseHudRules::GearText(Hud.Gear, Hud.bManual), EWeight::ExtraBold, 56.f, SideX, CardY + P(26.f), A(Amber(), Alpha));
	TextAt(TEXT("mph"), EWeight::Bold, 26.f, SideX + P(2.f), CardY + P(94.f), A(Mist(), Alpha));
	if (Hud.bAbs)
	{
		const float W = Measure(TEXT("ABS"), EWeight::Black, 20.f).X + P(16.f);
		RoundRect(SideX + P(80.f), CardY + P(38.f), W, P(30.f), P(6.f), A(Amber(), Alpha));
		TextAt(TEXT("ABS"), EWeight::Black, 20.f, SideX + P(88.f), CardY + P(38.f), A(GuideDeep(), Alpha), 0, false, false);
	}

	// The rev bar: 360 px, white fill, the red zone from the shift point to the redline.
	const float BarX = X, BarY = CardY + P(160.f), BarW = P(360.f), BarH = P(12.f);
	const double Redline = FMath::Max(Hud.RedlineRpm, 1000.0);
	const float Fill = static_cast<float>(FMath::Clamp(Hud.Rpm / Redline, 0.0, 1.0));
	const float Zone = static_cast<float>(FMath::Clamp((Hud.UpshiftRpm - 300.0) / Redline, 0.5, 0.97));
	RoundRect(BarX, BarY, BarW, BarH, BarH * 0.5f, A(FLinearColor(1.f, 1.f, 1.f, 0.18f), Alpha));
	RoundRect(BarX + BarW * Zone, BarY, BarW * (1.f - Zone), BarH, BarH * 0.5f, A(FLinearColor(RedZone().R, RedZone().G, RedZone().B, 0.45f), Alpha));
	if (Fill > 0.01f)
	{
		const FLinearColor FillColor = Fill >= Zone ? RedZone() : White();
		RoundRect(BarX, BarY, BarW * Fill, BarH, BarH * 0.5f, A(FillColor, Alpha));
	}
	TextAt(FString::Printf(TEXT("%d rpm"), FMath::RoundToInt(Hud.Rpm / 50.0) * 50), EWeight::SemiBold, 20.f, BarX + BarW + P(14.f), BarY - P(8.f), A(Mist(), Alpha));

	if (bFull)
	{
		// Full: the car and the gearbox, and the g-g meter with the car's limits.
		const FString Mode = Hud.bManual ? TEXT("Manual") : TEXT("Automatic");
		TextAt(FString::Printf(TEXT("%s, %s"), *Hud.CarName, *Mode), EWeight::SemiBold, 21.f, BarX, BarY + P(28.f), A(White(), Alpha));
		Hud.DrawGMeter(FVector2D(X - P(24.f) + CardW - P(96.f), CardY + CardH * 0.5f), P(72.f), Alpha);
	}
}

// ------------------------------------------------------------------ the turn sign

void FSimCruiseDriveHud::DrawTurnSign(const USimCruiseRouteSubsystem& Gps)
{
	const FSimCruiseRoute& Route = Gps.GetRoute();
	const FSimCruiseRouteProgress& Progress = Route.GetProgress();
	const FSimCruiseRouteStep* Next = Route.GetStep(Progress.NextStep);
	if (!Next) return;
	const float Alpha = RouteAlpha;
	FString Number, Unit;
	SimCruiseRouteText::DistanceParts(Progress.ToNextStepM, Number, Unit);
	const FString Instruction = SimCruiseRouteText::Instruction(Next->Turn, Next->Street);
	const FSimCruiseRouteStep* Then = Route.GetThenStep(500.0);

	const float H = P(132.f);
	const float ArrowW = H;
	const float TextW = FMath::Max(Measure(Instruction, EWeight::Bold, 32.f).X, Measure(Number + TEXT(" ") + Unit, EWeight::ExtraBold, 56.f).X) + P(56.f);
	const FString ThenStreet = Then ? (Then->Street.IsEmpty() ? SimCruiseRouteText::TurnVerb(Then->Turn) : Then->Street) : FString();
	const float ThenW = Then ? P(64.f) + P(44.f) + Measure(ThenStreet, EWeight::Bold, 26.f).X + P(40.f) : 0.f;
	const float W = FMath::Min(ArrowW + TextW + ThenW, Canvas->ClipX - P(80.f));
	const float X = (Canvas->ClipX - W) * 0.5f, Y = P(28.f);

	RoundRect(X, Y, W, H, P(14.f), A(Guide(), Alpha));
	if (Then) RoundRect(X + W - ThenW, Y, ThenW, H, P(14.f), A(GuideDeep(), Alpha));
	// The white border inside the edge of a guide sign.
	const float In = P(7.f), T = P(3.f);
	const FLinearColor Border = A(White(), Alpha);
	Line(FVector2D(X + In, Y + In), FVector2D(X + W - In, Y + In), Border, T);
	Line(FVector2D(X + In, Y + H - In), FVector2D(X + W - In, Y + H - In), Border, T);
	Line(FVector2D(X + In, Y + In - T * 0.5f), FVector2D(X + In, Y + H - In + T * 0.5f), Border, T);
	Line(FVector2D(X + W - In, Y + In - T * 0.5f), FVector2D(X + W - In, Y + H - In + T * 0.5f), Border, T);

	TArray<TArray<FVector2D>> Arrow;
	TurnStrokes(Next->Turn, Arrow);
	Strokes(Arrow, FVector2D(X + P(22.f), Y + P(22.f)), H - P(44.f), Border, P(11.f));

	const float TX = X + ArrowW + P(8.f);
	const float NumW = TextAt(Number, EWeight::ExtraBold, 56.f, TX, Y + P(14.f), Border, 0, true);
	TextAt(Unit, EWeight::Bold, 30.f, TX + NumW + P(8.f), Y + P(36.f), Border);
	TextAt(Instruction, EWeight::Bold, 32.f, TX, Y + P(78.f), Border);

	if (Then)
	{
		const float CX = X + W - ThenW + P(20.f);
		TextAt(TEXT("Then"), EWeight::SemiBold, 22.f, CX, Y + P(50.f), A(FLinearColor(1.f, 1.f, 1.f, 0.8f), Alpha));
		TArray<TArray<FVector2D>> Small;
		TurnStrokes(Then->Turn, Small);
		Strokes(Small, FVector2D(CX + P(58.f), Y + P(44.f)), P(44.f), Border, P(5.f));
		TextAt(ThenStreet, EWeight::Bold, 26.f, CX + P(112.f), Y + P(48.f), Border);
	}
}

void FSimCruiseDriveHud::DrawArrival(const FString& Text)
{
	const float H = P(110.f);
	const float W = Measure(Text, EWeight::ExtraBold, 40.f).X + P(150.f);
	const float X = (Canvas->ClipX - W) * 0.5f, Y = P(28.f);
	RoundRect(X, Y, W, H, P(14.f), Guide());
	const float In = P(7.f), T = P(3.f);
	Line(FVector2D(X + In, Y + In), FVector2D(X + W - In, Y + In), White(), T);
	Line(FVector2D(X + In, Y + H - In), FVector2D(X + W - In, Y + H - In), White(), T);
	Line(FVector2D(X + In, Y + In), FVector2D(X + In, Y + H - In), White(), T);
	Line(FVector2D(X + W - In, Y + In), FVector2D(X + W - In, Y + H - In), White(), T);
	TArray<TArray<FVector2D>> Flag;
	IconStrokes(EIcon::Flag, Flag);
	Strokes(Flag, FVector2D(X + P(30.f), Y + P(25.f)), P(60.f), Amber(), P(6.f));
	TextAt(Text, EWeight::ExtraBold, 40.f, X + P(110.f), Y + P(30.f), White());
}

void FSimCruiseDriveHud::DrawStatus(const FString& Text, float Y)
{
	const FVector2D Size = Measure(Text, EWeight::SemiBold, 24.f);
	const float W = Size.X + P(48.f), H = P(46.f);
	const float X = (Canvas->ClipX - W) * 0.5f;
	RoundRect(X, Y, W, H, H * 0.5f, Glass());
	Disc(FVector2D(X + P(20.f), Y + H * 0.5f), P(6.f), A(Course(), 0.6f + 0.4f * FMath::Abs(FMath::Sin(static_cast<float>(Clock) * 3.f))));
	TextAt(Text, EWeight::SemiBold, 24.f, X + P(34.f), Y + P(8.f), White());
}

void FSimCruiseDriveHud::DrawMessage(const FString& Text, float Y)
{
	const FVector2D Size = Measure(Text, EWeight::SemiBold, 26.f);
	const float W = Size.X + P(48.f), H = P(50.f);
	const float X = (Canvas->ClipX - W) * 0.5f;
	RoundRect(X, Y, W, H, P(12.f), Glass());
	TextAt(Text, EWeight::SemiBold, 26.f, X + P(24.f), Y + P(9.f), White());
}

// ------------------------------------------------------------------ chevrons and the arrow board

void FSimCruiseDriveHud::GatherStations(ASimCruiseHUD& Hud, const FVector2D& CarM)
{
	if (Clock - StationsTime < 0.5 && FVector2D::Distance(CarM, StationsAt) < 15.0) return;
	StationsTime = Clock;
	StationsAt = CarM;
	Stations.Reset();
	const ASimCruiseGameMode* Mode = Hud.GetWorld() ? Hud.GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
	const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	if (!Stream || !Stream->GetFrame().IsValid()) return;
	const FIntPoint Here = Stream->GetFrame().TileAtWorldCm(FVector(CarM * 100.0, 0.0));
	for (int32 dx = -1; dx <= 1; ++dx)
	{
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			const ASimCruiseWorldTile* Tile = Stream->GetTile(Here + FIntPoint(dx, dy));
			const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
			if (!Build) continue;
			const FVector Origin = Tile->GetActorLocation();
			const FVector2D OriginM = FVector2D(Origin.X, Origin.Y) / 100.0;
			for (const FSimCruiseRibbon& Ribbon : Build->Roads.Ribbons)
			{
				for (const FSimCruiseRibbonStation& St : Ribbon.St)
				{
					const FVector2D C = OriginM + FVector2D(St.C.X, St.C.Y);
					if (FVector2D::DistSquared(C, CarM) > 160.0 * 160.0) continue;
					FStation S;
					S.St = St;
					S.C = C;
					S.TileZCm = Origin.Z;
					Stations.Add(S);
				}
			}
		}
	}
}

bool FSimCruiseDriveHud::RoadHeightCm(const FVector2D& PointM, double& OutZCm) const
{
	const FStation* Best = nullptr;
	double BestD = TNumericLimits<double>::Max();
	for (const FStation& S : Stations)
	{
		// Across the road counts less than along it: a junction's crossing road is not "under" the point.
		const FVector2D D = PointM - S.C;
		const double Lat = FVector2D::DotProduct(D, FVector2D(S.St.R.X, S.St.R.Y));
		const double Lon = FVector2D::DotProduct(D, FVector2D(S.St.T.X, S.St.T.Y));
		const double Out = FMath::Max(0.0, FMath::Abs(Lat) - S.St.HalfW);
		const double Score = Lon * Lon + Out * Out * 4.0;
		if (Score < BestD) { BestD = Score; Best = &S; }
	}
	if (!Best || BestD > 9.0 * 9.0) return false;
	const double L = FVector2D::DotProduct(PointM - Best->C, FVector2D(Best->St.R.X, Best->St.R.Y));
	OutZCm = Best->TileZCm + Best->St.Height(L) * 100.0;
	return true;
}

void FSimCruiseDriveHud::DrawRoadGuides(ASimCruiseHUD& Hud, const USimCruiseRouteSubsystem& Gps)
{
	const APlayerController* PC = Hud.PlayerOwner;
	const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
	FVector2D CarM;
	if (!Camera || !Gps.GetCarM(CarM)) return;
	const FSimCruiseRoute& Route = Gps.GetRoute();
	const FSimCruiseRouteProgress& Progress = Route.GetProgress();
	const FSimCruiseRouteLeg* Leg = Route.GetLeg();
	if (!Leg || Leg->CumM.Num() < 2) return;
	GatherStations(Hud, CarM);
	const FVector Eye = Camera->GetCameraLocation();
	const FVector Look = Camera->GetCameraRotation().Vector();
	auto Screen = [&](const FVector& WorldCm, FVector2D& Out)
	{
		if (FVector::DotProduct(WorldCm - Eye, Look) < 30.0) return false;
		const FVector S = Canvas->Project(WorldCm, false);
		Out = FVector2D(S.X, S.Y);
		return true;
	};

	// The driven lane: the car's own offset from the route line, smoothed and held inside the road.
	const float Dt = 1.f / 60.f;
	SmoothLateral += (static_cast<float>(Progress.LateralM) - SmoothLateral) * (1.f - FMath::Exp(-Dt / 0.4f));
	const double Lateral = FMath::Clamp(static_cast<double>(SmoothLateral), -6.0, 6.0);

	// Chevrons every 4 m of the route (fixed to the road like paint), 5 to 60 m ahead, fading.
	const double Along = Progress.AlongM;
	const double LegEnd = Leg->CumM.Last();
	for (double S = FMath::CeilToDouble((Along + 5.0) / 4.0) * 4.0; S <= Along + 60.0 && S <= LegEnd - 2.0; S += 4.0)
	{
		FVector2D M, Dir;
		if (!Route.PointAt(S, M, Dir)) break;
		const FVector2D Right(-Dir.Y, Dir.X);
		const FVector2D C = M + Right * Lateral;
		double Z = 0.0;
		if (!RoadHeightCm(C, Z)) continue;
		const double Ahead = S - Along;
		const float Alpha = RouteAlpha * 0.88f * static_cast<float>(FMath::Clamp((Ahead - 5.0) / 5.0, 0.0, 1.0) * FMath::Clamp((60.0 - Ahead) / 25.0, 0.0, 1.0));
		if (Alpha <= 0.01f) continue;
		const double Half = 0.85, Depth = 0.45, Thick = 0.38;
		auto W = [&](const FVector2D& P2) { return FVector(P2.X * 100.0, P2.Y * 100.0, Z + 4.0); };
		const FVector2D Tip = C + Dir * Depth, LeftBack = C - Dir * Depth - Right * Half, RightBack = C - Dir * Depth + Right * Half;
		FVector2D S0, S1, S2, S3, S4, S5;
		if (!Screen(W(LeftBack), S0) || !Screen(W(Tip), S1) || !Screen(W(Tip - Dir * Thick), S2) || !Screen(W(LeftBack - Dir * Thick), S3)
			|| !Screen(W(RightBack), S4) || !Screen(W(RightBack - Dir * Thick), S5)) continue;
		const FLinearColor Color = A(Course(), Alpha);
		Quad(S0, S1, S2, S3, Color);
		Quad(S1, S4, S5, S2, Color);
	}

	// The arrow board at the corner of the next turn, beyond the junction, facing the car.
	const FSimCruiseRouteStep* Next = Route.GetStep(Progress.NextStep);
	if (!Next || !IsTurn(Next->Turn) || Progress.ToNextStepM > 220.0) return;
	FVector2D TurnM, OutDir, InM, InDir;
	if (!Route.PointAt(Next->AtM, TurnM, OutDir) || !Route.PointAt(FMath::Max(Next->AtM - 12.0, 0.0), InM, InDir)) return;
	InDir = (TurnM - InM).GetSafeNormal();
	if (InDir.IsNearlyZero()) return;
	const FVector2D InRight(-InDir.Y, InDir.X);
	const FVector2D Board = TurnM + InDir * 9.0 + InRight * (IsRightTurn(Next->Turn) ? 1.5 : -1.5);
	double Z = 0.0;
	if (!RoadHeightCm(TurnM, Z)) return;
	const float Alpha = RouteAlpha * static_cast<float>(FMath::Clamp((220.0 - Progress.ToNextStepM) / 70.0, 0.0, 1.0));
	const double HalfW = 1.4, Bottom = Z + 110.0, Height = 140.0;
	auto Corner = [&](double U, double V) { const FVector2D P2 = Board + InRight * ((U - 0.5) * 2.0 * HalfW); return FVector(P2.X * 100.0, P2.Y * 100.0, Bottom + V * Height); };
	FVector2D BL, BR, TR, TL, PostTop, PostFoot;
	if (!Screen(Corner(0, 0), BL) || !Screen(Corner(1, 0), BR) || !Screen(Corner(1, 1), TR) || !Screen(Corner(0, 1), TL)
		|| !Screen(Corner(0.5, 0), PostTop) || !Screen(FVector(Board.X * 100.0, Board.Y * 100.0, Z), PostFoot)) return;
	Line(PostFoot, PostTop, A(FLinearColor(0.75f, 0.77f, 0.76f, 1.f), Alpha), FMath::Max(2.f, FVector2D::Distance(BL, BR) * 0.04f));
	Quad(BL, BR, TR, TL, A(Course(), Alpha));
	const float Edge = FMath::Max(1.5f, FVector2D::Distance(BL, BR) * 0.035f);
	const FLinearColor Ink = A(White(), Alpha);
	Line(BL, BR, Ink, Edge); Line(BR, TR, Ink, Edge); Line(TR, TL, Ink, Edge); Line(TL, BL, Ink, Edge);
	// Two chevrons pointing the way of the turn.
	const bool bRight = IsRightTurn(Next->Turn);
	for (int32 k = 0; k < 2; ++k)
	{
		const double U0 = 0.30 + 0.24 * k;
		const double Tip = bRight ? U0 + 0.14 : U0 - 0.14 + 0.28;
		const double Back = bRight ? U0 : U0 + 0.28;
		FVector2D P0, P1, P2;
		if (Screen(Corner(Back, 0.18), P0) && Screen(Corner(Tip, 0.5), P1) && Screen(Corner(Back, 0.82), P2))
		{
			Line(P0, P1, Ink, Edge * 2.2f);
			Line(P1, P2, Ink, Edge * 2.2f);
		}
	}
}

// ------------------------------------------------------------------ the round map

void FSimCruiseDriveHud::DrawRoundMap(ASimCruiseHUD& Hud, const USimCruiseRouteSubsystem* Gps, float Alpha)
{
	const ASimCruiseGameMode* Mode = Hud.GetWorld() ? Hud.GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
	const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	const APawn* Car = Hud.GetOwningPawn();
	if (!Stream || !Car || !Stream->GetFrame().IsValid()) return;
	const bool bRoute = Gps && Gps->HasRoute();
	const float R = P(165.f);
	const FVector2D Centre(Canvas->ClipX - P(64.f) - R, Canvas->ClipY - P(bRoute ? 128.f : 56.f) - R);
	Disc(Centre, R + P(4.f), A(FLinearColor(1.f, 1.f, 1.f, 0.55f), Alpha), 64);
	Disc(Centre, R, A(Glass(0.86f), Alpha), 64);

	static const float PxPerM[] = { 1.2f, 0.75f, 0.45f, 0.26f, 0.14f };
	const double S = PxPerM[FMath::Clamp(Hud.MinimapZoom, 0, 4)] * Scale;
	const FVector CarCm = Car->GetActorLocation();
	const FVector2D CarM(CarCm.X / 100.0, CarCm.Y / 100.0);
	const double Yaw = FMath::DegreesToRadians(Car->GetActorRotation().Yaw);
	const FVector2D F(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D Rt(-F.Y, F.X);
	// The car sits a little below the centre, so more of the road ahead shows.
	const FVector2D CarAt = Centre + FVector2D(0.0, R * 0.22);
	auto ToScreen = [&](const FVector2D& WorldM)
	{
		const FVector2D D = WorldM - CarM;
		return CarAt + FVector2D(FVector2D::DotProduct(D, Rt), -FVector2D::DotProduct(D, F)) * S;
	};
	const double Clip = R - P(5.f);
	const double RangeM = (R * 2.0) / S;
	const FIntPoint Here = Stream->GetFrame().TileAtWorldCm(CarCm);
	for (int32 dx = -1; dx <= 1; ++dx)
	{
		for (int32 dy = -1; dy <= 1; ++dy)
		{
			const ASimCruiseWorldTile* Tile = Stream->GetTile(Here + FIntPoint(dx, dy));
			const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
			if (!Build) continue;
			const FVector2D Origin = FVector2D(Tile->GetActorLocation().X, Tile->GetActorLocation().Y) / 100.0;
			for (const FSimCruiseRibbon& Ribbon : Build->Roads.Ribbons)
			{
				if (Ribbon.St.Num() < 2) continue;
				const bool bWide = Ribbon.St[0].HalfW * 2.0 > 9.0;
				const FLinearColor Color = A(bWide ? FLinearColor(0.80f, 0.83f, 0.80f, 0.95f) : FLinearColor(0.58f, 0.63f, 0.60f, 0.9f), Alpha);
				const float Thick = FMath::Clamp(static_cast<float>(Ribbon.St[0].HalfW * 2.0 * 0.45 * S), P(2.f), P(9.f));
				for (int32 i = 0; i + 1 < Ribbon.St.Num(); ++i)
				{
					const FVector2D WA = Origin + FVector2D(Ribbon.St[i].C.X, Ribbon.St[i].C.Y);
					if (FVector2D::DistSquared(WA, CarM) > RangeM * RangeM) continue;
					FVector2D Pa = ToScreen(WA), Pb = ToScreen(Origin + FVector2D(Ribbon.St[i + 1].C.X, Ribbon.St[i + 1].C.Y));
					if (!ClipToCircle(Pa, Pb, Centre, Clip)) continue;
					Line(Pa, Pb, Color, Thick);
				}
			}
		}
	}

	if (bRoute)
	{
		// The course: the rest of the active leg bright, the later legs dimmer.
		const FSimCruiseRoute& Route = Gps->GetRoute();
		const TArray<FSimCruiseRouteLeg>& Legs = Route.GetLegs();
		for (int32 L = Route.GetActiveLeg(); L < Legs.Num(); ++L)
		{
			const FSimCruiseRouteLeg& Leg = Legs[L];
			const bool bActive = L == Route.GetActiveLeg();
			const FLinearColor Color = A(Course(), (bActive ? 1.f : 0.55f) * RouteAlpha);
			for (int32 i = 0; i + 1 < Leg.M.Num(); ++i)
			{
				if (bActive && Leg.CumM[i + 1] < Route.GetProgress().AlongM) continue;
				FVector2D Pa = ToScreen(bActive && Leg.CumM[i] < Route.GetProgress().AlongM ? CarM : Leg.M[i]);
				FVector2D Pb = ToScreen(Leg.M[i + 1]);
				if (!ClipToCircle(Pa, Pb, Centre, Clip)) continue;
				Line(Pa, Pb, Color, P(8.f));
				Disc(Pb, P(4.f), Color, 10);
			}
		}
		// Stops: amber discs with their numbers.
		const TArray<FSimCruiseRouteStop>& Stops = Route.GetStops();
		for (int32 i = Route.GetActiveLeg() + 1; i < Stops.Num() && Legs.IsValidIndex(i - 1); ++i)
		{
			if (Legs[i - 1].M.Num() == 0) continue;
			FVector2D At = ToScreen(Legs[i - 1].M.Last());
			const FVector2D D = At - Centre;
			if (D.Size() > Clip - P(12.f)) At = Centre + D.GetSafeNormal() * (Clip - P(12.f));
			Disc(At, P(14.f), A(FLinearColor(0.06f, 0.07f, 0.08f, 0.9f), Alpha));
			Disc(At, P(11.f), A(Amber(), Alpha));
			TextAt(FString::FromInt(i), EWeight::Black, 16.f, At.X, At.Y - P(10.f), A(GuideDeep(), Alpha), 1, false, false);
		}
	}

	// The car: a white arrow pointing up.
	const FLinearColor Ink = A(White(), Alpha);
	const FVector2D Tip = CarAt + FVector2D(0.f, -P(15.f)), Left = CarAt + FVector2D(-P(11.f), P(11.f)), Right = CarAt + FVector2D(P(11.f), P(11.f)), Notch = CarAt + FVector2D(0.f, P(5.f));
	TArray<FCanvasUVTri> Arrow = { Tri(Tip, Right, Notch, Ink), Tri(Tip, Notch, Left, Ink) };
	FCanvasTriangleItem ArrowItem(Arrow, GWhiteTexture);
	ArrowItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(ArrowItem);
	// North on the rim.
	// World north is -y (y runs south).
	const FVector2D North = FVector2D(FVector2D::DotProduct(FVector2D(0.0, -1.0), Rt), -FVector2D::DotProduct(FVector2D(0.0, -1.0), F)).GetSafeNormal();
	const FVector2D NAt = Centre + North * (R - P(18.f));
	TextAt(TEXT("N"), EWeight::Black, 20.f, NAt.X, NAt.Y - P(12.f), A(Mist(), Alpha), 1);

	if (bRoute)
	{
		const FSimCruiseRoute& Route = Gps->GetRoute();
		const FSimCruiseRouteStop* Stop = Route.GetNextStop();
		const FSimCruiseRouteProgress& Progress = Route.GetProgress();
		const float TX = Centre.X + R, TY = Centre.Y + R + P(12.f);
		TextAt(SimCruiseRouteText::Distance(Progress.LegRemainingM), EWeight::ExtraBold, 40.f, TX, TY, White(), 2, true);
		double Lon = 0.0, Lat = 0.0;
		Gps->GetFrame().WorldCmToLonLat(CarCm, Lon, Lat);
		const FDateTime Arrive = LocalTime(Hud.GetWorld(), Lon) + FTimespan::FromSeconds(Progress.LegRemainingS);
		TextAt(FString::Printf(TEXT("to %s, %s"), Stop ? *Stop->Title : TEXT("the stop"), *SimCruiseRouteText::Clock(Arrive)), EWeight::SemiBold, 22.f, TX, TY + P(52.f), Mist(), 2);
	}
	if (!Hud.bOnTestPad) DrawPlaceCard(Hud, Centre.Y - R - P(22.f));
}

void FSimCruiseDriveHud::DrawPlaceCard(ASimCruiseHUD& Hud, float Bottom)
{
	const USimCruisePlaceGraphSubsystem* Graph = USimCruisePlaceGraphSubsystem::Get(Hud.GetWorld());
	FSimCruisePlaceCard Card;
	if (!Graph || !Graph->GetPlaceCard(Card)) return;
	const float Right = Canvas->ClipX - P(64.f);
	const FVector2D T = Measure(Card.Title, EWeight::ExtraBold, 26.f), L = Measure(Card.Line, EWeight::SemiBold, 20.f), N = Measure(Card.Note, EWeight::Regular, 17.f);
	const float W = FMath::Max3(T.X, L.X, N.X) + P(40.f);
	const float H = T.Y + L.Y + N.Y + P(30.f);
	RoundRect(Right - W, Bottom - H, W, H, P(12.f), A(Glass(), Card.Alpha));
	float Y = Bottom - H + P(12.f);
	TextAt(Card.Title, EWeight::ExtraBold, 26.f, Right - P(20.f), Y, A(White(), Card.Alpha), 2);
	Y += T.Y + P(2.f);
	TextAt(Card.Line, EWeight::SemiBold, 20.f, Right - P(20.f), Y, A(Amber(), Card.Alpha), 2);
	Y += L.Y + P(2.f);
	TextAt(Card.Note, EWeight::Regular, 17.f, Right - P(20.f), Y, A(Mist(), Card.Alpha), 2);
}
