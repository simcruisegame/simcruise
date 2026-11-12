// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruisePlaces.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "World/SimCruiseGeo.h"

class ASimCruiseWorldStreamer;
class SEditableTextBox;
class SVerticalBox;
class STextBlock;
class UTexture2D;
struct FSlateBrush;

/**
 * The place search and the road atlas, the two screens that take the driver
 * anywhere (godot-final:game/Hud.gd "Where to?" and godot-final:game/RoadMap.gd).
 * Both are Slate widgets the player controller adds to the viewport while the
 * game is paused; the place itself is chosen through callbacks the controller
 * forwards to ASimCruiseGameMode::TravelTo.
 */

/** What the atlas shows of the drive, read each frame. */
struct FSimCruiseAtlasView
{
	FSimCruiseWorldFrame Frame;
	FVector CarCm = FVector::ZeroVector;
	double CarYawDeg = 0.0;
	bool bHasCar = false;
	FString Title;
	FString CarName;
	FString Transmission;
	/** The cockpit settings shown on the atlas (Hud.gd): sound, HUD level, head motion, rumble. */
	TArray<FString> Settings;
	TWeakObjectPtr<ASimCruiseWorldStreamer> Streamer;
};

DECLARE_DELEGATE_OneParam(FSimCruiseOnPlace, const FSimCruisePlace&);
DECLARE_DELEGATE_RetVal(bool, FSimCruiseOnSurprise);
DECLARE_DELEGATE_RetVal(FSimCruiseAtlasView, FSimCruiseGetAtlasView);
DECLARE_DELEGATE_OneParam(FSimCruiseOnSetting, int32);

/** Shared look (Hud.gd palette: INK #15221f, GLASS #15221f9e, WHITE #f4efdb, LIME #c6ef96). */
namespace SimCruiseUiStyle
{
	SIMCRUISE_API FLinearColor Ink();
	SIMCRUISE_API FLinearColor Glass();
	SIMCRUISE_API FLinearColor Paper();
	SIMCRUISE_API FLinearColor Lime();
	SIMCRUISE_API FLinearColor Dim();
}

/** "Where to?": a search box, the presets, Surprise me, and the helper's answers. */
class SIMCRUISE_API SSimCruisePlaceSearch : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruisePlaceSearch) {}
		SLATE_EVENT(FSimCruiseOnPlace, OnChoose)
		SLATE_EVENT(FSimCruiseOnSurprise, OnSurprise)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSimCruisePlaceSearch() override;

	/** Focus for the device that opened it: the text box for a keyboard, the first place for a pad. */
	void FocusFirst(bool bGamepad);
	TSharedPtr<SWidget> FocusTarget(bool bGamepad) const;
	/** Run a query as if typed (also used by checks). */
	void Submit(const FString& Query);

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	void ShowResults(const TArray<FSimCruisePlace>& Places, const FString& Note);
	void Choose(const FSimCruisePlace& Place);
	void DoSurprise();

	FSimCruiseOnPlace OnChoose;
	FSimCruiseOnSurprise OnSurprise;
	FSimpleDelegate OnClose;
	TSharedPtr<SEditableTextBox> Field;
	TSharedPtr<SVerticalBox> Choices;
	TSharedPtr<STextBlock> Status;
	TArray<TSharedPtr<SWidget>> ChoiceButtons;

	/** The newest search; older answers are dropped (Drive.gd query_serial). */
	struct FPending;
	TSharedPtr<FPending, ESPMode::ThreadSafe> Pending;
	uint32 Serial = 0;
};

/** A place name on a helper map tile: [name, x, y (0..4096 across the tile), priority, bold]. */
struct FSimCruiseMapLabel
{
	FString Text;
	FVector2D At = FVector2D::ZeroVector;
	int32 Priority = 0;
	bool bBold = false;
};

/** The road atlas: helper `map` tiles around the car, pan and zoom, pin a spot and drive there. */
class SIMCRUISE_API SSimCruiseRoadAtlas : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruiseRoadAtlas) {}
		SLATE_EVENT(FSimCruiseGetAtlasView, GetView)
		SLATE_EVENT(FSimCruiseOnPlace, OnChoose)
		SLATE_EVENT(FSimCruiseOnSurprise, OnSurprise)
		SLATE_EVENT(FSimpleDelegate, OnFindPlace)
		SLATE_EVENT(FSimpleDelegate, OnCycleCar)
		SLATE_EVENT(FSimpleDelegate, OnToggleTransmission)
		SLATE_EVENT(FSimCruiseOnSetting, OnSetting)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SSimCruiseRoadAtlas() override;

	void Recenter();
	void Zoom(int32 Step, const FVector2D* AnchorPx = nullptr);
	void DriveToPin();

	/** Map tiles drawn and still to fetch (checks). */
	int32 NumMapImages() const;

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

	/** Zoom ladder, screen px per metre (RoadMap.gd SCALES). */
	static const TArray<double>& Scales();

private:
	struct FMapImage
	{
		TStrongObjectPtr<UTexture2D> Texture;
		TSharedPtr<FSlateBrush> Brush;
		TArray<FSimCruiseMapLabel> Names;
		double LastUsed = 0.0;
	};
	struct FFetch;

	FVector2D ScreenOf(const FVector2D& WorldM) const;
	FVector2D WorldOfScreen(const FVector2D& ScreenPx) const;
	void WantedTiles(TArray<FIntVector>& Out) const;
	void StartFetches();
	void CollectFetches();
	FSimCruisePlace PlaceAt(const FVector2D& WorldM) const;

	FSimCruiseGetAtlasView GetView;
	FSimCruiseOnPlace OnChoose;
	FSimCruiseOnSurprise OnSurprise;
	FSimpleDelegate OnFindPlace;
	FSimpleDelegate OnCycleCar;
	FSimpleDelegate OnToggleTransmission;
	FSimCruiseOnSetting OnSetting;
	FSimpleDelegate OnClose;
	TArray<TSharedPtr<STextBlock>> SettingLabels;

	FSimCruiseAtlasView View;
	int32 Level = 4;
	FVector2D CenterM = FVector2D::ZeroVector;
	bool bPinned = false;
	FVector2D PinM = FVector2D::ZeroVector;
	FVector2D SizePx = FVector2D(1280.0, 720.0);
	FVector2D Stick = FVector2D::ZeroVector;
	bool bDragging = false;
	bool bMoved = false;
	FVector2D PressAt = FVector2D::ZeroVector;
	FVector2D LastMouse = FVector2D::ZeroVector;
	bool bCentered = false;

	TMap<FString, FMapImage> Images;
	TMap<FString, double> RetryAt;
	TSharedPtr<FFetch, ESPMode::ThreadSafe> Fetch;
	TSharedPtr<STextBlock> Status;
	TSharedPtr<STextBlock> Coords;
	TSharedPtr<STextBlock> CarLabel;
	TSharedPtr<STextBlock> GearLabel;
};
