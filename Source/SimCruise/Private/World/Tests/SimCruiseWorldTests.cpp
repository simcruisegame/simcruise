// Copyright SimCruise contributors. All Rights Reserved.

#include "Async/ParallelFor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileManager.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"
#include "World/SimCruiseWorldTile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseWorldTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	double MiamiTileSizeM()
	{
		return FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat).TileSizeM;
	}

	/** Every bundled zoom-16 tile (game/data/<x>_<y>.json). */
	TArray<FIntPoint> BundledTiles()
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("game"), TEXT("data"), TEXT("*_*.json")), true, false);
		TArray<FIntPoint> Out;
		for (const FString& File : Files)
		{
			FString X, Y;
			if (FPaths::GetBaseFilename(File).Split(TEXT("_"), &X, &Y) && X.IsNumeric() && Y.IsNumeric())
			{
				Out.Add(FIntPoint(FCString::Atoi(*X), FCString::Atoi(*Y)));
			}
		}
		Out.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
		return Out;
	}

	/**
	 * Build tiles the way the streamer does: one after another, each reading the
	 * seam contracts of the neighbours already built (the start tile first, then
	 * outward), so seams are checked as a drive produces them.
	 */
	TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>> BuildAll(TArray<FIntPoint> Tiles, double TileSizeM, TArray<FString>& Errors)
	{
		const FIntPoint Start(18180, 27908);
		Tiles.Sort([Start](const FIntPoint& A, const FIntPoint& B)
		{
			const int32 DA = FMath::Max(FMath::Abs(A.X - Start.X), FMath::Abs(A.Y - Start.Y));
			const int32 DB = FMath::Max(FMath::Abs(B.X - Start.X), FMath::Abs(B.Y - Start.Y));
			if (DA != DB) return DA < DB;
			return A.X != B.X ? A.X < B.X : A.Y < B.Y;
		});
		TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>> Out;
		for (const FIntPoint& Tile : Tiles)
		{
			FSimCruiseTileData Data;
			FString Error;
			if (!SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(Tile), Data, Error)) { Errors.Add(Error); continue; }
			const TArray<FSimCruiseSeamEnd> Contracts = SimCruiseTileBuilder::NeighbourContracts(Tile, TileSizeM, [&Out](const FIntPoint& T) -> const FSimCruiseTileBuild*
			{
				const TSharedPtr<FSimCruiseTileBuild>* Found = Out.Find(T);
				return Found ? Found->Get() : nullptr;
			});
			Out.Add(Tile, SimCruiseTileBuilder::Build(Data, TileSizeM, Contracts));
		}
		return Out;
	}

	/** A transient game world for spawning tiles and tracing their collision. */
	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SimCruiseWorldTest"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

using namespace SimCruiseWorldTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseTileMathTest, "SimCruise.World.TileMath", SimCruiseWorldTest::Flags)
bool FSimCruiseTileMathTest::RunTest(const FString& Parameters)
{
	struct FPlace { const TCHAR* Name; double Lat; double Lon; };
	const FPlace Places[] = {
		{ TEXT("Ocean Drive, Miami Beach"), 25.78000, -80.13025 },
		{ TEXT("Key West"), 24.5551, -81.7800 },
		{ TEXT("Anchorage"), 61.2181, -149.9003 },
		{ TEXT("Honolulu"), 21.3069, -157.8583 },
		{ TEXT("Bethel, Maine"), 44.4040, -70.7909 },
		{ TEXT("Death Valley"), 36.4614, -116.8656 },
	};
	for (const FPlace& P : Places)
	{
		const FVector2d Tile = SimCruiseGeo::LonLatToTile(P.Lon, P.Lat);
		double Lon = 0.0, Lat = 0.0;
		SimCruiseGeo::TileToLonLat(Tile, Lon, Lat);
		TestTrue(FString::Printf(TEXT("%s: lon/lat round trip (%.3g, %.3g deg)"), P.Name, Lon - P.Lon, Lat - P.Lat), FMath::Abs(Lon - P.Lon) < 1e-9 && FMath::Abs(Lat - P.Lat) < 1e-9);

		FSimCruiseWorldFrame Frame = FSimCruiseWorldFrame::ForPlace(P.Lon, P.Lat);
		const FVector World = Frame.LonLatToWorldCm(P.Lon, P.Lat, 12.5);
		TestTrue(FString::Printf(TEXT("%s: the start lies in the origin tile"), P.Name), Frame.TileAtWorldCm(World) == Frame.OriginTile);
		double Lon2 = 0.0, Lat2 = 0.0;
		Frame.WorldCmToLonLat(World, Lon2, Lat2);
		const double ErrorM = FMath::Sqrt(FMath::Square((Lon2 - P.Lon) * 111320.0 * FMath::Cos(FMath::DegreesToRadians(P.Lat))) + FMath::Square((Lat2 - P.Lat) * 110540.0));
		TestTrue(FString::Printf(TEXT("%s: world round trip within 1 mm (%.2e m)"), P.Name, ErrorM), ErrorM < 1e-3);
		TestTrue(TEXT("height carried in cm"), FMath::IsNearlyEqual(World.Z, 1250.0));

		// Floating origin: moving the origin by whole tiles keeps every place where it was.
		const FVector2d Before = Frame.WorldCmToTileCoord(FVector2d(World.X, World.Y));
		const FVector Offset = Frame.RebaseTo(Frame.OriginTile + FIntPoint(3, -2));
		const FVector Moved = World + Offset;
		const FVector2d After = Frame.WorldCmToTileCoord(FVector2d(Moved.X, Moved.Y));
		TestTrue(FString::Printf(TEXT("%s: rebasing keeps the tile coordinate (%.2e tiles)"), P.Name, FVector2d::Distance(Before, After)), FVector2d::Distance(Before, After) < 1e-9);
		TestTrue(TEXT("rebase offset is whole tiles"), FMath::IsNearlyEqual(Offset.X, -3.0 * Frame.TileSizeCm(), 1e-6) && FMath::IsNearlyEqual(Offset.Y, 2.0 * Frame.TileSizeCm(), 1e-6));
	}

	// The tile size Drive.gd uses for Miami Beach: 40075016.686 / 65536 * cos(25.78 deg).
	const double Side = MiamiTileSizeM();
	const double Expected = 40075016.686 / 65536.0 * FMath::Cos(FMath::DegreesToRadians(25.78));
	TestTrue(FString::Printf(TEXT("Miami Beach tile is %.3f m as in Drive.gd (%.3f)"), Expected, Side), FMath::Abs(Side - Expected) < 1e-3);
	const FSimCruiseWorldFrame Miami = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat);
	TestTrue(FString::Printf(TEXT("the bundled start is tile %s"), *SimCruiseTileName(Miami.OriginTile)), Miami.OriginTile == FIntPoint(18180, 27908));
	TestTrue(TEXT("aerial z14 of a z16 tile"), SimCruiseGeo::AerialTileOf(FIntPoint(18179, 27907)) == FIntPoint(4544, 6976));
	TestTrue(TEXT("negative floors"), SimCruiseGeo::AerialTileOf(FIntPoint(-1, -5)) == FIntPoint(-1, -2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseTileParseTest, "SimCruise.World.TileParse", SimCruiseWorldTest::Flags)
bool FSimCruiseTileParseTest::RunTest(const FString& Parameters)
{
	FSimCruiseTileData Data;
	FString Error;
	TestTrue(TEXT("bundled Ocean Drive tile parses"), SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(FIntPoint(18180, 27907)), Data, Error));
	TestTrue(TEXT("grid and heights"), Data.IsValid() && Data.Grid == 64);
	TestTrue(TEXT("roads read"), Data.Roads.Num() > 10);
	bool bOcean = false;
	for (const FSimCruiseRoadRow& R : Data.Roads) bOcean |= R.Name == TEXT("Ocean Drive");
	TestTrue(TEXT("Ocean Drive is on the start tile"), bOcean);
	TestTrue(TEXT("aerial key"), Data.AerialKey == TEXT("a-4545-6976.jpg"));
	TestFalse(TEXT("bundled aerial is found"), SimCruiseTileData::BundledAerialPath(Data.AerialKey).IsEmpty());

	FSimCruiseTileData Bad;
	TestFalse(TEXT("not JSON is rejected"), SimCruiseTileData::Parse(TEXT("{oops"), Bad, Error));
	TestFalse(TEXT("missing heights are rejected"), SimCruiseTileData::Parse(TEXT("{\"id\":[1,2],\"grid\":2,\"h\":[0,0],\"roads\":[]}"), Bad, Error));
	TestTrue(TEXT("the error names the field"), Error.Contains(TEXT("tile.h")));
	TestFalse(TEXT("a bad road row is rejected"), SimCruiseTileData::Parse(TEXT("{\"id\":[1,2],\"grid\":1,\"h\":[0,0,0,0],\"roads\":[[\"minor\",0,\"x\",\"no\",8]]}"), Bad, Error));
	TestTrue(TEXT("a minimal tile parses"), SimCruiseTileData::Parse(TEXT("{\"id\":[1,2],\"grid\":1,\"h\":[0,1,2,3],\"roads\":[]}"), Bad, Error));
	TestTrue(TEXT("bilinear heights"), FMath::IsNearlyEqual(Bad.HeightAtUnit(FVector2d(0.5, 0.5)), 1.5, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseOfflineStartTest, "SimCruise.World.OfflineStart", SimCruiseWorldTest::Flags)
bool FSimCruiseOfflineStartTest::RunTest(const FString& Parameters)
{
	// The ring around the bundled start is in the repository: the first drive needs no network.
	const FSimCruiseWorldFrame Frame = FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat);
	for (int32 dy = -1; dy <= 1; ++dy)
	for (int32 dx = -1; dx <= 1; ++dx)
	{
		const FIntPoint Tile = Frame.OriginTile + FIntPoint(dx, dy);
		TestTrue(FString::Printf(TEXT("tile %s is bundled"), *SimCruiseTileName(Tile)), SimCruiseTileData::HasBundledTile(Tile));
	}

	// Stream it for real in a game world with the helper switched off.
	FTestWorld TestWorld;
	ASimCruiseWorldStreamer* Streamer = TestWorld.World->SpawnActor<ASimCruiseWorldStreamer>();
	Streamer->bUseHelper = false;
	Streamer->StartAt(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat, TEXT("test"));
	const FVector Start = Streamer->GetFrame().LonLatToWorldCm(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat);
	const double T0 = FPlatformTime::Seconds();
	int32 MaxTiles = 0;
	while (!Streamer->AreTilesReadyAround(Start, 1) && FPlatformTime::Seconds() - T0 < 120.0)
	{
		Streamer->TickStreaming(0.016f);
		MaxTiles = FMath::Max(MaxTiles, Streamer->NumTiles());
		FPlatformProcess::Sleep(0.005f);
	}
	const double Seconds = FPlatformTime::Seconds() - T0;
	TestTrue(FString::Printf(TEXT("3 x 3 tiles streamed offline (%d in %.1f s)"), Streamer->NumTiles(), Seconds), Streamer->AreTilesReadyAround(Start, 1));
	TestTrue(TEXT("never more resident tiles than the bound"), MaxTiles <= 16);

	FVector Pose;
	FRotator Rotation;
	TestTrue(TEXT("Ocean Drive found near the start"), Streamer->FindRoadPose(TEXT("Ocean Drive"), Start, 900.0, Pose, Rotation));
	double Z = 0.0;
	TestTrue(TEXT("surface height under the pose"), Streamer->SurfaceHeightAt(Pose, Z) && FMath::Abs(Z - Pose.Z) < 2.0);
	AddInfo(FString::Printf(TEXT("Ocean Drive pose %s yaw %.1f, %d tiles"), *Pose.ToString(), Rotation.Yaw, Streamer->NumTiles()));
	Streamer->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSeamContinuityTest, "SimCruise.World.SeamContinuity", SimCruiseWorldTest::Flags)
bool FSimCruiseSeamContinuityTest::RunTest(const FString& Parameters)
{
	const double Side = MiamiTileSizeM();
	TArray<FString> Errors;
	const TArray<FIntPoint> Tiles = BundledTiles();
	TestTrue(TEXT("bundled tiles found"), Tiles.Num() >= 9);
	const TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>> Built = BuildAll(Tiles, Side, Errors);
	for (const FString& E : Errors) AddError(E);

	double WorstTerrain = 0.0, WorstRoad = 0.0, WorstEdge = 0.0;
	int32 Pairs = 0, Ends = 0, Matched = 0;
	for (const auto& Pair : Built)
	{
		const FSimCruiseTileBuild& A = *Pair.Value;
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(0, 1) })
		{
			const TSharedPtr<FSimCruiseTileBuild>* Other = Built.Find(Pair.Key + Step);
			if (!Other) continue;
			const FSimCruiseTileBuild& B = **Other;
			++Pairs;
			const int32 N = A.TerrainN, S = N + 1;
			if (B.TerrainN != N) { AddError(TEXT("terrain grids differ")); continue; }
			for (int32 k = 0; k <= N; ++k)
			{
				const float HA = Step.X ? A.TerrainHeights[k * S + N] : A.TerrainHeights[N * S + k];
				const float HB = Step.X ? B.TerrainHeights[k * S + 0] : B.TerrainHeights[0 * S + k];
				const double D = FMath::Abs(HA - HB);
				if (D > 0.01 && D > WorstTerrain)
				{
					AddInfo(FString::Printf(TEXT("terrain step %.3f m between %s and %s at node %d (%.3f / %.3f, natural %.3f)"), D, *SimCruiseTileName(Pair.Key), *SimCruiseTileName(Pair.Key + Step), k, HA, HB,
						Step.X ? A.NaturalHeights[k * S + N] : A.NaturalHeights[N * S + k]));
				}
				WorstTerrain = FMath::Max(WorstTerrain, D);
			}
			// Road ends on the shared edge must meet a road end of the neighbour.
			const FVector2d Shift(Step.X * Side, Step.Y * Side);
			for (const FSimCruiseRibbon& RA : A.Roads.Ribbons)
			{
				for (int32 EndA = 0; EndA < 2; ++EndA)
				{
					if (!(EndA == 0 ? RA.bStartOnBoundary : RA.bEndOnBoundary)) continue;
					const FSimCruiseRibbonStation& SA = EndA == 0 ? RA.St[0] : RA.St.Last();
					const bool bOnEdge = Step.X ? SA.C.X >= Side - 1e-3 : SA.C.Y >= Side - 1e-3;
					if (!bOnEdge) continue;
					// Roads that cross the seam (not ones running along it).
					if (FMath::Abs(Step.X ? SA.T.X : SA.T.Y) < 0.5) continue;
					++Ends;
					const FVector2d Across = SA.C - Shift;
					const FSimCruiseRibbonStation* SB = nullptr;
					const FSimCruiseRibbon* RBest = nullptr;
					double Nearest = 0.05;
					for (const FSimCruiseRibbon& RB : B.Roads.Ribbons)
					{
						for (int32 EndB = 0; EndB < 2; ++EndB)
						{
							if (!(EndB == 0 ? RB.bStartOnBoundary : RB.bEndOnBoundary)) continue;
							const FSimCruiseRibbonStation& Cand = EndB == 0 ? RB.St[0] : RB.St.Last();
							if (FMath::Abs(FVector2d::DotProduct(Cand.T, SA.T)) < 0.7) continue; // another road meeting there
							const double D = FVector2d::Distance(Cand.C, Across);
							if (D < Nearest) { Nearest = D; SB = &Cand; RBest = &RB; }
						}
					}
					if (!SB)
					{
						FString Near;
						for (const FSimCruiseRibbon& RB : B.Roads.Ribbons)
						{
							for (int32 i = 0; i < RB.St.Num(); ++i)
							{
								const double D = FVector2d::Distance(RB.St[i].C, Across);
								if (D < 3.0) Near += FString::Printf(TEXT(" [%s st %d/%d d=%.2f start=%d end=%d]"), *RB.Name, i, RB.St.Num(), D, RB.bStartOnBoundary, RB.bEndOnBoundary);
							}
						}
						AddInfo(FString::Printf(TEXT("%s: %s ends on the edge at (%.2f, %.2f) without a partner;%s"), *SimCruiseTileName(Pair.Key), *RA.Name, SA.C.X, SA.C.Y, *Near));
						continue;
					}
					++Matched;
					const double Before = FMath::Max(WorstRoad, WorstEdge);
					WorstRoad = FMath::Max(WorstRoad, FMath::Abs(SA.HC - SB->HC));
					// Edges by position (the neighbour may have chained the road the other way).
					for (const double SignA : { -1.0, 1.0 })
					{
						const FVector2d PA = SA.C + SA.R * (SignA * SA.HalfW) - Shift;
						const double HA = SignA < 0 ? SA.HL : SA.HR;
						const FVector2d PB1 = SB->C - SB->R * SB->HalfW, PB2 = SB->C + SB->R * SB->HalfW;
						const bool bFirst = FVector2d::Distance(PA, PB1) < FVector2d::Distance(PA, PB2);
						const double HB = bFirst ? SB->HL : SB->HR;
						WorstEdge = FMath::Max(WorstEdge, FMath::Min(FVector2d::Distance(PA, PB1), FVector2d::Distance(PA, PB2)));
						WorstRoad = FMath::Max(WorstRoad, FMath::Abs(HA - HB));
					}
					if (FMath::Max(WorstRoad, WorstEdge) > FMath::Max(Before, 0.01))
					{
						AddInfo(FString::Printf(TEXT("seam mismatch %s|%s on %s/%s: centre %.3f/%.3f, half width %.2f/%.2f, T (%.3f,%.3f)/(%.3f,%.3f)"),
							*SimCruiseTileName(Pair.Key), *SimCruiseTileName(Pair.Key + Step), *RA.Name, *RBest->Name, SA.HC, SB->HC, SA.HalfW, SB->HalfW, SA.T.X, SA.T.Y, SB->T.X, SB->T.Y));
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d tile pairs; %d road ends on seams, %d matched; worst terrain %.4f m, road %.4f m, edge offset %.4f m"), Pairs, Ends, Matched, WorstTerrain, WorstRoad, WorstEdge));
	TestTrue(TEXT("tile pairs checked"), Pairs >= 12);
	TestTrue(FString::Printf(TEXT("terrain meets across every seam within 1 cm (%.4f m)"), WorstTerrain), WorstTerrain <= 0.01);
	TestTrue(FString::Printf(TEXT("road surface meets across every seam within 1 cm (%.4f m)"), WorstRoad), WorstRoad <= 0.01);
	TestTrue(FString::Printf(TEXT("road edges meet within 2 cm (%.4f m)"), WorstEdge), WorstEdge <= 0.02);
	TestTrue(FString::Printf(TEXT("road ends find their continuation (%d of %d)"), Matched, Ends), Ends > 20 && Matched >= Ends * 0.9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadCollisionTest, "SimCruise.World.RoadCollision", SimCruiseWorldTest::Flags)
bool FSimCruiseRoadCollisionTest::RunTest(const FString& Parameters)
{
	const double Side = MiamiTileSizeM();
	FTestWorld TestWorld;
	int32 Checked = 0, Missed = 0, Off = 0;
	double Worst = 0.0;
	for (const FIntPoint TileId : { FIntPoint(18180, 27907), FIntPoint(18179, 27908) })
	{
		FSimCruiseTileData Data;
		FString Error;
		if (!TestTrue(TEXT("tile loads"), SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(TileId), Data, Error))) return false;
		TSharedRef<FSimCruiseTileBuild> Build = SimCruiseTileBuilder::Build(Data, Side);
		const TArray<FVector> Points = Build->CarriagewayPoints;
		TestTrue(TEXT("the tile has road surface"), Points.Num() > 500);
		ASimCruiseWorldTile* Tile = TestWorld.World->SpawnActor<ASimCruiseWorldTile>(ASimCruiseWorldTile::StaticClass(), FVector(0, 0, 0), FRotator::ZeroRotator);
		Tile->Install(Build, nullptr, nullptr, false);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruiseRoadCollision), true);
		for (const FVector& P : Points)
		{
			++Checked;
			// A vertex on the outer border of the mesh sits exactly on a triangle edge;
			// rays 1.5 cm around it count, as a tyre's contact patch would.
			bool bHit = false;
			double D = TNumericLimits<double>::Max();
			for (const FVector2D Nudge : { FVector2D(0, 0), FVector2D(1.5, 0), FVector2D(-1.5, 0), FVector2D(0, 1.5), FVector2D(0, -1.5) })
			{
				FHitResult Hit;
				const FVector Q = P + FVector(Nudge.X, Nudge.Y, 0.0);
				if (Tile->Mesh->LineTraceComponent(Hit, Q + FVector(0, 0, 200), Q - FVector(0, 0, 200), Params))
				{
					bHit = true;
					D = FMath::Min(D, FMath::Abs(Hit.ImpactPoint.Z - P.Z));
				}
			}
			if (!bHit)
			{
				++Missed;
				if (Missed <= 5) AddInfo(FString::Printf(TEXT("no collision at %s on %s"), *P.ToString(), *SimCruiseTileName(TileId)));
				continue;
			}
			Worst = FMath::Max(Worst, D);
			if (D > 1.0)
			{
				++Off;
				FSimCruiseRoadHit RoadHit;
				const bool bOnOther = Build->Roads.FindCarriageway(FVector2d(P.X, P.Y) / 100.0, RoadHit, INDEX_NONE, 0.0);
				if (Off <= 8) AddInfo(FString::Printf(TEXT("surface %.1f cm off at %s (%s); highest carriageway there %.1f cm"), D, *P.ToString(), *SimCruiseTileName(TileId), bOnOther ? RoadHit.Height * 100.0 - P.Z : 0.0));
			}
		}
		Tile->Destroy();
	}
	AddInfo(FString::Printf(TEXT("%d carriageway vertices traced: %d without collision, %d more than 1 cm off (worst %.2f cm)"), Checked, Missed, Off, Worst));
	TestEqual(TEXT("collision under every carriageway vertex"), Missed, 0);
	TestTrue(FString::Printf(TEXT("collision is the drawn surface (%d of %d off by > 1 cm)"), Off, Checked), Off <= Checked / 200);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseRoadEdgesTest, "SimCruise.World.RoadEdges", SimCruiseWorldTest::Flags)
bool FSimCruiseRoadEdgesTest::RunTest(const FString& Parameters)
{
	const double Side = MiamiTileSizeM();

	// A straight road mapped with jittered vertices builds dead straight edges.
	{
		FSimCruiseTileData Data;
		Data.Id = FIntPoint(100, 100);
		Data.Grid = 4;
		Data.Heights.Init(1.f, 25);
		FSimCruiseRoadRow Row;
		Row.Class = TEXT("primary");
		Row.WidthM = 12.0;
		for (int32 k = 0; k <= 10; ++k) Row.Points.Add(FVector2d(0.1 + 0.08 * k, 0.5));
		Data.Roads.Add(Row);
		FSimCruiseRoadNetwork Net;
		Net.Build(Data, Side);
		TestEqual(TEXT("one ribbon"), Net.Ribbons.Num(), 1);
		double Worst = 0.0;
		for (const FSimCruiseRibbonStation& St : Net.Ribbons[0].St)
		{
			Worst = FMath::Max(Worst, FMath::Abs((St.C + St.R * St.HalfW).Y - (0.5 * Side + 6.0)));
			Worst = FMath::Max(Worst, FMath::Abs((St.C - St.R * St.HalfW).Y - (0.5 * Side - 6.0)));
		}
		TestTrue(FString::Printf(TEXT("straight road edges are straight (%.5f m)"), Worst), Worst < 0.001);
	}

	// A sharp bend is one smooth arc: no fold, no kink above a few degrees per station.
	{
		FSimCruiseTileData Data;
		Data.Id = FIntPoint(100, 100);
		Data.Grid = 4;
		Data.Heights.Init(1.f, 25);
		FSimCruiseRoadRow Row;
		Row.Class = TEXT("minor");
		Row.WidthM = 8.0;
		Row.Points = { FVector2d(0.2, 0.2), FVector2d(0.5, 0.2), FVector2d(0.5, 0.5) };
		Data.Roads.Add(Row);
		FSimCruiseRoadNetwork Net;
		Net.Build(Data, Side);
		double MaxTurn = 0.0;
		const TArray<FSimCruiseRibbonStation>& St = Net.Ribbons[0].St;
		for (int32 i = 1; i + 1 < St.Num(); ++i)
		{
			const double A = FMath::Atan2(St[i].T.Y, St[i].T.X), B = FMath::Atan2(St[i + 1].T.Y, St[i + 1].T.X);
			MaxTurn = FMath::Max(MaxTurn, FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(FMath::Sin(B - A), FMath::Cos(B - A)))));
		}
		TestTrue(FString::Printf(TEXT("a 90 degree corner turns smoothly (%.1f deg per station)"), MaxTurn), MaxTurn < 20.0);
	}

	// No zig-zags on any bundled road: an edge never turns one way and straight back.
	TArray<FString> Errors;
	const TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>> Built = BuildAll(BundledTiles(), Side, Errors);
	int32 Zigzags = 0, Visible = 0, Segments = 0;
	double WorstWiggle = 0.0;
	for (const auto& Pair : Built)
	{
		for (const FSimCruiseRibbon& Ribbon : Pair.Value->Roads.Ribbons)
		{
			TArray<FVector2d> Tangents;
			for (const FSimCruiseRibbonStation& St : Ribbon.St) Tangents.Add(St.T);
			for (const double Sign : { -1.0, 0.0, 1.0 })
			{
				// The edge as drawn: the mesh's offset line, unfolded on tight insides.
				TArray<FVector2d> Raw;
				for (const FSimCruiseRibbonStation& St : Ribbon.St) Raw.Add(St.C + St.R * (Sign * St.HalfW));
				if (Sign != 0.0) SimCruiseTileBuilder::UnfoldLine(Raw, Tangents);
				TArray<FVector2d> Line;
				for (const FVector2d& P : Raw)
				{
					if (Line.Num() == 0 || FVector2d::Distance(Line.Last(), P) > 0.05) Line.Add(P);
				}
				TArray<double> Turn;
				for (int32 i = 1; i + 1 < Line.Num(); ++i)
				{
					const FVector2d A = (Line[i] - Line[i - 1]).GetSafeNormal(), B = (Line[i + 1] - Line[i]).GetSafeNormal();
					Turn.Add(FMath::RadiansToDegrees(FMath::Atan2(A.X * B.Y - A.Y * B.X, FVector2d::DotProduct(A, B))));
					++Segments;
				}
				for (int32 i = 0; i + 1 < Turn.Num(); ++i)
				{
					if (Turn[i] * Turn[i + 1] < 0.0)
					{
						const double Wiggle = FMath::Min(FMath::Abs(Turn[i]), FMath::Abs(Turn[i + 1]));
						WorstWiggle = FMath::Max(WorstWiggle, Wiggle);
						// Counted from 2 degrees (reported); a visible sawtooth (over 8 degrees on
						// public roads, 16 on service ways mapped with tight corners) fails.
						if (Wiggle > (Ribbon.bService ? 16.0 : 8.0)) ++Visible;
						if (Wiggle > (Ribbon.bService ? 10.0 : 2.0))
						{
							++Zigzags;
							if (Zigzags <= 12) AddInfo(FString::Printf(TEXT("zig-zag on %s (%s) at %s near (%.1f, %.1f), edge %+.0f: %.1f / %.1f deg"), *Ribbon.Name, *Ribbon.Class, *SimCruiseTileName(Pair.Key), Line[i + 1].X, Line[i + 1].Y, Sign, Turn[i], Turn[i + 1]));
						}
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d edge segments; %d small back-and-forth kinks over 2 deg (10 on service ways); %d visible; worst %.2f deg"), Segments, Zigzags, Visible, WorstWiggle));
	TestTrue(TEXT("edges measured"), Segments > 10000);
	TestEqual(TEXT("no visible zig-zag on any road edge"), Visible, 0);
	TestTrue(FString::Printf(TEXT("small kinks are rare (%d per 100k segments)"), Zigzags * 100000 / FMath::Max(Segments, 1)), Zigzags * 10000 < Segments);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStaleJobsTest, "SimCruise.World.StaleJobs", SimCruiseWorldTest::Flags)
bool FSimCruiseStaleJobsTest::RunTest(const FString& Parameters)
{
	FSimCruiseTileManager M;
	M.RingRadius = 1;
	M.MaxInFlight = 2;
	M.Reset();
	M.SetFocus(FVector2d(100.5, 200.5), FVector2d(100.5, 200.5));
	TArray<FSimCruiseTileJobTicket> First = M.TakeRequests(0.0);
	TestEqual(TEXT("bounded jobs in flight"), First.Num(), 2);
	TestTrue(TEXT("the car's own tile first"), First.Num() > 0 && First[0].Tile == FIntPoint(100, 200));
	TestEqual(TEXT("no more while busy"), M.TakeRequests(0.0).Num(), 0);

	// A new place: everything older is rejected when it lands.
	M.Reset();
	M.SetFocus(FVector2d(5000.5, 6000.5), FVector2d(5000.5, 6000.5));
	TestFalse(TEXT("a job of the previous place is stale"), M.Complete(First[0], true, 1.0));
	TestFalse(TEXT("so is its sibling"), M.IsCurrent(First[1]));
	TestFalse(TEXT("and it never becomes resident"), M.IsResident(First[0].Tile));

	// Driving away: a tile that left the ring before it finished is rejected.
	TArray<FSimCruiseTileJobTicket> Here = M.TakeRequests(2.0);
	TestEqual(TEXT("new place requests"), Here.Num(), 2);
	M.SetFocus(FVector2d(5010.5, 6000.5), FVector2d(5010.5, 6000.5));
	TestFalse(TEXT("a tile no longer wanted is rejected"), M.Complete(Here[0], true, 3.0));
	TestFalse(TEXT("and not installed"), M.IsResident(Here[0].Tile));

	// A superseded ticket for the same tile is rejected; the current one lands.
	M.Reset();
	M.SetFocus(FVector2d(10.5, 10.5), FVector2d(10.5, 10.5));
	TArray<FSimCruiseTileJobTicket> A = M.TakeRequests(0.0);
	FSimCruiseTileJobTicket Old = A[0];
	Old.Serial += 1000;
	TestFalse(TEXT("unknown serial rejected"), M.Complete(Old, true, 0.0));
	TestTrue(TEXT("current job accepted"), M.Complete(A[0], true, 0.0));
	TestTrue(TEXT("resident"), M.IsResident(A[0].Tile));
	TestFalse(TEXT("a second result for a resident tile is rejected"), M.Complete(A[0], true, 0.0));

	// A failure waits before retrying.
	TestFalse(TEXT("failure is not installed"), M.Complete(A[1], false, 10.0));
	bool bRetriedEarly = false;
	for (const FSimCruiseTileJobTicket& T : M.TakeRequests(11.0)) bRetriedEarly |= T.Tile == A[1].Tile;
	TestFalse(TEXT("no retry within the delay"), bRetriedEarly);

	// A long random drive never holds more than the bound.
	M.Reset();
	FRandomStream Rng(7);
	FVector2d P(300.5, 300.5);
	int32 MaxResident = 0;
	for (int32 Step = 0; Step < 2000; ++Step)
	{
		P += FVector2d(Rng.FRandRange(-0.2, 0.25), Rng.FRandRange(-0.2, 0.25));
		M.SetFocus(P, P);
		M.TakeEvictions();
		for (const FSimCruiseTileJobTicket& T : M.TakeRequests(Step)) M.Complete(T, true, Step);
		MaxResident = FMath::Max(MaxResident, M.NumResident());
	}
	TestTrue(FString::Printf(TEXT("resident tiles bounded (%d <= %d)"), MaxResident, M.MaxResident()), MaxResident <= M.MaxResident());
	return true;
}

#endif
