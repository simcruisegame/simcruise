// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseStreetFurniture.h"

#include "Knowledge/SimCruiseFurnitureData.h"
#include "Knowledge/SimCruisePlaceGraph.h"
#include "World/SimCruiseCellContext.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileDetail.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarFurniture(TEXT("simcruise.World.Furniture"), 1,
		TEXT("Street furniture on tiles built after the change. 1: mapped items and hydrants by rule; 2: mapped items only; 0: none."));

	namespace Kind = SimCruiseSurfaceKind;
	using namespace SimCruiseRoadSection;

	FLinearColor Srgb(uint8 R, uint8 G, uint8 B) { return FLinearColor::FromSRGBColor(FColor(R, G, B)); }

	/** Small solids in tile metres for the Props layer (UV1 = surface kind, seed). */
	struct FProps
	{
		FSimCruiseMeshData& M;
		float Seed = 0.5f;

		void Quad(const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3d& D, const FLinearColor& Col, float K) const
		{
			const FVector N = FVector(FVector3d::CrossProduct(B - A, D - A).GetSafeNormal());
			const int32 I0 = M.Add(FVector(A * 100.0), N, Col, FVector2D(0, 0), FVector2D(K, Seed));
			const int32 I1 = M.Add(FVector(B * 100.0), N, Col, FVector2D(1, 0), FVector2D(K, Seed));
			const int32 I2 = M.Add(FVector(C * 100.0), N, Col, FVector2D(1, 1), FVector2D(K, Seed));
			const int32 I3 = M.Add(FVector(D * 100.0), N, Col, FVector2D(0, 1), FVector2D(K, Seed));
			M.Quad(I0, I1, I2, I3, N);
		}

		/** A box centred at C (x along F, y along S = F rotated left), from Z0 to Z1. */
		void Box(const FVector2d& C, const FVector2d& F, double Len, double Wid, double Z0, double Z1, const FLinearColor& Col, float K, bool bBottom = false) const
		{
			const FVector2d S(-F.Y, F.X);
			const FVector2d P[4] = { C - F * Len * 0.5 - S * Wid * 0.5, C + F * Len * 0.5 - S * Wid * 0.5, C + F * Len * 0.5 + S * Wid * 0.5, C - F * Len * 0.5 + S * Wid * 0.5 };
			for (int32 k = 0; k < 4; ++k)
			{
				const FVector2d A = P[k], B = P[(k + 1) % 4];
				Quad(FVector3d(A.X, A.Y, Z0), FVector3d(B.X, B.Y, Z0), FVector3d(B.X, B.Y, Z1), FVector3d(A.X, A.Y, Z1), Col, K);
			}
			Quad(FVector3d(P[0].X, P[0].Y, Z1), FVector3d(P[1].X, P[1].Y, Z1), FVector3d(P[2].X, P[2].Y, Z1), FVector3d(P[3].X, P[3].Y, Z1), Col, K);
			if (bBottom) Quad(FVector3d(P[3].X, P[3].Y, Z0), FVector3d(P[2].X, P[2].Y, Z0), FVector3d(P[1].X, P[1].Y, Z0), FVector3d(P[0].X, P[0].Y, Z0), Col, K);
		}

		/** An upright n-sided prism (radius R0 at Z0 to R1 at Z1), capped on top. */
		void Cylinder(const FVector2d& C, double R0, double R1, double Z0, double Z1, int32 Sides, const FLinearColor& Col, float K, bool bCap = true) const
		{
			for (int32 s = 0; s < Sides; ++s)
			{
				const double A = UE_DOUBLE_TWO_PI * s / Sides, B = UE_DOUBLE_TWO_PI * (s + 1) / Sides;
				const FVector2d Da(FMath::Cos(A), FMath::Sin(A)), Db(FMath::Cos(B), FMath::Sin(B));
				Quad(FVector3d(C.X + Da.X * R0, C.Y + Da.Y * R0, Z0), FVector3d(C.X + Db.X * R0, C.Y + Db.Y * R0, Z0),
					FVector3d(C.X + Db.X * R1, C.Y + Db.Y * R1, Z1), FVector3d(C.X + Da.X * R1, C.Y + Da.Y * R1, Z1), Col, K);
			}
			if (!bCap) return;
			const FVector N = FVector::UpVector;
			const int32 Hub = M.Add(FVector(C.X * 100.0, C.Y * 100.0, Z1 * 100.0), N, Col, FVector2D(0, 0), FVector2D(K, Seed));
			TArray<int32> Ring;
			for (int32 s = 0; s < Sides; ++s)
			{
				const double A = UE_DOUBLE_TWO_PI * s / Sides;
				Ring.Add(M.Add(FVector((C.X + FMath::Cos(A) * R1) * 100.0, (C.Y + FMath::Sin(A) * R1) * 100.0, Z1 * 100.0), N, Col, FVector2D(0, 0), FVector2D(K, Seed)));
			}
			for (int32 s = 0; s < Sides; ++s) M.Tri(Hub, Ring[s], Ring[(s + 1) % Sides], N);
		}
	};

	/** A colour word (OSM colour=*, or a graph fact's value) as paint. */
	FLinearColor Paint(const FString& Word, const FLinearColor& Default)
	{
		FLinearColor C;
		if (!Word.IsEmpty() && SimCruisePlaceGraph::ParseColour(Word, C)) return C;
		if (Word == TEXT("silver") || Word == TEXT("chrome")) return Srgb(188, 190, 192);
		return Default;
	}

	/** The look of a kind at this place: the strongest "<kind>:<colour>" fact of the region, or the default. */
	FLinearColor PlaceColour(const FSimCruiseFactSet& Facts, const TCHAR* Kind, const FLinearColor& Default)
	{
		const FName Best = Facts.Strongest(Kind);
		if (Best.IsNone()) return Default;
		FString V = Best.ToString();
		V.RemoveFromStart(FString(Kind) + TEXT(":"));
		return Paint(V.Replace(TEXT("_"), TEXT("")), Default);
	}

	struct FSpot
	{
		FVector2d P = FVector2d::ZeroVector;  // tile metres
		FVector2d ToRoad = FVector2d(1, 0);    // horizontal, toward the nearest carriageway
		FVector2d Along = FVector2d(0, 1);     // along the road
		double Z = 0.0;                        // ground (or sidewalk) height
		bool bByRoad = false;
	};

	/**
	 * Where a mapped point stands: on the sidewalk if it is near a road (a point mapped in
	 * the carriageway or the gutter is moved behind the kerb), else on the ground.
	 */
	bool Place(const SimCruiseTileDetail::FContext& X, const FVector2d& P, double Setback, FSpot& Out)
	{
		const FSimCruiseRoadNetwork& Roads = X.B.Roads;
		Out.P = P;
		Out.Z = X.Data.HeightAtUnit(P / X.Side);
		FSimCruiseRoadHit Hit;
		if (!Roads.FindNearest(P, 14.0, Hit) || !Roads.Ribbons.IsValidIndex(Hit.Ribbon)) return true;
		const FSimCruiseRibbon& R = Roads.Ribbons[Hit.Ribbon];
		if (!R.St.IsValidIndex(Hit.Station) || !R.St.IsValidIndex(Hit.Station + 1)) return true;
		const FSimCruiseRibbonStation& A = R.St[Hit.Station];
		const FSimCruiseRibbonStation& B = R.St[Hit.Station + 1];
		const double Tt = FMath::Clamp(Hit.T, 0.0, 1.0);
		const FVector2d C = FMath::Lerp(A.C, B.C, Tt);
		const FVector2d Rv = FMath::Lerp(A.R, B.R, Tt).GetSafeNormal();
		const double HalfW = FMath::Lerp(A.HalfW, B.HalfW, Tt);
		const double Sgn = Hit.Lateral >= 0.0 ? 1.0 : -1.0;
		const int32 SideIndex = Sgn > 0.0 ? 1 : 0;
		const FSimCruiseRibbonSide& Side = R.Sides[SideIndex];
		const bool bKerb = Side.Edge.IsValidIndex(Hit.Station) && Side.Edge[Hit.Station] == ESimCruiseRoadEdge::Kerb;
		const double Behind = HalfW + (bKerb ? GutterM + CurbBatterM + CurbTopM : TownEdgeM);
		// Never in the road: a point mapped in the carriageway or the gutter goes behind the kerb.
		const double Lateral = FMath::Abs(Hit.Lateral) < Behind + Setback ? Behind + Setback : FMath::Abs(Hit.Lateral);
		Out.P = C + Rv * Sgn * Lateral;
		Out.ToRoad = -Rv * Sgn;
		Out.Along = FVector2d(-Out.ToRoad.Y, Out.ToRoad.X);
		const double Edge = FMath::Lerp(A.Height(HalfW * Sgn), B.Height(HalfW * Sgn), Tt);
		const double Walk = bKerb ? Edge - GutterDropM + CurbFaceM + SidewalkSlope * (Lateral - Behind) : Edge - TownEdgeDropM;
		// Within the sidewalk's reach the walk's height; further back, the ground.
		Out.Z = (Lateral - Behind) < 4.0 ? Walk : FMath::Max(Walk, Out.Z);
		Out.bByRoad = true;
		return !SimCruiseTileDetail::IsInsideBuilding(X.Data, X.Side, Out.P);
	}

	void Hydrant(const FProps& W, const FSpot& S, const FLinearColor& Body, const FLinearColor& Cap)
	{
		// A US dry-barrel hydrant (AWWA C502): about 0.75 m above grade, 0.2 m barrel, a domed
		// bonnet, two 2.5 in hose nozzles and a 4.5 in pumper nozzle facing the street.
		const FVector2d C = S.P;
		const double Z = S.Z;
		W.Cylinder(C, 0.16, 0.15, Z - 0.05, Z + 0.06, 8, Body, Kind::Metal, false);       // base flange
		W.Cylinder(C, 0.11, 0.105, Z + 0.06, Z + 0.58, 10, Body, Kind::Metal, false);    // barrel
		W.Cylinder(C, 0.125, 0.125, Z + 0.58, Z + 0.62, 10, Body, Kind::Metal, false);   // upper flange
		W.Cylinder(C, 0.12, 0.06, Z + 0.62, Z + 0.72, 10, Cap, Kind::Metal, true);       // bonnet
		W.Cylinder(C, 0.025, 0.02, Z + 0.72, Z + 0.77, 5, Cap, Kind::Metal, true);       // operating nut
		W.Box(C + S.ToRoad * 0.15, S.ToRoad, 0.12, 0.13, Z + 0.36, Z + 0.49, Cap, Kind::Metal);  // pumper nozzle cap
		W.Box(C + S.Along * 0.14, S.Along, 0.09, 0.08, Z + 0.4, Z + 0.48, Body, Kind::Metal);      // hose nozzles
		W.Box(C - S.Along * 0.14, -S.Along, 0.09, 0.08, Z + 0.4, Z + 0.48, Body, Kind::Metal);
	}

	void Bench(const FProps& W, const FSpot& S, const FLinearColor& Seat, const FLinearColor& Frame)
	{
		// 1.8 m long, seat 0.45 m high and 0.42 m deep, back to the road's far side (the sitter faces the street).
		const FVector2d F = S.Along, Back = -S.ToRoad;
		const FVector2d C = S.P;
		for (const double End : { -0.75, 0.75 })
		{
			W.Box(C + F * End, F, 0.05, 0.42, S.Z, S.Z + 0.43, Frame, Kind::Metal);
			W.Box(C + F * End + Back * 0.21, F, 0.05, 0.05, S.Z + 0.43, S.Z + 0.86, Frame, Kind::Metal);
		}
		for (int32 k = 0; k < 4; ++k) W.Box(C + Back * (-0.15 + 0.1 * k), F, 1.8, 0.075, S.Z + 0.43, S.Z + 0.46, Seat, Kind::Metal);
		for (int32 k = 0; k < 3; ++k)
		{
			const double Zk = S.Z + 0.55 + 0.1 * k;
			W.Box(C + Back * 0.23, F, 1.8, 0.03, Zk, Zk + 0.075, Seat, Kind::Metal);
		}
	}

	void Bin(const FProps& W, const FSpot& S, const FLinearColor& Col)
	{
		// A 32-gallon street litter receptacle: 0.58 m across, 0.9 m tall, a domed lid.
		W.Cylinder(S.P, 0.28, 0.29, S.Z, S.Z + 0.82, 12, Col, Kind::Metal, false);
		W.Cylinder(S.P, 0.3, 0.12, S.Z + 0.82, S.Z + 0.92, 12, Col, Kind::Metal, true);
	}

	void Bollard(const FProps& W, const FSpot& S, const FLinearColor& Col)
	{
		W.Cylinder(S.P, 0.085, 0.085, S.Z - 0.05, S.Z + 0.86, 8, Col, Kind::Metal, false);
		W.Cylinder(S.P, 0.085, 0.03, S.Z + 0.86, S.Z + 0.93, 8, Col, Kind::Metal, true);
	}

	void PostBox(const FProps& W, const FSpot& S)
	{
		// USPS collection box: 0.53 x 0.48 m, 1.24 m tall, blue, a rounded top.
		const FLinearColor Blue = Srgb(30, 62, 132);
		W.Box(S.P, S.ToRoad, 0.48, 0.53, S.Z + 0.05, S.Z + 1.05, Blue, Kind::Metal);
		W.Cylinder(S.P, 0.26, 0.2, S.Z + 1.05, S.Z + 1.24, 8, Blue, Kind::Metal, true);
		for (const double Side : { -0.2, 0.2 }) W.Box(S.P + S.Along * Side, S.ToRoad, 0.42, 0.05, S.Z, S.Z + 0.06, Srgb(40, 40, 42), Kind::Metal);
	}

	void BikeRack(const FProps& W, const FSpot& S, const FLinearColor& Col)
	{
		// Three inverted-U hoops, 0.85 m tall and 0.6 m wide, 0.9 m apart, square to the kerb.
		for (int32 k = -1; k <= 1; ++k)
		{
			const FVector2d C = S.P + S.Along * (0.9 * k);
			for (const double Leg : { -0.3, 0.3 }) W.Cylinder(C + S.ToRoad * Leg, 0.025, 0.025, S.Z - 0.05, S.Z + 0.8, 6, Col, Kind::Metal, false);
			W.Box(C, S.ToRoad, 0.64, 0.05, S.Z + 0.8, S.Z + 0.85, Col, Kind::Metal);
		}
	}

	void BusStop(const FProps& W, const FSpot& S, bool bShelter, bool bBench, const FLinearColor& Frame)
	{
		// The flag: a 2.4 m post by the kerb with a 0.45 x 0.6 m sign facing the traffic.
		const FVector2d Post = S.P + S.ToRoad * 0.3;
		W.Cylinder(Post, 0.035, 0.035, S.Z - 0.05, S.Z + 2.9, 6, Srgb(150, 154, 156), Kind::Metal, true);
		W.Box(Post + S.Along * 0.02, S.Along, 0.02, 0.45, S.Z + 2.25, S.Z + 2.85, Srgb(20, 70, 140), Kind::Metal);
		if (bShelter)
		{
			// A 3.6 x 1.5 m shelter: four posts, a flat roof at 2.4 m, a glass back.
			const FVector2d C = S.P - S.ToRoad * 1.2;
			for (const double A : { -1.75, 1.75 })
			{
				for (const double D : { -0.7, 0.7 }) W.Cylinder(C + S.Along * A + S.ToRoad * D, 0.04, 0.04, S.Z, S.Z + 2.4, 6, Frame, Kind::Metal, false);
			}
			W.Box(C, S.Along, 3.8, 1.6, S.Z + 2.4, S.Z + 2.5, Frame, Kind::Metal, true);
			W.Box(C - S.ToRoad * 0.7, S.Along, 3.5, 0.02, S.Z + 0.15, S.Z + 2.3, FLinearColor(0.55f, 0.6f, 0.62f), Kind::Railing);
		}
		if (bBench || bShelter)
		{
			FSpot B = S;
			B.P = S.P - S.ToRoad * (bShelter ? 1.6 : 0.9);
			Bench(W, B, Srgb(120, 124, 126), Frame);
		}
	}

	void PicnicTable(const FProps& W, const FSpot& S)
	{
		const FLinearColor Wood = Srgb(132, 100, 70);
		W.Box(S.P, S.Along, 1.8, 0.75, S.Z + 0.72, S.Z + 0.77, Wood, Kind::Metal);
		for (const double D : { -0.65, 0.65 }) W.Box(S.P + S.ToRoad * D, S.Along, 1.8, 0.25, S.Z + 0.43, S.Z + 0.47, Wood, Kind::Metal);
		for (const double A : { -0.7, 0.7 }) W.Box(S.P + S.Along * A, S.ToRoad, 1.5, 0.06, S.Z, S.Z + 0.72, Wood, Kind::Metal);
	}
}

namespace SimCruiseStreetFurniture
{
	void Build(const SimCruiseTileDetail::FContext& X, FStats* OutStats)
	{
		const int32 Mode = CVarFurniture.GetValueOnAnyThread();
		if (Mode <= 0) return;
		FSimCruiseTileBuild& B = X.B;
		FSimCruiseMeshData& Mesh = B.Layer(ESimCruiseTileLayer::Props);
		FStats Stats;

		// The local look from the place graph: hydrant, bench and bin colours of the region.
		// The tile's context record carries them already inferred (region, Köppen, season).
		FSimCruiseFactSet Facts;
		if (X.Cells)
		{
			Facts = X.Cells->TileFacts;
		}
		else
		{
			const FSimCruisePlaceRules& Rules = SimCruisePlaceGraph::Rules();
			Rules.RegionFacts(X.LatDeg, X.LonDeg, Facts);
			Rules.Infer(Facts);
		}
		const FLinearColor HydrantBody = PlaceColour(Facts, TEXT("hydrant"), Srgb(196, 40, 36));
		const FLinearColor HydrantCap = PlaceColour(Facts, TEXT("hydrant_cap"), HydrantBody);
		const FLinearColor BenchSeat = PlaceColour(Facts, TEXT("bench"), Srgb(118, 84, 56));
		const FLinearColor Frame = PlaceColour(Facts, TEXT("street_metal"), Srgb(40, 44, 42));
		const FLinearColor BinColour = PlaceColour(Facts, TEXT("bin"), Srgb(34, 58, 44));
		const FLinearColor BollardColour = PlaceColour(Facts, TEXT("bollard"), Srgb(70, 72, 72));

		// 1. What the map has, where the map has it.
		TArray<FSimCruiseFurniture> Items;
		SimCruiseFurniture::ForTile(X.Data.Id, Items);
		TArray<FVector2d> Hydrants;
		for (const FSimCruiseFurniture& F : Items)
		{
			const FVector2d U = SimCruiseGeo::LonLatToTile(F.Lon, F.Lat) - FVector2d(X.Data.Id.X, X.Data.Id.Y);
			if (U.X < 0.0 || U.Y < 0.0 || U.X >= 1.0 || U.Y >= 1.0) continue;
			const FName K = F.Kind;
			const double Setback = K == TEXT("fire_hydrant") ? 0.45 : K == TEXT("bollard") ? 0.3 : K == TEXT("bus_stop") ? 0.5 : 0.9;
			FSpot S;
			if (!Place(X, U * X.Side, Setback, S)) continue;
			const uint32 H = HashCombine(GetTypeHash(F.Lat), GetTypeHash(F.Lon));
			FProps W{ Mesh, static_cast<float>((H >> 8) & 0xFFFF) / 65535.f };
			if (K == TEXT("fire_hydrant")) { Hydrant(W, S, Paint(F.Colour, HydrantBody), Paint(F.Colour, HydrantCap)); Hydrants.Add(S.P); }
			else if (K == TEXT("bench")) Bench(W, S, Paint(F.Colour, BenchSeat), Frame);
			else if (K == TEXT("waste_basket")) Bin(W, S, Paint(F.Colour, BinColour));
			else if (K == TEXT("bollard")) Bollard(W, S, Paint(F.Colour, BollardColour));
			else if (K == TEXT("post_box")) PostBox(W, S);
			else if (K == TEXT("bicycle_parking")) BikeRack(W, S, Frame);
			else if (K == TEXT("bus_stop")) BusStop(W, S, F.bShelter, F.bBench, Frame);
			else if (K == TEXT("picnic_table")) PicnicTable(W, S);
			else continue;
			++Stats.Mapped;
		}

		// 2. What the map leaves out, by rule: a hydrant every 300 ft (91 m) along curbed
		// streets, sides alternating, anchored to the world so the rhythm runs across tile
		// seams (US practice: 300-500 ft in town; NFPA 1 / IFC Appendix C spacing tables),
		// none within 45 m of a mapped one.
		constexpr double SpacingM = 91.44;
		const FSimCruiseRoadNetwork& Roads = B.Roads;
		for (int32 r = 0; r < (Mode == 1 ? Roads.Ribbons.Num() : 0); ++r)
		{
			const FSimCruiseRibbon& Ribbon = Roads.Ribbons[r];
			if (Ribbon.bService || Ribbon.bUnpaved || Ribbon.St.Num() < 2) continue;
			const FVector2d T0 = Ribbon.St[0].T;
			const double Canon = (T0.X > 1e-6 || (FMath::Abs(T0.X) <= 1e-6 && T0.Y > 0.0)) ? 1.0 : -1.0;
			const FVector2d World0(X.Data.Id.X * X.Side + Ribbon.St[0].C.X, X.Data.Id.Y * X.Side + Ribbon.St[0].C.Y);
			const double Anchor = FVector2d::DotProduct(World0, T0) * Canon;
			for (int32 i = 0; i + 1 < Ribbon.St.Num(); ++i)
			{
				const FSimCruiseRibbonStation& A = Ribbon.St[i];
				const FSimCruiseRibbonStation& Bs = Ribbon.St[i + 1];
				const double U0 = Anchor + Canon * (A.S - Ribbon.St[0].S), U1 = Anchor + Canon * (Bs.S - Ribbon.St[0].S);
				const double Lo = FMath::Min(U0, U1), Hi = FMath::Max(U0, U1);
				for (double Mark = FMath::CeilToDouble(Lo / SpacingM) * SpacingM; Mark < Hi; Mark += SpacingM)
				{
					const int32 SideIndex = (FMath::FloorToInt(Mark / SpacingM) & 1);
					const FSimCruiseRibbonSide& Side = Ribbon.Sides[SideIndex];
					if (!Side.Edge.IsValidIndex(i) || Side.Edge[i] != ESimCruiseRoadEdge::Kerb) continue;
					if (Side.Cut.IsValidIndex(i) && Side.Cut[i]) continue;
					if (Side.SidewalkM.IsValidIndex(i) && Side.SidewalkM[i] < 1.2f) continue;
					const double Tt = (Mark - U0) / (U1 - U0);
					// The context record: no hydrant where OSM maps no sidewalk, or beside water or a beach.
					const FSimCruiseCellContext* Cell = X.Cells ? X.Cells->At(r, FMath::Lerp(A.S, Bs.S, Tt)) : nullptr;
					if (Cell)
					{
						const FSimCruiseCellSide& CS = Cell->Sides[SideIndex];
						if ((CS.bSidewalkMapped && CS.Sidewalk == TEXT("no")) || CS.LandUse == TEXT("water") || CS.LandUse == TEXT("beach")) continue;
					}
					const double Sgn = SideIndex == 0 ? -1.0 : 1.0;
					const FVector2d C = FMath::Lerp(A.C, Bs.C, Tt);
					const FVector2d Rv = FMath::Lerp(A.R, Bs.R, Tt).GetSafeNormal();
					const double HalfW = FMath::Lerp(A.HalfW, Bs.HalfW, Tt);
					const FVector2d P = C + Rv * Sgn * (HalfW + GutterM + CurbBatterM + CurbTopM + 0.45);
					bool bNear = false;
					for (const FVector2d& Q : Hydrants) bNear |= FVector2d::DistSquared(P, Q) < 45.0 * 45.0;
					if (bNear || IsInsideBuilding(X, P)) continue;
					FSpot S;
					S.P = P;
					S.ToRoad = -Rv * Sgn;
					S.Along = FVector2d(-S.ToRoad.Y, S.ToRoad.X);
					const double Edge = FMath::Lerp(A.Height(HalfW * Sgn), Bs.Height(HalfW * Sgn), Tt);
					S.Z = Edge - GutterDropM + CurbFaceM;
					FProps W{ Mesh, Cell ? static_cast<float>((Cell->Seed >> 8) & 0xFFFF) / 65535.f : 0.3f };
					Hydrant(W, S, HydrantBody, HydrantCap);
					Hydrants.Add(P);
					++Stats.Inferred;
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("SimCruise tile %s: %d street furniture (%d mapped of %d, %d hydrants by rule)"), *SimCruiseTileName(X.Data.Id),
			Stats.Mapped + Stats.Inferred, Stats.Mapped, Items.Num(), Stats.Inferred);
		if (OutStats) *OutStats = Stats;
	}

	bool IsInsideBuilding(const SimCruiseTileDetail::FContext& X, const FVector2d& P)
	{
		return SimCruiseTileDetail::IsInsideBuilding(X.Data, X.Side, P);
	}
}
