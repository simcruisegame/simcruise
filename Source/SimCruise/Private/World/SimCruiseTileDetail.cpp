// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTileDetail.h"

#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileData.h"
#include "HAL/IConsoleManager.h"

namespace
{
	namespace Kind = SimCruiseSurfaceKind;

	TAutoConsoleVariable<int32> CVarPalms(TEXT("simcruise.World.Palms"), 1,
		TEXT("1: plant palms where they grow (tiles built after the change). 0: none, for measuring their cost."));

	FVector Cm3(const FVector2d& P, double Z) { return FVector(P.X * 100.0, P.Y * 100.0, Z * 100.0); }

	uint32 Hash(uint32 A, uint32 B) { return HashCombine(A * 2654435761u, B + 0x9E3779B9u); }
	/** 0..1 from a hash. */
	double Unit(uint32 H) { return static_cast<double>((H >> 8) & 0xFFFFFF) / 16777216.0; }

	FLinearColor Srgb(const TCHAR* Hex) { return FLinearColor::FromSRGBColor(FColor::FromHex(Hex)); }

	/** Writes quads with the two texture coordinates the building material reads. */
	struct FWriter
	{
		FSimCruiseMeshData& M;
		float Seed = 0.f;

		/** Corners in order around the quad (metres; z up), each with its UV0. */
		void Quad(const FVector3d (&P)[4], const FVector& N, const FLinearColor& C, const FVector2D (&UV)[4], float SurfaceKind) const
		{
			int32 I[4];
			for (int32 k = 0; k < 4; ++k)
			{
				I[k] = M.Add(FVector(P[k].X * 100.0, P[k].Y * 100.0, P[k].Z * 100.0), N, C, UV[k], FVector2D(SurfaceKind, Seed));
			}
			M.Quad(I[0], I[1], I[2], I[3], N);
		}

		/** A vertical panel on the line A->B, from Z0 to Z1, facing N2; UV0 = (along from Along0, height above Base). */
		void Panel(const FVector2d& A, const FVector2d& B, double Z0, double Z1, const FVector2d& N2, const FLinearColor& C, float SurfaceKind,
			double Along0, double Base) const
		{
			const double Len = FVector2d::Distance(A, B);
			const FVector3d P[4] = { FVector3d(A.X, A.Y, Z0), FVector3d(B.X, B.Y, Z0), FVector3d(B.X, B.Y, Z1), FVector3d(A.X, A.Y, Z1) };
			const FVector2D UV[4] = { FVector2D(Along0, Z0 - Base), FVector2D(Along0 + Len, Z0 - Base), FVector2D(Along0 + Len, Z1 - Base), FVector2D(Along0, Z1 - Base) };
			Quad(P, FVector(N2.X, N2.Y, 0.0), C, UV, SurfaceKind);
		}

		/** A horizontal quad (four corners at height Z), facing up or down. */
		void Flat(const FVector2d (&Q)[4], double Z, bool bUp, const FLinearColor& C, float SurfaceKind) const
		{
			const FVector3d P[4] = { FVector3d(Q[0].X, Q[0].Y, Z), FVector3d(Q[1].X, Q[1].Y, Z), FVector3d(Q[2].X, Q[2].Y, Z), FVector3d(Q[3].X, Q[3].Y, Z) };
			const FVector2D UV[4] = { FVector2D(Q[0].X, Q[0].Y), FVector2D(Q[1].X, Q[1].Y), FVector2D(Q[2].X, Q[2].Y), FVector2D(Q[3].X, Q[3].Y) };
			Quad(P, bUp ? FVector::UpVector : -FVector::UpVector, C, UV, SurfaceKind);
		}

		/**
		 * A box against a facade line A->B (outward N2): from Depth0 to Depth1 out of the
		 * facade, Z0 to Z1. Front, top, bottom and both ends; the back only when asked
		 * (a part that stands clear of the wall, above the roof).
		 */
		void Slab(const FVector2d& A, const FVector2d& B, const FVector2d& N2, double Depth0, double Depth1, double Z0, double Z1,
			const FLinearColor& C, float SurfaceKind, double Along0, double Base, bool bBack = false, bool bBottom = true) const
		{
			const FVector2d T = (B - A).GetSafeNormal();
			const FVector2d A0 = A + N2 * Depth0, B0 = B + N2 * Depth0, A1 = A + N2 * Depth1, B1 = B + N2 * Depth1;
			Panel(A1, B1, Z0, Z1, N2, C, SurfaceKind, Along0, Base);
			if (bBack) Panel(B0, A0, Z0, Z1, -N2, C, SurfaceKind, Along0, Base);
			Panel(A0, A1, Z0, Z1, -T, C, SurfaceKind, 0.0, Base);
			Panel(B1, B0, Z0, Z1, T, C, SurfaceKind, 0.0, Base);
			const FVector2d Q[4] = { A0, B0, B1, A1 };
			Flat(Q, Z1, true, C, SurfaceKind);
			if (bBottom) Flat(Q, Z0, false, C, SurfaceKind);
		}
	};

	/** A ring's footprint cleaned to metres, with its signed area. */
	bool FootprintMetres(const FSimCruiseBuildingRow& Row, double Side, TArray<FVector2d>& Ring, double& Area)
	{
		Ring.Reset();
		for (const FVector2d& U : Row.Ring)
		{
			const FVector2d P(FMath::Clamp(U.X, 0.0, 1.0) * Side, FMath::Clamp(U.Y, 0.0, 1.0) * Side);
			if (Ring.Num() == 0 || FVector2d::Distance(Ring.Last(), P) > 0.05) Ring.Add(P);
		}
		if (Ring.Num() > 2 && FVector2d::Distance(Ring[0], Ring.Last()) < 0.05) Ring.Pop();
		if (Ring.Num() < 3) return false;
		Area = 0.0;
		for (int32 i = 0; i < Ring.Num(); ++i)
		{
			const FVector2d& P = Ring[i];
			const FVector2d& Q = Ring[(i + 1) % Ring.Num()];
			Area += P.X * Q.Y - P.Y * Q.X;
		}
		Area *= 0.5;
		return FMath::Abs(Area) >= 4.0;
	}

	bool PointInRing(const TArray<FVector2d>& Ring, const FVector2d& P)
	{
		bool bIn = false;
		for (int32 i = 0, j = Ring.Num() - 1; i < Ring.Num(); j = i++)
		{
			if ((Ring[i].Y > P.Y) != (Ring[j].Y > P.Y) &&
				P.X < (Ring[j].X - Ring[i].X) * (P.Y - Ring[i].Y) / (Ring[j].Y - Ring[i].Y) + Ring[i].X)
			{
				bIn = !bIn;
			}
		}
		return bIn;
	}

	/** Facade faces a public street (not an alley or a service way) within about 12 m. */
	bool FacesStreet(const FSimCruiseRoadNetwork& Roads, const FVector2d& Mid, const FVector2d& N2)
	{
		for (const double D : { 3.0, 7.0, 11.0 })
		{
			FSimCruiseRoadHit Hit;
			if (Roads.FindCarriageway(Mid + N2 * D, Hit))
			{
				return Hit.Ribbon < 0 || !Roads.Ribbons[Hit.Ribbon].bService;
			}
		}
		return false;
	}

	// Ocean Drive's 1930s hotels in their restored colours (Leonard Horowitz's 1980s
	// pastel palette for the district): stucco and contrasting trim.
	const TArray<FLinearColor>& DecoWalls()
	{
		static const TArray<FLinearColor> Colours = { Srgb(TEXT("F5F1E6")), Srgb(TEXT("F3E3BD")), Srgb(TEXT("F7E39A")), Srgb(TEXT("BFE6D3")),
			Srgb(TEXT("A9DDD6")), Srgb(TEXT("F6C6C8")), Srgb(TEXT("F4B39A")), Srgb(TEXT("D2C1E8")), Srgb(TEXT("F8D2A9")), Srgb(TEXT("BCD9EE")),
			Srgb(TEXT("F5F1E6")), Srgb(TEXT("EFEBE0")) };
		return Colours;
	}
	const TArray<FLinearColor>& DecoAccents()
	{
		static const TArray<FLinearColor> Colours = { Srgb(TEXT("2F8F8A")), Srgb(TEXT("38B6C2")), Srgb(TEXT("E5739A")), Srgb(TEXT("E07A5F")),
			Srgb(TEXT("34558B")), Srgb(TEXT("F2C14E")), Srgb(TEXT("FFFFFF")), Srgb(TEXT("7FB069")), Srgb(TEXT("FFFFFF")) };
		return Colours;
	}
	/** Neon tube colours (linear, the glow's hue). */
	const TArray<FLinearColor>& NeonColours()
	{
		static const TArray<FLinearColor> Colours = { FLinearColor(1.0f, 0.12f, 0.5f), FLinearColor(0.1f, 0.75f, 1.0f), FLinearColor(0.25f, 0.35f, 1.0f),
			FLinearColor(0.2f, 1.0f, 0.45f), FLinearColor(1.0f, 0.12f, 0.08f), FLinearColor(0.65f, 0.2f, 1.0f) };
		return Colours;
	}
	// The generic palette: 12 common US exterior paints and 4 brick colours (Drive.gd).
	const TArray<FLinearColor>& GenericWalls()
	{
		static const TArray<FLinearColor> Colours = []()
		{
			TArray<FLinearColor> Out;
			for (const TCHAR* Hex : { TEXT("ece9e1"), TEXT("e2d9c4"), TEXT("c9cbc7"), TEXT("b8b1a2"), TEXT("c6b28d"), TEXT("a8b29a"),
				TEXT("a8bbc7"), TEXT("6f8293"), TEXT("e4d397"), TEXT("8d8372"), TEXT("53575a"), TEXT("b9a48a"),
				TEXT("8b4838"), TEXT("76503f"), TEXT("9f5a42"), TEXT("a98c6a") })
			{
				Out.Add(Srgb(Hex));
			}
			return Out;
		}();
		return Colours;
	}
	// Miami Beach condominiums and MiMo hotels: white and pale concrete.
	const TArray<FLinearColor>& TowerWalls()
	{
		static const TArray<FLinearColor> Colours = { Srgb(TEXT("F2F0EA")), Srgb(TEXT("E9E4D8")), Srgb(TEXT("DCD8CF")), Srgb(TEXT("F2F0EA")), Srgb(TEXT("D9E2E4")) };
		return Colours;
	}

	template <typename T>
	const T& Pick(const TArray<T>& Items, uint32 H) { return Items[H % static_cast<uint32>(Items.Num())]; }
}

namespace SimCruiseTileDetail
{
	EBuildingStyle StyleForPlace(double LatDeg, double LonDeg)
	{
		// The district runs from about 6th Street to Dade Boulevard, Ocean Drive to Lenox Avenue;
		// South Beach's Art Deco and MiMo streets continue to the water on both sides.
		if (LatDeg > 25.763 && LatDeg < 25.800 && LonDeg > -80.146 && LonDeg < -80.125)
		{
			return EBuildingStyle::MiamiBeachDeco;
		}
		return EBuildingStyle::Generic;
	}

	bool IsCoconutPalmRegion(double LatDeg, double LonDeg)
	{
		// USDA zone 10b and warmer: Florida's southeast coast north to about Jupiter, the
		// southwest coast to about Fort Myers, the Keys; and Hawaii.
		const bool bSouthFlorida = LatDeg > 24.4 && LatDeg < 26.95 && LonDeg > -82.3 && LonDeg < -79.9;
		const bool bHawaii = LatDeg > 18.8 && LatDeg < 22.4 && LonDeg > -160.4 && LonDeg < -154.7;
		return bSouthFlorida || bHawaii;
	}

	bool IsInsideBuilding(const FSimCruiseTileData& Data, double Side, const FVector2d& P)
	{
		const FVector2d U = P / Side;
		for (const FSimCruiseBuildingRow& Row : Data.Buildings)
		{
			if (Row.Ring.Num() < 3) continue;
			FBox2d Box(ForceInit);
			for (const FVector2d& V : Row.Ring) Box += V;
			if (!Box.ExpandBy(1.5 / Side).IsInside(U)) continue;
			if (PointInRing(Row.Ring, U)) return true;
		}
		return false;
	}

	void BuildBuildings(const FContext& X)
	{
		FSimCruiseTileBuild& B = X.B;
		const FSimCruiseTileData& Data = X.Data;
		const FSimCruiseRoadNetwork& Roads = B.Roads;
		FSimCruiseMeshData& Mesh = B.Layer(ESimCruiseTileLayer::Building);
		const EBuildingStyle Place = StyleForPlace(X.LatDeg, X.LonDeg);

		for (int32 b = 0; b < Data.Buildings.Num(); ++b)
		{
			const FSimCruiseBuildingRow& Row = Data.Buildings[b];
			TArray<FVector2d> Ring;
			double Area = 0.0;
			if (!FootprintMetres(Row, X.Side, Ring, Area)) continue;
			double Ground = TNumericLimits<double>::Max();
			for (const FVector2d& P : Ring) Ground = FMath::Min(Ground, Data.HeightAtUnit(P / X.Side));
			const double Sign = Area >= 0.0 ? 1.0 : -1.0;
			const double Height = FMath::Clamp(Row.HeightM, 2.8, 650.0);
			const double Top = Ground + Height;
			const bool bFloating = Row.MinHeightM > 0.5;
			const double Bottom = bFloating ? Ground + Row.MinHeightM : Ground - 0.3;
			if (Top <= Bottom + 0.5) continue;

			const uint32 H = Hash(GetTypeHash(Data.Id), static_cast<uint32>(b));
			FWriter W{ Mesh, static_cast<float>(Unit(Hash(H, 7))) };
			const bool bRegionDeco = Place == EBuildingStyle::MiamiBeachDeco && !bFloating;
			const bool bDeco = bRegionDeco && Height <= 26.0;
			const bool bTower = bRegionDeco && Height > 26.0;
			const double FloorH = bDeco ? 3.1 : 3.2;
			const double Parapet = bDeco ? (Height > 5.0 ? 0.9 : 0.4) : (bTower ? 1.1 : 0.5);
			const FLinearColor Wall = bDeco ? Pick(DecoWalls(), H) : (bTower ? Pick(TowerWalls(), H) : Pick(GenericWalls(), H));
			// Half the hotels keep white trim; the rest a contrasting accent.
			const FLinearColor Accent = (bDeco && Unit(Hash(H, 3)) < 0.55) ? Pick(DecoAccents(), Hash(H, 4)) : Srgb(TEXT("FAF8F2"));
			const FLinearColor Neon = Pick(NeonColours(), Hash(H, 5));
			const bool bNeonParapet = bDeco && Unit(Hash(H, 6)) < 0.5;
			const bool bEyebrowsAround = bDeco && Unit(Hash(H, 8)) < 0.5;
			const int32 Floors = FMath::Max(1, FMath::FloorToInt((Height + 0.2) / FloorH));
			const int32 N = Ring.Num();

			auto EdgeNormal = [&](int32 i)
			{
				const FVector2d D = Ring[(i + 1) % N] - Ring[i];
				return FVector2d(D.Y, -D.X).GetSafeNormal() * Sign;
			};
			auto EdgeDir = [&](int32 i) { return (Ring[(i + 1) % N] - Ring[i]).GetSafeNormal(); };

			// The street front: the longest edge facing a public street carries the tower.
			int32 Longest = INDEX_NONE;
			double LongestLen = 0.0;
			TArray<bool> Front;
			Front.Init(false, N);
			for (int32 i = 0; i < N; ++i)
			{
				const FVector2d A = Ring[i], C = Ring[(i + 1) % N];
				const double Len = FVector2d::Distance(A, C);
				if (Len < 0.5) continue;
				Front[i] = (bDeco || bTower) && FacesStreet(Roads, (A + C) * 0.5, EdgeNormal(i));
				if (Front[i] && Len > LongestLen) { LongestLen = Len; Longest = i; }
			}

			for (int32 i = 0; i < N; ++i)
			{
				const FVector2d A = Ring[i], C = Ring[(i + 1) % N];
				const double Len = FVector2d::Distance(A, C);
				if (Len < 0.05) continue;
				const FVector2d T = (C - A) / Len;
				const FVector2d N2 = EdgeNormal(i);
				const double Along0 = -0.5 * Len; // windows centre on each facade

				// A porch: Ocean Drive's raised front terraces under the upper floors, here when
				// both corners are convex and the building is deep enough to give up 2.2 m.
				const double PrevLen = FVector2d::Distance(Ring[(i + N - 1) % N], A);
				const double NextLen = FVector2d::Distance(C, Ring[(i + 2) % N]);
				const bool bConvexA = FVector2d::DotProduct(EdgeDir((i + N - 1) % N), N2) > 0.5;
				const bool bConvexC = FVector2d::DotProduct(EdgeDir((i + 1) % N), N2) < -0.5;
				const double PorchDepth = FMath::Min(2.2, 0.35 * FMath::Min(PrevLen, NextLen));
				const bool bPorch = bDeco && Front[i] && Len >= 8.0 && Height >= 6.0 && bConvexA && bConvexC && PorchDepth >= 1.5 && Unit(Hash(H, 9)) < 0.8;

				const double WallStart = bPorch ? Ground + FloorH : Bottom;
				// Street fronts: shop glass in the ground storey, and on Art Deco hotels the
				// coloured uplight wash that lights Ocean Drive at night.
				const float WallKind = !Front[i] ? Kind::Wall : bPorch ? (bDeco ? Kind::DecoUpper : Kind::Wall) : (bDeco ? Kind::DecoFront : Kind::WallFront);
				W.Panel(A, C, WallStart, Top + Parapet, N2, Wall, WallKind, Along0, Ground);
				// The parapet's inside, seen from above and from taller neighbours.
				W.Panel(C, A, Top, Top + Parapet, -N2, Wall, Kind::Trim, Along0, Ground);

				if (bPorch)
				{
					const double Floor = Ground + 0.45;
					const double Ceiling = Ground + FloorH;
					const FVector2d Ai = A - N2 * PorchDepth, Ci = C - N2 * PorchDepth;
					const FLinearColor Tile = Srgb(TEXT("D9D2C3"));
					W.Panel(A, C, Ground - 0.1, Floor, N2, Tile, Kind::Terrazzo, Along0, Ground);
					const FVector2d FloorQ[4] = { A, C, Ci, Ai };
					W.Flat(FloorQ, Floor, true, Tile, Kind::Terrazzo);
					W.Flat(FloorQ, Ceiling, false, Accent, Kind::Trim);
					W.Panel(Ai, Ci, Floor, Ceiling, N2, Wall, Kind::Storefront, Along0, Ground);
					const int32 Columns = FMath::Max(2, FMath::RoundToInt(Len / 4.2) + 1);
					for (int32 k = 0; k < Columns; ++k)
					{
						const double S = FMath::Lerp(0.3, Len - 0.3, k / static_cast<double>(Columns - 1));
						const FVector2d P0 = A + T * (S - 0.17), P1 = A + T * (S + 0.17);
						W.Slab(P0, P1, N2, -0.36, -0.02, Floor, Ceiling, Accent, Kind::Trim, 0.0, Ground, true, false);
					}
				}

				// Canvas awnings over the shopfronts: 1.3 m deep, sloping out from the storey line.
				if (bDeco && Front[i] && !bPorch && Len >= 3.0 && Unit(Hash(H, 12 + i)) < 0.75)
				{
					const FLinearColor Canvas = Unit(Hash(H, 31 + i)) < 0.5 ? Accent : Pick(DecoAccents(), Hash(H, 41 + i));
					const double Zi = Ground + 2.95, Zo = Ground + 2.5;
					const FVector2d A0 = A + T * 0.25, C0 = C - T * 0.25;
					const FVector2d A1 = A0 + N2 * 1.3, C1 = C0 + N2 * 1.3;
					const FVector Up = FVector(N2.X * 0.33, N2.Y * 0.33, 1.0).GetSafeNormal();
					const FVector3d Top4[4] = { FVector3d(A0.X, A0.Y, Zi), FVector3d(C0.X, C0.Y, Zi), FVector3d(C1.X, C1.Y, Zo), FVector3d(A1.X, A1.Y, Zo) };
					const FVector2D UVs[4] = { FVector2D(Along0, 2.95), FVector2D(Along0 + Len, 2.95), FVector2D(Along0 + Len, 2.5), FVector2D(Along0, 2.5) };
					W.Quad(Top4, Up, Canvas, UVs, Kind::Trim);
					const FVector3d Under4[4] = { FVector3d(A0.X, A0.Y, Zi - 0.02), FVector3d(A1.X, A1.Y, Zo - 0.02), FVector3d(C1.X, C1.Y, Zo - 0.02), FVector3d(C0.X, C0.Y, Zi - 0.02) };
					W.Quad(Under4, -Up, Canvas * 0.7f, UVs, Kind::Trim);
					W.Panel(A1, C1, Zo - 0.28, Zo, N2, Canvas, Kind::Trim, Along0, Ground);
				}

				// Eyebrows: cantilevered sunshades over each upper storey's windows.
				if (bDeco && Floors >= 2 && Len >= 3.0 && (Front[i] || bEyebrowsAround))
				{
					for (int32 k = 1; k < Floors; ++k)
					{
						const double Z = Ground + k * FloorH + 2.45;
						if (Z > Top - 0.25) break;
						W.Slab(A + T * 0.15, C - T * 0.15, N2, 0.0, 0.5, Z, Z + 0.1, Accent, Kind::Trim, Along0, Ground);
					}
				}
				// Speed lines near the parapet, and a neon tube along it on some hotels.
				if (bDeco && Front[i] && Height > 5.0)
				{
					for (const double Dz : { 0.35, 0.62, 0.89 })
					{
						W.Slab(A, C, N2, 0.0, 0.05, Top - Dz, Top - Dz + 0.11, Accent, Kind::Trim, Along0, Ground);
					}
					if (bNeonParapet)
					{
						W.Slab(A + T * 0.1, C - T * 0.1, N2, 0.02, 0.07, Top + Parapet - 0.18, Top + Parapet - 0.13, Neon, Kind::Neon, Along0, Ground);
					}
				}
				// Balconies on the condominium towers: a slab and a glass rail every storey.
				if (bTower && Len >= 6.0)
				{
					for (int32 k = 1; k < Floors; ++k)
					{
						const double Z = Ground + k * FloorH;
						if (Z > Top - 1.5) break;
						W.Slab(A + T * 0.3, C - T * 0.3, N2, 0.0, 1.3, Z - 0.18, Z, Wall, Kind::Trim, Along0, Ground);
						W.Panel(A + T * 0.3 + N2 * 1.3, C - T * 0.3 + N2 * 1.3, Z, Z + 1.05, N2, Wall, Kind::Railing, Along0, Ground);
					}
				}
			}

			// The stepped tower over the entrance, with neon up its edges.
			if (bDeco && Longest != INDEX_NONE && LongestLen >= 10.0 && Height >= 6.0)
			{
				const FVector2d A = Ring[Longest], C = Ring[(Longest + 1) % N];
				const FVector2d T = (C - A).GetSafeNormal();
				const FVector2d N2 = EdgeNormal(Longest);
				const FVector2d Mid = (A + C) * 0.5;
				const double Width = FMath::Clamp(LongestLen * 0.16, 2.2, 3.6);
				const double Rise = 2.0 + 2.5 * Unit(Hash(H, 11));
				const double Z0 = Ground + FloorH;
				const double Z1 = Top + Parapet + Rise;
				const FVector2d P0 = Mid - T * (0.5 * Width), P1 = Mid + T * (0.5 * Width);
				W.Slab(P0, P1, N2, -0.6, 0.45, Z0, Z1, Wall, Kind::Trim, -0.5 * Width, Ground, true, true);
				const FVector2d Q0 = Mid - T * (0.28 * Width), Q1 = Mid + T * (0.28 * Width);
				W.Slab(Q0, Q1, N2, -0.4, 0.25, Z1, Z1 + 1.3, Accent, Kind::Trim, -0.28 * Width, Ground, true, false);
				for (const double Sx : { -1.0, 1.0 })
				{
					const FVector2d E = Mid + T * (Sx * (0.5 * Width - 0.1));
					W.Slab(E - T * 0.03, E + T * 0.03, N2, 0.45, 0.51, Z0 + 0.6, Z1 - 0.15, Neon, Kind::Neon, 0.0, Ground, false, true);
				}
				W.Slab(P0 + T * 0.1, P1 - T * 0.1, N2, 0.45, 0.51, Z1 - 0.21, Z1 - 0.15, Neon, Kind::Neon, 0.0, Ground, false, true);
			}

			// Roof: a light, gravel-and-membrane cool roof.
			{
				TArray<int32> Tris;
				const FLinearColor RoofColour = bRegionDeco ? Srgb(TEXT("C9C6BF")) : FLinearColor(0.42f, 0.41f, 0.39f);
				const int32 Base = Mesh.Positions.Num();
				for (const FVector2d& P : Ring) Mesh.Add(Cm3(P, Top), FVector::UpVector, RoofColour, FVector2D(P.X, P.Y), FVector2D(Kind::Roof, W.Seed));
				if (SimCruiseTileBuilder::Triangulate(Ring, Tris))
				{
					for (int32 t = 0; t + 2 < Tris.Num(); t += 3) Mesh.Tri(Base + Tris[t], Base + Tris[t + 1], Base + Tris[t + 2], FVector::UpVector);
				}
				else
				{
					FVector2d Centre = FVector2d::ZeroVector;
					for (const FVector2d& P : Ring) Centre += P;
					Centre /= N;
					const int32 Hub = Mesh.Add(Cm3(Centre, Top), FVector::UpVector, RoofColour, FVector2D(Centre.X, Centre.Y), FVector2D(Kind::Roof, W.Seed));
					for (int32 i = 0; i < N; ++i) Mesh.Tri(Hub, Base + i, Base + (i + 1) % N, FVector::UpVector);
				}
				if (bFloating)
				{
					// The underside of a canopy or overhang.
					const int32 Under = Mesh.Positions.Num();
					for (const FVector2d& P : Ring) Mesh.Add(Cm3(P, Bottom), -FVector::UpVector, Wall, FVector2D(P.X, P.Y), FVector2D(Kind::Trim, W.Seed));
					for (int32 t = 0; t + 2 < Tris.Num(); t += 3) Mesh.Tri(Under + Tris[t], Under + Tris[t + 1], Under + Tris[t + 2], -FVector::UpVector);
				}
			}
			++B.Buildings;
		}
	}

	void BuildPalmVariant(int32 Variant, FSimCruiseMeshData& Trunks, FSimCruiseMeshData& Fronds)
	{
		// One coconut palm at the origin (cm), leaning toward +X. The variants differ in
		// height, lean and frond seeds; each tile places them with its own yaw and scale.
		const uint32 H = Hash(0x5A1Au, static_cast<uint32>(Variant) * 7919u + 13u);
		const float Seed = static_cast<float>(Unit(Hash(H, 3)));
		const double TrunkH = 8.0 + 8.0 * (Variant + 0.5) / PalmVariants;
		const double LeanDeg = 4.0 + 12.0 * Unit(Hash(H, 5));
		const FVector2d LeanDir(1.0, 0.0);
		const FVector2d P(0.0, 0.0);
		const double Z0 = -0.2;

		// Trunk: a tapering, curving tube with a swollen bole.
		constexpr int32 Sides = 6, Rings = 10;
		auto Centre = [&](double U)
		{
			const double Bend = FMath::Tan(FMath::DegreesToRadians(LeanDeg)) * TrunkH * (U - 0.35 * U * U);
			return FVector3d(P.X + LeanDir.X * Bend, P.Y + LeanDir.Y * Bend, Z0 + TrunkH * U);
		};
		auto Radius = [&](double U) { return 0.15 + 0.09 * FMath::Exp(-U * 18.0) - 0.03 * U; };
		const FLinearColor BarkColour = FLinearColor::FromSRGBColor(FColor(178, 172, 160));
		const int32 First = Trunks.Positions.Num();
		for (int32 r = 0; r <= Rings; ++r)
		{
			const double U = r / static_cast<double>(Rings);
			const FVector3d C = Centre(U);
			for (int32 sd = 0; sd <= Sides; ++sd)
			{
				const double A = UE_DOUBLE_TWO_PI * sd / Sides;
				const FVector Nrm(FMath::Cos(A), FMath::Sin(A), 0.0);
				const FVector3d V = C + FVector3d(Nrm) * Radius(U);
				Trunks.Add(FVector(V * 100.0), Nrm, BarkColour, FVector2D(sd / static_cast<double>(Sides), TrunkH * U), FVector2D(Kind::Bark, Seed));
			}
		}
		for (int32 r = 0; r < Rings; ++r)
		for (int32 sd = 0; sd < Sides; ++sd)
		{
			const int32 I0 = First + r * (Sides + 1) + sd, I1 = I0 + 1, I2 = I0 + Sides + 2, I3 = I0 + Sides + 1;
			Trunks.Quad(I0, I1, I2, I3, Trunks.Normals[I0] + Trunks.Normals[I1]);
		}

		// Crown: 16 fronds, 4-5.5 m, arching then drooping, leaflets hanging in a V.
		const FVector3d Crown = Centre(1.0);
		constexpr int32 FrondCount = 16;
		for (int32 f = 0; f < FrondCount; ++f)
		{
			const uint32 Hf = Hash(H, 100 + f);
			const double Az = UE_DOUBLE_TWO_PI * (f + 0.4 * Unit(Hf)) / FrondCount;
			const double Rise = FMath::DegreesToRadians(45.0 - 60.0 * (f % 3) / 2.0 + 10.0 * (Unit(Hash(Hf, 1)) - 0.5));
			const double Length = 4.0 + 1.5 * Unit(Hash(Hf, 2));
			const double Droop = FMath::DegreesToRadians(55.0 + 35.0 * Unit(Hash(Hf, 3)));
			const FVector3d Out(FMath::Cos(Az), FMath::Sin(Az), 0.0);
			const FVector3d Side(-FMath::Sin(Az), FMath::Cos(Az), 0.0);
			// Older, lower fronds yellow and brown.
			const bool bOld = (f % 3) == 2 && Unit(Hash(Hf, 4)) < 0.35;
			const FLinearColor Leaf = bOld ? FLinearColor::FromSRGBColor(FColor(150, 132, 72)) : FLinearColor::FromSRGBColor(FColor(52, 84, 30)).Desaturate(0.1f * Unit(Hash(Hf, 5)));
			constexpr int32 Segs = 5;
			FVector3d Rachis[Segs + 1];
			Rachis[0] = Crown;
			for (int32 k = 1; k <= Segs; ++k)
			{
				const double S = (k - 0.5) / Segs;
				const double Elev = Rise - Droop * S * S;
				Rachis[k] = Rachis[k - 1] + (Out * FMath::Cos(Elev) + FVector3d(0, 0, 1) * FMath::Sin(Elev)) * (Length / Segs);
			}
			for (const double Wing : { -1.0, 1.0 })
			{
				const int32 Base = Fronds.Positions.Num();
				for (int32 k = 0; k <= Segs; ++k)
				{
					const double S = k / static_cast<double>(Segs);
					const double Width = 0.95 * FMath::Pow(FMath::Sin(UE_DOUBLE_PI * FMath::Clamp(S * 0.92 + 0.08, 0.0, 1.0)), 0.6);
					const FVector3d Hang = (Side * Wing * FMath::Cos(FMath::DegreesToRadians(35.0)) - FVector3d(0, 0, 1) * FMath::Sin(FMath::DegreesToRadians(35.0)));
					const FVector3d Tip = Rachis[k] + Hang * Width;
					const FVector Up = FVector(FVector3d::CrossProduct(Hang, Rachis[FMath::Min(k + 1, Segs)] - Rachis[FMath::Max(k - 1, 0)]).GetSafeNormal() * -Wing);
					const FVector Nrm = (Up.Z < 0.0 ? -Up : Up);
					Fronds.Add(FVector(Rachis[k] * 100.0), Nrm, Leaf, FVector2D(S, 0.0), FVector2D(Kind::Frond, Seed + f * 0.0137f));
					Fronds.Add(FVector(Tip * 100.0), Nrm, Leaf, FVector2D(S, Wing), FVector2D(Kind::Frond, Seed + f * 0.0137f));
				}
				for (int32 k = 0; k < Segs; ++k)
				{
					const int32 I0 = Base + 2 * k, I1 = I0 + 2, I2 = I0 + 3, I3 = I0 + 1;
					Fronds.Quad(I0, I1, I2, I3, Fronds.Normals[I0]);
				}
			}
		}
	}

	void BuildPalms(const FContext& X)
	{
		if (!IsCoconutPalmRegion(X.LatDeg, X.LonDeg) || CVarPalms.GetValueOnAnyThread() == 0) return;
		FSimCruiseTileBuild& B = X.B;
		const FSimCruiseRoadNetwork& Roads = B.Roads;

		// Lummus Park and the city's lawns: coconut palms about 10 m apart, never on a
		// path, a road or its first 2.5 m, in water or a building. Each is an instance of
		// one of the shared palm meshes (USimCruiseWorldResources::PalmMeshes).
		constexpr double Spacing = 10.5;
		const int32 Cells = FMath::FloorToInt(X.Side / Spacing);
		const uint32 TileHash = GetTypeHash(X.Data.Id);
		for (int32 gy = 0; gy < Cells; ++gy)
		for (int32 gx = 0; gx < Cells; ++gx)
		{
			const uint32 H = Hash(TileHash, static_cast<uint32>(gy * 4099 + gx));
			if (Unit(H) > 0.42) continue;
			const FVector2d P((gx + 0.5 + 0.7 * (Unit(Hash(H, 1)) - 0.5)) * Spacing, (gy + 0.5 + 0.7 * (Unit(Hash(H, 2)) - 0.5)) * Spacing);
			if (!X.IsVegetation(P) || X.IsPavedPath(P) || X.IsWater(P)) continue;
			FSimCruiseRoadHit Hit;
			if (Roads.FindCarriageway(P, Hit, INDEX_NONE, 2.5)) continue;
			if (IsInsideBuilding(X.Data, X.Side, P)) continue;
			// Coconut palms lean, mostly away from the trade wind toward the land.
			FSimCruisePalmInstance Palm;
			Palm.LocationCm = Cm3(P, B.TerrainHeightAt(P));
			Palm.YawDeg = static_cast<float>(200.0 + 140.0 * Unit(Hash(H, 6)));
			Palm.Scale = static_cast<float>(0.88 + 0.24 * Unit(Hash(H, 7)));
			Palm.Variant = static_cast<uint8>(Hash(H, 4) % PalmVariants);
			B.PalmInstances.Add(Palm);
			++B.Palms;
		}
	}

	void BuildStreetLights(const FContext& X)
	{
		FSimCruiseTileBuild& B = X.B;
		const FSimCruiseRoadNetwork& Roads = B.Roads;
		FSimCruiseMeshData& Props = B.Layer(ESimCruiseTileLayer::Props);
		const bool bDeco = StyleForPlace(X.LatDeg, X.LonDeg) == EBuildingStyle::MiamiBeachDeco;
		// Poles every 33 m on curbed sides, staggered side to side, anchored to the world
		// so the rhythm runs on across tile seams.
		constexpr double SpacingM = 33.0;
		// Miami Beach's poles are light grey (as in the reference frame), the lanterns small drums.
		const FLinearColor PoleColour = bDeco ? FLinearColor::FromSRGBColor(FColor(176, 180, 180)) : FLinearColor::FromSRGBColor(FColor(150, 152, 150));
		const FLinearColor LensColour(1.0f, 0.86f, 0.68f);
		for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& Ribbon = Roads.Ribbons[r];
			if (Ribbon.bService || Ribbon.St.Num() < 2) continue;
			const FVector2d T0 = Ribbon.St[0].T;
			const double Canon = (T0.X > 1e-6 || (FMath::Abs(T0.X) <= 1e-6 && T0.Y > 0.0)) ? 1.0 : -1.0;
			const FVector2d World0(static_cast<double>(X.Data.Id.X) * X.Side + Ribbon.St[0].C.X, static_cast<double>(X.Data.Id.Y) * X.Side + Ribbon.St[0].C.Y);
			const double Anchor = FVector2d::DotProduct(World0, T0) * Canon;
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				const FSimCruiseRibbonSide& Side = Ribbon.Sides[SideIndex];
				const double Sgn = SideIndex == 0 ? -1.0 : 1.0;
				const double Stagger = SideIndex == 0 ? 0.0 : 0.5 * SpacingM;
				for (int32 i = 0; i + 1 < Ribbon.St.Num(); ++i)
				{
					if (!Side.Edge.IsValidIndex(i) || Side.Edge[i] == ESimCruiseRoadEdge::Rural) continue;
					if (Side.Cut.IsValidIndex(i) && (Side.Cut[i] || (Side.Cut.IsValidIndex(i + 1) && Side.Cut[i + 1]))) continue;
					const FSimCruiseRibbonStation& A = Ribbon.St[i];
					const FSimCruiseRibbonStation& Bs = Ribbon.St[i + 1];
					const double U0 = Anchor + Canon * (A.S - Ribbon.St[0].S) - Stagger, U1 = Anchor + Canon * (Bs.S - Ribbon.St[0].S) - Stagger;
					const double Lo = FMath::Min(U0, U1), Hi = FMath::Max(U0, U1);
					for (double Mark = FMath::CeilToDouble(Lo / SpacingM) * SpacingM; Mark < Hi; Mark += SpacingM)
					{
						const double Tt = (Mark - U0) / (U1 - U0);
						const FVector2d C = FMath::Lerp(A.C, Bs.C, Tt);
						const FVector2d R = FMath::Lerp(A.R, Bs.R, Tt).GetSafeNormal();
						const double HalfW = FMath::Lerp(A.HalfW, Bs.HalfW, Tt);
						const bool bKerb = Side.Edge[i] == ESimCruiseRoadEdge::Kerb;
						const double Walk = Side.SidewalkM.IsValidIndex(i) ? Side.SidewalkM[i] : 1.8;
						if (bKerb && Walk < 1.2) continue;
						const FVector2d Out = R * Sgn;
						const FVector2d Base2 = C + Out * (HalfW + (bKerb ? SimCruiseRoadSection::GutterM + SimCruiseRoadSection::CurbBatterM + SimCruiseRoadSection::CurbTopM + 0.55 : SimCruiseRoadSection::TownEdgeM + 0.3));
						if (IsInsideBuilding(X.Data, X.Side, Base2)) continue;
						const double Kerb = FMath::Lerp(A.Height(HalfW * Sgn), Bs.Height(HalfW * Sgn), Tt) + (bKerb ? -SimCruiseRoadSection::GutterDropM + SimCruiseRoadSection::CurbFaceM : 0.0);
						const float Seed = static_cast<float>(Unit(Hash(GetTypeHash(Base2.X * 10.0), GetTypeHash(Base2.Y * 10.0))));
						FWriter W{ Props, Seed };
						const double PoleH = bDeco ? 5.2 : 8.5;
						// Pole: a slim hexagonal shaft.
						constexpr int32 PSides = 6;
						const double Rb = bDeco ? 0.09 : 0.1, Rt = bDeco ? 0.06 : 0.065;
						for (int32 s = 0; s < PSides; ++s)
						{
							const double Aa = UE_DOUBLE_TWO_PI * s / PSides, Ab = UE_DOUBLE_TWO_PI * (s + 1) / PSides;
							const FVector2d Da(FMath::Cos(Aa), FMath::Sin(Aa)), Db(FMath::Cos(Ab), FMath::Sin(Ab));
							const FVector2d Nm = (Da + Db).GetSafeNormal();
							const FVector3d P[4] = { FVector3d(Base2.X + Da.X * Rb, Base2.Y + Da.Y * Rb, Kerb - 0.05), FVector3d(Base2.X + Db.X * Rb, Base2.Y + Db.Y * Rb, Kerb - 0.05),
								FVector3d(Base2.X + Db.X * Rt, Base2.Y + Db.Y * Rt, Kerb + PoleH), FVector3d(Base2.X + Da.X * Rt, Base2.Y + Da.Y * Rt, Kerb + PoleH) };
							const FVector2D UV[4] = { FVector2D(0, 0), FVector2D(0.1, 0), FVector2D(0.1, PoleH), FVector2D(0, PoleH) };
							W.Quad(P, FVector(Nm.X, Nm.Y, 0.0), PoleColour, UV, Kind::Metal);
						}
						FVector Lamp;
						if (bDeco)
						{
							// A post-top lantern: a glass drum under a dark cap.
							const FVector2d L0 = Base2 - Out * 0.13, L1 = Base2 + Out * 0.13;
							const FVector2d Tg(-Out.Y, Out.X);
							W.Slab(L0 - Tg * 0.13, L1 - Tg * 0.13, Tg, 0.0, 0.26, Kerb + PoleH, Kerb + PoleH + 0.42, LensColour, Kind::LampGlass, 0.0, Kerb, true, true);
							W.Slab(L0 - Tg * 0.17 - Out * 0.04, L1 - Tg * 0.17 + Out * 0.04, Tg, 0.0, 0.34, Kerb + PoleH + 0.42, Kerb + PoleH + 0.52, PoleColour, Kind::Metal, 0.0, Kerb, true, true);
							Lamp = Cm3(Base2, Kerb + PoleH + 0.2);
						}
						else
						{
							// A davit arm reaching 1.8 m over the road and a flat LED head.
							const FVector2d ArmEnd = Base2 - Out * 1.8;
							const FVector2d Tg(-Out.Y, Out.X);
							W.Slab(Base2 - Tg * 0.04, Base2 + Tg * 0.04, -Out, 0.0, 1.8, Kerb + PoleH - 0.06, Kerb + PoleH + 0.04, PoleColour, Kind::Metal, 0.0, Kerb, true, true);
							W.Slab(ArmEnd - Tg * 0.17, ArmEnd + Tg * 0.17, -Out, -0.1, 0.55, Kerb + PoleH - 0.12, Kerb + PoleH + 0.02, PoleColour, Kind::Metal, 0.0, Kerb, true, false);
							const FVector2d Q[4] = { ArmEnd - Tg * 0.14, ArmEnd + Tg * 0.14, ArmEnd + Tg * 0.14 - Out * 0.5, ArmEnd - Tg * 0.14 - Out * 0.5 };
							W.Flat(Q, Kerb + PoleH - 0.125, false, LensColour, Kind::LampGlass);
							Lamp = Cm3(ArmEnd - Out * 0.25, Kerb + PoleH - 0.15);
						}
						B.LampPointsCm.Add(Lamp);
						++B.StreetLights;
					}
				}
			}
		}
	}

	void AddStopSign(FSimCruiseMeshData& Props, const FVector2d& BaseM, double GroundZM, const FVector2d& FacingIn)
	{
		const FVector2d F = FacingIn.GetSafeNormal();      // out of the face, toward the drivers
		const FVector2d Across(-F.Y, F.X);                  // the face's horizontal axis
		FWriter W{ Props, 0.37f };
		const FLinearColor Steel = FLinearColor::FromSRGBColor(FColor(150, 154, 156));
		const FLinearColor Red = FLinearColor::FromSRGBColor(FColor(178, 24, 36));
		const FLinearColor White = FLinearColor::FromSRGBColor(FColor(236, 236, 232));
		const FLinearColor Back = FLinearColor::FromSRGBColor(FColor(160, 162, 160));
		// Post: 2 in square galvanised tube, to just under the sign's top.
		constexpr double PostHalf = 0.026, SignAcross = 0.762, Bottom = 2.13;
		const double Top = Bottom + SignAcross;
		const FVector2d Corner[4] = { BaseM + (F + Across) * PostHalf, BaseM + (Across - F) * PostHalf, BaseM + (-F - Across) * PostHalf, BaseM + (F - Across) * PostHalf };
		for (int32 k = 0; k < 4; ++k)
		{
			const FVector2d A = Corner[k], B = Corner[(k + 1) % 4];
			const FVector2d N = ((A + B) * 0.5 - BaseM).GetSafeNormal();
			W.Panel(A, B, GroundZM - 0.1, Top - 0.05, N, Steel, Kind::Metal, 0.0, GroundZM);
		}
		// The face stands just in front of the post; the octagon across flats SignAcross.
		const FVector2d Face = BaseM + F * (PostHalf + 0.01);
		const double Mid = Bottom + 0.5 * SignAcross;
		auto At = [&](double X, double Y, double Out) { return FVector3d(Face.X + Across.X * X + F.X * Out, Face.Y + Across.Y * X + F.Y * Out, Mid + Y); };
		const FVector Normal(F.X, F.Y, 0.0);
		auto Octagon = [&](double Radius, double Out, const FLinearColor& C, const FVector& N)
		{
			// Radius across flats / 2 -> vertices on the circumcircle, flats top and bottom.
			const double Rv = Radius / FMath::Cos(UE_DOUBLE_PI / 8.0);
			const int32 Centre = Props.Add(FVector(At(0, 0, Out) * 100.0), N, C, FVector2D(0, 0), FVector2D(Kind::Metal, 0.37f));
			int32 Ring[8];
			for (int32 k = 0; k < 8; ++k)
			{
				const double A = UE_DOUBLE_PI / 8.0 + UE_DOUBLE_PI / 4.0 * k;
				const FVector3d P = At(FMath::Cos(A) * Rv, FMath::Sin(A) * Rv, Out);
				Ring[k] = Props.Add(FVector(P * 100.0), N, C, FVector2D(0, 0), FVector2D(Kind::Metal, 0.37f));
			}
			for (int32 k = 0; k < 8; ++k) Props.Tri(Centre, Ring[k], Ring[(k + 1) % 8], N);
		};
		Octagon(0.5 * SignAcross, 0.0, White, Normal);                 // the white border
		Octagon(0.5 * SignAcross - 0.019, 0.002, Red, Normal);          // the red field
		Octagon(0.5 * SignAcross, -0.003, Back, -Normal);               // the plain back
		// STOP in 10 in block letters (Series C proportions, approximated by strokes).
		constexpr double LH = 0.254, LW = 0.135, St = 0.036, Gap = 0.03;
		struct FRect { double X0, Y0, X1, Y1; };
		const TArray<TArray<FRect>> Letters = {
			{ { 0, LH - St, LW, LH }, { 0, LH / 2 - St / 2, LW, LH / 2 + St / 2 }, { 0, 0, LW, St }, { 0, LH / 2, St, LH }, { LW - St, 0, LW, LH / 2 } }, // S
			{ { 0, LH - St, LW, LH }, { LW / 2 - St / 2, 0, LW / 2 + St / 2, LH } },                                                                     // T
			{ { 0, 0, St, LH }, { LW - St, 0, LW, LH }, { 0, LH - St, LW, LH }, { 0, 0, LW, St } },                                                      // O
			{ { 0, 0, St, LH }, { 0, LH - St, LW, LH }, { 0, LH / 2 - St / 2, LW, LH / 2 + St / 2 }, { LW - St, LH / 2, LW, LH } },                    // P
		};
		const double Width = 4 * LW + 3 * Gap;
		for (int32 L = 0; L < Letters.Num(); ++L)
		{
			const double X0 = -0.5 * Width + L * (LW + Gap);
			for (const FRect& Rc : Letters[L])
			{
				const FVector3d P[4] = { At(X0 + Rc.X0, Rc.Y0 - LH / 2, 0.004), At(X0 + Rc.X1, Rc.Y0 - LH / 2, 0.004), At(X0 + Rc.X1, Rc.Y1 - LH / 2, 0.004), At(X0 + Rc.X0, Rc.Y1 - LH / 2, 0.004) };
				const FVector2D UV[4] = { FVector2D(0, 0), FVector2D(0, 0), FVector2D(0, 0), FVector2D(0, 0) };
				W.Quad(P, Normal, White, UV, Kind::Metal);
			}
		}
	}
}
