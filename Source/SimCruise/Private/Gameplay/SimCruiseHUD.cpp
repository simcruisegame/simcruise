// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseHUD.h"

#include "CanvasItem.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePlayerController.h"

namespace SimCruiseHudRules
{
	ESimCruiseHudLevel Next(ESimCruiseHudLevel Level)
	{
		return static_cast<ESimCruiseHudLevel>((static_cast<uint8>(Level) + 1) % 3);
	}

	const TCHAR* Name(ESimCruiseHudLevel Level)
	{
		switch (Level)
		{
		case ESimCruiseHudLevel::Off: return TEXT("Off");
		case ESimCruiseHudLevel::Full: return TEXT("Full");
		default: return TEXT("Glance");
		}
	}

	FString GearText(int32 Gear, bool bManual)
	{
		if (Gear < 0) { return TEXT("R"); }
		if (Gear == 0) { return TEXT("N"); }
		return bManual ? FString::FromInt(Gear) : FString::Printf(TEXT("D%d"), Gear);
	}

	float GlanceAlpha(float Current, bool bAwake, float DeltaTimeS)
	{
		const float Target = bAwake ? 1.f : GlanceFaded;
		return Current + (Target - Current) * (1.f - FMath::Exp(-DeltaTimeS / GlanceFadeS));
	}
}

namespace
{
	const FLinearColor Ink(0.012f, 0.016f, 0.02f, 1.f);
	const FLinearColor Paper(0.93f, 0.95f, 0.94f, 1.f);
	const FLinearColor Lime(0.72f, 0.92f, 0.35f, 1.f);
	const FLinearColor Amber(1.f, 0.66f, 0.15f, 1.f);
	const FLinearColor Signal(1.f, 0.24f, 0.18f, 1.f);
	const FLinearColor Dim(0.62f, 0.66f, 0.66f, 1.f);

	FLinearColor WithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}
}

ASimCruiseHUD::ASimCruiseHUD()
{
	bShowHUD = true;
}

void ASimCruiseHUD::CycleLevel()
{
	SetLevel(SimCruiseHudRules::Next(Level));
	ShowMessage(FString::Printf(TEXT("HUD · %s"), SimCruiseHudRules::Name(Level)), 1.5f);
}

void ASimCruiseHUD::SetLevel(ESimCruiseHudLevel InLevel)
{
	Level = InLevel;
	GlanceAlphaValue = 1.f;
	WakeTime = SimCruiseHudRules::WakeS;
}

void ASimCruiseHUD::ShowMessage(const FString& Text, float Seconds)
{
	Message = Text;
	MessageTime = Seconds;
	if (!Text.IsEmpty())
	{
		WakeTime = FMath::Max(WakeTime, 3.f);
	}
}

void ASimCruiseHUD::SampleVehicle(float DeltaTimeS)
{
	const ASimCruisePlayerController* Controller = Cast<ASimCruisePlayerController>(PlayerOwner);
	bHasVehicle = Controller && Controller->HasVehicle();
	if (!bHasVehicle)
	{
		return;
	}
	const FSimCruiseDriveTelemetry& Telemetry = Controller->GetTelemetry();
	const FSimCruiseVehicleSetup& Setup = Controller->GetCachedVehicleSetup();
	Mph = FMath::RoundToInt(FMath::Abs(Telemetry.BodyVelocityMps.X) * 2.236936);
	Gear = Telemetry.Gear;
	bManual = Telemetry.bManualTransmission;
	Rpm = Telemetry.EngineSpeedRpm;
	UpshiftRpm = Setup.UpshiftRpm;
	RedlineRpm = Setup.RedlineRpm;
	SkidpadG = Setup.SkidpadG;
	BrakingG = Setup.BrakingG;
	CarName = Setup.DisplayName;
	bAbs = Controller->IsAbsActive();

	if (const APawn* Pawn = Controller->GetPawn())
	{
		const FTransform Body = Pawn->GetActorTransform();
		GMeter.Update(Body, Body.GetRotation().RotateVector(Telemetry.BodyVelocityMps), DeltaTimeS);
	}
	TrailClock += DeltaTimeS;
	if (TrailClock >= 1.f / 30.f)
	{
		TrailClock = 0.f;
		GTrail.Add(GMeter.Get());
		if (GTrail.Num() > 60)
		{
			GTrail.RemoveAt(0);
		}
	}
	if (FMath::Abs(Mph - ShownMph) >= SimCruiseHudRules::SpeedWakeMph)
	{
		ShownMph = Mph;
		WakeTime = FMath::Max(WakeTime, SimCruiseHudRules::WakeS);
	}
}

void ASimCruiseHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	const float DeltaTimeS = LastDrawTime < 0.0 ? 1.f / 60.f : static_cast<float>(FMath::Clamp(Now - LastDrawTime, 0.0, 0.25));
	LastDrawTime = Now;
	if (MessageTime > 0.f)
	{
		MessageTime -= DeltaTimeS;
		if (MessageTime <= 0.f)
		{
			Message.Reset();
		}
	}
	WakeTime = FMath::Max(0.f, WakeTime - DeltaTimeS);

	SampleVehicle(DeltaTimeS);
	const float Scale = Canvas->ClipY / 900.f;
	if (bHasVehicle && Level == ESimCruiseHudLevel::Glance)
	{
		DrawGlance(Scale, DeltaTimeS);
	}
	else if (bHasVehicle && Level == ESimCruiseHudLevel::Full)
	{
		DrawFull(Scale);
	}
	DrawMessage(Scale);
}

namespace
{
	constexpr float HudFontBasePoints = 24.f;
}

UFont* ASimCruiseHUD::HudFont(bool bBold)
{
	TObjectPtr<UFont>& Slot = bBold ? RobotoBold : RobotoRegular;
	if (!Slot)
	{
		if (UFont* Roboto = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto")))
		{
			Slot = DuplicateObject<UFont>(Roboto, this);
			Slot->LegacyFontName = bBold ? FName(TEXT("Bold")) : FName(TEXT("Regular"));
			Slot->LegacyFontSize = static_cast<int32>(HudFontBasePoints);
		}
		else
		{
			Slot = GEngine->GetMediumFont();
		}
	}
	return Slot;
}

FVector2D ASimCruiseHUD::MeasureText(const FString& Text, bool bBold, float PointSize)
{
	float W = 0.f;
	float H = 0.f;
	const float TextScale = PointSize / HudFontBasePoints;
	Canvas->TextSize(HudFont(bBold), Text, W, H, TextScale, TextScale);
	return FVector2D(W, H);
}

void ASimCruiseHUD::DrawTextAt(const FString& Text, bool bBold, float PointSize, float X, float Y, const FLinearColor& Color, bool bRightAlign)
{
	UFont* Font = HudFont(bBold);
	if (!Font || Text.IsEmpty())
	{
		return;
	}
	if (bRightAlign)
	{
		X -= MeasureText(Text, bBold, PointSize).X;
	}
	const float TextScale = PointSize / HudFontBasePoints;
	FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), Font, Color);
	Item.Scale = FVector2D(TextScale, TextScale);
	Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, 0.45f * Color.A), FVector2D(1.f, 1.f));
	Canvas->DrawItem(Item);
}

void ASimCruiseHUD::DrawRpmBar(float X, float Y, float W, float H, float Alpha)
{
	const double Fraction = FMath::Clamp(Rpm / FMath::Max(RedlineRpm, 1000.0), 0.0, 1.0);
	const FLinearColor Fill = Rpm >= RedlineRpm - 60.0 ? Signal : (Rpm >= UpshiftRpm - 400.0 ? Amber : Paper);
	FCanvasTileItem Track(FVector2D(X, Y), FVector2D(W, H), WithAlpha(FLinearColor(1.f, 1.f, 1.f, 0.14f), Alpha));
	Track.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Track);
	FCanvasTileItem Bar(FVector2D(X, Y), FVector2D(W * Fraction, H), WithAlpha(Fill, 0.9f * Alpha));
	Bar.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Bar);
	// The automatic's upshift point.
	const float Tick = X + W * static_cast<float>(FMath::Clamp(UpshiftRpm / FMath::Max(RedlineRpm, 1000.0), 0.0, 1.0));
	FCanvasTileItem Mark(FVector2D(Tick - 1.f, Y - 2.f), FVector2D(2.f, H + 4.f), WithAlpha(Amber, 0.75f * Alpha));
	Mark.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Mark);
}

void ASimCruiseHUD::DrawGMeter(const FVector2D& Centre, float Radius, float Alpha)
{
	// The plot spans +-1.2 g; the ellipse marks the car's published limits:
	// skidpad g sideways, the 70-0 mph stop forward and back.
	const float PerG = Radius / 1.2f;
	auto Line = [this](const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness)
	{
		FCanvasLineItem Item(A, B);
		Item.SetColor(Color);
		Item.LineThickness = Thickness;
		Canvas->DrawItem(Item);
	};
	const FLinearColor Faint = WithAlpha(FLinearColor(1.f, 1.f, 1.f, 0.22f), Alpha);
	const float Arm = 0.35f * Radius;
	Line(Centre - FVector2D(Arm, 0.f), Centre + FVector2D(Arm, 0.f), Faint, 1.f);
	Line(Centre - FVector2D(0.f, Arm), Centre + FVector2D(0.f, Arm), Faint, 1.f);
	constexpr int32 Segments = 64;
	FVector2D Previous;
	for (int32 I = 0; I <= Segments; ++I)
	{
		const double T = UE_DOUBLE_TWO_PI * I / Segments;
		const FVector2D P = Centre + FVector2D(FMath::Cos(T) * SkidpadG * PerG, FMath::Sin(T) * BrakingG * PerG);
		if (I > 0)
		{
			Line(Previous, P, WithAlpha(FLinearColor(1.f, 1.f, 1.f, 0.5f), Alpha), 1.2f);
		}
		Previous = P;
	}
	// Two-second trace: x = lateral (right +), up = accelerating.
	for (int32 I = 1; I < GTrail.Num(); ++I)
	{
		const float Fade = static_cast<float>(I) / GTrail.Num();
		const FVector2D A = Centre + FVector2D(GTrail[I - 1].Y, -GTrail[I - 1].X) * PerG;
		const FVector2D B = Centre + FVector2D(GTrail[I].Y, -GTrail[I].X) * PerG;
		Line(A, B, WithAlpha(Lime, 0.6f * Fade * Alpha), 1.5f);
	}
	const FVector2D G = GMeter.Get();
	const FVector2D Dot = Centre + FVector2D(FMath::Clamp(G.Y, -1.2, 1.2), -FMath::Clamp(G.X, -1.2, 1.2)) * PerG;
	const float DotSize = FMath::Max(5.f, Radius * 0.14f);
	FCanvasTileItem DotItem(Dot - FVector2D(DotSize, DotSize) * 0.5f, FVector2D(DotSize, DotSize), WithAlpha(Lime, Alpha));
	DotItem.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(DotItem);
}

void ASimCruiseHUD::DrawGlance(float Scale, float DeltaTimeS)
{
	const bool bMoving = Mph >= SimCruiseHudRules::SpeedWakeMph;
	const bool bAwake = WakeTime > 0.f || !bMoving || !Message.IsEmpty();
	GlanceAlphaValue = SimCruiseHudRules::GlanceAlpha(GlanceAlphaValue, bAwake, DeltaTimeS);
	const float Alpha = GlanceAlphaValue;

	const float W = 292.f * Scale;
	const float H = 104.f * Scale;
	const float X = 28.f * Scale;
	const float Y = Canvas->ClipY - 28.f * Scale - H;
	FCanvasTileItem Panel(FVector2D(X, Y), FVector2D(W, H), WithAlpha(FLinearColor(Ink.R, Ink.G, Ink.B, 0.38f), Alpha));
	Panel.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Panel);

	const float NumberRight = X + 116.f * Scale;
	DrawTextAt(FString::FromInt(Mph), true, 32.f * Scale, NumberRight, Y + 8.f * Scale, WithAlpha(Paper, Alpha), true);
	DrawTextAt(TEXT("MPH"), false, 10.f * Scale, NumberRight + 8.f * Scale, Y + 17.f * Scale, WithAlpha(Lime, Alpha));
	DrawTextAt(SimCruiseHudRules::GearText(Gear, bManual), true, 13.f * Scale, NumberRight + 8.f * Scale, Y + 34.f * Scale, WithAlpha(Paper, Alpha));
	DrawTextAt(FString::Printf(TEXT("%d rpm"), FMath::RoundToInt(Rpm / 10.0) * 10), false, 10.f * Scale, X + 16.f * Scale, Y + H - 34.f * Scale, WithAlpha(Dim, Alpha));
	if (bAbs)
	{
		DrawTextAt(TEXT("ABS"), true, 10.f * Scale, NumberRight + 52.f * Scale, Y + 36.f * Scale, WithAlpha(Amber, Alpha));
	}
	DrawRpmBar(X + 16.f * Scale, Y + H - 16.f * Scale, 168.f * Scale, 4.f * Scale, Alpha);
	DrawGMeter(FVector2D(X + W - 52.f * Scale, Y + H * 0.5f), 40.f * Scale, Alpha);
}

void ASimCruiseHUD::DrawFull(float Scale)
{
	const float H = 92.f * Scale;
	const float Y = Canvas->ClipY - H;
	FCanvasTileItem Strip(FVector2D(0.f, Y), FVector2D(Canvas->ClipX, H), FLinearColor(Ink.R, Ink.G, Ink.B, 0.88f));
	Strip.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Strip);

	const float NumberRight = 150.f * Scale;
	DrawTextAt(FString::FromInt(Mph), true, 36.f * Scale, NumberRight, Y + 12.f * Scale, Paper, true);
	DrawTextAt(TEXT("MPH"), false, 11.f * Scale, NumberRight + 10.f * Scale, Y + 22.f * Scale, Lime);
	DrawTextAt(SimCruiseHudRules::GearText(Gear, bManual), true, 15.f * Scale, NumberRight + 10.f * Scale, Y + 42.f * Scale, Paper);

	const float BarX = 250.f * Scale;
	DrawTextAt(FString::Printf(TEXT("%d rpm"), FMath::RoundToInt(Rpm / 10.0) * 10), false, 11.f * Scale, BarX, Y + 22.f * Scale, Paper);
	DrawRpmBar(BarX, Y + 52.f * Scale, 300.f * Scale, 6.f * Scale, 1.f);

	const ASimCruisePlayerController* Controller = Cast<ASimCruisePlayerController>(PlayerOwner);
	const ASimCruisePlayerCameraManager* Camera = Controller ? Cast<ASimCruisePlayerCameraManager>(Controller->PlayerCameraManager) : nullptr;
	const FString Mode = bManual ? TEXT("Manual") : TEXT("Automatic");
	const FString View = Camera ? ASimCruisePlayerCameraManager::ViewName(Camera->GetView()) : TEXT("");
	DrawTextAt(CarName, true, 12.f * Scale, 600.f * Scale, Y + 22.f * Scale, Paper);
	DrawTextAt(FString::Printf(TEXT("%s  ·  %s%s"), *Mode, *View, bAbs ? TEXT("  ·  ABS") : TEXT("")), false, 11.f * Scale, 600.f * Scale, Y + 48.f * Scale, Dim);

	const bool bPad = Controller && Controller->IsUsingGamepad();
	FString Hint = bPad ? TEXT("RT / LT  drive     Y  camera     B  manual     View  recover     L3  HUD")
	                    : TEXT("W / S  drive     A / D  steer     C  camera     T  manual     R  recover     F2  HUD");
	if (bManual)
	{
		Hint = bPad ? TEXT("LB  clutch     RB / X  shift     A  parking brake     Y  camera     View  recover")
		            : TEXT("Shift  clutch     E / Q  shift     H  parking brake     C  camera     R  recover");
	}
	DrawTextAt(Hint, false, 10.f * Scale, Canvas->ClipX - 130.f * Scale, Y + 36.f * Scale, Dim, true);
	DrawGMeter(FVector2D(Canvas->ClipX - 62.f * Scale, Y + H * 0.5f), 36.f * Scale, 1.f);
}

void ASimCruiseHUD::DrawMessage(float Scale)
{
	if (Message.IsEmpty())
	{
		return;
	}
	const float Size = 12.f * Scale;
	const FVector2D TextSize = MeasureText(Message, false, Size);
	const float PadX = 18.f * Scale;
	const float PadY = 9.f * Scale;
	const float X = (Canvas->ClipX - TextSize.X) * 0.5f;
	const float Y = 56.f * Scale;
	FCanvasTileItem Box(FVector2D(X - PadX, Y - PadY), FVector2D(TextSize.X + 2.f * PadX, TextSize.Y + 2.f * PadY), FLinearColor(0.07f, 0.12f, 0.1f, 0.8f));
	Box.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Box);
	DrawTextAt(Message, false, Size, X, Y, Paper);
}
