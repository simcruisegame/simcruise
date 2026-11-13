// Copyright SimCruise contributors. All Rights Reserved.

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseWorldSubsystem.h"
#include "World/SimCruiseWorldResources.h"
#include "World/SimCruiseWorldTile.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruiseSeamInstallTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

	double TileSizeM() { return FSimCruiseWorldFrame::ForPlace(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat).TileSizeM; }

	TSharedRef<FSimCruiseTileBuild> BuildWith(const FIntPoint& Tile, double Side, const TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>>& Done)
	{
		FSimCruiseTileData Data;
		FString Error;
		SimCruiseTileData::LoadFile(SimCruiseTileData::BundledTilePath(Tile), Data, Error);
		const TArray<FSimCruiseSeamEnd> Contracts = SimCruiseTileBuilder::NeighbourContracts(Tile, Side, [&Done](const FIntPoint& T) -> const FSimCruiseTileBuild*
		{
			const TSharedPtr<FSimCruiseTileBuild>* Found = Done.Find(T);
			return Found ? Found->Get() : nullptr;
		});
		return SimCruiseTileBuilder::Build(Data, Side, Contracts);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSeamBankingTest, "SimCruise.World.SeamBanking", SimCruiseSeamInstallTest::Flags)
bool FSimCruiseSeamBankingTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseSeamInstallTest;
	// The start tile, then its east and south neighbours built to its contracts.
	const double Side = TileSizeM();
	const FIntPoint Start(18180, 27908);
	TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>> Done;
	Done.Add(Start, BuildWith(Start, Side, Done));
	int32 Pairs = 0, Lifts = 0;
	double WorstBank = 0.0, WorstGrade = 0.0;
	for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(-1, 0), FIntPoint(0, -1) })
	{
		const FIntPoint Next = Start + Step;
		if (!SimCruiseTileData::HasBundledTile(Next)) continue;
		const TSharedRef<FSimCruiseTileBuild> B = BuildWith(Next, Side, Done);
		const FSimCruiseTileBuild& A = *Done[Start];
		const FVector2d Shift(Step.X * Side, Step.Y * Side);
		for (const FSimCruiseRibbon& RB : B->Roads.Ribbons)
		{
			for (int32 End = 0; End < 2; ++End)
			{
				if (!(End == 0 ? RB.bStartOnBoundary : RB.bEndOnBoundary)) continue;
				const FSimCruiseRibbonStation& SB = End == 0 ? RB.St[0] : RB.St.Last();
				// The same point on the start tile.
				const FVector2d OnA = SB.C + Shift;
				for (const FSimCruiseRibbon& RA : A.Roads.Ribbons)
				{
					for (int32 EndA = 0; EndA < 2; ++EndA)
					{
						if (!(EndA == 0 ? RA.bStartOnBoundary : RA.bEndOnBoundary)) continue;
						const FSimCruiseRibbonStation& SA = EndA == 0 ? RA.St[0] : RA.St.Last();
						if (FVector2d::Distance(SA.C, OnA) > 0.05 || FMath::Abs(FVector2d::DotProduct(SA.T, SB.T)) < 0.7) continue;
						++Pairs;
						// The seam section stores the banking it has, the same seen from either side.
						const double Sign = FVector2d::DotProduct(SA.T, SB.T) >= 0.0 ? 1.0 : -1.0;
						// The cross-slope as built (edge heights), which is what the tyres meet.
						const double XA = (SA.HL - SA.HR) / (2.0 * FMath::Max(SA.HalfW, 0.1)), XB = (SB.HL - SB.HR) / (2.0 * FMath::Max(SB.HalfW, 0.1));
						WorstBank = FMath::Max(WorstBank, FMath::Abs(XA - Sign * XB));
					}
				}
			}
		}
		Done.Add(Next, B);
	}
	// Seam pins change the grade by 1 % at most: the profile's slope near an uncontracted end
	// on the start tile differs from the slope 60 m in by no more than the lift allows.
	for (const FSimCruiseRibbon& R : Done[Start]->Roads.Ribbons)
	{
		for (int32 k = 1; k < R.St.Num(); ++k)
		{
			const double Ds = R.St[k].S - R.St[k - 1].S;
			if (Ds < 1.0) continue;
			const double FromEnd = FMath::Min(R.St[k].S - R.St[0].S, R.St.Last().S - R.St[k].S);
			if (FromEnd > 30.0) continue;
			if (!(R.bStartOnBoundary || R.bEndOnBoundary)) continue;
			++Lifts;
			WorstGrade = FMath::Max(WorstGrade, FMath::Abs(R.St[k].Z - R.St[k - 1].Z) / Ds);
		}
	}
	AddInfo(FString::Printf(TEXT("%d seam pairs, worst banking mismatch %.5f; worst grade within 30 m of a seam %.3f over %d steps"), Pairs, WorstBank, WorstGrade, Lifts));
	TestTrue(TEXT("seams found"), Pairs > 3);
	// Contracted ends meet exactly; an end whose neighbour found no contract (a corner, a
	// split) is solved on each side alone and may differ by about a centimetre at the edges.
	TestTrue(FString::Printf(TEXT("the banking meets across the seam (%.5f)"), WorstBank), WorstBank < 3e-3);
	TestTrue(FString::Printf(TEXT("no steep pin ramp at a seam (%.3f)"), WorstGrade), WorstGrade < 0.12);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStepInstallTest, "SimCruise.World.StepInstall", SimCruiseSeamInstallTest::Flags)
bool FSimCruiseStepInstallTest::RunTest(const FString& Parameters)
{
	using namespace SimCruiseSeamInstallTest;
	// A tile installed without the drawn resources (headless) is complete at once; its
	// collision is there after one install.
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("SimCruiseStepInstallTest"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	{
		const double Side = TileSizeM();
		TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>> None;
		const TSharedRef<FSimCruiseTileBuild> Build = BuildWith(FIntPoint(18180, 27908), Side, None);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASimCruiseWorldTile* Tile = World->SpawnActor<ASimCruiseWorldTile>(ASimCruiseWorldTile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		Tile->Install(Build, nullptr, nullptr, false);
		TestFalse(TEXT("headless: no steps left after the install"), Tile->HasPendingInstall());
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestTrue(TEXT("headless: collision ready"), Tile->IsCollisionReady());
		// ContinueInstall with nothing left does nothing.
		TestTrue(TEXT("nothing to continue"), Tile->ContinueInstall(5.0) < 5.0);
		Tile->Destroy();

		// With the world's resources the install is staged: steps remain, the tile is not
		// collision-ready until they are done, and each ContinueInstall runs at least one.
		USimCruiseWorldResources* Resources = NewObject<USimCruiseWorldResources>();
		Resources->AddToRoot();
		Resources->Initialise();
		ASimCruiseWorldTile* Staged = World->SpawnActor<ASimCruiseWorldTile>(ASimCruiseWorldTile::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
		Staged->Install(BuildWith(FIntPoint(18180, 27908), Side, None), Resources, nullptr, false);
		TestTrue(TEXT("staged: steps remain after the install"), Staged->HasPendingInstall());
		TestFalse(TEXT("staged: not collision-ready while steps remain"), Staged->IsCollisionReady());
		int32 Steps = 0;
		while (Staged->HasPendingInstall() && Steps < 100)
		{
			Staged->ContinueInstall(0.0); // a zero budget still runs one step
			++Steps;
		}
		AddInfo(FString::Printf(TEXT("%d install steps"), Steps));
		TestTrue(TEXT("staged: several steps (collision sections, one cook, palms, signs)"), Steps >= 3 && Steps < 100);
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		TestTrue(TEXT("staged: collision ready once the steps are done"), Staged->IsCollisionReady());
		Staged->Destroy();
		Resources->RemoveFromRoot();
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseSlopedSeamTest, "SimCruise.World.SlopedSeam", SimCruiseSeamInstallTest::Flags)
bool FSimCruiseSlopedSeamTest::RunTest(const FString& Parameters)
{
	// Two small synthetic tiles on a hillside: the ground rises 6 % east and falls 8 % across
	// the road (north to south), and one road runs east across their shared edge.
	constexpr double Side = 550.0;
	auto TileJson = [](int32 X, double Z0, const TCHAR* Road)
	{
		FString H;
		const int32 Grid = 8;
		for (int32 j = 0; j <= Grid; ++j)
		for (int32 i = 0; i <= Grid; ++i)
		{
			const double East = (i / double(Grid)) * Side, South = (j / double(Grid)) * Side;
			if (!H.IsEmpty()) H += TEXT(",");
			H += FString::Printf(TEXT("%.3f"), Z0 + 0.06 * East - 0.08 * (South - 0.5 * Side));
		}
		return FString::Printf(TEXT("{\"id\":[%d,500],\"grid\":%d,\"h\":[%s],\"roads\":[%s]}"), X, Grid, *H, Road);
	};
	FSimCruiseTileData A, B;
	FString Error;
	const bool bA = SimCruiseTileData::Parse(TileJson(100, 0.0, TEXT("[\"tertiary\",0,\"Hill Road\",[[0.2,0.5],[0.6,0.52],[1.0,0.5]],7]")), A, Error);
	const bool bB = SimCruiseTileData::Parse(TileJson(101, 0.06 * Side, TEXT("[\"tertiary\",0,\"Hill Road\",[[0.0,0.5],[0.4,0.48],[0.8,0.5]],7]")), B, Error);
	if (!TestTrue(FString::Printf(TEXT("synthetic tiles parse (%s)"), *Error), bA && bB)) return false;
	TMap<FIntPoint, TSharedPtr<FSimCruiseTileBuild>> Done;
	const TSharedRef<FSimCruiseTileBuild> BA = SimCruiseTileBuilder::Build(A, Side);
	Done.Add(A.Id, BA);
	const TArray<FSimCruiseSeamEnd> Contracts = SimCruiseTileBuilder::NeighbourContracts(B.Id, Side, [&Done](const FIntPoint& T) -> const FSimCruiseTileBuild*
	{
		const TSharedPtr<FSimCruiseTileBuild>* Found = Done.Find(T);
		return Found ? Found->Get() : nullptr;
	});
	const TSharedRef<FSimCruiseTileBuild> BB = SimCruiseTileBuilder::Build(B, Side, Contracts);
	const FSimCruiseRibbon* RA = BA->Roads.Ribbons.FindByPredicate([](const FSimCruiseRibbon& R) { return R.bEndOnBoundary || R.bStartOnBoundary; });
	const FSimCruiseRibbon* RB = BB->Roads.Ribbons.FindByPredicate([](const FSimCruiseRibbon& R) { return R.bEndOnBoundary || R.bStartOnBoundary; });
	if (!TestNotNull(TEXT("road on tile A"), RA) || !TestNotNull(TEXT("road on tile B"), RB)) return false;
	const FSimCruiseRibbonStation& EA = RA->bEndOnBoundary ? RA->St.Last() : RA->St[0];
	const FSimCruiseRibbonStation& EB = RB->bStartOnBoundary ? RB->St[0] : RB->St.Last();
	// The sections meet exactly, banking and all.
	const bool bSame = FVector2d::DotProduct(EA.T, EB.T) >= 0.0;
	TestTrue(FString::Printf(TEXT("centre heights meet (%.4f / %.4f)"), EA.HC, EB.HC), FMath::Abs(EA.HC - EB.HC) < 1e-3);
	TestTrue(TEXT("edge heights meet"), FMath::Abs((bSame ? EA.HL : EA.HR) - EB.HL) < 1e-3 && FMath::Abs((bSame ? EA.HR : EA.HL) - EB.HR) < 1e-3);
	TestTrue(TEXT("the seam keeps its banking"), FMath::Abs((bSame ? EA.E : -EA.E) - EB.E) < 1e-3);
	// Grade change from the seam pins: at most 1 % plus the hill's own change of grade.
	double Worst = 0.0;
	for (const FSimCruiseRibbon* R : { RA, RB })
	{
		for (int32 k = 2; k < R->St.Num(); ++k)
		{
			const double D1 = R->St[k - 1].S - R->St[k - 2].S, D2 = R->St[k].S - R->St[k - 1].S;
			if (D1 < 1.0 || D2 < 1.0) continue;
			const double G1 = (R->St[k - 1].Z - R->St[k - 2].Z) / D1, G2 = (R->St[k].Z - R->St[k - 1].Z) / D2;
			Worst = FMath::Max(Worst, FMath::Abs(FMath::Abs(G2) - 0.06));
		}
	}
	AddInfo(FString::Printf(TEXT("worst departure from the hill's 6 %% grade: %.4f"), Worst));
	// Every boundary end keeps its clearance: the shared ground stays under both edges of the
	// seam section by the shoulder drop (0.05 m), across the whole carriageway.
	double WorstClear = 1e9;
	for (const TPair<const FSimCruiseTileData*, const FSimCruiseTileBuild*>& T : { TPair<const FSimCruiseTileData*, const FSimCruiseTileBuild*>(&A, &BA.Get()), TPair<const FSimCruiseTileData*, const FSimCruiseTileBuild*>(&B, &BB.Get()) })
	{
		for (const FSimCruiseRibbon& R : T.Value->Roads.Ribbons)
		{
			for (int32 End = 0; End < 2; ++End)
			{
				if (!(End == 0 ? R.bStartOnBoundary : R.bEndOnBoundary)) continue;
				const FSimCruiseRibbonStation& St = End == 0 ? R.St[0] : R.St.Last();
				for (double L = -St.HalfW; L <= St.HalfW + 1e-6; L += 0.5)
				{
					const double Ground = T.Key->HeightAtUnit((St.C + St.R * L) / Side);
					WorstClear = FMath::Min(WorstClear, St.Height(L) - Ground);
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("least clearance over the ground at a seam: %.3f m"), WorstClear));
	TestTrue(FString::Printf(TEXT("the road clears the ground at the seams (%.3f m)"), WorstClear), WorstClear >= 0.04);
	TestTrue(FString::Printf(TEXT("seam pins bend the grade by about 1 %% at most (%.4f)"), Worst), Worst < 0.02);
	return true;
}

#endif
