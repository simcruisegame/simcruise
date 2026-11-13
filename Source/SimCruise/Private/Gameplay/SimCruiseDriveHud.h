// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Gameplay/SimCruiseUiTheme.h"
#include "World/SimCruiseRoadNetwork.h"

class ASimCruiseHUD;
class UCanvas;
class UWorld;
class USimCruiseRouteSubsystem;
struct FCanvasUVTri;

/**
 * The 2.0.2 HUD (docs/ui/ui-spec.md, mockup 5), drawn on the HUD's canvas:
 * speed, gear and rev bar at the bottom left; with a route, the guide-sign
 * turn panel at the top, magenta chevrons on the road in the driven lane, an
 * arrow board at the next turn and the course on a round map that turns with
 * the car. simcruise.Hud.Look 0 brings the earlier HUD back.
 */
class FSimCruiseDriveHud
{
public:
	static bool IsEnabled();

	void Draw(ASimCruiseHUD& Hud, float DeltaTimeS);

	/** Local clock at a place: the environment's UTC time and the live weather's offset (else the longitude's). */
	static FDateTime LocalTime(const UWorld* World, double Lon);

private:
	/** A road station near the car, in world metres. */
	struct FStation
	{
		FSimCruiseRibbonStation St;
		FVector2D C = FVector2D::ZeroVector;
		double TileZCm = 0.0;
	};

	void DrawSpeed(ASimCruiseHUD& Hud, float Alpha);
	void DrawTurnSign(const USimCruiseRouteSubsystem& Gps);
	void DrawArrival(const FString& Text);
	void DrawStatus(const FString& Text, float Y);
	void DrawMessage(const FString& Text, float Y);
	void DrawRoundMap(ASimCruiseHUD& Hud, const USimCruiseRouteSubsystem* Gps, float Alpha);
	void DrawRoadGuides(ASimCruiseHUD& Hud, const USimCruiseRouteSubsystem& Gps);
	void DrawPlaceCard(ASimCruiseHUD& Hud, float Bottom);
	void GatherStations(ASimCruiseHUD& Hud, const FVector2D& CarM);
	/** World height (cm) of the road surface at a point in world metres, from the nearest ribbon station. */
	bool RoadHeightCm(const FVector2D& PointM, double& OutZCm) const;

	// Canvas helpers in pixels at 1080p (scaled to the canvas).
	float P(float Px) const { return Px * Scale; }
	void Rect(float X, float Y, float W, float H, const FLinearColor& Color);
	void RoundRect(float X, float Y, float W, float H, float R, const FLinearColor& Color);
	void Disc(const FVector2D& C, float R, const FLinearColor& Color, int32 Segments = 40);
	void Line(const FVector2D& A, const FVector2D& B, const FLinearColor& Color, float Thickness);
	void Strokes(const TArray<TArray<FVector2D>>& In, const FVector2D& Origin, float Size, const FLinearColor& Color, float Thickness);
	void Quad(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D, const FLinearColor& Color);
	FVector2D Measure(const FString& Text, SimCruiseTheme::EWeight Weight, float Px) const;
	/** Text at X, Y (top); Align 0 left, 1 centre, 2 right. Digits get one width when bTabular. */
	float TextAt(const FString& Text, SimCruiseTheme::EWeight Weight, float Px, float X, float Y, const FLinearColor& Color, int32 Align = 0, bool bTabular = false, bool bShadow = true);

	UCanvas* Canvas = nullptr;
	float Scale = 1.f;
	double Clock = 0.0;
	TArray<FStation> Stations;
	double StationsTime = -100.0;
	FVector2D StationsAt = FVector2D::ZeroVector;
	float SmoothLateral = 0.f;
	float RouteAlpha = 0.f;
};
