// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseAtlasUI.h"

#include "Async/Async.h"
#include "Atlas/SimCruiseAtlasClient.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/CoreStyle.h"
#include "TextureResource.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseAtlasUI, Log, All);

namespace SimCruiseUiStyle
{
	FLinearColor Ink() { return FLinearColor(FColor(0x15, 0x22, 0x1f)); }
	FLinearColor Glass() { return FLinearColor(FColor(0x15, 0x22, 0x1f, 0xe6)); }
	FLinearColor Paper() { return FLinearColor(FColor(0xf4, 0xef, 0xdb)); }
	FLinearColor Lime() { return FLinearColor(FColor(0xc6, 0xef, 0x96)); }
	FLinearColor Dim() { return FLinearColor(FColor(0xb2, 0xc3, 0xb9)); }
}

namespace
{
	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	TSharedRef<SWidget> Label(const FString& Text, int32 Size, const FLinearColor& Color, bool bBold = false)
	{
		return SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size, bBold)).ColorAndOpacity(Color);
	}

	TSharedRef<SButton> Button(const FString& Text, TFunction<void()> OnClick, int32 Size = 14)
	{
		return SNew(SButton)
			.ButtonColorAndOpacity(FLinearColor(FColor(0x2a, 0x3d, 0x37)))
			.ForegroundColor(SimCruiseUiStyle::Paper())
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(FText::FromString(Text)).Font(Font(Size)).ColorAndOpacity(SimCruiseUiStyle::Paper())
			];
	}

	bool IsCloseKey(const FKey& Key)
	{
		return Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Right;
	}

	FSimCruiseAtlasConfig HelperConfig()
	{
		static FSimCruiseAtlasConfig Config = FSimCruiseAtlasConfig::ResolveDefault();
		return Config;
	}

	// ------------------------------------------------------------------ map tile raster
	// The helper's `map z x y` document (helper/cartography.mjs): lines [[class, [[x,y],..]]],
	// fills [[kind, [ring, ..]]] (0 water, else green), names [[name, x, y, priority, bold]],
	// in 0..4096 across the tile. RoadMap.gd drew it as SVG at 512 px; this draws the same.
	constexpr int32 MapPx = 512;
	constexpr double MapUnits = 4096.0;

	struct FRaster
	{
		TArray<FColor> Px;
		FRaster() { Px.Init(FColor(0x22, 0x33, 0x2e, 0xff), MapPx * MapPx); }

		void Blend(int32 X, int32 Y, const FColor& C, float A)
		{
			if (X < 0 || Y < 0 || X >= MapPx || Y >= MapPx || A <= 0.f) return;
			FColor& D = Px[Y * MapPx + X];
			A = FMath::Min(A, 1.f);
			D.R = static_cast<uint8>(FMath::Lerp(static_cast<float>(D.R), static_cast<float>(C.R), A));
			D.G = static_cast<uint8>(FMath::Lerp(static_cast<float>(D.G), static_cast<float>(C.G), A));
			D.B = static_cast<uint8>(FMath::Lerp(static_cast<float>(D.B), static_cast<float>(C.B), A));
		}

		/** Even-odd fill of a set of rings (map units). */
		void Fill(const TArray<TArray<FVector2D>>& Rings, const FColor& C)
		{
			TArray<double> Cross;
			for (int32 Y = 0; Y < MapPx; ++Y)
			{
				const double V = (Y + 0.5) * MapUnits / MapPx;
				Cross.Reset();
				for (const TArray<FVector2D>& Ring : Rings)
				{
					for (int32 i = 0, n = Ring.Num(); i < n; ++i)
					{
						const FVector2D& A = Ring[i];
						const FVector2D& B = Ring[(i + 1) % n];
						if ((A.Y <= V) != (B.Y <= V))
						{
							Cross.Add(A.X + (V - A.Y) / (B.Y - A.Y) * (B.X - A.X));
						}
					}
				}
				Cross.Sort();
				for (int32 k = 0; k + 1 < Cross.Num(); k += 2)
				{
					const int32 X0 = FMath::Max(0, FMath::RoundToInt(Cross[k] * MapPx / MapUnits));
					const int32 X1 = FMath::Min(MapPx, FMath::RoundToInt(Cross[k + 1] * MapPx / MapUnits));
					for (int32 X = X0; X < X1; ++X) Blend(X, Y, C, 1.f);
				}
			}
		}

		/** A polyline of the given width (map units), antialiased, round joins. */
		void Line(const TArray<FVector2D>& Pts, double WidthUnits, const FColor& C)
		{
			const double K = MapPx / MapUnits;
			const double Half = FMath::Max(WidthUnits * K * 0.5, 0.45);
			for (int32 i = 0; i + 1 < Pts.Num(); ++i)
			{
				const FVector2D A = Pts[i] * K;
				const FVector2D B = Pts[i + 1] * K;
				const int32 X0 = FMath::FloorToInt(FMath::Min(A.X, B.X) - Half - 1), X1 = FMath::CeilToInt(FMath::Max(A.X, B.X) + Half + 1);
				const int32 Y0 = FMath::FloorToInt(FMath::Min(A.Y, B.Y) - Half - 1), Y1 = FMath::CeilToInt(FMath::Max(A.Y, B.Y) + Half + 1);
				const FVector2D AB = B - A;
				const double L2 = FMath::Max(AB.SizeSquared(), 1e-9);
				for (int32 Y = FMath::Max(Y0, 0); Y <= FMath::Min(Y1, MapPx - 1); ++Y)
				{
					for (int32 X = FMath::Max(X0, 0); X <= FMath::Min(X1, MapPx - 1); ++X)
					{
						const FVector2D P(X + 0.5, Y + 0.5);
						const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0, 1.0);
						const double D = FVector2D::Distance(P, A + AB * T);
						const float Cover = static_cast<float>(FMath::Clamp(Half + 0.5 - D, 0.0, 1.0));
						if (Cover > 0.f)
						{
							// Overlapping segments of one line must not darken twice: keep the max.
							FColor& Dst = Px[Y * MapPx + X];
							const float Already = Dst == C ? 1.f : 0.f;
							if (Already < 1.f) Blend(X, Y, C, Cover);
						}
					}
				}
			}
		}
	};

	bool ReadPoints(const TSharedPtr<FJsonValue>& Value, TArray<FVector2D>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Points)) return false;
		for (const TSharedPtr<FJsonValue>& P : *Points)
		{
			const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
			if (P.IsValid() && P->TryGetArray(XY) && XY->Num() >= 2)
			{
				Out.Add(FVector2D((*XY)[0]->AsNumber(), (*XY)[1]->AsNumber()));
			}
		}
		return Out.Num() > 0;
	}

	/** Rasterise a map document; false when it is not one. */
	bool RasterMap(const FString& Raw, TArray<FColor>& OutPixels, TArray<FSimCruiseMapLabel>& OutNames)
	{
		TSharedPtr<FJsonObject> Doc;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Doc) || !Doc.IsValid() || !Doc->HasField(TEXT("lines")))
		{
			return false;
		}
		FRaster R;
		const TArray<TSharedPtr<FJsonValue>>* Fills = nullptr;
		if (Doc->TryGetArrayField(TEXT("fills"), Fills))
		{
			for (const TSharedPtr<FJsonValue>& F : *Fills)
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (!F.IsValid() || !F->TryGetArray(Parts) || Parts->Num() < 2) continue;
				const int32 Kind = static_cast<int32>((*Parts)[0]->AsNumber());
				const TArray<TSharedPtr<FJsonValue>>* RingValues = nullptr;
				if (!(*Parts)[1]->TryGetArray(RingValues)) continue;
				TArray<TArray<FVector2D>> Rings;
				for (const TSharedPtr<FJsonValue>& RingValue : *RingValues)
				{
					TArray<FVector2D> Ring;
					if (ReadPoints(RingValue, Ring) && Ring.Num() >= 3) Rings.Add(MoveTemp(Ring));
				}
				R.Fill(Rings, Kind == 0 ? FColor(0x29, 0x4c, 0x5a) : FColor(0x2c, 0x44, 0x35));
			}
		}
		static const FColor Colors[] = {
			FColor(0x62, 0x6a, 0x61), FColor(0x54, 0x67, 0x5e), FColor(0x66, 0x79, 0x6c),
			FColor(0x84, 0x93, 0x85), FColor(0xb5, 0xb7, 0x98), FColor(0xcf, 0xa9, 0x6b) };
		static const double Widths[] = { 6, 7, 9, 12, 16, 22 };
		const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
		if (Doc->TryGetArrayField(TEXT("lines"), Lines))
		{
			// Minor roads first so the main roads draw over them.
			for (int32 Pass = 0; Pass < 6; ++Pass)
			{
				for (const TSharedPtr<FJsonValue>& L : *Lines)
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if (!L.IsValid() || !L->TryGetArray(Parts) || Parts->Num() < 2) continue;
					const int32 Cls = FMath::Clamp(static_cast<int32>((*Parts)[0]->AsNumber()), 0, 5);
					if (Cls != Pass) continue;
					TArray<FVector2D> Pts;
					if (ReadPoints((*Parts)[1], Pts)) R.Line(Pts, Widths[Cls], Colors[Cls]);
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (Doc->TryGetArrayField(TEXT("names"), Names))
		{
			for (const TSharedPtr<FJsonValue>& N : *Names)
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (!N.IsValid() || !N->TryGetArray(Parts) || Parts->Num() < 5) continue;
				FSimCruiseMapLabel Label;
				Label.Text = (*Parts)[0]->AsString();
				Label.At = FVector2D((*Parts)[1]->AsNumber(), (*Parts)[2]->AsNumber());
				Label.Priority = static_cast<int32>((*Parts)[3]->AsNumber());
				Label.bBold = (*Parts)[4]->AsNumber() != 0.0 || (*Parts)[4]->AsBool();
				if (!Label.Text.IsEmpty()) OutNames.Add(MoveTemp(Label));
			}
		}
		OutPixels = MoveTemp(R.Px);
		return true;
	}

	FString MapKey(const FIntVector& Id) { return FString::Printf(TEXT("%d_%d_%d"), Id.Z, Id.X, Id.Y); }
}

// ====================================================================== place search

struct SSimCruisePlaceSearch::FPending
{
	std::atomic<bool> bDone{ false };
	uint32 Serial = 0;
	TArray<FSimCruisePlace> Places;
	FString Error;
};

void SSimCruisePlaceSearch::Construct(const FArguments& InArgs)
{
	OnChoose = InArgs._OnChoose;
	OnSurprise = InArgs._OnSurprise;
	OnClose = InArgs._OnClose;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.45f))
		]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(620.f)
			[
				SNew(SBorder)
				.BorderImage(new FSlateRoundedBoxBrush(SimCruiseUiStyle::Glass(), 14.f))
				.Padding(FMargin(22.f, 18.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
					[
						Label(TEXT("Where to?"), 26, SimCruiseUiStyle::Paper(), true)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
						[
							SAssignNew(Field, SEditableTextBox)
							.Font(Font(15))
							.HintText(FText::FromString(TEXT("Street, city, landmark, or latitude, longitude")))
							.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type How)
							{
								if (How == ETextCommit::OnEnter) Submit(Text.ToString());
							})
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(TEXT("Search"), [this]() { Submit(Field->GetText().ToString()); })
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
						[
							Button(TEXT("Surprise me"), [this]() { DoSurprise(); })
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 8)
					[
						SAssignNew(Status, STextBlock).Font(Font(13)).ColorAndOpacity(SimCruiseUiStyle::Dim()).AutoWrapText(true)
					]
					+ SVerticalBox::Slot().MaxHeight(380.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(Choices, SVerticalBox)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[
							Label(TEXT("Enter or A: go  ·  Esc or B: back to the drive"), 12, SimCruiseUiStyle::Dim())
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							Button(TEXT("Back to drive"), [this]() { OnClose.ExecuteIfBound(); })
						]
					]
					// The data the world is built from (Hud.gd credits block).
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
					[
						SNew(STextBlock).Font(Font(10)).ColorAndOpacity(SimCruiseUiStyle::Dim()).AutoWrapText(true)
						.Text(FText::FromString(TEXT("Map data © OpenStreetMap contributors (ODbL), served by OpenFreeMap and OpenMapTiles · search by Photon (komoot) · elevation USGS 3DEP and Mapzen · aerial photographs USGS NAIP and The National Map · trees WRI and Meta canopy height map v2 (CC BY 4.0) · buildings Overture Maps and OpenStreetMap · full list in DATA-SOURCES.md")))
					]
				]
			]
		]
	];
	ShowResults(SimCruisePlaces::Presets(), TEXT("Pick a place, search, or press Surprise me for a random road."));
}

SSimCruisePlaceSearch::~SSimCruisePlaceSearch()
{
	Pending.Reset();
}

TSharedPtr<SWidget> SSimCruisePlaceSearch::FocusTarget(bool bGamepad) const
{
	return (bGamepad && ChoiceButtons.Num() > 0) ? ChoiceButtons[0] : StaticCastSharedPtr<SWidget>(Field);
}

void SSimCruisePlaceSearch::FocusFirst(bool bGamepad)
{
	TSharedPtr<SWidget> Target = FocusTarget(bGamepad);
	if (Target.IsValid())
	{
		FSlateApplication::Get().SetAllUserFocus(Target, EFocusCause::SetDirectly);
	}
}

void SSimCruisePlaceSearch::ShowResults(const TArray<FSimCruisePlace>& Places, const FString& Note)
{
	Status->SetText(FText::FromString(Note));
	Choices->ClearChildren();
	ChoiceButtons.Reset();
	for (const FSimCruisePlace& Place : Places)
	{
		TSharedRef<SButton> B = SNew(SButton)
			.ButtonColorAndOpacity(FLinearColor(FColor(0x22, 0x33, 0x2e)))
			.ContentPadding(FMargin(10.f, 7.f))
			.OnClicked_Lambda([this, Place]() { Choose(Place); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(FText::FromString(Place.Title)).Font(Font(14)).ColorAndOpacity(SimCruiseUiStyle::Paper())
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			];
		Choices->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[B];
		ChoiceButtons.Add(B);
	}
}

void SSimCruisePlaceSearch::Choose(const FSimCruisePlace& Place)
{
	Pending.Reset();
	OnChoose.ExecuteIfBound(Place);
}

void SSimCruisePlaceSearch::DoSurprise()
{
	Pending.Reset();
	if (!OnSurprise.IsBound() || !OnSurprise.Execute())
	{
		ShowResults(SimCruisePlaces::Presets(), TEXT("No start pool is bundled. Search for a place instead."));
	}
}

void SSimCruisePlaceSearch::Submit(const FString& Query)
{
	++Serial;
	Pending.Reset();
	const SimCruisePlaces::FQueryAnswer Answer = SimCruisePlaces::ParseQuery(Query);
	switch (Answer.Kind)
	{
	case SimCruisePlaces::EQueryKind::Empty:
		ShowResults(SimCruisePlaces::Presets(), TEXT("Pick a place, search, or press Surprise me for a random road."));
		return;
	case SimCruisePlaces::EQueryKind::Surprise:
		DoSurprise();
		return;
	case SimCruisePlaces::EQueryKind::Places:
		ShowResults(Answer.Places, Answer.Note);
		FocusFirst(true);
		return;
	case SimCruisePlaces::EQueryKind::Search:
		break;
	}
	const FSimCruiseAtlasConfig Config = HelperConfig();
	if (!Config.IsConfigured())
	{
		ShowResults(SimCruisePlaces::Presets(), TEXT("Search needs the atlas helper (tools/atlas.exe). Type latitude, longitude, or pick a place."));
		return;
	}
	ShowResults({}, TEXT("Searching…"));
	TSharedPtr<FPending, ESPMode::ThreadSafe> Job = MakeShared<FPending, ESPMode::ThreadSafe>();
	Job->Serial = Serial;
	Pending = Job;
	FSimCruiseAtlasRequest Request;
	Request.Command = TEXT("search");
	Request.Args = { Query.TrimStartAndEnd() };
	Request.TimeoutSeconds = 25.0;
	Async(EAsyncExecution::ThreadPool, [Job, Config, Request]()
	{
		const FSimCruiseAtlasResult Result = FSimCruiseAtlasClient::RunBlocking(Config, Request, nullptr);
		if (Result.IsOk()) Job->Places = SimCruisePlaces::ParseSearchResults(Result.Json);
		else Job->Error = Result.Describe();
		Job->bDone = true;
	});
}

void SSimCruisePlaceSearch::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (Pending.IsValid() && Pending->bDone)
	{
		const TSharedPtr<FPending, ESPMode::ThreadSafe> Done = Pending;
		Pending.Reset();
		if (Done->Serial != Serial) return;
		if (!Done->Error.IsEmpty())
		{
			UE_LOG(LogSimCruiseAtlasUI, Warning, TEXT("Place search failed: %s"), *Done->Error);
			ShowResults(SimCruisePlaces::Presets(), TEXT("Search did not answer (offline?). Type latitude, longitude, press Surprise me, or pick Miami Beach."));
		}
		else if (Done->Places.IsEmpty())
		{
			ShowResults(SimCruisePlaces::Presets(), TEXT("No place found. Try latitude, longitude, Surprise me, or Miami Beach."));
		}
		else
		{
			ShowResults(Done->Places, TEXT("Choose a place."));
			// The answers take the focus: Enter, A or the arrows pick one.
			FocusFirst(true);
		}
	}
}

FReply SSimCruisePlaceSearch::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (IsCloseKey(Key) || Key == EKeys::Slash)
	{
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::Gamepad_FaceButton_Top)
	{
		DoSurprise();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ====================================================================== road atlas

struct SSimCruiseRoadAtlas::FFetch
{
	FString Key;
	std::atomic<bool> bDone{ false };
	bool bOk = false;
	TArray<FColor> Pixels;
	TArray<FSimCruiseMapLabel> Names;
};

const TArray<double>& SSimCruiseRoadAtlas::Scales()
{
	static const TArray<double> List = { 1.3, .74, .37, .185, .0925, .046, .023, .0115, .00575, .002875, .00144 };
	return List;
}

void SSimCruiseRoadAtlas::Construct(const FArguments& InArgs)
{
	GetView = InArgs._GetView;
	OnChoose = InArgs._OnChoose;
	OnSurprise = InArgs._OnSurprise;
	OnFindPlace = InArgs._OnFindPlace;
	OnCycleCar = InArgs._OnCycleCar;
	OnToggleTransmission = InArgs._OnToggleTransmission;
	OnSetting = InArgs._OnSetting;
	OnClose = InArgs._OnClose;
	TSharedRef<SHorizontalBox> SettingsRow = SNew(SHorizontalBox);
	for (int32 Index = 0; Index < 7; ++Index)
	{
		TSharedPtr<STextBlock> Text;
		SettingsRow->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
		[
			SNew(SButton)
			.ButtonColorAndOpacity(FLinearColor(FColor(0x2a, 0x3d, 0x37)))
			.ContentPadding(FMargin(12.f, 6.f))
			.OnClicked_Lambda([this, Index]() { OnSetting.ExecuteIfBound(Index); return FReply::Handled(); })
			[
				SAssignNew(Text, STextBlock).Font(Font(14)).ColorAndOpacity(SimCruiseUiStyle::Paper())
			]
		];
		SettingLabels.Add(Text);
	}
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor(FColor(0x17, 0x25, 0x22, 0xf2)))
			.Padding(FMargin(16.f, 10.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(Status, STextBlock).Font(Font(19)).ColorAndOpacity(SimCruiseUiStyle::Paper())
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SAssignNew(Coords, STextBlock).Font(Font(13)).ColorAndOpacity(SimCruiseUiStyle::Dim())
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("−"), [this]() { Zoom(1); }, 16)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("+"), [this]() { Zoom(-1); }, 16)]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("Recenter"), [this]() { Recenter(); })]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("Drive here"), [this]() { DriveToPin(); })]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0)[Button(TEXT("Back to drive"), [this]() { OnClose.ExecuteIfBound(); })]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(TEXT("Find a place"), [this]() { OnFindPlace.ExecuteIfBound(); })]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Button(TEXT("Surprise me"), [this]() { if (OnSurprise.IsBound()) OnSurprise.Execute(); })]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(SButton)
						.ButtonColorAndOpacity(FLinearColor(FColor(0x2a, 0x3d, 0x37)))
						.ContentPadding(FMargin(12.f, 6.f))
						.OnClicked_Lambda([this]() { OnCycleCar.ExecuteIfBound(); return FReply::Handled(); })
						[
							SAssignNew(CarLabel, STextBlock).Font(Font(14)).ColorAndOpacity(SimCruiseUiStyle::Paper())
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
					[
						SNew(SButton)
						.ButtonColorAndOpacity(FLinearColor(FColor(0x2a, 0x3d, 0x37)))
						.ContentPadding(FMargin(12.f, 6.f))
						.OnClicked_Lambda([this]() { OnToggleTransmission.ExecuteIfBound(); return FReply::Handled(); })
						[
							SAssignNew(GearLabel, STextBlock).Font(Font(14)).ColorAndOpacity(SimCruiseUiStyle::Paper())
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SettingsRow
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
					[
						Label(TEXT("Stick or drag: move  ·  D-pad, wheel or +/−: zoom  ·  A or Enter: drive here  ·  X: find  ·  Y: recenter  ·  RB: car  ·  B: back"), 12, SimCruiseUiStyle::Dim())
					]
				]
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNullWidget::NullWidget
		]
	];
	if (GetView.IsBound()) View = GetView.Execute();
	Recenter();
}

SSimCruiseRoadAtlas::~SSimCruiseRoadAtlas()
{
	Fetch.Reset();
}

void SSimCruiseRoadAtlas::Recenter()
{
	if (GetView.IsBound()) View = GetView.Execute();
	CenterM = FVector2D(View.CarCm.X, View.CarCm.Y) / 100.0;
	bPinned = false;
	bCentered = true;
}

void SSimCruiseRoadAtlas::Zoom(int32 Step, const FVector2D* AnchorPx)
{
	const TArray<double>& S = Scales();
	const double Before = S[Level];
	Level = FMath::Clamp(Level + Step, 0, S.Num() - 1);
	if (AnchorPx)
	{
		CenterM += (*AnchorPx - SizePx * 0.5) * (1.0 / Before - 1.0 / S[Level]);
	}
}

FVector2D SSimCruiseRoadAtlas::ScreenOf(const FVector2D& WorldM) const
{
	return (WorldM - CenterM) * Scales()[Level] + SizePx * 0.5;
}

FVector2D SSimCruiseRoadAtlas::WorldOfScreen(const FVector2D& ScreenPx) const
{
	return CenterM + (ScreenPx - SizePx * 0.5) / Scales()[Level];
}

FSimCruisePlace SSimCruiseRoadAtlas::PlaceAt(const FVector2D& WorldM) const
{
	double Lon = 0.0, Lat = 0.0;
	View.Frame.WorldCmToLonLat(FVector(WorldM.X * 100.0, WorldM.Y * 100.0, 0.0), Lon, Lat);
	return FSimCruisePlace::Make(Lon, Lat, FString::Printf(TEXT("%.5f, %.5f"), Lat, Lon));
}

void SSimCruiseRoadAtlas::DriveToPin()
{
	if (!View.Frame.IsValid()) return;
	// A pad user moves the map under the crosshair: the centre is the spot.
	const FVector2D Spot = bPinned ? PinM : CenterM;
	const FSimCruisePlace Place = PlaceAt(Spot);
	if (Place.IsValid()) OnChoose.ExecuteIfBound(Place);
}

int32 SSimCruiseRoadAtlas::NumMapImages() const
{
	return Images.Num();
}

void SSimCruiseRoadAtlas::WantedTiles(TArray<FIntVector>& Out) const
{
	Out.Reset();
	if (!View.Frame.IsValid()) return;
	const double Side = View.Frame.TileSizeM;
	const double Scale = Scales()[Level];
	// The map zoom whose tiles draw near 460 px wide (RoadMap.gd refresh).
	const int32 Z = FMath::Clamp(FMath::RoundToInt(16.0 - FMath::Log2(460.0 / (Side * Scale))), 4, 14);
	const double Count = static_cast<double>(1 << (16 - Z));
	const FVector2D Ext = SizePx * 0.5 / Scale;
	const FVector2d Lo = View.Frame.WorldCmToTileCoord(FVector2d((CenterM.X - Ext.X) * 100.0, (CenterM.Y - Ext.Y) * 100.0));
	const FVector2d Hi = View.Frame.WorldCmToTileCoord(FVector2d((CenterM.X + Ext.X) * 100.0, (CenterM.Y + Ext.Y) * 100.0));
	const FVector2d C = View.Frame.WorldCmToTileCoord(FVector2d(CenterM.X * 100.0, CenterM.Y * 100.0));
	const int32 N = 1 << Z;
	for (int32 X = FMath::FloorToInt(Lo.X / Count); X <= FMath::FloorToInt(Hi.X / Count); ++X)
	{
		for (int32 Y = FMath::FloorToInt(Lo.Y / Count); Y <= FMath::FloorToInt(Hi.Y / Count); ++Y)
		{
			if (X < 0 || Y < 0 || X >= N || Y >= N) continue;
			Out.Add(FIntVector(X, Y, Z));
		}
	}
	// Nearest the centre first.
	Out.Sort([&C, Count](const FIntVector& A, const FIntVector& B)
	{
		const FVector2d CA((A.X + 0.5) * Count, (A.Y + 0.5) * Count), CB((B.X + 0.5) * Count, (B.Y + 0.5) * Count);
		return FVector2d::DistSquared(CA, C) < FVector2d::DistSquared(CB, C);
	});
}

void SSimCruiseRoadAtlas::StartFetches()
{
	if (Fetch.IsValid()) return;
	TArray<FIntVector> Wanted;
	WantedTiles(Wanted);
	const double Now = FPlatformTime::Seconds();
	for (const FIntVector& Id : Wanted)
	{
		const FString Key = MapKey(Id);
		if (Images.Contains(Key)) continue;
		if (const double* Retry = RetryAt.Find(Key); Retry && *Retry > Now) continue;
		TSharedPtr<FFetch, ESPMode::ThreadSafe> Job = MakeShared<FFetch, ESPMode::ThreadSafe>();
		Job->Key = Key;
		Fetch = Job;
		const FString Offline = FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("map"), FString::Printf(TEXT("map-%s.json"), *Key));
		const FSimCruiseAtlasConfig Config = HelperConfig();
		Async(EAsyncExecution::ThreadPool, [Job, Id, Offline, Config]()
		{
			// Offline tiles first (the bundled places), then the helper (RoadMap.gd load_map).
			FString Raw;
			if (!FFileHelper::LoadFileToString(Raw, *Offline) && Config.IsConfigured())
			{
				FSimCruiseAtlasRequest Request;
				Request.Command = TEXT("map");
				Request.Args = { FString::FromInt(Id.Z), FString::FromInt(Id.X), FString::FromInt(Id.Y) };
				Request.TimeoutSeconds = 40.0;
				const FSimCruiseAtlasResult Result = FSimCruiseAtlasClient::RunBlocking(Config, Request, nullptr);
				if (Result.Error == ESimCruiseAtlasError::None) Raw = Result.Stdout;
			}
			Job->bOk = !Raw.IsEmpty() && RasterMap(Raw, Job->Pixels, Job->Names);
			Job->bDone = true;
		});
		return;
	}
}

void SSimCruiseRoadAtlas::CollectFetches()
{
	if (!Fetch.IsValid() || !Fetch->bDone) return;
	const TSharedPtr<FFetch, ESPMode::ThreadSafe> Done = Fetch;
	Fetch.Reset();
	if (!Done->bOk || Done->Pixels.Num() != MapPx * MapPx)
	{
		RetryAt.Add(Done->Key, FPlatformTime::Seconds() + 60.0);
		return;
	}
	if (Images.Num() >= 64)
	{
		FString Oldest;
		double OldestTime = TNumericLimits<double>::Max();
		for (const TPair<FString, FMapImage>& Pair : Images)
		{
			if (Pair.Value.LastUsed < OldestTime) { OldestTime = Pair.Value.LastUsed; Oldest = Pair.Key; }
		}
		Images.Remove(Oldest);
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(MapPx, MapPx, PF_B8G8R8A8, *FString::Printf(TEXT("SimCruiseMap_%s"), *Done->Key));
	if (!Texture) return;
	Texture->Filter = TF_Bilinear;
	Texture->SRGB = true;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Done->Pixels.GetData(), Done->Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	FMapImage& Image = Images.Add(Done->Key);
	Image.Texture.Reset(Texture);
	Image.Brush = MakeShared<FSlateBrush>();
	Image.Brush->SetResourceObject(Texture);
	Image.Brush->ImageSize = FVector2D(MapPx, MapPx);
	Image.Brush->DrawAs = ESlateBrushDrawType::Image;
	Image.Names = MoveTemp(Done->Names);
	Image.LastUsed = FPlatformTime::Seconds();
}

void SSimCruiseRoadAtlas::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	SizePx = FVector2D(AllottedGeometry.GetLocalSize());
	if (GetView.IsBound())
	{
		View = GetView.Execute();
		if (!bCentered) Recenter();
	}
	if (Stick.Size() > 0.19)
	{
		// RoadMap.gd: 430 px a second at full stick. The stick's Y is up.
		CenterM += FVector2D(Stick.X, -Stick.Y) * InDeltaTime * 430.0 / Scales()[Level];
		bPinned = false;
	}
	CollectFetches();
	StartFetches();

	double Lon = 0.0, Lat = 0.0;
	if (View.Frame.IsValid())
	{
		View.Frame.WorldCmToLonLat(FVector((bPinned ? PinM : CenterM) * 100.0, 0.0), Lon, Lat);
	}
	const bool bAtCar = !bPinned && FVector2D::Distance(CenterM, FVector2D(View.CarCm.X, View.CarCm.Y) / 100.0) < 1.0;
	Status->SetText(FText::FromString(bPinned ? TEXT("Pinned spot · A, Enter or Drive here to go") : bAtCar ? FString::Printf(TEXT("You are in %s"), *View.Title) : TEXT("Map centre · A, Enter or Drive here to go")));
	Coords->SetText(FText::FromString(FString::Printf(TEXT("%.5f°, %.5f°   ·   %s"), Lat, Lon, *View.Title)));
	CarLabel->SetText(FText::FromString(FString::Printf(TEXT("Car · %s"), *View.CarName)));
	GearLabel->SetText(FText::FromString(FString::Printf(TEXT("Transmission · %s"), *View.Transmission)));
	for (int32 i = 0; i < SettingLabels.Num(); ++i)
	{
		SettingLabels[i]->SetText(FText::FromString(View.Settings.IsValidIndex(i) ? View.Settings[i] : FString()));
	}
}

int32 SSimCruiseRoadAtlas::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform()), White, ESlateDrawEffect::None, FLinearColor(FColor(0x22, 0x33, 0x2e)));
	const int32 MapLayer = LayerId + 1;
	const double Scale = Scales()[Level];

	struct FSpot { int32 Priority; FString Text; FVector2D At; bool bBold; };
	TArray<FSpot> Spots;
	TArray<FIntVector> Wanted;
	WantedTiles(Wanted);
	const double Now = FPlatformTime::Seconds();
	for (const FIntVector& Id : Wanted)
	{
		const FMapImage* Image = Images.Find(MapKey(Id));
		if (!Image) continue;
		const_cast<FMapImage*>(Image)->LastUsed = Now;
		const double Count = static_cast<double>(1 << (16 - Id.Z));
		const FVector2d NwCm = View.Frame.TileCoordToWorldCm(FVector2d(Id.X * Count, Id.Y * Count));
		const FVector2D Nw = ScreenOf(FVector2D(NwCm.X, NwCm.Y) / 100.0);
		const double Width = View.Frame.TileSizeM * Count * Scale;
		FSlateDrawElement::MakeBox(OutDrawElements, MapLayer,
			AllottedGeometry.ToPaintGeometry(FVector2f(Width + 0.5, Width + 0.5), FSlateLayoutTransform(FVector2f(Nw))), Image->Brush.Get());
		for (const FSimCruiseMapLabel& Name : Image->Names)
		{
			const FVector2D P = Nw + Name.At * Width / MapUnits;
			if (P.X < 22 || P.X > SizePx.X - 22 || P.Y < 110 || P.Y > SizePx.Y - 54) continue;
			Spots.Add({ Name.Priority, Name.Text, P, Name.bBold });
		}
	}

	// The roads of the tiles already built around the car, at the close zooms.
	const int32 RoadLayer = MapLayer + 1;
	if (Level < 5 && View.Streamer.IsValid())
	{
		const ASimCruiseWorldStreamer* Stream = View.Streamer.Get();
		for (int32 dx = -2; dx <= 2; ++dx)
		{
			for (int32 dy = -2; dy <= 2; ++dy)
			{
				const FIntPoint TileId = View.Frame.TileAtWorldCm(View.CarCm) + FIntPoint(dx, dy);
				const ASimCruiseWorldTile* Tile = Stream->GetTile(TileId);
				const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
				if (!Build) continue;
				const FVector2D Origin = FVector2D(Tile->GetActorLocation().X, Tile->GetActorLocation().Y) / 100.0;
				for (const FSimCruiseRibbon& Ribbon : Build->Roads.Ribbons)
				{
					if (Ribbon.St.Num() < 2) continue;
					TArray<FVector2f> Pts;
					Pts.Reserve(Ribbon.St.Num());
					for (const FSimCruiseRibbonStation& St : Ribbon.St)
					{
						Pts.Add(FVector2f(ScreenOf(Origin + FVector2D(St.C.X, St.C.Y))));
					}
					const float W = FMath::Clamp(static_cast<float>(Ribbon.St[0].HalfW * 2.0 * Scale * 0.7), 1.2f, 6.f);
					FSlateDrawElement::MakeLines(OutDrawElements, RoadLayer, AllottedGeometry.ToPaintGeometry(), MoveTemp(Pts), ESlateDrawEffect::None, FLinearColor(FColor(0xa4, 0xac, 0x91)), true, W);
				}
			}
		}
	}

	// Labels, most important first, never overlapping (RoadMap.gd _draw).
	const int32 TextLayer = RoadLayer + 1;
	Spots.Sort([](const FSpot& A, const FSpot& B) { return A.Priority < B.Priority; });
	TArray<FSlateRect> Occupied;
	const FVector2D Car = ScreenOf(FVector2D(View.CarCm.X, View.CarCm.Y) / 100.0);
	Occupied.Add(FSlateRect(FVector2f(Car - FVector2D(74, 24)), FVector2f(Car + FVector2D(74, 26))));
	TSet<FString> Used;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	for (const FSpot& Spot : Spots)
	{
		if (Used.Contains(Spot.Text) || Occupied.Num() > 65) continue;
		FSlateFontInfo LabelFont = Font(Spot.bBold ? 13 : 11, Spot.bBold);
		LabelFont.OutlineSettings.OutlineSize = 2;
		LabelFont.OutlineSettings.OutlineColor = FLinearColor(FColor(0x1c, 0x29, 0x25));
		const FVector2D TextSize = Measure->Measure(Spot.Text, LabelFont);
		const FSlateRect Rect(FVector2f(Spot.At - FVector2D(TextSize.X * 0.5 + 5, TextSize.Y)), FVector2f(Spot.At + FVector2D(TextSize.X * 0.5 + 5, 5)));
		bool bOverlap = false;
		for (const FSlateRect& Other : Occupied)
		{
			if (FSlateRect::DoRectanglesIntersect(Other, Rect)) { bOverlap = true; break; }
		}
		if (bOverlap) continue;
		Occupied.Add(Rect);
		Used.Add(Spot.Text);
		FSlateDrawElement::MakeText(OutDrawElements, TextLayer,
			AllottedGeometry.ToPaintGeometry(FVector2f(TextSize), FSlateLayoutTransform(FVector2f(Spot.At - FVector2D(TextSize.X * 0.5, TextSize.Y)))),
			Spot.Text, LabelFont, ESlateDrawEffect::None, FLinearColor(FColor(0xe0, 0xe6, 0xd7)));
	}

	// The car: a lime arrow on a dark disc, and "You".
	const int32 MarkLayer = TextLayer + 1;
	if (View.bHasCar)
	{
		const double Yaw = FMath::DegreesToRadians(View.CarYawDeg);
		const FVector2D Dir(FMath::Cos(Yaw), FMath::Sin(Yaw));
		const FVector2D Right(-Dir.Y, Dir.X);
		const FSlateRoundedBoxBrush Disc(FLinearColor(FColor(0x17, 0x38, 0x24)), 14.f);
		FSlateDrawElement::MakeBox(OutDrawElements, MarkLayer, AllottedGeometry.ToPaintGeometry(FVector2f(28.f, 28.f), FSlateLayoutTransform(FVector2f(Car - FVector2D(14, 14)))), &Disc);
		TArray<FVector2f> Arrow = { FVector2f(Car + Dir * 11), FVector2f(Car - Dir * 7 + Right * 6), FVector2f(Car - Dir * 4), FVector2f(Car - Dir * 7 - Right * 6), FVector2f(Car + Dir * 11) };
		FSlateDrawElement::MakeLines(OutDrawElements, MarkLayer + 1, AllottedGeometry.ToPaintGeometry(), MoveTemp(Arrow), ESlateDrawEffect::None, SimCruiseUiStyle::Lime(), true, 3.f);
		FSlateFontInfo YouFont = Font(14, true);
		YouFont.OutlineSettings.OutlineSize = 2;
		YouFont.OutlineSettings.OutlineColor = SimCruiseUiStyle::Ink();
		FSlateDrawElement::MakeText(OutDrawElements, MarkLayer + 1, AllottedGeometry.ToPaintGeometry(FVector2f(60.f, 20.f), FSlateLayoutTransform(FVector2f(Car + FVector2D(18, -9)))),
			FString(TEXT("You")), YouFont, ESlateDrawEffect::None, FLinearColor::White);
	}
	// The spot a drive goes to: the pin, or the crosshair at the centre.
	const FVector2D Spot = bPinned ? ScreenOf(PinM) : SizePx * 0.5;
	const FLinearColor Amber(FColor(0xed, 0xb6, 0x6e));
	if (bPinned)
	{
		const FSlateRoundedBoxBrush Pin(Amber, 7.f);
		FSlateDrawElement::MakeBox(OutDrawElements, MarkLayer + 1, AllottedGeometry.ToPaintGeometry(FVector2f(14.f, 14.f), FSlateLayoutTransform(FVector2f(Spot - FVector2D(7, 7)))), &Pin);
	}
	else if (!View.bHasCar || FVector2D::Distance(Car, Spot) > 3.0)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, MarkLayer + 1, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Spot - FVector2D(12, 0)), FVector2f(Spot + FVector2D(12, 0)) }, ESlateDrawEffect::None, Amber, true, 2.f);
		FSlateDrawElement::MakeLines(OutDrawElements, MarkLayer + 1, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Spot - FVector2D(0, 12)), FVector2f(Spot + FVector2D(0, 12)) }, ESlateDrawEffect::None, Amber, true, 2.f);
	}

	// Scale bar and credit.
	double ScaleM = 10.0;
	while (ScaleM * Scale < 55.0) ScaleM *= 2.0;
	const FVector2D Bar(24.0, SizePx.Y - 28.0);
	FSlateDrawElement::MakeLines(OutDrawElements, MarkLayer, AllottedGeometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(Bar), FVector2f(Bar + FVector2D(ScaleM * Scale, 0)) }, ESlateDrawEffect::None, FLinearColor::White, true, 2.f);
	const FString BarText = ScaleM >= 400.0 ? FString::Printf(TEXT("%.1f mi"), ScaleM / 1609.344) : FString::Printf(TEXT("%d ft"), FMath::RoundToInt(ScaleM * 3.28084));
	FSlateDrawElement::MakeText(OutDrawElements, MarkLayer, AllottedGeometry.ToPaintGeometry(FVector2f(120.f, 18.f), FSlateLayoutTransform(FVector2f(Bar - FVector2D(0, 22)))), BarText, Font(12), ESlateDrawEffect::None, FLinearColor::White);
	FSlateDrawElement::MakeText(OutDrawElements, MarkLayer, AllottedGeometry.ToPaintGeometry(FVector2f(300.f, 18.f), FSlateLayoutTransform(FVector2f(SizePx.X - 290.0, SizePx.Y - 26.0))),
		FString(TEXT("N ↑    © OpenStreetMap contributors")), Font(12), ESlateDrawEffect::None, SimCruiseUiStyle::Dim());

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, MarkLayer + 2, InWidgetStyle, bParentEnabled);
}

FReply SSimCruiseRoadAtlas::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (IsCloseKey(Key) || Key == EKeys::Tab)
	{
		OnClose.ExecuteIfBound();
		return FReply::Handled();
	}
	if (Key == EKeys::Equals || Key == EKeys::Add || Key == EKeys::Gamepad_DPad_Up) { Zoom(-1); return FReply::Handled(); }
	if (Key == EKeys::Hyphen || Key == EKeys::Subtract || Key == EKeys::Gamepad_DPad_Down) { Zoom(1); return FReply::Handled(); }
	if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom) { DriveToPin(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Top || Key == EKeys::Home) { Recenter(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_FaceButton_Left || Key == EKeys::Slash || Key == EKeys::F) { OnFindPlace.ExecuteIfBound(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_RightShoulder) { OnCycleCar.ExecuteIfBound(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_LeftShoulder) { OnToggleTransmission.ExecuteIfBound(); return FReply::Handled(); }
	// Keep the arrows and the d-pad's sides from moving focus to the buttons.
	if (Key == EKeys::Left || Key == EKeys::Right || Key == EKeys::Up || Key == EKeys::Down)
	{
		const FVector2D Step = FVector2D(Key == EKeys::Left ? -1 : Key == EKeys::Right ? 1 : 0, Key == EKeys::Up ? -1 : Key == EKeys::Down ? 1 : 0);
		CenterM += Step * 120.0 / Scales()[Level];
		bPinned = false;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SSimCruiseRoadAtlas::OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent)
{
	const FKey Key = InAnalogInputEvent.GetKey();
	if (Key == EKeys::Gamepad_LeftX) { Stick.X = InAnalogInputEvent.GetAnalogValue(); return FReply::Handled(); }
	if (Key == EKeys::Gamepad_LeftY) { Stick.Y = InAnalogInputEvent.GetAnalogValue(); return FReply::Handled(); }
	return FReply::Unhandled();
}

FReply SSimCruiseRoadAtlas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return FReply::Unhandled();
	const FVector2D Local = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (Local.Y < 100.0) return FReply::Unhandled();
	bDragging = true;
	bMoved = false;
	PressAt = Local;
	LastMouse = Local;
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SSimCruiseRoadAtlas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging) return FReply::Unhandled();
	bDragging = false;
	if (!bMoved)
	{
		PinM = WorldOfScreen(FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition())));
		bPinned = true;
	}
	return FReply::Handled().ReleaseMouseCapture();
}

FReply SSimCruiseRoadAtlas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging) return FReply::Unhandled();
	const FVector2D Local = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	if (FVector2D::Distance(PressAt, Local) > 3.0) bMoved = true;
	if (bMoved) CenterM -= (Local - LastMouse) / Scales()[Level];
	LastMouse = Local;
	return FReply::Handled();
}

FReply SSimCruiseRoadAtlas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D Local = FVector2D(MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
	Zoom(MouseEvent.GetWheelDelta() > 0.f ? -1 : 1, &Local);
	return FReply::Handled();
}
