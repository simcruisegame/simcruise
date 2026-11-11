// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCruiseTileData;

/**
 * The engineered road surface of one tile: what the tyres drive on and what
 * is drawn for every paved road. Ported from the Godot client's
 * RoadSurface.gd / Kerbs.gd (branch roads/edge-authority) and the helper's
 * road-grade.mjs chain profile, so the design and its reasons live there:
 *
 *  1. Road pieces of the tile are chained through junctions (the straightest
 *     pair joins, preferring the same name and class).
 *  2. The plan-view centreline is filleted: each mapped vertex is rounded by a
 *     circular arc tangent to both legs, so ribbons, edge lines and curbs
 *     follow curves instead of showing every chord corner. Chain ends keep
 *     their exact point and direction, so two tiles that share a road meet.
 *  3. Stations every ~2 m carry a robust Gaussian local-linear fit of the
 *     ground under the centreline (sigma 16 m, one Tukey pass), a 2 % crown,
 *     superelevation from the filleted curvature on high-speed classes, and
 *     the paved half width.
 *  4. A chain end on the tile boundary is pinned to the ground there (the
 *     tiles share their edge heights exactly), fading over 30 m, and its
 *     superelevation runs out to zero, so both tiles build the same section
 *     at the seam.
 *  5. Lower-ranked ribbons take the height of the ribbon they overlap at a
 *     junction (fading over 15 m), so there is no step under a tyre.
 *  6. Beyond the paved edge each side is a curb and gutter with a sidewalk
 *     where a mapped sidewalk runs alongside, else a paved (town) or gravel
 *     (rural) shoulder. Curbs and shoulders stop where another road's
 *     carriageway crosses them (junctions, driveways).
 *
 * Coordinates: tile metres, x east and y south from the tile's north-west
 * corner; heights in metres. R is the right-hand normal of the station
 * direction (x east / y south makes (-T.y, T.x) the driver's right).
 */

enum class ESimCruiseRoadEdge : uint8
{
	Rural = 0,   // gravel shoulder
	Town = 1,    // built-up street without a mapped sidewalk: a paved edge
	Kerb = 2,    // curb, gutter and sidewalk
};

struct FSimCruiseRibbonStation
{
	FVector2d C = FVector2d::ZeroVector;   // centreline point
	FVector2d T = FVector2d(1.0, 0.0);     // unit tangent (station direction)
	FVector2d R = FVector2d(0.0, 1.0);     // unit right normal
	double S = 0.0;                        // station along the chain, m
	double Z = 0.0;                        // profile height at the centre
	double E = 0.0;                        // superelevation (+ lowers the R side)
	double HalfW = 4.0;                    // paved half width
	double HL = 0.0, HC = 0.0, HR = 0.0;   // carriageway heights at -HalfW, 0, +HalfW
	double HQL = 0.0, HQR = 0.0;           // ... and at -HalfW/2, +HalfW/2 (junctions snap all five)
	int8 Travel = 0;                       // +1 one-way along S, -1 against, 0 two-way

	/** Height across the carriageway at signed lateral L (five-point piecewise linear). */
	double Height(double L) const
	{
		const double U = FMath::Clamp(L / FMath::Max(HalfW, 0.01), -1.0, 1.0) * 2.0;
		if (U < -1.0) return FMath::Lerp(HQL, HL, -U - 1.0);
		if (U < 0.0) return FMath::Lerp(HC, HQL, -U);
		if (U < 1.0) return FMath::Lerp(HC, HQR, U);
		return FMath::Lerp(HQR, HR, U - 1.0);
	}
	double Column(int32 Index) const { return Index == 0 ? HL : Index == 1 ? HQL : Index == 2 ? HC : Index == 3 ? HQR : HR; }
	double& Column(int32 Index) { return Index == 0 ? HL : Index == 1 ? HQL : Index == 2 ? HC : Index == 3 ? HQR : HR; }
	static double ColumnOffset(int32 Index) { return (Index - 2) * 0.5; } // times HalfW
};

/** One side of a ribbon beyond the paved edge, per station. */
struct FSimCruiseRibbonSide
{
	TArray<ESimCruiseRoadEdge> Edge;
	TArray<float> SidewalkM;               // sidewalk width behind the curb (kerb stations)
	TArray<uint8> Cut;                     // 1 where another carriageway crosses this side
};

struct FSimCruiseRibbon
{
	FString Class;
	FString Name;
	int32 Rank = 0;
	bool bMajor = false;
	bool bService = false;
	bool bStartOnBoundary = false;
	bool bEndOnBoundary = false;
	TArray<FSimCruiseRibbonStation> St;
	FSimCruiseRibbonSide Sides[2];        // 0: left (-R), 1: right (+R)

	/** Height of the carriageway at station i and signed lateral offset L (clamped to the paved width). */
	double CarriagewayHeight(int32 I, double L) const;
	double Length() const { return St.Num() ? St.Last().S - St[0].S : 0.0; }
};

/**
 * US default lane layout of a carriageway (MUTCD markings as LaneMarkings.gd):
 * the markings and the car's spawn pose share it, so a car placed "in the
 * right lane" sits between the lines that are drawn.
 */
namespace SimCruiseLanes
{
	/** Lane width (m) by road class. */
	SIMCRUISE_API double LaneWidthM(const FString& Class);
	/** Parallel parking lanes (m) on the station's left (-R) and right (+R) sides; 0 where there are none. */
	struct FParking
	{
		double Left = 0.0;
		double Right = 0.0;
	};

	/**
	 * Built-up streets (a curb or a town edge) with room keep a 2.4 m (8 ft)
	 * parallel parking lane at the edge (MUTCD 3B.19, NACTO): a two-way street on
	 * both sides when 10.8 m or wider, a one-way street on both sides from 7.8 m
	 * or on one side (the driver's right first) from 5.4 m. Arterials and above,
	 * and rural roads, have none. The kerb flags mean "built-up edge".
	 */
	SIMCRUISE_API FParking Parking(const FString& Class, double HalfW, int32 Travel, bool bKerbLeft, bool bKerbRight);

	/**
	 * A stop line stands this far (m) before the crossing carriageway's edge: the
	 * crosswalk's 1 m setback and 10 ft depth, then 4 ft (MUTCD 3B.16: a stop line 4 ft
	 * in advance of a crosswalk).
	 */
	inline constexpr double StopLineSetbackM = 1.0 + 3.05 + 1.2;

	/**
	 * Whether an approach on a road of this rank stops where a road of CrossRank crosses,
	 * at a junction without signals: a stop sign on the minor road; between equal roads
	 * that are not major roads, an all-way stop.
	 */
	inline bool ApproachStops(int32 Rank, bool bMajor, int32 CrossRank) { return CrossRank > Rank || (CrossRank == Rank && !bMajor); }

	/** Signed lateral offset (m, + to the driver's right) of the rightmost travel lane's centre. */
	SIMCRUISE_API double RightLaneCentreM(const FString& Class, double HalfW, int32 Travel, const FParking& Parking = FParking());
}

/** Cross-section geometry beyond the paved edge (metres). */
namespace SimCruiseRoadSection
{
	inline constexpr double Crown = 0.02;            // normal crown, AASHTO 1.5-2 %
	inline constexpr double GutterM = 0.45;          // 18 in gutter pan
	inline constexpr double GutterDropM = 0.035;     // pan slopes to the curb face
	inline constexpr double CurbFaceM = 0.15;        // 6 in exposed face
	inline constexpr double CurbBatterM = 0.02;
	inline constexpr double CurbTopM = 0.15;
	inline constexpr double SidewalkSlope = 0.02;    // sidewalks drain to the street
	inline constexpr double DefaultSidewalkM = 1.8;
	inline constexpr double TownEdgeM = 0.5;
	inline constexpr double TownEdgeDropM = 0.03;
	inline constexpr double RuralShoulderM = 1.2;
	inline constexpr double RuralShoulderDropM = 0.05;
	inline constexpr double SkirtM = 0.3;            // outer skirt that hides the ground edge
	inline constexpr double StationStepM = 2.0;
}

/**
 * A turnaround mapped as a small closed ring (a cul-de-sac bulb, a loop around
 * a tiny island): a constant-width ribbon cannot follow a 5 m radius, and the
 * real thing is a paved disc, so it is built as one. Outline: the ring's convex
 * hull pushed out by the road's half width.
 */
struct FSimCruiseRoadPad
{
	FVector2d Centre = FVector2d::ZeroVector;
	TArray<double> Radii;        // outline radius at uniform angles from +x
	double Height = 0.0;
	int32 Rank = 2;
	FString Name;

	double RadiusAt(const FVector2d& P) const;
	bool Contains(const FVector2d& P, double Margin = 0.0) const { return FVector2d::Distance(P, Centre) <= RadiusAt(P) + Margin; }
	FVector2d OutlinePoint(int32 K) const;
};

/** Query result for a point on a ribbon's carriageway. */
struct FSimCruiseRoadHit
{
	int32 Ribbon = INDEX_NONE;
	int32 Station = INDEX_NONE;   // segment start
	double T = 0.0;               // along the segment
	double Lateral = 0.0;         // signed, + toward R
	double HalfW = 0.0;
	double Height = 0.0;
	double Along = 0.0;           // residual along the road beyond a clamped segment end
};

/**
 * A road end on a tile edge as one tile built it: the seam contract. A tile
 * built later snaps its matching road ends to its resident neighbours'
 * contracts (point, direction, width and the three carriageway heights), so
 * both sides of a seam share one cross-section whichever tile came first.
 * Positions are in the receiving tile's metres when passed to Build().
 */
struct FSimCruiseSeamEnd
{
	FVector2d P = FVector2d::ZeroVector;
	FVector2d T = FVector2d(1.0, 0.0);
	double HalfW = 0.0;
	double HL = 0.0, HC = 0.0, HR = 0.0;
};

class SIMCRUISE_API FSimCruiseRoadNetwork
{
public:
	/** Build every drivable ribbon of a tile. TileSizeM is the session's tile size. */
	void Build(const FSimCruiseTileData& Tile, double TileSizeM, const TArray<FSimCruiseSeamEnd>& NeighbourEnds = TArray<FSimCruiseSeamEnd>());

	/** This tile's road ends on its edges, in its own metres. */
	void CollectSeamEnds(TArray<FSimCruiseSeamEnd>& Out) const;

	TArray<FSimCruiseRibbon> Ribbons;
	TArray<FSimCruiseRoadPad> Pads;
	double TileSizeM = 0.0;
	int32 NumContractedEnds = 0;

	/**
	 * The ribbon whose carriageway covers P (expanded by Margin), the highest
	 * where several do; ExcludeRibbon is skipped. Returns false off every carriageway.
	 */
	bool FindCarriageway(const FVector2d& P, FSimCruiseRoadHit& OutHit, int32 ExcludeRibbon = INDEX_NONE, double Margin = 0.0, int32 MinRank = MIN_int32) const;

	/** Nearest ribbon station segment to P within Reach (any lateral), for terrain conditioning and placement. */
	bool FindNearest(const FVector2d& P, double Reach, FSimCruiseRoadHit& OutHit, int32 ExcludeRibbon = INDEX_NONE) const;

	/** Every ribbon segment whose station centres pass within Reach of P (bins). */
	void GatherSegments(const FVector2d& P, double Reach, TArray<TPair<int32, int32>>& Out) const;

	/** Project P onto segment (Ribbon, I..I+1). */
	FSimCruiseRoadHit Project(int32 RibbonIndex, int32 I, const FVector2d& P) const;

	/** Outer lateral extent (from the centre) and height of a ribbon side's last surface point at station i. */
	void OuterEdge(int32 RibbonIndex, int32 I, int32 Side, double& OutLateral, double& OutHeight) const;

	/** Lowest surface height of a station's full cross-section. */
	double LowestSurface(int32 RibbonIndex, int32 I) const;

	/** Stats for logs and tests. */
	int32 NumStations() const;

	static int32 ClassRank(const FString& Class);
	static bool IsDrivableClass(const FString& Class);
	static bool IsMajorClass(const FString& Class);

private:
	static constexpr double BinM = 16.0;
	TMap<FIntPoint, TArray<TPair<int32, int32>>> Bins; // (ribbon, segment start)
	void AddToBins(int32 RibbonIndex);
};
