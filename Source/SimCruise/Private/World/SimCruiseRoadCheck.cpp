// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseRoadCheck.h"

#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"

namespace
{
	/** The collision triangles of a build, binned in 4 m cells (tile metres). */
	struct FTriangleGrid
	{
		static constexpr double CellM = 4.0;
		struct FTri { FVector3d A, B, C; };
		TArray<FTri> Tris;
		TMap<FIntPoint, TArray<int32>> Cells;

		void AddLayer(const FSimCruiseMeshData& M)
		{
			for (int32 i = 0; i + 2 < M.Indices.Num(); i += 3)
			{
				FTri T{ FVector3d(M.Positions[M.Indices[i]]) / 100.0, FVector3d(M.Positions[M.Indices[i + 1]]) / 100.0, FVector3d(M.Positions[M.Indices[i + 2]]) / 100.0 };
				// Vertical faces (curb faces, skirts) carry no wheel.
				const FVector3d N = FVector3d::CrossProduct(T.B - T.A, T.C - T.A);
				if (FMath::Abs(N.Z) < 1e-6 * N.Size() + 1e-9) continue;
				const int32 Index = Tris.Add(T);
				const double X0 = FMath::Min3(T.A.X, T.B.X, T.C.X), X1 = FMath::Max3(T.A.X, T.B.X, T.C.X);
				const double Y0 = FMath::Min3(T.A.Y, T.B.Y, T.C.Y), Y1 = FMath::Max3(T.A.Y, T.B.Y, T.C.Y);
				for (int32 y = FMath::FloorToInt(Y0 / CellM); y <= FMath::FloorToInt(Y1 / CellM); ++y)
				for (int32 x = FMath::FloorToInt(X0 / CellM); x <= FMath::FloorToInt(X1 / CellM); ++x)
				{
					Cells.FindOrAdd(FIntPoint(x, y)).Add(Index);
				}
			}
		}

		bool Top(const FVector2d& P, double& Out) const
		{
			const TArray<int32>* List = Cells.Find(FIntPoint(FMath::FloorToInt(P.X / CellM), FMath::FloorToInt(P.Y / CellM)));
			if (!List) return false;
			bool bAny = false;
			for (const int32 i : *List)
			{
				const FTri& T = Tris[i];
				// Barycentric in the plane.
				const FVector2d A(T.A.X, T.A.Y), B(T.B.X, T.B.Y), C(T.C.X, T.C.Y);
				const double D = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
				if (FMath::Abs(D) < 1e-12) continue;
				const double L1 = ((B.Y - C.Y) * (P.X - C.X) + (C.X - B.X) * (P.Y - C.Y)) / D;
				const double L2 = ((C.Y - A.Y) * (P.X - C.X) + (A.X - C.X) * (P.Y - C.Y)) / D;
				const double L3 = 1.0 - L1 - L2;
				constexpr double Eps = -1e-7;
				if (L1 < Eps || L2 < Eps || L3 < Eps) continue;
				const double Z = L1 * T.A.Z + L2 * T.B.Z + L3 * T.C.Z;
				if (!bAny || Z > Out) { Out = Z; bAny = true; }
			}
			return bAny;
		}
	};

	/** The carriageways' top: the asphalt (curbs, medians and verges are not lane surface). */
	void BuildGrid(const FSimCruiseTileBuild& B, FTriangleGrid& G)
	{
		G.AddLayer(B.Layer(ESimCruiseTileLayer::Asphalt));
	}
}

namespace SimCruiseRoadCheck
{
	bool TopSurface(const FSimCruiseTileBuild& Build, const FVector2d& P, double& OutHeightM)
	{
		FTriangleGrid G;
		BuildGrid(Build, G);
		return G.Top(P, OutHeightM);
	}

	TArray<FSimCruiseRoadStep> FindSteps(const FSimCruiseTileBuild& Build, double ThresholdCm)
	{
		TArray<FSimCruiseRoadStep> Out;
		FTriangleGrid G;
		BuildGrid(Build, G);
		const FSimCruiseRoadNetwork& Roads = Build.Roads;
		const double Side = Build.TileSizeM;
		for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& R = Roads.Ribbons[r];
			if (R.St.Num() < 2 || R.bUnpaved) continue;
			// The centre line and the middle of each half: where the wheels of a car in a lane run.
			for (const double Frac : { -0.5, 0.0, 0.5 })
			{
				struct FSample { double S; double Z; double H; FVector2d P; bool bOk; };
				TArray<FSample> Samples;
				const double S0 = R.St[0].S, S1 = R.St.Last().S;
				int32 I = 0;
				for (double S = S0; S <= S1 + 1e-6; S += SpacingM)
				{
					while (I + 2 < R.St.Num() && R.St[I + 1].S < S) ++I;
					const FSimCruiseRibbonStation& A = R.St[I];
					const FSimCruiseRibbonStation& Bs = R.St[I + 1];
					const double Span = Bs.S - A.S;
					const double T = Span > 1e-9 ? FMath::Clamp((S - A.S) / Span, 0.0, 1.0) : 0.0;
					const FVector2d C = FMath::Lerp(A.C, Bs.C, T);
					const FVector2d Rt = FMath::Lerp(A.R, Bs.R, T).GetSafeNormal();
					const double HalfW = FMath::Lerp(A.HalfW, Bs.HalfW, T);
					const FVector2d P = C + Rt * (Frac * HalfW);
					// The ribbon's own profile there (what its own mesh follows).
					const double L = Frac * HalfW;
					const double H = FMath::Lerp(A.Height(L), Bs.Height(L), T);
					FSample Smp{ S, 0.0, H, P, false };
					// Inside the tile only (a neighbour owns the rest; the seam tests cover the border);
					// a deck more than 1 m over the profile is a bridge across, not this road's surface.
					if (P.X > 0.25 && P.Y > 0.25 && P.X < Side - 0.25 && P.Y < Side - 0.25) Smp.bOk = G.Top(P, Smp.Z) && Smp.Z - H < 1.0;
					Samples.Add(Smp);
				}
				for (int32 k = 1; k + 1 < Samples.Num(); ++k)
				{
					const FSample& Pa = Samples[k - 1];
					const FSample& Pb = Samples[k];
					const FSample& Pc = Samples[k + 1];
					if (!Pa.bOk || !Pb.bOk || !Pc.bOk) continue;
					// What the wheel feels: the top surface's change from B to C that its slope from A
					// to B does not explain, at 10 cm spacing (a road's grade cannot change 20 % in 10 cm,
					// so more than 2 cm here is a step or a crease, never a slope).
					const double JumpCm = FMath::Abs((Pc.Z - Pb.Z) - (Pb.Z - Pa.Z)) * 100.0;
					if (JumpCm <= ThresholdCm) continue;
					FSimCruiseRoadStep Step;
					Step.Ribbon = r;
					Step.S = Pb.S;
					Step.Lateral = Frac;
					Step.PositionM = 0.5 * (Pb.P + Pc.P);
					Step.JumpCm = JumpCm;
					Step.HeightBeforeM = Pb.Z;
					Step.HeightAfterM = Pc.Z;
					Out.Add(Step);
				}
			}
		}
		// One step per place: the samples on either side of a jump both see it.
		Out.Sort([](const FSimCruiseRoadStep& A, const FSimCruiseRoadStep& B) { return A.JumpCm > B.JumpCm; });
		TArray<FSimCruiseRoadStep> Unique;
		for (const FSimCruiseRoadStep& S : Out)
		{
			bool bNear = false;
			for (const FSimCruiseRoadStep& U : Unique) bNear |= FVector2d::DistSquared(U.PositionM, S.PositionM) < 1.5 * 1.5;
			if (!bNear) Unique.Add(S);
		}
		return Unique;
	}

	FString Describe(const FSimCruiseTileBuild& Build, const TArray<FSimCruiseRoadStep>& Steps, int32 Max)
	{
		FString Text = FString::Printf(TEXT("Road steps %s: %d over the threshold"), *SimCruiseTileName(Build.Tile), Steps.Num());
		for (int32 i = 0; i < FMath::Min(Max, Steps.Num()); ++i)
		{
			const FSimCruiseRoadStep& S = Steps[i];
			const FSimCruiseRibbon& R = Build.Roads.Ribbons[S.Ribbon];
			double Lon, Lat;
			SimCruiseGeo::TileToLonLat(FVector2d(Build.Tile.X, Build.Tile.Y) + S.PositionM / Build.TileSizeM, Lon, Lat);
			Text += FString::Printf(TEXT("\n  %.1f cm on %s %s (ribbon %d) at s %.1f m, lane %+.1f, (%.1f, %.1f) m = %.6f, %.6f; %.3f -> %.3f m"),
				S.JumpCm, *R.Class, R.Name.IsEmpty() ? TEXT("(unnamed)") : *R.Name, S.Ribbon, S.S, S.Lateral, S.PositionM.X, S.PositionM.Y, Lat, Lon, S.HeightBeforeM, S.HeightAfterM);
		}
		return Text;
	}
}
