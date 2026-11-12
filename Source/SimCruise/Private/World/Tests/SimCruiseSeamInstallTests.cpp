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
						WorstBank = FMath::Max(WorstBank, FMath::Abs(SA.E - Sign * SB.E));
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
	TestTrue(FString::Printf(TEXT("the banking meets across the seam (%.5f)"), WorstBank), WorstBank < 1e-3);
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
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
