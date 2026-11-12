// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Gameplay/SimCruiseCameraModels.h"
#include "SimCruiseHUD.generated.h"

class UFont;

/** HUD levels, cycled with F2 or the left-stick click (godot-final:game/Hud.gd).
 *  Off: only prompts. Glance: a translucent speed tag and g-meter that fade
 *  while cruising. Full: an opaque strip with every readout and the hints. */
enum class ESimCruiseHudLevel : uint8
{
	Off = 0,
	Glance = 1,
	Full = 2,
};

namespace SimCruiseHudRules
{
	constexpr float GlanceFaded = 0.4f;
	constexpr float GlanceFadeS = 0.35f;
	constexpr int32 SpeedWakeMph = 3;
	constexpr float WakeS = 2.5f;

	SIMCRUISE_API ESimCruiseHudLevel Next(ESimCruiseHudLevel Level);
	SIMCRUISE_API const TCHAR* Name(ESimCruiseHudLevel Level);
	/** Gear readout: R, N, 1..n; automatic shows D before the gear. */
	SIMCRUISE_API FString GearText(int32 Gear, bool bManual);
	/** Alpha of the Glance panel after DeltaTimeS: 1 while awake, fading to GlanceFaded. */
	SIMCRUISE_API float GlanceAlpha(float Current, bool bAwake, float DeltaTimeS);
}

/** Canvas HUD drawn in code (no UMG asset): speed, gear, rpm, a g-g meter
 *  with the car's published skidpad and braking limits as an ellipse. */
UCLASS()
class SIMCRUISE_API ASimCruiseHUD : public AHUD
{
	GENERATED_BODY()

public:
	ASimCruiseHUD();

	virtual void DrawHUD() override;

	void CycleLevel();
	void SetLevel(ESimCruiseHudLevel InLevel);
	ESimCruiseHudLevel GetLevel() const { return Level; }
	void ShowMessage(const FString& Text, float Seconds = 3.f);
	const FString& GetMessage() const { return Message; }

private:
	void DrawGlance(float Scale, float DeltaTimeS);
	void DrawFull(float Scale);
	void DrawMessage(float Scale);
	/** The co-driver's card for the notable building the car passes (USimCruisePlaceGraphSubsystem). */
	void DrawPlaceCard(float Scale);
	/** The roads round the car, heading up (MapHUD.gd). */
	void DrawMinimap(float Scale, float Alpha);
	/** While the car waits for a new place to be built. */
	void DrawLoading(float Scale);
	void DrawGMeter(const FVector2D& Centre, float Radius, float Alpha);
	void DrawRpmBar(float X, float Y, float W, float H, float Alpha);
	/** Roboto from the engine content, Bold or Regular, at PointSize (already scaled). */
	void DrawTextAt(const FString& Text, bool bBold, float PointSize, float X, float Y, const FLinearColor& Color, bool bRightAlign = false);
	FVector2D MeasureText(const FString& Text, bool bBold, float PointSize);
	void SampleVehicle(float DeltaTimeS);

	UFont* HudFont(bool bBold);

	/** Engine Roboto duplicated per typeface (Regular, Bold) at a 24 pt base,
	 *  drawn scaled: runtime fonts rasterise at the drawn size, so text stays crisp. */
	UPROPERTY(Transient)
	TObjectPtr<UFont> RobotoRegular;
	UPROPERTY(Transient)
	TObjectPtr<UFont> RobotoBold;

	ESimCruiseHudLevel Level = ESimCruiseHudLevel::Glance;
	FString Message;
	float MessageTime = 0.f;
	/** Index into the minimap scales (MapHUD.gd starts at 1). */
	int32 MinimapZoom = 1;
	float GlanceAlphaValue = 1.f;
	float WakeTime = SimCruiseHudRules::WakeS;
	int32 ShownMph = 0;
	double LastDrawTime = -1.0;

	// Snapshot of the vehicle for this frame.
	bool bHasVehicle = false;
	int32 Mph = 0;
	int32 Gear = 0;
	bool bManual = false;
	bool bAbs = false;
	double Rpm = 0.0;
	double UpshiftRpm = 6700.0;
	double RedlineRpm = 7000.0;
	double SkidpadG = 0.89;
	double BrakingG = 0.95;
	FString CarName;
	FSimCruiseGMeter GMeter;
	TArray<FVector2D> GTrail;
	float TrailClock = 0.f;
};
