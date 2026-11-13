// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruiseAtlasUI.h"
#include "Gameplay/SimCruiseMapTiles.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class USimCruiseRouteSubsystem;

/**
 * Plan a route (docs/ui/ui-spec.md, mockup 2): a full-screen map with a cross
 * in the middle. The left stick (arrows, WASD, mouse drag) moves the map,
 * A (Enter, a click) adds a stop at the cross, X (Delete) removes the stop
 * under the cross or the last one, Y starts the route, LT and RT (- and +,
 * the wheel) zoom, B (Esc) goes back. The list shows each stop with the
 * distance and time the router finds for it.
 */
class SIMCRUISE_API SSimCruiseRoutePlanner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruiseRoutePlanner) : _bPad(true) {}
		SLATE_EVENT(FSimCruiseGetAtlasView, GetView)
		SLATE_ARGUMENT(TWeakObjectPtr<USimCruiseRouteSubsystem>, Gps)
		SLATE_ARGUMENT(bool, bPad)
		SLATE_EVENT(FSimpleDelegate, OnStart)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	/** Add a stop at a place in world metres (the cross, a click; tests). */
	void AddStop(const FVector2D& WorldM);
	void RemoveStop();
	void MoveCross(const FVector2D& WorldM) { CenterM = WorldM; bCentered = true; }
	int32 NumStops() const;
	void Zoom(int32 Step);

private:
	double PxPerM() const;
	FVector2D ScreenOf(const FVector2D& WorldM) const { return (WorldM - CenterM) * PxPerM() + SizePx * 0.5; }
	FVector2D WorldOf(const FVector2D& ScreenPx) const { return CenterM + (ScreenPx - SizePx * 0.5) / PxPerM(); }
	void StopsChanged();
	void RebuildList();
	USimCruiseRouteSubsystem* GetGps() const { return Gps.Get(); }

	FSimCruiseGetAtlasView GetView;
	TWeakObjectPtr<USimCruiseRouteSubsystem> Gps;
	FSimpleDelegate OnStart;
	FSimpleDelegate OnBack;
	FSimCruiseAtlasView View;
	FSimCruiseMapTiles Tiles;
	TSharedPtr<SVerticalBox> List;
	FVector2D CenterM = FVector2D::ZeroVector;
	FVector2D SizePx = FVector2D(1920.0, 1080.0);
	FVector2D Stick = FVector2D::ZeroVector;
	int32 Level = 2;
	bool bCentered = false;
	bool bPad = true;
	bool bDragging = false;
	bool bMoved = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	int32 SeenSerial = -1;
};
