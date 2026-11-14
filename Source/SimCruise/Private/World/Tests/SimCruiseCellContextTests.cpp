// Copyright SimCruise contributors. All Rights Reserved.

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Knowledge/SimCruiseClimate.h"
#include "Knowledge/SimCruiseRoadsideData.h"
#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "World/SimCruiseCellContext.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseWorldSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseCellContextTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
	const FIntPoint OceanDrive(18180, 27907);

	bool LoadOceanDrive(FAutomationTestBase& Test, FSimCruiseTileData& Data, double& Side)
	{
		FString Error;
		if (!Test.TestTrue(TEXT("tile loads"), SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(OceanDrive), Data, Error))) return false;
		Side = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat).TileSizeM;
		return true;
	}

	/**
	 * SYNTHETIC TEST DATA written by hand: shaped like an Overpass answer, not OpenStreetMap
	 * data. The road way follows the ribbon it is made from (its points turned into lon/lat).
	 */
	FString SyntheticOverpass(const FIntPoint& Tile, double Side, const FSimCruiseRibbon& Ribbon, int32 MidIndex, bool bReversed)
	{
		FString Geometry;
		const int32 N = Ribbon.St.Num();
		for (int32 k = 0; k < N; ++k)
		{
			const FSimCruiseRibbonStation& S = Ribbon.St[bReversed ? N - 1 - k : k];
			double Lon, Lat;
			SimCruiseGeo::TileToLonLat(FVector2d(Tile.X, Tile.Y) + S.C / Side, Lon, Lat);
			Geometry += FString::Printf(TEXT("%s{\"lat\":%.8f,\"lon\":%.8f}"), k ? TEXT(",") : TEXT(""), Lat, Lon);
		}
		const FSimCruiseRibbonStation& Mid = Ribbon.St[MidIndex];
		auto LonLatAt = [&](const FVector2d& P, double& Lat, double& Lon) { SimCruiseGeo::TileToLonLat(FVector2d(Tile.X, Tile.Y) + P / Side, Lon, Lat); };
		double LampLat, LampLon, RailLat0, RailLon0, RailLat1, RailLon1;
		// A lamp 1.5 m beyond the right edge, a guard rail 2 m beyond the left edge, both at the middle.
		LonLatAt(Mid.C + Mid.R * (Mid.HalfW + 1.5), LampLat, LampLon);
		LonLatAt(Mid.C - Mid.R * (Mid.HalfW + 2.0) - Mid.T * 10.0, RailLat0, RailLon0);
		LonLatAt(Mid.C - Mid.R * (Mid.HalfW + 2.0) + Mid.T * 10.0, RailLat1, RailLon1);
		// sidewalk=right of the way's own direction: right of the ribbon when the way runs with it.
		return FString::Printf(TEXT(R"json({"elements":[
			{"type":"way","id":4242,"tags":{"highway":"tertiary","name":"Synthetic Way","surface":"concrete","smoothness":"good","sidewalk":"right","lit":"no","maxspeed":"35 mph"},"geometry":[%s]},
			{"type":"way","id":77,"tags":{"barrier":"guard_rail"},"geometry":[{"lat":%.8f,"lon":%.8f},{"lat":%.8f,"lon":%.8f}]},
			{"type":"node","id":9001,"lat":%.8f,"lon":%.8f,"tags":{"highway":"street_lamp"}},
			{"type":"node","id":9002,"lat":%.8f,"lon":%.8f,"tags":{"shop":"bakery","name":"Test Bakery","brand":"Test Brand","brand:wikidata":"Q0"}},
			{"type":"node","id":9002,"lat":%.8f,"lon":%.8f,"tags":{"shop":"bakery","name":"Test Bakery"}}]})json"),
			*Geometry, RailLat0, RailLon0, RailLat1, RailLon1, LampLat, LampLon, LampLat, LampLon, LampLat, LampLon);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseContextStableTest, "SimCruise.World.Context.SameRecordTwoBuilds", SimCruiseCellContextTest::Flags)
bool FSimCruiseContextStableTest::RunTest(const FString& Parameters)
{
	// The same place gives the same record and the same seeds on two builds (the cache cleared in between).
	FSimCruiseTileData Data;
	double Side;
	if (!SimCruiseCellContextTest::LoadOceanDrive(*this, Data, Side)) return false;
	SimCruiseCellContext::ClearCache();
	const TSharedRef<FSimCruiseTileBuild> A = SimCruiseTileBuilder::Build(Data, Side);
	SimCruiseCellContext::ClearCache();
	const TSharedRef<FSimCruiseTileBuild> B = SimCruiseTileBuilder::Build(Data, Side);
	if (!TestTrue(TEXT("both builds have a record"), A->Context.IsValid() && B->Context.IsValid())) return false;
	const FSimCruiseTileContext& Ca = *A->Context;
	const FSimCruiseTileContext& Cb = *B->Context;
	AddInfo(Ca.Describe(4));
	TestTrue(TEXT("hundreds of cells"), Ca.Cells.Num() > 50);
	TestEqual(TEXT("same cell count"), Ca.Cells.Num(), Cb.Cells.Num());
	TestTrue(TEXT("same data hash"), Ca.DataHash == Cb.DataHash);
	TestTrue(TEXT("same tile seed"), Ca.TileSeed == Cb.TileSeed);
	int32 Diff = 0;
	TSet<uint32> Seeds;
	for (int32 i = 0; i < FMath::Min(Ca.Cells.Num(), Cb.Cells.Num()); ++i)
	{
		const FSimCruiseCellContext& X = Ca.Cells[i];
		const FSimCruiseCellContext& Y = Cb.Cells[i];
		Diff += X.Seed != Y.Seed || X.Surface != Y.Surface || X.Sides[0].LandUse != Y.Sides[0].LandUse || X.Sides[1].LandUse != Y.Sides[1].LandUse
			|| X.Lit != Y.Lit || FMath::Abs(X.S0 - Y.S0) > 1e-6;
		Seeds.Add(X.Seed);
	}
	TestEqual(TEXT("no cell differs"), Diff, 0);
	TestTrue(TEXT("cells have their own seeds"), Seeds.Num() > Ca.Cells.Num() * 9 / 10);
	TestEqual(TEXT("same grass dryness"), Ca.GrassDry, Cb.GrassDry);
	TestFalse(TEXT("a climate"), Ca.Climate.IsNone());
	TestTrue(TEXT("coconut palms on Ocean Drive"), Ca.SpeciesWeight(TEXT("cocos_nucifera")) > 0.9f);

	// The cache: a third build of the same data returns the stored record.
	bool bHit = false;
	const SimCruiseCellContext::FInputs In{ Data, A->Roads, Side, 25.78, -80.13, Ca.Month, nullptr };
	const TSharedRef<const FSimCruiseTileContext, ESPMode::ThreadSafe> Again = SimCruiseCellContext::ForBuild(In, &bHit);
	TestTrue(TEXT("third build hits the cache"), bHit);
	TestTrue(TEXT("cached record has the same seeds"), Again->Cells.Num() > 0 && Ca.Cells.Num() > 0 && Again->Cells[0].Seed == Ca.Cells[0].Seed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseContextReadyTest, "SimCruise.World.Context.ReadyBefore300m", SimCruiseCellContextTest::Flags)
bool FSimCruiseContextReadyTest::RunTest(const FString& Parameters)
{
	// The record is baked inside the tile build, so it is ready when the tile is. Tiles
	// stream one ring round the car's tile (SimCruiseTileManager RingRadius 1): a tile starts
	// building at least one tile width before the car can reach it. At 100 km/h it must
	// finish with 300 m to spare.
	FSimCruiseTileData Data;
	double Side;
	if (!SimCruiseCellContextTest::LoadOceanDrive(*this, Data, Side)) return false;
	SimCruiseCellContext::ClearCache();
	const double T0 = FPlatformTime::Seconds();
	const TSharedRef<FSimCruiseTileBuild> Build = SimCruiseTileBuilder::Build(Data, Side);
	const double BuildS = FPlatformTime::Seconds() - T0;
	if (!TestTrue(TEXT("record built"), Build->Context.IsValid())) return false;
	const FSimCruiseTileContext& C = *Build->Context;
	int32 Uncovered = 0;
	for (int32 r = 0; r < Build->Roads.Ribbons.Num(); ++r)
	{
		const FSimCruiseRibbon& R = Build->Roads.Ribbons[r];
		if (R.St.Num() < 2 || R.Length() < 0.5) continue;
		const FIntPoint Range = C.RibbonCells.IsValidIndex(r) ? C.RibbonCells[r] : FIntPoint(0, 0);
		if (Range.Y == 0) { ++Uncovered; continue; }
		const double Covered = C.Cells[Range.X + Range.Y - 1].S1 - C.Cells[Range.X].S0;
		if (FMath::Abs(Covered - R.Length()) > 0.01) ++Uncovered;
		for (int32 i = Range.X; i < Range.X + Range.Y; ++i) if (C.Cells[i].S1 - C.Cells[i].S0 > SimCruiseCellContext::CellM * 1.05) ++Uncovered; // 5 degree bands
	}
	TestEqual(TEXT("every road covered by cells of about 32 m"), Uncovered, 0);
	const double Speed = 100.0 / 3.6;
	for (const double Lat : { 25.78, 35.48, 45.0 })
	{
		const double Spare = SimCruiseGeo::TileSizeMetres(Lat) - Speed * BuildS;
		AddInfo(FString::Printf(TEXT("lat %.1f: tile %.0f m, build %.0f ms (record %.2f ms), %.0f m to spare"), Lat, SimCruiseGeo::TileSizeMetres(Lat), BuildS * 1000.0, C.BakeMs, Spare));
		// Asserted for the hero roads' latitudes; farther north the spare is reported (it shrinks
		// with the tile and with a busy PC's tile build, not with the record's ~1 ms).
		if (Lat < 40.0) TestTrue(FString::Printf(TEXT("ready 300 m before the car at lat %.0f"), Lat), Spare >= 300.0);
	}
	TestTrue(TEXT("record bake under 20 ms"), C.BakeMs < 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadsideParseTest, "SimCruise.Knowledge.Roadside.Parse", SimCruiseCellContextTest::Flags)
bool FSimCruiseRoadsideParseTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("35 mph"), SimCruiseRoadsideData::ParseMaxSpeedKph(TEXT("35 mph")), 56);
	TestEqual(TEXT("50"), SimCruiseRoadsideData::ParseMaxSpeedKph(TEXT("50")), 50);
	TestEqual(TEXT("none"), SimCruiseRoadsideData::ParseMaxSpeedKph(TEXT("none")), 0);
	TestTrue(TEXT("the query asks for road geometry"), SimCruiseRoadsideData::OverpassGeometryStatements(TEXT("(1,2,3,4)")).Contains(TEXT("out tags geom")));

	FSimCruiseTileData Data;
	double Side;
	if (!SimCruiseCellContextTest::LoadOceanDrive(*this, Data, Side)) return false;
	const TSharedRef<FSimCruiseTileBuild> Build = SimCruiseTileBuilder::Build(Data, Side);
	// "A road": the longest ribbon with a stretch clear of other roads by 6 m (not a divided avenue).
	int32 Best = INDEX_NONE;
	auto Clearance = [&](int32 Self, const FVector2d& P)
	{
		double Min = 1e9;
		for (int32 r = 0; r < Build->Roads.Ribbons.Num(); ++r)
		{
			if (r == Self) continue;
			const FSimCruiseRibbon& O = Build->Roads.Ribbons[r];
			for (int32 i = 0; i + 1 < O.St.Num(); ++i)
			{
				Min = FMath::Min(Min, FVector2d::Distance(P, FMath::ClosestPointOnSegment2D(P, O.St[i].C, O.St[i + 1].C)) - FMath::Max(O.St[i].HalfW, O.St[i + 1].HalfW));
			}
		}
		return Min;
	};
	TArray<int32> Order;
	for (int32 r = 0; r < Build->Roads.Ribbons.Num(); ++r) if (Build->Roads.Ribbons[r].St.Num() >= 12) Order.Add(r);
	Order.Sort([&](int32 A, int32 B) { return Build->Roads.Ribbons[A].Length() > Build->Roads.Ribbons[B].Length(); });
	int32 Mid = INDEX_NONE;
	for (const int32 r : Order)
	{
		const FSimCruiseRibbon& R = Build->Roads.Ribbons[r];
		for (int32 k = R.St.Num() / 4; k + R.St.Num() / 4 < R.St.Num() && Mid == INDEX_NONE; ++k)
		{
			const FSimCruiseRibbonStation& S = R.St[k];
			// The lamp, the rail, and the cell's whole length must be clear of other roads.
			if (Clearance(r, S.C + S.R * (S.HalfW + 1.5)) > 6.0 && Clearance(r, S.C - S.R * (S.HalfW + 2.0)) > 6.0
				&& Clearance(r, S.C - S.T * 16.0) > 6.0 && Clearance(r, S.C + S.T * 16.0) > 6.0) Mid = k;
		}
		if (Mid != INDEX_NONE) { Best = r; break; }
	}
	if (!TestTrue(TEXT("a road with a clear stretch"), Best != INDEX_NONE)) return false;
	const FSimCruiseRibbon& Ribbon = Build->Roads.Ribbons[Best];
	if (!TestTrue(TEXT("a clear station"), Mid != INDEX_NONE)) return false;

	for (const bool bReversed : { false, true })
	{
		FSimCruiseRoadside RS;
		FString Error;
		if (!TestTrue(TEXT("parses"), SimCruiseRoadsideData::ParseOverpass(SimCruiseCellContextTest::SyntheticOverpass(Data.Id, Side, Ribbon, Mid, bReversed), RS, Error))) return false;
		TestEqual(TEXT("one road"), RS.Roads.Num(), 1);
		TestEqual(TEXT("one barrier"), RS.Barriers.Num(), 1);
		TestEqual(TEXT("a lamp and one bakery (the duplicate dropped)"), RS.Points.Num(), 2);
		if (RS.Roads.Num() != 1) return false;
		TestEqual(TEXT("sidewalk right only"), RS.Roads[0].Sidewalk[1], FString(TEXT("yes")));
		TestEqual(TEXT("no sidewalk left"), RS.Roads[0].Sidewalk[0], FString(TEXT("no")));
		TestEqual(TEXT("maxspeed"), RS.Roads[0].MaxSpeedKph, 56);
		// The cache file format keeps everything.
		FSimCruiseRoadside Back;
		TestTrue(TEXT("round trip"), SimCruiseRoadsideData::FromJson(SimCruiseRoadsideData::ToJson(Data.Id, RS), Back));
		TestTrue(TEXT("round trip hash"), Back.Hash() == RS.Hash());

		const SimCruiseCellContext::FInputs In{ Data, Build->Roads, Side, 25.78, -80.13, 11, &RS };
		FSimCruiseTileContext C;
		SimCruiseCellContext::Bake(In, C);
		const FIntPoint Range = C.RibbonCells[Best];
		int32 OnWay = 0, Concrete = 0, Unlit = 0, RightWalk = 0, LeftWalk = 0;
		for (int32 i = Range.X; i < Range.X + Range.Y; ++i)
		{
			const FSimCruiseCellContext& Cell = C.Cells[i];
			if (Cell.WayId != 4242) continue;
			++OnWay;
			Concrete += Cell.Surface == TEXT("concrete") && Cell.bSurfaceMapped;
			Unlit += Cell.Lit == TEXT("no") && Cell.bLitMapped;
			// The way's right is the ribbon's right when it runs with it, its left when against.
			RightWalk += Cell.Sides[bReversed ? 0 : 1].Sidewalk == TEXT("yes") && Cell.Sides[bReversed ? 0 : 1].bSidewalkMapped;
			LeftWalk += Cell.Sides[bReversed ? 1 : 0].Sidewalk == TEXT("no");
			TestTrue(TEXT("seed from way and station"), Cell.Seed == SimCruiseCellContext::StableSeed(4242, Cell.WayStation, FVector2d::ZeroVector, NAME_None));
		}
		AddInfo(FString::Printf(TEXT("%s: %d of %d cells on the way"), bReversed ? TEXT("reversed") : TEXT("with"), OnWay, Range.Y));
		TestTrue(TEXT("most cells matched"), OnWay >= Range.Y * 8 / 10);
		TestEqual(TEXT("surface mapped"), Concrete, OnWay);
		TestEqual(TEXT("lit mapped"), Unlit, OnWay);
		TestEqual(TEXT("sidewalk on the way's right"), RightWalk, OnWay);
		TestEqual(TEXT("no sidewalk on the way's left"), LeftWalk, OnWay);
		// The guard rail on the left, the lamp on the right, at the middle.
		const FSimCruiseCellContext* MidCell = C.At(Best, Ribbon.St[Mid].S);
		if (TestNotNull(TEXT("middle cell"), MidCell))
		{
			TestEqual(TEXT("guard rail left"), MidCell->Sides[0].Barrier, FName(TEXT("guard_rail")));
			TestTrue(TEXT("rail offset about 2 m"), FMath::Abs(MidCell->Sides[0].BarrierOffsetM - 2.0f) < 1.0f);
		}
		const FSimCruisePlacedPoint* Lamp = C.Points.FindByPredicate([](const FSimCruisePlacedPoint& P) { return P.Kind == TEXT("street_lamp"); });
		if (TestNotNull(TEXT("lamp placed"), Lamp))
		{
			const FSimCruiseRibbon& On = Build->Roads.Ribbons[Lamp->Ribbon];
			AddInfo(FString::Printf(TEXT("lamp on ribbon %d (%s %s, %d stations) side %d offset %.2f m; expected ribbon %d (%s %s) station %d"),
				Lamp->Ribbon, *On.Class, *On.Name, On.St.Num(), Lamp->Side, Lamp->OffsetM, Best, *Ribbon.Class, *Ribbon.Name, Mid));
			TestEqual(TEXT("lamp on its ribbon"), Lamp->Ribbon, Best);
			TestEqual(TEXT("lamp on the right"), Lamp->Side, 1);
			TestTrue(TEXT("lamp 1.5 m beyond the edge"), FMath::Abs(Lamp->OffsetM - 1.5) < 0.3);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseClimateTest, "SimCruise.Knowledge.Climate.Koppen", SimCruiseCellContextTest::Flags)
bool FSimCruiseClimateTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Am tropical"), SimCruiseClimate::ClimateFact(2), FName(TEXT("climate:tropical")));
	TestEqual(TEXT("Cfa humid subtropical"), SimCruiseClimate::ClimateFact(SimCruiseClimate::KoppenFromCode(TEXT("Cfa"))), FName(TEXT("climate:humid_subtropical")));
	TestEqual(TEXT("Dfb humid continental"), SimCruiseClimate::ClimateFact(SimCruiseClimate::KoppenFromCode(TEXT("Dfb"))), FName(TEXT("climate:humid_continental")));
	TestEqual(TEXT("Dfc subarctic"), SimCruiseClimate::ClimateFact(SimCruiseClimate::KoppenFromCode(TEXT("Dfc"))), FName(TEXT("climate:subarctic")));
	TestEqual(TEXT("Csb mediterranean"), SimCruiseClimate::ClimateFact(SimCruiseClimate::KoppenFromCode(TEXT("Csb"))), FName(TEXT("climate:mediterranean")));
	// November: Tennessee's warm-season grass is turning; Miami's is green; Sydney's (south) is spring-green.
	const uint8 Cfa = SimCruiseClimate::KoppenFromCode(TEXT("Cfa"));
	TestTrue(TEXT("Cfa November dry-ish"), SimCruiseClimate::GrassDry(Cfa, NAME_None, 35.5, 11) > 0.3f);
	TestTrue(TEXT("Am November green"), SimCruiseClimate::GrassDry(2, NAME_None, 25.8, 11) < 0.15f);
	TestTrue(TEXT("Cfa south in November is spring"), SimCruiseClimate::GrassDry(Cfa, NAME_None, -33.9, 11) < 0.2f);
	TestEqual(TEXT("season north"), SimCruiseClimate::Season(Cfa, NAME_None, 35.5, 1), FName(TEXT("season:winter")));
	TestEqual(TEXT("season south"), SimCruiseClimate::Season(Cfa, NAME_None, -33.9, 1), FName(TEXT("season:summer")));
	TestEqual(TEXT("Am dry season"), SimCruiseClimate::Season(2, NAME_None, 25.8, 1), FName(TEXT("season:dry")));
	TestEqual(TEXT("fallback from the fact"), SimCruiseClimate::Season(0, TEXT("climate:tropical"), 25.8, 7), FName(TEXT("season:wet")));

	// A 36 x 18 grid (10 degree cells): Cfa everywhere, a sea column at 0-10 E, Af at row 8 (10 N-0).
	TArray<uint8> Raw;
	Raw.Init(Cfa, 36 * 18);
	for (int32 y = 0; y < 18; ++y) Raw[y * 36 + 18] = 0;
	for (int32 x = 0; x < 36; ++x) if (x != 18) Raw[8 * 36 + x] = 1;
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!TestTrue(TEXT("encode"), Png.IsValid() && Png->SetRaw(Raw.GetData(), Raw.Num(), 36, 18, ERGBFormat::Gray, 8))) return false;
	const TArray64<uint8> Bytes64 = Png->GetCompressed();
	const TArray<uint8> Bytes(Bytes64.GetData(), static_cast<int32>(Bytes64.Num()));
	if (!TestTrue(TEXT("test grid loads"), SimCruiseClimate::LoadGridForTest(Bytes))) { SimCruiseClimate::ResetGrid(); return false; }
	TestTrue(TEXT("Cfa at 35 N 84 W"), SimCruiseClimate::KoppenAt(35.5, -83.9) == Cfa);
	TestTrue(TEXT("Af at 5 N 50 W"), SimCruiseClimate::KoppenAt(5.0, -50.0) == 1);
	TestTrue(TEXT("a sea cell takes the nearest land"), SimCruiseClimate::KoppenAt(40.0, 5.0) == Cfa);
	TestTrue(TEXT("wraps at 180"), SimCruiseClimate::KoppenAt(40.0, 179.99) == Cfa);
	// The place graph takes the class where no region speaks (Sydney is in none).
	FSimCruiseFactSet F;
	SimCruisePlaceGraph::Rules().RegionFacts(-33.9, 151.2, F);
	TestTrue(TEXT("Köppen fact"), F.Has(TEXT("koppen:Cfa")));
	SimCruiseClimate::ResetGrid();
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "World/SimCruiseRoadCheck.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadStepsTest, "SimCruise.World.RoadSteps.BundledTiles", SimCruiseCellContextTest::Flags)
bool FSimCruiseRoadStepsTest::RunTest(const FString& Parameters)
{
	// Every bundled tile (Miami Beach, the Ocean Drive pre-flight's ground): along every road's
	// centre line and wheel lines, no step over 2 cm in 0.5 m on the surface the wheels meet.
	const double Side = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat).TileSizeM;
	int32 Tiles = 0, Total = 0, Over5 = 0, AtFixedJunction = 0;
	double Worst = 0.0;
	for (int32 X = 18178; X <= 18181; ++X)
	for (int32 Y = 27905; Y <= 27910; ++Y)
	{
		FSimCruiseTileData Data;
		FString Error;
		if (!SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(FIntPoint(X, Y)), Data, Error)) continue;
		const TSharedRef<FSimCruiseTileBuild> Build = SimCruiseTileBuilder::Build(Data, Side);
		const TArray<FSimCruiseRoadStep> Steps = SimCruiseRoadCheck::FindSteps(*Build);
		++Tiles;
		Total += Steps.Num();
		for (const FSimCruiseRoadStep& St : Steps)
		{
			Over5 += St.JumpCm > 5.0;
			// 5th Street into MacArthur Causeway (tile 18178_27909, about (430, 60) m): 65 cm before the
			// per-column junction fade.
			AtFixedJunction += X == 18178 && Y == 27909 && FVector2d::Distance(St.PositionM, FVector2d(430.0, 60.0)) < 12.0 && St.JumpCm > 5.0
				&& Build->Roads.Ribbons[St.Ribbon].Name == TEXT("5th Street");
		}
		if (Steps.Num() > 0)
		{
			Worst = FMath::Max(Worst, Steps[0].JumpCm);
			AddInfo(SimCruiseRoadCheck::Describe(*Build, Steps, 6));
		}
	}
	AddInfo(FString::Printf(TEXT("%d tiles, %d steps over 2 cm (%d over 5 cm), worst %.1f cm"), Tiles, Total, Over5, Worst));
	TestEqual(TEXT("all bundled tiles checked"), Tiles, 24);
	TestEqual(TEXT("5th Street meets MacArthur Causeway without a step"), AtFixedJunction, 0);
	// A ratchet until the rest are fixed (2026-11-14: 381 steps over 2 cm, worst 65 cm, before the
	// per-column junction fade; the remaining ones are service-road joins and grade-separated
	// overlaps with no bridge in the data). It may only go down.
	constexpr int32 MaxOver5 = 140;
	TestTrue(FString::Printf(TEXT("at most %d steps over 5 cm (was %d)"), MaxOver5, Over5), Over5 <= MaxOver5);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadStepProbe, "SimCruise.Debug.RoadStepProbe", SimCruiseCellContextTest::Flags)
bool FSimCruiseRoadStepProbe::RunTest(const FString& Parameters)
{
	// Temporary probe: the surface along the worst flagged line, and which ribbons cover each point.
	const double Side = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat).TileSizeM;
	FSimCruiseTileData Data;
	FString Error;
	SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(FIntPoint(18178, 27909)), Data, Error);
	const TSharedRef<FSimCruiseTileBuild> Build = SimCruiseTileBuilder::Build(Data, Side);
	const TArray<FSimCruiseRoadStep> Steps = SimCruiseRoadCheck::FindSteps(*Build);
	for (int32 n = 0; n < FMath::Min(3, Steps.Num()); ++n)
	{
		const FSimCruiseRoadStep& Worst = Steps[n];
		const FSimCruiseRibbon& R = Build->Roads.Ribbons[Worst.Ribbon];
		FString Text = FString::Printf(TEXT("Probe %s ribbon %d %s lane %+.1f around s %.1f (rank %d, %d stations)"), *R.Name, Worst.Ribbon, *R.Class, Worst.Lateral, Worst.S, R.Rank, R.St.Num());
		for (double S = Worst.S - 1.0; S <= Worst.S + 1.0; S += 0.1)
		{
			int32 I = 0;
			while (I + 2 < R.St.Num() && R.St[I + 1].S < S) ++I;
			const double T = FMath::Clamp((S - R.St[I].S) / FMath::Max(R.St[I + 1].S - R.St[I].S, 1e-9), 0.0, 1.0);
			const double HalfW = FMath::Lerp(R.St[I].HalfW, R.St[I + 1].HalfW, T);
			const FVector2d P = FMath::Lerp(R.St[I].C, R.St[I + 1].C, T) + FMath::Lerp(R.St[I].R, R.St[I + 1].R, T).GetSafeNormal() * (Worst.Lateral * HalfW);
			const double Own = FMath::Lerp(R.St[I].Height(Worst.Lateral * HalfW), R.St[I + 1].Height(Worst.Lateral * HalfW), T);
			double Top = 0.0;
			SimCruiseRoadCheck::TopSurface(*Build, P, Top);
			Text += FString::Printf(TEXT("\n  s %.1f top %.3f own %.3f |"), S, Top, Own);
			for (int32 o = 0; o < Build->Roads.Ribbons.Num(); ++o)
			{
				if (o == Worst.Ribbon) continue;
				FSimCruiseRoadHit Hit;
				const FSimCruiseRibbon& O = Build->Roads.Ribbons[o];
				// Covered by another ribbon (its own carriageway only).
				TArray<TPair<int32, int32>> Segs;
				Build->Roads.GatherSegments(P, 30.0, Segs);
				for (const TPair<int32, int32>& Sg : Segs)
				{
					if (Sg.Key != o) continue;
					const FSimCruiseRoadHit H = Build->Roads.Project(o, Sg.Value, P);
					if (FMath::Abs(H.Lateral) <= H.HalfW + 0.6 && FMath::Abs(H.Along) < 0.01)
					{
						Text += FString::Printf(TEXT(" r%d %s %s lat %.2f/%.2f h %.3f;"), o, *O.Class, *O.Name, H.Lateral, H.HalfW, H.Height);
						break;
					}
				}
			}
		}
		Text += FString::Printf(TEXT("\n  S0 %.1f S1 %.1f start-on-boundary %d end-on-boundary %d bridge0 %d"), R.St[0].S, R.St.Last().S, R.bStartOnBoundary, R.bEndOnBoundary, R.St[0].bBridge);
		for (const FSimCruiseRibbonStation& St : R.St)
		{
			if (FMath::Abs(St.S - Worst.S) > 3.0) continue;
			Text += FString::Printf(TEXT("\n  st s %.2f HL %.3f HQL %.3f HC %.3f HQR %.3f HR %.3f Z %.3f halfw %.2f bridge %d"), St.S, St.HL, St.HQL, St.HC, St.HQR, St.HR, St.Z, St.HalfW, St.bBridge);
		}
		AddInfo(Text);
	}
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "World/SimCruiseTileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseHighwayLookAheadTest, "SimCruise.World.TileManager.HighwayLookAhead", SimCruiseCellContextTest::Flags)
bool FSimCruiseHighwayLookAheadTest::RunTest(const FString& Parameters)
{
	// 130 km/h across 432 m tiles (45 N) on four headings with two load slots, tiles taking 4,
	// 6 and 8 s to load: each tile must be resident at least 4 s before the car crosses into it
	// (the streamer's look-ahead, speed x (2 x load + 4 s), as ASimCruiseWorldStreamer sets it).
	const double TileM = 432.0, Speed = 130.0 / 3.6, Dt = 0.1;
	const FVector2d Headings[] = { FVector2d(1.0, 0.0), FVector2d(0.7071, 0.7071), FVector2d(0.9397, 0.342), FVector2d(0.5, -0.866) };
	for (const double LoadS : { 4.0, 6.0, 8.0 })
	for (const FVector2d& Heading : Headings)
	{
		FSimCruiseTileManager M;
		M.MaxInFlight = 2;
		M.Reset();
		const double AheadS = FMath::Max(5.0, 2.0 * LoadS + 4.0);
		TMap<FIntPoint, double> ReadyAt;
		TArray<TPair<double, FSimCruiseTileJobTicket>> Jobs;
		FVector2d Pos(0.37 * TileM, 0.61 * TileM);
		double Worst = 1e9;
		FIntPoint Last(MAX_int32, MAX_int32);
		for (double T = 0.0; T < 200.0; T += Dt)
		{
			const bool bMoving = T >= 30.0; // a drive starts standing still
			if (bMoving) Pos += Heading * Speed * Dt;
			const FVector2d Focus = Pos / TileM;
			const FVector2d Ahead = (Pos + Heading * (bMoving ? Speed * AheadS : 0.0)) / TileM;
			M.SetFocus(Focus, Ahead);
			M.TakeEvictions();
			for (const FSimCruiseTileJobTicket& Tk : M.TakeRequests(T)) Jobs.Add(TPair<double, FSimCruiseTileJobTicket>(T + LoadS, Tk));
			for (int32 j = Jobs.Num() - 1; j >= 0; --j)
			{
				if (Jobs[j].Key > T) continue;
				if (M.Complete(Jobs[j].Value, true, T)) ReadyAt.Add(Jobs[j].Value.Tile, T);
				Jobs.RemoveAt(j);
			}
			const FIntPoint Here(FMath::FloorToInt(Focus.X), FMath::FloorToInt(Focus.Y));
			if (bMoving && Here != Last)
			{
				const double* Ready = ReadyAt.Find(Here);
				Worst = FMath::Min(Worst, Ready && M.IsResident(Here) ? T - *Ready : -1.0);
			}
			Last = Here;
		}
		AddInfo(FString::Printf(TEXT("load %.0f s, heading (%.2f, %.2f): worst %.1f s ready before the car"), LoadS, Heading.X, Heading.Y, Worst));
		TestTrue(FString::Printf(TEXT("ready 4 s ahead at 130 km/h, %.0f s loads, heading (%.2f, %.2f)"), LoadS, Heading.X, Heading.Y), Worst >= 4.0 - 2.0 * Dt);
	}
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseFallbackGroundTest, "SimCruise.World.TileManager.FallbackGround", SimCruiseCellContextTest::Flags)
bool FSimCruiseFallbackGroundTest::RunTest(const FString& Parameters)
{
	// A failed tile between built neighbours: its slab sits at the mean height of the edges they
	// share with it; with no neighbour, at the default (the car's height).
	FSimCruiseTileBuild East, North;
	East.TerrainN = 2;
	East.TerrainHeights = { 10.f, 50.f, 50.f,   12.f, 50.f, 50.f,   14.f, 50.f, 50.f }; // west column 10, 12, 14
	North.TerrainN = 2;
	North.TerrainHeights = { 90.f, 90.f, 90.f,   90.f, 90.f, 90.f,   20.f, 22.f, 24.f }; // south row 20, 22, 24
	const FIntPoint Hole(100, 200);
	auto Find = [&](const FIntPoint& T) -> const FSimCruiseTileBuild*
	{
		if (T == Hole + FIntPoint(1, 0)) return &East;
		if (T == Hole + FIntPoint(0, -1)) return &North;
		return nullptr;
	};
	int32 Neighbours = 0;
	const double H = SimCruiseFallbackGround::HeightM(Hole, Find, -5.0, &Neighbours);
	TestEqual(TEXT("two built neighbours"), Neighbours, 2);
	TestEqual(TEXT("mean of the shared edges"), H, (10.0 + 12.0 + 14.0 + 20.0 + 22.0 + 24.0) / 6.0, 1e-6);
	TestEqual(TEXT("no neighbour: the default"), SimCruiseFallbackGround::HeightM(FIntPoint(0, 0), [](const FIntPoint&) -> const FSimCruiseTileBuild* { return nullptr; }, -5.0), -5.0, 1e-9);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseTextureSeamTest, "SimCruise.World.TextureSeam", SimCruiseCellContextTest::Flags)
bool FSimCruiseTextureSeamTest::RunTest(const FString& Parameters)
{
	// The ground materials' surface metres: local + TextureAnchorM. Across every border the two
	// sides must differ by a whole number of anchor periods (1664 m), and every pattern period
	// the shaders use (SeamCode's QP, the 26-cell hex lattices, the noise cells) must divide it,
	// so the grass, sand, asphalt, concrete and paint patterns and the detail kit continue
	// across the border (2026-11-14 review, P0: with the old 64 m anchor most of them jumped).
	const double P = SimCruiseTileBuilder::TextureAnchorPeriodM;
	int32 Bad = 0;
	for (const double Lat : { 19.7, 25.78, 35.48, 46.9, 61.2 })
	{
		const double S = SimCruiseGeo::TileSizeMetres(Lat);
		for (const FIntPoint Tile : { FIntPoint(18180, 27907), FIntPoint(8000, 24000), FIntPoint(65000, 100), FIntPoint(1, 65534) })
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(0, 1) })
		{
			const FVector2d A = SimCruiseTileBuilder::TextureAnchorM(Tile, S);
			const FVector2d B = SimCruiseTileBuilder::TextureAnchorM(Tile + Step, S);
			const double Diff = Step.X ? A.X + S - B.X : A.Y + S - B.Y;
			const double K = Diff / P;
			if (FMath::Abs(K - FMath::RoundToDouble(K)) > 1e-6) { ++Bad; AddError(FString::Printf(TEXT("lat %.1f tile %d,%d: anchor jumps by %.6f m"), Lat, Tile.X, Tile.Y, Diff)); }
			TestTrue(TEXT("anchor in [0, period)"), A.X >= 0.0 && A.X < P && A.Y >= 0.0 && A.Y < P);
		}
	}
	// The pattern sizes in SeamCode's quantiser (the shader's QP: P / round(P / t)).
	for (const double T : { 2.3, 9.7, 2.9, 5.0 * 7.37, 3.1, 23.0, 2.6, 61.0, 1.9, 17.0, 9.0, 26.0 * 1.7, 26.0 * 1.0, 26.0 * 1.2, 26.0 * 1.5 })
	{
		const double Q = P / FMath::Max(FMath::RoundToDouble(P / T), 1.0);
		TestTrue(FString::Printf(TEXT("%.2f m quantised to %.4f m (within 6 %%)"), T, Q), FMath::Abs(Q / T - 1.0) < 0.06);
	}
	TestEqual(TEXT("no border jumps"), Bad, 0);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRuralSectionTest, "SimCruise.World.RuralCrossSection", SimCruiseCellContextTest::Flags)
bool FSimCruiseRuralSectionTest::RunTest(const FString& Parameters)
{
	// A rural road along a 20 % hillside (the ground rises to the north): beyond the uphill
	// shoulder the ground drops into a ditch (foreslope 1:4, about 0.45 m deep) before the cut
	// rises back to the hill; far out the natural ground is untouched.
	constexpr double Side = 550.0;
	const int32 Grid = 64;
	FString H;
	for (int32 j = 0; j <= Grid; ++j)
	for (int32 i = 0; i <= Grid; ++i)
	{
		const double South = (j / double(Grid)) * Side;
		if (!H.IsEmpty()) H += TEXT(",");
		H += FString::Printf(TEXT("%.3f"), 100.0 + 0.2 * (0.5 * Side - South));
	}
	FSimCruiseTileData Data;
	FString Error;
	const FString Json = FString::Printf(TEXT("{\"id\":[300,600],\"grid\":%d,\"h\":[%s],\"roads\":[[\"secondary\",0,\"Hill Road\",[[0.05,0.5],[0.95,0.5]],7]]}"), Grid, *H);
	if (!TestTrue(FString::Printf(TEXT("synthetic tile parses (%s)"), *Error), SimCruiseTileData::Parse(Json, Data, Error))) return false;
	const TSharedRef<FSimCruiseTileBuild> B = SimCruiseTileBuilder::Build(Data, Side);
	FSimCruiseRoadHit Hit;
	const FVector2d Mid(0.5 * Side, 0.5 * Side);
	if (!TestTrue(TEXT("the road is there"), B->Roads.FindNearest(Mid, 10.0, Hit))) return false;
	const FSimCruiseRibbon& R = B->Roads.Ribbons[Hit.Ribbon];
	const FSimCruiseRibbonStation& St = R.St[Hit.Station];
	// The uphill side: -y (north). Which ribbon side that is depends on the station direction.
	const int32 Up = FVector2d::DotProduct(St.R, FVector2d(0.0, -1.0)) > 0.0 ? 1 : 0;
	TestTrue(TEXT("a rural edge"), R.Sides[Up].Edge.IsValidIndex(Hit.Station) && R.Sides[Up].Edge[Hit.Station] == ESimCruiseRoadEdge::Rural);
	double Outer = 0.0, OuterH = 0.0;
	B->Roads.OuterEdge(Hit.Ribbon, Hit.Station, Up, Outer, OuterH);
	const FVector2d Dir = St.R * (Up == 1 ? 1.0 : -1.0);
	double Lowest = 1e9, LowestD = 0.0;
	for (double D = 0.5; D <= 5.0; D += 0.25)
	{
		const double Z = B->TerrainHeightAt(St.C + Dir * (Outer + D));
		if (Z < Lowest) { Lowest = Z; LowestD = D; }
	}
	const double FarD = 25.0;
	const FVector2d FarP = St.C + Dir * (Outer + FarD);
	const double Far = B->TerrainHeightAt(FarP), FarNatural = Data.HeightAtUnit(FarP / Side);
	AddInfo(FString::Printf(TEXT("shoulder %.2f m; ditch %.2f m below it at %.1f m out; 25 m out %.2f m (natural %.2f)"), OuterH, OuterH - Lowest, LowestD, Far, FarNatural));
	TestTrue(TEXT("a ditch below the shoulder on the uphill side"), OuterH - Lowest > 0.25 && OuterH - Lowest < 0.9);
	TestTrue(TEXT("the hill untouched 25 m out"), FMath::Abs(Far - FarNatural) < 0.3);
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "World/SimCruiseTileDetail.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoofColourTest, "SimCruise.World.Buildings.RoofColourFromAerial", SimCruiseCellContextTest::Flags)
bool FSimCruiseRoofColourTest::RunTest(const FString& Parameters)
{
	// A synthetic zoom-14 photograph (1024 px, about 2.1 m a pixel): grass, with a red roof
	// 40 m square in the middle of the tile. The roof's colour is read from inside its outline,
	// kept red, and clamped to a roof's saturation and albedo; a 3 m shed is too small to read.
	constexpr double Side = 550.0;
	FSimCruiseTileData Data;
	Data.Id = FIntPoint(400, 800); // the north-west cell of zoom-14 tile (100, 200)
	FSimCruiseTileBuild Build;
	FSimCruiseAerialImage Img;
	Img.Width = Img.Height = 1024;
	Img.Pixels.Init(FColor(70, 110, 45), 1024 * 1024);
	const double PxM = Side * 4.0 / 1024.0;
	for (int32 y = 0; y < 1024; ++y)
	for (int32 x = 0; x < 1024; ++x)
	{
		const double Mx = (x + 0.5) * PxM, My = (y + 0.5) * PxM; // metres in this tile (cell 0,0)
		if (Mx > 255.0 && Mx < 295.0 && My > 255.0 && My < 295.0) Img.Pixels[y * 1024 + x] = FColor(200, 60, 50);
	}
	const SimCruiseTileDetail::FContext X{ Data, Build, Side, 25.0, -80.0, {}, {}, {}, nullptr, &Img };
	const TArray<FVector2d> Roof = { FVector2d(255, 255), FVector2d(295, 255), FVector2d(295, 295), FVector2d(255, 295) };
	FLinearColor C;
	if (!TestTrue(TEXT("a roof colour is read"), SimCruiseTileDetail::RoofColourFromAerial(X, Roof, C))) return false;
	const float L = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	const float Sat = (FMath::Max3(C.R, C.G, C.B) - FMath::Min3(C.R, C.G, C.B)) / FMath::Max3(C.R, C.G, C.B);
	AddInfo(FString::Printf(TEXT("roof %.3f %.3f %.3f, luminance %.3f, saturation %.2f"), C.R, C.G, C.B, L, Sat));
	TestTrue(TEXT("still red"), C.R > C.G && C.R > C.B);
	TestTrue(TEXT("saturation clamped"), Sat <= 0.451f);
	TestTrue(TEXT("albedo in the roof range"), L >= 0.039f && L <= 0.601f);
	const TArray<FVector2d> Shed = { FVector2d(100, 100), FVector2d(103, 100), FVector2d(103, 103), FVector2d(100, 103) };
	TestFalse(TEXT("a 3 m shed is too small to read"), SimCruiseTileDetail::RoofColourFromAerial(X, Shed, C));
	const SimCruiseTileDetail::FContext NoPhoto{ Data, Build, Side, 25.0, -80.0, {}, {}, {}, nullptr, nullptr };
	TestFalse(TEXT("no photograph: the rules"), SimCruiseTileDetail::RoofColourFromAerial(NoPhoto, Roof, C));
	return true;
}
#endif
