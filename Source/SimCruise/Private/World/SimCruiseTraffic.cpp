// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTraffic.h"

#include "Algo/BinarySearch.h"
#include "Audio/SimCruiseEngineSoundComponent.h"
#include "AudioDevice.h"
#include "Camera/PlayerCameraManager.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Presentation/SimCruiseCarAssets.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"

namespace
{
	TAutoConsoleVariable<int32> CVarTrafficCars(TEXT("simcruise.Traffic.Cars"), 20,
		TEXT("How many moving cars drive round the player's car (0: none)."));
	TAutoConsoleVariable<int32> CVarTrafficSound(TEXT("simcruise.Traffic.Sound"), 1,
		TEXT("1: traffic cars within 40 m are heard (their engines, in place); 0: silent traffic."));
	TAutoConsoleVariable<int32> CVarTrafficGridlockRules(TEXT("simcruise.Traffic.GridlockRules"), 1,
		TEXT("1: crossing cars give way to the right and break a gridlock after 6 s; 0: the old rule (every car waits for any car across its path), for comparison."));
	constexpr double VoiceRangeM = 40.0;

	constexpr double SpawnMinM = 60.0, SpawnMaxM = 260.0, RemoveM = 320.0;

	uint32 NextRandom(uint32& State)
	{
		State = State * 1664525u + 1013904223u;
		return State >> 8;
	}
	double Unit(uint32& State) { return static_cast<double>(NextRandom(State) & 0xFFFFFF) / 16777216.0; }

	double SignedAngleDeg(const FVector2d& A, const FVector2d& B)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(A.X * B.Y - A.Y * B.X, FVector2d::DotProduct(A, B)));
	}
}

// ---------------------------------------------------------------------------- model

double SimCruiseTrafficModel::IdmAcceleration(double V, double V0, double GapM, double ClosingMps)
{
	// Urban car parameters (Treiber and Kesting, Traffic Flow Dynamics, table 11.2).
	constexpr double A = 1.4, B = 2.0, S0 = 2.0, T = 1.4;
	double Accel = A * (1.0 - FMath::Pow(FMath::Max(V, 0.0) / FMath::Max(V0, 0.1), 4.0));
	if (GapM >= 0.0)
	{
		const double Desired = S0 + FMath::Max(0.0, V * T + V * ClosingMps / (2.0 * FMath::Sqrt(A * B)));
		Accel -= A * FMath::Square(Desired / FMath::Max(GapM, 0.1));
	}
	return FMath::Clamp(Accel, -9.0, A);
}

double SimCruiseTrafficModel::SpeedLimitMps(const FString& Class)
{
	// mph: motorway 65, trunk 55, primary 40, secondary 35, tertiary 30, residential 25.
	if (Class == TEXT("motorway")) return 29.1;
	if (Class == TEXT("trunk")) return 24.6;
	if (Class == TEXT("primary")) return 17.9;
	if (Class == TEXT("secondary")) return 15.6;
	if (Class == TEXT("tertiary")) return 13.4;
	if (Class == TEXT("living_street")) return 6.7;
	if (Class == TEXT("service")) return 4.5;
	return 11.2;
}

namespace
{
	/** Station I's parking, in the frame of a driver going Dir (Right is the driver's right). */
	SimCruiseLanes::FParking DriverParking(const FSimCruiseRibbon& Ribbon, int32 I, int32 Dir, bool& bOneWay)
	{
		const FSimCruiseRibbonStation& St = Ribbon.St[I];
		auto Kerbed = [&](int32 Side) { return Ribbon.Sides[Side].Edge.IsValidIndex(I) && Ribbon.Sides[Side].Edge[I] != ESimCruiseRoadEdge::Rural; };
		const SimCruiseLanes::FParking Park = SimCruiseLanes::Parking(Ribbon.Class, St.HalfW, St.Travel, Kerbed(0), Kerbed(1));
		bOneWay = St.Travel != 0;
		if (bOneWay) return Park; // the one-way layout reads the station frame itself
		SimCruiseLanes::FParking Driver;
		Driver.Right = Dir > 0 ? Park.Right : Park.Left;
		Driver.Left = Dir > 0 ? Park.Left : Park.Right;
		return Driver;
	}
}

double SimCruiseTrafficModel::LaneOffsetM(const FSimCruiseRibbon& Ribbon, int32 I, int32 Dir, int32 Lane)
{
	const FSimCruiseRibbonStation& St = Ribbon.St[I];
	bool bOneWay = false;
	const SimCruiseLanes::FParking Park = DriverParking(Ribbon, I, Dir, bOneWay);
	int32 Count = 1;
	double Width = 3.0;
	SimCruiseLanes::TravelLanes(Ribbon.Class, St.HalfW, St.Travel, Park, Count, Width);
	// Lane k from the right lies k widths to the driver's left; the driver's right is R * Dir
	// (on a one-way street Dir is its direction).
	const int32 K = FMath::Clamp(Lane, 0, Count - 1);
	const int32 Way = bOneWay ? St.Travel : Dir;
	return Way * (SimCruiseLanes::RightLaneCentreM(Ribbon.Class, St.HalfW, St.Travel, Park) - K * Width);
}

int32 SimCruiseTrafficModel::LaneCount(const FSimCruiseRibbon& Ribbon, int32 I, int32 Dir)
{
	bool bOneWay = false;
	const SimCruiseLanes::FParking Park = DriverParking(Ribbon, I, Dir, bOneWay);
	int32 Count = 1;
	double Width = 3.0;
	SimCruiseLanes::TravelLanes(Ribbon.Class, Ribbon.St[I].HalfW, Ribbon.St[I].Travel, Park, Count, Width);
	return FMath::Max(Count, 1);
}

double SimCruiseTrafficModel::NextDecisionS(const FSimCruiseRibbon& R, int32 Dir, double S, double SkipNearS, bool& bEnd)
{
	const int32 N = R.St.Num();
	auto IsCut = [&](int32 I)
	{
		return (R.Sides[0].Cut.IsValidIndex(I) && R.Sides[0].Cut[I]) || (R.Sides[1].Cut.IsValidIndex(I) && R.Sides[1].Cut[I]);
	};
	for (int32 K = 0; K < N; ++K)
	{
		const int32 I = Dir > 0 ? K : N - 1 - K;
		if (Dir * (R.St[I].S - S) <= 2.0 || !IsCut(I) || FMath::Abs(R.St[I].S - SkipNearS) < 15.0) continue;
		int32 J = I;
		while (J + Dir >= 0 && J + Dir < N && IsCut(J + Dir)) J += Dir;
		bEnd = false;
		return 0.5 * (R.St[I].S + R.St[J].S);
	}
	bEnd = true;
	return Dir > 0 ? R.St.Last().S : R.St[0].S;
}

// ---------------------------------------------------------------------------- car

ASimCruiseTrafficCar::ASimCruiseTrafficCar()
{
	PrimaryActorTick.bCanEverTick = false;
	Presentation = CreateDefaultSubobject<USimCruiseCarPresentationComponent>(TEXT("Presentation"));
	RootComponent = Presentation;
	Presentation->SetMobility(EComponentMobility::Movable);
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	Collision->SetupAttachment(Presentation);
	Collision->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Collision->SetMobility(EComponentMobility::Movable);
}

bool ASimCruiseTrafficCar::Setup(const FString& InCarId, const FLinearColor& Paint)
{
	CarId = InCarId;
	if (!Presentation->InitializeForCar(CarId))
	{
		return false;
	}
	Presentation->SetPaintColor(Paint);
	Presentation->SetWheelStateProvider(TScriptInterface<ISimCruiseWheelStateProvider>(this));
	FString Error;
	const TSharedPtr<FSimCruiseBuiltCar> Built = SimCruiseCarAssets::Get(CarId, Error);
	if (Built.IsValid() && Built->Body.Mesh.IsValid())
	{
		const FBox Body = Built->Body.Mesh->GetBoundingBox().TransformBy(Built->Body.PartToVehicle);
		Collision->SetRelativeLocation(Body.GetCenter());
		Collision->SetBoxExtent(Body.GetExtent());
		LengthM = Body.GetSize().X / 100.0;
		TyreRadiusCm = Built->TyreRadiusCm;
	}
	return true;
}

bool ASimCruiseTrafficCar::GetCarVisualState(FSimCruiseCarVisualState& OutState) const
{
	for (int32 W = 0; W < 4; ++W)
	{
		FSimCruiseWheelVisualState& Wheel = OutState.Wheels[W];
		Wheel.SpinAngleRad = static_cast<float>(Spin);
		Wheel.SteerAngleRad = W < 2 ? static_cast<float>(SteerRad) : 0.f;
	}
	OutState.bBrake = bBraking;
	OutState.bLamps = bLamps;
	return true;
}

// ---------------------------------------------------------------------------- traffic

ASimCruiseTraffic::ASimCruiseTraffic()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
}

void ASimCruiseTraffic::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearCars();
	Super::EndPlay(Reason);
}

void ASimCruiseTraffic::ClearCars()
{
	for (ASimCruiseTrafficCar* Car : Cars)
	{
		if (Car) Car->Destroy();
	}
	Cars.Reset();
	SinceStart = 0.0;
}

const FSimCruiseRibbon* ASimCruiseTraffic::GetRibbon(const FIntPoint& Tile, int32 Ribbon) const
{
	const ASimCruiseWorldTile* T = Streamer.IsValid() ? Streamer->GetTile(Tile) : nullptr;
	const FSimCruiseTileBuild* Build = T ? T->GetBuild() : nullptr;
	return Build && Build->Roads.Ribbons.IsValidIndex(Ribbon) && Build->Roads.Ribbons[Ribbon].St.Num() >= 2 ? &Build->Roads.Ribbons[Ribbon] : nullptr;
}

ASimCruiseTraffic::FPose ASimCruiseTraffic::PoseAt(const FIntPoint& Tile, int32 RibbonIndex, int32 Dir, double S, int32 Lane) const
{
	FPose Pose;
	const FSimCruiseRibbon* R = GetRibbon(Tile, RibbonIndex);
	if (!R) return Pose;
	const TArray<FSimCruiseRibbonStation>& St = R->St;
	S = FMath::Clamp(S, St[0].S, St.Last().S);
	int32 Lo = 0, Hi = St.Num() - 1;
	while (Hi - Lo > 1)
	{
		const int32 Mid = (Lo + Hi) / 2;
		(St[Mid].S <= S ? Lo : Hi) = Mid;
	}
	const double T = FMath::Clamp((S - St[Lo].S) / FMath::Max(St[Hi].S - St[Lo].S, 1e-6), 0.0, 1.0);
	const FVector2d C = FMath::Lerp(St[Lo].C, St[Hi].C, T);
	const FVector2d Rn = FMath::Lerp(St[Lo].R, St[Hi].R, T).GetSafeNormal();
	const FVector2d Tn = FMath::Lerp(St[Lo].T, St[Hi].T, T).GetSafeNormal();
	const double Offset = FMath::Lerp(SimCruiseTrafficModel::LaneOffsetM(*R, Lo, Dir, Lane), SimCruiseTrafficModel::LaneOffsetM(*R, Hi, Dir, Lane), T);
	const double H = FMath::Lerp(R->CarriagewayHeight(Lo, Offset), R->CarriagewayHeight(Hi, Offset), T);
	const FVector2d P = C + Rn * Offset;
	const FVector Origin = Streamer->GetTile(Tile)->GetActorLocation();
	Pose.LocationCm = Origin + FVector(P.X * 100.0, P.Y * 100.0, H * 100.0);
	Pose.Heading = Tn * static_cast<double>(Dir);
	Pose.bValid = true;
	return Pose;
}

bool ASimCruiseTraffic::ChooseAt(ASimCruiseTrafficCar& Car, double DecisionS, bool bAtEnd) const
{
	const FSimCruiseRibbon* R = GetRibbon(Car.Tile, Car.Ribbon);
	if (!R) return false;
	const FPose End = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, DecisionS);
	if (!End.bValid) return false;
	struct FOption { FIntPoint Tile; int32 Ribbon; int32 Dir; double S; double TurnDeg; double Weight; };
	TArray<FOption> Options;
	// At a junction along the ribbon, straight on is the likeliest way.
	if (!bAtEnd) Options.Add({ Car.Tile, Car.Ribbon, Car.Dir, DecisionS, 0.0, 3.0 });
	// What meets this end: the same street carrying on (a tile seam) or a crossing road.
	int32 CrossRank = MIN_int32;
	double CrossHalfW = 0.0;
	const ASimCruiseWorldTile* Here = Streamer->GetTile(Car.Tile);
	for (int32 Dx = -1; Dx <= 1; ++Dx)
	{
		for (int32 Dy = -1; Dy <= 1; ++Dy)
		{
			const FIntPoint TileId(Car.Tile.X + Dx, Car.Tile.Y + Dy);
			const ASimCruiseWorldTile* T = Streamer->GetTile(TileId);
			const FSimCruiseTileBuild* Build = T ? T->GetBuild() : nullptr;
			if (!Build || !Here) continue;
			const FVector Local3 = (End.LocationCm - T->GetActorLocation()) / 100.0;
			const FVector2d Local(Local3.X, Local3.Y);
			if (Local.X < -20.0 || Local.Y < -20.0 || Local.X > Build->TileSizeM + 20.0 || Local.Y > Build->TileSizeM + 20.0) continue;
			TArray<TPair<int32, int32>> Segments;
			Build->Roads.GatherSegments(Local, 14.0, Segments);
			TMap<int32, TPair<int32, double>> Best; // ribbon -> (segment, t) of the nearest approach
			TMap<int32, double> BestDist;
			for (const TPair<int32, int32>& Seg : Segments)
			{
				if (TileId == Car.Tile && Seg.Key == Car.Ribbon) continue;
				const FSimCruiseRibbon& Other = Build->Roads.Ribbons[Seg.Key];
				if (Other.bService || Other.Class == TEXT("service") || !Other.St.IsValidIndex(Seg.Value + 1)) continue;
				const FVector2d A = Other.St[Seg.Value].C, B = Other.St[Seg.Value + 1].C;
				const double Len2 = FMath::Max((B - A).SizeSquared(), 1e-9);
				const double Tt = FMath::Clamp(FVector2d::DotProduct(Local - A, B - A) / Len2, 0.0, 1.0);
				const double D = FVector2d::Distance(Local, FMath::Lerp(A, B, Tt));
				if (D > Other.St[Seg.Value].HalfW + 4.0) continue;
				if (!BestDist.Contains(Seg.Key) || D < BestDist[Seg.Key])
				{
					BestDist.Add(Seg.Key, D);
					Best.Add(Seg.Key, TPair<int32, double>(Seg.Value, Tt));
				}
			}
			for (const TPair<int32, TPair<int32, double>>& Hit : Best)
			{
				const FSimCruiseRibbon& Other = Build->Roads.Ribbons[Hit.Key];
				const int32 I = Hit.Value.Key;
				const double Tt = Hit.Value.Value;
				const double EntryS = FMath::Lerp(Other.St[I].S, Other.St[I + 1].S, Tt);
				const FVector2d Tn = FMath::Lerp(Other.St[I].T, Other.St[I + 1].T, Tt).GetSafeNormal();
				const bool bAtItsEnd = EntryS - Other.St[0].S < 2.0 || Other.St.Last().S - EntryS < 2.0;
				const bool bInLine = FMath::Abs(FVector2d::DotProduct(Tn, End.Heading)) > 0.94;
				if (!(bAtItsEnd && bInLine && Other.Name == R->Name))
				{
					CrossRank = FMath::Max(CrossRank, Other.Rank);
					CrossHalfW = FMath::Max(CrossHalfW, Other.St[I].HalfW);
				}
				for (const int32 Dir : { 1, -1 })
				{
					if (Other.St[I].Travel != 0 && Other.St[I].Travel != Dir) continue;
					const double Left = Dir > 0 ? Other.St.Last().S - EntryS : EntryS - Other.St[0].S;
					if (Left < 15.0) continue;
					const FVector2d Heading = Tn * static_cast<double>(Dir);
					const double Turn = SignedAngleDeg(End.Heading, Heading);
					if (FMath::Abs(Turn) > 135.0) continue; // no U-turns
					Options.Add({ TileId, Hit.Key, Dir, EntryS, Turn, FMath::Abs(Turn) < 30.0 ? 3.0 : 1.0 });
				}
			}
		}
	}
	if (Options.IsEmpty()) return false;
	// US practice at a junction without signals: the minor approach stops (a stop sign);
	// between equal minor streets every approach does (an all-way stop); the priority road
	// carries on.
	Car.StopState = CrossRank != MIN_int32 && SimCruiseLanes::ApproachStops(R->Rank, R->bMajor, CrossRank) ? 1 : 0;
	Car.DecisionS = DecisionS;
	Car.bDecisionAtEnd = bAtEnd;
	Car.JunctionCm = End.LocationCm;
	// The painted stop line: before the crosswalk, beyond the crossing carriageway's edge.
	Car.StopBackM = CrossHalfW + SimCruiseLanes::StopLineSetbackM;
	Car.WaitS = 0.0;
	double Total = 0.0;
	for (const FOption& O : Options) Total += O.Weight;
	uint32 Seed = Car.Seed;
	double Pick = Unit(Seed) * Total;
	Car.Seed = Seed;
	for (const FOption& O : Options)
	{
		Pick -= O.Weight;
		if (Pick <= 0.0)
		{
			Car.NextTile = O.Tile; Car.NextRibbon = O.Ribbon; Car.NextDir = O.Dir; Car.NextS = O.S; Car.NextTurnDeg = O.TurnDeg;
			Car.bNextStraight = O.Tile == Car.Tile && O.Ribbon == Car.Ribbon && O.Dir == Car.Dir;
			Car.bNextChosen = true;
			return true;
		}
	}
	const FOption& O = Options.Last();
	Car.NextTile = O.Tile; Car.NextRibbon = O.Ribbon; Car.NextDir = O.Dir; Car.NextS = O.S; Car.NextTurnDeg = O.TurnDeg;
	Car.bNextStraight = O.Tile == Car.Tile && O.Ribbon == Car.Ribbon && O.Dir == Car.Dir;
	Car.bNextChosen = true;
	return true;
}

void ASimCruiseTraffic::Advance(ASimCruiseTrafficCar& Car, double Dt, const FVector& PlayerCm, const FVector& PlayerVelCm)
{
	const FSimCruiseRibbon* R = GetRibbon(Car.Tile, Car.Ribbon);
	if (!R) { Car.Ribbon = INDEX_NONE; return; }
	const FPose Here = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.S, Car.Lane);
	const FVector2d Fwd = Here.Heading;
	// The car ahead in this lane (traffic or the player): bumper-to-bumper gap and closing speed.
	double Gap = -1.0, Closing = 0.0;
	int32 Why = 0, Source = 1; FVector BlockerCm = FVector::ZeroVector;
	auto Consider = [&](const FVector& OtherCm, const FVector2d& OtherVel, double OtherLength)
	{
		const FVector2d D((OtherCm.X - Here.LocationCm.X) / 100.0, (OtherCm.Y - Here.LocationCm.Y) / 100.0);
		const double Along = FVector2d::DotProduct(D, Fwd);
		const double Across = FMath::Abs(D.X * Fwd.Y - D.Y * Fwd.X);
		if (Along <= 0.0 || Along > 80.0 || Across > 1.9) return;
		if (CVarTrafficGridlockRules.GetValueOnGameThread() != 0 && FMath::Abs(OtherCm.Z - Here.LocationCm.Z) > 300.0) return;
		const double G = Along - 0.5 * (Car.LengthM + OtherLength);
		if (Gap < 0.0 || G < Gap)
		{
			Gap = FMath::Max(G, 0.05);
			Closing = Car.Speed - FVector2d::DotProduct(OtherVel, Fwd);
			Why = Source; BlockerCm = OtherCm;
		}
	};
	for (const ASimCruiseTrafficCar* Other : Cars)
	{
		if (!Other || Other == &Car || Other->Ribbon == INDEX_NONE) continue;
		const FVector Loc = Other->GetActorLocation();
		const FVector2d Heading(FMath::Cos(FMath::DegreesToRadians(Other->GetActorRotation().Yaw)), FMath::Sin(FMath::DegreesToRadians(Other->GetActorRotation().Yaw)));
		Consider(Loc, Heading * Other->Speed, Other->LengthM);
	}
	Source = 2;
	Consider(PlayerCm, FVector2d(PlayerVelCm.X, PlayerVelCm.Y) / 100.0, 4.2);

	// Anything crossing the path just ahead (a car turning or crossing at a junction): brake
	// as for a stopped car where it is.
	const double Probe = FMath::Clamp(Car.Speed * 2.5, 8.0, 25.0);
	const FVector2d HereM(Here.LocationCm.X / 100.0, Here.LocationCm.Y / 100.0);
	// Distance ahead along a path from From (heading H, probe length L) at which Target lies
	// within 3 m of it, or -1.
	auto InPath = [](const FVector2d& From, const FVector2d& H, double L, const FVector2d& Target)
	{
		for (double D = 4.0; D <= L; D += 4.0)
		{
			if (FVector2d::Distance(From + H * D, Target) < 3.0) return D;
		}
		return -1.0;
	};
	// Only a car at an angle crosses the path: one going the same way or coming the other way
	// is in a lane, and a car in this lane is already the car ahead. Two cars that each stand
	// in the other's path would wait for each other for ever: as at an uncontrolled junction,
	// the one with the other on its right gives way and the other goes. A car stood still for
	// 6 s no longer waits for a crossing car that is itself standing (a gridlock breaks the way
	// drivers break one, by creeping through).
	auto Crossing = [&](const FVector& OtherCm, const FVector2d& OtherHeading, double OtherSpeed, double OtherProbe, const ASimCruiseTrafficCar* OtherCar)
	{
		const bool bRules = CVarTrafficGridlockRules.GetValueOnGameThread() != 0;
		const double Along = FVector2d::DotProduct(OtherHeading, Fwd);
		if (bRules ? FMath::Abs(Along) > 0.7 : Along > 0.5) return;
		// A car on a bridge over this road, or on the road under it, is not in the way.
		if (bRules && FMath::Abs(OtherCm.Z - Here.LocationCm.Z) > 300.0) return;
		const FVector2d OtherM(OtherCm.X / 100.0, OtherCm.Y / 100.0);
		const double D = InPath(HereM, Fwd, Probe, OtherM);
		if (D < 0.0) return;
		if (!bRules)
		{
			const double G = FMath::Max(D - 0.5 * Car.LengthM - 2.0, 0.05);
			if (Gap < 0.0 || G < Gap) { Gap = G; Closing = Car.Speed; Why = 3; BlockerCm = OtherCm; }
			return;
		}
		if (OtherProbe > 0.0 && InPath(OtherM, OtherHeading, OtherProbe, HereM) >= 0.0)
		{
			const FVector2d Right(-Fwd.Y, Fwd.X);
			if (FVector2d::DotProduct(OtherM - HereM, Right) < 0.0) return;
		}
		// Three or four cars round a junction can each wait for the next: after 6 s the one that
		// has stood longest (ties by identity) creeps through, and the others wait for it.
		if (Car.StandS > 6.0 && OtherSpeed < 0.3 && OtherCar &&
			(OtherCar->StandS < Car.StandS - 1e-3 || (FMath::IsNearlyEqual(OtherCar->StandS, Car.StandS, 1e-3) && OtherCar > &Car))) return;
		const double G = FMath::Max(D - 0.5 * Car.LengthM - 2.0, 0.05);
		if (Gap < 0.0 || G < Gap) { Gap = G; Closing = Car.Speed; Why = 3; BlockerCm = OtherCm; }
	};
	for (const ASimCruiseTrafficCar* Other : Cars)
	{
		// A car waiting behind its stop line is not in the way.
		if (!Other || Other == &Car || Other->Ribbon == INDEX_NONE || Other->StopState == 2) continue;
		const double Yaw = FMath::DegreesToRadians(Other->GetActorRotation().Yaw);
		Crossing(Other->GetActorLocation(), FVector2d(FMath::Cos(Yaw), FMath::Sin(Yaw)), Other->Speed, FMath::Clamp(Other->Speed * 2.5, 8.0, 25.0), Other);
	}
	const FVector2d PlayerHeading = FVector2d(PlayerVelCm.X, PlayerVelCm.Y).GetSafeNormal();
	// The player never gives way to traffic by rule here: traffic always waits for the player.
	if (!PlayerHeading.IsNearlyZero()) Crossing(PlayerCm, PlayerHeading, PlayerVelCm.Size2D() / 100.0, -1.0, nullptr);

	// Speed: the class's limit, and slower into a turn (about 0.25 g round a corner).
	double V0 = Car.DesiredSpeed;
	const double EndS = Car.Dir > 0 ? R->St.Last().S : R->St[0].S;
	if (!Car.bNextChosen)
	{
		bool bEnd = true;
		const double Next = SimCruiseTrafficModel::NextDecisionS(*R, Car.Dir, Car.S, Car.LastDecisionS, bEnd);
		if (FMath::Abs(Next - Car.S) < 45.0) ChooseAt(Car, Next, bEnd);
	}
	const double Remaining = Car.bNextChosen ? FMath::Abs(Car.DecisionS - Car.S) : FMath::Abs(EndS - Car.S);
	if (Car.bNextChosen && FMath::Abs(Car.NextTurnDeg) > 30.0)
	{
		const double TurnSpeed = FMath::Lerp(7.0, 4.5, FMath::Clamp((FMath::Abs(Car.NextTurnDeg) - 30.0) / 60.0, 0.0, 1.0));
		// Brake gently to the turn speed over the last stretch.
		V0 = FMath::Min(V0, FMath::Sqrt(TurnSpeed * TurnSpeed + 2.0 * 1.5 * FMath::Max(Remaining - 5.0, 0.0)));
	}
	// A stop: the stop line is a stopped car to the IDM; stopped, wait at least a second
	// and until the junction is clear and nothing on the other roads is about to arrive.
	if (Car.StopState == 1 || Car.StopState == 2)
	{
		const double ToLine = Remaining - Car.StopBackM;
		if (ToLine < 60.0)
		{
			// The stopped car the IDM keeps 2 m (its jam gap) behind stands 2 m past the line:
			// the front bumper stops at the line.
			const double G = FMath::Max(ToLine - 0.5 * Car.LengthM + 2.0, 0.05);
			if (Gap < 0.0 || G < Gap) { Gap = G; Closing = Car.Speed; Why = 4; }
		}
		if (Car.StopState == 1 && Car.Speed < 0.3 && ToLine < 0.5 * Car.LengthM + 1.5)
		{
			Car.StopState = 2;
			Car.WaitS = 0.0;
		}
		if (Car.StopState == 2)
		{
			Car.WaitS += Dt;
			bool bClear = Car.WaitS > 1.2;
			auto Blocks = [&](const FVector& OtherCm, const FVector2d& OtherHeading, double OtherSpeed, const ASimCruiseTrafficCar* OtherCar)
			{
				const FVector2d ToJ((Car.JunctionCm.X - OtherCm.X) / 100.0, (Car.JunctionCm.Y - OtherCm.Y) / 100.0);
				const double Dist = ToJ.Size();
				// Another car waiting at its own stop line: all-way stop order, first to stop goes first.
				if (OtherCar && OtherCar->StopState == 2)
				{
					// After 10 s a waiting car stops deferring to others that wait too (they wait on something else).
					return Dist < 25.0 && (Car.WaitS < 10.0 || CVarTrafficGridlockRules.GetValueOnGameThread() == 0) && (OtherCar->WaitS > Car.WaitS + 1e-3 || (FMath::IsNearlyEqual(OtherCar->WaitS, Car.WaitS, 1e-3) && OtherCar < &Car));
				}
				// Moving through the junction (not alongside in this lane).
				if (Dist < 10.0 && OtherSpeed > 0.5 && FVector2d::DotProduct(OtherHeading, Fwd) < 0.9) return true;
				// About to arrive on a road that does not stop (within 5 s).
				const bool bTowards = FVector2d::DotProduct(OtherHeading, ToJ) > 0.0;
				const bool bWillStop = OtherCar && OtherCar->StopState == 1;
				return bTowards && !bWillStop && OtherSpeed > 1.0 && Dist > 2.0 && Dist / OtherSpeed < 5.0;
			};
			for (const ASimCruiseTrafficCar* Other : Cars)
			{
				if (!bClear) break;
				if (!Other || Other == &Car || Other->Ribbon == INDEX_NONE) continue;
				const double Yaw = FMath::DegreesToRadians(Other->GetActorRotation().Yaw);
				if (Blocks(Other->GetActorLocation(), FVector2d(FMath::Cos(Yaw), FMath::Sin(Yaw)), Other->Speed, Other)) bClear = false;
			}
			if (bClear && Blocks(PlayerCm, PlayerHeading, PlayerVelCm.Size2D() / 100.0, nullptr)) bClear = false;
			if (bClear) { Car.StopState = 3; ++StopsPassed; }
		}
	}
	// A left turn gives way to oncoming traffic (traffic going straight or turning right has
	// the right of way): hold just short of the junction's centre while anything coming the
	// other way will reach it within 4.5 s.
	const bool bLeftTurn = Car.bNextChosen && Car.NextTurnDeg < -30.0 && Car.StopState != 1 && Car.StopState != 2;
	if (bLeftTurn && Remaining < 25.0)
	{
		const FPose Centre = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.DecisionS, Car.Lane);
		auto Oncoming = [&](const FVector& OtherCm, const FVector2d& OtherHeading, double OtherSpeed)
		{
			if (FVector2d::DotProduct(OtherHeading, Fwd) > -0.7 || OtherSpeed < 1.0) return false;
			const FVector2d ToJ((Centre.LocationCm.X - OtherCm.X) / 100.0, (Centre.LocationCm.Y - OtherCm.Y) / 100.0);
			const double Ahead = FVector2d::DotProduct(ToJ, OtherHeading);
			return Ahead > -2.0 && Ahead < 60.0 && Ahead / OtherSpeed < 4.5;
		};
		bool bYield = false;
		for (const ASimCruiseTrafficCar* Other : Cars)
		{
			if (bYield) break;
			if (!Other || Other == &Car || Other->Ribbon == INDEX_NONE) continue;
			const double Yaw = FMath::DegreesToRadians(Other->GetActorRotation().Yaw);
			bYield = Oncoming(Other->GetActorLocation(), FVector2d(FMath::Cos(Yaw), FMath::Sin(Yaw)), Other->Speed);
		}
		if (!bYield) bYield = Oncoming(PlayerCm, PlayerHeading, PlayerVelCm.Size2D() / 100.0);
		if (bYield)
		{
			// The stopped car the IDM keeps 2 m behind stands at the junction's centre: the
			// front bumper waits 2 m short of it.
			const double G = FMath::Max(Remaining - 0.5 * Car.LengthM, 0.05);
			if (Gap < 0.0 || G < Gap) { Gap = G; Closing = Car.Speed; Why = 5; }
			if (!Car.bYielding) ++LeftTurnYields;
		}
		Car.bYielding = bYield;
	}
	else
	{
		Car.bYielding = false;
	}
	Car.Why = Why; Car.BlockerCm = BlockerCm;
	const double Accel = SimCruiseTrafficModel::IdmAcceleration(Car.Speed, V0, Gap, Closing);
	Car.Accel = Accel;
	Car.Speed = FMath::Max(0.0, Car.Speed + Accel * Dt);
	const double StoodBefore = Car.StandS;
	Car.StandS = Car.Speed < 0.3 ? Car.StandS + Dt : 0.0;
	if (StoodBefore < 30.0 && Car.StandS >= 30.0)
	{
		UE_LOG(LogTemp, Display, TEXT("SimCruise traffic stuck 30 s: car at %s (stop state %d, why %d, %.0f m to its junction) waits for %s"),
			*Car.GetActorLocation().ToCompactString(), Car.StopState, Why, FVector::Dist2D(Car.GetActorLocation(), Car.JunctionCm) / 100.0,
			Why == 2 ? TEXT("the player") : (BlockerCm.IsZero() ? TEXT("its stop line or a gap") : *BlockerCm.ToCompactString()));
	}
	Car.bBraking = Accel < -0.6 || (Car.Speed < 0.2 && Gap >= 0.0 && Gap < 6.0);
	Car.S += Car.Dir * Car.Speed * Dt;

	// Through a junction straight on: carry on along this ribbon.
	if (Car.bNextChosen && Car.bNextStraight && Car.Dir * (Car.S - Car.DecisionS) >= 0.0)
	{
		Car.bNextChosen = false;
		Car.StopState = 0;
		Car.LastDecisionS = Car.DecisionS;
	}
	// Past a turn or the end: onto the chosen successor, the jump eased out.
	const double Over = Car.bNextChosen && !Car.bNextStraight ? Car.Dir * (Car.S - Car.DecisionS) : (Car.Dir > 0 ? Car.S - R->St.Last().S : R->St[0].S - Car.S);
	if (Over > 0.0)
	{
		if (!Car.bNextChosen && !ChooseAt(Car, EndS, true))
		{
			Car.Ribbon = INDEX_NONE;
			return;
		}
		if (Car.bNextStraight)
		{
			// The end of a ribbon with only the way it came: nowhere to go.
			Car.Ribbon = INDEX_NONE;
			return;
		}
		const FPose Old = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.DecisionS, Car.Lane);
		Car.Tile = Car.NextTile;
		Car.Ribbon = Car.NextRibbon;
		Car.Dir = Car.NextDir;
		Car.S = Car.NextS + Car.Dir * Over;
		const double Turned = Car.NextTurnDeg;
		Car.bNextChosen = false;
		Car.StopState = 0;
		Car.LastDecisionS = -1e9;
		++RibbonChanges;
		// Into the nearest lane of the new road: the right one after a right turn, the left
		// one after a left turn (US practice); straight on, the same lane where it exists.
		if (const FSimCruiseRibbon* NewR = GetRibbon(Car.Tile, Car.Ribbon))
		{
			const int32 Station = FMath::Clamp(Algo::LowerBound(NewR->St, Car.S, [](const FSimCruiseRibbonStation& St, double V) { return St.S < V; }), 0, NewR->St.Num() - 1);
			const int32 Lanes = SimCruiseTrafficModel::LaneCount(*NewR, Station, Car.Dir);
			Car.Lane = Turned > 30.0 ? 0 : Turned < -30.0 ? Lanes - 1 : FMath::Clamp(Car.Lane, 0, Lanes - 1);
		}
		const FPose New = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.S, Car.Lane);
		if (!New.bValid) { Car.Ribbon = INDEX_NONE; return; }
		Car.BlendCm += FVector(Old.LocationCm.X - New.LocationCm.X, Old.LocationCm.Y - New.LocationCm.Y, Old.LocationCm.Z - New.LocationCm.Z);
		Car.BlendYawDeg = FRotator::NormalizeAxis(Car.BlendYawDeg + SignedAngleDeg(New.Heading, Old.Heading));
	}

	// Lanes: keep to one that exists here, take the turning lane in time, and pass a slow
	// car on the left (else the right) when the next lane is clear.
	{
		const int32 Station = FMath::Clamp(Algo::LowerBound(R->St, Car.S, [](const FSimCruiseRibbonStation& St, double V) { return St.S < V; }), 0, R->St.Num() - 1);
		const int32 Lanes = SimCruiseTrafficModel::LaneCount(*R, Station, Car.Dir);
		int32 Want = FMath::Clamp(Car.Lane, 0, Lanes - 1);
		Car.LaneChangeCooldownS = FMath::Max(0.0, Car.LaneChangeCooldownS - Dt);
		const double ToDecision = Car.bNextChosen ? FMath::Abs(Car.DecisionS - Car.S) : 1e9;
		if (Car.bNextChosen && FMath::Abs(Car.NextTurnDeg) > 30.0 && ToDecision < 80.0)
		{
			Want = Car.NextTurnDeg > 0.0 ? 0 : Lanes - 1; // positive turns are to the right (Unreal's Y is right)
		}
		else if (Lanes > 1 && Car.LaneChangeCooldownS <= 0.0 && ToDecision > 60.0 && Gap >= 0.0 && Gap < 35.0 && Closing > 1.0)
		{
			if (Car.Lane + 1 < Lanes && LaneClear(Car, Car.Lane + 1, PlayerCm)) Want = Car.Lane + 1;
			else if (Car.Lane > 0 && LaneClear(Car, Car.Lane - 1, PlayerCm)) Want = Car.Lane - 1;
		}
		// One lane at a time, and only into a clear one (a lane that ends is left regardless).
		if (Want != Car.Lane)
		{
			const int32 Step = Car.Lane + (Want > Car.Lane ? 1 : -1);
			if (Car.Lane >= Lanes || Car.LaneChangeCooldownS <= 0.0 && LaneClear(Car, Step, PlayerCm))
			{
				const FPose From = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.S, Car.Lane);
				const FPose To = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.S, Car.Lane >= Lanes ? Lanes - 1 : Step);
				Car.LaneShiftCm += From.LocationCm - To.LocationCm;
				Car.Lane = Car.Lane >= Lanes ? Lanes - 1 : Step;
				Car.LaneChangeCooldownS = 6.0;
				++LaneChanges;
			}
		}
	}

	const FPose Now = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.S, Car.Lane);
	if (!Now.bValid) { Car.Ribbon = INDEX_NONE; return; }
	const double Ease = FMath::Exp(-Dt / 0.7);
	Car.BlendCm *= Ease;
	Car.BlendYawDeg *= Ease;
	// A lane change eases out over about 3 s (time constant 1 s), the car angled into it.
	constexpr double LaneTau = 1.0;
	Car.LaneShiftCm *= FMath::Exp(-Dt / LaneTau);
	const FVector2d RightOfPath(-Now.Heading.Y, Now.Heading.X);
	const double SideSpeed = -FVector2d::DotProduct(FVector2d(Car.LaneShiftCm.X, Car.LaneShiftCm.Y) / 100.0, RightOfPath) / LaneTau;
	const double LaneYawDeg = FMath::RadiansToDegrees(FMath::Atan2(SideSpeed, FMath::Max(Car.Speed, 2.0)));
	const double PrevYaw = Car.GetActorRotation().Yaw;
	const double Yaw = FMath::RadiansToDegrees(FMath::Atan2(Now.Heading.Y, Now.Heading.X)) + Car.BlendYawDeg + LaneYawDeg;
	// The car's origin is between the axles at the body's height: the tyres touch the road
	// a tyre radius under the hubs.
	const double GroundInFrameCm = Car.Presentation->GetStaticHubLocation(0).Z - Car.TyreRadiusCm;
	Car.SetActorLocationAndRotation(Now.LocationCm + Car.BlendCm + Car.LaneShiftCm - FVector(0.0, 0.0, GroundInFrameCm), FRotator(0.0, Yaw, 0.0));
	// Wheels: rolling with the distance, steered by the yaw rate (bicycle model).
	Car.Spin = FMath::Fmod(Car.Spin + Car.Speed * Dt / FMath::Max(Car.TyreRadiusCm / 100.0, 0.1), UE_DOUBLE_TWO_PI);
	const double YawRate = FMath::DegreesToRadians(FRotator::NormalizeAxis(Yaw - PrevYaw)) / FMath::Max(Dt, 1e-3);
	const double WheelbaseM = FMath::Max((Car.Presentation->GetStaticHubLocation(0).X - Car.Presentation->GetStaticHubLocation(2).X) / 100.0, 1.5);
	const double Steer = Car.Speed > 0.5 ? FMath::Atan(WheelbaseM * YawRate / Car.Speed) : Car.SteerRad;
	Car.SteerRad += (FMath::Clamp(Steer, -0.6, 0.6) - Car.SteerRad) * (1.0 - FMath::Exp(-Dt / 0.15));
}

void ASimCruiseTraffic::UpdateVoice(ASimCruiseTrafficCar& Car, double DistanceM, bool bSound)
{
	// An electric car has no combustion sound.
	const bool bWant = bSound && DistanceM < VoiceRangeM && Car.CarId != TEXT("model3");
	if (!bWant)
	{
		if (Car.Voice && Car.Voice->IsPlaying()) Car.Voice->Stop();
		return;
	}
	if (!Car.Voice)
	{
		Car.Voice = NewObject<USimCruiseEngineSoundComponent>(&Car, TEXT("Voice"));
		Car.Voice->SetupAttachment(Car.Presentation);
		Car.Voice->bAllowSpatialization = true;
		Car.Voice->bOverrideAttenuation = true;
		FSoundAttenuationSettings& A = Car.Voice->AttenuationOverrides;
		A.bAttenuate = true;
		A.bSpatialize = true;
		A.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		A.AttenuationShapeExtents = FVector(300.f, 0.f, 0.f);  // full level within 3 m
		A.FalloffDistance = static_cast<float>(VoiceRangeM * 100.0) - 300.f;
		// The player's engine sits at -12 dB as the Godot mix had it; an engine outside is
		// heard at the same level up close.
		Car.Voice->SetVolumeMultiplier(0.25f);
		if (const FSimCruiseCarPresentationProfile* Profile = Car.Presentation->GetProfile())
		{
			if (Profile->EngineSound.IsSet())
			{
				Car.Voice->ConfigureCrossPlaneV8(Profile->EngineSound->BankOfEvent, Profile->EngineSound->NearGain, Profile->EngineSound->FarGain);
			}
		}
		Car.Voice->RegisterComponent();
	}
	// An automatic cruising in a tall gear: about 750 rpm plus 80 per m/s, more while it
	// pulls away; the load follows the acceleration.
	FSimCruiseInlineFourSynth::FInputs In;
	const double Pull = FMath::Max(Car.Accel, 0.0);
	In.Rpm = FMath::Clamp(750.0 + 80.0 * Car.Speed + 600.0 * Pull, 700.0, 4500.0);
	In.Combustion = FMath::Clamp(0.12 + 0.4 * Pull + 0.01 * Car.Speed, 0.0, 1.0);
	In.ShaftLoad = FMath::Clamp(0.06 + 0.45 * Car.Accel, -1.0, 1.0);
	In.Pedal = FMath::Clamp(Pull / 1.4, 0.0, 1.0);
	In.bCabin = false;
	Car.Voice->SetEngineState(In);
	if (!Car.Voice->IsPlaying()) Car.Voice->Start();
}

bool ASimCruiseTraffic::LaneClear(const ASimCruiseTrafficCar& Car, int32 Lane, const FVector& PlayerCm) const
{
	const FPose Target = PoseAt(Car.Tile, Car.Ribbon, Car.Dir, Car.S, Lane);
	if (!Target.bValid) return false;
	auto Occupies = [&](const FVector& OtherCm)
	{
		const FVector2d D((OtherCm.X - Target.LocationCm.X) / 100.0, (OtherCm.Y - Target.LocationCm.Y) / 100.0);
		const double Along = FVector2d::DotProduct(D, Target.Heading);
		const double Across = FMath::Abs(D.X * Target.Heading.Y - D.Y * Target.Heading.X);
		return Across < 1.6 && Along > -15.0 && Along < 25.0;
	};
	for (const ASimCruiseTrafficCar* Other : Cars)
	{
		if (Other && Other != &Car && Other->Ribbon != INDEX_NONE && Occupies(Other->GetActorLocation())) return false;
	}
	return !Occupies(PlayerCm);
}

bool ASimCruiseTraffic::Spawn(const FVector& FocusCm, const FVector& PlayerFwd, const FVector& CameraCm, const FVector& CameraFwd, bool bAnywhere)
{
	// A random lane position on a public road of a loaded tile, out of the driver's sight.
	TArray<FIntPoint> TileIds;
	for (int32 Dx = -2; Dx <= 2; ++Dx)
	{
		for (int32 Dy = -2; Dy <= 2; ++Dy)
		{
			const FIntPoint Id = Streamer->GetFrame().TileAtWorldCm(FocusCm) + FIntPoint(Dx, Dy);
			if (Streamer->GetTile(Id) && Streamer->GetTile(Id)->GetBuild()) TileIds.Add(Id);
		}
	}
	if (TileIds.IsEmpty()) return false;
	for (int32 Attempt = 0; Attempt < 12; ++Attempt)
	{
		const FIntPoint TileId = TileIds[NextRandom(Random) % TileIds.Num()];
		const FSimCruiseTileBuild* Build = Streamer->GetTile(TileId)->GetBuild();
		if (Build->Roads.Ribbons.IsEmpty()) continue;
		const int32 RibbonIndex = NextRandom(Random) % Build->Roads.Ribbons.Num();
		const FSimCruiseRibbon& R = Build->Roads.Ribbons[RibbonIndex];
		if (R.bService || R.Class == TEXT("service") || R.St.Num() < 2 || R.Length() < 40.0) continue;
		const double S = R.St[0].S + 10.0 + Unit(Random) * (R.Length() - 20.0);
		const int32 Travel = R.St[R.St.Num() / 2].Travel;
		const int32 Dir = Travel != 0 ? Travel : (Unit(Random) < 0.5 ? 1 : -1);
		const int32 SpawnStation = FMath::Clamp(Algo::LowerBound(R.St, S, [](const FSimCruiseRibbonStation& St, double V) { return St.S < V; }), 0, R.St.Num() - 1);
		const int32 SpawnLane = static_cast<int32>(NextRandom(Random) % static_cast<uint32>(SimCruiseTrafficModel::LaneCount(R, SpawnStation, Dir)));
		const FPose Pose = PoseAt(TileId, RibbonIndex, Dir, S, SpawnLane);
		if (!Pose.bValid) continue;
		const double Dist = FVector::Dist(Pose.LocationCm, FocusCm) / 100.0;
		if (Dist < (bAnywhere ? 25.0 : SpawnMinM) || Dist > SpawnMaxM) continue;
		const FVector ToPose = Pose.LocationCm - CameraCm;
		const bool bInSight = FVector::DotProduct(ToPose.GetSafeNormal(), CameraFwd) > 0.2 && ToPose.Size() < 160.0 * 100.0;
		if (!bAnywhere && bInSight) continue;
		// Never in the player's path just ahead (a car appearing there would be in the way
		// before the driver could have seen it arrive).
		const FVector2d Ahead(PlayerFwd.X, PlayerFwd.Y);
		const FVector2d Rel((Pose.LocationCm.X - FocusCm.X) / 100.0, (Pose.LocationCm.Y - FocusCm.Y) / 100.0);
		const double Along = FVector2d::DotProduct(Rel, Ahead.GetSafeNormal());
		if (!Ahead.IsNearlyZero() && Along > 0.0 && Along < 150.0 && FMath::Abs(Rel.X * Ahead.GetSafeNormal().Y - Rel.Y * Ahead.GetSafeNormal().X) < 4.0) continue;
		bool bCrowded = false;
		for (const ASimCruiseTrafficCar* Other : Cars)
		{
			if (Other && FVector::Dist(Other->GetActorLocation(), Pose.LocationCm) < 15.0 * 100.0) { bCrowded = true; break; }
		}
		if (bCrowded) continue;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		ASimCruiseTrafficCar* Car = GetWorld()->SpawnActor<ASimCruiseTrafficCar>(Pose.LocationCm, FRotator::ZeroRotator, Params);
		if (!Car) return false;
		const uint8 Pick = static_cast<uint8>(NextRandom(Random));
		if (!Car->Setup(SimCruiseParkedCars::CarFor(Pick), SimCruiseParkedCars::ColourFor(static_cast<uint8>(NextRandom(Random)))))
		{
			Car->Destroy();
			return false;
		}
		Car->Tile = TileId;
		Car->Ribbon = RibbonIndex;
		Car->Dir = Dir;
		Car->S = S;
		Car->Seed = NextRandom(Random) | 1u;
		Car->Lane = SpawnLane;
		Car->DesiredSpeed = SimCruiseTrafficModel::SpeedLimitMps(R.Class) * (0.9 + 0.2 * Unit(Random));
		Car->Speed = Car->DesiredSpeed * 0.8;
		Cars.Add(Car);
		++Spawned;
		Advance(*Car, 0.0, FVector(1e12), FVector::ZeroVector);
		return true;
	}
	return false;
}

int32 ASimCruiseTraffic::CountStuckCars(double Seconds) const
{
	int32 Count = 0;
	for (const ASimCruiseTrafficCar* Car : Cars)
	{
		Count += Car && Car->Ribbon != INDEX_NONE && Car->StandS > Seconds ? 1 : 0;
	}
	return Count;
}

void ASimCruiseTraffic::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Streamer.IsValid() || (!Player && !bTestFocus) || !Streamer->GetFrame().IsValid())
	{
		return;
	}
	SinceStart += DeltaSeconds;
	// A test drives traffic round a fixed point with no player car in the way (far off) and a
	// camera looking down from high above, which sees nothing on the streets.
	const FVector Focus = bTestFocus ? TestFocusCm : Player->GetActorLocation();
	const FVector PlayerCm = bTestFocus ? TestFocusCm + FVector(0.0, 0.0, 1.0e6) : Focus;
	const FVector PlayerVel = bTestFocus ? FVector::ZeroVector : Player->GetVelocity();
	const FVector CameraCm = bTestFocus ? TestFocusCm + FVector(0.0, 0.0, 1.0e6)
		: (PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : Focus);
	const FVector CameraFwd = bTestFocus ? FVector::UpVector
		: (PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation().Vector() : Player->GetActorForwardVector());
	const double Dt = FMath::Min(static_cast<double>(DeltaSeconds), 0.1);
	const bool bNight = Streamer->GetLightsOn() > 0.5f;
	// Heard only with an audio device (not in headless runs).
	const bool bSound = CVarTrafficSound.GetValueOnGameThread() != 0 && GetWorld()->GetAudioDevice().IsValid();
	for (int32 I = Cars.Num() - 1; I >= 0; --I)
	{
		ASimCruiseTrafficCar* Car = Cars[I];
		if (Car && Car->Ribbon != INDEX_NONE)
		{
			Car->bLamps = bNight;
			Advance(*Car, Dt, PlayerCm, PlayerVel);
			UpdateVoice(*Car, FVector::Dist(Car->GetActorLocation(), CameraCm) / 100.0, bSound);
		}
		// A queue behind a car or the player that stands for good (you parked in the lane) would
		// grow without end: a car stuck in it for 25 s turns off out of sight, as drivers find
		// another way. Only when nothing can see it go.
		bool bGivesUp = false;
		if (Car && Car->Ribbon != INDEX_NONE && Car->StandS > 25.0 && (Car->Why == 1 || Car->Why == 2) &&
			CVarTrafficGridlockRules.GetValueOnGameThread() != 0)
		{
			const FVector ToCar = Car->GetActorLocation() - CameraCm;
			bGivesUp = ToCar.Size() > 6000.0 || FVector::DotProduct(ToCar.GetSafeNormal(), CameraFwd) < 0.2;
			QueueGiveUps += bGivesUp ? 1 : 0;
		}
		if (!Car || Car->Ribbon == INDEX_NONE || bGivesUp || FVector::Dist(Car->GetActorLocation(), Focus) > RemoveM * 100.0)
		{
			if (Car) Car->Destroy();
			Cars.RemoveAtSwap(I);
		}
	}
	ReportClock -= DeltaSeconds;
	if (ReportClock <= 0.0)
	{
		ReportClock = 5.0;
		int32 Moving = 0, AtStops = 0, Standing = 0, Heard = 0;
		int32 WhyCount[6] = {};
		int32 Stuck = 0;
		int32 StuckWhy[6] = {};
		double Sum = 0.0;
		// Wheels seen rolling: a moving car whose drawn front-left wheel has turned since the last report.
		int32 Rolling = 0;
		double MaxSteerDeg = 0.0;
		for (ASimCruiseTrafficCar* Car : Cars)
		{
			if (!Car) continue;
			const double Drawn = Car->Presentation->GetDrawnSpinRad(0);
			Rolling += Car->Speed > 1.0 && FMath::Abs(FMath::UnwindRadians(Drawn - Car->ReportedSpinRad)) > 1e-3 ? 1 : 0;
			Car->ReportedSpinRad = Drawn;
			MaxSteerDeg = FMath::Max(MaxSteerDeg, FMath::Abs(FMath::RadiansToDegrees(Car->Presentation->GetDrawnSteerRad(0))));
			Sum += Car->Speed;
			Moving += Car->Speed > 1.0 ? 1 : 0;
			AtStops += Car->StopState == 2 ? 1 : 0;
			Heard += Car->Voice && Car->Voice->IsPlaying() ? 1 : 0;
			if (Car->StandS > 20.0) { ++Stuck; ++StuckWhy[FMath::Clamp(Car->Why, 0, 5)]; }
			if (Car->Speed < 0.3 && Car->StopState != 2)
			{
				++Standing;
				++WhyCount[FMath::Clamp(Car->Why, 0, 5)];
			}
		}
		UE_LOG(LogTemp, Display, TEXT("SimCruise traffic wheels: %d of %d moving cars drawn rolling; largest drawn steer %.1f deg"), Rolling, Moving, MaxSteerDeg);
		UE_LOG(LogTemp, Display, TEXT("SimCruise traffic: %d cars (%d heard), %d moving, %d waiting at stops, %d standing elsewhere (lane %d, player %d, crossing %d, stop line %d, giving way %d, none %d), mean %.1f m/s, %d ribbon changes, %d lane changes, %d left-turn yields, %d spawned, %d stops passed; %d stuck over 20 s (lane %d, player %d, crossing %d, stop line %d, giving way %d, none %d)"),
			Cars.Num(), Heard, Moving, AtStops, Standing, WhyCount[1], WhyCount[2], WhyCount[3], WhyCount[4], WhyCount[5], WhyCount[0], Cars.Num() ? Sum / Cars.Num() : 0.0, RibbonChanges, LaneChanges, LeftTurnYields, Spawned, StopsPassed, Stuck, StuckWhy[1], StuckWhy[2], StuckWhy[3], StuckWhy[4], StuckWhy[5], StuckWhy[0]);
	}
	const int32 Want = FMath::Max(0, CVarTrafficCars.GetValueOnGameThread());
	for (int32 Tries = 0; Tries < 2 && Cars.Num() < Want; ++Tries)
	{
		Spawn(Focus, Player ? Player->GetActorForwardVector() : FVector::ForwardVector, CameraCm, CameraFwd, SinceStart < 3.0 || bTestFocus);
	}
}
