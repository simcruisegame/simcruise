// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Gameplay/SimCruiseRoute.h"
#include "InputCoreTypes.h"
#include "Layout/Geometry.h"
#include "Styling/SlateBrush.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class FSlateWindowElementList;

/**
 * The 2.0.2 look (docs/ui/ui-spec.md): every menu row is a US green guide
 * sign, easy to read from a sofa; the GPS course is magenta. Tokens, the
 * Overpass font (data/fonts/overpass, SIL OFL 1.1, read from disk so it
 * works in a packaged build), stroke icons shared by Slate and the Canvas HUD,
 * and the widgets the menus are built from. Sizes are pixels at 1080p.
 */
namespace SimCruiseTheme
{
	SIMCRUISE_API FLinearColor Guide();
	SIMCRUISE_API FLinearColor GuideDeep();
	SIMCRUISE_API FLinearColor White();
	SIMCRUISE_API FLinearColor Glass(float Alpha = 0.82f);
	SIMCRUISE_API FLinearColor Course();
	SIMCRUISE_API FLinearColor Amber();
	SIMCRUISE_API FLinearColor Mist();
	SIMCRUISE_API FLinearColor RedZone();
	SIMCRUISE_API FLinearColor PadA();
	SIMCRUISE_API FLinearColor PadB();
	SIMCRUISE_API FLinearColor PadX();
	SIMCRUISE_API FLinearColor PadY();

	enum class EWeight : uint8 { Regular, SemiBold, Bold, ExtraBold, Black };

	/** Overpass at a pixel size (1080p); the engine font when the files are missing. */
	SIMCRUISE_API FSlateFontInfo Font(EWeight Weight, float Px);
	/** data/fonts/overpass/Overpass-<Weight>.ttf */
	SIMCRUISE_API FString FontPath(EWeight Weight);
	SIMCRUISE_API bool HasOverpass();

	/** A rounded box brush that lives as long as the game (kept by its values). */
	SIMCRUISE_API const FSlateBrush* RoundedBrush(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f);
	SIMCRUISE_API const FSlateBrush* WhiteBrush();

	enum class EIcon : uint8
	{
		Drive, Search, Route, Star, Traffic, Car, Gearbox, Wrench, Seat, Eye, Sound, Hud, Rumble, Stick, Trigger, Map, Lights, Lens, Screen, Back, Flag,
	};
	/** An icon as strokes in a unit box (0..1, y down). */
	SIMCRUISE_API void IconStrokes(EIcon Icon, TArray<TArray<FVector2D>>& Out);
	/** A turn arrow pictogram (a guide sign's arrow) as strokes in a unit box. */
	SIMCRUISE_API void TurnStrokes(ESimCruiseTurn Turn, TArray<TArray<FVector2D>>& Out);
	/** Draw strokes (unit box) into a Slate element list at Origin with Size. */
	SIMCRUISE_API void PaintStrokes(const TArray<TArray<FVector2D>>& Strokes, FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2D& Origin, float Size, const FLinearColor& Color, float Thickness);

	/** What a menu key means, from the pad or the keyboard. The left stick is not a key here:
	 *  it moves through the menus by its analog value (StickNav), so one push is one step. */
	enum class ENav : uint8 { None, Up, Down, Left, Right, Accept, Back, PrevPage, NextPage, X, Y, ZoomIn, ZoomOut, Menu };
	SIMCRUISE_API ENav NavOf(const FKey& Key);

	/** The left stick as menu steps: one step per push, and repeats while it is held. */
	struct FStickRepeat
	{
		ENav HeldX = ENav::None;
		ENav HeldY = ENav::None;
		double NextX = 0.0;
		double NextY = 0.0;
	};
	/** Up, Down, Left or Right when the left stick's event makes a step now, else None. */
	SIMCRUISE_API ENav StickNav(const FKey& Axis, float Value, FStickRepeat& State, double Now);
	/** Is this event the left stick (menus take all of it, so Slate's own stick navigation never moves focus too)? */
	SIMCRUISE_API bool IsLeftStick(const FKey& Axis);

	/** Give the pad and keyboard focus to a widget (every Slate user, so any pad slot reaches it).
	 *  True when it has it now; false when it is not on screen yet (try again next frame). */
	SIMCRUISE_API bool FocusPad(const TSharedPtr<SWidget>& Widget);
	/** A pad step on a screen laid out in rows of controls: the control after the focused one.
	 *  Up and down go to the row above or below, about the same place across, and stop at the
	 *  first and last rows; left and right walk the row and go on into the next row (bAcross) or
	 *  round the same row. Null when nothing in the rows has the focus. */
	SIMCRUISE_API TSharedPtr<SWidget> StepGrid(const TArray<TArray<TSharedPtr<SWidget>>>& Rows, const TSharedPtr<SWidget>& From, int32 DX, int32 DY, bool bAcross);

	/** Does a pointer event come from a real mouse move (not Slate's synthetic move after a layout change)? */
	SIMCRUISE_API bool IsRealMouseMove(const struct FPointerEvent& Event);

	/** One button hint at the bottom of a screen: the pad glyph (A, B, X, Y, LB, RB, LT, RT, LS, DPad) and what it does. */
	struct FHint
	{
		FString Glyph;
		FString Text;
		/** The keyboard key shown instead when the player uses the keyboard. */
		FString Key;
	};
	SIMCRUISE_API TSharedRef<SWidget> Hints(const TArray<FHint>& Hints, const TAttribute<bool>& bPad);
	/** A pad glyph: a coloured circle (A, B, X, Y) or a white rounded box (LB, RB, LT, RT, ...). */
	SIMCRUISE_API TSharedRef<SWidget> Glyph(const FString& Name, float Px = 34.f);
	SIMCRUISE_API TSharedRef<SWidget> TextOf(const TAttribute<FText>& Text, EWeight Weight, float Px, const FLinearColor& Color);
	SIMCRUISE_API TSharedRef<SWidget> Text(const FString& Text, EWeight Weight, float Px, const FLinearColor& Color);
}

DECLARE_DELEGATE_FiveParams(FSimCruisePaintFn, const FGeometry& /*Geometry*/, FSlateWindowElementList& /*Out*/, int32 /*Layer*/, const FWidgetStyle& /*Style*/, bool /*bEnabled*/);

/** A leaf widget drawn by a function (icons, pointers, maps). */
class SIMCRUISE_API SSimCruisePainted : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruisePainted) : _Size(FVector2D(46.0, 46.0)) {}
		SLATE_ARGUMENT(FVector2D, Size)
		SLATE_EVENT(FSimCruisePaintFn, OnPaintFn)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) { DesiredSizeValue = InArgs._Size; PaintFn = InArgs._OnPaintFn; }
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return DesiredSizeValue; }

private:
	FVector2D DesiredSizeValue;
	FSimCruisePaintFn PaintFn;
};

/**
 * The rows below take the pad's focus (the menu moves Slate's focus with its
 * selection, so a pad, Slate's own navigation and the tests agree on where it
 * is). OnHover runs when the row gets that focus, and when a real mouse moves
 * onto it; a mouse that only sits there while the menu changes under it does not
 * move the selection.
 */
#define SIMCRUISE_FOCUSABLE_ROW() \
	virtual bool SupportsKeyboardFocus() const override { return true; } \
	virtual const FSlateBrush* GetFocusBrush() const override { return nullptr; } \
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override { OnHover.ExecuteIfBound(); return FReply::Handled(); } \
	/* A focusable widget's default OnKeyDown turns the D-pad into a Slate navigation request; */ \
	/* the keys go up to the menu instead, which moves the focus itself. */ \
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { return FReply::Unhandled(); } \
	virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override { return FReply::Unhandled(); } \
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override \
	{ if (SimCruiseTheme::IsRealMouseMove(MouseEvent) && !Focused.Get()) OnHover.ExecuteIfBound(); return FReply::Unhandled(); }

/**
 * A guide-sign row: a white line icon, a title, a note under it and a value
 * on the right. The focused row has a 4 px white inner border, moves 18 px
 * right and gets a white pointer. Clicking it chooses it.
 */
class SIMCRUISE_API SSimCruiseGuideRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruiseGuideRow) : _Icon(SimCruiseTheme::EIcon::Drive), _Width(720.f), _Fill(SimCruiseTheme::Guide()) {}
		SLATE_ARGUMENT(SimCruiseTheme::EIcon, Icon)
		SLATE_ATTRIBUTE(FText, Title)
		SLATE_ATTRIBUTE(FText, Note)
		SLATE_ATTRIBUTE(FText, Value)
		SLATE_ATTRIBUTE(bool, Focused)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(FLinearColor, Fill)
		/** Shown at the right: a pad glyph (the magenta Start row's Y). */
		SLATE_ARGUMENT(FString, Glyph)
		SLATE_EVENT(FSimpleDelegate, OnHover)
		SLATE_EVENT(FSimpleDelegate, OnClick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	SIMCRUISE_FOCUSABLE_ROW()

private:
	TAttribute<bool> Focused;
	FSimpleDelegate OnHover;
	FSimpleDelegate OnClick;
};

/**
 * A slider row (the Your car and Seat & view pages): name | track | value with
 * its unit in smaller mist text, an amber Stock tick with the word under it.
 */
class SIMCRUISE_API SSimCruiseSliderRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruiseSliderRow) : _Width(1060.f), _Stock(-1.f) {}
		SLATE_ATTRIBUTE(FText, Name)
		/** 0..1 along the track. */
		SLATE_ATTRIBUTE(float, Fraction)
		SLATE_ATTRIBUTE(FText, Value)
		SLATE_ATTRIBUTE(FText, Unit)
		SLATE_ATTRIBUTE(bool, Focused)
		SLATE_ARGUMENT(float, Width)
		/** Where the stock value sits on the track (0..1), < 0 for none. */
		SLATE_ATTRIBUTE(float, Stock)
		SLATE_EVENT(FSimpleDelegate, OnHover)
		SLATE_EVENT(FSimpleDelegate, OnClick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	SIMCRUISE_FOCUSABLE_ROW()

private:
	TAttribute<bool> Focused;
	TAttribute<float> Fraction;
	TAttribute<float> Stock;
	FSimpleDelegate OnHover;
	FSimpleDelegate OnClick;
};

/** A segmented control row: a name and options; the chosen one is a white pill. */
class SIMCRUISE_API SSimCruiseSegmentedRow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruiseSegmentedRow) : _Width(1060.f) {}
		SLATE_ATTRIBUTE(FText, Name)
		SLATE_ARGUMENT(TArray<FString>, Options)
		SLATE_ATTRIBUTE(int32, Chosen)
		SLATE_ATTRIBUTE(bool, Focused)
		SLATE_ARGUMENT(float, Width)
		SLATE_EVENT(FSimpleDelegate, OnHover)
		SLATE_EVENT(FSimpleDelegate, OnClick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	SIMCRUISE_FOCUSABLE_ROW()

private:
	TAttribute<bool> Focused;
	FSimpleDelegate OnHover;
	FSimpleDelegate OnClick;
};

/**
 * A page tab on the pause menu's top row. The open page is a white pill with
 * guide-deep text; the tab the pad is on gets a 4 px white ring around it (the
 * same white border as a focused row), so the pad's place on the row is plain
 * even on the open page.
 */
class SIMCRUISE_API SSimCruiseTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimCruiseTab) {}
		SLATE_ARGUMENT(FText, Label)
		SLATE_ATTRIBUTE(bool, Current)
		SLATE_ATTRIBUTE(bool, Focused)
		SLATE_EVENT(FSimpleDelegate, OnHover)
		SLATE_EVENT(FSimpleDelegate, OnClick)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	SIMCRUISE_FOCUSABLE_ROW()

private:
	TAttribute<bool> Focused;
	FSimpleDelegate OnHover;
	FSimpleDelegate OnClick;
};
