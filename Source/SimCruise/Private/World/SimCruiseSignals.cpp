// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseSignals.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"
#include "ProceduralMeshComponent.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseSignals, Log, All);

namespace
{
	bool IsSignalClass(const FString& Class)
	{
		// Through roads of collector class and above; motorways have no at-grade junctions.
		return Class == TEXT("trunk") || Class == TEXT("primary") || Class == TEXT("secondary") || Class == TEXT("tertiary");
	}

	struct FBuffers
	{
		TArray<FVector> P, N;
		TArray<int32> I;
		TArray<FVector2D> UV;

		/** A box between two points (its axis), Half wide and Thick deep across it. */
		void Box(const FVector& From, const FVector& To, const FVector& Across, double Half, double Thick)
		{
			const FVector Axis = (To - From).GetSafeNormal();
			const FVector U = Across.GetSafeNormal() * Half;
			const FVector V = FVector::CrossProduct(Axis, Across).GetSafeNormal() * Thick;
			const FVector C[8] = { From - U - V, From + U - V, From + U + V, From - U + V, To - U - V, To + U - V, To + U + V, To - U + V };
			Face(C[0], C[1], C[5], C[4]); Face(C[1], C[2], C[6], C[5]); Face(C[2], C[3], C[7], C[6]); Face(C[3], C[0], C[4], C[7]);
			Face(C[4], C[5], C[6], C[7]); Face(C[3], C[2], C[1], C[0]);
		}

		void Face(const FVector& A, const FVector& B, const FVector& C, const FVector& D)
		{
			const FVector Normal = FVector::CrossProduct(B - A, D - A).GetSafeNormal();
			const int32 Base = P.Num();
			P.Append({ A, B, C, D });
			N.Append({ Normal, Normal, Normal, Normal });
			UV.Append({ FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1) });
			I.Append({ Base, Base + 2, Base + 1, Base, Base + 3, Base + 2 });
		}
	};
}

ASimCruiseSignals::ASimCruiseSignals()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Signals"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetMobility(EComponentMobility::Movable);
	RootComponent = Mesh;
}

ASimCruiseSignals* ASimCruiseSignals::Find(const UWorld* World)
{
	if (!World) return nullptr;
	for (TActorIterator<ASimCruiseSignals> It(const_cast<UWorld*>(World)); It; ++It) return *It;
	return nullptr;
}

FString ASimCruiseSignals::MaterialPath()
{
	return TEXT("/Game/World/Materials/M_SimCruiseSignal.M_SimCruiseSignal");
}

UMaterial* ASimCruiseSignals::BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags)
{
#if WITH_EDITOR
	UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
	auto Add = [Material](auto* E) { Material->GetExpressionCollection().AddExpression(E); return E; };
	UMaterialExpressionVectorParameter* Colour = Add(NewObject<UMaterialExpressionVectorParameter>(Material));
	Colour->ParameterName = TEXT("Colour");
	Colour->DefaultValue = FLinearColor(0.03f, 0.03f, 0.03f);
	UMaterialExpressionScalarParameter* Glow = Add(NewObject<UMaterialExpressionScalarParameter>(Material));
	Glow->ParameterName = TEXT("Glow");
	Glow->DefaultValue = 0.f;
	UMaterialExpressionMultiply* Emissive = Add(NewObject<UMaterialExpressionMultiply>(Material));
	Emissive->A.Connect(0, Colour);
	Emissive->B.Connect(0, Glow);
	UMaterialExpressionConstant* Rough = Add(NewObject<UMaterialExpressionConstant>(Material));
	Rough->R = 0.45f;
	Material->GetEditorOnlyData()->BaseColor.Connect(0, Colour);
	Material->GetEditorOnlyData()->EmissiveColor.Connect(0, Emissive);
	Material->GetEditorOnlyData()->Roughness.Connect(0, Rough);
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	return Material;
#else
	return nullptr;
#endif
}

ESimCruiseSignalAspect ASimCruiseSignals::AspectAt(double CycleSeconds, int32 Axis)
{
	const double Half = GreenS + YellowS + AllRedS;
	double T = FMath::Fmod(CycleSeconds, CycleS);
	if (T < 0.0) T += CycleS;
	// Axis 0 runs in the first half of the cycle, axis 1 in the second.
	const double Local = Axis == 0 ? T : FMath::Fmod(T + Half, CycleS);
	if (Local < GreenS) return ESimCruiseSignalAspect::Green;
	if (Local < GreenS + YellowS) return ESimCruiseSignalAspect::Yellow;
	return ESimCruiseSignalAspect::Red;
}

ESimCruiseSignalAspect ASimCruiseSignals::AspectFor(const FSimCruiseSignalJunction& J, const FVector2D& Dir) const
{
	const int32 Axis = FMath::Abs(FVector2D::DotProduct(Dir, J.Axis[0])) >= FMath::Abs(FVector2D::DotProduct(Dir, J.Axis[1])) ? 0 : 1;
	return AspectAt(Clock + J.Offset, Axis);
}

bool ASimCruiseSignals::IsStopFor(const FVector& PositionCm, const FVector2D& Dir, double LookAheadM, FVector& OutStopLineCm) const
{
	constexpr double YellowCommitM = 25.0; // closer than this to the line at yellow, go on
	double Best = TNumericLimits<double>::Max();
	bool bStop = false;
	for (const FSimCruiseSignalJunction& J : Junctions)
	{
		const FVector2D ToC = FVector2D(J.CentreCm.X - PositionCm.X, J.CentreCm.Y - PositionCm.Y) / 100.0;
		const double Along = FVector2D::DotProduct(ToC, Dir);
		const double Lateral = FMath::Abs(FVector2D::CrossProduct(Dir, ToC));
		if (Along <= 0.0 || Along > LookAheadM + 20.0 || Lateral > 14.0) continue;
		const int32 Axis = FMath::Abs(FVector2D::DotProduct(Dir, J.Axis[0])) >= FMath::Abs(FVector2D::DotProduct(Dir, J.Axis[1])) ? 0 : 1;
		const double LineM = Along - (J.HalfW[1 - Axis] + 1.5);
		if (LineM < -1.0 || LineM > LookAheadM || LineM >= Best) continue;
		Best = LineM;
		const ESimCruiseSignalAspect A = AspectAt(Clock + J.Offset, Axis);
		bStop = A == ESimCruiseSignalAspect::Red || (A == ESimCruiseSignalAspect::Yellow && LineM > YellowCommitM);
		OutStopLineCm = PositionCm + FVector(Dir.X, Dir.Y, 0.0) * LineM * 100.0;
	}
	return bStop;
}

void ASimCruiseSignals::Rebuild()
{
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	Junctions.Reset();
	Mesh->ClearAllMeshSections();
	if (!Stream) return;
	// Junctions of two signal-class roads on the tiles that are built.
	for (const FIntPoint& Id : ShownTiles)
	{
		const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
		const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
		if (!Build) continue;
		const FVector Origin = Tile->GetActorLocation();
		const TArray<FSimCruiseRibbon>& Ribbons = Build->Roads.Ribbons;
		for (int32 r = 0; r < Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& R = Ribbons[r];
			if (!IsSignalClass(R.Class) || R.bUnpaved) continue;
			for (int32 k = 0; k < R.St.Num(); ++k)
			{
				const bool bCut = (R.Sides[0].Cut.IsValidIndex(k) && R.Sides[0].Cut[k]) || (R.Sides[1].Cut.IsValidIndex(k) && R.Sides[1].Cut[k]);
				if (!bCut) continue;
				int32 e = k;
				while (e + 1 < R.St.Num() && ((R.Sides[0].Cut.IsValidIndex(e + 1) && R.Sides[0].Cut[e + 1]) || (R.Sides[1].Cut.IsValidIndex(e + 1) && R.Sides[1].Cut[e + 1]))) ++e;
				const FSimCruiseRibbonStation& Mid = R.St[(k + e) / 2];
				k = e;
				// The road that cuts it here: another signal-class ribbon passing within 12 m.
				int32 Other = INDEX_NONE;
				FVector2d OtherT = FVector2d::ZeroVector;
				double OtherHalfW = 4.0, BestD = 12.0;
				for (int32 o = 0; o < Ribbons.Num(); ++o)
				{
					if (o == r || !IsSignalClass(Ribbons[o].Class) || Ribbons[o].bUnpaved) continue;
					for (const FSimCruiseRibbonStation& St : Ribbons[o].St)
					{
						const double D = FVector2d::Distance(St.C, Mid.C);
						if (D < BestD) { BestD = D; Other = o; OtherT = St.T; OtherHalfW = St.HalfW; }
					}
				}
				// Only a real crossing (not two parts of one road meeting in line).
				if (Other == INDEX_NONE || FMath::Abs(FVector2d::DotProduct(OtherT, Mid.T)) > 0.6) continue;
				const FVector Centre = Origin + FVector(Mid.C.X * 100.0, Mid.C.Y * 100.0, 0.0);
				bool bKnown = false;
				for (const FSimCruiseSignalJunction& J : Junctions) if (FVector::Dist2D(J.CentreCm, Centre) < 2500.0) { bKnown = true; break; }
				if (bKnown) continue;
				FSimCruiseSignalJunction J;
				J.CentreCm = Centre;
				double Z = 0.0;
				if (Stream->SurfaceHeightAt(Centre, Z)) J.CentreCm.Z = Z;
				J.Axis[0] = FVector2D(Mid.T.X, Mid.T.Y).GetSafeNormal();
				J.Axis[1] = FVector2D(OtherT.X, OtherT.Y).GetSafeNormal();
				J.HalfW[0] = Mid.HalfW;
				J.HalfW[1] = OtherHalfW;
				Junctions.Add(J);
			}
		}
	}

	// Poles, mast arms and heads: one for each of the four approaches, on the far right corner.
	FBuffers Dark, Lamps[6];
	for (const FSimCruiseSignalJunction& J : Junctions)
	{
		for (int32 Axis = 0; Axis < 2; ++Axis)
		for (int32 Way = -1; Way <= 1; Way += 2)
		{
			const FVector2D H = J.Axis[Axis] * Way;          // the approach's direction of travel
			const FVector2D Right(-H.Y, H.X);
			const double Far = J.HalfW[1 - Axis] + 3.0, Side = J.HalfW[Axis] + 1.5;
			const FVector2D PoleXY = FVector2D(J.CentreCm.X, J.CentreCm.Y) / 100.0 + H * Far + Right * Side;
			double Ground = J.CentreCm.Z;
			if (Stream) { double Z = 0.0; if (Stream->SurfaceHeightAt(FVector(PoleXY * 100.0, 0.0), Z)) Ground = Z; }
			const FVector Foot(PoleXY.X * 100.0, PoleXY.Y * 100.0, Ground);
			const FVector Up(0, 0, 1);
			const FVector Toward(-H.X, -H.Y, 0.0);            // the head faces the oncoming traffic
			const FVector Over(-Right.X, -Right.Y, 0.0);      // the arm reaches over the lanes
			Dark.Box(Foot - Up * 30.0, Foot + Up * 640.0, FVector(H.X, H.Y, 0.0), 11.0, 11.0);
			const double Reach = FMath::Clamp(J.HalfW[Axis] * 0.6 + 1.5, 2.5, 8.0) * 100.0;
			const FVector ArmEnd = Foot + Up * 620.0 + Over * Reach;
			Dark.Box(Foot + Up * 620.0, ArmEnd, Up, 6.0, 6.0);
			const FVector HeadTop = ArmEnd - Up * 10.0, HeadBottom = ArmEnd - Up * 120.0;
			Dark.Box(HeadBottom, HeadTop, Over, 18.0, 15.0);
			// Back plate.
			Dark.Box(HeadBottom - Up * 10.0 - Toward * 16.0, HeadTop + Up * 10.0 - Toward * 16.0, Over, 33.0, 1.5);
			for (int32 Lamp = 0; Lamp < 3; ++Lamp)
			{
				const FVector C = HeadTop - Up * (24.0 + 36.0 * Lamp) + Toward * 16.0;
				Lamps[Axis * 3 + Lamp].Box(C - Toward * 1.0, C + Toward * 1.5, Over, 13.0, 13.0);
			}
		}
	}
	Mesh->SetWorldLocation(FVector::ZeroVector);
	Mesh->CreateMeshSection(0, Dark.P, Dark.I, Dark.N, Dark.UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	for (int32 s = 0; s < 6; ++s)
	{
		Mesh->CreateMeshSection(1 + s, Lamps[s].P, Lamps[s].I, Lamps[s].N, Lamps[s].UV, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	}
	for (int32 s = 0; s < Materials.Num(); ++s) Mesh->SetMaterial(s, Materials[s]);
	Shown[0] = Shown[1] = ESimCruiseSignalAspect(255);
	UpdateLamps();
	if (Junctions.Num()) UE_LOG(LogSimCruiseSignals, Display, TEXT("%d signalised junctions round the car"), Junctions.Num());
}

void ASimCruiseSignals::UpdateLamps()
{
	if (Materials.Num() != 7) return;
	// Lamp colours (LED signal lenses) and how bright a lit lens is.
	static const FLinearColor Colours[3] = { FLinearColor(1.f, 0.04f, 0.02f), FLinearColor(1.f, 0.45f, 0.02f), FLinearColor(0.05f, 1.f, 0.55f) };
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const ESimCruiseSignalAspect A = AspectAt(Clock, Axis);
		if (A == Shown[Axis]) continue;
		Shown[Axis] = A;
		for (int32 Lamp = 0; Lamp < 3; ++Lamp)
		{
			const bool bLit = (Lamp == 0 && A == ESimCruiseSignalAspect::Red) || (Lamp == 1 && A == ESimCruiseSignalAspect::Yellow) || (Lamp == 2 && A == ESimCruiseSignalAspect::Green);
			UMaterialInstanceDynamic* M = Materials[1 + Axis * 3 + Lamp];
			M->SetVectorParameterValue(TEXT("Colour"), bLit ? Colours[Lamp] : Colours[Lamp] * 0.05f);
			M->SetScalarParameterValue(TEXT("Glow"), bLit ? 60.f : 0.f);
		}
	}
}

void ASimCruiseSignals::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	ASimCruiseWorldStreamer* Stream = Streamer.Get();
	if (!Stream || !Stream->GetFrame().IsValid()) return;
	if (Materials.Num() == 0 && FApp::CanEverRender())
	{
#if WITH_EDITOR
		BaseMaterial = BuildMaterial(this, TEXT("M_SimCruiseSignal"), RF_Transient);
#else
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, *MaterialPath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
#endif
		if (BaseMaterial)
		{
			for (int32 i = 0; i < 7; ++i) Materials.Add(UMaterialInstanceDynamic::Create(BaseMaterial, this));
			Materials[0]->SetVectorParameterValue(TEXT("Colour"), FLinearColor(0.02f, 0.022f, 0.02f));
		}
	}
	UpdateLamps();
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	const APawn* Car = Player ? Player->GetPawn() : nullptr;
	if (!Car) return;
	TSet<FIntPoint> Tiles;
	const FIntPoint Here = Stream->GetFrame().TileAtWorldCm(Car->GetActorLocation());
	for (int32 dx = -1; dx <= 1; ++dx)
	for (int32 dy = -1; dy <= 1; ++dy)
	{
		const ASimCruiseWorldTile* Tile = Stream->GetTile(Here + FIntPoint(dx, dy));
		if (Tile && Tile->GetBuild()) Tiles.Add(Here + FIntPoint(dx, dy));
	}
	if (Tiles.Num() != ShownTiles.Num() || !Tiles.Includes(ShownTiles) || Stream->GetFrame().OriginTile != ShownOrigin)
	{
		ShownTiles = MoveTemp(Tiles);
		ShownOrigin = Stream->GetFrame().OriginTile;
		Rebuild();
	}
}
