// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseCellContext.h"

#include "HAL/PlatformTime.h"
#include "Knowledge/SimCruiseClimate.h"
#include "Knowledge/SimCruiseRoadsideData.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileDetail.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseContext, Log, All);

namespace
{
	uint32 Mix(uint32 A, uint32 B) { return HashCombine(A * 2654435761u, B + 0x9E3779B9u); }
	uint32 Q(double V, double Step) { return static_cast<uint32>(FMath::RoundToInt64(V / Step)); }

	/** A roadside way in tile metres. */
	struct FWayM
	{
		int32 Index = INDEX_NONE;
		TArray<FVector2d> P;
		FBox2d Box = FBox2d(ForceInit);
	};

	FVector2d ToTileM(const FIntPoint& Tile, double Side, double Lon, double Lat)
	{
		return (SimCruiseGeo::LonLatToTile(Lon, Lat) - FVector2d(Tile.X, Tile.Y)) * Side;
	}

	void ToWaysM(const FIntPoint& Tile, double Side, const TArray<FSimCruiseRoadsideWay>& In, TArray<FWayM>& Out)
	{
		for (int32 w = 0; w < In.Num(); ++w)
		{
			FWayM W;
			W.Index = w;
			for (const FVector2d& LL : In[w].LonLat) W.P.Add(ToTileM(Tile, Side, LL.X, LL.Y));
			if (W.P.Num() < 2) continue;
			W.Box = FBox2d(W.P);
			Out.Add(MoveTemp(W));
		}
	}

	/** Closest point of a polyline: distance and the unit direction of that segment. */
	double ClosestOnPolyline(const TArray<FVector2d>& P, const FVector2d& X, FVector2d& OutDir)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i + 1 < P.Num(); ++i)
		{
			const FVector2d Q2 = FMath::ClosestPointOnSegment2D(X, P[i], P[i + 1]);
			const double D = FVector2d::Distance(X, Q2);
			if (D < Best) { Best = D; OutDir = (P[i + 1] - P[i]).GetSafeNormal(); }
		}
		return Best;
	}

	/** The ribbon's interpolated station at S (C, T, R, half width) and the station index below it. */
	int32 StationAt(const FSimCruiseRibbon& R, double S, FVector2d& C, FVector2d& T, FVector2d& Rt, double& HalfW)
	{
		int32 Lo = 0, Hi = R.St.Num() - 1;
		while (Hi - Lo > 1)
		{
			const int32 Mid = (Lo + Hi) / 2;
			if (R.St[Mid].S <= S) Lo = Mid; else Hi = Mid;
		}
		const FSimCruiseRibbonStation& A = R.St[Lo];
		const FSimCruiseRibbonStation& B = R.St[Hi];
		const double Span = B.S - A.S;
		const double Tt = Span > 1e-6 ? FMath::Clamp((S - A.S) / Span, 0.0, 1.0) : 0.0;
		C = FMath::Lerp(A.C, B.C, Tt);
		T = FMath::Lerp(A.T, B.T, Tt).GetSafeNormal();
		Rt = FMath::Lerp(A.R, B.R, Tt).GetSafeNormal();
		HalfW = FMath::Lerp(A.HalfW, B.HalfW, Tt);
		return Lo;
	}

	FName Lower(const FString& S) { return S.IsEmpty() ? NAME_None : FName(*S.ToLower()); }

	int32 BarrierRank(FName Kind)
	{
		// What a driver reads first at the road edge.
		if (Kind == TEXT("guard_rail")) return 4;
		if (Kind == TEXT("retaining_wall")) return 3;
		if (Kind == TEXT("wall")) return 2;
		if (Kind == TEXT("fence")) return 1;
		return 0;
	}

	/** Points binned in square cells (tile metres), for "what is near here" without a full scan. */
	struct FBins
	{
		double CellM = 32.0;
		TMap<FIntPoint, TArray<int32>> Cells;
		FIntPoint Key(const FVector2d& P) const { return FIntPoint(FMath::FloorToInt(P.X / CellM), FMath::FloorToInt(P.Y / CellM)); }
		void Add(const FVector2d& P, int32 Index) { Cells.FindOrAdd(Key(P)).Add(Index); }
		template <typename F> void Near(const FVector2d& P, double R, F&& Visit) const
		{
			const FIntPoint A = Key(P - FVector2d(R, R)), B = Key(P + FVector2d(R, R));
			for (int32 y = A.Y; y <= B.Y; ++y)
			for (int32 x = A.X; x <= B.X; ++x)
			{
				if (const TArray<int32>* L = Cells.Find(FIntPoint(x, y))) for (const int32 i : *L) Visit(i);
			}
		}
	};

	FCriticalSection ContextLock;
	using FContextRef = TSharedRef<const FSimCruiseTileContext, ESPMode::ThreadSafe>;
	TMap<FIntPoint, FContextRef>& ContextCache() { static TMap<FIntPoint, FContextRef> C; return C; }
}

float FSimCruiseTileContext::SpeciesWeight(FName Name) const
{
	for (const TPair<FName, float>& S : Species) if (S.Key == Name) return S.Value;
	return 0.0f;
}

const FSimCruiseCellContext* FSimCruiseTileContext::Nearest(const FVector2d& P, double MaxM) const
{
	const FSimCruiseCellContext* Best = nullptr;
	double BestD = MaxM;
	for (const FSimCruiseCellContext& C : Cells)
	{
		const double Half = 0.5 * (C.S1 - C.S0);
		const FVector2d Q = FMath::ClosestPointOnSegment2D(P, C.CentreM - C.Tangent * Half, C.CentreM + C.Tangent * Half);
		const double D = FMath::Max(0.0, FVector2d::Distance(P, Q) - C.HalfW);
		if (D <= BestD) { BestD = D; Best = &C; }
	}
	return Best;
}

const FSimCruiseCellContext* FSimCruiseTileContext::At(int32 Ribbon, double S) const
{
	if (!RibbonCells.IsValidIndex(Ribbon)) return nullptr;
	const FIntPoint Range = RibbonCells[Ribbon];
	for (int32 i = Range.X; i < Range.X + Range.Y; ++i)
	{
		const FSimCruiseCellContext& C = Cells[i];
		if (S >= C.S0 - 1e-6 && S <= C.S1 + 1e-6) return &C;
	}
	return nullptr;
}

FString FSimCruiseTileContext::Describe(int32 MaxCells) const
{
	FString Text = FString::Printf(TEXT("Context %s: %d cells (%d on a mapped OSM way of %d), Köppen %s, %s, %s, grass dry %.2f, %d roadside points; bake %.2f ms"),
		*SimCruiseTileName(Tile), Cells.Num(), MatchedCells, MappedWays, Koppen ? SimCruiseClimate::KoppenCode(Koppen) : TEXT("-"), *Climate.ToString(), *Season.ToString(),
		GrassDry, Points.Num(), BakeMs);
	for (int32 i = 0; i < FMath::Min(MaxCells, Cells.Num()); ++i)
	{
		const FSimCruiseCellContext& C = Cells[i];
		Text += FString::Printf(TEXT("\n  %s %s s%.0f-%.0f: %s%s, lit %s, edge %d/%d, sidewalk %s/%s, barrier %s/%s, land %s/%s, seed %08x"),
			*C.RoadClass.ToString(), C.StreetName.IsEmpty() ? TEXT("(unnamed)") : *C.StreetName, C.S0, C.S1, *C.Surface.ToString(), C.bSurfaceMapped ? TEXT(" (OSM)") : TEXT(""),
			*C.Lit.ToString(), C.Sides[0].Edge, C.Sides[1].Edge, *C.Sides[0].Sidewalk.ToString(), *C.Sides[1].Sidewalk.ToString(),
			*C.Sides[0].Barrier.ToString(), *C.Sides[1].Barrier.ToString(), *C.Sides[0].LandUse.ToString(), *C.Sides[1].LandUse.ToString(), C.Seed);
	}
	return Text;
}

namespace SimCruiseCellContext
{
	int32 CurrentMonth()
	{
		int32 M = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("SimCruiseMonth="), M) && M >= 1 && M <= 12) return M;
		return FDateTime::Now().GetMonth();
	}

	uint32 StableSeed(int64 WayId, int32 WayStation, const FVector2d& GlobalM, FName RoadClass)
	{
		if (WayId != 0) return Mix(Mix(static_cast<uint32>(WayId), static_cast<uint32>(WayId >> 32)), static_cast<uint32>(WayStation) * 7919u + 1u);
		// No mapped way: the global (Web Mercator) position on a 4 m grid (a rebuild moves a centre
		// by centimetres; the session's start place does not matter).
		return Mix(Mix(Q(GlobalM.X, 4.0), Q(GlobalM.Y, 4.0)), GetTypeHash(RoadClass));
	}

	double CellMercatorM(double LatDeg)
	{
		// Web Mercator stretches the ground by 1 / cos(latitude). By 5 degree bands, so every tile
		// of a band cuts its cells on the same global marks (within 4 % of 32 m on the ground).
		const double Band = FMath::Clamp(FMath::RoundToDouble(LatDeg / 5.0) * 5.0, -80.0, 80.0);
		return CellM / FMath::Cos(FMath::DegreesToRadians(Band));
	}

	void SpeciesFor(uint8 Koppen, FName Climate, double Lat, double Lon, TArray<TPair<FName, float>>& Out)
	{
		// Estimates by climate family until item 6 (OSM, city inventories, GBIF) measures them.
		Out.Reset();
		auto Add = [&Out](const TCHAR* Name, float W) { Out.Add(TPair<FName, float>(FName(Name), W)); };
		const FString C = Climate.ToString();
		// Coconut palms wherever they live outdoors (USDA zone 10b and warmer: the curated
		// SimCruiseTileDetail::IsCoconutPalmRegion), whatever class a 0.05 degree grid cell has:
		// leeward Oahu and Kona are BSh there and line their streets with coconut palms.
		if (SimCruiseTileDetail::IsCoconutPalmRegion(Lat, Lon)) Add(TEXT("cocos_nucifera"), 1.0f);
		const bool bTropical = (Koppen >= 1 && Koppen <= 3) || (Koppen == 0 && C == TEXT("climate:tropical"));
		if (bTropical)
		{
			Add(TEXT("roystonea_regia"), 0.4f);
			Add(TEXT("sabal_palmetto"), 0.3f);
			Add(TEXT("ficus"), 0.3f);
			return;
		}
		if (C == TEXT("climate:humid_subtropical"))
		{
			Add(TEXT("pinus_taeda"), 0.5f);
			Add(TEXT("quercus"), 0.5f);
			Add(TEXT("acer_rubrum"), 0.3f);
			Add(TEXT("liquidambar_styraciflua"), 0.3f);
			if (Lat < 31.5 && Lon > -100.0) Add(TEXT("sabal_palmetto"), 0.3f);
			if (Lat < 33.0) Add(TEXT("quercus_virginiana"), 0.4f);
			return;
		}
		if (C == TEXT("climate:mediterranean"))
		{
			Add(TEXT("quercus_agrifolia"), 0.4f);
			Add(TEXT("washingtonia_robusta"), 0.4f);
			Add(TEXT("pinus"), 0.3f);
			return;
		}
		if (C == TEXT("climate:arid"))
		{
			Add(TEXT("prosopis"), 0.5f);
			if (Koppen == 4) Add(TEXT("washingtonia_filifera"), 0.3f);
			if (Koppen == 5 || Koppen == 7) Add(TEXT("juniperus"), 0.4f);
			return;
		}
		if (C == TEXT("climate:oceanic"))
		{
			if (Lon < -115.0) Add(TEXT("pseudotsuga_menziesii"), 0.6f);
			Add(TEXT("acer"), 0.4f);
			Add(TEXT("quercus"), 0.3f);
			return;
		}
		if (C == TEXT("climate:humid_continental"))
		{
			Add(TEXT("acer_saccharum"), 0.5f);
			Add(TEXT("quercus_rubra"), 0.4f);
			Add(TEXT("pinus_strobus"), 0.3f);
			return;
		}
		if (C == TEXT("climate:subarctic"))
		{
			Add(TEXT("picea"), 0.6f);
			Add(TEXT("betula"), 0.4f);
			return;
		}
		if (C == TEXT("climate:polar")) Add(TEXT("salix"), 0.2f);
	}

	uint32 DataHash(const FInputs& In)
	{
		uint32 H = Mix(GetTypeHash(In.Data.Id), static_cast<uint32>(In.Month));
		H = Mix(H, static_cast<uint32>(SimCruisePlaceGraph::Rules().Version));
		H = Mix(H, SimCruiseClimate::KoppenAt(In.LatDeg, In.LonDeg));
		for (const FSimCruiseRibbon& R : In.Roads.Ribbons)
		{
			H = Mix(H, GetTypeHash(R.Class));
			H = Mix(H, GetTypeHash(R.Name));
			H = Mix(H, static_cast<uint32>(R.St.Num()));
			if (R.St.Num() == 0) continue;
			for (const FSimCruiseRibbonStation* S : { &R.St[0], &R.St.Last() })
			{
				H = Mix(H, Q(S->C.X, 0.1));
				H = Mix(H, Q(S->C.Y, 0.1));
				H = Mix(H, Q(S->HalfW, 0.1));
			}
			for (int32 s = 0; s < 2; ++s)
			{
				for (const ESimCruiseRoadEdge E : R.Sides[s].Edge) H = Mix(H, static_cast<uint32>(E));
			}
		}
		H = Mix(H, static_cast<uint32>(In.Data.Land.Num()));
		for (const FSimCruiseLandPolygon& L : In.Data.Land) H = Mix(H, GetTypeHash(L.Class) ^ static_cast<uint32>(L.Rings.Num() ? L.Rings[0].Num() : 0));
		H = Mix(H, static_cast<uint32>(In.Data.BuildingCentres.Num()));
		H = Mix(H, static_cast<uint32>(In.Data.Pois.Num()));
		H = Mix(H, In.Roadside ? In.Roadside->Hash() : 0u);
		return H;
	}

	void Bake(const FInputs& In, FSimCruiseTileContext& Out, const uint32* KnownHash)
	{
		const double T0 = FPlatformTime::Seconds();
		const FSimCruiseTileData& Data = In.Data;
		const FSimCruiseRoadNetwork& Roads = In.Roads;
		const double Side = In.Side;
		Out = FSimCruiseTileContext();
		Out.Tile = Data.Id;
		Out.RulesVersion = RulesVersion;
		Out.DataHash = KnownHash ? *KnownHash : DataHash(In);
		Out.Month = In.Month;

		// ---------------------------------------------------------------- the place: climate, season, species
		const FSimCruisePlaceRules& Rules = SimCruisePlaceGraph::Rules();
		Out.Koppen = SimCruiseClimate::KoppenAt(In.LatDeg, In.LonDeg);
		Rules.RegionFacts(In.LatDeg, In.LonDeg, Out.TileFacts);
		Out.Climate = Out.TileFacts.Strongest(TEXT("climate"));
		Out.Season = SimCruiseClimate::Season(Out.Koppen, Out.Climate, In.LatDeg, In.Month);
		Out.TileFacts.Add(Out.Season, 1.0f, FString::Printf(TEXT("month %d"), In.Month));
		Rules.Infer(Out.TileFacts);
		Out.GrassDry = SimCruiseClimate::GrassDry(Out.Koppen, Out.Climate, In.LatDeg, In.Month);
		SpeciesFor(Out.Koppen, Out.Climate, In.LatDeg, In.LonDeg, Out.Species);
		// The tile's own seed (what the palms always used): a tile id never changes.
		Out.TileSeed = GetTypeHash(Data.Id);

		// ---------------------------------------------------------------- OSM roadside in tile metres
		static const FSimCruiseRoadside None;
		const FSimCruiseRoadside& RS = In.Roadside ? *In.Roadside : None;
		TArray<FWayM> WaysM, BarriersM;
		ToWaysM(Data.Id, Side, RS.Roads, WaysM);
		ToWaysM(Data.Id, Side, RS.Barriers, BarriersM);
		Out.MappedWays = WaysM.Num();
		// Barriers as samples every 4 m or less (a cell asks which lie beside it).
		struct FBarrierSample { FVector2d P; FName Kind; };
		TArray<FBarrierSample> BarrierSamples;
		for (const FWayM& W : BarriersM)
		{
			for (int32 i = 0; i + 1 < W.P.Num(); ++i)
			{
				const double L = FVector2d::Distance(W.P[i], W.P[i + 1]);
				const int32 N = FMath::Clamp(FMath::CeilToInt(L / 4.0), 1, 500);
				for (int32 k = 0; k < N; ++k) BarrierSamples.Add({ FMath::Lerp(W.P[i], W.P[i + 1], k / static_cast<double>(N)), RS.Barriers[W.Index].Kind });
			}
			BarrierSamples.Add({ W.P.Last(), RS.Barriers[W.Index].Kind });
		}
		FBins BarrierBins; BarrierBins.CellM = 16.0;
		for (int32 i = 0; i < BarrierSamples.Num(); ++i) BarrierBins.Add(BarrierSamples[i].P, i);
		// Named businesses in tile metres (commercial land use beside a road).
		TArray<FVector2d> Businesses;
		for (const FSimCruiseRoadsidePoint& P : RS.Points) if (P.Kind == TEXT("business")) Businesses.Add(ToTileM(Data.Id, Side, P.Lon, P.Lat));
		TArray<FVector2d> Centres;
		Centres.Reserve(Data.BuildingCentres.Num());
		for (const FVector2d& U : Data.BuildingCentres) Centres.Add(U * Side);
		FBins CentreBins, BusinessBins;
		for (int32 i = 0; i < Centres.Num(); ++i) CentreBins.Add(Centres[i], i);
		for (int32 i = 0; i < Businesses.Num(); ++i) BusinessBins.Add(Businesses[i], i);

		auto LandUseAt = [&](const FVector2d& C, const FVector2d& Out2, double HalfW, const FVector2d& T)
		{
			for (const double D : { 8.0, 20.0 })
			{
				const FVector2d P = C + Out2 * (HalfW + D);
				if (In.IsWater && In.IsWater(P)) return FName(TEXT("water"));
				if (In.IsSand && In.IsSand(P)) return FName(TEXT("beach"));
			}
			int32 Built = 0;
			const FVector2d P = C + Out2 * (HalfW + 15.0);
			CentreBins.Near(P, 30.0, [&](int32 i) { Built += FVector2d::DistSquared(Centres[i], P) < 30.0 * 30.0 ? 1 : 0; });
			if (Built > 0)
			{
				bool bShop = false;
				BusinessBins.Near(C, 45.0, [&](int32 i)
				{
					const FVector2d D = Businesses[i] - C;
					bShop |= FVector2d::DotProduct(D, Out2) > 0.0 && FMath::Abs(FVector2d::DotProduct(D, T)) < 30.0 && D.SizeSquared() < 45.0 * 45.0;
				});
				if (bShop) return FName(TEXT("commercial"));
				return Built >= 2 ? FName(TEXT("residential")) : FName(TEXT("built"));
			}
			for (const double D : { 8.0, 20.0 })
			{
				if (In.IsVegetation && In.IsVegetation(C + Out2 * (HalfW + D))) return FName(TEXT("green"));
			}
			return FName(TEXT("open"));
		};

		// ---------------------------------------------------------------- cells
		Out.RibbonCells.SetNum(Roads.Ribbons.Num());
		for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& Ribbon = Roads.Ribbons[r];
			Out.RibbonCells[r] = FIntPoint(Out.Cells.Num(), 0);
			if (Ribbon.St.Num() < 2 || Ribbon.Length() < 0.5) continue;
			// World-anchored marks along a canonical direction (as the street lights and the dashes).
			const FVector2d T0v = Ribbon.St[0].T;
			const double Canon = (T0v.X > 1e-6 || (FMath::Abs(T0v.X) <= 1e-6 && T0v.Y > 0.0)) ? 1.0 : -1.0;
			// Marks on global Web Mercator metres (the same in every session and on both sides of a
			// tile border), every CellMercatorM of them: about 32 m on the ground.
			const double MercPerM = MercatorTileM / Side;
			const double Lc = CellMercatorM(In.LatDeg);
			const FVector2d World0 = (FVector2d(Data.Id.X, Data.Id.Y) + Ribbon.St[0].C / Side) * MercatorTileM;
			const double Anchor = FVector2d::DotProduct(World0, T0v) * Canon;
			const double SA = Ribbon.St[0].S, SB = Ribbon.St.Last().S;
			auto UOf = [&](double S) { return Anchor + Canon * (S - SA) * MercPerM; };
			auto SOf = [&](double U) { return SA + (U - Anchor) * Canon / MercPerM; };
			const double ULo = FMath::Min(UOf(SA), UOf(SB)), UHi = FMath::Max(UOf(SA), UOf(SB));
			const int64 K0 = FMath::FloorToInt64(ULo / Lc), K1 = FMath::FloorToInt64((UHi - 1e-6) / Lc);
			TArray<FSimCruiseCellContext> RibbonCells;
			for (int64 K = K0; K <= K1; ++K)
			{
				const double Ua = FMath::Max(ULo, K * Lc), Ub = FMath::Min(UHi, (K + 1) * Lc);
				if (Ub - Ua < 1e-3) continue;
				FSimCruiseCellContext Cell;
				Cell.Ribbon = r;
				Cell.S0 = FMath::Min(SOf(Ua), SOf(Ub));
				Cell.S1 = FMath::Max(SOf(Ua), SOf(Ub));
				const double SMid = 0.5 * (Cell.S0 + Cell.S1);
				const int32 I = StationAt(Ribbon, SMid, Cell.CentreM, Cell.Tangent, Cell.Right, Cell.HalfW);
				Cell.RoadClass = FName(*Ribbon.Class);
				Cell.StreetName = Ribbon.Name;

				// The OSM way under the cell: near the centre line, along it, the same name first.
				const FSimCruiseRoadsideWay* Way = nullptr;
				bool bReversed = false;
				{
					double BestScore = TNumericLimits<double>::Max();
					const double Limit = Cell.HalfW + 4.0;
					for (const FWayM& W : WaysM)
					{
						if (!W.Box.ExpandBy(Limit).IsInside(Cell.CentreM)) continue;
						FVector2d Dir;
						const double D = ClosestOnPolyline(W.P, Cell.CentreM, Dir);
						const double Dot = FVector2d::DotProduct(Dir, Cell.Tangent);
						if (D > Limit || FMath::Abs(Dot) < 0.7) continue;
						const FSimCruiseRoadsideWay& Rw = RS.Roads[W.Index];
						const double Score = D - (!Rw.Name.IsEmpty() && Rw.Name.Equals(Ribbon.Name, ESearchCase::IgnoreCase) ? 3.0 : 0.0);
						if (Score < BestScore) { BestScore = Score; Way = &Rw; bReversed = Dot < 0.0; }
					}
				}
				Cell.WayStation = static_cast<int32>(K);
				if (Way)
				{
					// The station along the way: its own canonical direction, global metres, so both
					// tiles of a way (and every session) number its 32 m cells alike.
					const TArray<FVector2d>& LL = Way->LonLat;
					FVector2d Dir = (SimCruiseGeo::LonLatToTile(LL.Last().X, LL.Last().Y) - SimCruiseGeo::LonLatToTile(LL[0].X, LL[0].Y)).GetSafeNormal();
					if (Dir.X < -1e-9 || (FMath::Abs(Dir.X) <= 1e-9 && Dir.Y < 0.0)) Dir = -Dir;
					const FVector2d CentreGlobal = (FVector2d(Data.Id.X, Data.Id.Y) + Cell.CentreM / Side) * MercatorTileM;
					Cell.WayStation = static_cast<int32>(FMath::FloorToInt64(FVector2d::DotProduct(CentreGlobal, Dir) / Lc));
					Cell.WayId = Way->Id;
					if (Cell.StreetName.IsEmpty()) Cell.StreetName = Way->Name;
					++Out.MatchedCells;
				}

				// Surface, smoothness, light, speed: mapped first, then the ribbon.
				Cell.bSurfaceMapped = Way && !Way->Surface.IsEmpty();
				Cell.Surface = Cell.bSurfaceMapped ? Lower(Way->Surface) : FName(Ribbon.bUnpaved ? TEXT("unpaved") : TEXT("asphalt"));
				Cell.Smoothness = Way ? Lower(Way->Smoothness) : NAME_None;
				Cell.MaxSpeedKph = Way ? Way->MaxSpeedKph : 0;
				for (int32 s = 0; s < 2; ++s)
				{
					FSimCruiseCellSide& CS = Cell.Sides[s];
					const FSimCruiseRibbonSide& RSd = Ribbon.Sides[s];
					const ESimCruiseRoadEdge Edge = RSd.Edge.IsValidIndex(I) ? RSd.Edge[I] : ESimCruiseRoadEdge::Rural;
					CS.Edge = static_cast<uint8>(Edge);
					CS.SidewalkM = RSd.SidewalkM.IsValidIndex(I) ? RSd.SidewalkM[I] : 0.0f;
					// OSM left/right follow the way's drawing direction; flip when it runs against the ribbon.
					const int32 W = bReversed ? 1 - s : s;
					CS.bSidewalkMapped = Way && !Way->Sidewalk[W].IsEmpty();
					CS.Sidewalk = CS.bSidewalkMapped ? Lower(Way->Sidewalk[W]) : FName(Edge == ESimCruiseRoadEdge::Kerb ? TEXT("yes") : TEXT("no"));
					CS.bKerbMapped = Way && !Way->Kerb[W].IsEmpty();
					CS.Kerb = CS.bKerbMapped ? Lower(Way->Kerb[W])
						: FName(Edge == ESimCruiseRoadEdge::Kerb ? TEXT("raised") : Edge == ESimCruiseRoadEdge::Town ? TEXT("flush") : TEXT("none"));
					CS.Shoulder = Way && !Way->Shoulder[W].IsEmpty() ? Lower(Way->Shoulder[W]) : FName(Edge == ESimCruiseRoadEdge::Rural ? TEXT("yes") : TEXT("no"));
					CS.LandUse = LandUseAt(Cell.CentreM, Cell.Right * (s == 0 ? -1.0 : 1.0), Cell.HalfW, Cell.Tangent);
				}
				Cell.bLitMapped = Way && !Way->Lit.IsEmpty();
				// Unmapped: town streets are lit (a curb or a built-up paved edge on either side), rural roads are not.
				const bool bTown = Cell.Sides[0].Edge != static_cast<uint8>(ESimCruiseRoadEdge::Rural) || Cell.Sides[1].Edge != static_cast<uint8>(ESimCruiseRoadEdge::Rural);
				Cell.Lit = Cell.bLitMapped ? Lower(Way->Lit) : FName(bTown ? TEXT("yes") : TEXT("no"));

				// Barriers within 12 m of the paved edge, beside this cell.
				const double HalfLen = 0.5 * (Cell.S1 - Cell.S0) + 1.0;
				int32 Rank[2] = { 0, 0 };
				double Off[2] = { 1e9, 1e9 };
				BarrierBins.Near(Cell.CentreM, HalfLen + Cell.HalfW + 12.0, [&](int32 Bi)
				{
					const FBarrierSample& B = BarrierSamples[Bi];
					const FVector2d D = B.P - Cell.CentreM;
					const double Along = FVector2d::DotProduct(D, Cell.Tangent);
					if (FMath::Abs(Along) > HalfLen) return;
					const double Lat = FVector2d::DotProduct(D, Cell.Right);
					const double Beyond = FMath::Abs(Lat) - Cell.HalfW;
					if (Beyond < -0.5 || Beyond > 12.0) return;
					const int32 s = Lat > 0.0 ? 1 : 0;
					const int32 Rk = BarrierRank(B.Kind);
					if (Rk > Rank[s] || (Rk == Rank[s] && Beyond < Off[s]))
					{
						Rank[s] = Rk;
						Off[s] = Beyond;
						Cell.Sides[s].Barrier = B.Kind;
						Cell.Sides[s].BarrierOffsetM = static_cast<float>(FMath::Max(0.0, Beyond));
					}
				});

				const FVector2d GlobalM = (FVector2d(Data.Id.X, Data.Id.Y) + Cell.CentreM / Side) * MercatorTileM;
				Cell.Seed = StableSeed(Cell.WayId, Cell.WayStation, GlobalM, Cell.RoadClass);
				RibbonCells.Add(MoveTemp(Cell));
			}
			// Station order (the marks ran in world order, which is backwards when Canon < 0).
			RibbonCells.Sort([](const FSimCruiseCellContext& A, const FSimCruiseCellContext& B) { return A.S0 < B.S0; });
			Out.RibbonCells[r].Y = RibbonCells.Num();
			Out.Cells.Append(MoveTemp(RibbonCells));
		}

		// ---------------------------------------------------------------- mapped points as (road, station, side)
		for (int32 p = 0; p < RS.Points.Num(); ++p)
		{
			const FSimCruiseRoadsidePoint& Pt = RS.Points[p];
			const FVector2d P = ToTileM(Data.Id, Side, Pt.Lon, Pt.Lat);
			if (P.X < 0.0 || P.Y < 0.0 || P.X >= Side || P.Y >= Side) continue;
			FSimCruisePlacedPoint Placed;
			Placed.Kind = Pt.Kind;
			Placed.Point = p;
			Placed.PositionM = P;
			double BestD = 30.0;
			for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
			{
				const FSimCruiseRibbon& R = Roads.Ribbons[r];
				for (int32 i = 0; i + 1 < R.St.Num(); ++i)
				{
					const FSimCruiseRibbonStation& A = R.St[i];
					const FSimCruiseRibbonStation& B = R.St[i + 1];
					if (FMath::Abs(P.X - A.C.X) > BestD + 40.0 || FMath::Abs(P.Y - A.C.Y) > BestD + 40.0) continue;
					const FVector2d Q2 = FMath::ClosestPointOnSegment2D(P, A.C, B.C);
					const double Seg = FVector2d::Distance(A.C, B.C);
					const double Tt = Seg > 1e-6 ? FVector2d::Distance(A.C, Q2) / Seg : 0.0;
					const double HalfW = FMath::Lerp(A.HalfW, B.HalfW, Tt);
					const double D = FMath::Max(0.0, FVector2d::Distance(P, Q2) - HalfW);
					if (D >= BestD) continue;
					BestD = D;
					const FVector2d Rt = FMath::Lerp(A.R, B.R, Tt).GetSafeNormal();
					const double Lat = FVector2d::DotProduct(P - Q2, Rt);
					Placed.Ribbon = r;
					Placed.S = FMath::Lerp(A.S, B.S, Tt);
					Placed.Side = Lat > 0.0 ? 1 : 0;
					Placed.OffsetM = FMath::Abs(Lat) - HalfW;
				}
			}
			if (Placed.Ribbon != INDEX_NONE)
			{
				if (const FSimCruiseCellContext* C = Out.At(Placed.Ribbon, Placed.S)) Placed.Cell = static_cast<int32>(C - Out.Cells.GetData());
			}
			else if (Pt.Kind != TEXT("business"))
			{
				continue; // a lamp in a park far from any road is not roadside
			}
			Out.Points.Add(Placed);
		}
		Out.BakeMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	TSharedRef<const FSimCruiseTileContext, ESPMode::ThreadSafe> ForBuild(const FInputs& In, bool* bOutCacheHit)
	{
		const uint32 Hash = DataHash(In);
		{
			FScopeLock Lock(&ContextLock);
			if (const FContextRef* Found = ContextCache().Find(In.Data.Id))
			{
				if ((*Found)->RulesVersion == RulesVersion && (*Found)->DataHash == Hash)
				{
					if (bOutCacheHit) *bOutCacheHit = true;
					return *Found;
				}
			}
		}
		TSharedRef<FSimCruiseTileContext, ESPMode::ThreadSafe> Made = MakeShared<FSimCruiseTileContext, ESPMode::ThreadSafe>();
		Bake(In, *Made, &Hash);
		UE_LOG(LogSimCruiseContext, Verbose, TEXT("%s"), *Made->Describe());
		FScopeLock Lock(&ContextLock);
		ContextCache().Add(In.Data.Id, Made);
		// The tiles round the car only (a record is a few hundred cells).
		if (ContextCache().Num() > 96)
		{
			TArray<FIntPoint> Keys;
			ContextCache().GetKeys(Keys);
			Keys.Sort([&](const FIntPoint& A, const FIntPoint& B) { return (A - In.Data.Id).SizeSquared() > (B - In.Data.Id).SizeSquared(); });
			for (int32 i = 0; i < Keys.Num() - 64; ++i) ContextCache().Remove(Keys[i]);
		}
		if (bOutCacheHit) *bOutCacheHit = false;
		return Made;
	}

	void ClearCache()
	{
		FScopeLock Lock(&ContextLock);
		ContextCache().Reset();
	}
}
