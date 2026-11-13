// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseRoutePlanner.h"

#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruiseRouteSubsystem.h"
#include "Gameplay/SimCruiseUiTheme.h"
#include "Rendering/DrawElements.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

using namespace SimCruiseTheme;

namespace
{
	const double Scales[] = { 1.3, 0.74, 0.37, 0.185, 0.0925, 0.046, 0.023, 0.0115 };
	constexpr int32 NumScales = UE_ARRAY_COUNT(Scales);
}

void SSimCruiseRoutePlanner::Construct(const FArguments& InArgs)
{
	GetView = InArgs._GetView;
	Gps = InArgs._Gps;
	OnStart = InArgs._OnStart;
	OnBack = InArgs._OnBack;
	bPad = InArgs._bPad;
	if (GetView.IsBound()) View = GetView.Execute();
	CenterM = FVector2D(View.CarCm.X, View.CarCm.Y) / 100.0;
	const TAttribute<bool> Pad = TAttribute<bool>::CreateLambda([this]() { return bPad; });

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(96.f, 56.f, 0.f, 0.f)
		[
			SNew(SBox).WidthOverride(640.f)
			[
				SNew(SBorder).BorderImage(RoundedBrush(Glass(), 16.f)).Padding(FMargin(32.f, 28.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Text(TEXT("Plan a route"), EWeight::ExtraBold, 44.f, White())]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 20)
					[
						SNew(STextBlock).AutoWrapText(true).Font(Font(EWeight::Regular, 23.f)).ColorAndOpacity(Mist())
						.Text(FText::FromString(TEXT("Move the cross to a place and add it as a stop. The route follows the roads.")))
					]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(List, SVerticalBox)]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
					[
						SNew(SSimCruiseGuideRow).Width(560.f).Icon(EIcon::Route).Fill(Course()).Glyph(TEXT("Y"))
						.Title(FText::FromString(TEXT("Start the route")))
						.Note_Lambda([this]()
						{
							const USimCruiseRouteSubsystem* G = GetGps();
							if (!G || G->DraftStops.Num() == 0) return FText::FromString(TEXT("Add a stop first"));
							const FSimCruiseRoute& P = G->GetPreview();
							if (G->IsPreviewBusy() || P.GetLegs().Num() == 0) return FText::FromString(TEXT("Finding the roads"));
							return FText::FromString(FString::Printf(TEXT("%s, %s"), *SimCruiseRouteText::Distance(P.TotalLengthM()), *SimCruiseRouteText::Duration(P.TotalDurationS())));
						})
						.Focused(false)
						.OnClick_Lambda([this]() { OnStart.ExecuteIfBound(); })
					]
				]
			]
		]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(96.f, 0.f, 0.f, 52.f)
		[
			SNew(SBorder).BorderImage(RoundedBrush(Glass(), 14.f)).Padding(FMargin(24.f, 12.f))
			[
				Hints({ { TEXT("A"), TEXT("Add a stop here"), TEXT("Enter") }, { TEXT("X"), TEXT("Remove stop"), TEXT("Del") }, { TEXT("Y"), TEXT("Start the route"), TEXT("Y") },
					{ TEXT("LT RT"), TEXT("Zoom"), TEXT("- +") }, { TEXT("B"), TEXT("Back"), TEXT("Esc") } }, Pad)
			]
		]
	];
	RebuildList();
}

double SSimCruiseRoutePlanner::PxPerM() const
{
	return Scales[FMath::Clamp(Level, 0, NumScales - 1)];
}

int32 SSimCruiseRoutePlanner::NumStops() const
{
	return GetGps() ? GetGps()->DraftStops.Num() : 0;
}

void SSimCruiseRoutePlanner::Zoom(int32 Step)
{
	Level = FMath::Clamp(Level - Step, 0, NumScales - 1);
}

void SSimCruiseRoutePlanner::AddStop(const FVector2D& WorldM)
{
	USimCruiseRouteSubsystem* G = GetGps();
	if (!G || !View.Frame.IsValid() || G->DraftStops.Num() >= 10) return;
	FSimCruiseRouteStop Stop;
	View.Frame.WorldCmToLonLat(FVector(WorldM * 100.0, 0.0), Stop.Lon, Stop.Lat);
	// The nearest place name on the map, else a number until the router names the street.
	TArray<FSimCruiseMapTiles::FLabel> Labels;
	Tiles.Labels(View.Frame, CenterM, PxPerM(), SizePx, Labels);
	double Best = 60.0 / PxPerM();
	for (const FSimCruiseMapTiles::FLabel& L : Labels)
	{
		const double D = FVector2D::Distance(L.AtM, WorldM);
		if (D < Best && L.Priority < 6) { Best = D; Stop.Title = L.Text; }
	}
	if (Stop.Title.IsEmpty()) Stop.Title = FString::Printf(TEXT("Stop %d"), G->DraftStops.Num() + 1);
	G->DraftStops.Add(Stop);
	StopsChanged();
}

void SSimCruiseRoutePlanner::RemoveStop()
{
	USimCruiseRouteSubsystem* G = GetGps();
	if (!G || G->DraftStops.Num() == 0) return;
	int32 Remove = G->DraftStops.Num() - 1;
	for (int32 i = 0; i < G->DraftStops.Num(); ++i)
	{
		const FVector Cm = View.Frame.LonLatToWorldCm(G->DraftStops[i].Lon, G->DraftStops[i].Lat);
		if (FVector2D::Distance(ScreenOf(FVector2D(Cm.X, Cm.Y) / 100.0), SizePx * 0.5) < 40.0) { Remove = i; break; }
	}
	G->DraftStops.RemoveAt(Remove);
	StopsChanged();
}

void SSimCruiseRoutePlanner::StopsChanged()
{
	if (USimCruiseRouteSubsystem* G = GetGps()) G->RequestPreview(G->DraftStops);
	RebuildList();
}

void SSimCruiseRoutePlanner::RebuildList()
{
	if (!List.IsValid()) return;
	List->ClearChildren();
	auto Line = [this](const FString& Number, const FString& Title, const FString& Note, bool bCar)
	{
		List->AddSlot().AutoHeight().Padding(0, 0, 0, 12)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 18, 0)
			[
				SNew(SBox).WidthOverride(40.f).HeightOverride(40.f)
				[
					SNew(SBorder).BorderImage(RoundedBrush(bCar ? White() : Amber(), 20.f)).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0.f)
					[
						SNew(STextBlock).Text(FText::FromString(Number)).Font(Font(EWeight::Black, 21.f)).ColorAndOpacity(GuideDeep())
					]
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[Text(Title, EWeight::Bold, 28.f, White())]
				+ SVerticalBox::Slot().AutoHeight()[Text(Note, EWeight::Regular, 21.f, Mist())]
			]
		];
	};
	Line(TEXT("•"), TEXT("You are here"), View.Title, true);
	const USimCruiseRouteSubsystem* G = GetGps();
	if (!G) return;
	const FSimCruiseRoute& Preview = G->GetPreview();
	for (int32 i = 0; i < G->DraftStops.Num(); ++i)
	{
		FString Note = G->IsPreviewBusy() ? FString(TEXT("Finding the roads")) : FString(TEXT("No road to it"));
		if (Preview.GetLegs().IsValidIndex(i))
		{
			const FSimCruiseRouteLeg& Leg = Preview.GetLegs()[i];
			Note = FString::Printf(TEXT("%s, %s"), *SimCruiseRouteText::Distance(Leg.LengthM), *SimCruiseRouteText::Duration(Leg.DurationS));
		}
		if (i == G->DraftStops.Num() - 1) Note += TEXT(", end of route");
		Line(FString::FromInt(i + 1), G->DraftStops[i].Title, Note, false);
	}
}

void SSimCruiseRoutePlanner::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	SizePx = FVector2D(AllottedGeometry.GetLocalSize());
	if (GetView.IsBound()) View = GetView.Execute();
	if (!bCentered && View.bHasCar) { CenterM = FVector2D(View.CarCm.X, View.CarCm.Y) / 100.0; bCentered = true; }
	if (Stick.Size() > 0.19)
	{
		CenterM += FVector2D(Stick.X, -Stick.Y) * InDeltaTime * 520.0 / PxPerM();
	}
	Tiles.Update(View.Frame, CenterM, PxPerM(), SizePx);
	// A new preview names the stops still called by a number after their street.
	if (USimCruiseRouteSubsystem* G = GetGps())
	{
		if (G->GetPreviewSerial() != SeenSerial)
		{
			SeenSerial = G->GetPreviewSerial();
			const FSimCruiseRoute& Preview = G->GetPreview();
			for (int32 i = 0; i < G->DraftStops.Num() && Preview.GetLegs().IsValidIndex(i); ++i)
			{
				const TArray<FString>& Streets = Preview.GetLegs()[i].Streets;
				if (G->DraftStops[i].Title.StartsWith(TEXT("Stop ")) && Streets.Num() && !Streets.Last().IsEmpty()) G->DraftStops[i].Title = Streets.Last();
			}
			RebuildList();
		}
	}
}

int32 SSimCruiseRoutePlanner::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform()), WhiteBrush(), ESlateDrawEffect::None, FLinearColor(FColor(0x1B, 0x24, 0x22)));
	if (View.Frame.IsValid()) Tiles.Paint(OutDrawElements, LayerId + 1, AllottedGeometry, View.Frame, CenterM, PxPerM(), SizePx);
	const int32 Top = LayerId + 2;

	// The route the router found, else straight lines while it looks.
	const USimCruiseRouteSubsystem* G = GetGps();
	const FVector2D Car = ScreenOf(FVector2D(View.CarCm.X, View.CarCm.Y) / 100.0);
	TArray<FVector2D> StopsPx;
	if (G)
	{
		for (const FSimCruiseRouteStop& Stop : G->DraftStops)
		{
			const FVector Cm = View.Frame.LonLatToWorldCm(Stop.Lon, Stop.Lat);
			StopsPx.Add(ScreenOf(FVector2D(Cm.X, Cm.Y) / 100.0));
		}
		const FSimCruiseRoute& Preview = G->GetPreview();
		if (Preview.GetLegs().Num() && !G->IsPreviewBusy())
		{
			for (const FSimCruiseRouteLeg& Leg : Preview.GetLegs())
			{
				TArray<FVector2f> Pts;
				for (const FVector2D& M : Leg.M) Pts.Add(FVector2f(ScreenOf(M)));
				if (Pts.Num() >= 2) FSlateDrawElement::MakeLines(OutDrawElements, Top, AllottedGeometry.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, Course(), true, 7.f);
			}
		}
		else
		{
			FVector2D From = Car;
			for (const FVector2D& To : StopsPx)
			{
				const double Len = FVector2D::Distance(From, To);
				for (double T = 0.0; T < Len; T += 24.0)
				{
					const FVector2D A = FMath::Lerp(From, To, T / FMath::Max(Len, 1.0)), B = FMath::Lerp(From, To, FMath::Min(T + 12.0, Len) / FMath::Max(Len, 1.0));
					FSlateDrawElement::MakeLines(OutDrawElements, Top, AllottedGeometry.ToPaintGeometry(), { FVector2f(A), FVector2f(B) }, ESlateDrawEffect::None, Course(), true, 4.f);
				}
				From = To;
			}
		}
	}
	for (int32 i = 0; i < StopsPx.Num(); ++i)
	{
		const FVector2D At = StopsPx[i];
		FSlateDrawElement::MakeBox(OutDrawElements, Top + 1, AllottedGeometry.ToPaintGeometry(FVector2f(40.f, 40.f), FSlateLayoutTransform(FVector2f(At - FVector2D(20.0, 20.0)))), RoundedBrush(SimCruiseTheme::White(), 20.f), ESlateDrawEffect::None, FLinearColor(0.05f, 0.06f, 0.07f, 0.9f));
		FSlateDrawElement::MakeBox(OutDrawElements, Top + 2, AllottedGeometry.ToPaintGeometry(FVector2f(34.f, 34.f), FSlateLayoutTransform(FVector2f(At - FVector2D(17.0, 17.0)))), RoundedBrush(SimCruiseTheme::White(), 17.f), ESlateDrawEffect::None, Amber());
		FSlateDrawElement::MakeText(OutDrawElements, Top + 3, AllottedGeometry.ToPaintGeometry(FVector2f(30.f, 30.f), FSlateLayoutTransform(FVector2f(At - FVector2D(i >= 9 ? 12.0 : 6.0, 14.0)))),
			FString::FromInt(i + 1), Font(EWeight::Black, 21.f), ESlateDrawEffect::None, GuideDeep());
	}
	if (View.bHasCar)
	{
		const double Yaw = FMath::DegreesToRadians(View.CarYawDeg);
		const FVector2D Dir(FMath::Cos(Yaw), FMath::Sin(Yaw)), Right(-Dir.Y, Dir.X);
		FSlateDrawElement::MakeBox(OutDrawElements, Top + 1, AllottedGeometry.ToPaintGeometry(FVector2f(34.f, 34.f), FSlateLayoutTransform(FVector2f(Car - FVector2D(17.0, 17.0)))), RoundedBrush(GuideDeep(), 17.f, White(), 2.f));
		TArray<FVector2f> Arrow = { FVector2f(Car + Dir * 12), FVector2f(Car - Dir * 8 + Right * 7), FVector2f(Car - Dir * 4), FVector2f(Car - Dir * 8 - Right * 7), FVector2f(Car + Dir * 12) };
		FSlateDrawElement::MakeLines(OutDrawElements, Top + 2, AllottedGeometry.ToPaintGeometry(), MoveTemp(Arrow), ESlateDrawEffect::None, White(), true, 3.f);
	}
	// The cross.
	const FVector2D C = SizePx * 0.5;
	for (const FLinearColor& Color : { FLinearColor(0.f, 0.f, 0.f, 0.6f), White() })
	{
		const float T = Color.A < 1.f ? 7.f : 3.f;
		FSlateDrawElement::MakeLines(OutDrawElements, Top + 4, AllottedGeometry.ToPaintGeometry(), { FVector2f(C - FVector2D(26, 0)), FVector2f(C - FVector2D(8, 0)) }, ESlateDrawEffect::None, Color, true, T);
		FSlateDrawElement::MakeLines(OutDrawElements, Top + 4, AllottedGeometry.ToPaintGeometry(), { FVector2f(C + FVector2D(8, 0)), FVector2f(C + FVector2D(26, 0)) }, ESlateDrawEffect::None, Color, true, T);
		FSlateDrawElement::MakeLines(OutDrawElements, Top + 4, AllottedGeometry.ToPaintGeometry(), { FVector2f(C - FVector2D(0, 26)), FVector2f(C - FVector2D(0, 8)) }, ESlateDrawEffect::None, Color, true, T);
		FSlateDrawElement::MakeLines(OutDrawElements, Top + 4, AllottedGeometry.ToPaintGeometry(), { FVector2f(C + FVector2D(0, 8)), FVector2f(C + FVector2D(0, 26)) }, ESlateDrawEffect::None, Color, true, T);
	}
	FSlateDrawElement::MakeText(OutDrawElements, Top + 4, AllottedGeometry.ToPaintGeometry(FVector2f(420.f, 24.f), FSlateLayoutTransform(FVector2f(SizePx.X - 400.0, SizePx.Y - 40.0))),
		FString(TEXT("N up    © OpenStreetMap contributors")), Font(EWeight::Regular, 18.f), ESlateDrawEffect::None, Mist());
	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, Top + 6, InWidgetStyle, bParentEnabled);
}

FReply SSimCruiseRoutePlanner::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	const ENav Nav = NavOf(Key);
	if (Nav == ENav::None) return FReply::Unhandled();
	bPad = Key.IsGamepadKey();
	const double Step = 90.0 / PxPerM();
	switch (Nav)
	{
	case ENav::Up: CenterM.Y -= Step; break;
	case ENav::Down: CenterM.Y += Step; break;
	case ENav::Left: CenterM.X -= Step; break;
	case ENav::Right: CenterM.X += Step; break;
	case ENav::Accept: AddStop(CenterM); break;
	case ENav::X: RemoveStop(); break;
	case ENav::Y: OnStart.ExecuteIfBound(); break;
	case ENav::ZoomIn: case ENav::NextPage: Zoom(1); break;
	case ENav::ZoomOut: case ENav::PrevPage: Zoom(-1); break;
	case ENav::Back: case ENav::Menu: OnBack.ExecuteIfBound(); break;
	default: break;
	}
	return FReply::Handled();
}

FReply SSimCruiseRoutePlanner::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftX) { Stick.X = InAnalogInputEvent.GetAnalogValue(); bPad = true; return FReply::Handled(); }
	if (Key == EKeys::Gamepad_LeftY) { Stick.Y = InAnalogInputEvent.GetAnalogValue(); bPad = true; return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SSimCruiseRoutePlanner::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	bDragging = true;
	bMoved = false;
	bPad = false;
	LastMouse = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSimCruiseRoutePlanner::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging) return FReply::Unhandled();
	bDragging = false;
	if (!bMoved) AddStop(WorldOf(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())));
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SSimCruiseRoutePlanner::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging) return FReply::Unhandled();
	const FVector2D At = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	if (FVector2D::Distance(At, LastMouse) > 3.0 || bMoved)
	{
		CenterM -= (At - LastMouse) / PxPerM();
		LastMouse = At;
		bMoved = true;
	}
	return FReply::Handled();
}

FReply SSimCruiseRoutePlanner::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Zoom(MouseEvent.GetWheelDelta() > 0.f ? 1 : -1);
	return FReply::Handled();
}
