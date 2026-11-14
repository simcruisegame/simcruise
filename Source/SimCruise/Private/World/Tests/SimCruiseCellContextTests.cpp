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
		for (int32 i = Range.X; i < Range.X + Range.Y; ++i) if (C.Cells[i].S1 - C.Cells[i].S0 > SimCruiseCellContext::CellM + 1e-3) ++Uncovered;
	}
	TestEqual(TEXT("every road covered by cells of 32 m or less"), Uncovered, 0);
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
