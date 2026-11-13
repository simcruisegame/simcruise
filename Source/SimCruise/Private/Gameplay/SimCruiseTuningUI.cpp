// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseTuningUI.h"

#include "Gameplay/SimCruiseAtlasUI.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCruiseTuning"

void SSimCruiseTuningMenu::Construct(const FArguments& InArgs)
{
	Vehicle = InArgs._Vehicle;
	OnClose = InArgs._OnClose;
	if (const ASimCruiseVehicle* Car = Vehicle.Get())
	{
		Stock = Car->GetStockTuning();
		Working = Car->GetTuning();
	}
	Rows = {
		{ LOCTEXT("FrontSpring", "Front springs"), &FSimCruiseTuning::FrontSpringLbIn, 10.0, TEXT("lb/in"), 0 },
		{ LOCTEXT("RearSpring", "Rear springs"), &FSimCruiseTuning::RearSpringLbIn, 10.0, TEXT("lb/in"), 0 },
		{ LOCTEXT("Damping", "Damping"), &FSimCruiseTuning::DampingPct, 5.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("RideHeight", "Ride height"), &FSimCruiseTuning::RideHeightMm, 5.0, TEXT("mm"), 0 },
		{ LOCTEXT("FrontBar", "Front anti-roll bar"), &FSimCruiseTuning::FrontBarPct, 10.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("RearBar", "Rear anti-roll bar"), &FSimCruiseTuning::RearBarPct, 10.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("Torque", "Engine torque"), &FSimCruiseTuning::TorquePct, 5.0, TEXT("% of stock"), 0 },
		{ LOCTEXT("FinalDrive", "Final drive"), &FSimCruiseTuning::FinalDrive, 0.05, TEXT(":1"), 2 },
		{ LOCTEXT("Steering", "Steering ratio"), &FSimCruiseTuning::SteeringRatio, 0.5, TEXT(":1"), 1 },
	};

	using namespace SimCruiseUiStyle;
	// Readable across a room on a TV: rows at 20 pt (about 27 px), light text on a near-opaque
	// dark panel (well over 4.5:1), the selected row in lime.
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 20))
			.Text(this, &SSimCruiseTuningMenu::RowText, Index)
			.ColorAndOpacity(this, &SSimCruiseTuningMenu::RowColor, Index)
		];
	}
	const FString CarName = Vehicle.IsValid() ? Vehicle->GetRealCar().Name : FString();
	FLinearColor Panel = Ink();
	Panel.A = 0.94f;
	ChildSlot
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(Panel)
		.Padding(32.f)
		[
			SNew(SBox)
			.MinDesiredWidth(900.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
					.ColorAndOpacity(Paper())
					.Text(FText::Format(LOCTEXT("Title", "Tuning · {0}"), FText::FromString(CarName)))
				]
				+ SVerticalBox::Slot().AutoHeight()[ List ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 20))
					.ColorAndOpacity(Lime())
					.Text(this, &SSimCruiseTuningMenu::RollText)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
					.ColorAndOpacity(Paper())
					.Text_Lambda([this]() { return Notice; })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 20))
					.ColorAndOpacity(Paper())
					.AutoWrapText(true)
					.Text(LOCTEXT("Help", "Up/Down choose   Left/Right change   X reset this row   Y reset all (press twice)\nA save for this car   B cancel"))
				]
			]
		]
	];
}

FText SSimCruiseTuningMenu::RowText(int32 Index) const
{
	const FRow& Row = Rows[Index];
	const double Value = Working.*(Row.Field);
	const double Real = Stock.*(Row.Field);
	if (Row.Field == &FSimCruiseTuning::SteeringRatio && Real <= 0.0)
	{
		return FText::Format(LOCTEXT("RowNone", "{0}   no ratio for this car"), Row.Label);
	}
	const FString Shown = Row.Field == &FSimCruiseTuning::RideHeightMm
		? FString::Printf(TEXT("%+.0f"), Value)
		: FString::Printf(TEXT("%.*f"), Row.Decimals, Value);
	const FString RealShown = Row.Field == &FSimCruiseTuning::RideHeightMm
		? TEXT("0")
		: FString::Printf(TEXT("%.*f"), Row.Decimals, Real);
	return FText::FromString(FString::Printf(TEXT("%s %-22s %8s %s   (real %s)"), Index == Selected ? TEXT(">") : TEXT(" "),
		*Row.Label.ToString(), *Shown, *Row.Unit, *RealShown));
}

FSlateColor SSimCruiseTuningMenu::RowColor(int32 Index) const
{
	using namespace SimCruiseUiStyle;
	return FSlateColor(Index == Selected ? Lime() : Paper());
}

FText SSimCruiseTuningMenu::RollText() const
{
	if (!Vehicle.IsValid())
	{
		return FText::GetEmpty();
	}
	const FSimCruiseCarDefinition& Real = Vehicle->GetRealCar();
	FSimCruiseTuning Clamped = Working;
	Clamped.Clamp(Stock);
	const double Roll = FSimCruiseTuning::RollGradientDegPerG(Clamped.Apply(Real, Stock));
	const double RealRoll = FSimCruiseTuning::RollGradientDegPerG(Real);
	return FText::FromString(FString::Printf(TEXT("Body roll %.2f deg per g of cornering (real car %.2f)"), Roll, RealRoll));
}

void SSimCruiseTuningMenu::Nudge(int32 Steps)
{
	if (!Rows.IsValidIndex(Selected))
	{
		return;
	}
	const FRow& Row = Rows[Selected];
	Working.*(Row.Field) += Steps * Row.Step;
	Working.Clamp(Stock);
}

bool SSimCruiseTuningMenu::Commit()
{
	ASimCruiseVehicle* Car = Vehicle.Get();
	return Car && Car->ApplyTuning(Working, true);
}

void SSimCruiseTuningMenu::Close(bool bSave)
{
	// With no car left to tune (it was removed while the menu was open) nothing was applied.
	Result = !bSave || !Vehicle.IsValid() ? ETuningResult::Cancelled
		: Commit() ? ETuningResult::Saved : ETuningResult::SaveFailed;
	OnClose.ExecuteIfBound();
}

FReply SSimCruiseTuningMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const bool bResetKey = Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Delete;
	if (!bResetKey && bConfirmReset)
	{
		bConfirmReset = false;
		Notice = FText::GetEmpty();
	}
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::W)
	{
		Selected = (Selected + Rows.Num() - 1) % Rows.Num();
		return FReply::Handled();
	}
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::S)
	{
		Selected = (Selected + 1) % Rows.Num();
		return FReply::Handled();
	}
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::A)
	{
		Nudge(-1);
		return FReply::Handled();
	}
	if (Key == EKeys::Right || Key == EKeys::D || Key == EKeys::Gamepad_DPad_Right)
	{
		Nudge(1);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::BackSpace)
	{
		if (Rows.IsValidIndex(Selected))
		{
			Working.*(Rows[Selected].Field) = Stock.*(Rows[Selected].Field);
		}
		return FReply::Handled();
	}
	if (bResetKey)
	{
		// Everything back to the real car takes a second press, so one slip of a thumb loses nothing.
		if (!bConfirmReset)
		{
			bConfirmReset = true;
			Notice = LOCTEXT("ConfirmReset", "Press Y again to put everything back as the real car");
		}
		else
		{
			bConfirmReset = false;
			Working = Stock;
			Notice = LOCTEXT("DidReset", "Back to the real car (A saves it, B keeps your old setup)");
		}
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Enter)
	{
		Close(true);
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Escape || Key == EKeys::F4)
	{
		Close(false);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
FReply SSimCruiseTuningMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	// The left stick moves through the rows and values like the D-pad, a step at a time.
	const double Now = FPlatformTime::Seconds();
	const float Value = InAnalogInputEvent.GetAnalogValue();
	if (FMath::Abs(Value) < 0.6f || Now < StickHeldUntil)
	{
		return FReply::Unhandled();
	}
	StickHeldUntil = Now + 0.22;
	const FKey Key = InAnalogInputEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftY)
	{
		Selected = (Selected + (Value > 0.f ? Rows.Num() - 1 : 1)) % Rows.Num();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_LeftX)
	{
		Nudge(Value > 0.f ? 1 : -1);
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
