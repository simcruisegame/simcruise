// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePauseMenu.h"

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

using namespace SimCruiseTheme;

namespace
{
	using FRowInfo = SSimCruisePauseMenu::FRowInfo;
	using EKind = SSimCruisePauseMenu::EKind;

	FRowInfo Row(ESimCruiseMenuRow R, EKind Kind, EIcon Icon, const TCHAR* Title, const TCHAR* Note, const TCHAR* Help)
	{
		return FRowInfo{ R, Kind, Icon, Title, Note, Help };
	}

	TSharedRef<SWidget> Card(float Width, const TSharedRef<SWidget>& Content)
	{
		return SNew(SBox).WidthOverride(Width)
			[
				SNew(SBorder).BorderImage(RoundedBrush(Glass(), 16.f)).Padding(FMargin(36.f, 30.f))
				[
					Content
				]
			];
	}

	/** Fit points (world metres) into a box, north up. */
	struct FFit
	{
		FVector2D Min = FVector2D(TNumericLimits<double>::Max()), Max = FVector2D(-TNumericLimits<double>::Max());
		void Add(const FVector2D& P) { Min = FVector2D(FMath::Min(Min.X, P.X), FMath::Min(Min.Y, P.Y)); Max = FVector2D(FMath::Max(Max.X, P.X), FMath::Max(Max.Y, P.Y)); }
		FVector2D Map(const FVector2D& P, const FVector2D& Size, double Pad) const
		{
			const FVector2D Span = (Max - Min).ComponentMax(FVector2D(50.0, 50.0));
			const double K = FMath::Min((Size.X - 2 * Pad) / Span.X, (Size.Y - 2 * Pad) / Span.Y);
			const FVector2D Mid = (Min + Max) * 0.5;
			return Size * 0.5 + (P - Mid) * K;
		}
	};
}

TSharedRef<SWidget> SimCruiseMenuBackdrop(TAttribute<float> LeftFraction, TAttribute<bool> bBlur)
{
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.BlurStrength_Lambda([bBlur]() { return bBlur.Get() ? 14.f : 0.f; })
			.Visibility_Lambda([bBlur]() { return bBlur.Get() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		]
		+ SOverlay::Slot()
		[
			SNew(SSimCruisePainted).Size(FVector2D(1920.0, 1080.0)).Visibility(EVisibility::HitTestInvisible)
			.OnPaintFn_Lambda([LeftFraction](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool)
			{
				// Dark on the left where the rows are, lighter to the right (left to right).
				const FVector2f Size = FVector2f(G.GetLocalSize());
				const float Left = FMath::Clamp(LeftFraction.Get(), 0.05f, 1.f);
				const float W = Size.X * Left;
				TArray<FSlateGradientStop> Stops;
				Stops.Add(FSlateGradientStop(FVector2D(0.0, 0.0), Glass(0.92f)));
				Stops.Add(FSlateGradientStop(FVector2D(W * 0.55, 0.0), Glass(Left < 0.9f ? 0.72f : 0.80f)));
				Stops.Add(FSlateGradientStop(FVector2D(W, 0.0), Glass(Left < 0.9f ? 0.0f : 0.55f)));
				FSlateDrawElement::MakeGradient(Out, Layer, G.ToPaintGeometry(FVector2f(W, Size.Y), FSlateLayoutTransform()), Stops, Orient_Vertical);
			})
		];
}

// ====================================================================== pause menu

const TArray<FRowInfo>& SSimCruisePauseMenu::Rows(int32 InPage)
{
	static const TArray<FRowInfo> Drive = {
		Row(ESimCruiseMenuRow::Continue, EKind::Action, EIcon::Drive, TEXT("Continue driving"), TEXT("Back to the road"), TEXT("Close the menu and drive on.")),
		Row(ESimCruiseMenuRow::WhereTo, EKind::Action, EIcon::Search, TEXT("Where to?"), TEXT("Find a place, then go there or make it your stop"), TEXT("Type a place or an address. Go there now, or let the GPS take you there.")),
		Row(ESimCruiseMenuRow::PlanRoute, EKind::Action, EIcon::Route, TEXT("Plan a route"), TEXT("Put stops on the map"), TEXT("Move the cross over the map and add stops. The GPS finds the roads between them.")),
		Row(ESimCruiseMenuRow::FamousRoads, EKind::Action, EIcon::Star, TEXT("Famous roads"), TEXT("Great drives with the route ready"), TEXT("Go to the start of a famous road. The route to its end starts when you arrive.")),
		Row(ESimCruiseMenuRow::Traffic, EKind::Value, EIcon::Traffic, TEXT("Traffic"), TEXT("Other cars on the road"), TEXT("How many other cars drive near you.")),
		Row(ESimCruiseMenuRow::Atlas, EKind::Action, EIcon::Map, TEXT("Road atlas"), TEXT("Jump to any place on the map"), TEXT("The full map. Pin a place and drive there now.")),
	};
	static const TArray<FRowInfo> Car = {
		Row(ESimCruiseMenuRow::Car, EKind::Value, EIcon::Car, TEXT("Car"), TEXT("Change the car you drive"), TEXT("Change to the next car. You keep your place on the road.")),
		Row(ESimCruiseMenuRow::Transmission, EKind::Value, EIcon::Gearbox, TEXT("Gearbox"), TEXT("Automatic or manual"), TEXT("With the manual gearbox you use the clutch and shift the gears yourself.")),
		Row(ESimCruiseMenuRow::Tune, EKind::Action, EIcon::Wrench, TEXT("Tune car"), TEXT("Springs, dampers, brakes and steering"), TEXT("Change the setup of this car in workshop units. Stop the car first.")),
		Row(ESimCruiseMenuRow::Lights, EKind::Value, EIcon::Lights, TEXT("Headlights"), TEXT("On, or on when it gets dark"), TEXT("Auto turns the headlights on at dusk.")),
	};
	static const TArray<FRowInfo> Seat = {
		Row(ESimCruiseMenuRow::Lens, EKind::Segmented, EIcon::Lens, TEXT("Lens"), TEXT(""), TEXT("Natural looks like a camera. True FOV shows the road at its real size for your screen. Wide shows more round you.")),
		Row(ESimCruiseMenuRow::HeadMotion, EKind::Slider, EIcon::Eye, TEXT("Head motion"), TEXT(""), TEXT("How much your head moves with the car in turns and bumps.")),
		Row(ESimCruiseMenuRow::ScreenSize, EKind::Slider, EIcon::Screen, TEXT("Screen size"), TEXT(""), TEXT("The diagonal of your screen. True FOV uses it.")),
		Row(ESimCruiseMenuRow::EyeDistance, EKind::Slider, EIcon::Eye, TEXT("Eye to screen"), TEXT(""), TEXT("How far your eyes are from the screen. True FOV uses it.")),
		Row(ESimCruiseMenuRow::Seat, EKind::Action, EIcon::Seat, TEXT("Seat position"), TEXT("Forward, back and height"), TEXT("Move the seat and your eyes in the car.")),
	};
	static const TArray<FRowInfo> Settings = {
		Row(ESimCruiseMenuRow::Sound, EKind::Value, EIcon::Sound, TEXT("Sound"), TEXT("The engine and the road"), TEXT("Turn the sound on or off.")),
		Row(ESimCruiseMenuRow::HudLevel, EKind::Value, EIcon::Hud, TEXT("HUD"), TEXT("Off, Glance or Full"), TEXT("Glance shows the speed and fades while you cruise. Full shows all the readouts.")),
		Row(ESimCruiseMenuRow::SpeedUnits, EKind::Segmented, EIcon::Hud, TEXT("Speed units"), TEXT(""), TEXT("The units of the speed on the HUD and of the GPS distances: mph and miles, or km/h and kilometres.")),
		Row(ESimCruiseMenuRow::Rumble, EKind::Slider, EIcon::Rumble, TEXT("Rumble"), TEXT(""), TEXT("How strong the pad shakes for the tyres and the road.")),
		Row(ESimCruiseMenuRow::StickSpeed, EKind::Slider, EIcon::Stick, TEXT("Stick steering speed"), TEXT(""), TEXT("How fast the wheels turn when you move the stick.")),
		Row(ESimCruiseMenuRow::StickResponse, EKind::Value, EIcon::Stick, TEXT("Stick response"), TEXT("Linear, between or fine centre"), TEXT("Fine centre gives small steering near the middle of the stick.")),
		Row(ESimCruiseMenuRow::StickDeadZone, EKind::Slider, EIcon::Stick, TEXT("Stick dead zone"), TEXT(""), TEXT("How far the stick moves before the wheels turn.")),
		Row(ESimCruiseMenuRow::Trigger, EKind::Value, EIcon::Trigger, TEXT("Throttle trigger"), TEXT("Linear to very progressive"), TEXT("Progressive gives fine throttle at the start of the trigger.")),
	};
	switch (InPage)
	{
	case 1: return Car;
	case 2: return Seat;
	case 3: return Settings;
	default: return Drive;
	}
}

FString SSimCruisePauseMenu::PageName(int32 InPage)
{
	static const TCHAR* Names[] = { TEXT("Drive"), TEXT("Your car"), TEXT("Seat & view"), TEXT("Settings") };
	return Names[FMath::Clamp(InPage, 0, NumPages - 1)];
}

FSimCruiseMenuValue SSimCruisePauseMenu::ValueOf(ESimCruiseMenuRow InRow) const
{
	const FSimCruiseMenuValue* Found = State.Values.Find(InRow);
	return Found ? *Found : FSimCruiseMenuValue();
}

void SSimCruisePauseMenu::Construct(const FArguments& InArgs)
{
	GetState = InArgs._GetState;
	OnAction = InArgs._OnAction;
	OnPage = InArgs._OnPage;
	OnClose = InArgs._OnClose;
	Page = FMath::Clamp(InArgs._Page, 0, NumPages - 1);
	Selected[Page] = FMath::Clamp(InArgs._Row, 0, Rows(Page).Num() - 1);
	TabCursor = Page;
	if (GetState.IsBound()) State = GetState.Execute();

	// The index is set, not bound: after LB or RB the new page is on screen at once, so the
	// pad's focus can move onto its first row in the same frame.
	TSharedRef<SWidgetSwitcher> Switcher = SNew(SWidgetSwitcher);
	for (int32 P = 0; P < NumPages; ++P) Switcher->AddSlot()[BuildPage(P)];
	Switcher->SetActiveWidgetIndex(Page);
	Pages = Switcher;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			// The world keeps running behind Seat & view: no blur there, and only the left part dark.
			SimCruiseMenuBackdrop(TAttribute<float>::CreateLambda([this]() { return Page == 2 ? 0.45f : 1.f; }), TAttribute<bool>::CreateLambda([this]() { return Page != 2; }))
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(96.f, 56.f, 0.f, 0.f)
		[
			BuildTabs()
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 52.f, 96.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(State.Place); }).Font(Font(EWeight::Bold, 26.f)).ColorAndOpacity(White())
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(State.Where); }).Font(Font(EWeight::Regular, 22.f)).ColorAndOpacity(Mist())
			]
		]
		+ SOverlay::Slot()
		[
			Switcher
		]
	];
	if (OnPage.IsBound()) OnPage.Execute(Page);
}

TSharedRef<SWidget> SSimCruisePauseMenu::BuildTabs()
{
	const TAttribute<bool> bPad = TAttribute<bool>::CreateLambda([this]() { return State.bPad; });
	TSharedRef<SHorizontalBox> Tabs = SNew(SHorizontalBox);
	auto Shoulder = [bPad](const FString& Pad, const FString& Key)
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([bPad]() { return bPad.Get() ? EVisibility::Visible : EVisibility::Collapsed; })[Glyph(Pad, 36.f)]]
			+ SOverlay::Slot()[SNew(SBox).Visibility_Lambda([bPad]() { return bPad.Get() ? EVisibility::Collapsed : EVisibility::Visible; })[Glyph(Key, 36.f)]];
	};
	Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)[Shoulder(TEXT("LB"), TEXT("Q"))];
	for (int32 P = 0; P < NumPages; ++P)
	{
		TSharedRef<SSimCruiseTab> Tab = SNew(SSimCruiseTab)
			.Tag(FName(*FString::Printf(TEXT("Tab.%s"), *PageName(P))))
			.Label(FText::FromString(PageName(P)))
			.Current_Lambda([this, P]() { return Page == P; })
			.Focused_Lambda([this, P]() { return bTabs && TabCursor == P; })
			// The tab got the focus (the pad, or a real mouse moving onto it): the pad is on the tab row.
			.OnHover_Lambda([this, P]() { if (bSyncing) return; bTabs = true; TabCursor = P; SyncFocus(); })
			.OnClick_Lambda([this, P]() { SetPage(P); });
		TabWidgets[P] = Tab;
		Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center)[Tab];
	}
	Tabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)[Shoulder(TEXT("RB"), TEXT("E"))];
	return Tabs;
}

TSharedRef<SWidget> SSimCruisePauseMenu::BuildPage(int32 P)
{
	const TArray<FRowInfo>& List = Rows(P);
	const bool bDrive = P == 0;
	const float RowWidth = bDrive ? 720.f : 1060.f;
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	for (int32 i = 0; i < List.Num(); ++i)
	{
		const FRowInfo Info = List[i];
		const TAttribute<bool> Focused = TAttribute<bool>::CreateLambda([this, P, i]() { return !bTabs && Page == P && Selected[P] == i; });
		// The row got the focus (the pad, Slate, or a real mouse moving onto it).
		const FSimpleDelegate Hover = FSimpleDelegate::CreateLambda([this, P, i]() { if (bSyncing || Page != P) return; bTabs = false; Selected[P] = i; SyncFocus(); });
		const FSimpleDelegate Click = FSimpleDelegate::CreateLambda([this, P, i]() { if (Page == P) { bTabs = false; Selected[P] = i; SyncFocus(); Choose(0); } });
		const ESimCruiseMenuRow R = Info.Row;
		TSharedPtr<SWidget> Widget;
		if (Info.Kind == EKind::Slider)
		{
			Widget = SNew(SSimCruiseSliderRow).Width(RowWidth).Focused(Focused).OnHover(Hover).OnClick(Click)
				.Name(FText::FromString(Info.Title))
				.Fraction_Lambda([this, R]() { return ValueOf(R).Fraction; })
				.Stock_Lambda([this, R]() { return ValueOf(R).Stock; })
				.Value_Lambda([this, R]() { return FText::FromString(ValueOf(R).Text); })
				.Unit_Lambda([this, R]() { return FText::FromString(ValueOf(R).Unit); });
		}
		else if (Info.Kind == EKind::Segmented)
		{
			Widget = SNew(SSimCruiseSegmentedRow).Width(RowWidth).Focused(Focused).OnHover(Hover).OnClick(Click)
				.Name(FText::FromString(Info.Title))
				.Options(R == ESimCruiseMenuRow::SpeedUnits ? TArray<FString>{ TEXT("mph"), TEXT("km/h") } : TArray<FString>{ TEXT("Natural"), TEXT("True FOV"), TEXT("Wide") })
				.Chosen_Lambda([this, R]() { return ValueOf(R).Chosen; });
		}
		else
		{
			const FString Note = Info.Note;
			Widget = SNew(SSimCruiseGuideRow).Width(RowWidth).Icon(Info.Icon).Focused(Focused).OnHover(Hover).OnClick(Click)
				.Title(FText::FromString(Info.Title))
				.Note_Lambda([this, R, Note]() { return FText::FromString(ValueOf(R).bUnavailable ? FString(TEXT("Comes with the next driving update")) : Note); })
				.Value_Lambda([this, R]()
				{
					const FSimCruiseMenuValue V = ValueOf(R);
					return FText::FromString(V.Unit.IsEmpty() ? V.Text : V.Text + TEXT(" ") + V.Unit);
				});
		}
		Widget->SetTag(FName(*FString::Printf(TEXT("Row.%s"), *Info.Title)));
		RowWidgets[P].Add(Widget);
		// Eight rows (Settings) sit closer so the list stays above the hints.
		Column->AddSlot().AutoHeight().Padding(0, 0, 0, List.Num() > 7 ? 4 : 14)[Widget.ToSharedRef()];
	}

	TSharedRef<SOverlay> Layout = SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(70.f, List.Num() > 7 ? 150.f : 190.f, 0.f, 0.f)
		[
			Column
		]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.f, 190.f, 96.f, 0.f)
		[
			bDrive ? BuildRouteCard() : BuildHelpCard()
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 52.f)
		[
			BuildHints()
		];
	if (P == 2)
	{
		// What the two main lenses do, at the bottom left.
		Layout->AddSlot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 128.f)
		[
			Card(720.f, SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Natural or True FOV?"), EWeight::ExtraBold, 26.f, White())]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[
					SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::Regular, 21.f)).ColorAndOpacity(Mist())
					.Text(FText::FromString(TEXT("Natural looks like a photo of the road. True FOV shows the road at its real size for your screen and how far you sit from it, so speed feels real. Set your screen size and eye distance first.")))
				])
		];
	}
	return Layout;
}

TSharedRef<SWidget> SSimCruisePauseMenu::BuildRouteCard()
{
	return Card(760.f, SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Text(TEXT("Your route"), EWeight::SemiBold, 23.f, Mist())]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14, 0, 0, 0)
			[
				SNew(SBorder).BorderImage(RoundedBrush(Amber(), 14.f)).Padding(FMargin(12.f, 2.f))
				.Visibility_Lambda([this]() { return State.bRoute ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(STextBlock).Font(Font(EWeight::Black, 19.f)).ColorAndOpacity(GuideDeep())
					.Text_Lambda([this]() { const int32 N = State.RouteStops.Num(); return FText::FromString(FString::Printf(TEXT("%d %s"), N, N == 1 ? TEXT("stop") : TEXT("stops"))); })
				]
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::ExtraBold, 44.f)).ColorAndOpacity(White())
			.Text_Lambda([this]() { return FText::FromString(State.bRoute ? State.RouteTitle : FString(TEXT("No route yet"))); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 18)
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::Regular, 23.f)).ColorAndOpacity(White())
			.Text_Lambda([this]() { return FText::FromString(State.bRoute ? State.RouteSummary : FString(TEXT("Plan a route or choose a famous road. The magenta course then shows on the road, on the turn sign and on the map."))); })
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox).HeightOverride(330.f).Visibility_Lambda([this]() { return State.bRoute ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBorder).BorderImage(RoundedBrush(FLinearColor(FColor(0x1B, 0x24, 0x22)), 12.f)).Padding(0.f)
				[
					SNew(SSimCruisePainted).Size(FVector2D(688.0, 330.0))
					.OnPaintFn_Lambda([this](const FGeometry& G, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool)
					{
						FFit Fit;
						for (const TArray<FVector2D>& Leg : State.RouteLegs) for (const FVector2D& P : Leg) Fit.Add(P);
						Fit.Add(State.CarM);
						const FVector2D Size = FVector2D(G.GetLocalSize());
						for (const TArray<FVector2D>& Leg : State.RouteLegs)
						{
							TArray<FVector2f> Pts;
							for (const FVector2D& P : Leg) Pts.Add(FVector2f(Fit.Map(P, Size, 30.0)));
							if (Pts.Num() >= 2) FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, Course(), true, 6.f);
						}
						const FVector2D Car = Fit.Map(State.CarM, Size, 30.0);
						FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(18.f, 18.f), FSlateLayoutTransform(FVector2f(Car - FVector2D(9.0, 9.0)))), RoundedBrush(SimCruiseTheme::White(), 9.f), ESlateDrawEffect::None, White());
						for (int32 i = 0; i < State.RouteStops.Num(); ++i)
						{
							const FVector2D At = Fit.Map(State.RouteStops[i], Size, 30.0);
							FSlateDrawElement::MakeBox(Out, Layer + 1, G.ToPaintGeometry(FVector2f(28.f, 28.f), FSlateLayoutTransform(FVector2f(At - FVector2D(14.0, 14.0)))), RoundedBrush(SimCruiseTheme::White(), 14.f), ESlateDrawEffect::None, Amber());
							const FSlateFontInfo Number = Font(EWeight::Black, 17.f);
							FSlateDrawElement::MakeText(Out, Layer + 2, G.ToPaintGeometry(FVector2f(20.f, 22.f), FSlateLayoutTransform(FVector2f(At - FVector2D(i >= 9 ? 9.0 : 5.0, 11.0)))),
								FString::FromInt(i + 1), Number, ESlateDrawEffect::None, GuideDeep());
						}
					})
				]
			]
		]);
}

TSharedRef<SWidget> SSimCruisePauseMenu::BuildHelpCard()
{
	auto Info = [this]() -> const FRowInfo& { return Rows(Page)[FMath::Clamp(Selected[Page], 0, Rows(Page).Num() - 1)]; };
	return Card(600.f, SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::ExtraBold, 44.f)).ColorAndOpacity(White())
			.Text_Lambda([Info]() { return FText::FromString(Info().Title); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::Regular, 23.f)).ColorAndOpacity(White())
			.Text_Lambda([Info]() { return FText::FromString(Info().Help); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 18, 0, 0)
		[
			SNew(STextBlock).Font(Font(EWeight::Bold, 26.f)).ColorAndOpacity(Amber())
			.Text_Lambda([this, Info]()
			{
				const FSimCruiseMenuValue V = ValueOf(Info().Row);
				if (V.bUnavailable) return FText::FromString(TEXT("Comes with the next driving update"));
				return FText::FromString(V.Unit.IsEmpty() ? V.Text : V.Text + TEXT(" ") + V.Unit);
			})
		]);
}

TSharedRef<SWidget> SSimCruisePauseMenu::BuildHints()
{
	const TAttribute<bool> bPad = TAttribute<bool>::CreateLambda([this]() { return State.bPad; });
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	// Rows that change with left and right show it; X ends a route on the Drive page.
	Box->AddSlot().AutoHeight()
	[
		SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]()
		{
			const EKind Kind = Rows(Page)[FMath::Clamp(Selected[Page], 0, Rows(Page).Num() - 1)].Kind;
			if (bTabs) return 3;
			if (Page == 0) return State.bRoute ? 1 : 0;
			return Kind == EKind::Action ? 0 : 2;
		})
		+ SWidgetSwitcher::Slot()[Hints({ { TEXT("A"), TEXT("Choose"), TEXT("Enter") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } }, bPad)]
		+ SWidgetSwitcher::Slot()[Hints({ { TEXT("A"), TEXT("Choose"), TEXT("Enter") }, { TEXT("X"), TEXT("End the route"), TEXT("X") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } }, bPad)]
		+ SWidgetSwitcher::Slot()[Hints({ { TEXT("D-pad"), TEXT("Change"), TEXT("Left Right") }, { TEXT("Y"), TEXT("Back to stock"), TEXT("Y") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } }, bPad)]
		// On the tab row.
		+ SWidgetSwitcher::Slot()[Hints({ { TEXT("D-pad"), TEXT("Choose a page"), TEXT("Left Right") }, { TEXT("A"), TEXT("Open"), TEXT("Enter") }, { TEXT("B"), TEXT("Back to the drive"), TEXT("Esc") }, { TEXT("LB RB"), TEXT("Change page"), TEXT("Q E") } }, bPad)]
	];
	return Box;
}

void SSimCruisePauseMenu::SetPage(int32 InPage)
{
	const int32 Next = (InPage % NumPages + NumPages) % NumPages;
	const bool bChanged = Next != Page;
	Page = Next;
	// Every page opens on its first row, with the pad on it.
	Selected[Page] = 0;
	bTabs = false;
	TabCursor = Page;
	if (Pages.IsValid()) Pages->SetActiveWidgetIndex(Page);
	if (bChanged) OnPage.ExecuteIfBound(Page);
	SyncFocus();
}

TSharedPtr<SWidget> SSimCruisePauseMenu::GetFocusTarget() const
{
	if (bTabs) return TabWidgets[FMath::Clamp(TabCursor, 0, NumPages - 1)];
	return RowWidgets[Page].IsValidIndex(Selected[Page]) ? RowWidgets[Page][Selected[Page]] : TSharedPtr<SWidget>();
}

void SSimCruisePauseMenu::SyncFocus()
{
	if (bSyncing) return;
	TGuardValue<bool> Guard(bSyncing, true);
	bFocusDirty = !FocusPad(GetFocusTarget());
}

FReply SSimCruisePauseMenu::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	// The menu itself was given the focus (it opened): hand it on to the row or tab the pad is on.
	bFocusDirty = true;
	const TSharedPtr<SWidget> Target = GetFocusTarget();
	return Target.IsValid() ? FReply::Handled().SetUserFocus(Target.ToSharedRef(), EFocusCause::SetDirectly, false) : FReply::Handled();
}

FReply SSimCruisePauseMenu::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// A real mouse move shows the keyboard's keys in the hints.
	if (IsRealMouseMove(MouseEvent)) State.bPad = false;
	return FReply::Unhandled();
}

FNavigationReply SSimCruisePauseMenu::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	// The menu moves the focus itself; Slate's own navigation never takes it off the menu.
	return FNavigationReply::Stop();
}

void SSimCruisePauseMenu::Choose(int32 Dir)
{
	const TArray<FRowInfo>& List = Rows(Page);
	const FRowInfo& Info = List[FMath::Clamp(Selected[Page], 0, List.Num() - 1)];
	if (Dir != 0 && Info.Kind == EKind::Action) return;
	OnAction.ExecuteIfBound(Info.Row, Dir);
	if (GetState.IsBound()) State = GetState.Execute();
}

void SSimCruisePauseMenu::Navigate(ENav Nav)
{
	const int32 N = Rows(Page).Num();
	if (bTabs)
	{
		// On the tab row: left and right move along it, A or Down opens the tab (on its first row).
		switch (Nav)
		{
		case ENav::Left: TabCursor = (TabCursor + NumPages - 1) % NumPages; SyncFocus(); break;
		case ENav::Right: TabCursor = (TabCursor + 1) % NumPages; SyncFocus(); break;
		case ENav::Accept: case ENav::Down: SetPage(TabCursor); break;
		case ENav::PrevPage: SetPage(Page - 1); break;
		case ENav::NextPage: SetPage(Page + 1); break;
		case ENav::Back: case ENav::Menu: OnClose.ExecuteIfBound(); break;
		default: break;
		}
		return;
	}
	switch (Nav)
	{
	case ENav::Up:
		// Up from the first row goes onto the tab row, on the open page's tab.
		if (Selected[Page] == 0) { bTabs = true; TabCursor = Page; }
		else --Selected[Page];
		SyncFocus();
		break;
	case ENav::Down: Selected[Page] = (Selected[Page] + 1) % N; SyncFocus(); break;
	case ENav::Left: Choose(-1); break;
	case ENav::Right: Choose(+1); break;
	case ENav::Accept: Choose(0); break;
	case ENav::PrevPage: SetPage(Page - 1); break;
	case ENav::NextPage: SetPage(Page + 1); break;
	case ENav::X:
		if (Page == 0 && State.bRoute) OnAction.ExecuteIfBound(ESimCruiseMenuRow::EndRoute, 0);
		break;
	case ENav::Y:
		// Back to stock on a slider row.
		if (Rows(Page)[Selected[Page]].Kind == EKind::Slider) Choose(2);
		break;
	case ENav::Back:
	case ENav::Menu:
		OnClose.ExecuteIfBound();
		break;
	default: break;
	}
}

FReply SSimCruisePauseMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const ENav Nav = NavOf(InKeyEvent.GetKey());
	if (Nav == ENav::None) return FReply::Unhandled();
	State.bPad = InKeyEvent.GetKey().IsGamepadKey();
	Navigate(Nav);
	return FReply::Handled();
}

FReply SSimCruisePauseMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (!IsLeftStick(Key)) return FReply::Unhandled();
	const ENav Nav = StickNav(Key, InAnalogInputEvent.GetAnalogValue(), Stick, FPlatformTime::Seconds());
	if (Nav != ENav::None)
	{
		State.bPad = true;
		Navigate(Nav);
	}
	// All of the stick is the menu's: Slate's own stick navigation must not move the focus as well.
	return FReply::Handled();
}

void SSimCruisePauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const bool bPad = State.bPad;
	if (GetState.IsBound()) State = GetState.Execute();
	// The last device used wins over the controller's guess.
	State.bPad = bPad;
	// The pad's focus is always on the menu: put it back if it went (the menu just opened,
	// a click outside a row, the window came back).
	if (bFocusDirty || !HasFocusedDescendants()) SyncFocus();
}

// ====================================================================== list menu

void SSimCruiseListMenu::Construct(const FArguments& InArgs)
{
	Items = InArgs._Items;
	OnChoose = InArgs._OnChoose;
	OnBack = InArgs._OnBack;
	bPad = InArgs._bPad;
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FItem& Item = Items[i];
		TSharedRef<SSimCruiseGuideRow> Row = SNew(SSimCruiseGuideRow).Width(980.f).Icon(Item.Icon)
			.Tag(FName(*FString::Printf(TEXT("Item.%s"), *Item.Title)))
			.Fill(Item.bCourse ? Course() : Guide())
			.Title(FText::FromString(Item.Title)).Note(FText::FromString(Item.Note)).Value(FText::FromString(Item.Value))
			.Focused_Lambda([this, i]() { return Selected == i; })
			.OnHover_Lambda([this, i]() { if (bSyncing) return; Selected = i; SyncFocus(); })
			.OnClick_Lambda([this, i]() { Selected = i; OnChoose.ExecuteIfBound(i); });
		RowWidgets.Add(Row);
		Column->AddSlot().AutoHeight().Padding(0, 0, 0, 14)[Row];
	}
	const TAttribute<bool> Pad = TAttribute<bool>::CreateLambda([this]() { return bPad; });
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[SimCruiseMenuBackdrop(1.f, true)]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(96.f, 56.f, 0.f, 0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Text(InArgs._Title, EWeight::ExtraBold, 44.f, White())]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[Text(InArgs._Help, EWeight::Regular, 23.f, Mist())]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(70.f, 190.f, 0.f, 0.f)
		[
			Column
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 52.f)
		[
			Hints({ { TEXT("A"), TEXT("Choose"), TEXT("Enter") }, { TEXT("B"), TEXT("Back"), TEXT("Esc") } }, Pad)
		]
	];
}

void SSimCruiseListMenu::Navigate(ENav Nav)
{
	const int32 N = FMath::Max(Items.Num(), 1);
	switch (Nav)
	{
	case ENav::Up: Selected = (Selected + N - 1) % N; SyncFocus(); break;
	case ENav::Down: Selected = (Selected + 1) % N; SyncFocus(); break;
	case ENav::Accept: OnChoose.ExecuteIfBound(Selected); break;
	case ENav::Back: case ENav::Menu: OnBack.ExecuteIfBound(); break;
	default: break;
	}
}

FReply SSimCruiseListMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const ENav Nav = NavOf(InKeyEvent.GetKey());
	if (Nav == ENav::None) return FReply::Unhandled();
	bPad = InKeyEvent.GetKey().IsGamepadKey();
	Navigate(Nav);
	return FReply::Handled();
}

FReply SSimCruiseListMenu::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (!IsLeftStick(Key)) return FReply::Unhandled();
	const ENav Nav = StickNav(Key, InAnalogInputEvent.GetAnalogValue(), Stick, FPlatformTime::Seconds());
	if (Nav == ENav::Up || Nav == ENav::Down)
	{
		bPad = true;
		Navigate(Nav);
	}
	return FReply::Handled();
}

void SSimCruiseListMenu::SyncFocus()
{
	if (bSyncing) return;
	TGuardValue<bool> Guard(bSyncing, true);
	bFocusDirty = !FocusPad(GetRowWidget(Selected));
}

FReply SSimCruiseListMenu::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	bFocusDirty = true;
	const TSharedPtr<SWidget> Target = GetRowWidget(Selected);
	return Target.IsValid() ? FReply::Handled().SetUserFocus(Target.ToSharedRef(), EFocusCause::SetDirectly, false) : FReply::Handled();
}

FNavigationReply SSimCruiseListMenu::OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent)
{
	return FNavigationReply::Stop();
}

void SSimCruiseListMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bFocusDirty || !HasFocusedDescendants()) SyncFocus();
}
