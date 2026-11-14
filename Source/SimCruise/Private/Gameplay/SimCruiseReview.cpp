// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseReview.h"

#include <limits>

#include "Camera/PlayerCameraManager.h"
#include "DynamicResolutionState.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Environment/SimCruiseGraphics.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Engine/World.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Knowledge/SimCruisePlaceGraphSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseReview, Log, All);

namespace
{
	constexpr float TimedShotEveryS = 5.f;
	constexpr int32 MaxEventShots = 40;

	double Median(TArray<double> Values)
	{
		if (Values.IsEmpty()) return 0.0;
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	double Percentile(TArray<double> Values, double Q)
	{
		if (Values.IsEmpty()) return 0.0;
		Values.Sort();
		return Values[FMath::Clamp(FMath::CeilToInt32(Q * Values.Num()) - 1, 0, Values.Num() - 1)];
	}

	FString Num(double V)
	{
		return FMath::IsFinite(V) ? FString::Printf(TEXT("%.3f"), V) : FString(TEXT("null"));
	}

	FString Box(const FVector2D& Min, const FVector2D& Max)
	{
		return FString::Printf(TEXT("[%.0f, %.0f, %.0f, %.0f]"), Min.X, Min.Y, Max.X, Max.Y);
	}

	/** The speedometer's unit and the value its needle shows (NaN without a speedometer). */
	double NeedleSpeed(const USimCruiseCarPresentationComponent* Model, FString& OutUnit)
	{
		OutUnit.Reset();
		if (!Model) return std::numeric_limits<double>::quiet_NaN();
		const double Mph = Model->GetDialValue(TEXT("speed_mph"));
		if (FMath::IsFinite(Mph)) { OutUnit = TEXT("mph"); return Mph; }
		const double Kmh = Model->GetDialValue(TEXT("speed_kmh"));
		if (FMath::IsFinite(Kmh)) { OutUnit = TEXT("km/h"); return Kmh; }
		return std::numeric_limits<double>::quiet_NaN();
	}
}

namespace SimCruiseReviewRules
{
	double SteeringRatio(const TArray<FVector2D>& RimDegWheelDeg, double MinWheelDeg)
	{
		TArray<double> Ratios;
		for (const FVector2D& S : RimDegWheelDeg)
		{
			if (FMath::Abs(S.Y) > MinWheelDeg) Ratios.Add(FMath::Abs(S.X) / FMath::Abs(S.Y));
		}
		return Median(MoveTemp(Ratios));
	}

	double LockToLockTurns(double FullLockRoadWheelDeg, double Ratio)
	{
		return 2.0 * FMath::Abs(FullLockRoadWheelDeg) * Ratio / 360.0;
	}

	bool GaugeIsWrong(double Needle, double True)
	{
		return FMath::Abs(Needle - True) > FMath::Max(MaxGaugeErrorFloor, MaxGaugeErrorShare * FMath::Abs(True));
	}
}

void FSimCruiseReviewRecorder::Begin(const FString& InOutputDir)
{
	bOn = true;
	OutputDir = InOutputDir;
	IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputDir, TEXT("review")), true);
	NextTimedS = 0.f;
	// The real preset picture: dynamic resolution off, so the 3D picture stays at the preset's screen
	// percentage (Apply sets it) and the frame times are its honest cost. Console priority, so the
	// preset (game-setting priority) cannot turn it back on. -SimCruisePlayerRes keeps the player's.
	if (!FParse::Param(FCommandLine::Get(), TEXT("SimCruisePlayerRes")))
	{
		if (IConsoleVariable* DynRes = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicRes.OperationMode"))) DynRes->Set(0, ECVF_SetByConsole);
	}
	UE_LOG(LogSimCruiseReview, Display, TEXT("Review: full-size frames, sidecars and close-up boxes -> %s/review"), *OutputDir);
}

void FSimCruiseReviewRecorder::Event(const FString& Name, float DriveSeconds)
{
	if (!bOn) return;
	Events.Add(FString::Printf(TEXT("%.1f s %s"), DriveSeconds, *Name));
	if (DriveSeconds - LastEventS < 1.f || Shots.Num() >= MaxEventShots + 20 || !PendingName.IsEmpty()) return;
	LastEventS = DriveSeconds;
	// Names go into file names: letters, digits and dashes only.
	FString Safe;
	for (const TCHAR C : Name.ToLower()) Safe.AppendChar(FChar::IsAlnum(C) && C < 128 ? C : TEXT('-'));
	PendingName = Safe.Left(40);
}

void FSimCruiseReviewRecorder::Tick(ASimCruisePlayerController& Player, float DriveSeconds, float DeltaSeconds, float Stick)
{
	if (!bOn) return;
	SampleResolution();
	StickNow = Stick;
	if (DriveSeconds >= NextTimedS && PendingName.IsEmpty())
	{
		PendingName = DriveSeconds < 0.5f ? TEXT("start") : TEXT("timed");
		NextTimedS = DriveSeconds + TimedShotEveryS;
	}

	// A place card the co-driver shows.
	PlaceClock += DeltaSeconds;
	if (PlaceClock > 0.5f)
	{
		PlaceClock = 0.f;
		if (const USimCruisePlaceGraphSubsystem* Graph = USimCruisePlaceGraphSubsystem::Get(Player.GetWorld()))
		{
			FSimCruisePlaceCard Card;
			if (Graph->GetPlaceCard(Card) && Card.Title != LastPlaceCard)
			{
				LastPlaceCard = Card.Title;
				Event(TEXT("place-card ") + Card.Title, DriveSeconds);
			}
		}
	}

	const ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(Player.GetPawn());
	const USimCruiseCarPresentationComponent* Model = Car ? Car->GetCarModel() : nullptr;
	if (!Model || DeltaSeconds <= 0.f) return;
	if (CarId.IsEmpty())
	{
		CarId = Car->GetRealCar().Id;
		FullLockDeg = FMath::RadiansToDegrees(Car->GetVehicleSetup().ParkingRackAngleRad);
		ProfileRatio = Model->GetSteeringRatio();
	}

	// Steering: what the driver sees the rim do against what the road wheels do.
	const double RimDeg = FMath::RadiansToDegrees(Model->GetSteeringWheelAngleRad());
	const double WheelDeg = FMath::RadiansToDegrees(0.5 * (Model->GetDrawnSteerRad(0) + Model->GetDrawnSteerRad(1)));
	RimWheel.Add(FVector2D(RimDeg, WheelDeg));
	if (bHasRim) MaxRimRateDps = FMath::Max(MaxRimRateDps, FMath::Abs(RimDeg - LastRimDeg) / DeltaSeconds);
	LastRimDeg = RimDeg;
	bHasRim = true;
	MaxRimDeg = FMath::Max(MaxRimDeg, FMath::Abs(RimDeg));
	MaxWheelDeg = FMath::Max(MaxWheelDeg, FMath::Abs(WheelDeg));

	// Gauges, when the speed is steady (a needle lags a fast change on purpose).
	const FSimCruiseDriveTelemetry& T = Player.GetTelemetry();
	const double SpeedMps = FMath::Abs(T.BodyVelocityMps.X);
	const double Accel = FMath::Abs(SpeedMps - LastSpeedMps) / DeltaSeconds;
	LastSpeedMps = SpeedMps;
	FString Unit;
	const double NeedleV = NeedleSpeed(Model, Unit);
	const double NeedleRpm = Model->GetDialValue(TEXT("rpm"));
	if (SpeedMps > 2.0 && Accel < 0.5)
	{
		++GaugeSamples;
		if (FMath::IsFinite(NeedleV))
		{
			SpeedUnit = Unit;
			const double TrueV = Unit == TEXT("mph") ? SpeedMps * 2.2369362921 : SpeedMps * 3.6;
			SpeedErrors.Add(FMath::Abs(NeedleV - TrueV));
			SpeedWrongSamples += SimCruiseReviewRules::GaugeIsWrong(NeedleV, TrueV) ? 1 : 0;
		}
		if (FMath::IsFinite(NeedleRpm))
		{
			RpmErrors.Add(FMath::Abs(NeedleRpm - T.EngineSpeedRpm));
			RpmWrongSamples += FMath::Abs(NeedleRpm - T.EngineSpeedRpm) > FMath::Max(150.0, 0.05 * T.EngineSpeedRpm) ? 1 : 0;
		}
	}
}

void FSimCruiseReviewRecorder::PrepareShot(ASimCruisePlayerController& Player, float DriveSeconds)
{
	const ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(Player.GetPawn());
	const USimCruiseCarPresentationComponent* Model = Car ? Car->GetCarModel() : nullptr;
	const FSimCruiseDriveTelemetry& T = Player.GetTelemetry();
	int32 W = 0, H = 0;
	Player.GetViewportSize(W, H);
	const double SpeedMps = FMath::Abs(T.BodyVelocityMps.X);
	FString Unit;
	const double NeedleV = NeedleSpeed(Model, Unit);
	const double TrueV = Unit == TEXT("km/h") ? SpeedMps * 3.6 : SpeedMps * 2.2369362921;
	const double NeedleRpm = Model ? Model->GetDialValue(TEXT("rpm")) : std::numeric_limits<double>::quiet_NaN();
	const double RimDeg = Model ? FMath::RadiansToDegrees(Model->GetSteeringWheelAngleRad()) : 0.0;
	const double WheelDeg = Model ? FMath::RadiansToDegrees(0.5 * (Model->GetDrawnSteerRad(0) + Model->GetDrawnSteerRad(1))) : 0.0;

	// Close-up boxes: the steering wheel and the cluster behind it from the wheel's place on screen;
	// the mirrors and the windscreen as fixed parts of the driver's-seat view.
	FString WheelBox = TEXT("null"), ClusterBox = TEXT("null");
	if (Model && Model->HasSteeringWheel() && Player.PlayerCameraManager)
	{
		const FVector Centre = Model->GetComponentTransform().TransformPosition(Model->GetSteeringWheelRestTransform().GetLocation());
		const FRotator View = Player.PlayerCameraManager->GetCameraRotation();
		const double RadiusCm = 19.0;
		FVector2D C, R, U;
		if (Player.ProjectWorldLocationToScreen(Centre, C) && Player.ProjectWorldLocationToScreen(Centre + FRotationMatrix(View).GetUnitAxis(EAxis::Y) * RadiusCm, R)
			&& Player.ProjectWorldLocationToScreen(Centre + FRotationMatrix(View).GetUnitAxis(EAxis::Z) * RadiusCm, U))
		{
			const double Rx = FMath::Max(FMath::Abs(R.X - C.X), 20.0) * 1.15;
			const double Ry = FMath::Max(FMath::Abs(U.Y - C.Y), 20.0) * 1.15;
			auto Clamp = [W, H](FVector2D P) { return FVector2D(FMath::Clamp(P.X, 0.0, double(W)), FMath::Clamp(P.Y, 0.0, double(H))); };
			WheelBox = Box(Clamp(C - FVector2D(Rx, Ry)), Clamp(C + FVector2D(Rx, Ry)));
			// The cluster sits behind the wheel's upper half, seen through the rim.
			ClusterBox = Box(Clamp(C - FVector2D(Rx * 1.1, Ry * 1.4)), Clamp(C + FVector2D(Rx * 1.1, Ry * 0.1)));
		}
	}
	const FString Mirrors = FString::Printf(TEXT("\"mirror_inside\": %s, \"mirror_left\": %s, \"mirror_right\": %s, \"windscreen\": %s"),
		*Box(FVector2D(0.50 * W, 0.28 * H), FVector2D(0.98 * W, 0.58 * H)), *Box(FVector2D(0.0, 0.55 * H), FVector2D(0.22 * W, 0.95 * H)),
		*Box(FVector2D(0.78 * W, 0.55 * H), FVector2D(1.0 * W, 0.95 * H)), *Box(FVector2D(0.12 * W, 0.20 * H), FVector2D(0.95 * W, 0.70 * H)));

	const FString Name = FString::Printf(TEXT("%03d-%05.1fs-%s"), ShotIndex++, DriveSeconds, *PendingName);
	PendingSidecar = FString::Printf(TEXT("{\n  \"name\": \"%s\", \"t\": %.2f, \"event\": \"%s\", \"width\": %d, \"height\": %d,\n")
		TEXT("  \"speed_unit\": \"%s\", \"speed_true\": %s, \"speed_needle\": %s, \"rpm_true\": %.0f, \"rpm_needle\": %s, \"gear\": %d,\n")
		TEXT("  \"stick\": %s, \"rack_deg\": %s, \"road_wheel_deg\": %s, \"rim_deg\": %s,\n")
		TEXT("  \"boxes\": {\"wheel\": %s, \"cluster\": %s, %s}\n}\n"),
		*Name, DriveSeconds, *PendingName, W, H, Unit.IsEmpty() ? TEXT("none") : *Unit, *Num(TrueV), *Num(NeedleV), T.EngineSpeedRpm, *Num(NeedleRpm), T.Gear,
		*Num(StickNow), *Num(FMath::RadiansToDegrees(Player.GetLastCommand().RackAngleRad)), *Num(WheelDeg), *Num(RimDeg),
		*WheelBox, *ClusterBox, *Mirrors);
	PendingName = Name;
	bPrepared = true;
}

void FSimCruiseReviewRecorder::OnFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (!bPrepared) return;
	bPrepared = false;
	const FString Base = FPaths::Combine(OutputDir, TEXT("review"), PendingName);
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	if (!Png.IsEmpty() && FFileHelper::SaveArrayToFile(Png, *(Base + TEXT(".png"))))
	{
		FFileHelper::SaveStringToFile(PendingSidecar, *(Base + TEXT(".json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Shots.Add(PendingName);
	}
	PendingName.Reset();
	PendingSidecar.Reset();
}

void FSimCruiseReviewRecorder::Finish(const FString& Verdict, const FString& Reasons)
{
	if (!bOn) return;
	using namespace SimCruiseReviewRules;
	const double Ratio = SteeringRatio(RimWheel);
	const double Turns = LockToLockTurns(FullLockDeg, Ratio);
	const double ProfileTurns = LockToLockTurns(FullLockDeg, ProfileRatio);
	FString EventList, ShotList;
	for (const FString& E : Events) EventList += FString::Printf(TEXT("%s\"%s\""), EventList.IsEmpty() ? TEXT("") : TEXT(", "), *E.ReplaceCharWithEscapedChar());
	for (const FString& S : Shots) ShotList += FString::Printf(TEXT("%s\"%s\""), ShotList.IsEmpty() ? TEXT("") : TEXT(", "), *S);
	const FString Json = FString::Printf(TEXT("{\n  \"car\": \"%s\", \"preflight\": \"%s\", \"preflight_reasons\": \"%s\",\n")
		TEXT("  \"steering\": {\"rim_deg_per_road_wheel_deg\": %.2f, \"car_data_ratio\": %.2f, \"full_lock_road_wheel_deg\": %.1f, \"lock_to_lock_turns\": %.2f, \"car_data_lock_to_lock_turns\": %.2f, \"max_rim_deg\": %.1f, \"max_road_wheel_deg\": %.1f, \"max_rim_speed_dps\": %.0f, \"samples\": %d},\n")
		TEXT("  \"gauges\": {\"samples\": %d, \"speed_unit\": \"%s\", \"speed_median_error\": %.2f, \"speed_p95_error\": %.2f, \"speed_wrong_samples\": %d, \"rpm_median_error\": %.0f, \"rpm_p95_error\": %.0f, \"rpm_wrong_samples\": %d},\n")
		TEXT("  \"settings\": %s,\n  \"events\": [%s],\n  \"shots\": [%s]\n}\n"),
		*CarId, *Verdict, *Reasons.ReplaceCharWithEscapedChar(),
		Ratio, ProfileRatio, FullLockDeg, Turns, ProfileTurns, MaxRimDeg, MaxWheelDeg, MaxRimRateDps, RimWheel.Num(),
		GaugeSamples, SpeedUnit.IsEmpty() ? TEXT("none") : *SpeedUnit, Median(SpeedErrors), Percentile(SpeedErrors, 0.95), SpeedWrongSamples,
		Median(RpmErrors), Percentile(RpmErrors, 0.95), RpmWrongSamples, *SettingsJson(true), *EventList, *ShotList);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDir, TEXT("review.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogSimCruiseReview, Display, TEXT("REVIEW STEERING car=%s rim_per_wheel=%.2f (car data %.2f) lock_to_lock=%.2f turns (car data %.2f) max_rim_speed=%.0f deg/s"),
		*CarId, Ratio, ProfileRatio, Turns, ProfileTurns, MaxRimRateDps);
	UE_LOG(LogSimCruiseReview, Display, TEXT("REVIEW GAUGES car=%s speed (%s) median_error=%.2f p95=%.2f wrong=%d of %d; rpm median_error=%.0f p95=%.0f wrong=%d"),
		*CarId, *SpeedUnit, Median(SpeedErrors), Percentile(SpeedErrors, 0.95), SpeedWrongSamples, GaugeSamples, Median(RpmErrors), Percentile(RpmErrors, 0.95), RpmWrongSamples);
}

void FSimCruiseReviewRecorder::SampleResolution()
{
	if (!GEngine) return;
	FDynamicResolutionStateInfos Infos;
	GEngine->GetDynamicResolutionCurrentStateInfos(Infos);
	float Fraction = Infos.Status == EDynamicResolutionStatus::Enabled ? Infos.ResolutionFractionApproximations[GDynamicPrimaryResolutionFraction] : -1.f;
	if (Fraction <= 0.f)
	{
		const IConsoleVariable* Pct = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		Fraction = Pct ? Pct->GetFloat() / 100.f : 1.f;
	}
	ResFractionMin = ResFractionSamples ? FMath::Min(ResFractionMin, Fraction) : Fraction;
	ResFractionMax = ResFractionSamples ? FMath::Max(ResFractionMax, Fraction) : Fraction;
	ResFractionSum += Fraction;
	++ResFractionSamples;
}

FString FSimCruiseReviewRecorder::SettingsJson(bool bLog)
{
	IConsoleManager& Cvars = IConsoleManager::Get();
	auto Float = [&Cvars](const TCHAR* Name) { const IConsoleVariable* V = Cvars.FindConsoleVariable(Name); return V ? V->GetFloat() : -1.f; };
	const ESimCruiseGraphicsPreset Preset = FSimCruiseGraphics::Current();
	const FSimCruisePresetSpec Spec = FSimCruiseGraphics::Spec(Preset);
	const FSimCruisePresetSpec Top = FSimCruiseGraphics::Spec(ESimCruiseGraphicsPreset::Ultra);
	static const TCHAR* Groups[] = { TEXT("ViewDistance"), TEXT("AntiAliasing"), TEXT("Shadow"), TEXT("GlobalIllumination"), TEXT("Reflection"),
		TEXT("PostProcess"), TEXT("Texture"), TEXT("Effects"), TEXT("Foliage"), TEXT("Shading"), TEXT("Landscape") };
	FString SgJson, SgLog;
	bool bGroupsTop = true;
	for (const TCHAR* G : Groups)
	{
		const int32 Level = FMath::RoundToInt(Float(*FString::Printf(TEXT("sg.%sQuality"), G)));
		bGroupsTop &= Level >= Top.ScalabilityLevel;
		SgJson += FString::Printf(TEXT("%s\"%s\": %d"), SgJson.IsEmpty() ? TEXT("") : TEXT(", "), G, Level);
		SgLog += FString::Printf(TEXT("%s%s:%d"), SgLog.IsEmpty() ? TEXT("") : TEXT(","), G, Level);
	}
	FIntPoint Size(0, 0);
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) Size = GEngine->GameViewport->Viewport->GetSizeXY();
	// The screen's own resolution: from the script (an off-screen game has no display), else the primary display.
	FIntPoint Native(0, 0);
	FString NativeText;
	if (FParse::Value(FCommandLine::Get(), TEXT("SimCruiseNativeRes="), NativeText))
	{
		FString W, H;
		if (NativeText.Split(TEXT("x"), &W, &H)) Native = FIntPoint(FCString::Atoi(*W), FCString::Atoi(*H));
	}
	if (Native.X <= 0)
	{
		if (FSlateApplication::IsInitialized())
		{
			FDisplayMetrics Metrics;
			FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
			Native = FIntPoint(Metrics.PrimaryDisplayWidth, Metrics.PrimaryDisplayHeight);
		}
	}
	const float ScreenPct = Float(TEXT("r.ScreenPercentage"));
	const bool bDynRes = Float(TEXT("r.DynamicRes.OperationMode")) > 0.f;
	const float DynMin = Float(TEXT("r.DynamicRes.MinScreenPercentage"));
	const float DynMax = Float(TEXT("r.DynamicRes.MaxScreenPercentage"));
	const float MeanPct = ResFractionSamples ? float(100.0 * ResFractionSum / ResFractionSamples) : ScreenPct;
	const bool bTopPreset = Preset == ESimCruiseGraphicsPreset::Ultra;
	const bool bNative = Native.X > 0 && Size.X >= Native.X && Size.Y >= Native.Y;
	// The percentage the engine really drew at (sampled each frame), not only the setting.
	const bool bPct = ScreenPct >= Top.ScreenPercentage - 0.01f && MeanPct >= Top.ScreenPercentage - 0.5f;
	const bool bValid = bTopPreset && bGroupsTop && bNative && bPct;
	FString Why;
	if (!bTopPreset) Why += TEXT("preset-below-ultra,");
	if (!bGroupsTop) Why += TEXT("sg-below-top,");
	if (!bNative) Why += TEXT("below-native-resolution,");
	if (!bPct) Why += FString::Printf(TEXT("screen-percentage-below-preset-%.0f-actual,"), MeanPct);
	Why.RemoveFromEnd(TEXT(","));
	if (bLog)
	{
		UE_LOG(LogSimCruiseReview, Display, TEXT("REVIEW SETTINGS preset=%s resolution=%dx%d native=%dx%d screen_pct=%.0f preset_pct=%.0f dynres=%s min=%.0f max=%.0f mean_actual_pct=%.1f actual_min=%.1f actual_max=%.1f sg=%s needs=ultra,%dx%d,%.0f valid=%s why=%s"),
			*FSimCruiseGraphics::Name(Preset), Size.X, Size.Y, Native.X, Native.Y, ScreenPct, Spec.ScreenPercentage, bDynRes ? TEXT("on") : TEXT("off"), DynMin, DynMax,
			MeanPct, ResFractionMin * 100.f, ResFractionMax * 100.f, *SgLog, Native.X, Native.Y, Top.ScreenPercentage, bValid ? TEXT("yes") : TEXT("no"), Why.IsEmpty() ? TEXT("-") : *Why);
	}
	return FString::Printf(TEXT("{\"preset\": \"%s\", \"resolution\": \"%dx%d\", \"native\": \"%dx%d\", \"screen_percentage\": %.1f, \"preset_screen_percentage\": %.1f, \"dynamic_resolution\": %s, \"dynamic_min_percentage\": %.1f, \"dynamic_max_percentage\": %.1f, \"mean_actual_percentage\": %.1f, \"sg\": {%s}, \"valid_for_sign_off\": %s, \"why\": \"%s\"}"),
		*FSimCruiseGraphics::Name(Preset), Size.X, Size.Y, Native.X, Native.Y, ScreenPct, Spec.ScreenPercentage, bDynRes ? TEXT("true") : TEXT("false"), DynMin, DynMax, MeanPct,
		*SgJson, bValid ? TEXT("true") : TEXT("false"), *Why);
}