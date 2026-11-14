// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseStartPick.h"

#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTraffic.h"

namespace
{
	bool IsPublic(const FSimCruiseRibbon& R) { return !R.bService && R.Class != TEXT("service") && R.St.Num() >= 2; }

	FVector2d World(const FSimCruiseStartTile& T, const FVector2d& LocalM) { return FVector2d(T.OriginCm.X, T.OriginCm.Y) / 100.0 + LocalM; }

	bool IsCut(const FSimCruiseRibbon& R, int32 I)
	{
		return (R.Sides[0].Cut.IsValidIndex(I) && R.Sides[0].Cut[I]) || (R.Sides[1].Cut.IsValidIndex(I) && R.Sides[1].Cut[I]);
	}

	/** The station index whose S is nearest S. */
	int32 StationAt(const FSimCruiseRibbon& R, double S)
	{
		int32 Best = 0;
		for (int32 i = 1; i < R.St.Num(); ++i) if (FMath::Abs(R.St[i].S - S) < FMath::Abs(R.St[Best].S - S)) Best = i;
		return Best;
	}

	/** The point of a ribbon's centreline nearest P (tile metres), within stations [Lo, Hi]: segment index and t. */
	bool Nearest(const FSimCruiseRibbon& R, const FVector2d& P, int32 Lo, int32 Hi, int32& OutSeg, double& OutT)
	{
		double Best = TNumericLimits<double>::Max();
		Lo = FMath::Clamp(Lo, 0, R.St.Num() - 2);
		Hi = FMath::Clamp(Hi, 0, R.St.Num() - 2);
		for (int32 i = Lo; i <= Hi; ++i)
		{
			const FVector2d A = R.St[i].C, B = R.St[i + 1].C;
			const double L2 = FMath::Max((B - A).SizeSquared(), 1e-9);
			const double T = FMath::Clamp(FVector2d::DotProduct(P - A, B - A) / L2, 0.0, 1.0);
			const double D = FVector2d::DistSquared(P, A + (B - A) * T);
			if (D < Best) { Best = D; OutSeg = i; OutT = T; }
		}
		return Best < TNumericLimits<double>::Max();
	}
}

double SimCruiseStartPick::AheadM(const TArray<FSimCruiseStartTile>& Tiles, int32 Tile, int32 Ribbon, double S, int32 Dir, double CapM)
{
	if (!Tiles.IsValidIndex(Tile) || !Tiles[Tile].Build || !Tiles[Tile].Build->Roads.Ribbons.IsValidIndex(Ribbon)) return 0.0;
	double Total = 0.0;
	TSet<uint64> Seen;
	for (int32 Hop = 0; Hop < 256; ++Hop)
	{
		const FSimCruiseRibbon& R = Tiles[Tile].Build->Roads.Ribbons[Ribbon];
		Seen.Add((static_cast<uint64>(Tile) << 40) | (static_cast<uint64>(Ribbon) << 2) | (Dir > 0 ? 1 : 0));
		const double EndS = Dir > 0 ? R.St.Last().S : R.St[0].S;
		Total += FMath::Abs(EndS - S);
		if (Total >= CapM) return CapM;
		// The ribbon's end: on along the ribbon that starts or ends within 3 m of it, straightest
		// first, as the drive robot (ASimCruisePreflight::Walk) and a driver going on do.
		const FVector2d End = World(Tiles[Tile], Dir > 0 ? R.St.Last().C : R.St[0].C);
		const FVector2d Heading = (Dir > 0 ? R.St.Last().T : -R.St[0].T).GetSafeNormal();
		int32 NextTile = INDEX_NONE, NextRibbon = INDEX_NONE, NextDir = 0;
		double NextS = 0.0, BestDot = -0.2;
		for (int32 t = 0; t < Tiles.Num(); ++t)
		{
			if (!Tiles[t].Build) continue;
			const TArray<FSimCruiseRibbon>& Ribbons = Tiles[t].Build->Roads.Ribbons;
			for (int32 r = 0; r < Ribbons.Num(); ++r)
			{
				if (t == Tile && r == Ribbon) continue;
				const FSimCruiseRibbon& O = Ribbons[r];
				if (!IsPublic(O)) continue;
				if (FVector2d::Distance(World(Tiles[t], O.St[0].C), End) < 3.0)
				{
					const double Dot = FVector2d::DotProduct((O.St[1].C - O.St[0].C).GetSafeNormal(), Heading);
					if (Dot > BestDot) { BestDot = Dot; NextTile = t; NextRibbon = r; NextDir = 1; NextS = O.St[0].S; }
				}
				if (FVector2d::Distance(World(Tiles[t], O.St.Last().C), End) < 3.0)
				{
					const int32 N = O.St.Num();
					const double Dot = FVector2d::DotProduct((O.St[N - 2].C - O.St[N - 1].C).GetSafeNormal(), Heading);
					if (Dot > BestDot) { BestDot = Dot; NextTile = t; NextRibbon = r; NextDir = -1; NextS = O.St.Last().S; }
				}
			}
		}
		if (NextRibbon == INDEX_NONE) return Total; // a dead end
		if (Seen.Contains((static_cast<uint64>(NextTile) << 40) | (static_cast<uint64>(NextRibbon) << 2) | (NextDir > 0 ? 1 : 0))) return CapM; // a loop goes on
		Tile = NextTile; Ribbon = NextRibbon; Dir = NextDir; S = NextS;
	}
	return Total;
}

bool SimCruiseStartPick::OnCarriageway(const TArray<FSimCruiseStartTile>& Tiles, const FSimCruiseStartPose& Pose, FString* Why)
{
	auto Fail = [Why](const FString& Text) { if (Why) *Why = Text; return false; };
	if (!Tiles.IsValidIndex(Pose.Tile) || !Tiles[Pose.Tile].Build) return Fail(TEXT("no tile"));
	const FSimCruiseTileBuild& B = *Tiles[Pose.Tile].Build;
	if (!B.Roads.Ribbons.IsValidIndex(Pose.Ribbon)) return Fail(TEXT("no ribbon"));
	const FSimCruiseRibbon& R = B.Roads.Ribbons[Pose.Ribbon];
	const FVector2d Centre = FVector2d(Pose.LocationCm.X - Tiles[Pose.Tile].OriginCm.X, Pose.LocationCm.Y - Tiles[Pose.Tile].OriginCm.Y) / 100.0;
	const double Yaw = FMath::DegreesToRadians(Pose.Rotation.Yaw);
	const FVector2d Fwd(FMath::Cos(Yaw), FMath::Sin(Yaw)), Right(-Fwd.Y, Fwd.X);
	// A car: 4.6 m long, 2 m wide; its corners and its middle.
	const FVector2d Points[5] = { Centre, Centre + Fwd * 2.3 + Right * 1.0, Centre + Fwd * 2.3 - Right * 1.0, Centre - Fwd * 2.3 + Right * 1.0, Centre - Fwd * 2.3 - Right * 1.0 };
	for (const FVector2d& P : Points)
	{
		int32 Seg = 0;
		double T = 0.0;
		if (!Nearest(R, P, Pose.Station - 8, Pose.Station + 8, Seg, T)) return Fail(TEXT("off its ribbon"));
		const FSimCruiseRibbonStation& A = R.St[Seg];
		const FSimCruiseRibbonStation& C = R.St[Seg + 1];
		const FVector2d On = FMath::Lerp(A.C, C.C, T);
		const FVector2d Rn = FMath::Lerp(A.R, C.R, T).GetSafeNormal();
		const double Lateral = FVector2d::DotProduct(P - On, Rn);
		const double HalfW = FMath::Lerp(A.HalfW, C.HalfW, T);
		if (FMath::Abs(Lateral) > HalfW - 0.1 || FVector2d::Distance(P, On + Rn * Lateral) > 0.5)
		{
			return Fail(FString::Printf(TEXT("a corner %.1f m from the centre line, the road %.1f m wide"), Lateral, 2.0 * HalfW));
		}
		// No ground above the road under the car (a bank the road conditioning missed would hide
		// the road and stop the car).
		const double Road = FMath::Lerp(R.CarriagewayHeight(Seg, Lateral), R.CarriagewayHeight(Seg + 1, Lateral), T);
		if (B.TerrainN > 0 && P.X >= 0.0 && P.Y >= 0.0 && P.X <= B.TileSizeM && P.Y <= B.TileSizeM && !A.bBridge && !C.bBridge)
		{
			const double Ground = B.TerrainHeightAt(P);
			if (Ground > Road + 0.1) return Fail(FString::Printf(TEXT("ground %.2f m above the road"), Ground - Road));
		}
	}
	return true;
}

TArray<FSimCruiseStartPose> SimCruiseStartPick::Candidates(const TArray<FSimCruiseStartTile>& Tiles, const FString& RoadName, const FVector& NearCm,
	double RadiusM, double PreferredYawDeg, int32 MaxCandidates)
{
	TArray<FSimCruiseStartPose> Out;
	const bool bPreferred = PreferredYawDeg < 1e9;
	const FVector2d Wanted = bPreferred ? FVector2d(FMath::Cos(FMath::DegreesToRadians(PreferredYawDeg)), FMath::Sin(FMath::DegreesToRadians(PreferredYawDeg))) : FVector2d::ZeroVector;
	const FVector2d Near(NearCm.X / 100.0, NearCm.Y / 100.0);
	for (int32 t = 0; t < Tiles.Num(); ++t)
	{
		const FSimCruiseTileBuild* B = Tiles[t].Build;
		if (!B) continue;
		for (int32 r = 0; r < B->Roads.Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& Ribbon = B->Roads.Ribbons[r];
			if (!IsPublic(Ribbon) || (!RoadName.IsEmpty() && !Ribbon.Name.Equals(RoadName, ESearchCase::IgnoreCase))) continue;
			// Not in or next to a junction: a start there faces the crossing road.
			TArray<uint8> NearJunction;
			NearJunction.Init(0, Ribbon.St.Num());
			for (int32 j = 0; j < Ribbon.St.Num(); ++j)
			{
				if (!IsCut(Ribbon, j)) continue;
				for (int32 k = 0; k < Ribbon.St.Num(); ++k) if (FMath::Abs(Ribbon.St[k].S - Ribbon.St[j].S) < 15.0) NearJunction[k] = 1;
			}
			for (int32 i = 1; i + 1 < Ribbon.St.Num(); ++i)
			{
				const FSimCruiseRibbonStation& St = Ribbon.St[i];
				if (NearJunction[i]) continue;
				const double D = FVector2d::Distance(World(Tiles[t], St.C), Near);
				if (D > RadiusM) continue;
				// 25 m or more inside the tile: the seam's road ends meet the next tile's there.
				if (St.C.X < 25.0 || St.C.Y < 25.0 || St.C.X > B->TileSizeM - 25.0 || St.C.Y > B->TileSizeM - 25.0) continue;
				// A straight piece: under 25 degrees of turn over 12 m each way.
				const FVector2d Back = Ribbon.St[StationAt(Ribbon, St.S - 12.0)].T, Fore = Ribbon.St[StationAt(Ribbon, St.S + 12.0)].T;
				if (FVector2d::DotProduct(Back, Fore) < FMath::Cos(FMath::DegreesToRadians(25.0))) continue;
				for (const int32 Way : { 1, -1 })
				{
					if (St.Travel != 0 && St.Travel != Way) continue;
					bool bEnd = false;
					const double Room = FMath::Abs(SimCruiseTrafficModel::NextDecisionS(Ribbon, Way, St.S, -1e9, bEnd) - St.S);
					if (Room < 25.0) continue;
					FSimCruiseStartPose Pose;
					Pose.Tile = t; Pose.Ribbon = r; Pose.Station = i; Pose.Dir = Way; Pose.DistanceM = D;
					const FVector2d Dir = St.T * static_cast<double>(Way);
					// The lane the traffic drives in this direction, between the lines the markings draw.
					const double L = SimCruiseTrafficModel::LaneOffsetM(Ribbon, i, Way);
					const FVector2d P = St.C + St.R * L;
					Pose.LocationCm = Tiles[t].OriginCm + FVector(P.X * 100.0, P.Y * 100.0, Ribbon.CarriagewayHeight(i, L) * 100.0);
					Pose.Rotation = FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0);
					if (!OnCarriageway(Tiles, Pose)) continue;
					Pose.AheadM = AheadM(Tiles, t, r, St.S, Way, WantedAheadM);
					// Nearest first among the starts with a road ahead; a short road (a dead end in
					// reach) only when there is no other; the requested heading where the road allows.
					Pose.Score = D + (Pose.AheadM < WantedAheadM ? 10000.0 + 10.0 * (WantedAheadM - Pose.AheadM) : 0.0)
						+ (bPreferred && FVector2d::DotProduct(Dir, Wanted) < 0.0 ? 2000.0 : 0.0);
					Out.Add(Pose);
				}
			}
		}
	}
	Out.Sort([](const FSimCruiseStartPose& A, const FSimCruiseStartPose& B) { return A.Score < B.Score; });
	// One candidate per ribbon and direction within 40 m of a better one, so the list offers
	// different places, not the stations of one stretch.
	TArray<FSimCruiseStartPose> Distinct;
	for (const FSimCruiseStartPose& P : Out)
	{
		bool bClose = false;
		for (const FSimCruiseStartPose& Q : Distinct)
		{
			if (Q.Tile == P.Tile && Q.Ribbon == P.Ribbon && Q.Dir == P.Dir && FVector::Dist2D(Q.LocationCm, P.LocationCm) < 4000.0) { bClose = true; break; }
		}
		if (!bClose) Distinct.Add(P);
		if (Distinct.Num() >= MaxCandidates) break;
	}
	return Distinct;
}
