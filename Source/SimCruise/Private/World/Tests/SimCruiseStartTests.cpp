// Copyright SimCruise contributors. All Rights Reserved.
//
// The start of a drive and the land around it (release test 20261114-0725-53b290c): the car
// starts on a road that goes on, never on a bank beside it; a corrupt elevation pixel never
// stands as a spike; the start tiles load together; a background car never builds a heavy model.

#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarAssets.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRegionalTerrain.h"
#include "World/SimCruiseRoadCheck.h"
#include "World/SimCruiseStartPick.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileManager.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseStartTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	/** A flat synthetic tile (100 m up) with the given roads ([class, travel, name, [[u, v]...], width] rows). */
	TSharedRef<FSimCruiseTileBuild> FlatTile(const FString& Roads, double Side)
	{
		const int32 Grid = 32;
		FString H;
		for (int32 i = 0; i < (Grid + 1) * (Grid + 1); ++i) H += i ? TEXT(",100") : TEXT("100");
		FSimCruiseTileData Data;
		FString Error;
		SimCruiseTileData::Parse(FString::Printf(TEXT("{\"id\":[300,600],\"grid\":%d,\"h\":[%s],\"roads\":[%s]}"), Grid, *H, *Roads), Data, Error);
		return SimCruiseTileBuilder::Build(Data, Side);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStartOnRoadTest, "SimCruise.World.Start.OnRoadNotDeadEnd", SimCruiseStartTest::Flags)
bool FSimCruiseStartOnRoadTest::RunTest(const FString& Parameters)
{
	// Main Street across the tile; Wharf Road leaves it north and ends 165 m on (a dead end, as
	// a road to the water does). The start point lies near the wharf's end: the car starts on
	// Main Street, with the most road ahead there is, not facing the end of the wharf.
	constexpr double Side = 550.0;
	const TSharedRef<FSimCruiseTileBuild> B = SimCruiseStartTest::FlatTile(
		TEXT("[\"secondary\",0,\"Main Street\",[[0.02,0.5],[0.98,0.5]],7],[\"minor\",0,\"Wharf Road\",[[0.5,0.5],[0.5,0.2]],6]"), Side);
	const TArray<FSimCruiseStartTile> Tiles = { { FVector::ZeroVector, &B.Get() } };
	const FVector Near(0.5 * Side * 100.0, 0.25 * Side * 100.0, 0.0);
	const TArray<FSimCruiseStartPose> Picks = SimCruiseStartPick::Candidates(Tiles, FString(), Near, 900.0, 1e10);
	if (!TestTrue(TEXT("start candidates"), Picks.Num() > 0)) return false;
	const FSimCruiseStartPose& Best = Picks[0];
	const FSimCruiseRibbon& R = B->Roads.Ribbons[Best.Ribbon];
	TestEqual(TEXT("the start is on the road that goes on"), R.Name, FString(TEXT("Main Street")));
	double MostAhead = 0.0;
	for (const FSimCruiseStartPose& P : Picks) MostAhead = FMath::Max(MostAhead, P.AheadM);
	TestTrue(FString::Printf(TEXT("the most road ahead there is (%.0f of %.0f m)"), Best.AheadM, MostAhead), Best.AheadM >= MostAhead - 1.0);
	for (const FSimCruiseStartPose& P : Picks)
	{
		if (B->Roads.Ribbons[P.Ribbon].Name == TEXT("Wharf Road")) TestTrue(FString::Printf(TEXT("the wharf goes on only %.0f m"), P.AheadM), P.AheadM < 170.0);
	}
	// Every candidate stands on its carriageway; one moved 5 m to the side (onto the verge) does not.
	FString Why;
	TestTrue(TEXT("the start's footprint is on the carriageway"), SimCruiseStartPick::OnCarriageway(Tiles, Best, &Why));
	FSimCruiseStartPose Off = Best;
	const double Yaw = FMath::DegreesToRadians(Best.Rotation.Yaw);
	Off.LocationCm += FVector(-FMath::Sin(Yaw), FMath::Cos(Yaw), 0.0) * 500.0;
	TestFalse(TEXT("a pose 5 m to the side is off the carriageway"), SimCruiseStartPick::OnCarriageway(Tiles, Off, &Why));
	// The ahead walk: from Main Street's middle eastward to its end (~250 m), not past it.
	const double East = SimCruiseStartPick::AheadM(Tiles, Best.Tile, Best.Ribbon, B->Roads.Ribbons[Best.Ribbon].St[B->Roads.Ribbons[Best.Ribbon].St.Num() / 2].S, 1, 5000.0);
	TestTrue(FString::Printf(TEXT("ahead to the road's end: %.0f m"), East), East > 150.0 && East < 400.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseLandscapeOutlierTest, "SimCruise.World.Landscape.NoSpikes", SimCruiseStartTest::Flags)
bool FSimCruiseLandscapeOutlierTest::RunTest(const FString& Parameters)
{
	// A coast at 20 m (88 m mesh) with the Stonington ripple in it (a 1438 m peak beside a -314
	// m pit), and 30 km off a real mountain (a 1200 m cone, 3 km round) and a 300 m escarpment.
	TArray<FVector> P;
	auto Mountain = [](double X, double Y) { const double R = FMath::Sqrt(FMath::Square(X - 30000.0) + FMath::Square(Y)); return FMath::Max(0.0, 1200.0 * (1.0 - R / 3000.0)); };
	auto Escarpment = [](double X, double Y) { return X > 20000.0 && Y > 5000.0 ? 300.0 : 0.0; };
	for (int32 j = -60; j <= 60; ++j)
	for (int32 i = -60; i <= 420; ++i)
	{
		const double X = i * 88.0, Y = j * 88.0;
		P.Add(FVector(X * 100.0, Y * 100.0, (20.0 + Mountain(X, Y) + Escarpment(X, Y)) * 100.0));
	}
	const int32 Peak = P.IndexOfByPredicate([](const FVector& V) { return FMath::IsNearlyEqual(V.X, 0.0) && FMath::IsNearlyEqual(V.Y, 0.0); });
	const TArray<FVector> Clean = P;
	// The ripple: the row through the peak climbs to 1438 m, the row below drops to -314 m.
	const double Row[7] = { 157.0, 601.0, 1077.0, 1438.0, 1327.0, 1137.0, 274.0 };
	for (int32 k = 0; k < 7; ++k)
	{
		for (FVector& V : P)
		{
			if (FMath::IsNearlyEqual(V.X, (k - 3) * 8800.0) && FMath::IsNearlyEqual(V.Y, 0.0)) V.Z = Row[k] * 100.0;
			if (FMath::IsNearlyEqual(V.X, (k - 3) * 8800.0) && FMath::IsNearlyEqual(V.Y, -8800.0)) V.Z = -Row[k] * 0.22 * 100.0;
		}
	}
	const int32 Changed = ASimCruiseRegionalTerrain::RejectHeightOutliers(P);
	if (TestTrue(TEXT("the peak vertex is there"), P.IsValidIndex(Peak)))
	{
		TestTrue(FString::Printf(TEXT("the 1438 m peak is the coast again (%.1f m)"), P[Peak].Z / 100.0), FMath::Abs(P[Peak].Z / 100.0 - 20.0) < 1.0);
	}
	double WorstCoast = 0.0, WorstLand = 0.0;
	for (int32 i = 0; i < P.Num(); ++i)
	{
		const double Off = FMath::Abs(P[i].Z - Clean[i].Z) / 100.0;
		if (P[i].X < 15000.0 * 100.0) WorstCoast = FMath::Max(WorstCoast, Off);
		else WorstLand = FMath::Max(WorstLand, Off);
	}
	// What stays is under 100 m (the ripple's shallowest shoulder, a pit under the sea surface).
	TestTrue(FString::Printf(TEXT("the ripple is gone (worst %.1f m off the coast's 20 m)"), WorstCoast), WorstCoast < 100.0);
	TestTrue(FString::Printf(TEXT("the mountain and the escarpment keep their relief (worst change %.1f m)"), WorstLand), WorstLand < 0.01);
	TestTrue(FString::Printf(TEXT("only the ripple changed (%d vertices)"), Changed), Changed >= 7 && Changed <= 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStartTilesTest, "SimCruise.World.Start.TilesTogether", SimCruiseStartTest::Flags)
bool FSimCruiseStartTilesTest::RunTest(const FString& Parameters)
{
	// Before the car is placed the start tile and its four corners load together (the start
	// boost), the start tile first; edge neighbours never build at the same time.
	FSimCruiseTileManager M;
	M.RingRadius = 1;
	M.MaxInFlight = ASimCruiseWorldStreamer::StartConcurrentBuilds;
	M.SetFocus(FVector2d(10.5, 20.5), FVector2d(10.5, 20.5));
	const TArray<FSimCruiseTileJobTicket> First = M.TakeRequests(0.0);
	TestEqual(TEXT("five start tiles at once"), First.Num(), 5);
	if (First.Num() > 0) TestEqual(TEXT("the start tile first"), First[0].Tile, FIntPoint(10, 20));
	for (int32 a = 0; a < First.Num(); ++a)
	for (int32 b = a + 1; b < First.Num(); ++b)
	{
		const FIntPoint D = First[a].Tile - First[b].Tile;
		TestFalse(TEXT("no two edge neighbours together"), FMath::Abs(D.X) + FMath::Abs(D.Y) == 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseBackgroundCarTest, "SimCruise.World.Start.NoHeavyBackgroundCars", SimCruiseStartTest::Flags)
bool FSimCruiseBackgroundCarTest::RunTest(const FString& Parameters)
{
	// The Miatas' model is heavy (38 MB of geometry, a 11-30 s build on the game thread): a parked
	// or traffic car never builds it; until the player's choice has, its share parks a lighter car.
	const int64 Miata = SimCruiseCarAssets::ModelBytes(TEXT("nb1999"));
	TestTrue(FString::Printf(TEXT("the Miata's model is heavy (%.1f MB)"), Miata / 1048576.0), Miata > SimCruiseCarAssets::BackgroundModelMaxBytes);
	TestTrue(TEXT("the Mustang's is light"), SimCruiseCarAssets::ModelBytes(TEXT("mustang65")) > 0 && SimCruiseCarAssets::ModelBytes(TEXT("mustang65")) <= SimCruiseCarAssets::BackgroundModelMaxBytes);
	if (!SimCruiseCarAssets::IsBuilt(TEXT("nb1999")))
	{
		for (int32 Pick = 0; Pick < 25; ++Pick)
		{
			const FString Id = SimCruiseParkedCars::CarFor(static_cast<uint8>(Pick));
			TestNotEqual(TEXT("the Miata's share parks a lighter car"), Id, FString(TEXT("nb1999")));
			TestTrue(TEXT("a light stand-in"), SimCruiseCarAssets::IsBuilt(Id) || SimCruiseCarAssets::ModelBytes(Id) <= SimCruiseCarAssets::BackgroundModelMaxBytes);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseReleaseFaultProbe, "SimCruise.Debug.ReleaseFaultProbe", SimCruiseStartTest::Flags)
bool FSimCruiseReleaseFaultProbe::RunTest(const FString& Parameters)
{
	// A probe on recorded places (not a check): SIMCRUISE_PROBE_DIR holds one folder per place with
	// its tiles' documents (x_y.json, from the helper's tile command) and place.txt (start lat lon;
	// order of the tiles as the game built them; placed x y in world cm; point lat lon label). It
	// builds the tiles in that order with their seam contracts, logs the start the rules pick, why
	// the old start failed, and what each flagged step is. Without the variable it does nothing.
	const FString Root = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_PROBE_DIR"));
	if (Root.IsEmpty()) return true;
	TArray<FString> Places;
	IFileManager::Get().FindFiles(Places, *FPaths::Combine(Root, TEXT("*")), false, true);
	for (const FString& Place : Places)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *FPaths::Combine(Root, Place, TEXT("place.txt")))) continue;
		double Lat = 0.0, Lon = 0.0;
		FString Road;
		TArray<FIntPoint> Order;
		FVector2d Placed(1e30, 1e30);
		struct FPoint { double Lat, Lon; FString Label; };
		TArray<FPoint> Points;
		for (const FString& L : Lines)
		{
			TArray<FString> W;
			L.ParseIntoArrayWS(W);
			if (W.Num() >= 3 && W[0] == TEXT("start")) { Lat = FCString::Atod(*W[1]); Lon = FCString::Atod(*W[2]); }
			else if (W.Num() >= 2 && W[0] == TEXT("road")) { Road = L.RightChop(5).TrimStartAndEnd(); }
			else if (W.Num() >= 3 && W[0] == TEXT("placed")) { Placed = FVector2d(FCString::Atod(*W[1]), FCString::Atod(*W[2])); }
			else if (W.Num() >= 4 && W[0] == TEXT("point")) { Points.Add({ FCString::Atod(*W[1]), FCString::Atod(*W[2]), W[3] }); }
			else if (W.Num() >= 4 && W[0] == TEXT("pointcm")) { Points.Add({ 1e9 + FCString::Atod(*W[1]), FCString::Atod(*W[2]), W[3] }); }
			else if (W.Num() >= 2 && W[0] == TEXT("order")) { for (int32 k = 1; k < W.Num(); ++k) { FString X, Y; W[k].Split(TEXT("_"), &X, &Y); Order.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y))); } }
		}
		const FSimCruiseWorldFrame Frame = FSimCruiseWorldFrame::ForPlace(Lon, Lat);
		TMap<FIntPoint, TSharedRef<FSimCruiseTileBuild>> Built;
		TArray<FSimCruiseStartTile> Tiles;
		TArray<FIntPoint> TileIds;
		for (const FIntPoint& Id : Order)
		{
			FSimCruiseTileData Data;
			FString Error;
			if (!SimCruiseTileData::LoadFile(FPaths::Combine(Root, Place, FString::Printf(TEXT("%d_%d.json"), Id.X, Id.Y)), Data, Error)) { AddInfo(FString::Printf(TEXT("%s %d_%d: %s"), *Place, Id.X, Id.Y, *Error)); continue; }
			const TArray<FSimCruiseSeamEnd> Contracts = SimCruiseTileBuilder::NeighbourContracts(Id, Frame.TileSizeM, [&Built](const FIntPoint& T) -> const FSimCruiseTileBuild* { const TSharedRef<FSimCruiseTileBuild>* F = Built.Find(T); return F ? &F->Get() : nullptr; });
			Built.Add(Id, SimCruiseTileBuilder::Build(Data, Frame.TileSizeM, Contracts));
		}
		for (const TPair<FIntPoint, TSharedRef<FSimCruiseTileBuild>>& Pair : Built) { Tiles.Add({ Frame.TileOriginCm(Pair.Key), &Pair.Value.Get() }); TileIds.Add(Pair.Key); }
		const FVector StartCm = Frame.LonLatToWorldCm(Lon, Lat);
		const TArray<FSimCruiseStartPose> Picks = SimCruiseStartPick::Candidates(Tiles, Road, StartCm, 900.0, 1e10);
		FString Text = FString::Printf(TEXT("PROBE %s: %d tiles, start %s, %d start candidates"), *Place, Tiles.Num(), *StartCm.ToString(), Picks.Num());
		for (int32 k = 0; k < FMath::Min(3, Picks.Num()); ++k)
		{
			const FSimCruiseStartPose& P = Picks[k];
			const FSimCruiseRibbon& R = Tiles[P.Tile].Build->Roads.Ribbons[P.Ribbon];
			Text += FString::Printf(TEXT("\n  pick %d: %s (%s) tile %d_%d at %s yaw %.0f, %.0f m from the start, %.0f m ahead"), k, *R.Name, *R.Class, TileIds[P.Tile].X, TileIds[P.Tile].Y, *P.LocationCm.ToString(), P.Rotation.Yaw, P.DistanceM, P.AheadM);
		}
		// The release test's start: the ribbons there, and the ground.
		if (Placed.X < 1e29)
		{
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FVector2d Local = (Placed - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				if (Local.X < -5.0 || Local.Y < -5.0 || Local.X > B.TileSizeM + 5.0 || Local.Y > B.TileSizeM + 5.0) continue;
				double Top = 0.0;
				const bool bTop = SimCruiseRoadCheck::TopSurface(B, Local, Top);
				Text += FString::Printf(TEXT("\n  old start in %d_%d at (%.1f, %.1f): road top %s, ground %.2f m"), TileIds[t].X, TileIds[t].Y, Local.X, Local.Y,
					bTop ? *FString::Printf(TEXT("%.2f m"), Top) : TEXT("none"), B.TerrainN > 0 ? B.TerrainHeightAt(FVector2d(FMath::Clamp(Local.X, 0.0, B.TileSizeM), FMath::Clamp(Local.Y, 0.0, B.TileSizeM))) : 0.0);
				for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
				{
					const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
					int32 BestI = INDEX_NONE;
					double BestD = 15.0;
					for (int32 i = 0; i + 1 < R.St.Num(); ++i)
					{
						const FVector2d A = R.St[i].C, C = R.St[i + 1].C;
						const double T = FMath::Clamp(FVector2d::DotProduct(Local - A, C - A) / FMath::Max((C - A).SizeSquared(), 1e-9), 0.0, 1.0);
						const double D = FVector2d::Distance(Local, FMath::Lerp(A, C, T));
						if (D < BestD) { BestD = D; BestI = i; }
					}
					if (BestI == INDEX_NONE) continue;
					Text += FString::Printf(TEXT("\n    ribbon %d %s (%s) %d stations, station %d S %.1f: %.1f m from its centre line, half width %.1f, height %.2f, boundary start %d end %d"),
						r, *R.Name, *R.Class, R.St.Num(), BestI, R.St[BestI].S, BestD, R.St[BestI].HalfW, R.St[BestI].HC, R.bStartOnBoundary, R.bEndOnBoundary);
				}
			}
		}
		// The flagged steps: the step check's steps within 12 m of each point, and what is there.
		for (const FPoint& Pt : Points)
		{
			// A point given in world cm (pointcm x y) carries x + 1e9 in Lat and y in Lon.
			const FVector W = Pt.Lat > 1e8 ? FVector(Pt.Lat - 1e9, Pt.Lon, 0.0) : Frame.LonLatToWorldCm(Pt.Lon, Pt.Lat);
			Text += FString::Printf(TEXT("\n  %s at %s:"), *Pt.Label, *W.ToString());
			// The ribbons there (within 10 m).
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				const FVector2d Local = (FVector2d(W.X, W.Y) - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				if (Local.X < -10.0 || Local.Y < -10.0 || Local.X > B.TileSizeM + 10.0 || Local.Y > B.TileSizeM + 10.0) continue;
				for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
				{
					const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
					for (int32 i = 0; i + 1 < R.St.Num(); ++i)
					{
						const FVector2d A = R.St[i].C, C = R.St[i + 1].C;
						const double T = FMath::Clamp(FVector2d::DotProduct(Local - A, C - A) / FMath::Max((C - A).SizeSquared(), 1e-9), 0.0, 1.0);
						const double D = FVector2d::Distance(Local, FMath::Lerp(A, C, T));
						if (D > 10.0) continue;
						Text += FString::Printf(TEXT("\n    on tile %d_%d ribbon %d %s (%s, travel %d) %.1f m from its centre line, half width %.1f, height %.2f, heading %.0f, S %.0f of %.0f..%.0f"),
							TileIds[t].X, TileIds[t].Y, r, *R.Name, *R.Class, R.St[i].Travel, D, R.St[i].HalfW, R.St[i].HC,
							FMath::RadiansToDegrees(FMath::Atan2(R.St[i].T.Y, R.St[i].T.X)), R.St[i].S, R.St[0].S, R.St.Last().S);
						break;
					}
				}
			}
			for (int32 t = 0; t < Tiles.Num(); ++t)
			{
				const FSimCruiseTileBuild& B = *Tiles[t].Build;
				const FVector2d Local = (FVector2d(W.X, W.Y) - FVector2d(Tiles[t].OriginCm.X, Tiles[t].OriginCm.Y)) / 100.0;
				if (Local.X < -12.0 || Local.Y < -12.0 || Local.X > B.TileSizeM + 12.0 || Local.Y > B.TileSizeM + 12.0) continue;
				const double ToEdge = FMath::Min(FMath::Min(Local.X, Local.Y), FMath::Min(B.TileSizeM - Local.X, B.TileSizeM - Local.Y));
				Text += FString::Printf(TEXT("\n    tile %d_%d local (%.1f, %.1f), %.1f m from its edge"), TileIds[t].X, TileIds[t].Y, Local.X, Local.Y, ToEdge);
				// The top surface the car drives on (any ribbon), along the nearest ribbon through the
				// point, 0.1 m apart for 8 m each way, across this tile and its neighbours: the largest
				// second difference and where.
				{
					int32 NearR = INDEX_NONE, NearI = 0;
					double NearD = 12.0;
					for (int32 r = 0; r < B.Roads.Ribbons.Num(); ++r)
					{
						const FSimCruiseRibbon& R = B.Roads.Ribbons[r];
						for (int32 i = 0; i + 1 < R.St.Num(); ++i)
						{
							const FVector2d A = R.St[i].C, C = R.St[i + 1].C;
							const double T = FMath::Clamp(FVector2d::DotProduct(Local - A, C - A) / FMath::Max((C - A).SizeSquared(), 1e-9), 0.0, 1.0);
							const double D = FVector2d::Distance(Local, FMath::Lerp(A, C, T));
							if (D < NearD) { NearD = D; NearR = r; NearI = i; }
						}
					}
					if (NearR != INDEX_NONE)
					{
						const FVector2d Dir = B.Roads.Ribbons[NearR].St[NearI].T;
						const FVector2d WorldLocal = FVector2d(W.X, W.Y) / 100.0;
						TArray<double> H;
						TArray<FString> Who;
						for (double D = -8.0; D <= 8.0 + 1e-6; D += 0.1)
						{
							const FVector2d Q = WorldLocal + Dir * D;
							double Top = TNumericLimits<double>::Lowest();
							FString By = TEXT("none");
							for (int32 u = 0; u < Tiles.Num(); ++u)
							{
								const FSimCruiseTileBuild& U = *Tiles[u].Build;
								const FVector2d L2 = Q - FVector2d(Tiles[u].OriginCm.X, Tiles[u].OriginCm.Y) / 100.0;
								if (L2.X < 0.0 || L2.Y < 0.0 || L2.X > U.TileSizeM || L2.Y > U.TileSizeM) continue;
								double Th = 0.0;
								if (SimCruiseRoadCheck::TopSurface(U, L2, Th) && Th > Top) { Top = Th; By = FString::Printf(TEXT("%d_%d"), TileIds[u].X, TileIds[u].Y); }
							}
							H.Add(Top);
							Who.Add(By);
						}
						double Worst = 0.0;
						int32 At = INDEX_NONE;
						for (int32 k = 1; k + 1 < H.Num(); ++k)
						{
							if (H[k - 1] < -1e30 || H[k] < -1e30 || H[k + 1] < -1e30) continue;
							const double J = FMath::Abs(H[k + 1] - 2.0 * H[k] + H[k - 1]);
							if (J > Worst) { Worst = J; At = k; }
						}
						if (At != INDEX_NONE)
						{
							Text += FString::Printf(TEXT("\n      top surface along %s: worst jump %.1f cm at %+.1f m (%.3f, %.3f, %.3f m; tiles %s / %s / %s)"),
								*B.Roads.Ribbons[NearR].Name, Worst * 100.0, -8.0 + At * 0.1, H[At - 1], H[At], H[At + 1], *Who[At - 1], *Who[At], *Who[At + 1]);
						}
					}
				}
				for (const FSimCruiseRoadStep& S : SimCruiseRoadCheck::FindSteps(B))
				{
					if (FVector2d::Distance(S.PositionM, Local) > 12.0) continue;
					const FSimCruiseRibbon& R = B.Roads.Ribbons[S.Ribbon];
					// The nearest junction cut along the ribbon, and a bridge.
					double ToCut = 1e9;
					bool bBridge = false;
					for (int32 i = 0; i < R.St.Num(); ++i)
					{
						const bool bCut = (R.Sides[0].Cut.IsValidIndex(i) && R.Sides[0].Cut[i]) || (R.Sides[1].Cut.IsValidIndex(i) && R.Sides[1].Cut[i]);
						if (bCut) ToCut = FMath::Min(ToCut, FMath::Abs(R.St[i].S - S.S));
						if (FMath::Abs(R.St[i].S - S.S) < 10.0) bBridge |= R.St[i].bBridge;
					}
					Text += FString::Printf(TEXT("\n      step %.1f cm on %s (%s) at S %.1f lane %+.1f, %.1f m from a junction cut, bridge %d, ribbon S %.0f..%.0f boundary %d/%d"),
						S.JumpCm, *R.Name, *R.Class, S.S, S.Lateral, ToCut, bBridge ? 1 : 0, R.St[0].S, R.St.Last().S, R.bStartOnBoundary, R.bEndOnBoundary);
				}
			}
		}
		AddInfo(Text);
		UE_LOG(LogTemp, Display, TEXT("%s"), *Text);
	}
	return true;
}

#endif
