// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTileDetail.h"

#include "Knowledge/SimCruisePlaceGraph.h"
#include "Knowledge/SimCruisePlaceGraphSubsystem.h"

#include "World/SimCruiseCellContext.h"
#include "World/SimCruiseGeo.h"
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

	/**
	 * Painted exterior walls in the weather reflect at most about 72 % (luminance): new white
	 * exterior paint is LRV 80-85, and sun, salt and dirt take it down. Measured at Lummus Park
	 * (24 April 2025 photograph, same pose and hour): the sunlit white hotel fronts read a
	 * median sRGB 191 where the game's 0.88 white read 210, with the promenade concrete equal
	 * (159) in both. Colours lighter than the cap keep their hue and scale down.
	 */
	using SimCruiseTileDetail::MaxWallAlbedo;
	using SimCruiseTileDetail::MaxTrimAlbedo;
	using SimCruiseTileDetail::MaxTowerAlbedo;
	using SimCruiseTileDetail::WeatheredPaint;

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

	/** The helper's height for a footprint with no measured or tagged height (render_height || 5). */
	bool IsUnknownHeight(double HeightM) { return FMath::Abs(HeightM - 5.0) < 1e-3; }

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

	/**
	 * The class rank (FSimCruiseRoadNetwork::ClassRank) of the public street a facade faces
	 * within about 12 m, or -1 (none, or an alley or service way).
	 */
	int32 StreetRankFaced(const FSimCruiseRoadNetwork& Roads, const FVector2d& Mid, const FVector2d& N2)
	{
		for (const double D : { 3.0, 7.0, 11.0 })
		{
			FSimCruiseRoadHit Hit;
			if (Roads.FindCarriageway(Mid + N2 * D, Hit))
			{
				if (Hit.Ribbon < 0) return 0;
				return Roads.Ribbons[Hit.Ribbon].bService ? -1 : Roads.Ribbons[Hit.Ribbon].Rank;
			}
		}
		return -1;
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
	// Main Street's painted and brick fronts (Charleston, Savannah, the Midwest's brick rows):
	// the generic paints' warmer half, four brick reds, and the Lowcountry's pastels.
	const TArray<FLinearColor>& MainStreetWalls()
	{
		static const TArray<FLinearColor> Colours = { Srgb(TEXT("8B4838")), Srgb(TEXT("76503F")), Srgb(TEXT("9F5A42")), Srgb(TEXT("A98C6A")),
			Srgb(TEXT("E9DEC4")), Srgb(TEXT("C9D8C5")), Srgb(TEXT("E6C9A8")), Srgb(TEXT("B8C7D4")), Srgb(TEXT("F0E2B6")), Srgb(TEXT("D8B4A0")),
			Srgb(TEXT("ECE9E1")), Srgb(TEXT("8D8372")) };
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
	FLinearColor WeatheredPaint(const FLinearColor& C, float Max)
	{
		const float L = C.GetLuminance();
		if (L <= Max || L <= 0.f) return C;
		const float K = Max / L;
		return FLinearColor(C.R * K, C.G * K, C.B * K, C.A);
	}

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

	FSignStyle SignStyleFor(const FString& K)
	{
		FSignStyle S;
		if (K == TEXT("lodging") || K == TEXT("hotel") || K == TEXT("motel") || K == TEXT("hostel") || K == TEXT("guest_house"))
		{
			S.Rank = 3; S.bNeon = true; S.bHotel = true;
		}
		else if (K == TEXT("bar") || K == TEXT("nightclub") || K == TEXT("pub") || K == TEXT("cinema") || K == TEXT("theatre"))
		{
			S.Rank = 2; S.bNeon = true;
		}
		else
		{
			static const TSet<FString> Painted = { TEXT("restaurant"), TEXT("cafe"), TEXT("fast_food"), TEXT("ice_cream"), TEXT("bakery"),
				TEXT("shop"), TEXT("clothing_store"), TEXT("grocery"), TEXT("supermarket"), TEXT("convenience"), TEXT("pharmacy"), TEXT("bank"),
				TEXT("alcohol_shop"), TEXT("art_gallery"), TEXT("museum"), TEXT("hairdresser"), TEXT("bicycle_rental"), TEXT("beer"),
				TEXT("laundry"), TEXT("office"), TEXT("library"), TEXT("post"), TEXT("post_office"), TEXT("fire_station"), TEXT("police"),
				TEXT("town_hall"), TEXT("hospital"), TEXT("doctors"), TEXT("veterinary"), TEXT("yoga"), TEXT("boxing"), TEXT("car"),
				TEXT("bicycle"), TEXT("fuel"), TEXT("school"), TEXT("college"), TEXT("sports_centre") };
			if (Painted.Contains(K)) S.Rank = 1;
		}
		return S;
	}

	FString SignText(const FString& Name, bool bHotel)
	{
		FString T = Name;
		// A branch or location tail after a separator.
		for (const TCHAR* Sep : { TEXT(" | "), TEXT(" - "), TEXT(" – "), TEXT(" @ "), TEXT(" (") })
		{
			const int32 At = T.Find(Sep);
			if (At > 0) T.LeftInline(At);
		}
		// Words that name the area rather than the place.
		const TCHAR* Tails[] = { TEXT(" South Beach"), TEXT(" Miami Beach"), TEXT(" Miami"), TEXT(" Ocean Drive"), TEXT(" Collins Avenue"),
			TEXT(" Downtown"), TEXT(" Beach Front"), TEXT(" Beachfront") };
		const TCHAR* HotelWords[] = { TEXT(" Hotel"), TEXT(" Hotels"), TEXT(" Resort"), TEXT(" & Spa"), TEXT(" and Spa"), TEXT(" Suites"), TEXT(" Inn") };
		for (int32 Pass = 0; Pass < 3; ++Pass)
		{
			for (const TCHAR* Tail : Tails)
			{
				if (T.EndsWith(Tail, ESearchCase::IgnoreCase) && T.Len() > FCString::Strlen(Tail) + 2) T.LeftChopInline(FCString::Strlen(Tail));
			}
			if (bHotel)
			{
				for (const TCHAR* W : HotelWords)
				{
					if (T.EndsWith(W, ESearchCase::IgnoreCase) && T.Len() > FCString::Strlen(W) + 2) T.LeftChopInline(FCString::Strlen(W));
				}
			}
			T.TrimStartAndEndInline();
		}
		if (bHotel && T.StartsWith(TEXT("Hotel "), ESearchCase::IgnoreCase) && T.Len() > 8) T.RightChopInline(6);
		T.TrimStartAndEndInline();
		// Long names keep their first words.
		while (T.Len() > 22)
		{
			int32 Space = INDEX_NONE;
			if (!T.FindLastChar(TEXT(' '), Space) || Space < 4) { T.LeftInline(22); break; }
			T.LeftInline(Space);
		}
		return bHotel ? T.ToUpper() : T;
	}

	void RoundCorners(const TArray<FVector2d>& Ring, double Sign, const TArray<bool>& Front, const TArray<bool>& Keep, double MaxRadiusM,
		TArray<FVector2d>& OutRing, TArray<int32>& OutSource)
	{
		const int32 N = Ring.Num();
		OutRing.Reset();
		OutSource.Reset();
		auto Dir = [&](int32 i) { return (Ring[(i + 1) % N] - Ring[i]).GetSafeNormal(); };
		auto Len = [&](int32 i) { return FVector2d::Distance(Ring[i], Ring[(i + 1) % N]); };
		auto Normal = [&](int32 i) { const FVector2d D = Dir(i); return FVector2d(D.Y, -D.X) * Sign; };
		constexpr int32 Segments = 4;
		for (int32 i = 0; i < N; ++i)
		{
			const int32 Prev = (i + N - 1) % N;
			const FVector2d D0 = Dir(Prev), D1 = Dir(i);
			const FVector2d N0 = Normal(Prev), N1 = Normal(i);
			// Convex (the next edge turns in), near square (45-135 degrees), on a street front,
			// both sides long enough, and neither side kept sharp (a porch's corners).
			const bool bConvex = FVector2d::DotProduct(D1, N0) < -0.7;
			const double Turn = FMath::Acos(FMath::Clamp(FVector2d::DotProduct(D0, D1), -1.0, 1.0));
			const double Short = FMath::Min(Len(Prev), Len(i));
			const bool bRound = bConvex && Turn > FMath::DegreesToRadians(45.0) && Turn < FMath::DegreesToRadians(135.0)
				&& (Front[Prev] || Front[i]) && !Keep[Prev] && !Keep[i] && Short >= 5.0;
			if (!bRound)
			{
				OutRing.Add(Ring[i]);
				OutSource.Add(i);
				continue;
			}
			const double Radius = FMath::Min(MaxRadiusM, 0.22 * Short);
			const double Tangent = Radius * FMath::Tan(0.5 * Turn);
			const FVector2d P0 = Ring[i] - D0 * Tangent;
			const FVector2d Centre = P0 - N0 * Radius;
			const double A0 = FMath::Atan2(N0.Y, N0.X);
			double Delta = FMath::Atan2(N1.Y, N1.X) - A0;
			while (Delta > UE_DOUBLE_PI) Delta -= UE_DOUBLE_TWO_PI;
			while (Delta < -UE_DOUBLE_PI) Delta += UE_DOUBLE_TWO_PI;
			for (int32 k = 0; k <= Segments; ++k)
			{
				const double A = A0 + Delta * k / Segments;
				OutRing.Add(Centre + FVector2d(FMath::Cos(A), FMath::Sin(A)) * Radius);
				OutSource.Add(k < Segments ? INDEX_NONE : i); // the arc's chords, then the edge on from P1
			}
		}
	}

	bool RoofColourFromAerial(const FContext& X, const TArray<FVector2d>& Ring, FLinearColor& OutColour)
	{
		const FSimCruiseAerialImage* A = X.Aerial;
		if (!A || A->Width <= 0 || Ring.Num() < 3) return false;
		const FIntPoint Z14 = SimCruiseGeo::AerialTileOf(X.Data.Id);
		const FVector2d CellUnit(X.Data.Id.X - Z14.X * 4, X.Data.Id.Y - Z14.Y * 4);
		const double PxM = X.Side * 4.0 / A->Width; // metres a pixel
		FBox2d Box(Ring);
		const int32 X0 = FMath::Max(0, FMath::FloorToInt(((Box.Min.X / X.Side) + CellUnit.X) / 4.0 * A->Width));
		const int32 X1 = FMath::Min(A->Width - 1, FMath::CeilToInt(((Box.Max.X / X.Side) + CellUnit.X) / 4.0 * A->Width));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt(((Box.Min.Y / X.Side) + CellUnit.Y) / 4.0 * A->Height));
		const int32 Y1 = FMath::Min(A->Height - 1, FMath::CeilToInt(((Box.Max.Y / X.Side) + CellUnit.Y) / 4.0 * A->Height));
		TArray<FLinearColor> In;
		for (int32 py = Y0; py <= Y1; ++py)
		for (int32 px = X0; px <= X1; ++px)
		{
			const FVector2d P(((px + 0.5) / A->Width * 4.0 - CellUnit.X) * X.Side, ((py + 0.5) / A->Height * 4.0 - CellUnit.Y) * X.Side);
			if (!PointInRing(Ring, P)) continue;
			double Edge = 1e9;
			for (int32 i = 0; i < Ring.Num(); ++i) Edge = FMath::Min(Edge, FVector2d::Distance(P, FMath::ClosestPointOnSegment2D(P, Ring[i], Ring[(i + 1) % Ring.Num()])));
			if (Edge < 0.5 * PxM) continue;
			In.Add(FLinearColor::FromSRGBColor(A->Pixels[py * A->Width + px]));
		}
		if (In.Num() < 6) return false;
		auto Median = [&In](int32 C) { TArray<float> V; for (const FLinearColor& L : In) V.Add(C == 0 ? L.R : C == 1 ? L.G : L.B); V.Sort(); return V[V.Num() / 2]; };
		FLinearColor Col(Median(0), Median(1), Median(2), 1.0f);
		const float L = FMath::Max(0.2126f * Col.R + 0.7152f * Col.G + 0.0722f * Col.B, 1e-4f);
		const float Mx = FMath::Max3(Col.R, Col.G, Col.B), Mn = FMath::Min3(Col.R, Col.G, Col.B);
		const float Sat = (Mx - Mn) / FMath::Max(Mx, 1e-4f);
		if (Sat > 0.45f) Col = FLinearColor(L, L, L) + (Col - FLinearColor(L, L, L)) * (0.45f / Sat);
		Col *= FMath::Clamp(L, 0.04f, 0.60f) / L;
		Col.A = 1.0f;
		OutColour = Col;
		return true;
	}

	void BuildBuildings(const FContext& X)
	{
		FSimCruiseTileBuild& B = X.B;
		const FSimCruiseTileData& Data = X.Data;
		const FSimCruiseRoadNetwork& Roads = B.Roads;
		FSimCruiseMeshData& Mesh = B.Layer(ESimCruiseTileLayer::Building);
		const EBuildingStyle Place = StyleForPlace(X.LatDeg, X.LonDeg);
		// The place knowledge graph (data/knowledge/place-graph.json): each building's recipe,
		// colours and how likely each detail is, from its open data, its neighbours, the region.
		FSimCruiseTilePlan Plan;
		const bool bGraph = SimCruisePlaceGraph::Rules().Rules.Num() > 0;
		if (bGraph) SimCruisePlaceHub::BakeForBuild(Data, X.LatDeg, X.LonDeg, X.Side, Plan);

		// Footprints in metres, and each named place on its building: the footprint it stands
		// in, or the nearest within 12 m (map points often sit on the sidewalk at the door).
		TArray<TArray<FVector2d>> Rings;
		TArray<double> Areas;
		Rings.SetNum(Data.Buildings.Num());
		Areas.Init(0.0, Data.Buildings.Num());
		for (int32 b = 0; b < Data.Buildings.Num(); ++b)
		{
			if (!FootprintMetres(Data.Buildings[b], X.Side, Rings[b], Areas[b])) Rings[b].Reset();
		}
		TMultiMap<int32, int32> PlacesOf;
		for (int32 p = 0; p < Data.Pois.Num(); ++p)
		{
			if (SignStyleFor(Data.Pois[p].Kind).Rank == 0) continue;
			const FVector2d P = Data.Pois[p].Unit * X.Side;
			int32 Best = INDEX_NONE;
			double BestD = 12.0;
			for (int32 b = 0; b < Rings.Num(); ++b)
			{
				const TArray<FVector2d>& R = Rings[b];
				if (R.Num() < 3 || Data.Buildings[b].MinHeightM > 0.5) continue;
				FBox2d Box(R);
				if (!Box.ExpandBy(BestD).IsInside(P)) continue;
				if (PointInRing(R, P)) { Best = b; break; }
				for (int32 i = 0; i < R.Num(); ++i)
				{
					const FVector2d Q = FMath::ClosestPointOnSegment2D(P, R[i], R[(i + 1) % R.Num()]);
					const double D = FVector2d::Distance(P, Q);
					if (D < BestD) { BestD = D; Best = b; }
				}
			}
			if (Best != INDEX_NONE) PlacesOf.Add(Best, p);
		}

		// Unknown heights. The helper writes 5 m for a footprint with no height (Overture's
		// render_height default): two thirds of Ocean Drive, whose hotels are three and four
		// storeys. Such a building takes the lower-quartile height of the measured buildings
		// within 120 m (a street keeps its scale), up to 14 m, a storey more or less by hash; a hotel at least
		// three storeys; a house at most two. With fewer than three measured neighbours it
		// keeps 5 m.
		TArray<double> HeightOf;
		HeightOf.SetNumUninitialized(Data.Buildings.Num());
		{
			TArray<FVector2d> Centres;
			Centres.SetNum(Rings.Num());
			for (int32 b = 0; b < Rings.Num(); ++b)
			{
				FVector2d C = FVector2d::ZeroVector;
				for (const FVector2d& P : Rings[b]) C += P;
				Centres[b] = Rings[b].Num() > 0 ? C / Rings[b].Num() : FVector2d::ZeroVector;
			}
			for (int32 b = 0; b < Data.Buildings.Num(); ++b)
			{
				const double Raw = Data.Buildings[b].HeightM;
				HeightOf[b] = Raw;
				if (!IsUnknownHeight(Raw) || Rings[b].Num() < 3 || Data.Buildings[b].MinHeightM > 0.5) continue;
				TArray<double> Known;
				for (int32 o = 0; o < Rings.Num(); ++o)
				{
					if (o == b || Rings[o].Num() < 3 || IsUnknownHeight(Data.Buildings[o].HeightM) || Data.Buildings[o].MinHeightM > 0.5) continue;
					if (FVector2d::DistSquared(Centres[o], Centres[b]) < 120.0 * 120.0) Known.Add(Data.Buildings[o].HeightM);
				}
				if (Known.Num() < 3) continue;
				Known.Sort();
				// The lower quartile, not the median: the measured buildings are the notable, taller
				// ones (towers get surveyed); an unmeasured one is ordinary, and at most four storeys.
				double Est = Known[Known.Num() / 4];
				const uint32 Hb = Hash(GetTypeHash(Data.Id), static_cast<uint32>(b) * 31u + 7u);
				Est += 3.2 * (static_cast<int32>(Hb % 3) - 1);
				bool bHotelHere = false;
				TArray<int32> Here;
				PlacesOf.MultiFind(b, Here);
				for (const int32 p : Here) bHotelHere |= SignStyleFor(Data.Pois[p].Kind).bHotel;
				if (bHotelHere) Est = FMath::Max(Est, 10.5);
				HeightOf[b] = FMath::Clamp(Est, 3.5, 14.0);
			}
		}

		for (int32 b = 0; b < Data.Buildings.Num(); ++b)
		{
			const FSimCruiseBuildingRow& Row = Data.Buildings[b];
			if (Rings[b].Num() < 3) continue;
			const TArray<FVector2d>& Footprint = Rings[b];
			const double Area = Areas[b];
			double Ground = TNumericLimits<double>::Max();
			for (const FVector2d& P : Footprint) Ground = FMath::Min(Ground, Data.HeightAtUnit(P / X.Side));
			const double Sign = Area >= 0.0 ? 1.0 : -1.0;
			const FSimCruiseBuildingPlan* PlanOf = bGraph ? Plan.Find(b) : nullptr;
			// A house keeps to two storeys when its height was a guess.
			const double Guess = (IsUnknownHeight(Row.HeightM) && PlanOf && PlanOf->P(TEXT("recipe:house")) >= 0.5f) ? FMath::Min(HeightOf[b], 7.5) : HeightOf[b];
			const double Height = FMath::Clamp(Guess, 2.8, 650.0);
			const double Top = Ground + Height;
			const bool bFloating = Row.MinHeightM > 0.5;
			const double Bottom = bFloating ? Ground + Row.MinHeightM : Ground - 0.3;
			if (Top <= Bottom + 0.5) continue;

			const uint32 H = Hash(GetTypeHash(Data.Id), static_cast<uint32>(b));
			FWriter W{ Mesh, static_cast<float>(Unit(Hash(H, 7))) };
			const FSimCruiseBuildingPlan* BP = PlanOf;
			const bool bGraphDeco = BP && BP->P(TEXT("recipe:deco")) >= 0.5f;
			const bool bGraphTower = BP && !bGraphDeco && BP->P(TEXT("recipe:resort_tower")) >= 0.5f;
			const bool bRegionDeco = (BP ? (bGraphDeco || bGraphTower) : Place == EBuildingStyle::MiamiBeachDeco) && !bFloating;
			const bool bDeco = BP ? bGraphDeco && !bFloating : bRegionDeco && Height <= 26.0;
			const bool bTower = BP ? bGraphTower && !bFloating : bRegionDeco && Height > 26.0;
			// Probability of a detail: the graph's, else the recipe's own default.
			// Given that the recipe is deco, a deco detail's chance is the graph's divided by how sure
			// the graph is of the deco family (an undated district hotel keeps the full set).
			const double DecoSure = BP ? FMath::Max(static_cast<double>(BP->P(TEXT("family:deco"))), 0.05) : 1.0;
			auto Chance = [BP, DecoSure](const TCHAR* Detail, double Default)
			{
				return BP ? FMath::Min(1.0, static_cast<double>(BP->P(Detail, static_cast<float>(Default * DecoSure))) / DecoSure) : Default;
			};
			// The town recipes for the rest of the US (the graph's recipe:house, main_street, downtown):
			// a house under a pitched roof, a Main Street block with a cornice and belt course, a
			// downtown tower in a glass curtain wall. Their details read the graph's chances directly.
			auto Recipe = [BP](const TCHAR* Key) { return BP && BP->P(Key) >= 0.5f; };
			auto ChanceOf = [BP](const TCHAR* Detail, double Default) { return BP ? static_cast<double>(BP->P(Detail, static_cast<float>(Default))) : Default; };
			const bool bPlain = !bDeco && !bTower && !bFloating;
			const bool bHouse = bPlain && Height <= 14.0 && Recipe(TEXT("recipe:house"));
			const bool bMainStreet = bPlain && !bHouse && Recipe(TEXT("recipe:main_street"));
			const bool bDowntown = bPlain && !bHouse && !bMainStreet && Height >= 20.0 && (Recipe(TEXT("recipe:downtown")) || (!BP && Height > 45.0));
			const bool bPitched = bHouse && Unit(Hash(H, 25)) < ChanceOf(TEXT("detail:pitched_roof"), 0.85);
			const bool bCurtain = bDowntown && Unit(Hash(H, 26)) < ChanceOf(TEXT("detail:curtain_wall"), 0.6);
			const bool bCornice = bMainStreet && Unit(Hash(H, 27)) < ChanceOf(TEXT("detail:cornice"), 0.8);
			// Main Street's commercial blocks of 1880-1930 have tall storeys (a 4.5-5 m shop storey,
			// 3.8-4.2 m above): 12 m is three storeys there, not four.
			const double FloorH = bDeco ? 3.1 : bHouse ? 3.0 : bDowntown ? 3.8 : bMainStreet ? 4.2 : 3.2;
			const double Parapet = bPitched ? 0.0 : bDeco ? (Height > 5.0 ? 0.9 : 0.4) : (bTower ? 1.1 : bMainStreet ? 0.9 : 0.5);
			FLinearColor Wall = bDeco ? Pick(DecoWalls(), H) : (bTower ? Pick(TowerWalls(), H) : Pick(GenericWalls(), H));
			// A colour mapped on the building wins; else the region's palette for ordinary buildings
			// (the deco and tower palettes above are matched to Ocean Drive photographs).
			// A colour measured from a photograph is the camera's sRGB of the wall in sun, not its
			// albedo: a white wall the camera puts at 0.85-0.9 linear is paint of about 0.65-0.7.
			// Scale it as the Lummus Park comparison did (photo 191 vs game 210 at 0.88), so a
			// measured white sits under the cap with room for wear, not on it.
			if (BP && BP->bColourFromData) Wall = BP->Wall * 0.78f;
			else if (BP && BP->bHasColour && !bDeco && !bTower) Wall = BP->Wall;
			// Main Street's two- and three-storey blocks are mostly brick (the generic palette's last four).
			if (bMainStreet && !(BP && BP->bHasColour) && Unit(Hash(H, 24)) < 0.6) Wall = GenericWalls()[12 + Hash(H, 28) % 4];
			if (BP) Wall = BP->Weathered(Wall);
			Wall = WeatheredPaint(Wall, bTower || bDowntown ? MaxTowerAlbedo : MaxWallAlbedo);
			// The shader reads a storey height other than 3.1 m from the wall colour's alpha.
			Wall.A = FMath::IsNearlyEqual(FloorH, 3.1) ? 1.f : static_cast<float>(FMath::Clamp((FloorH - 2.4) / 4.0, 0.0, 0.99));
			// Half the hotels keep white trim; the rest a contrasting accent.
			const FLinearColor Accent = WeatheredPaint((bDeco && Unit(Hash(H, 3)) < Chance(TEXT("detail:accent_trim"), 0.55)) ? Pick(DecoAccents(), Hash(H, 4)) : Srgb(TEXT("FAF8F2")), MaxTrimAlbedo);
			const FLinearColor Neon = Pick(NeonColours(), Hash(H, 5));
			const bool bNeonParapet = bDeco && Unit(Hash(H, 6)) < Chance(TEXT("detail:neon_parapet"), 0.5);
			const bool bEyebrowsAround = bDeco && Unit(Hash(H, 8)) < Chance(TEXT("detail:eyebrows_wrap"), 0.5);
			const bool bFloorBands = bDeco && Unit(Hash(H, 33)) < Chance(TEXT("detail:parapet_bands"), 0.3);
			// Bands need a colour that shows on the stucco: the trim, or a deco accent when the trim is white.
			const FLinearColor BandColour = Accent.GetLuminance() < 0.6f ? Accent : Pick(DecoAccents(), Hash(H, 34) % 6);
			const int32 Floors = FMath::Max(1, FMath::FloorToInt((Height + 0.2) / FloorH));
			TArray<int32> Places;
			PlacesOf.MultiFind(b, Places);
			// Sidewalk cafes: a hotel, restaurant, cafe or bar on the building sets tables under
			// umbrellas along its street front (Ocean Drive's porches and sidewalks).
			bool bDiners = false;
			for (const int32 p : Places)
			{
				const FString& K = Data.Pois[p].Kind;
				bDiners |= K == TEXT("restaurant") || K == TEXT("cafe") || K == TEXT("bar") || K == TEXT("lodging") || K == TEXT("ice_cream") || K == TEXT("fast_food");
			}
			const bool bCafe = bDiners && !bFloating && Unit(Hash(H, 36)) < ChanceOf(TEXT("detail:outdoor_seating"), bDeco ? 0.75 : 0.35);
			static const TCHAR* UmbrellaHex[] = { TEXT("F2C14E"), TEXT("F5F1E6"), TEXT("E5739A"), TEXT("38B6C2"), TEXT("E07A5F"), TEXT("34558B"), TEXT("2F8F8A"), TEXT("C0392B") };
			const FLinearColor Umbrella = Srgb(UmbrellaHex[Hash(H, 37) % 8]);

			// Street fronts on the footprint as mapped; porches on the fronts that can take one.
			const int32 N0 = Footprint.Num();
			auto EdgeNormalOf = [&](const TArray<FVector2d>& R, int32 i)
			{
				const FVector2d D = R[(i + 1) % R.Num()] - R[i];
				return FVector2d(D.Y, -D.X).GetSafeNormal() * Sign;
			};
			auto EdgeDirOf = [&](const TArray<FVector2d>& R, int32 i) { return (R[(i + 1) % R.Num()] - R[i]).GetSafeNormal(); };
			TArray<bool> Front0, Porch0;
			TArray<double> PorchDepth0;
			TArray<int32> Rank0;
			Rank0.Init(-1, N0);
			Front0.Init(false, N0);
			Porch0.Init(false, N0);
			PorchDepth0.Init(0.0, N0);
			for (int32 i = 0; i < N0; ++i)
			{
				const FVector2d A = Footprint[i], C = Footprint[(i + 1) % N0];
				const double Len = FVector2d::Distance(A, C);
				if (Len < 0.5) continue;
				const FVector2d N2 = EdgeNormalOf(Footprint, i);
				Rank0[i] = (bDeco || bTower || bMainStreet || bDowntown || Places.Num() > 0) ? StreetRankFaced(Roads, (A + C) * 0.5, N2) : -1;
				Front0[i] = Rank0[i] >= 0;
				// A porch: Ocean Drive's raised front terraces under the upper floors, here when
				// both corners are convex and the building is deep enough to give up 2.2 m.
				const double PrevLen = FVector2d::Distance(Footprint[(i + N0 - 1) % N0], A);
				const double NextLen = FVector2d::Distance(C, Footprint[(i + 2) % N0]);
				const bool bConvexA = FVector2d::DotProduct(EdgeDirOf(Footprint, (i + N0 - 1) % N0), N2) > 0.5;
				const bool bConvexC = FVector2d::DotProduct(EdgeDirOf(Footprint, (i + 1) % N0), N2) < -0.5;
				PorchDepth0[i] = FMath::Min(2.2, 0.35 * FMath::Min(PrevLen, NextLen));
				Porch0[i] = bDeco && Front0[i] && Len >= 8.0 && Height >= 6.0 && bConvexA && bConvexC && PorchDepth0[i] >= 1.5 && Unit(Hash(H, 9)) < Chance(TEXT("detail:porch"), 0.8);
			}

			// Streamline Moderne: most of the district's hotels round their street corners, and
			// the eyebrows and speed lines wrap round them (the Carlyle, the Cardozo, the Leslie).
			TArray<FVector2d> Ring;
			TArray<int32> Source;
			if (bDeco && Height >= 6.0 && Unit(Hash(H, 21)) < Chance(TEXT("detail:round_corners"), 0.7))
			{
				RoundCorners(Footprint, Sign, Front0, Porch0, 2.2, Ring, Source);
			}
			else
			{
				Ring = Footprint;
				for (int32 i = 0; i < N0; ++i) Source.Add(i);
			}
			const int32 N = Ring.Num();
			auto EdgeNormal = [&](int32 i) { return EdgeNormalOf(Ring, i); };
			TArray<bool> Front, Arc, PorchEdge;
			TArray<double> PorchDepth;
			for (int32 i = 0; i < N; ++i)
			{
				const int32 S = Source[i];
				Arc.Add(S == INDEX_NONE);
				Front.Add(S == INDEX_NONE ? true : Front0[S]);
				PorchEdge.Add(S == INDEX_NONE ? false : Porch0[S]);
				PorchDepth.Add(S == INDEX_NONE ? 0.0 : PorchDepth0[S]);
			}

			// The main front carries the tower and the hotel's name: on a corner, the side on the
			// bigger street (the Carlyle fronts Ocean Drive, not 13th Street), then the longer.
			int32 Longest = INDEX_NONE;
			double LongestLen = 0.0, BestScore = -1.0;
			for (int32 i = 0; i < N; ++i)
			{
				const double Len = FVector2d::Distance(Ring[i], Ring[(i + 1) % N]);
				if (!Front[i] || Arc[i] || Len < 6.0) continue;
				const double Score = Rank0[Source[i]] * 1000.0 + Len;
				if (Score > BestScore) { BestScore = Score; LongestLen = Len; Longest = i; }
			}
			// The tower's form: a stepped central tower (the Colony, the Breakwater), three
			// pylons with flagpoles (the Carlyle), or twin fins (the Avalon, the Majestic).
			const double FormPick = Unit(Hash(H, 22));
			const bool bHasTower = bDeco && Longest != INDEX_NONE && LongestLen >= 10.0 && Height >= 6.0;
			// The graph weighs the three forms (a Streamline Moderne front leans to pylons and fins).
			const double WCentral = Chance(TEXT("detail:central_tower"), 0.45), WPylons = Chance(TEXT("detail:pylons"), 0.35), WFins = Chance(TEXT("detail:twin_fins"), 0.2);
			const double WSum = FMath::Max(WCentral + WPylons + WFins, 1e-3);
			const int32 TowerForm = !bHasTower ? -1 : FormPick < WCentral / WSum ? 0 : FormPick < (WCentral + WPylons) / WSum ? 1 : 2;

			// Where the lettering goes: each named place on the street edge nearest it, the
			// hotel first; spans already lettered on an edge are not used again.
			Places.Sort([&](int32 L, int32 R) { return SignStyleFor(Data.Pois[L].Kind).Rank > SignStyleFor(Data.Pois[R].Kind).Rank; });
			TArray<TArray<FVector3d>> Taken;  // per edge: (from, to along it, band: 0 fascia, 1 parapet)
			Taken.SetNum(N);
			bool bHotelSign = false;
			auto AddSign = [&](const FString& Text, const FVector2d& At, const FVector2d& N2, double Z, double LetterM, double MaxSpanM, bool bStacked, bool bNeonSign, const FLinearColor& Colour)
			{
				FSimCruiseSign S;
				S.Text = Text;
				S.LocationCm = Cm3(At, Z);
				S.YawDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(N2.Y, N2.X)));
				S.LetterCm = static_cast<float>(LetterM * 100.0);
				S.MaxSpanCm = static_cast<float>(MaxSpanM * 100.0);
				S.bStacked = bStacked;
				S.bNeon = bNeonSign;
				S.Colour = Colour;
				B.Signs.Add(MoveTemp(S));
			};
			// Painted letters contrast with the wall: the trim colour when it is dark enough, else near black.
			const FLinearColor Ink = Accent.GetLuminance() < 0.25f ? Accent : (Wall.GetLuminance() > 0.2f ? FLinearColor(0.025f, 0.025f, 0.03f) : FLinearColor(0.85f, 0.84f, 0.8f));

			TArray<bool> Awning;
			Awning.Init(false, N);
			for (int32 i = 0; i < N; ++i)
			{
				const double Len = FVector2d::Distance(Ring[i], Ring[(i + 1) % N]);
				Awning[i] = bDeco && Front[i] && !Arc[i] && !PorchEdge[i] && Len >= 3.0 && Unit(Hash(H, 12 + i)) < Chance(TEXT("detail:awnings"), 0.75);
			}

			for (const int32 p : Places)
			{
				const FSimCruisePoi& Poi = Data.Pois[p];
				const FSignStyle Style = SignStyleFor(Poi.Kind);
				const FString Text = SignText(Poi.Name, Style.bHotel);
				if (Text.IsEmpty()) continue;
				const FVector2d P = Poi.Unit * X.Side;
				int32 Edge = INDEX_NONE;
				double EdgeD = TNumericLimits<double>::Max();
				for (int32 i = 0; i < N; ++i)
				{
					if (!Front[i] || Arc[i] || FVector2d::Distance(Ring[i], Ring[(i + 1) % N]) < 3.0) continue;
					const double D = FVector2d::Distance(P, FMath::ClosestPointOnSegment2D(P, Ring[i], Ring[(i + 1) % N]));
					if (D < EdgeD) { EdgeD = D; Edge = i; }
				}
				const bool bHotel = Style.bHotel && !bHotelSign;
				if (bHotel && Longest != INDEX_NONE) Edge = Longest; // a hotel signs its main front
				if (Edge == INDEX_NONE) continue;
				const FVector2d A = Ring[Edge], C = Ring[(Edge + 1) % N];
				const double Len = FVector2d::Distance(A, C);
				const FVector2d T = (C - A) / Len;
				const FVector2d N2 = EdgeNormal(Edge);
				const FLinearColor Colour = Style.bNeon ? (bHotel ? Neon : Pick(NeonColours(), Hash(H, 60 + p))) : Ink;

				// The hotel: up the central tower's face, letter over letter; on the parapet; or
				// over the porch or shopfronts (the Carlyle).
				if (bHotel && Edge == Longest && TowerForm == 0)
				{
					const double Width = FMath::Clamp(LongestLen * 0.16, 2.2, 3.6);
					const double Rise = 2.0 + 2.5 * Unit(Hash(H, 11));
					const double Z0 = Ground + FloorH + 0.8, Z1 = Top + Parapet + Rise - 0.5;
					const int32 Letters = FMath::Max(1, Text.Replace(TEXT(" "), TEXT("")).Len());
					const double Letter = FMath::Min3((Width - 0.5) / 0.8, 0.95, (Z1 - Z0) / (Letters * 1.05));
					if (Letter >= 0.3)
					{
						AddSign(Text, (A + C) * 0.5 + N2 * 0.47, N2, 0.5 * (Z0 + Z1), Letter, Z1 - Z0, true, true, Colour);
						bHotelSign = true;
						continue;
					}
				}
				// No central tower: a blade standing out of the front, the name letter over letter on
				// both faces, rising over the parapet (the Colony, the Breakwater, the Avalon).
				if (bHotel && bDeco && Edge == Longest && TowerForm != 0 && Height >= 5.5 && Unit(Hash(H, 35)) < Chance(TEXT("detail:vertical_sign"), 0.5))
				{
					const int32 Letters = FMath::Max(1, Text.Replace(TEXT(" "), TEXT("")).Len());
					const double Z0 = Ground + FloorH + 0.4;
					const double Z1 = FMath::Max(Top + Parapet + 1.6, Z0 + Letters * 0.6);
					const double Letter = FMath::Min(0.7, (Z1 - Z0 - 0.4) / (Letters * 1.05));
					if (Letter >= 0.3)
					{
						// Off centre when a pylon or fin stands at the middle.
						const double At = TowerForm < 0 ? 0.5 * Len : FMath::Clamp(0.5 * Len - 0.3 * Len, 1.5, Len - 1.5);
						const FVector2d Root = A + T * At;
						constexpr double Reach = 1.15, Thick = 0.16;
						W.Slab(Root - T * (0.5 * Thick), Root + T * (0.5 * Thick), N2, 0.0, Reach, Z0, Z1, Wall, Kind::Trim, 0.0, Ground, false, true);
						// Neon round the blade's outer edge.
						W.Slab(Root - T * 0.03, Root + T * 0.03, N2, Reach, Reach + 0.05, Z0 + 0.15, Z1 - 0.15, Colour, Kind::Neon, 0.0, Ground, false, true);
						for (const double Side : { -1.0, 1.0 })
						{
							AddSign(Text, Root + T * (Side * (0.5 * Thick + 0.02)) + N2 * (0.5 * Reach), T * Side, 0.5 * (Z0 + Z1), Letter, Z1 - Z0 - 0.3, true, true, Colour);
						}
						bHotelSign = true;
						continue;
					}
				}
				const bool bParapet = bHotel && Height >= 6.0 && Unit(Hash(H, 23)) < 0.5;
				double Z, Letter;
				double Out = 0.04;
				if (bParapet)
				{
					Letter = FMath::Clamp(Parapet * 0.72, 0.3, 0.75);
					Z = Top + 0.5 * Parapet;
				}
				else if (Awning[Edge] && !bHotel)
				{
					// On the awning's valance, 1.3 m out.
					Letter = 0.17;
					Z = Ground + 2.5 - 0.14;
					Out = 1.32;
				}
				else if (Height >= FloorH + 1.2)
				{
					// The fascia between the shop glass (or porch) and the first-floor windows.
					Letter = bHotel ? 0.7 : 0.4;
					Z = Ground + FloorH + 0.42;
				}
				else
				{
					Letter = 0.32;
					Z = FMath::Min(Ground + 2.92, Top - 0.25);
				}
				// Along the edge at the place, inside the free span.
				const double Want = FMath::Min(Text.Len() * 0.62 * Letter + 0.2, Len - 0.8);
				if (Want < 1.0) continue;
				double S = FMath::Clamp(FVector2d::DotProduct(P - A, T), 0.4 + 0.5 * Want, Len - 0.4 - 0.5 * Want);
				if (bHotel) S = 0.5 * Len;
				const double Band = bParapet ? 1.0 : 0.0;
				bool bFree = true;
				for (const FVector3d& Span : Taken[Edge])
				{
					if (Span.Z == Band && S + 0.5 * Want > Span.X && S - 0.5 * Want < Span.Y) bFree = false;
				}
				if (!bFree) continue;
				Taken[Edge].Add(FVector3d(S - 0.5 * Want - 0.3, S + 0.5 * Want + 0.3, Band));
				AddSign(Text, A + T * S + N2 * Out, N2, Z, Letter, Want, false, Style.bNeon, Colour);
				if (bHotel) bHotelSign = true;
			}

			for (int32 i = 0; i < N; ++i)
			{
				const FVector2d A = Ring[i], C = Ring[(i + 1) % N];
				const double Len = FVector2d::Distance(A, C);
				if (Len < 0.05) continue;
				const FVector2d T = (C - A) / Len;
				const FVector2d N2 = EdgeNormal(i);
				const double Along0 = -0.5 * Len; // windows centre on each facade
				const bool bPorch = PorchEdge[i];
				// Ends that meet a rounded corner run on into it (no gap in the wrapped bands).
				const bool bArcBefore = Arc[(i + N - 1) % N], bArcAfter = Arc[(i + 1) % N];
				const double Ext = Arc[i] ? 0.06 : 0.0;
				const FVector2d BandA = Arc[i] ? A - T * Ext : (bArcBefore ? A : A + T * 0.15);
				const FVector2d BandC = Arc[i] ? C + T * Ext : (bArcAfter ? C : C - T * 0.15);

				const double WallStart = bPorch ? Ground + FloorH : Bottom;
				// Street fronts: shop glass in the ground storey, and on Art Deco hotels the
				// coloured uplight wash that lights Ocean Drive at night. A rounded corner is
				// plain stucco (its windows would be cut by the chords).
				const float WallKind = Arc[i] ? Kind::Trim : !Front[i] ? Kind::Wall : bPorch ? (bDeco ? Kind::DecoUpper : Kind::Wall) : (bDeco ? Kind::DecoFront : Kind::WallFront);
				if (bCurtain)
				{
					// A glass curtain wall over a lobby storey: mullions every 1.5 m and a spandrel
					// band at every floor slab.
					const double Lobby = Ground + 4.5;
					W.Panel(A, C, WallStart, Lobby, N2, Wall, Front[i] ? Kind::WallFront : Kind::Wall, Along0, Ground);
					W.Panel(A, C, Lobby, Top + Parapet, N2, Wall, Kind::Railing, Along0, Ground);
					const FLinearColor Frame = Srgb(TEXT("3A3F44"));
					const int32 Mullions = FMath::FloorToInt(Len / 1.5);
					for (int32 k = 1; k < Mullions; ++k)
					{
						const FVector2d P = A + T * (k * Len / Mullions);
						W.Slab(P - T * 0.035, P + T * 0.035, N2, 0.0, 0.12, Lobby, Top + Parapet, Frame, Kind::Metal, 0.0, Ground, false, false);
					}
					for (double Z = Lobby + FloorH; Z < Top - 0.5; Z += FloorH)
					{
						W.Slab(A, C, N2, 0.0, 0.06, Z - 0.45, Z + 0.05, Frame, Kind::Metal, Along0, Ground, false, true);
					}
				}
				else
				{
					if (bMainStreet && Front[i] && Len >= 12.0 && !(BP && BP->bColourFromData))
					{
						// A long Main Street front is a row of narrow lots (6-9 m, as the town was
						// platted): each its own paint or brick, its parapet a little higher or lower,
						// a pilaster at each party wall, its windows centred on its own front.
						double S0 = 0.0;
						int32 Lot = 0;
						while (S0 < Len - 0.5)
						{
							double Width = 6.0 + 3.0 * Unit(Hash(H, 200 + Lot));
							if (Len - (S0 + Width) < 4.0) Width = Len - S0;
							const FVector2d L0 = A + T * S0, L1 = A + T * (S0 + Width);
							FLinearColor LotWall = Lot == 0 ? Wall : MainStreetWalls()[Hash(H, 220 + Lot) % static_cast<uint32>(MainStreetWalls().Num())];
							if (BP && Lot > 0) LotWall = BP->Weathered(LotWall);
							LotWall = WeatheredPaint(LotWall);
							LotWall.A = Wall.A;
							const double Rise = 0.35 * (Hash(H, 240 + Lot) % 3);
							W.Panel(L0, L1, WallStart, Top + Parapet + Rise, N2, LotWall, WallKind, -0.5 * Width, Ground);
							if (Rise > 0.0) W.Panel(L1, L0, Top + Parapet, Top + Parapet + Rise, -N2, LotWall, Kind::Trim, -0.5 * Width, Ground);
							if (S0 > 0.0) W.Slab(L0 - T * 0.15, L0 + T * 0.15, N2, 0.0, 0.07, WallStart, Top + Parapet - 0.3, LotWall * 0.85f, Kind::Trim, 0.0, Ground, false, false);
							S0 += Width;
							++Lot;
						}
					}
					else
					{
						W.Panel(A, C, WallStart, Top + Parapet, N2, Wall, WallKind, Along0, Ground);
					}
				}
				// The parapet's inside, seen from above and from taller neighbours.
				if (Parapet > 0.05) W.Panel(C, A, Top, Top + Parapet, -N2, Wall, Kind::Trim, Along0, Ground);
				// Main Street: a projecting cornice under the parapet's coping and a belt course
				// over the shopfronts, in a cream or darker trim (the commercial blocks of 1880-1930).
				if (bMainStreet && Front[i] && Len >= 2.0)
				{
					const FLinearColor Cornice = Unit(Hash(H, 29)) < 0.5 ? Srgb(TEXT("E8DFC9")) : Wall * 0.72f;
					if (bCornice)
					{
						W.Slab(A, C, N2, 0.0, 0.42, Top + Parapet - 0.32, Top + Parapet - 0.08, Cornice, Kind::Trim, Along0, Ground, false, true);
						W.Slab(A, C, N2, 0.0, 0.18, Top + Parapet - 0.62, Top + Parapet - 0.32, Cornice, Kind::Trim, Along0, Ground, false, true);
						W.Slab(A, C, N2, 0.0, 0.08, Top - 0.45, Top - 0.2, Cornice, Kind::Trim, Along0, Ground, false, true);
					}
					if (Height >= FloorH + 2.0)
					{
						W.Slab(A, C, N2, 0.0, 0.12, Ground + FloorH - 0.05, Ground + FloorH + 0.25, Cornice, Kind::Trim, Along0, Ground, false, true);
					}
				}

				if (bPorch)
				{
					const double PorchD = PorchDepth[i];
					const double Floor = Ground + 0.45;
					const double Ceiling = Ground + FloorH;
					const FVector2d Ai = A - N2 * PorchD, Ci = C - N2 * PorchD;
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

				// Tables under market umbrellas on the sidewalk (or the porch) in front of the street
				// front, every 3.2 m, clear of the carriageway.
				if (bCafe && Front[i] && !Arc[i] && Len >= 5.0)
				{
					FSimCruiseMeshData& PropsMesh = B.Layer(ESimCruiseTileLayer::Props);
					FWriter Pw{ PropsMesh, W.Seed };
					const FLinearColor Pole = Srgb(TEXT("D8D8D2")), Table = Srgb(TEXT("E9E7E1"));
					const double Out = bPorch ? -0.5 * PorchDepth[i] : 2.3;
					const double Floor = bPorch ? Ground + 0.45 : Ground + 0.1;
					for (double Sx = 1.6; Sx <= Len - 1.6; Sx += 3.2)
					{
						const FVector2d P = A + T * Sx + N2 * Out;
						FSimCruiseRoadHit Hit;
						if (!bPorch && Roads.FindCarriageway(P, Hit, INDEX_NONE, 0.6)) continue;
						// Pole and table.
						Pw.Slab(P - T * 0.025, P + T * 0.025, N2, -0.025, 0.025, Floor, Floor + 2.35, Pole, Kind::Metal, 0.0, Ground, true, false);
						Pw.Slab(P - T * 0.4, P + T * 0.4, N2, -0.4, 0.4, Floor + 0.72, Floor + 0.76, Table, Kind::Trim, 0.0, Ground, true, true);
						// The canopy: an octagonal cone, 2.6 m across, its rim at 2.15 m.
						const FVector3d Apex(P.X, P.Y, Floor + 2.6);
						FVector3d Rim[8];
						for (int32 k = 0; k < 8; ++k)
						{
							const double Ang = UE_DOUBLE_TWO_PI * (k + 0.5) / 8.0;
							Rim[k] = FVector3d(P.X + 1.3 * FMath::Cos(Ang), P.Y + 1.3 * FMath::Sin(Ang), Floor + 2.15);
						}
						const int32 Top0 = PropsMesh.Positions.Num();
						PropsMesh.Add(FVector(Apex * 100.0), FVector::UpVector, Umbrella, FVector2D(0.0, 2.6), FVector2D(Kind::Trim, W.Seed));
						for (int32 k = 0; k < 8; ++k)
						{
							const FVector Nrm = FVector(FVector3d(Rim[k].X - P.X, Rim[k].Y - P.Y, 1.3 * 1.3 / 0.45).GetSafeNormal());
							PropsMesh.Add(FVector(Rim[k] * 100.0), Nrm, Umbrella, FVector2D(k * 1.0, 2.15), FVector2D(Kind::Trim, W.Seed));
						}
						const int32 Under0 = PropsMesh.Positions.Num();
						PropsMesh.Add(FVector((Apex - FVector3d(0, 0, 0.02)) * 100.0), -FVector::UpVector, Umbrella * 0.75f, FVector2D(0.0, 2.6), FVector2D(Kind::Trim, W.Seed));
						for (int32 k = 0; k < 8; ++k)
						{
							PropsMesh.Add(FVector((Rim[k] - FVector3d(0, 0, 0.02)) * 100.0), -FVector::UpVector, Umbrella * 0.75f, FVector2D(k * 1.0, 2.15), FVector2D(Kind::Trim, W.Seed));
						}
						for (int32 k = 0; k < 8; ++k)
						{
							PropsMesh.Tri(Top0, Top0 + 1 + k, Top0 + 1 + (k + 1) % 8, FVector::UpVector);
							PropsMesh.Tri(Under0, Under0 + 1 + k, Under0 + 1 + (k + 1) % 8, -FVector::UpVector);
						}
					}
				}

				// Canvas awnings over the shopfronts: 1.3 m deep, sloping out from the storey line.
				if (Awning[i])
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

				// Eyebrows: cantilevered sunshades over each upper storey's windows, wrapping the
				// rounded corners.
				if (bDeco && Floors >= 2 && (Len >= 3.0 || Arc[i]) && (Front[i] || bEyebrowsAround))
				{
					for (int32 k = 1; k < Floors; ++k)
					{
						const double Z = Ground + k * FloorH + 2.45;
						if (Z > Top - 0.25) break;
						W.Slab(BandA, BandC, N2, 0.0, 0.5, Z, Z + 0.1, Accent, Kind::Trim, Along0, Ground);
					}
				}
				// Painted bands along the floor lines in the trim colour, round the street fronts
				// (the Breakwater's blue stripes, the Avalon's yellow): flush paint, not ledges.
				if (bDeco && bFloorBands && Front[i] && Floors >= 2)
				{
					for (int32 k = 1; k < Floors; ++k)
					{
						const double Z = Ground + k * FloorH;
						if (Z > Top - 0.4) break;
						W.Slab(Arc[i] ? BandA : A, Arc[i] ? BandC : C, N2, 0.0, 0.015, Z - 0.22, Z + 0.2, BandColour, Kind::Trim, Along0, Ground, false, true);
					}
				}
				// Speed lines near the parapet, and a neon tube along it on some hotels.
				if (bDeco && Front[i] && Height > 5.0)
				{
					for (const double Dz : { 0.35, 0.62, 0.89 })
					{
						W.Slab(Arc[i] ? BandA : A, Arc[i] ? BandC : C, N2, 0.0, 0.05, Top - Dz, Top - Dz + 0.11, Accent, Kind::Trim, Along0, Ground);
					}
					if (bNeonParapet)
					{
						W.Slab(Arc[i] ? BandA : A + T * 0.1, Arc[i] ? BandC : C - T * 0.1, N2, 0.02, 0.07, Top + Parapet - 0.18, Top + Parapet - 0.13, Neon, Kind::Neon, Along0, Ground);
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

			if (TowerForm >= 0)
			{
				const FVector2d A = Ring[Longest], C = Ring[(Longest + 1) % N];
				const FVector2d T = (C - A).GetSafeNormal();
				const FVector2d N2 = EdgeNormal(Longest);
				const FVector2d Mid = (A + C) * 0.5;
				const double Z0 = Ground + FloorH;
				const FLinearColor Steel = Srgb(TEXT("B8BCBE"));
				if (TowerForm == 0)
				{
					// The stepped tower over the entrance, with neon up its edges.
					const double Width = FMath::Clamp(LongestLen * 0.16, 2.2, 3.6);
					const double Rise = 2.0 + 2.5 * Unit(Hash(H, 11));
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
				else if (TowerForm == 1)
				{
					// Three pylons rising through the parapet, the parapet stepped up between them,
					// a flagpole on each.
					const double Spacing = FMath::Clamp(LongestLen * 0.15, 2.4, 4.6);
					const double Rise = 0.9 + 0.8 * Unit(Hash(H, 11));
					W.Slab(Mid - T * Spacing, Mid + T * Spacing, N2, -0.3, 0.02, Top + Parapet - 0.05, Top + Parapet + 0.7, Wall, Kind::Trim, -Spacing, Ground, true, false);
					for (int32 k = -1; k <= 1; ++k)
					{
						const FVector2d Pc = Mid + T * (k * Spacing);
						const double Z1 = Top + Parapet + Rise + (k == 0 ? 0.5 : 0.0);
						W.Slab(Pc - T * 0.32, Pc + T * 0.32, N2, -0.4, 0.32, Z0 + 0.4, Z1, Wall, Kind::Trim, -0.32, Ground, true, true);
						// Grooves: two slim ribs down each pylon's face.
						for (const double Sx : { -0.16, 0.16 })
						{
							W.Slab(Pc + T * (Sx - 0.035), Pc + T * (Sx + 0.035), N2, 0.32, 0.38, Z0 + 0.6, Z1 - 0.3, Accent, Kind::Trim, 0.0, Ground, false, true);
						}
						const FVector2d F = Pc - N2 * 0.04;
						W.Slab(F - T * 0.025, F + T * 0.025, N2, -0.025, 0.025, Z1, Z1 + 4.5, Steel, Kind::Metal, 0.0, Ground, true, false);
					}
				}
				else
				{
					// Twin fins standing out of the front, framing the entrance bay, taller than the roof.
					const double Gap = FMath::Clamp(LongestLen * 0.12, 1.6, 3.2);
					const double Z1 = Top + Parapet + 1.6 + 1.6 * Unit(Hash(H, 11));
					for (const double Sx : { -1.0, 1.0 })
					{
						const FVector2d Pc = Mid + T * (Sx * Gap);
						W.Slab(Pc - T * 0.12, Pc + T * 0.12, N2, -0.3, 0.85, Z0 - 0.2, Z1, Accent, Kind::Trim, 0.0, Ground, true, true);
						W.Slab(Pc - T * 0.03, Pc + T * 0.03, N2, 0.85, 0.9, Z0 + 0.3, Z1 - 0.2, Neon, Kind::Neon, 0.0, Ground, false, true);
					}
					W.Slab(Mid - T * Gap, Mid + T * Gap, N2, -0.3, 0.02, Top + Parapet - 0.05, Top + Parapet + 0.9, Wall, Kind::Trim, -Gap, Ground, true, false);
				}
			}

			// A house's pitched roof over the footprint's box (when the footprint nearly fills it):
			// a hip or a gable along the long side, a 6:12 pitch, 0.35 m eaves.
			bool bRoofDone = false;
			if (bPitched)
			{
				int32 LongEdge = 0;
				double LongLen = 0.0;
				for (int32 i = 0; i < N; ++i)
				{
					const double D = FVector2d::Distance(Ring[i], Ring[(i + 1) % N]);
					if (D > LongLen) { LongLen = D; LongEdge = i; }
				}
				const FVector2d U = EdgeDirOf(Ring, LongEdge);
				const FVector2d V(-U.Y, U.X);
				double U0 = 1e9, U1 = -1e9, V0 = 1e9, V1 = -1e9;
				for (const FVector2d& P : Ring)
				{
					U0 = FMath::Min(U0, FVector2d::DotProduct(P, U)); U1 = FMath::Max(U1, FVector2d::DotProduct(P, U));
					V0 = FMath::Min(V0, FVector2d::DotProduct(P, V)); V1 = FMath::Max(V1, FVector2d::DotProduct(P, V));
				}
				const double BoxArea = (U1 - U0) * (V1 - V0);
				if (BoxArea > 1.0 && FMath::Abs(Area) / BoxArea > 0.78 && (V1 - V0) >= 3.0)
				{
					constexpr double Eave = 0.35;
					U0 -= Eave; U1 += Eave; V0 -= Eave; V1 += Eave;
					const double Half = 0.5 * (V1 - V0);
					const double Rise = FMath::Min(0.5 * Half, 3.5);
					const double Zr = Top + Rise;
					auto At = [&](double Uu, double Vv, double Z) { const FVector2d P = U * Uu + V * Vv; return FVector3d(P.X, P.Y, Z); };
					const bool bTile = Unit(Hash(H, 30)) < ChanceOf(TEXT("detail:tile_roof"), 0.1);
					static const TCHAR* Shingles[] = { TEXT("5E5E5C"), TEXT("4A4642"), TEXT("6F6457"), TEXT("545A61"), TEXT("3F3D3B"), TEXT("7A746C") };
					static const TCHAR* Tiles[] = { TEXT("B5603E"), TEXT("C2734D"), TEXT("A45237"), TEXT("C98A62") };
					FLinearColor RoofC = bTile ? Srgb(Tiles[Hash(H, 31) % 4]) : Srgb(Shingles[Hash(H, 31) % 6]);
					// What the photograph shows of this roof, when it shows enough (the palette otherwise).
					FLinearColor Seen;
					if (RoofColourFromAerial(X, Ring, Seen)) RoofC = Seen;
					const double Vm = 0.5 * (V0 + V1);
					auto Face = [&](std::initializer_list<FVector3d> Pts, const FLinearColor& C, float SurfaceKind, const FVector& Facing)
					{
						TArray<FVector3d> P(Pts);
						const FVector Nrm = FVector(FVector3d::CrossProduct(P[1] - P[0], P[2] - P[0]).GetSafeNormal());
						const FVector Out = FVector::DotProduct(Nrm, Facing) >= 0.0 ? Nrm : -Nrm;
						const int32 First = Mesh.Positions.Num();
						for (const FVector3d& Q : P)
						{
							Mesh.Add(FVector(Q * 100.0), Out, C, FVector2D(FVector2d::DotProduct(FVector2d(Q.X, Q.Y), U), Q.Z - Ground), FVector2D(SurfaceKind, W.Seed));
						}
						for (int32 k = 1; k + 1 < P.Num(); ++k) Mesh.Tri(First, First + k, First + k + 1, Out);
					};
					const FVector Up = FVector::UpVector;
					const FVector Uo(U.X, U.Y, 0.0), Vo(V.X, V.Y, 0.0);
					const bool bHip = Unit(Hash(H, 32)) < 0.55 && (U1 - U0) > (V1 - V0) * 1.05;
					const double R0 = bHip ? U0 + Half : U0, R1 = bHip ? U1 - Half : U1;
					// The two long slopes.
					Face({ At(U0, V0, Top), At(U1, V0, Top), At(R1, Vm, Zr), At(R0, Vm, Zr) }, RoofC, Kind::Roof, Up - Vo);
					Face({ At(U1, V1, Top), At(U0, V1, Top), At(R0, Vm, Zr), At(R1, Vm, Zr) }, RoofC, Kind::Roof, Up + Vo);
					if (bHip)
					{
						Face({ At(U0, V1, Top), At(U0, V0, Top), At(R0, Vm, Zr) }, RoofC, Kind::Roof, Up - Uo);
						Face({ At(U1, V0, Top), At(U1, V1, Top), At(R1, Vm, Zr) }, RoofC, Kind::Roof, Up + Uo);
					}
					else
					{
						// Gable ends: the wall carried up to the ridge on the wall line.
						const double Zg = Zr - Rise * Eave / Half;
						Face({ At(U0 + Eave, V0 + Eave, Top), At(U0 + Eave, V1 - Eave, Top), At(U0 + Eave, Vm, Zg) }, Wall, Kind::Trim, -Uo);
						Face({ At(U1 - Eave, V1 - Eave, Top), At(U1 - Eave, V0 + Eave, Top), At(U1 - Eave, Vm, Zg) }, Wall, Kind::Trim, Uo);
					}
					// Soffits under the eaves.
					Face({ At(U0, V0, Top - 0.02), At(U1, V0, Top - 0.02), At(U1, V1, Top - 0.02), At(U0, V1, Top - 0.02) }, Srgb(TEXT("E8E6E0")), Kind::Trim, -Up);
					bRoofDone = true;
				}
			}

			// Roof: a light, gravel-and-membrane cool roof.
			if (!bRoofDone)
			{
				TArray<int32> Tris;
				FLinearColor RoofColour = bRegionDeco ? Srgb(TEXT("C9C6BF")) : FLinearColor(0.42f, 0.41f, 0.39f);
				// The roof as the photograph shows it (membrane, gravel, tar, rooftop gardens).
				FLinearColor SeenRoof;
				if (!bFloating && RoofColourFromAerial(X, Ring, SeenRoof)) RoofColour = SeenRoof;
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
		// The context record says which palms grow here (Köppen class and region); without one, the old rule.
		const float Weight = X.Cells ? X.Cells->SpeciesWeight(TEXT("cocos_nucifera")) : 1.0f;
		if (!IsCoconutPalmRegion(X.LatDeg, X.LonDeg) || Weight <= 0.0f || CVarPalms.GetValueOnAnyThread() == 0) return;
		FSimCruiseTileBuild& B = X.B;
		const FSimCruiseRoadNetwork& Roads = B.Roads;

		// Lummus Park and the city's lawns: coconut palms about 10 m apart, never on a
		// path, a road or its first 2.5 m, in water or a building. Each is an instance of
		// one of the shared palm meshes (USimCruiseWorldResources::PalmMeshes).
		constexpr double Spacing = 10.5;
		const int32 Cells = FMath::FloorToInt(X.Side / Spacing);
		const uint32 TileHash = X.Cells ? X.Cells->TileSeed : GetTypeHash(X.Data.Id);
		for (int32 gy = 0; gy < Cells; ++gy)
		for (int32 gx = 0; gx < Cells; ++gx)
		{
			const uint32 H = Hash(TileHash, static_cast<uint32>(gy * 4099 + gx));
			if (Unit(H) > 0.42 * Weight) continue;
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

	void BuildPromenades(const FContext& X)
	{
		FSimCruiseTileBuild& B = X.B;
		const FSimCruiseTileData& Data = X.Data;
		FSimCruiseMeshData& Props = B.Layer(ESimCruiseTileLayer::Props);
		// Sand on the tile (land cover "sand" or "beach"), in metres, with boxes for a quick reject.
		TArray<TArray<FVector2d>> Sand;
		TArray<FBox2d> SandBox;
		for (const FSimCruiseLandPolygon& L : Data.Land)
		{
			if (L.Class != TEXT("sand") && L.Class != TEXT("beach")) continue;
			for (const TArray<FVector2d>& R : L.Rings)
			{
				TArray<FVector2d> M;
				for (const FVector2d& U : R) M.Add(U * X.Side);
				if (M.Num() < 3) continue;
				SandBox.Add(FBox2d(M));
				Sand.Add(MoveTemp(M));
			}
		}
		auto IsSand = [&](const FVector2d& P)
		{
			for (int32 k = 0; k < Sand.Num(); ++k)
			{
				if (SandBox[k].IsInside(P) && PointInRing(Sand[k], P)) return true;
			}
			return false;
		};
		const FLinearColor Coral = Srgb(TEXT("C8BEA6")), Cap = Srgb(TEXT("D9D3C4"));
		const FLinearColor BenchC = Srgb(TEXT("9FB2BF")), PoleC = Srgb(TEXT("E6E6E2"));
		const FLinearColor LensColour(1.0f, 0.88f, 0.72f);
		for (int32 r = 0; r < Data.Pedestrian.Num(); ++r)
		{
			const FSimCruisePathRow& Row = Data.Pedestrian[r];
			if (Row.Points.Num() < 2) continue;
			const double HalfW = 0.5 * FMath::Clamp(Row.WidthM, 1.0, 6.0);
			const bool bPromenade = Row.WidthM >= 3.0 || Row.Kind == TEXT("pedestrian");
			FWriter W{ Props, static_cast<float>(Unit(Hash(GetTypeHash(Data.Id), static_cast<uint32>(r) + 977u))) };
			double Along = 0.0;
			for (int32 i = 0; i + 1 < Row.Points.Num(); ++i)
			{
				const FVector2d A = Row.Points[i] * X.Side, C = Row.Points[i + 1] * X.Side;
				const double Len = FVector2d::Distance(A, C);
				if (Len < 0.2) continue;
				const FVector2d T = (C - A) / Len;
				const FVector2d Rt(-T.Y, T.X);
				// The seawall: in 2 m pieces wherever one side of the path is sand and the other not.
				const int32 Pieces = FMath::Max(1, FMath::RoundToInt(Len / 2.0));
				for (int32 k = 0; k < Pieces; ++k)
				{
					const FVector2d P0 = A + T * (Len * k / Pieces), P1 = A + T * (Len * (k + 1) / Pieces);
					const FVector2d Mid = (P0 + P1) * 0.5;
					for (const double Sx : { -1.0, 1.0 })
					{
						const FVector2d Out = Rt * Sx;
						if (!IsSand(Mid + Out * (HalfW + 1.2)) || IsSand(Mid - Out * (HalfW + 1.2))) continue;
						const double Z = B.TerrainHeightAt(Mid + Out * (HalfW + 0.25));
						const FVector2d W0 = P0 + Out * HalfW, W1 = P1 + Out * HalfW;
						W.Slab(W0 - T * 0.03, W1 + T * 0.03, Out, 0.0, 0.5, Z - 0.4, Z + 0.62, Coral, Kind::Terrazzo, 0.0, Z, false, false);
						W.Slab(W0 - T * 0.03, W1 + T * 0.03, Out, -0.02, 0.52, Z + 0.62, Z + 0.7, Cap, Kind::Trim, 0.0, Z, false, true);
					}
				}
				if (!bPromenade) { Along += Len; continue; }
				// Benches every 24 m and lamp posts every 30 m, on the side away from the sand
				// (or the right), anchored to the path's length so they run on along it.
				const bool bSandLeft = IsSand((A + C) * 0.5 - Rt * (HalfW + 1.2));
				const FVector2d Land = bSandLeft ? Rt : -Rt;
				for (double Mark = FMath::CeilToDouble(Along / 24.0) * 24.0 + 6.0; Mark < Along + Len; Mark += 24.0)
				{
					const double S = Mark - Along;
					if (S < 0.0 || S > Len) continue;
					const FVector2d P = A + T * S + Land * (HalfW + 0.55);
					if (IsInsideBuilding(Data, X.Side, P) || X.IsWater(P)) continue;
					FSimCruiseRoadHit Hit;
					if (B.Roads.FindCarriageway(P, Hit, INDEX_NONE, 1.0)) continue;
					const double Z = B.TerrainHeightAt(P);
					const FVector2d Face = -Land; // the bench looks over the path
					const FVector2d S0 = P - T * 0.9, S1 = P + T * 0.9;
					W.Slab(S0, S1, Face, -0.22, 0.22, Z + 0.42, Z + 0.47, BenchC, Kind::Metal, 0.0, Z, false, true);
					W.Slab(S0, S1, Face, -0.26, -0.21, Z + 0.5, Z + 0.86, BenchC, Kind::Metal, 0.0, Z, true, false);
					for (const double Lx : { -0.75, 0.75 })
					{
						const FVector2d L = P + T * Lx;
						W.Slab(L - T * 0.03, L + T * 0.03, Face, -0.2, 0.2, Z - 0.05, Z + 0.42, BenchC * 0.7f, Kind::Metal, 0.0, Z, true, false);
					}
				}
				for (double Mark = FMath::CeilToDouble(Along / 30.0) * 30.0 + 15.0; Mark < Along + Len; Mark += 30.0)
				{
					const double S = Mark - Along;
					if (S < 0.0 || S > Len) continue;
					const FVector2d P = A + T * S - Land * (HalfW + 0.4);
					if (IsInsideBuilding(Data, X.Side, P) || X.IsWater(P) || IsSand(P)) continue;
					FSimCruiseRoadHit Hit;
					if (B.Roads.FindCarriageway(P, Hit, INDEX_NONE, 1.0)) continue;
					const double Z = B.TerrainHeightAt(P);
					constexpr double PoleH = 4.2;
					W.Slab(P - T * 0.06, P + T * 0.06, Rt, -0.06, 0.06, Z - 0.05, Z + PoleH, PoleC, Kind::Metal, 0.0, Z, true, false);
					// A white drum lantern with a disc cap (the beach walk's lamps).
					W.Slab(P - T * 0.17, P + T * 0.17, Rt, -0.17, 0.17, Z + PoleH, Z + PoleH + 0.45, LensColour, Kind::LampGlass, 0.0, Z, true, true);
					W.Slab(P - T * 0.3, P + T * 0.3, Rt, -0.3, 0.3, Z + PoleH + 0.45, Z + PoleH + 0.52, PoleC, Kind::Metal, 0.0, Z, true, true);
					B.LampPointsCm.Add(Cm3(P, Z + PoleH + 0.2));
					++B.StreetLights;
				}
				Along += Len;
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
