// Copyright SimCruise contributors. All Rights Reserved.

// The pause menu and the GPS screens of ASimCruisePlayerController (2.0.2). The
// rows call the controller's own settings, with the same console variables and
// preference keys as the road atlas's settings.

#include "Gameplay/SimCruisePlayerController.h"

#include "Engine/World.h"
#include "Gameplay/SimCruiseAtlasUI.h"
#include "Gameplay/SimCruiseDriveHud.h"
#include "Gameplay/SimCruiseFamousRoads.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruisePauseMenu.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePreferences.h"
#include "Gameplay/SimCruiseRoutePlanner.h"
#include "Gameplay/SimCruiseRouteSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Live/SimCruiseLiveWorldSubsystem.h"
#include "Vehicle/SimCruiseVehicle.h"

namespace
{
	/** The driving update's traffic switch (ChangeSetting 13) and seat menu (OpenSeatMenu) are on main. */
	constexpr bool bTrafficSettingReady() { return true; }
	constexpr bool bSeatMenuReady() { return true; }

	float MenuVar(const TCHAR* Name, float Default = 1.f)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetFloat() : Default;
	}

	/** One step up or down a ladder of values (no wrap), remembered under the atlas's key. */
	void StepLadder(const TCHAR* Name, const TArray<float>& Ladder, int32 Dir, float Stock, const TCHAR* Key, float PrefScale)
	{
		const float Now = MenuVar(Name);
		int32 Index = 0;
		for (int32 i = 0; i < Ladder.Num(); ++i) if (FMath::Abs(Ladder[i] - Now) < FMath::Abs(Ladder[Index] - Now)) Index = i;
		float Next = Ladder[FMath::Clamp(Index + (Dir > 0 ? 1 : Dir < 0 ? -1 : 0), 0, Ladder.Num() - 1)];
		if (Dir == 2 && Stock >= 0.f) Next = Stock;
		if (Dir == 0) Next = Ladder[(Index + 1) % Ladder.Num()];
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name)) Var->Set(Next, ECVF_SetByCode);
		SimCruisePreferences::SetInt(Key, FMath::RoundToInt(Next * PrefScale));
	}

	float Along(const TArray<float>& Ladder, float Value)
	{
		return Ladder.Num() > 1 ? FMath::Clamp((Value - Ladder[0]) / (Ladder.Last() - Ladder[0]), 0.f, 1.f) : 0.f;
	}

	const TArray<float>& HeadLadder() { static const TArray<float> L = { 0.f, 0.25f, 0.5f, 0.75f, 1.f }; return L; }
	const TArray<float>& RumbleLadder() { static const TArray<float> L = { 0.f, 0.3f, 0.6f, 1.f }; return L; }
	const TArray<float>& SteerLadder() { static const TArray<float> L = { 0.6f, 0.8f, 1.f, 1.25f, 1.5f }; return L; }
	const TArray<float>& DeadLadder() { static const TArray<float> L = { 0.f, 0.05f, 0.1f, 0.15f }; return L; }
	const TArray<float>& ScreenLadder() { static const TArray<float> L = { 13.3f, 14.f, 15.6f, 17.3f, 24.f, 27.f, 32.f, 43.f, 55.f, 65.f }; return L; }
	const TArray<float>& EyeLadder() { static const TArray<float> L = { 40.f, 50.f, 60.f, 70.f, 80.f, 100.f, 150.f, 200.f, 250.f }; return L; }

	float LadderFraction(const TArray<float>& Ladder, float Value)
	{
		// Even steps along the track, whatever the values.
		int32 Index = 0;
		for (int32 i = 0; i < Ladder.Num(); ++i) if (FMath::Abs(Ladder[i] - Value) < FMath::Abs(Ladder[Index] - Value)) Index = i;
		return Ladder.Num() > 1 ? static_cast<float>(Index) / (Ladder.Num() - 1) : 0.f;
	}
}

FSimCruiseMenuState ASimCruisePlayerController::MenuState() const
{
	FSimCruiseMenuState State;
	State.bPad = bUsingGamepad;
	const FSimCruiseAtlasView View = AtlasView();
	const ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
	const FSimCruisePlace Place = Mode ? Mode->GetPlace() : FSimCruisePlace();
	// "Ocean Drive, Miami Beach · Art Deco District": the name before the dot, the rest after it.
	FString Name = Place.Title, Rest;
	if (!Place.Title.Split(TEXT(" · "), &Name, &Rest)) Rest.Reset();
	State.Place = Name;
	double Lon = Place.Lon, Lat = Place.Lat;
	if (View.bHasCar && View.Frame.IsValid()) View.Frame.WorldCmToLonLat(View.CarCm, Lon, Lat);
	FString Where = SimCruiseRouteText::Clock(FSimCruiseDriveHud::LocalTime(GetWorld(), Lon));
	if (const USimCruiseLiveWorldSubsystem* Live = USimCruiseLiveWorldSubsystem::Get(GetWorld()))
	{
		if (Live->GetWeather().bValid) Where += FString::Printf(TEXT(", %d F"), FMath::RoundToInt(Live->GetWeather().TemperatureC * 9.f / 5.f + 32.f));
	}
	State.Where = Rest.IsEmpty() ? Where : Rest + TEXT(", ") + Where;

	auto Set = [&State](ESimCruiseMenuRow Row, const FString& Text, const FString& Unit = FString(), float Fraction = 0.f, float Stock = -1.f)
	{
		FSimCruiseMenuValue& V = State.Values.Add(Row);
		V.Text = Text; V.Unit = Unit; V.Fraction = Fraction; V.Stock = Stock;
		return &V;
	};
	// The atlas's settings text is "Name · Value".
	auto AfterDot = [&View](int32 Index) { FString L, R; return View.Settings.IsValidIndex(Index) && View.Settings[Index].Split(TEXT(" · "), &L, &R) ? R : FString(); };

	const float Cars = MenuVar(TEXT("simcruise.Traffic.Cars"), 20.f);
	// The driving update brings the traffic switch (ChangeSetting 13); until then the row says so.
	Set(ESimCruiseMenuRow::Traffic, Cars < 0.5f ? TEXT("Off") : Cars < 12.f ? TEXT("Light") : TEXT("Normal"))->bUnavailable = !bTrafficSettingReady();
	Set(ESimCruiseMenuRow::Car, View.CarName);
	Set(ESimCruiseMenuRow::Transmission, View.Transmission);
	Set(ESimCruiseMenuRow::Lights, bLightsOn ? TEXT("On") : TEXT("Auto"));
	const ASimCruisePlayerCameraManager* Camera = Cast<ASimCruisePlayerCameraManager>(PlayerCameraManager);
	FSimCruiseMenuValue* Lens = Set(ESimCruiseMenuRow::Lens, AfterDot(4));
	Lens->Chosen = !Camera ? 0 : Camera->GetLens() == ESimCruiseLens::Screen ? 1 : Camera->GetLens() == ESimCruiseLens::Wide ? 2 : 0;
	const float Head = MenuVar(TEXT("simcruise.Camera.HeadMotion"));
	Set(ESimCruiseMenuRow::HeadMotion, FString::FromInt(FMath::RoundToInt(Head * 100.f)), TEXT("%"), Along(HeadLadder(), Head), 1.f);
	const float Screen = MenuVar(TEXT("simcruise.Lens.ScreenInches"), 15.6f);
	Set(ESimCruiseMenuRow::ScreenSize, FString::Printf(TEXT("%.1f"), Screen), TEXT("in"), LadderFraction(ScreenLadder(), Screen));
	const float Eye = MenuVar(TEXT("simcruise.Lens.ViewDistanceCm"), 60.f);
	Set(ESimCruiseMenuRow::EyeDistance, FString::FromInt(FMath::RoundToInt(Eye)), TEXT("cm"), LadderFraction(EyeLadder(), Eye));
	Set(ESimCruiseMenuRow::Seat, FString())->bUnavailable = !bSeatMenuReady();
	Set(ESimCruiseMenuRow::Sound, bSoundOn ? TEXT("On") : TEXT("Off"));
	Set(ESimCruiseMenuRow::HudLevel, AfterDot(1));
	const float Rumble = MenuVar(TEXT("simcruise.Rumble.Strength"));
	Set(ESimCruiseMenuRow::Rumble, FString::FromInt(FMath::RoundToInt(Rumble * 100.f)), TEXT("%"), Along(RumbleLadder(), Rumble), 1.f);
	const float Steer = MenuVar(TEXT("simcruise.Pad.SteerSpeed"));
	Set(ESimCruiseMenuRow::StickSpeed, FString::FromInt(FMath::RoundToInt(Steer * 100.f)), TEXT("%"), Along(SteerLadder(), Steer), Along(SteerLadder(), 1.f));
	Set(ESimCruiseMenuRow::StickResponse, AfterDot(8));
	const float Dead = MenuVar(TEXT("simcruise.Pad.SteerDeadZone"), 0.f);
	Set(ESimCruiseMenuRow::StickDeadZone, FString::FromInt(FMath::RoundToInt(100.f * (0.12f + 0.88f * Dead))), TEXT("%"), Along(DeadLadder(), Dead), 0.f);
	Set(ESimCruiseMenuRow::Trigger, AfterDot(10));

	if (const USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(GetWorld()); Gps && Gps->HasRoute())
	{
		const FSimCruiseRoute& Route = Gps->GetRoute();
		State.bRoute = true;
		const TArray<FSimCruiseRouteStop>& Stops = Route.GetStops();
		const FString First = Stops.Num() > 1 ? Stops[1].Title : FString();
		State.RouteTitle = Stops.Num() > 2 ? FString::Printf(TEXT("%s to %s"), *First, *Stops.Last().Title) : FString::Printf(TEXT("To %s"), *First);
		double Left = Route.GetProgress().LegRemainingM, LeftS = Route.GetProgress().LegRemainingS;
		for (int32 i = Route.GetActiveLeg() + 1; i < Route.GetLegs().Num(); ++i) { Left += Route.GetLegs()[i].LengthM; LeftS += Route.GetLegs()[i].DurationS; }
		const int32 StopsLeft = Stops.Num() - 1 - Route.GetActiveLeg();
		State.RouteSummary = FString::Printf(TEXT("%s, %s, %d %s to go"), *SimCruiseRouteText::Distance(Left), *SimCruiseRouteText::Duration(LeftS), StopsLeft, StopsLeft == 1 ? TEXT("stop") : TEXT("stops"));
		for (int32 i = Route.GetActiveLeg(); i < Route.GetLegs().Num(); ++i)
		{
			State.RouteLegs.Add(Route.GetLegs()[i].M);
			if (Route.GetLegs()[i].M.Num()) State.RouteStops.Add(Route.GetLegs()[i].M.Last());
		}
		Gps->GetCarM(State.CarM);
	}
	return State;
}

void ASimCruisePlayerController::MenuAction(ESimCruiseMenuRow Row, int32 Dir)
{
	ASimCruiseGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<ASimCruiseGameMode>() : nullptr;
	USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(GetWorld());
	switch (Row)
	{
	case ESimCruiseMenuRow::Continue: CloseOverlays(); break;
	case ESimCruiseMenuRow::WhereTo: OpenWhereTo(); break;
	case ESimCruiseMenuRow::PlanRoute: OpenRoutePlanner(); break;
	case ESimCruiseMenuRow::FamousRoads: OpenFamousRoads(); break;
	case ESimCruiseMenuRow::Atlas: OpenRoadAtlas(); break;
	case ESimCruiseMenuRow::EndRoute:
		if (Gps) Gps->ClearRoute();
		ShowMessage(TEXT("Route ended"), 2.f);
		break;
	case ESimCruiseMenuRow::Traffic:
		if (bTrafficSettingReady()) ChangeSetting(13);
		else ShowMessage(TEXT("The traffic setting comes with the next driving update"), 3.f);
		break;
	case ESimCruiseMenuRow::Car: if (Mode) Mode->CycleCar(this); break;
	case ESimCruiseMenuRow::Transmission: OnToggleTransmission(); break;
	case ESimCruiseMenuRow::Lights: OnToggleLights(); break;
	case ESimCruiseMenuRow::Tune:
		if (FMath::Abs(Telemetry.BodyVelocityMps.X) > 1.0) ShowMessage(TEXT("Stop the car to tune it"), 2.f);
		else OpenTuning();
		break;
	case ESimCruiseMenuRow::Seat:
		OpenSeatMenu();
		break;
	case ESimCruiseMenuRow::Lens:
	{
		ASimCruisePlayerCameraManager* Camera = GetSimCamera();
		if (!Camera) break;
		const int32 Now = Camera->GetLens() == ESimCruiseLens::Screen ? 1 : Camera->GetLens() == ESimCruiseLens::Wide ? 2 : 0;
		const int32 Want = Dir < 0 ? FMath::Max(Now - 1, 0) : Dir > 0 ? FMath::Min(Now + 1, 2) : (Now + 1) % 3;
		// The camera cycles Natural, True FOV, Wide.
		for (int32 i = 0; i < 3; ++i)
		{
			const int32 Lens = Camera->GetLens() == ESimCruiseLens::Screen ? 1 : Camera->GetLens() == ESimCruiseLens::Wide ? 2 : 0;
			if (Lens == Want) break;
			Camera->CycleLens();
		}
		SimCruisePreferences::SetInt(TEXT("lens_v3"), static_cast<int32>(Camera->GetLens()));
		break;
	}
	case ESimCruiseMenuRow::HeadMotion: StepLadder(TEXT("simcruise.Camera.HeadMotion"), HeadLadder(), Dir, 1.f, TEXT("head_motion"), 100.f); break;
	case ESimCruiseMenuRow::ScreenSize: StepLadder(TEXT("simcruise.Lens.ScreenInches"), ScreenLadder(), Dir, -1.f, TEXT("screen_tenths_inch"), 10.f); break;
	case ESimCruiseMenuRow::EyeDistance: StepLadder(TEXT("simcruise.Lens.ViewDistanceCm"), EyeLadder(), Dir, -1.f, TEXT("view_distance_cm"), 1.f); break;
	case ESimCruiseMenuRow::Sound: OnToggleSound(); break;
	case ESimCruiseMenuRow::HudLevel: OnCycleHud(); break;
	case ESimCruiseMenuRow::Rumble: StepLadder(TEXT("simcruise.Rumble.Strength"), RumbleLadder(), Dir, 1.f, TEXT("rumble"), 100.f); break;
	case ESimCruiseMenuRow::StickSpeed: StepLadder(TEXT("simcruise.Pad.SteerSpeed"), SteerLadder(), Dir, 1.f, TEXT("pad_steer_speed"), 100.f); break;
	case ESimCruiseMenuRow::StickDeadZone: StepLadder(TEXT("simcruise.Pad.SteerDeadZone"), DeadLadder(), Dir, 0.f, TEXT("pad_steer_dead_zone"), 100.f); break;
	case ESimCruiseMenuRow::StickResponse: ChangeSetting(8); break;
	case ESimCruiseMenuRow::Trigger: ChangeSetting(10); break;
	default: break;
	}
}

void ASimCruisePlayerController::OpenPauseMenu(int32 Page)
{
	CloseOverlays();
	MenuPage = Page;
	PausePanel = SNew(SSimCruisePauseMenu)
		.Page(Page)
		.GetState_Lambda([this]() { return MenuState(); })
		.OnAction_Lambda([this](ESimCruiseMenuRow Row, int32 Dir) { MenuAction(Row, Dir); })
		.OnPage_Lambda([this](int32 P)
		{
			MenuPage = P;
			// Seat & view keeps the world running so the view can be judged.
			if (PausePanel.IsValid()) SetPause(P != 2);
		})
		.OnClose_Lambda([this]() { CloseOverlays(); });
	ShowOverlay(PausePanel.ToSharedRef(), PausePanel);
	SetPause(Page != 2);
}

void ASimCruisePlayerController::OpenRoutePlanner()
{
	CloseOverlays();
	PlannerPanel = SNew(SSimCruiseRoutePlanner)
		.GetView_Lambda([this]() { return AtlasView(); })
		.Gps(USimCruiseRouteSubsystem::Get(GetWorld()))
		.bPad(bUsingGamepad)
		.OnStart_Lambda([this]()
		{
			USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(GetWorld());
			if (!Gps || Gps->DraftStops.Num() == 0)
			{
				ShowMessage(TEXT("Add a stop first"), 2.f);
				return;
			}
			const TArray<FSimCruiseRouteStop> Stops = Gps->DraftStops;
			CloseOverlays();
			Gps->StartRoute(Stops);
		})
		.OnBack_Lambda([this]() { OpenPauseMenu(0); });
	ShowOverlay(PlannerPanel.ToSharedRef(), PlannerPanel);
}

void ASimCruisePlayerController::OpenFamousRoads()
{
	CloseOverlays();
	TArray<SSimCruiseListMenu::FItem> Items;
	for (const FSimCruiseFamousRoad& Road : SimCruiseFamousRoads::All())
	{
		SSimCruiseListMenu::FItem Item;
		Item.Icon = SimCruiseTheme::EIcon::Star;
		Item.Title = Road.Title;
		Item.Note = Road.Note;
		Item.Value = FString::Printf(TEXT("%.1f mi"), Road.LengthMi);
		Items.Add(Item);
	}
	TSharedRef<SSimCruiseListMenu> List = SNew(SSimCruiseListMenu)
		.Title(TEXT("Famous roads"))
		.Help(TEXT("Go to the start. The route to the end starts when the road is ready."))
		.Items(Items)
		.bPad(bUsingGamepad)
		.OnChoose_Lambda([this](int32 Index)
		{
			const TArray<FSimCruiseFamousRoad>& All = SimCruiseFamousRoads::All();
			if (!All.IsValidIndex(Index)) return;
			const FSimCruiseFamousRoad Road = All[Index];
			ChoosePlace(Road.Start);
			if (USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(GetWorld())) Gps->StartRouteAfterTravel(Road.Stops);
		})
		.OnBack_Lambda([this]() { OpenPauseMenu(0); });
	ListPanel = List;
	ShowOverlay(List, List);
}

void ASimCruisePlayerController::OpenWhereTo()
{
	CloseOverlays();
	SearchPanel = SNew(SSimCruisePlaceSearch)
		.OnChoose_Lambda([this](const FSimCruisePlace& Place)
		{
			// Go there now, or make it the GPS's stop.
			const FSimCruisePlace Found = Place;
			TArray<SSimCruiseListMenu::FItem> Items;
			SSimCruiseListMenu::FItem Go;
			Go.Icon = SimCruiseTheme::EIcon::Map;
			Go.Title = TEXT("Go there now");
			Go.Note = TEXT("Jump to the place and drive from there");
			Items.Add(Go);
			SSimCruiseListMenu::FItem Stop;
			Stop.Icon = SimCruiseTheme::EIcon::Route;
			Stop.Title = TEXT("Make it your stop");
			Stop.Note = TEXT("The GPS takes you there on the roads");
			Stop.bCourse = true;
			Items.Add(Stop);
			CloseOverlays();
			TSharedRef<SSimCruiseListMenu> Choice = SNew(SSimCruiseListMenu)
				.Title(Found.Title)
				.Help(FString::Printf(TEXT("%.5f, %.5f"), Found.Lat, Found.Lon))
				.Items(Items)
				.bPad(bUsingGamepad)
				.OnChoose_Lambda([this, Found](int32 Index)
				{
					if (Index == 0) { ChoosePlace(Found); return; }
					CloseOverlays();
					FSimCruiseRouteStop Stop;
					Stop.Title = Found.Title;
					Found.Title.Split(TEXT(","), &Stop.Title, nullptr);
					Stop.Lon = Found.Lon;
					Stop.Lat = Found.Lat;
					if (USimCruiseRouteSubsystem* Gps = USimCruiseRouteSubsystem::Get(GetWorld())) Gps->StartRoute({ Stop });
				})
				.OnBack_Lambda([this]() { OpenWhereTo(); });
			ListPanel = Choice;
			ShowOverlay(Choice, Choice);
		})
		.OnSurprise_Lambda([this]() { return SurpriseMe(); })
		.OnClose_Lambda([this]() { OpenPauseMenu(0); });
	ShowOverlay(SearchPanel.ToSharedRef(), SearchPanel->FocusTarget(bUsingGamepad));
}
