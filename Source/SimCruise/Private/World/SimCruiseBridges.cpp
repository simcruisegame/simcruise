// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseBridges.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseBridges, Log, All);

namespace
{
	constexpr double DeckDepthM = 1.2;
	constexpr double ParapetM = 0.85;       // 32 in F-shape barrier
	constexpr double ParapetWidthM = 0.4;
	constexpr double PierSpacingM = 35.0;
	constexpr double PierMinHeightM = 4.0;
	constexpr double PierHalfM = 0.6;

	struct FBuffers
	{
		TArray<FVector> P, N;
		TArray<int32> I;
		TArray<FVector2D> UV;
		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Facing)
		{
			const int32 Base = P.Num();
			P.Append({ A, B, C, D });
			N.Append({ Facing, Facing, Facing, Facing });
			UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			const FVector Face = FVector::CrossProduct(B - A, C - A);
			if (FVector::DotProduct(Face, Facing) >= 0.0) I.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
			else I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}
	};
}

FString ASimCruiseBridges::MaterialPath()
{
	return TEXT("/Game/World/Materials/M_SimCruiseBridgeConcrete.M_SimCruiseBridgeConcrete");
}

UMaterial* ASimCruiseBridges::BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags)
{
#if WITH_EDITOR
	UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
	Material->TwoSided = true;
	auto Add = [Material](auto* E) { Material->GetExpressionCollection().AddExpression(E); return E; };
	// Weathered structural concrete (albedo about 0.35).
	UMaterialExpressionConstant3Vector* Base = Add(NewObject<UMaterialExpressionConstant3Vector>(Material));
	Base->Constant = FLinearColor(0.34f, 0.33f, 0.31f);
	UMaterialExpressionConstant* Rough = Add(NewObject<UMaterialExpressionConstant>(Material));
	Rough->R = 0.9f;
	Material->GetEditorOnlyData()->BaseColor.Connect(0, Base);
	Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	return Material;
#else
	return nullptr;
#endif
}

ASimCruiseBridges::ASimCruiseBridges()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseBridges::BuildTile(const FIntPoint& Id)
{
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	const ASimCruiseWorldTile* Tile = Stream ? Stream->GetTile(Id) : nullptr;
	const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
	if (!Build) return;
	const double Side = Build->TileSizeM;
	auto GroundM = [&](const FVector2d& P) { return Build->TerrainHeightAt(FVector2d(FMath::Clamp(P.X, 0.0, Side), FMath::Clamp(P.Y, 0.0, Side))); };
	auto Cm = [](const FVector2d& P, double Z) { return FVector(P.X * 100.0, P.Y * 100.0, Z * 100.0); };
	FBuffers B;
	double Metres = 0.0;
	for (const FSimCruiseRibbon& Ribbon : Build->Roads.Ribbons)
	{
		const int32 N = Ribbon.St.Num();
		for (int32 k = 0; k < N; ++k)
		{
			if (!Ribbon.St[k].bBridge) continue;
			int32 e = k;
			while (e + 1 < N && Ribbon.St[e + 1].bBridge) ++e;
			// The span's stations, one beyond each end where there is one, so the deck meets the road.
			const int32 A = FMath::Max(k - 1, 0), Z = FMath::Min(e + 1, N - 1);
			double SincePier = PierSpacingM * 0.5;
			for (int32 j = A; j < Z; ++j)
			{
				const FSimCruiseRibbonStation& S0 = Ribbon.St[j];
				const FSimCruiseRibbonStation& S1 = Ribbon.St[j + 1];
				Metres += S1.S - S0.S;
				for (int32 Edge = -1; Edge <= 1; Edge += 2)
				{
					const FVector2d E0 = S0.C + S0.R * (Edge * S0.HalfW), E1 = S1.C + S1.R * (Edge * S1.HalfW);
					const FVector2d O0 = S0.C + S0.R * (Edge * (S0.HalfW + ParapetWidthM)), O1 = S1.C + S1.R * (Edge * (S1.HalfW + ParapetWidthM));
					const double Z0 = S0.Height(Edge * S0.HalfW), Z1 = S1.Height(Edge * S1.HalfW);
					const FVector Out = FVector(S0.R.X, S0.R.Y, 0.0) * Edge;
					// Parapet: inner face, top, outer face down to the deck's underside.
					B.Quad(Cm(E0, Z0), Cm(E1, Z1), Cm(E1, Z1 + ParapetM), Cm(E0, Z0 + ParapetM), -Out);
					B.Quad(Cm(E0, Z0 + ParapetM), Cm(E1, Z1 + ParapetM), Cm(O1, Z1 + ParapetM), Cm(O0, Z0 + ParapetM), FVector::UpVector);
					B.Quad(Cm(O0, Z0 + ParapetM), Cm(O1, Z1 + ParapetM), Cm(O1, Z1 - DeckDepthM), Cm(O0, Z0 - DeckDepthM), Out);
				}
				// The deck's underside.
				const FVector2d L0 = S0.C - S0.R * (S0.HalfW + ParapetWidthM), L1 = S1.C - S1.R * (S1.HalfW + ParapetWidthM);
				const FVector2d R0 = S0.C + S0.R * (S0.HalfW + ParapetWidthM), R1 = S1.C + S1.R * (S1.HalfW + ParapetWidthM);
				B.Quad(Cm(L0, S0.HC - DeckDepthM), Cm(R0, S0.HC - DeckDepthM), Cm(R1, S1.HC - DeckDepthM), Cm(L1, S1.HC - DeckDepthM), -FVector::UpVector);
				// Piers where the deck stands high over the ground.
				SincePier += S1.S - S0.S;
				if (SincePier >= PierSpacingM && j > A && j + 1 < Z)
				{
					SincePier = 0.0;
					const double Top = S0.HC - DeckDepthM, Ground = GroundM(S0.C);
					if (Top - Ground > PierMinHeightM)
					{
						const FVector2d T = S0.T * PierHalfM, R = S0.R * FMath::Max(S0.HalfW * 0.6, 2.0);
						const FVector2d C[4] = { S0.C - T - R, S0.C + T - R, S0.C + T + R, S0.C - T + R };
						for (int32 f = 0; f < 4; ++f)
						{
							const FVector2d P0 = C[f], P1 = C[(f + 1) % 4];
							const FVector2d Mid = (P0 + P1) * 0.5 - S0.C;
							B.Quad(Cm(P0, Ground - 1.0), Cm(P1, Ground - 1.0), Cm(P1, Top), Cm(P0, Top), FVector(Mid.X, Mid.Y, 0.0).GetSafeNormal());
						}
					}
				}
			}
			k = e;
		}
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->bUseAsyncCooking = true;
	Mesh->RegisterComponent();
	Mesh->SetWorldLocation(Tile->GetActorLocation());
	if (B.I.Num() > 0)
	{
		Mesh->CreateMeshSection(0, B.P, B.I, B.N, B.UV, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		if (Material) Mesh->SetMaterial(0, Material);
		UE_LOG(LogSimCruiseBridges, Display, TEXT("Tile %d_%d: %.0f m of bridge deck"), Id.X, Id.Y, Metres);
	}
	Built.Add(Id, Mesh);
}

void ASimCruiseBridges::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream || !Stream->GetFrame().IsValid()) return;
	if (!Material)
	{
#if WITH_EDITOR
		if (FApp::CanEverRender()) Material = BuildMaterial(this, TEXT("M_SimCruiseBridgeConcrete"), RF_Transient);
#else
		Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
#endif
	}
	if (Stream->GetTitle() != SeenTitle)
	{
		SeenTitle = Stream->GetTitle();
		for (const TPair<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Pair : Built) if (Pair.Value) Pair.Value->DestroyComponent();
		Built.Reset();
	}
	for (auto It = Built.CreateIterator(); It; ++It)
	{
		const ASimCruiseWorldTile* Tile = Stream->GetTile(It.Key());
		if (!Tile || !Tile->GetBuild())
		{
			if (It.Value()) It.Value()->DestroyComponent();
			It.RemoveCurrent();
			continue;
		}
		if (It.Value() && !It.Value()->GetComponentLocation().Equals(Tile->GetActorLocation(), 1.0))
		{
			It.Value()->SetWorldLocation(Tile->GetActorLocation(), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const APawn* Car = Player ? Player->GetPawn() : nullptr;
	if (!Car) return;
	const FIntPoint Here = Stream->GetFrame().TileAtWorldCm(Car->GetActorLocation());
	for (int32 dx = -1; dx <= 1; ++dx)
	for (int32 dy = -1; dy <= 1; ++dy)
	{
		const FIntPoint Id = Here + FIntPoint(dx, dy);
		const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
		if (Tile && Tile->GetBuild() && !Built.Contains(Id)) { BuildTile(Id); return; }
	}
}
