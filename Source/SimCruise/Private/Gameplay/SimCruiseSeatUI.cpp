// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseSeatUI.h"

#include "Gameplay/SimCruiseAtlasUI.h"
#include "Gameplay/SimCruiseUiTheme.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SimCruiseSeat"

void SSimCruiseSeatMenu::Construct(const FArguments& InArgs)
{
	Camera = InArgs._Camera;
	CarId = InArgs._CarId;
	OnClose = InArgs._OnClose;
	if (const ASimCruisePlayerCameraManager* Cam = Camera.Get())
	{
		Original = Cam->Seat;
	}
	Working = Original;

	using namespace SimCruiseUiStyle;
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		List->AddSlot().AutoHeight().Padding(0.f, 4.f)
		[
			SNew(STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Mono", 20))
			.Text(this, &SSimCruiseSeatMenu::RowText, Index)
			.ColorAndOpacity(this, &SSimCruiseSeatMenu::RowColor, Index)
		];
	}
	// A panel down the left side: the windscreen, the hood and the dash stay in sight.
	FLinearColor Panel = Ink();
	Panel.A = 0.88f;
	ChildSlot
	.HAlign(HAlign_Left)
	.VAlign(VAlign_Bottom)
	.Padding(24.f)
	[
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(Panel)
		.Padding(24.f)
		[
			SNew(SBox)
			.MinDesiredWidth(620.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 26))
					.ColorAndOpacity(Paper())
					.Text(FText::Format(LOCTEXT("Title", "Seat & view · {0}"), FText::FromString(InArgs._CarName)))
				]
				+ SVerticalBox::Slot().AutoHeight()[ List ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
					.ColorAndOpacity(Paper())
					.AutoWrapText(true)
					.Text(SimCruiseTheme::PadOrKeys(
						TEXT("D-pad Up/Down choose   Left/Right change (the view moves as you go)\nX reset this row   Y reset all   A save for this car   B or Menu cancel"),
						TEXT("Up/Down choose   Left/Right change (the view moves as you go)\nBackspace reset this row   Delete reset all   Enter save for this car   Esc cancel")))
				]
			]
		]
	];
}

FText SSimCruiseSeatMenu::RowText(int32 Index) const
{
	const TCHAR* Mark = Index == Selected ? TEXT(">") : TEXT(" ");
	switch (Index)
	{
	case 0: return FText::FromString(FString::Printf(TEXT("%s %-16s %+5.0f cm"), Mark, TEXT("Seat height"), Working.UpCm));
	case 1: return FText::FromString(FString::Printf(TEXT("%s %-16s %+5.0f cm  %s"), Mark, TEXT("Seat fore-aft"), Working.ForwardCm,
		Working.ForwardCm > 0.0 ? TEXT("(forward)") : Working.ForwardCm < 0.0 ? TEXT("(back)") : TEXT("")));
	case 2: return FText::FromString(FString::Printf(TEXT("%s %-16s %+5.1f deg %s"), Mark, TEXT("View tilt"), Working.PitchDeg,
		Working.PitchDeg > 0.0 ? TEXT("(up)") : Working.PitchDeg < 0.0 ? TEXT("(down)") : TEXT("")));
	default:
		return Working.FovDeg <= 0.0
			? FText::FromString(FString::Printf(TEXT("%s %-16s  Auto (the lens: F6 or the atlas)"), Mark, TEXT("View angle")))
			: FText::FromString(FString::Printf(TEXT("%s %-16s %5.0f deg tall"), Mark, TEXT("View angle"), Working.FovDeg));
	}
}

FSlateColor SSimCruiseSeatMenu::RowColor(int32 Index) const
{
	using namespace SimCruiseUiStyle;
	return FSlateColor(Index == Selected ? Lime() : Paper());
}

void SSimCruiseSeatMenu::Show()
{
	Working.Clamp();
	if (ASimCruisePlayerCameraManager* Cam = Camera.Get())
	{
		Cam->Seat = Working;
	}
}

void SSimCruiseSeatMenu::Nudge(int32 Steps)
{
	switch (Selected)
	{
	case 0: Working.UpCm += Steps * 1.0; break;
	case 1: Working.ForwardCm += Steps * 1.0; break;
	case 2: Working.PitchDeg += Steps * 0.5; break;
	default:
		// Auto steps to 45 deg (the natural lens) and on from there; below 30 goes back to Auto.
		if (Working.FovDeg <= 0.0)
		{
			Working.FovDeg = 45.0 + Steps * 1.0;
		}
		else
		{
			Working.FovDeg += Steps * 1.0;
			if (Working.FovDeg < FSimCruiseSeat::MinFovDeg) Working.FovDeg = 0.0;
		}
		break;
	}
	Show();
}

void SSimCruiseSeatMenu::Close(bool bSave)
{
	bSaved = bSave;
	if (bSave)
	{
		Working.Clamp();
		Working.Save(CarId);
	}
	else
	{
		Working = Original;
	}
	Show();
	OnClose.ExecuteIfBound();
}

FReply SSimCruiseSeatMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::W) { Selected = (Selected + 3) % 4; return FReply::Handled(); }
	if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::S) { Selected = (Selected + 1) % 4; return FReply::Handled(); }
	if (Key == EKeys::Left || Key == EKeys::Gamepad_DPad_Left || Key == EKeys::A) { Nudge(-1); return FReply::Handled(); }
	if (Key == EKeys::Right || Key == EKeys::Gamepad_DPad_Right || Key == EKeys::D) { Nudge(1); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::BackSpace)
	{
		const FSimCruiseSeat Default;
		switch (Selected)
		{
		case 0: Working.UpCm = Default.UpCm; break;
		case 1: Working.ForwardCm = Default.ForwardCm; break;
		case 2: Working.PitchDeg = Default.PitchDeg; break;
		default: Working.FovDeg = Default.FovDeg; break;
		}
		Show();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Delete) { Working = FSimCruiseSeat(); Show(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Enter) { Close(true); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right || Key == EKeys::Escape) { Close(false); return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SSimCruiseSeatMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const double Now = FPlatformTime::Seconds();
	const float Value = InAnalogInputEvent.GetAnalogValue();
	if (FMath::Abs(Value) < 0.6f || Now < StickHeldUntil)
	{
		return FReply::Unhandled();
	}
	StickHeldUntil = Now + 0.18;
	const FKey Key = InAnalogInputEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftY) { Selected = (Selected + (Value > 0.f ? 3 : 1)) % 4; return FReply::Handled(); }
	if (Key == EKeys::Gamepad_LeftX) { Nudge(Value > 0.f ? 1 : -1); return FReply::Handled(); }
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
