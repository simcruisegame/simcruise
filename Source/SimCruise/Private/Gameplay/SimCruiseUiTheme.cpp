// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseUiTheme.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "World/SimCruiseTileData.h"

namespace SimCruiseTheme
{
	FLinearColor Guide() { return FLinearColor(FColor(0x0A, 0x5C, 0x3A)); }
	FLinearColor GuideDeep() { return FLinearColor(FColor(0x06, 0x40, 0x2A)); }
	FLinearColor White() { return FLinearColor(FColor(0xF3, 0xF6, 0xF0)); }
	FLinearColor Glass(float Alpha) { FLinearColor C(FColor(0x16, 0x1B, 0x1D)); C.A = Alpha; return C; }
	FLinearColor Course() { return FLinearColor(FColor(0xE2, 0x3A, 0x9C)); }
	FLinearColor Amber() { return FLinearColor(FColor(0xF4, 0xB2, 0x30)); }
	FLinearColor Mist() { return FLinearColor(FColor(0xA9, 0xB6, 0xAF)); }
	FLinearColor RedZone() { return FLinearColor(FColor(0xE0, 0x3C, 0x31)); }
	FLinearColor PadA() { return FLinearColor(FColor(0x5D, 0xB4, 0x47)); }
	FLinearColor PadB() { return FLinearColor(FColor(0xD8, 0x42, 0x3A)); }
	FLinearColor PadX() { return FLinearColor(FColor(0x3F, 0x7F, 0xD6)); }
	FLinearColor PadY() { return FLinearColor(FColor(0xE8, 0xB9, 0x23)); }

	namespace
	{
		const TCHAR* WeightName(EWeight Weight)
		{
			switch (Weight)
			{
			case EWeight::SemiBold: return TEXT("SemiBold");
			case EWeight::Bold: return TEXT("Bold");
			case EWeight::ExtraBold: return TEXT("ExtraBold");
			case EWeight::Black: return TEXT("Black");
			default: return TEXT("Regular");
			}
		}

		/** One composite font per weight, made once and kept to the end of the process. */
		TSharedPtr<const FCompositeFont>& FontSlot(EWeight Weight)
		{
			static TArray<TSharedPtr<const FCompositeFont>>* Slots = new TArray<TSharedPtr<const FCompositeFont>>({ nullptr, nullptr, nullptr, nullptr, nullptr });
			return (*Slots)[static_cast<int32>(Weight)];
		}

		/** Slate font sizes are points at 96 DPI: a pixel is 0.75 pt. */
		constexpr float PointsPerPx = 0.75f;
	}

	FString FontPath(EWeight Weight)
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("fonts"), TEXT("overpass"), FString::Printf(TEXT("Overpass-%s.ttf"), WeightName(Weight)));
	}

	bool HasOverpass()
	{
		static const bool bHas = IFileManager::Get().FileExists(*FontPath(EWeight::Regular));
		return bHas;
	}

	FSlateFontInfo Font(EWeight Weight, float Px)
	{
		const float Points = FMath::Max(1.f, Px * PointsPerPx);
		if (!HasOverpass())
		{
			return FCoreStyle::GetDefaultFontStyle(Weight == EWeight::Regular ? "Regular" : "Bold", FMath::RoundToInt(Points));
		}
		TSharedPtr<const FCompositeFont>& Slot = FontSlot(Weight);
		if (!Slot.IsValid())
		{
			const FString Path = FontPath(Weight);
			// Lazy load: FreeType reads the face from the loose file on first use (packaged builds copy data/).
			Slot = MakeShared<FCompositeFont>(FName(*FString::Printf(TEXT("Overpass%s"), WeightName(Weight))), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		}
		return FSlateFontInfo(Slot, Points);
	}

	const FSlateBrush* RoundedBrush(const FLinearColor& Fill, float Radius, const FLinearColor& Outline, float OutlineWidth)
	{
		static TMap<FString, TSharedPtr<FSlateBrush>>* Brushes = new TMap<FString, TSharedPtr<FSlateBrush>>();
		const FString Key = FString::Printf(TEXT("%s|%.1f|%s|%.1f"), *Fill.ToString(), Radius, *Outline.ToString(), OutlineWidth);
		if (const TSharedPtr<FSlateBrush>* Found = Brushes->Find(Key)) return Found->Get();
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateRoundedBoxBrush>(Fill, Radius, Outline, OutlineWidth);
		Brushes->Add(Key, Brush);
		return Brush.Get();
	}

	const FSlateBrush* WhiteBrush()
	{
		return FCoreStyle::Get().GetBrush("WhiteBrush");
	}

	// ------------------------------------------------------------------ strokes

	namespace
	{
		using FStroke = TArray<FVector2D>;

		FStroke Circle(const FVector2D& C, double R, int32 Segments = 28, double From = 0.0, double To = UE_DOUBLE_TWO_PI)
		{
			FStroke S;
			for (int32 i = 0; i <= Segments; ++i)
			{
				const double T = From + (To - From) * i / Segments;
				S.Add(C + FVector2D(FMath::Cos(T), FMath::Sin(T)) * R);
			}
			return S;
		}

		/** A line with an arrow head at its last point. */
		void Arrow(TArray<FStroke>& Out, FStroke Line, double Head = 0.22)
		{
			if (Line.Num() < 2) return;
			const FVector2D Tip = Line.Last();
			const FVector2D Dir = (Tip - Line[Line.Num() - 2]).GetSafeNormal();
			const FVector2D Back = -Dir * Head;
			const double A = FMath::DegreesToRadians(40.0);
			const FVector2D L = Tip + FVector2D(Back.X * FMath::Cos(A) - Back.Y * FMath::Sin(A), Back.X * FMath::Sin(A) + Back.Y * FMath::Cos(A));
			const FVector2D R = Tip + FVector2D(Back.X * FMath::Cos(-A) - Back.Y * FMath::Sin(-A), Back.X * FMath::Sin(-A) + Back.Y * FMath::Cos(-A));
			Out.Add(MoveTemp(Line));
			Out.Add({ L, Tip, R });
		}

		void Mirror(TArray<FStroke>& Strokes)
		{
			for (FStroke& S : Strokes) for (FVector2D& P : S) P.X = 1.0 - P.X;
		}
	}

	void TurnStrokes(ESimCruiseTurn Turn, TArray<TArray<FVector2D>>& Out)
	{
		Out.Reset();
		const bool bLeft = Turn == ESimCruiseTurn::Left || Turn == ESimCruiseTurn::SlightLeft || Turn == ESimCruiseTurn::SharpLeft;
		switch (Turn)
		{
		case ESimCruiseTurn::Right: case ESimCruiseTurn::Left:
			Arrow(Out, { FVector2D(0.34, 0.92), FVector2D(0.34, 0.52), FVector2D(0.38, 0.44), FVector2D(0.46, 0.40), FVector2D(0.86, 0.40) });
			break;
		case ESimCruiseTurn::SlightRight: case ESimCruiseTurn::SlightLeft:
			Arrow(Out, { FVector2D(0.40, 0.92), FVector2D(0.40, 0.58), FVector2D(0.74, 0.18) });
			break;
		case ESimCruiseTurn::SharpRight: case ESimCruiseTurn::SharpLeft:
			Arrow(Out, { FVector2D(0.32, 0.92), FVector2D(0.32, 0.22), FVector2D(0.78, 0.70) });
			break;
		case ESimCruiseTurn::UTurn:
		{
			FStroke S = { FVector2D(0.70, 0.92), FVector2D(0.70, 0.42) };
			S.Append(Circle(FVector2D(0.50, 0.42), 0.20, 12, 0.0, -UE_DOUBLE_PI));
			S.Add(FVector2D(0.30, 0.72));
			Arrow(Out, MoveTemp(S));
			break;
		}
		case ESimCruiseTurn::Arrive:
			Out.Add(Circle(FVector2D(0.5, 0.36), 0.20));
			Out.Add({ FVector2D(0.36, 0.50), FVector2D(0.5, 0.90), FVector2D(0.64, 0.50) });
			break;
		default:
			Arrow(Out, { FVector2D(0.5, 0.92), FVector2D(0.5, 0.10) });
			break;
		}
		if (bLeft) Mirror(Out);
	}

	void IconStrokes(EIcon Icon, TArray<TArray<FVector2D>>& Out)
	{
		Out.Reset();
		switch (Icon)
		{
		case EIcon::Drive: // a steering wheel
			Out.Add(Circle(FVector2D(0.5, 0.5), 0.42));
			Out.Add(Circle(FVector2D(0.5, 0.5), 0.10, 14));
			Out.Add({ FVector2D(0.09, 0.46), FVector2D(0.40, 0.48) });
			Out.Add({ FVector2D(0.60, 0.48), FVector2D(0.91, 0.46) });
			Out.Add({ FVector2D(0.5, 0.60), FVector2D(0.5, 0.92) });
			break;
		case EIcon::Search:
			Out.Add(Circle(FVector2D(0.42, 0.42), 0.28));
			Out.Add({ FVector2D(0.63, 0.63), FVector2D(0.90, 0.90) });
			break;
		case EIcon::Route:
			Out.Add({ FVector2D(0.22, 0.80), FVector2D(0.22, 0.60), FVector2D(0.30, 0.52), FVector2D(0.70, 0.48), FVector2D(0.78, 0.40), FVector2D(0.78, 0.22) });
			Out.Add(Circle(FVector2D(0.22, 0.86), 0.07, 12));
			Out.Add(Circle(FVector2D(0.78, 0.15), 0.07, 12));
			break;
		case EIcon::Star:
		{
			FStroke S;
			for (int32 i = 0; i <= 10; ++i)
			{
				const double T = -UE_DOUBLE_HALF_PI + UE_DOUBLE_PI * i / 5.0;
				S.Add(FVector2D(0.5, 0.54) + FVector2D(FMath::Cos(T), FMath::Sin(T)) * (i % 2 ? 0.18 : 0.42));
			}
			Out.Add(MoveTemp(S));
			break;
		}
		case EIcon::Traffic: // two cars, one behind the other
			Out.Add({ FVector2D(0.10, 0.44), FVector2D(0.18, 0.28), FVector2D(0.46, 0.28), FVector2D(0.54, 0.44), FVector2D(0.54, 0.56), FVector2D(0.10, 0.56), FVector2D(0.10, 0.44), FVector2D(0.54, 0.44) });
			Out.Add({ FVector2D(0.46, 0.74), FVector2D(0.54, 0.58), FVector2D(0.82, 0.58), FVector2D(0.90, 0.74), FVector2D(0.90, 0.86), FVector2D(0.46, 0.86), FVector2D(0.46, 0.74), FVector2D(0.90, 0.74) });
			break;
		case EIcon::Car:
			Out.Add({ FVector2D(0.06, 0.66), FVector2D(0.06, 0.52), FVector2D(0.22, 0.48), FVector2D(0.36, 0.32), FVector2D(0.66, 0.32), FVector2D(0.80, 0.48), FVector2D(0.94, 0.52), FVector2D(0.94, 0.66), FVector2D(0.06, 0.66) });
			Out.Add(Circle(FVector2D(0.26, 0.68), 0.09, 14));
			Out.Add(Circle(FVector2D(0.74, 0.68), 0.09, 14));
			break;
		case EIcon::Gearbox:
			Out.Add({ FVector2D(0.22, 0.18), FVector2D(0.22, 0.82) });
			Out.Add({ FVector2D(0.50, 0.18), FVector2D(0.50, 0.82) });
			Out.Add({ FVector2D(0.78, 0.18), FVector2D(0.78, 0.50) });
			Out.Add({ FVector2D(0.22, 0.50), FVector2D(0.78, 0.50) });
			break;
		case EIcon::Wrench:
			Out.Add({ FVector2D(0.16, 0.84), FVector2D(0.56, 0.44) });
			Out.Add(Circle(FVector2D(0.68, 0.32), 0.18, 16, UE_DOUBLE_PI * 0.95, UE_DOUBLE_PI * 2.55));
			break;
		case EIcon::Seat:
			Out.Add({ FVector2D(0.30, 0.10), FVector2D(0.24, 0.62), FVector2D(0.70, 0.62), FVector2D(0.80, 0.90) });
			Out.Add({ FVector2D(0.24, 0.62), FVector2D(0.30, 0.90) });
			break;
		case EIcon::Eye:
		{
			FStroke Top = Circle(FVector2D(0.5, 0.86), 0.55, 18, -UE_DOUBLE_PI * 0.80, -UE_DOUBLE_PI * 0.20);
			FStroke Bottom = Circle(FVector2D(0.5, 0.14), 0.55, 18, UE_DOUBLE_PI * 0.20, UE_DOUBLE_PI * 0.80);
			Out.Add(MoveTemp(Top));
			Out.Add(MoveTemp(Bottom));
			Out.Add(Circle(FVector2D(0.5, 0.5), 0.13, 14));
			break;
		}
		case EIcon::Sound:
			Out.Add({ FVector2D(0.10, 0.38), FVector2D(0.26, 0.38), FVector2D(0.46, 0.18), FVector2D(0.46, 0.82), FVector2D(0.26, 0.62), FVector2D(0.10, 0.62), FVector2D(0.10, 0.38) });
			Out.Add(Circle(FVector2D(0.48, 0.5), 0.20, 10, -UE_DOUBLE_PI * 0.30, UE_DOUBLE_PI * 0.30));
			Out.Add(Circle(FVector2D(0.48, 0.5), 0.36, 12, -UE_DOUBLE_PI * 0.30, UE_DOUBLE_PI * 0.30));
			break;
		case EIcon::Hud:
			Out.Add(Circle(FVector2D(0.5, 0.62), 0.40, 20, -UE_DOUBLE_PI, 0.0));
			Out.Add({ FVector2D(0.5, 0.62), FVector2D(0.74, 0.38) });
			Out.Add({ FVector2D(0.10, 0.80), FVector2D(0.90, 0.80) });
			break;
		case EIcon::Rumble:
			Out.Add({ FVector2D(0.24, 0.36), FVector2D(0.76, 0.36), FVector2D(0.90, 0.74), FVector2D(0.72, 0.80), FVector2D(0.62, 0.62), FVector2D(0.38, 0.62), FVector2D(0.28, 0.80), FVector2D(0.10, 0.74), FVector2D(0.24, 0.36) });
			Out.Add({ FVector2D(0.30, 0.12), FVector2D(0.36, 0.22) });
			Out.Add({ FVector2D(0.70, 0.12), FVector2D(0.64, 0.22) });
			break;
		case EIcon::Stick:
			Out.Add(Circle(FVector2D(0.5, 0.5), 0.40));
			Out.Add(Circle(FVector2D(0.62, 0.42), 0.16, 16));
			break;
		case EIcon::Trigger:
			Out.Add({ FVector2D(0.20, 0.86), FVector2D(0.24, 0.30), FVector2D(0.40, 0.14), FVector2D(0.70, 0.14), FVector2D(0.80, 0.30), FVector2D(0.80, 0.86) });
			Out.Add({ FVector2D(0.20, 0.86), FVector2D(0.80, 0.86) });
			break;
		case EIcon::Map:
			Out.Add({ FVector2D(0.08, 0.22), FVector2D(0.36, 0.12), FVector2D(0.64, 0.22), FVector2D(0.92, 0.12), FVector2D(0.92, 0.78), FVector2D(0.64, 0.88), FVector2D(0.36, 0.78), FVector2D(0.08, 0.88), FVector2D(0.08, 0.22) });
			Out.Add({ FVector2D(0.36, 0.12), FVector2D(0.36, 0.78) });
			Out.Add({ FVector2D(0.64, 0.22), FVector2D(0.64, 0.88) });
			break;
		case EIcon::Lights:
			Out.Add(Circle(FVector2D(0.40, 0.5), 0.30, 16, -UE_DOUBLE_HALF_PI, UE_DOUBLE_HALF_PI));
			Out.Add({ FVector2D(0.40, 0.20), FVector2D(0.40, 0.80) });
			Out.Add({ FVector2D(0.80, 0.30), FVector2D(0.94, 0.26) });
			Out.Add({ FVector2D(0.80, 0.50), FVector2D(0.96, 0.50) });
			Out.Add({ FVector2D(0.80, 0.70), FVector2D(0.94, 0.74) });
			break;
		case EIcon::Lens:
			Out.Add({ FVector2D(0.08, 0.30), FVector2D(0.92, 0.30), FVector2D(0.92, 0.80), FVector2D(0.08, 0.80), FVector2D(0.08, 0.30) });
			Out.Add(Circle(FVector2D(0.5, 0.55), 0.16, 16));
			Out.Add({ FVector2D(0.34, 0.30), FVector2D(0.40, 0.18), FVector2D(0.60, 0.18), FVector2D(0.66, 0.30) });
			break;
		case EIcon::Screen:
			Out.Add({ FVector2D(0.08, 0.16), FVector2D(0.92, 0.16), FVector2D(0.92, 0.70), FVector2D(0.08, 0.70), FVector2D(0.08, 0.16) });
			Out.Add({ FVector2D(0.5, 0.70), FVector2D(0.5, 0.86) });
			Out.Add({ FVector2D(0.30, 0.88), FVector2D(0.70, 0.88) });
			break;
		case EIcon::Flag:
			Out.Add({ FVector2D(0.24, 0.92), FVector2D(0.24, 0.10) });
			Out.Add({ FVector2D(0.24, 0.14), FVector2D(0.80, 0.24), FVector2D(0.24, 0.50) });
			break;
		case EIcon::Back:
		default:
			Arrow(Out, { FVector2D(0.88, 0.5), FVector2D(0.12, 0.5) }, 0.30);
			break;
		}
	}

	void PaintStrokes(const TArray<TArray<FVector2D>>& Strokes, FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry,
		const FVector2D& Origin, float Size, const FLinearColor& Color, float Thickness)
	{
		for (const TArray<FVector2D>& Stroke : Strokes)
		{
			TArray<FVector2f> Points;
			Points.Reserve(Stroke.Num());
			for (const FVector2D& P : Stroke) Points.Add(FVector2f(Origin + P * Size));
			FSlateDrawElement::MakeLines(Out, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Color, true, Thickness);
		}
	}

	ENav NavOf(const FKey& Key)
	{
		if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::W) return ENav::Up;
		if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::S) return ENav::Down;
		if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::A) return ENav::Left;
		if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::D) return ENav::Right;
		if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Virtual_Accept) return ENav::Accept;
		if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Virtual_Back) return ENav::Back;
		if (Key == EKeys::Gamepad_LeftShoulder || Key == EKeys::Q || Key == EKeys::PageUp) return ENav::PrevPage;
		if (Key == EKeys::Gamepad_RightShoulder || Key == EKeys::E || Key == EKeys::PageDown) return ENav::NextPage;
		if (Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::X || Key == EKeys::Delete) return ENav::X;
		if (Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Y) return ENav::Y;
		if (Key == EKeys::Gamepad_RightTrigger || Key == EKeys::Equals || Key == EKeys::Add) return ENav::ZoomIn;
		if (Key == EKeys::Gamepad_LeftTrigger || Key == EKeys::Hyphen || Key == EKeys::Subtract) return ENav::ZoomOut;
		if (Key == EKeys::Gamepad_Special_Right || Key == EKeys::Tab) return ENav::Menu;
		return ENav::None;
	}

	namespace
	{
		bool GPadInUse = false;

		/** Sees every key, stick and mouse event before Slate routes it; takes none. */
		class FPadWatcher : public IInputProcessor
		{
		public:
			virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
			virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
			{
				GPadInUse = InKeyEvent.GetKey().IsGamepadKey();
				return false;
			}
			virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
			{
				if (InAnalogInputEvent.GetKey().IsGamepadKey() && FMath::Abs(InAnalogInputEvent.GetAnalogValue()) > 0.5f) GPadInUse = true;
				return false;
			}
			virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
			{
				if (IsRealMouseMove(MouseEvent)) GPadInUse = false;
				return false;
			}
			virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
			{
				GPadInUse = false;
				return false;
			}
			virtual const TCHAR* GetDebugName() const override { return TEXT("SimCruisePadWatcher"); }
		};

		TSharedPtr<FPadWatcher> GPadWatcher;
	}

	bool IsPadInUse() { return GPadInUse; }
	void SetPadInUse(bool bPad) { GPadInUse = bPad; }

	void EnsurePadWatcher()
	{
		if (GPadWatcher.IsValid() || !FSlateApplication::IsInitialized()) return;
		GPadWatcher = MakeShared<FPadWatcher>();
		FSlateApplication::Get().RegisterInputPreProcessor(GPadWatcher);
		// Off before the module's code goes away at exit.
		FCoreDelegates::OnPreExit.AddLambda([]()
		{
			if (GPadWatcher.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(GPadWatcher);
			GPadWatcher.Reset();
		});
	}

	TAttribute<FText> PadOrKeys(const FString& Pad, const FString& Keys)
	{
		EnsurePadWatcher();
		const FText PadText = FText::FromString(Pad), KeyText = FText::FromString(Keys);
		return TAttribute<FText>::CreateLambda([PadText, KeyText]() { return IsPadInUse() ? PadText : KeyText; });
	}

	bool IsLeftStick(const FKey& Axis)
	{
		return Axis == EKeys::Gamepad_LeftX || Axis == EKeys::Gamepad_LeftY;
	}

	ENav StickNav(const FKey& Axis, float Value, FStickRepeat& State, double Now)
	{
		// Past 0.6 is a push; back under 0.35 lets go. Held, it repeats after 0.4 s, then every 0.16 s.
		const bool bY = Axis == EKeys::Gamepad_LeftY;
		if (!bY && Axis != EKeys::Gamepad_LeftX) return ENav::None;
		ENav& Held = bY ? State.HeldY : State.HeldX;
		double& Next = bY ? State.NextY : State.NextX;
		if (FMath::Abs(Value) < 0.35f) { Held = ENav::None; return ENav::None; }
		if (FMath::Abs(Value) < 0.6f) return ENav::None;
		const ENav Dir = bY ? (Value > 0.f ? ENav::Up : ENav::Down) : (Value > 0.f ? ENav::Right : ENav::Left);
		if (Held != Dir) { Held = Dir; Next = Now + 0.4; return Dir; }
		if (Now >= Next) { Next = Now + 0.16; return Dir; }
		return ENav::None;
	}

	bool FocusPad(const TSharedPtr<SWidget>& Widget)
	{
		if (!Widget.IsValid() || !FSlateApplication::IsInitialized()) return false;
		FSlateApplication& App = FSlateApplication::Get();
		// Each user by index: SetAllUserFocus looks only in real windows, this also finds a virtual one (the tests).
		if (App.GetUserFocusedWidget(0) != Widget)
		{
			App.ForEachUser([&App, &Widget](FSlateUser& User) { App.SetUserFocus(User.GetUserIndex(), Widget, EFocusCause::SetDirectly); });
		}
		return App.GetUserFocusedWidget(0) == Widget;
	}

	TSharedPtr<SWidget> StepGrid(const TArray<TArray<TSharedPtr<SWidget>>>& Rows, const TSharedPtr<SWidget>& From, int32 DX, int32 DY, bool bAcross)
	{
		int32 R = INDEX_NONE, C = 0;
		for (int32 i = 0; i < Rows.Num() && R == INDEX_NONE; ++i) for (int32 j = 0; j < Rows[i].Num(); ++j) if (Rows[i][j] == From) { R = i; C = j; break; }
		if (R == INDEX_NONE) return nullptr;
		if (DY != 0)
		{
			// From the left edge across: a single wide control lands on the first of the row above.
			const float Across = static_cast<float>(C) / Rows[R].Num();
			int32 Next = R + DY;
			while (Rows.IsValidIndex(Next) && Rows[Next].Num() == 0) Next += DY;
			if (!Rows.IsValidIndex(Next)) return Rows[R][C];
			return Rows[Next][FMath::Clamp(FMath::FloorToInt(Across * Rows[Next].Num()), 0, Rows[Next].Num() - 1)];
		}
		C += DX;
		if (C >= Rows[R].Num() || C < 0)
		{
			if (!bAcross) return Rows[R][(C + Rows[R].Num()) % Rows[R].Num()];
			// On into the next (or the last) row that has any control.
			do { R = (R + (DX > 0 ? 1 : Rows.Num() - 1)) % Rows.Num(); } while (Rows[R].Num() == 0);
			C = DX > 0 ? 0 : Rows[R].Num() - 1;
		}
		return Rows[R][C];
	}

	bool IsRealMouseMove(const FPointerEvent& Event)
	{
		return !Event.GetCursorDelta().IsNearlyZero();
	}

	TSharedRef<SWidget> TextOf(const TAttribute<FText>& InText, EWeight Weight, float Px, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(InText).Font(Font(Weight, Px)).ColorAndOpacity(Color);
	}

	TSharedRef<SWidget> Text(const FString& InText, EWeight Weight, float Px, const FLinearColor& Color)
	{
		return TextOf(TAttribute<FText>(FText::FromString(InText)), Weight, Px, Color);
	}

	TSharedRef<SWidget> Glyph(const FString& Name, float Px)
	{
		const bool bFace = Name == TEXT("A") || Name == TEXT("B") || Name == TEXT("X") || Name == TEXT("Y");
		const FLinearColor Fill = Name == TEXT("A") ? PadA() : Name == TEXT("B") ? PadB() : Name == TEXT("X") ? PadX() : Name == TEXT("Y") ? PadY() : White();
		const float H = Px;
		const float W = bFace ? Px : FMath::Max(Px * 1.5f, Px * 0.45f * Name.Len() + Px * 0.6f);
		return SNew(SBox).WidthOverride(W).HeightOverride(H).VAlign(VAlign_Center).HAlign(HAlign_Center)
			[
				SNew(SBorder)
				.BorderImage(RoundedBrush(Fill, bFace ? H * 0.5f : 6.f))
				.Padding(0.f)
				.HAlign(HAlign_Center).VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(W).HeightOverride(H).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Name)).Font(Font(EWeight::Black, Px * (bFace ? 0.62f : 0.48f)))
						.ColorAndOpacity(bFace ? FLinearColor(FColor(0x10, 0x14, 0x16)) : GuideDeep())
					]
				]
			];
	}

	TSharedRef<SWidget> Hints(const TArray<FHint>& InHints, const TAttribute<bool>& /*bPadOfScreen*/)
	{
		// Every screen follows the last device used (one flag for all), not its own guess.
		EnsurePadWatcher();
		const TAttribute<bool> bPad = TAttribute<bool>::CreateStatic(&IsPadInUse);
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (const FHint& Hint : InHints)
		{
			// The pad glyph or the keyboard key, whichever the player uses now.
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBox).Visibility_Lambda([bPad]() { return bPad.Get() ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						Glyph(Hint.Glyph)
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SBox).Visibility_Lambda([bPad]() { return bPad.Get() ? EVisibility::Collapsed : EVisibility::Visible; })
					[
						Glyph(Hint.Key.IsEmpty() ? Hint.Glyph : Hint.Key)
					]
				]
			];
			Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 34, 0)
			[
				Text(Hint.Text, EWeight::SemiBold, 23.f, White())
			];
		}
		return Row;
	}
}

// ====================================================================== widgets

int32 SSimCruisePainted::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	PaintFn.ExecuteIfBound(AllottedGeometry, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	return LayerId + 4;
}

namespace
{
	using namespace SimCruiseTheme;

	TSharedRef<SWidget> IconWidget(EIcon Icon, float Size, FLinearColor Color = SimCruiseTheme::White())
	{
		return SNew(SSimCruisePainted).Size(FVector2D(Size, Size))
			.OnPaintFn_Lambda([Icon, Size, Color](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool)
			{
				TArray<TArray<FVector2D>> Strokes;
				IconStrokes(Icon, Strokes);
				PaintStrokes(Strokes, Out, Layer, G, FVector2D::ZeroVector, Size, Color, FMath::Max(2.f, Size / 14.f));
			});
	}

	/** The white pointer left of a focused row. */
	TSharedRef<SWidget> Pointer(TAttribute<bool> Focused, float Height)
	{
		return SNew(SSimCruisePainted).Size(FVector2D(22.0, Height))
			.Visibility_Lambda([Focused]() { return Focused.Get() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
			.OnPaintFn_Lambda([Height](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool)
			{
				const float M = Height * 0.5f;
				TArray<FVector2f> Tri = { FVector2f(2.f, M - 13.f), FVector2f(18.f, M), FVector2f(2.f, M + 13.f), FVector2f(2.f, M - 13.f) };
				for (int32 i = 0; i < 6; ++i)
				{
					// A filled triangle out of short strokes (Slate has no polygon fill without a brush).
					TArray<FVector2f> Line = { FVector2f(2.f + i * 2.4f, M - 13.f + i * 2.1f), FVector2f(2.f + i * 2.4f, M + 13.f - i * 2.1f) };
					FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), MoveTemp(Line), ESlateDrawEffect::None, SimCruiseTheme::White(), true, 3.f);
				}
				FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), MoveTemp(Tri), ESlateDrawEffect::None, SimCruiseTheme::White(), true, 2.f);
			});
	}

	FSlateRenderTransform Nudge(const TAttribute<bool>& Focused)
	{
		return FSlateRenderTransform(FVector2f(Focused.Get() ? 18.f : 0.f, 0.f));
	}
}

void SSimCruiseGuideRow::Construct(const FArguments& InArgs)
{
	Focused = InArgs._Focused;
	OnHover = InArgs._OnHover;
	OnClick = InArgs._OnClick;
	const FLinearColor Fill = InArgs._Fill;
	const TAttribute<bool> F = Focused;
	TSharedRef<SHorizontalBox> Inner = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 24, 0)
		[
			IconWidget(InArgs._Icon, 46.f)
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Text(InArgs._Title).Font(Font(EWeight::ExtraBold, 34.f)).ColorAndOpacity(SimCruiseTheme::White())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(STextBlock).Text(InArgs._Note).Font(Font(EWeight::Regular, 21.f)).ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f, 0.86f))
				.Visibility_Lambda([Note = InArgs._Note]() { return Note.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(20, 0, 0, 0)
		[
			SNew(STextBlock).Text(InArgs._Value).Font(Font(EWeight::Bold, 26.f)).ColorAndOpacity(SimCruiseTheme::White())
		];
	if (!InArgs._Glyph.IsEmpty())
	{
		Inner->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 0, 0)[Glyph(InArgs._Glyph, 38.f)];
	}
	ChildSlot
	[
		SNew(SHorizontalBox)
		.RenderTransform_Lambda([F]() { return Nudge(F); })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			Pointer(F, 40.f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(InArgs._Width)
			[
				SNew(SBorder)
				.BorderImage_Lambda([F, Fill]() { return F.Get() ? RoundedBrush(Fill, 12.f, SimCruiseTheme::White(), 4.f) : RoundedBrush(Fill, 12.f, Fill * 0.8f, 1.f); })
				.Padding(FMargin(28.f, 20.f))
				[
					Inner
				]
			]
		]
	];
}

FReply SSimCruiseGuideRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	OnHover.ExecuteIfBound();
	OnClick.ExecuteIfBound();
	return FReply::Handled();
}

void SSimCruiseGuideRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsRealMouseMove(MouseEvent)) OnHover.ExecuteIfBound();
}

void SSimCruiseSliderRow::Construct(const FArguments& InArgs)
{
	Focused = InArgs._Focused;
	Fraction = InArgs._Fraction;
	Stock = InArgs._Stock;
	OnHover = InArgs._OnHover;
	OnClick = InArgs._OnClick;
	const TAttribute<bool> F = Focused;
	const TAttribute<float> Frac = Fraction;
	const TAttribute<float> StockAt = Stock;
	ChildSlot
	[
		SNew(SHorizontalBox)
		.RenderTransform_Lambda([F]() { return Nudge(F); })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			Pointer(F, 40.f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(InArgs._Width).HeightOverride(86.f)
			[
				SNew(SBorder)
				.BorderImage_Lambda([F]() { return F.Get() ? RoundedBrush(SimCruiseTheme::Guide(), 12.f, SimCruiseTheme::White(), 4.f) : RoundedBrush(SimCruiseTheme::Glass(), 12.f); })
				.Padding(FMargin(28.f, 0.f))
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(330.f)
						[
							SNew(STextBlock).Text(InArgs._Name).Font(Font(EWeight::Bold, 28.f)).ColorAndOpacity(SimCruiseTheme::White())
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SSimCruisePainted).Size(FVector2D(300.0, 60.0))
						.OnPaintFn_Lambda([Frac, StockAt, F](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool)
						{
							const FVector2f Size = FVector2f(G.GetLocalSize());
							const float Y = Size.Y * 0.40f, X0 = 12.f, X1 = Size.X - 12.f;
							const float T = FMath::Clamp(Frac.Get(), 0.f, 1.f);
							const float X = FMath::Lerp(X0, X1, T);
							FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), { FVector2f(X0, Y), FVector2f(X1, Y) }, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.25f), true, 6.f);
							FSlateDrawElement::MakeLines(Out, Layer + 1, G.ToPaintGeometry(), { FVector2f(X0, Y), FVector2f(X, Y) }, ESlateDrawEffect::None, SimCruiseTheme::White(), true, 6.f);
							const float S = StockAt.Get();
							if (S >= 0.f)
							{
								const float SX = FMath::Lerp(X0, X1, FMath::Clamp(S, 0.f, 1.f));
								FSlateDrawElement::MakeLines(Out, Layer + 2, G.ToPaintGeometry(), { FVector2f(SX, Y - 13.f), FVector2f(SX, Y + 13.f) }, ESlateDrawEffect::None, SimCruiseTheme::Amber(), true, 4.f);
								const FSlateFontInfo Small = Font(EWeight::Bold, 15.f);
								FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(60.f, 20.f), FSlateLayoutTransform(FVector2f(SX - 19.f, Y + 14.f))), FString(TEXT("Stock")), Small, ESlateDrawEffect::None, SimCruiseTheme::Amber());
							}
							const float R = F.Get() ? 13.f : 10.f;
							FSlateDrawElement::MakeBox(Out, Layer + 3, G.ToPaintGeometry(FVector2f(2 * R, 2 * R), FSlateLayoutTransform(FVector2f(X - R, Y - R))), RoundedBrush(SimCruiseTheme::White(), R));
						})
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(200.f).HAlign(HAlign_Right)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
							[
								SNew(STextBlock).Text(InArgs._Value).Font(Font(EWeight::ExtraBold, 32.f)).ColorAndOpacity(SimCruiseTheme::White())
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).Padding(6, 0, 0, 4)
							[
								SNew(STextBlock).Text(InArgs._Unit).Font(Font(EWeight::SemiBold, 20.f)).ColorAndOpacity(SimCruiseTheme::Mist())
							]
						]
					]
				]
			]
		]
	];
}

FReply SSimCruiseSliderRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	OnHover.ExecuteIfBound();
	OnClick.ExecuteIfBound();
	return FReply::Handled();
}

void SSimCruiseSliderRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsRealMouseMove(MouseEvent)) OnHover.ExecuteIfBound();
}

void SSimCruiseSegmentedRow::Construct(const FArguments& InArgs)
{
	Focused = InArgs._Focused;
	OnHover = InArgs._OnHover;
	OnClick = InArgs._OnClick;
	const TAttribute<bool> F = Focused;
	const TAttribute<int32> Chosen = InArgs._Chosen;
	TSharedRef<SHorizontalBox> Options = SNew(SHorizontalBox);
	for (int32 i = 0; i < InArgs._Options.Num(); ++i)
	{
		Options->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SBorder)
			.BorderImage_Lambda([Chosen, i]() { return Chosen.Get() == i ? RoundedBrush(SimCruiseTheme::White(), 22.f) : RoundedBrush(FLinearColor(1.f, 1.f, 1.f, 0.10f), 22.f); })
			.Padding(FMargin(24.f, 8.f))
			[
				SNew(STextBlock).Text(FText::FromString(InArgs._Options[i])).Font(Font(EWeight::Bold, 25.f))
				.ColorAndOpacity_Lambda([Chosen, i]() { return FSlateColor(Chosen.Get() == i ? SimCruiseTheme::GuideDeep() : SimCruiseTheme::White()); })
			]
		];
	}
	ChildSlot
	[
		SNew(SHorizontalBox)
		.RenderTransform_Lambda([F]() { return Nudge(F); })
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
		[
			Pointer(F, 40.f)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(InArgs._Width).HeightOverride(86.f)
			[
				SNew(SBorder)
				.BorderImage_Lambda([F]() { return F.Get() ? RoundedBrush(SimCruiseTheme::Guide(), 12.f, SimCruiseTheme::White(), 4.f) : RoundedBrush(SimCruiseTheme::Glass(), 12.f); })
				.Padding(FMargin(28.f, 0.f))
				.VAlign(VAlign_Center)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SBox).WidthOverride(330.f)
						[
							SNew(STextBlock).Text(InArgs._Name).Font(Font(EWeight::Bold, 28.f)).ColorAndOpacity(SimCruiseTheme::White())
						]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						Options
					]
				]
			]
		]
	];
}

FReply SSimCruiseSegmentedRow::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	OnHover.ExecuteIfBound();
	OnClick.ExecuteIfBound();
	return FReply::Handled();
}

void SSimCruiseSegmentedRow::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	if (IsRealMouseMove(MouseEvent)) OnHover.ExecuteIfBound();
}

void SSimCruiseTab::Construct(const FArguments& InArgs)
{
	Focused = InArgs._Focused;
	OnHover = InArgs._OnHover;
	OnClick = InArgs._OnClick;
	const TAttribute<bool> Current = InArgs._Current;
	const TAttribute<bool> F = Focused;
	ChildSlot
	[
		// The ring sits 5 px outside the pill, so it shows round the white open tab too.
		SNew(SBorder)
		.BorderImage_Lambda([F]() { return F.Get() ? RoundedBrush(FLinearColor::Transparent, 29.f, SimCruiseTheme::White(), 4.f) : RoundedBrush(FLinearColor::Transparent, 29.f); })
		.Padding(5.f)
		[
			SNew(SBorder)
			.BorderImage_Lambda([Current, F]()
			{
				if (Current.Get()) return RoundedBrush(SimCruiseTheme::White(), 24.f);
				return F.Get() ? RoundedBrush(SimCruiseTheme::Guide(), 24.f) : RoundedBrush(FLinearColor::Transparent, 24.f);
			})
			.Padding(FMargin(26.f, 9.f))
			[
				SNew(STextBlock).Text(InArgs._Label).Font(Font(EWeight::Bold, 26.f))
				.ColorAndOpacity_Lambda([Current]() { return FSlateColor(Current.Get() ? SimCruiseTheme::GuideDeep() : SimCruiseTheme::White()); })
			]
		]
	];
}

FReply SSimCruiseTab::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	OnClick.ExecuteIfBound();
	return FReply::Handled();
}
