// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseGuardRails.h"
#include "World/SimCruiseCellContext.h"

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

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseRails, Log, All);

namespace
{
	constexpr double ProbeM = 8.0;          // natural ground this far beyond the shoulder
	constexpr double DropM = 3.0;           // steeper than a 1:3 slope over the probe
	constexpr double MinRunM = 12.0;
	constexpr double LeadM = 8.0;
	constexpr double RailOffsetM = 0.35;
	constexpr double PostSpacingM = 1.905;  // 6 ft 3 in
	constexpr double RailTopM = 0.79;
	constexpr double RailDepthM = 0.31;

	struct FBuffers
	{
		TArray<FVector> P;
		TArray<FVector> N;
		TArray<int32> I;
		TArray<FVector2D> UV;

		void Quad(const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal)
		{
			const int32 Base = P.Num();
			P.Append({ A, B, C, D });
			N.Append({ Normal, Normal, Normal, Normal });
			UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			const FVector Face = FVector::CrossProduct(B - A, C - A);
			if (FVector::DotProduct(Face, Normal) >= 0.0) I.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
			else I.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
		}

		/** An upright box (post): centre of its foot, half sizes. */
		void Box(const FVector& Foot, double Half, double Height)
		{
			const FVector X(Half, 0, 0), Y(0, Half, 0), Z(0, 0, Height);
			const FVector A = Foot - X - Y, B = Foot + X - Y, C = Foot + X + Y, D = Foot - X + Y;
			Quad(A, B, B + Z, A + Z, -FVector::YAxisVector);
			Quad(B, C, C + Z, B + Z, FVector::XAxisVector);
			Quad(C, D, D + Z, C + Z, FVector::YAxisVector);
			Quad(D, A, A + Z, D + Z, -FVector::XAxisVector);
			Quad(A + Z, B + Z, C + Z, D + Z, FVector::ZAxisVector);
		}
	};
}

TArray<FIntPoint> ASimCruiseGuardRails::HazardRuns(const TArray<double>& StationS, const TArray<double>& Drop)
{
	TArray<FIntPoint> Runs;
	const int32 N = FMath::Min(StationS.Num(), Drop.Num());
	int32 i = 0;
	while (i < N)
	{
		if (Drop[i] <= DropM) { ++i; continue; }
		int32 j = i;
		while (j + 1 < N && Drop[j + 1] > DropM) ++j;
		if (StationS[j] - StationS[i] >= MinRunM)
		{
			// Start LeadM before the hazard and end LeadM after it.
			int32 a = i, b = j;
			while (a > 0 && StationS[i] - StationS[a - 1] <= LeadM) --a;
			while (b + 1 < N && StationS[b + 1] - StationS[j] <= LeadM) ++b;
			// Close runs merge.
			if (Runs.Num() && StationS[a] - StationS[Runs.Last().Y] <= LeadM) Runs.Last().Y = b;
			else Runs.Add(FIntPoint(a, b));
		}
		i = j + 1;
	}
	return Runs;
}

FString ASimCruiseGuardRails::MaterialPath()
{
	return TEXT("/Game/World/Materials/M_SimCruiseGuardRail.M_SimCruiseGuardRail");
}

UMaterial* ASimCruiseGuardRails::BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags)
{
#if WITH_EDITOR
	UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
	Material->TwoSided = true;
	auto Add = [Material](auto* Expression) { Material->GetExpressionCollection().AddExpression(Expression); return Expression; };
	// Weathered hot-dip galvanised steel: light grey, mostly metal, satin.
	UMaterialExpressionConstant3Vector* Base = Add(NewObject<UMaterialExpressionConstant3Vector>(Material));
	Base->Constant = FLinearColor(0.52f, 0.53f, 0.53f);
	UMaterialExpressionConstant* Metal = Add(NewObject<UMaterialExpressionConstant>(Material));
	Metal->R = 0.85f;
	UMaterialExpressionConstant* Rough = Add(NewObject<UMaterialExpressionConstant>(Material));
	Rough->R = 0.5f;
	Material->GetEditorOnlyData()->BaseColor.Connect(0, Base);
	Material->GetEditorOnlyData()->Metallic.Connect(0, Metal);
	Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	return Material;
#else
	return nullptr;
#endif
}

ASimCruiseGuardRails::ASimCruiseGuardRails()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseGuardRails::BuildTile(const FIntPoint& Id)
{
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	const ASimCruiseWorldTile* Tile = Stream ? Stream->GetTile(Id) : nullptr;
	const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
	if (!Build) return;
	const double Began = FPlatformTime::Seconds();
	const FVector Origin = Tile->GetActorLocation();
	// The tile's terrain grid (a lookup, not a search of the carriageways): probes past the
	// tile's edge read its border.
	const double Side_ = Build->TileSizeM;
	auto Ground = [&](const FVector2d& LocalM, double& OutM)
	{
		if (LocalM.X < -20.0 || LocalM.Y < -20.0 || LocalM.X > Side_ + 20.0 || LocalM.Y > Side_ + 20.0) return false;
		OutM = Build->TerrainHeightAt(FVector2d(FMath::Clamp(LocalM.X, 0.0, Side_), FMath::Clamp(LocalM.Y, 0.0, Side_)));
		return true;
	};
	FBuffers B;
	double Metres = 0.0;
	FString FirstRail;
	const FSimCruiseTileContext* Ctx = Build->Context.Get();
	for (const FSimCruiseRibbon& Ribbon : Build->Roads.Ribbons)
	{
		if (Ribbon.bService || Ribbon.St.Num() < 3) continue;
		const int32 RibbonIndex = static_cast<int32>(&Ribbon - Build->Roads.Ribbons.GetData());
		TArray<double> S;
		for (const FSimCruiseRibbonStation& St : Ribbon.St) S.Add(St.S);
		for (int32 Side = -1; Side <= 1; Side += 2)
		{
			TArray<double> Drop;
			Drop.Init(0.0, Ribbon.St.Num());
			for (int32 k = 0; k < Ribbon.St.Num(); ++k)
			{
				const FSimCruiseRibbonStation& St = Ribbon.St[k];
				// Not across a junction: the cut stations are the crossing road.
				const bool bCut = Ribbon.Sides[Side > 0 ? 1 : 0].Cut.IsValidIndex(k) && Ribbon.Sides[Side > 0 ? 1 : 0].Cut[k];
				double Edge = 0.0, Out = 0.0;
				if (bCut || !Ground(St.C + St.R * Side * (St.HalfW + RailOffsetM), Edge) || !Ground(St.C + St.R * Side * (St.HalfW + RailOffsetM + ProbeM), Out)) continue;
				Drop[k] = Edge - Out;
				// OpenStreetMap maps a guard rail beside this station (the tile's context record):
				// a rail there, whatever the drop (bridges' approaches, curves, medians).
				if (Ctx)
				{
					const FSimCruiseCellContext* Cell = Ctx->At(RibbonIndex, St.S);
					if (Cell && Cell->Sides[Side > 0 ? 1 : 0].Barrier == TEXT("guard_rail")) Drop[k] = FMath::Max(Drop[k], 10.0);
				}
			}
			for (const FIntPoint& Run : HazardRuns(S, Drop))
			{
				if (Metres == 0.0)
				{
					double Lon = 0.0, Lat = 0.0;
					Stream->GetFrame().WorldCmToLonLat(Origin + FVector(Ribbon.St[Run.X].C.X * 100.0, Ribbon.St[Run.X].C.Y * 100.0, 0.0), Lon, Lat);
					UE_LOG(LogSimCruiseRails, Verbose, TEXT("Guard rail on %s from %.6f,%.6f"), *Ribbon.Name, Lat, Lon);
					FirstRail = FString::Printf(TEXT("%.6f,%.6f %s"), Lat, Lon, *Ribbon.Name);
				}
				// The rail follows the shoulder.
				TArray<FVector> Line;
				for (int32 k = Run.X; k <= Run.Y; ++k)
				{
					const FSimCruiseRibbonStation& St = Ribbon.St[k];
					const FVector2d At = St.C + St.R * Side * (St.HalfW + RailOffsetM);
					// On the shoulder: the carriageway's edge height.
					const double Z = St.Height(Side * St.HalfW);
					Line.Add(FVector(At.X * 100.0, At.Y * 100.0, Z * 100.0));
				}
				double Since = PostSpacingM;
				for (int32 k = 0; k + 1 < Line.Num(); ++k)
				{
					const FVector A = Line[k], C = Line[k + 1];
					const FVector Along = (C - A).GetSafeNormal2D();
					const FVector Out = FVector(-Along.Y, Along.X, 0.0) * Side;
					const double Len = FVector::Dist(A, C) / 100.0;
					Metres += Len;
					const FVector Top(0, 0, RailTopM * 100.0), Bottom(0, 0, (RailTopM - RailDepthM) * 100.0);
					// The W-beam: a face toward the road and one toward the drop, and its top.
					const FVector Back = Out * 8.0;
					B.Quad(A + Bottom, C + Bottom, C + Top, A + Top, -Out);
					B.Quad(A + Bottom + Back, C + Bottom + Back, C + Top + Back, A + Top + Back, Out);
					B.Quad(A + Top, C + Top, C + Top + Back, A + Top + Back, FVector::ZAxisVector);
					// Posts behind the beam.
					for (double D = PostSpacingM - Since; D < Len; D += PostSpacingM)
					{
						const FVector Foot = FMath::Lerp(A, C, D / FMath::Max(Len, 1e-3)) + Out * 14.0 - FVector(0, 0, 30.0);
						B.Box(Foot, 7.5, (RailTopM - 0.02) * 100.0 + 30.0);
						Since = Len - D;
					}
					Since += Len;
					Since = FMath::Fmod(Since, PostSpacingM);
				}
			}
		}
	}
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetupAttachment(RootComponent);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->bUseAsyncCooking = true;
	Mesh->RegisterComponent();
	Mesh->SetWorldLocation(Origin);
	if (B.I.Num() > 0)
	{
		Mesh->CreateMeshSection(0, B.P, B.I, B.N, B.UV, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		if (Material) Mesh->SetMaterial(0, Material);
	}
	Built.Add(Id, Mesh);
	RailM += Metres;
	if (Metres > 0.0)
	{
		UE_LOG(LogSimCruiseRails, Display, TEXT("Tile %d_%d: %.0f m of guard rail where the ground drops more than %.0f m beside the road (%.0f ms), first at %s"), Id.X, Id.Y, Metres, DropM, (FPlatformTime::Seconds() - Began) * 1000.0, *FirstRail);
	}
}

void ASimCruiseGuardRails::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream || !Stream->GetFrame().IsValid()) return;
	if (!Material)
	{
#if WITH_EDITOR
		if (FApp::CanEverRender()) Material = BuildMaterial(this, TEXT("M_SimCruiseGuardRail"), RF_Transient);
#else
		Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
#endif
	}
	if (Stream->GetTitle() != SeenTitle)
	{
		SeenTitle = Stream->GetTitle();
		for (const TPair<FIntPoint, TObjectPtr<UProceduralMeshComponent>>& Pair : Built)
		{
			if (Pair.Value) Pair.Value->DestroyComponent();
		}
		Built.Reset();
		RailM = 0.0;
	}
	// Tiles gone: their rails go. Tiles here: rails follow them (the floating origin) or get built.
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
	int32 Budget = 1; // one tile a tick: the ground probes are a few thousand traces
	for (int32 dx = -1; dx <= 1 && Budget > 0; ++dx)
	for (int32 dy = -1; dy <= 1 && Budget > 0; ++dy)
	{
		const FIntPoint Id = Here + FIntPoint(dx, dy);
		const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
		if (!Tile || !Tile->GetBuild() || Built.Contains(Id)) continue;
		BuildTile(Id);
		--Budget;
	}
}
