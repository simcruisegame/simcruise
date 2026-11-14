// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseScan.h"

#include "Live/SimCruiseAerialEye.h"
#include "Scan/SimCruiseScanEye.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Compression.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "World/SimCruiseCellContext.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseScan, Log, All);

namespace
{
	using namespace SimCruiseScan;

	/** The rules' version: bump when BakeRules changes what it writes (part of the cache key). */
	constexpr int32 ScanRulesVersion = 2; // 2: TINT by colour match (MatchTints)
	constexpr uint32 Magic = 0x314E4353; // "SCN1"
	enum : uint8 { CodecRaw = 0, CodecZlib = 2 };

	const TCHAR* const Names[] = { TEXT("unknown"), TEXT("asphalt"), TEXT("concrete"), TEXT("paving"), TEXT("gravel"), TEXT("dirt_track"),
		TEXT("bare_soil"), TEXT("rock"), TEXT("scree"), TEXT("sand"), TEXT("grass_short"), TEXT("grass_tall"), TEXT("dry_grass"), TEXT("crop"),
		TEXT("shrub"), TEXT("forest_floor_broadleaf"), TEXT("forest_floor_conifer"), TEXT("wetland"), TEXT("water"), TEXT("snow_and_ice"),
		TEXT("roof"), TEXT("parking"), TEXT("rail"), TEXT("kerb"), TEXT("shoulder_gravel"), TEXT("road_marking") };

	/** Each class's linear albedo, the middle of its published range (realtime-context.md item 4; estimates where marked). */
	FLinearColor ClassAlbedo(uint8 C)
	{
		switch (static_cast<EClass>(C))
		{
		case EClass::Asphalt: return FLinearColor(0.085f, 0.085f, 0.082f);       // 0.05-0.12
		case EClass::Concrete: return FLinearColor(0.30f, 0.29f, 0.27f);         // 0.25-0.35
		case EClass::Paving: return FLinearColor(0.24f, 0.21f, 0.18f);
		case EClass::Gravel: return FLinearColor(0.26f, 0.23f, 0.19f);
		case EClass::DirtTrack: return FLinearColor(0.20f, 0.16f, 0.11f);
		case EClass::BareSoil: return FLinearColor(0.17f, 0.13f, 0.09f);
		case EClass::Rock: return FLinearColor(0.21f, 0.20f, 0.18f);
		case EClass::Scree: return FLinearColor(0.24f, 0.22f, 0.20f);
		case EClass::Sand: return FLinearColor(0.42f, 0.38f, 0.30f);
		case EClass::GrassShort: return FLinearColor(0.07f, 0.11f, 0.04f);      // 0.08-0.25 broadband
		case EClass::GrassTall: return FLinearColor(0.08f, 0.11f, 0.05f);
		case EClass::DryGrass: return FLinearColor(0.20f, 0.17f, 0.09f);
		case EClass::Crop: return FLinearColor(0.09f, 0.12f, 0.05f);
		case EClass::Shrub: return FLinearColor(0.07f, 0.08f, 0.05f);
		case EClass::ForestBroadleaf: return FLinearColor(0.06f, 0.05f, 0.035f); // leaf litter under crowns
		case EClass::ForestConifer: return FLinearColor(0.05f, 0.04f, 0.03f);
		case EClass::Wetland: return FLinearColor(0.06f, 0.08f, 0.05f);
		case EClass::Water: return FLinearColor(0.03f, 0.06f, 0.07f);
		case EClass::SnowIce: return FLinearColor(0.80f, 0.82f, 0.85f);
		case EClass::Roof: return FLinearColor(0.30f, 0.29f, 0.27f);           // estimate: the mix of membranes and shingles
		case EClass::Parking: return FLinearColor(0.10f, 0.10f, 0.095f);
		case EClass::Rail: return FLinearColor(0.12f, 0.11f, 0.10f);
		case EClass::Kerb: return FLinearColor(0.33f, 0.32f, 0.30f);
		case EClass::ShoulderGravel: return FLinearColor(0.24f, 0.21f, 0.17f);
		case EClass::RoadMarking: return FLinearColor(0.60f, 0.60f, 0.58f);
		default: return FLinearColor(0.15f, 0.14f, 0.12f);
		}
	}

	uint32 Mix(uint32 A, uint32 B) { return HashCombine(A * 2654435761u, B + 0x9E3779B9u); }

	bool PointInRing(const TArray<FVector2d>& R, const FVector2d& P)
	{
		bool bIn = false;
		for (int32 i = 0, j = R.Num() - 1; i < R.Num(); j = i++)
		{
			if (((R[i].Y > P.Y) != (R[j].Y > P.Y)) && (P.X < (R[j].X - R[i].X) * (P.Y - R[i].Y) / (R[j].Y - R[i].Y) + R[i].X)) bIn = !bIn;
		}
		return bIn;
	}

	/** Appends little-endian bytes. */
	struct FWriter
	{
		TArray<uint8>& B;
		template <typename T> void Put(const T& V) { const int32 At = B.AddUninitialized(sizeof(T)); FMemory::Memcpy(B.GetData() + At, &V, sizeof(T)); }
		void Bytes(const void* P, int32 N) { const int32 At = B.AddUninitialized(N); FMemory::Memcpy(B.GetData() + At, P, N); }
	};
	struct FReader
	{
		const TArray<uint8>& B;
		int32 At = 0;
		bool bOk = true;
		template <typename T> T Get() { T V{}; if (At + static_cast<int32>(sizeof(T)) > B.Num()) { bOk = false; return V; } FMemory::Memcpy(&V, B.GetData() + At, sizeof(T)); At += sizeof(T); return V; }
	};

	struct FChunk { uint32 Tag; TArray<uint8> Data; };
	uint32 Tag4(const char* S) { return uint32(uint8(S[0])) | (uint32(uint8(S[1])) << 8) | (uint32(uint8(S[2])) << 16) | (uint32(uint8(S[3])) << 24); }

	template <typename T> TArray<uint8> Raw(const TArray<T>& A) { TArray<uint8> Out; Out.Append(reinterpret_cast<const uint8*>(A.GetData()), A.Num() * sizeof(T)); return Out; }
	template <typename T> bool Unraw(const TArray<uint8>& In, TArray<T>& Out)
	{
		if (In.Num() % sizeof(T)) return false;
		Out.SetNumUninitialized(In.Num() / sizeof(T));
		if (In.Num()) FMemory::Memcpy(Out.GetData(), In.GetData(), In.Num());
		return true;
	}

	TArray<uint8> Compress(const TArray<uint8>& In)
	{
		int32 Size = FCompression::CompressMemoryBound(NAME_Zlib, In.Num());
		TArray<uint8> Out;
		Out.SetNumUninitialized(Size);
		if (!FCompression::CompressMemory(NAME_Zlib, Out.GetData(), Size, In.GetData(), In.Num())) return TArray<uint8>();
		Out.SetNum(Size);
		return Out;
	}

	FCriticalSection ScanLock;
}

namespace SimCruiseScan
{
	const TCHAR* ClassName(uint8 C) { return C < static_cast<uint8>(EClass::Count) ? Names[C] : TEXT("reserved"); }

	FColor ClassColour(uint8 C)
	{
		const FLinearColor A = ClassAlbedo(C);
		// The albedo, brightened for the eye, and a few classes marked so they stand apart.
		if (C == static_cast<uint8>(EClass::Kerb)) return FColor(255, 255, 255);
		if (C == static_cast<uint8>(EClass::Roof)) return FColor(200, 80, 60);
		if (C == static_cast<uint8>(EClass::ShoulderGravel)) return FColor(230, 190, 110);
		return (A * 2.2f).GetClamped().ToFColor(true);
	}

	FString CachePath(const FIntPoint& Tile)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Scan"), FString::Printf(TEXT("v%d"), Version), TEXT("16"),
			FString::FromInt(Tile.X), FString::Printf(TEXT("%d.scan"), Tile.Y));
	}

	void BakeRules(const FSimCruiseTileData& Data, const FSimCruiseTileBuild& Build, FSimCruiseTileScan& Out)
	{
		const double T0 = FPlatformTime::Seconds();
		const double Side = Build.TileSizeM;
		const double C = Side / Cells;
		Out = FSimCruiseTileScan();
		Out.Tile = Data.Id;
		Out.RulesVersion = ScanRulesVersion;
		Out.DataHash = Build.Context.IsValid() ? Build.Context->DataHash : GetTypeHash(Data.Id);
		const FSimCruiseTileContext* Ctx = Build.Context.Get();
		const float Dry = Ctx ? Ctx->GrassDry : 0.0f;
		const bool bConiferClimate = Ctx && (Ctx->Climate == TEXT("climate:subarctic") || Ctx->Climate == TEXT("climate:oceanic"));

		// Classes at twice the cell resolution (the four sub-samples give each cell its top two).
		const int32 F = 2 * Stored;
		const double Fc = C / 2.0;
		TArray<uint8> Fine;
		Fine.Init(static_cast<uint8>(EClass::BareSoil), F * F);
		TArray<uint8> Prio;
		Prio.Init(0, F * F);
		auto CentreOf = [&](int32 i, int32 j) { return FVector2d((i - 2 * Guard + 0.5) * Fc, (j - 2 * Guard + 0.5) * Fc); };
		auto Set = [&](int32 k, EClass Cl, uint8 P) { if (P >= Prio[k]) { Prio[k] = P; Fine[k] = static_cast<uint8>(Cl); } };

		// 1. The ground cover (the build's mask: vegetation, sand, paths, water), slope and season.
		const int32 MS = SimCruiseGroundMask::Size;
		const bool bMask = Build.Mask.Num() == MS * MS;
		TArray<FVector2d> Centres;
		for (const FVector2d& U : Data.BuildingCentres) Centres.Add(U * Side);
		// Building centres within 35 m, counted once on a 16 m grid (one cell of apron each side).
		const double GridM = 16.0;
		const int32 GN = FMath::CeilToInt(Side / GridM) + 2;
		TArray<int32> Near;
		Near.Init(0, GN * GN);
		for (int32 gy = 0; gy < GN; ++gy)
		for (int32 gx = 0; gx < GN; ++gx)
		{
			const FVector2d G((gx - 1 + 0.5) * GridM, (gy - 1 + 0.5) * GridM);
			for (const FVector2d& B : Centres) Near[gy * GN + gx] += FVector2d::DistSquared(B, G) < 35.0 * 35.0 ? 1 : 0;
		}
		const int32 TN = Build.TerrainN;
		auto SlopeAt = [&](const FVector2d& P)
		{
			if (TN <= 0 || Build.TerrainHeights.Num() != (TN + 1) * (TN + 1)) return 0.0;
			const double Cell = Side / TN;
			const int32 I = FMath::Clamp(FMath::FloorToInt(P.X / Cell), 0, TN - 1), J = FMath::Clamp(FMath::FloorToInt(P.Y / Cell), 0, TN - 1);
			const double H00 = Build.TerrainHeights[J * (TN + 1) + I], H10 = Build.TerrainHeights[J * (TN + 1) + I + 1], H01 = Build.TerrainHeights[(J + 1) * (TN + 1) + I];
			return FMath::Sqrt(FMath::Square((H10 - H00) / Cell) + FMath::Square((H01 - H00) / Cell));
		};
		for (int32 j = 0; j < F; ++j)
		for (int32 i = 0; i < F; ++i)
		{
			const FVector2d P = CentreOf(i, j);
			const int32 k = j * F + i;
			if (bMask)
			{
				const int32 Mx = FMath::Clamp(FMath::FloorToInt(P.X / Side * MS), 0, MS - 1), My = FMath::Clamp(FMath::FloorToInt(P.Y / Side * MS), 0, MS - 1);
				const FColor M = Build.Mask[My * MS + Mx];
				if (M.A > 128) { Set(k, EClass::Water, 10); continue; }
				if (M.G > 128) Set(k, EClass::Sand, 2);
				else if (M.R > 128)
				{
					// The season's dry share: a stable share of the lawn cells by their global id.
					// Global half-cell ids (the guard band names the neighbour's cells by the same ids).
					const uint32 H = Mix(Mix(static_cast<uint32>(Data.Id.X * 2 * Cells + i - 2 * Guard), static_cast<uint32>(Data.Id.Y * 2 * Cells + j - 2 * Guard)), 77u);
					Set(k, (H & 0xFFFF) / 65535.0f < Dry ? EClass::DryGrass : EClass::GrassShort, 2);
				}
				if (M.B > 128) Set(k, EClass::Paving, 3);
			}
			// Bare ground in town is a lot or a yard: parking where buildings stand close.
			if (Prio[k] == 0)
			{
				const int32 Gx = FMath::Clamp(FMath::FloorToInt(P.X / GridM) + 1, 0, GN - 1), Gy = FMath::Clamp(FMath::FloorToInt(P.Y / GridM) + 1, 0, GN - 1);
				if (Near[Gy * GN + Gx] >= 2) Set(k, EClass::Parking, 1);
			}
			// Steep ground is cut soil, steeper rock (30 and 38 degrees).
			const double Slope = SlopeAt(P);
			if (Slope > 0.78) Set(k, EClass::Rock, 4);
			else if (Slope > 0.58) Set(k, EClass::BareSoil, 4);
		}
		// Woods and forests the map draws.
		for (const FSimCruiseLandPolygon& L : Data.Land)
		{
			const bool bWood = L.Layer == TEXT("landcover") && (L.Class == TEXT("wood") || L.Class == TEXT("forest"));
			const bool bWet = L.Layer == TEXT("landcover") && L.Class == TEXT("wetland");
			const bool bFarm = L.Layer == TEXT("landcover") && L.Class == TEXT("farmland");
			if (!(bWood || bWet || bFarm) || L.Rings.Num() == 0) continue;
			TArray<FVector2d> R;
			for (const FVector2d& U : L.Rings[0]) R.Add(U * Side);
			if (R.Num() < 3) continue;
			const FBox2d Box(R);
			const int32 I0 = FMath::Max(0, FMath::FloorToInt(Box.Min.X / Fc) + 2 * Guard), I1 = FMath::Min(F - 1, FMath::CeilToInt(Box.Max.X / Fc) + 2 * Guard);
			const int32 J0 = FMath::Max(0, FMath::FloorToInt(Box.Min.Y / Fc) + 2 * Guard), J1 = FMath::Min(F - 1, FMath::CeilToInt(Box.Max.Y / Fc) + 2 * Guard);
			const EClass Cl = bWood ? (bConiferClimate ? EClass::ForestConifer : EClass::ForestBroadleaf) : bWet ? EClass::Wetland : EClass::Crop;
			for (int32 j = J0; j <= J1; ++j) for (int32 i = I0; i <= I1; ++i) if (PointInRing(R, CentreOf(i, j))) Set(j * F + i, Cl, 3);
		}

		// 2. The roads: shoulders, sidewalks, curbs, carriageways (each over the last).
		const FSimCruiseRoadNetwork& Roads = Build.Roads;
		for (const FSimCruiseRibbon& Rb : Roads.Ribbons)
		{
			for (int32 s = 0; s + 1 < Rb.St.Num(); ++s)
			{
				const FSimCruiseRibbonStation& A = Rb.St[s];
				const FSimCruiseRibbonStation& B = Rb.St[s + 1];
				const double Reach = FMath::Max(A.HalfW, B.HalfW) + 6.0;
				const double X0 = FMath::Min(A.C.X, B.C.X) - Reach, X1 = FMath::Max(A.C.X, B.C.X) + Reach;
				const double Y0 = FMath::Min(A.C.Y, B.C.Y) - Reach, Y1 = FMath::Max(A.C.Y, B.C.Y) + Reach;
				const int32 I0 = FMath::Max(0, FMath::FloorToInt(X0 / Fc) + 2 * Guard), I1 = FMath::Min(F - 1, FMath::CeilToInt(X1 / Fc) + 2 * Guard);
				const int32 J0 = FMath::Max(0, FMath::FloorToInt(Y0 / Fc) + 2 * Guard), J1 = FMath::Min(F - 1, FMath::CeilToInt(Y1 / Fc) + 2 * Guard);
				const FVector2d AB = B.C - A.C;
				const double Len2 = FMath::Max(AB.SizeSquared(), 1e-9);
				for (int32 j = J0; j <= J1; ++j)
				for (int32 i = I0; i <= I1; ++i)
				{
					const FVector2d P = CentreOf(i, j);
					const double T = FMath::Clamp(FVector2d::DotProduct(P - A.C, AB) / Len2, 0.0, 1.0);
					const FVector2d Q = A.C + AB * T;
					const FVector2d Rt = FMath::Lerp(A.R, B.R, T).GetSafeNormal();
					const double Lat = FVector2d::DotProduct(P - Q, Rt);
					const double Along = FVector2d::Distance(P, Q + Rt * Lat);
					if (Along > 0.05 + 1e-6 && (T <= 0.0 || T >= 1.0)) continue; // past the segment's ends
					const double HalfW = FMath::Lerp(A.HalfW, B.HalfW, T);
					const double D = FMath::Abs(Lat) - HalfW;
					const int32 k = j * F + i;
					if (D <= 0.0) { Set(k, Rb.bUnpaved ? EClass::DirtTrack : EClass::Asphalt, Rb.bUnpaved ? 6 : 8); continue; }
					const int32 SideIdx = Lat > 0.0 ? 1 : 0;
					const ESimCruiseRoadEdge Edge = Rb.Sides[SideIdx].Edge.IsValidIndex(s) ? Rb.Sides[SideIdx].Edge[s] : ESimCruiseRoadEdge::Rural;
					if (Edge == ESimCruiseRoadEdge::Kerb)
					{
						const double Walk = Rb.Sides[SideIdx].SidewalkM.IsValidIndex(s) ? Rb.Sides[SideIdx].SidewalkM[s] : SimCruiseRoadSection::DefaultSidewalkM;
						const double Curb = SimCruiseRoadSection::GutterM + SimCruiseRoadSection::CurbBatterM + SimCruiseRoadSection::CurbTopM;
						if (D <= SimCruiseRoadSection::GutterM) Set(k, EClass::Concrete, 7);
						else if (D <= Curb) Set(k, EClass::Kerb, 7);
						else if (D <= Curb + Walk) Set(k, EClass::Concrete, 5);
					}
					else if (Edge == ESimCruiseRoadEdge::Rural && !Rb.bUnpaved)
					{
						if (D <= SimCruiseRoadSection::RuralShoulderM) Set(k, EClass::ShoulderGravel, 6);
					}
					else if (D <= SimCruiseRoadSection::TownEdgeM) Set(k, EClass::Concrete, 5);
				}
			}
		}

		// 3. Roofs over everything but the carriageway.
		for (const FSimCruiseBuildingRow& Row : Data.Buildings)
		{
			if (Row.MinHeightM > 0.5 || Row.Ring.Num() < 3) continue;
			TArray<FVector2d> R;
			for (const FVector2d& U : Row.Ring) R.Add(U * Side);
			const FBox2d Box(R);
			const int32 I0 = FMath::Max(0, FMath::FloorToInt(Box.Min.X / Fc) + 2 * Guard), I1 = FMath::Min(F - 1, FMath::CeilToInt(Box.Max.X / Fc) + 2 * Guard);
			const int32 J0 = FMath::Max(0, FMath::FloorToInt(Box.Min.Y / Fc) + 2 * Guard), J1 = FMath::Min(F - 1, FMath::CeilToInt(Box.Max.Y / Fc) + 2 * Guard);
			for (int32 j = J0; j <= J1; ++j) for (int32 i = I0; i <= I1; ++i) if (PointInRing(R, CentreOf(i, j))) Set(j * F + i, EClass::Roof, 7);
		}

		// 4. Per cell: the top two classes of its four sub-samples, the albedo, the condition, the ground offset.
		const int32 N = Stored * Stored;
		Out.Clas.SetNumUninitialized(N);
		Out.Cond.SetNumUninitialized(N);
		Out.Albd.SetNumUninitialized(N);
		Out.GroundOffset.Init(0, N);
		Out.CanopyTop.Init(0, N);
		int32 Counts[64] = { 0 };
		for (int32 j = 0; j < Stored; ++j)
		for (int32 i = 0; i < Stored; ++i)
		{
			uint8 S[4] = { Fine[(2 * j) * F + 2 * i], Fine[(2 * j) * F + 2 * i + 1], Fine[(2 * j + 1) * F + 2 * i], Fine[(2 * j + 1) * F + 2 * i + 1] };
			uint8 A = S[0], Bc = S[0];
			int32 NA = 0, NB = 0;
			for (int32 a = 0; a < 4; ++a)
			{
				int32 Na = 0;
				for (int32 b = 0; b < 4; ++b) Na += S[b] == S[a];
				if (Na > NA || (Na == NA && S[a] < A)) { NA = Na; A = S[a]; }
			}
			for (int32 a = 0; a < 4; ++a)
			{
				if (S[a] == A) continue;
				int32 Nb = 0;
				for (int32 b = 0; b < 4; ++b) Nb += S[b] == S[a];
				if (Nb > NB || (Nb == NB && S[a] < Bc)) { NB = Nb; Bc = S[a]; }
			}
			if (NB == 0) Bc = A;
			const int32 k = Out.Index(i, j);
			// Confidence: map geometry and the build's mask (rules) 160; guard cells outside the tile 96.
			const bool bInside = i >= Guard && j >= Guard && i < Guard + Cells && j < Guard + Cells;
			Out.Clas[k] = FColor(A, Bc, static_cast<uint8>(255 * NA / 4), bInside ? 160 : 96);
			++Counts[A];
			const FLinearColor Alb = ClassAlbedo(A) * (NA / 4.0f) + ClassAlbedo(Bc) * (1.0f - NA / 4.0f);
			const FColor Srgb = Alb.ToFColor(true);
			Out.Albd[k] = FColor(Srgb.R, Srgb.G, Srgb.B, 0);
			const uint8 Wear = A == static_cast<uint8>(EClass::Asphalt) ? 120 : A == static_cast<uint8>(EClass::Concrete) ? 90 : 40;
			Out.Cond[k] = FColor(Wear, static_cast<uint8>(255 * Dry), A == static_cast<uint8>(EClass::Water) ? 255 : 0, 0);
			// The ground the build shaped (cuts, ditches, beds under roads) less the natural ground.
			if (TN > 0 && Build.TerrainHeights.Num() == Build.NaturalHeights.Num() && Build.TerrainHeights.Num() == (TN + 1) * (TN + 1))
			{
				const FVector2d P((i - Guard + 0.5) * C, (j - Guard + 0.5) * C);
				const double Cell = Side / TN;
				const int32 I = FMath::Clamp(FMath::RoundToInt(P.X / Cell), 0, TN), J = FMath::Clamp(FMath::RoundToInt(P.Y / Cell), 0, TN);
				const double Off = Build.TerrainHeights[J * (TN + 1) + I] - Build.NaturalHeights[J * (TN + 1) + I];
				Out.GroundOffset[k] = static_cast<int16>(FMath::Clamp(FMath::RoundToInt(Off * 200.0), -32767, 32767));
			}
		}
		// TINT: the build's colour match (x1.0 where it had no photograph); an eye may override it.
		for (int32 c = 0; c < 64; ++c) Out.Tint[c] = Build.Tint[c];

		// 5. Strings (the street names of the road record) and META.
		TSet<FString> Streets;
		if (Ctx) for (const FSimCruiseCellContext& Cell : Ctx->Cells) if (!Cell.StreetName.IsEmpty()) Streets.Add(Cell.StreetName);
		Out.Strings = Streets.Array();
		Out.Strings.Sort();
		Out.BuildSeconds = FPlatformTime::Seconds() - T0;
		FString Hist;
		for (int32 c = 0; c < static_cast<int32>(EClass::Count); ++c) if (Counts[c] > 0) Hist += FString::Printf(TEXT("%s\"%s\":%d"), Hist.IsEmpty() ? TEXT("") : TEXT(","), Names[c], Counts[c]);
		Out.Meta = FString::Printf(TEXT("{\"scan_version\":%d,\"rules_version\":%d,\"tile\":\"16/%d/%d\",\"cells\":%d,\"guard\":%d,\"cell_m\":%.4f,")
			TEXT("\"sources\":[\"OpenStreetMap/Overture tile data (ODbL)\",\"USGS 3DEP heights (public domain)\",\"Koppen-Geiger, Beck et al. 2023 (CC BY 4.0)\"],")
			TEXT("\"eyes\":[],\"classes\":{%s},\"warnings\":[\"guard band from the tile's own apron (clamped)\"]}"),
			Version, ScanRulesVersion, Data.Id.X, Data.Id.Y, Cells, Guard, C, *Hist);
	}

	TSharedRef<const FSimCruiseTileScan, ESPMode::ThreadSafe> ForBuild(const FSimCruiseTileData& Data, const FSimCruiseTileBuild& Build, bool* bOutCacheHit)
	{
		const FString Path = CachePath(Data.Id);
		// The key includes the eye when one is on (its name and version): its arrival is new data.
		FSimCruiseAerialEye& Eye = FSimCruiseAerialEye::Get();
		uint32 Hash = Build.Context.IsValid() ? Build.Context->DataHash : GetTypeHash(Data.Id);
		// The ground the scan reads too: the shaped terrain, the natural terrain, the cover mask, the footprints.
		Hash = HashCombine(Hash, FCrc::MemCrc32(Build.TerrainHeights.GetData(), Build.TerrainHeights.Num() * sizeof(float)));
		Hash = HashCombine(Hash, FCrc::MemCrc32(Build.NaturalHeights.GetData(), Build.NaturalHeights.Num() * sizeof(float)));
		Hash = HashCombine(Hash, FCrc::MemCrc32(Build.Mask.GetData(), Build.Mask.Num() * sizeof(FColor)));
		Hash = HashCombine(Hash, static_cast<uint32>(Data.Buildings.Num() * 7919 + Data.Land.Num()));
		// and the photograph's colour match (TINT)
		Hash = HashCombine(Hash, FCrc::MemCrc32(Build.Tint, sizeof(Build.Tint)));
		if (Eye.IsReady()) Hash = HashCombine(Hash, HashCombine(GetTypeHash(Eye.Name()), static_cast<uint32>(Eye.Version())));
		TArray<uint8> Bytes;
		if (FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent))
		{
			TSharedRef<FSimCruiseTileScan, ESPMode::ThreadSafe> Cached = MakeShared<FSimCruiseTileScan, ESPMode::ThreadSafe>();
			FString Error;
			if (Cached->Deserialize(Bytes, Error) && Cached->DataHash == Hash && Cached->RulesVersion == ScanRulesVersion)
			{
				if (bOutCacheHit) *bOutCacheHit = true;
				return Cached;
			}
		}
		TSharedRef<FSimCruiseTileScan, ESPMode::ThreadSafe> Made = MakeShared<FSimCruiseTileScan, ESPMode::ThreadSafe>();
		BakeRules(Data, Build, *Made);
		// The eye, when it is on and has read this tile already (-SimCruiseScanEye=<name>). A rules
		// scan made while the eye is still reading the tile is not saved: the next build fuses it.
		bool bSave = !Eye.IsReady();
		if (Eye.IsReady())
		{
			TArray<FSimCruiseScanChannel> Channels;
			TArray<FSimCruiseClassTint> Tints;
			if (Eye.Lookup(Data.Id, Channels, &Tints)) { FuseEye(*Made, Eye.Name(), Channels, Tints); bSave = true; }
		}
		Made->DataHash = Hash;
		const TArray<uint8> Out = Made->Serialize();
		if (bSave) FFileHelper::SaveArrayToFile(Out, *Path);
		UE_LOG(LogSimCruiseScan, Display, TEXT("Scan %s: rules bake %.1f ms, %.2f MB on disk"), *SimCruiseTileName(Data.Id), Made->BuildSeconds * 1000.0, Out.Num() / 1048576.0);
		if (bOutCacheHit) *bOutCacheHit = false;
		return Made;
	}

	FLinearColor GroundClassColour(EClass Class)
	{
		switch (Class)
		{
		case EClass::GrassShort: return FLinearColor(0.052f, 0.088f, 0.026f);
		case EClass::BareSoil: return FLinearColor(0.15f, 0.14f, 0.11f);
		case EClass::Sand: return FLinearColor(0.50f, 0.45f, 0.35f);
		case EClass::Paving: return FLinearColor(0.27f, 0.26f, 0.24f);
		default: return ClassAlbedo(static_cast<uint8>(Class));
		}
	}

	int32 MatchTints(const TArray<FColor>& Mask, int32 MaskSize, FIntPoint TileId, const FSimCruiseAerialImage& Aerial, FTint (&Out)[64])
	{
		for (int32 c = 0; c < 64; ++c) Out[c] = FTint();
		if (MaskSize <= 0 || Mask.Num() != MaskSize * MaskSize || Aerial.Width <= 0 || Aerial.Pixels.Num() != Aerial.Width * Aerial.Height) return 0;
		// The photograph covers the zoom-14 cell: 4 x 4 tiles.
		const FIntPoint Z14 = SimCruiseGeo::AerialTileOf(TileId);
		const FIntPoint CellUnit(TileId.X - Z14.X * 4, TileId.Y - Z14.Y * 4);
		const EClass Classes[4] = { EClass::GrassShort, EClass::BareSoil, EClass::Sand, EClass::Paving };
		TArray<FLinearColor> Seen[4];
		const int32 Step = FMath::Max(1, MaskSize / 256); // 256 x 256 samples
		for (int32 y = Step / 2; y < MaskSize; y += Step)
		for (int32 x = Step / 2; x < MaskSize; x += Step)
		{
			const FColor M = Mask[y * MaskSize + x];
			const float Veg = M.R / 255.f, Sand = M.G / 255.f, Paved = M.B / 255.f, Water = M.A / 255.f;
			int32 C = -1;
			if (Water > 0.15f) continue;
			if (Veg >= 0.85f) C = 0;
			else if (Sand >= 0.85f) C = 2;
			else if (Paved >= 0.85f) C = 3;
			else if (Veg < 0.15f && Sand < 0.15f && Paved < 0.15f) C = 1;
			if (C < 0) continue;
			const int32 Px = FMath::Clamp(FMath::FloorToInt((CellUnit.X + (x + 0.5) / MaskSize) / 4.0 * Aerial.Width), 0, Aerial.Width - 1);
			const int32 Py = FMath::Clamp(FMath::FloorToInt((CellUnit.Y + (y + 0.5) / MaskSize) / 4.0 * Aerial.Height), 0, Aerial.Height - 1);
			const FLinearColor P = FLinearColor::FromSRGBColor(Aerial.Pixels[Py * Aerial.Width + Px]);
			const float L = 0.2126f * P.R + 0.7152f * P.G + 0.0722f * P.B;
			// Shadow and canopy (dark), glare and roofs (bright) say nothing about the ground's hue;
			// grass only where the photograph shows green (the shader's own test).
			if (L < 0.02f || L > 0.6f) continue;
			if (C == 0 && P.G <= FMath::Max(P.R, P.B)) continue;
			Seen[C].Add(P);
		}
		int32 Tinted = 0;
		for (int32 c = 0; c < 4; ++c)
		{
			const int32 N = Seen[c].Num();
			if (N < 400) continue;
			auto Median = [&](int32 Ch)
			{
				TArray<float> V;
				V.Reserve(N);
				for (const FLinearColor& P : Seen[c]) V.Add(Ch == 0 ? P.R : Ch == 1 ? P.G : P.B);
				V.Sort();
				return V[N / 2];
			};
			const FLinearColor Photo(Median(0), Median(1), Median(2));
			const FLinearColor Ref = GroundClassColour(Classes[c]);
			auto Lum = [](const FLinearColor& X) { return FMath::Max(0.2126f * X.R + 0.7152f * X.G + 0.0722f * X.B, 1e-5f); };
			// The photograph's hue over the class colour's hue, then held at the class luminance.
			FLinearColor T((Photo.R / Lum(Photo)) / (Ref.R / Lum(Ref)), (Photo.G / Lum(Photo)) / (Ref.G / Lum(Ref)), (Photo.B / Lum(Photo)) / (Ref.B / Lum(Ref)));
			T = T / (Lum(Ref * T) / Lum(Ref));
			const float Strength = FMath::Min(1.f, N / 4000.f);
			auto Ch = [Strength](float V) { return static_cast<uint8>(FMath::RoundToInt(128.f * FMath::Lerp(1.f, FMath::Clamp(V, 0.8f, 1.25f), Strength))); };
			FTint& O = Out[static_cast<int32>(Classes[c])];
			O.R = Ch(T.R); O.G = Ch(T.G); O.B = Ch(T.B);
			O.Confidence = static_cast<uint8>(FMath::Clamp(N / 40, 1, 120)); // below an eye's (it overrides)
			O.Samples = static_cast<uint16>(FMath::Min(N, 65535));
			++Tinted;
		}
		return Tinted;
	}

	uint8 ClassOfChannel(FName Channel)
	{
		FString C = Channel.ToString();
		C.RemoveFromStart(TEXT("surface_logits_"));
		if (C == TEXT("canopy_prob") || C == TEXT("tree")) return static_cast<uint8>(EClass::ForestBroadleaf);
		if (C == TEXT("water_prob") || C == TEXT("water")) return static_cast<uint8>(EClass::Water);
		if (C == TEXT("road_prob") || C == TEXT("road")) return static_cast<uint8>(EClass::Asphalt);
		if (C == TEXT("building_prob") || C == TEXT("roof")) return static_cast<uint8>(EClass::Roof);
		if (C == TEXT("built") || C == TEXT("built_ground")) return static_cast<uint8>(EClass::Concrete);
		for (uint8 k = 1; k < static_cast<uint8>(EClass::Count); ++k) if (C == Names[k]) return k;
		return 0;
	}

	int32 FuseEye(FSimCruiseTileScan& Scan, FName EyeName, const TArray<FSimCruiseScanChannel>& Channels, const TArray<FSimCruiseClassTint>& Tints)
	{
		const int32 N = Stored * Stored;
		if (Scan.Clas.Num() != N) return 0;
		// Held-out IoU per class on 8 random US places (the AI thread, 2026-11-14): how far to trust it.
		auto Iou = [](uint8 C)
		{
			switch (static_cast<EClass>(C))
			{
			case EClass::GrassShort: case EClass::GrassTall: case EClass::DryGrass: return 0.82f;
			case EClass::Crop: return 0.80f;
			case EClass::ForestBroadleaf: case EClass::ForestConifer: case EClass::Shrub: return 0.72f;
			case EClass::Water: return 0.65f;
			case EClass::Concrete: return 0.33f; // built ground
			case EClass::BareSoil: case EClass::DirtTrack: return 0.27f;
			case EClass::Wetland: case EClass::SnowIce: return 0.5f;
			default: return 0.0f; // roads and roofs: map geometry only
			}
		};
		auto IsMapGeometry = [](uint8 C)
		{
			switch (static_cast<EClass>(C))
			{
			case EClass::Asphalt: case EClass::Concrete: case EClass::Kerb: case EClass::ShoulderGravel: case EClass::Roof:
			case EClass::Water: case EClass::Paving: case EClass::DirtTrack: case EClass::Rail: case EClass::RoadMarking: return true;
			default: return false;
			}
		};
		struct FUse { const FSimCruiseScanChannel* Ch; uint8 Class; float Iou; };
		TArray<FUse> Use;
		for (const FSimCruiseScanChannel& Ch : Channels)
		{
			if (Ch.Size != Stored || Ch.Data.Num() != N) continue;
			const uint8 C = ClassOfChannel(Ch.Name);
			const float W = Iou(C);
			if (C == 0 || W <= 0.0f || C == static_cast<uint8>(EClass::Water)) continue; // water stays the map's
			Use.Add({ &Ch, C, W });
		}
		int32 Changed = 0;
		for (int32 k = 0; k < N; ++k)
		{
			FColor& Cell = Scan.Clas[k];
			if (IsMapGeometry(Cell.R)) continue;
			float Best = Cell.A / 255.0f;
			uint8 BestClass = 0;
			for (const FUse& U : Use)
			{
				const float P = U.Ch->Data[k] / 255.0f;
				const bool bWeak = U.Class == static_cast<uint8>(EClass::Concrete) || U.Class == static_cast<uint8>(EClass::BareSoil) || U.Class == static_cast<uint8>(EClass::DirtTrack);
				if (bWeak && P < 0.5f) continue;
				const float Score = P * U.Iou * FMath::Clamp(U.Ch->Confidence, 0.0f, 1.0f);
				if (Score > Best) { Best = Score; BestClass = U.Class; }
			}
			if (BestClass == 0 || BestClass == Cell.R) continue;
			// A forest floor of the climate the rules chose (conifer stays conifer).
			if (BestClass == static_cast<uint8>(EClass::ForestBroadleaf) && Cell.R == static_cast<uint8>(EClass::ForestConifer)) continue;
			Cell = FColor(BestClass, Cell.R, 200, static_cast<uint8>(FMath::Clamp(Best * 255.0f, 0.0f, 255.0f)));
			const FColor S = ClassAlbedo(BestClass).ToFColor(true);
			Scan.Albd[k] = FColor(S.R, S.G, S.B, Scan.Albd[k].A);
			++Changed;
		}
		// The place's own colours (TINT): the photo's mean over the class's sure cells, relative to
		// the class's albedo, as a multiplier (128 = x1.0, held to x0.5-x2).
		int32 Tinted = 0;
		for (const FSimCruiseClassTint& T : Tints)
		{
			const uint8 C = ClassOfChannel(T.Channel);
			if (C == 0 || T.Confidence <= 0.0f) continue;
			const FLinearColor Seen = FLinearColor::FromSRGBColor(T.Mean), Base = ClassAlbedo(C);
			auto M = [](float S, float B) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(128.0f * FMath::Clamp(S / FMath::Max(B, 1e-3f), 0.5f, 2.0f)), 64, 255)); };
			FTint& Out = Scan.Tint[C];
			const uint8 Conf = static_cast<uint8>(FMath::Clamp(T.Confidence * 255.0f, 0.0f, 255.0f));
			if (Conf <= Out.Confidence) continue;
			Out.R = M(Seen.R, Base.R); Out.G = M(Seen.G, Base.G); Out.B = M(Seen.B, Base.B);
			Out.Confidence = Conf;
			Out.Samples = static_cast<uint16>(FMath::Clamp(T.Share * Cells * Cells, 0.0f, 65535.0f));
			++Tinted;
		}
		if (Changed > 0 || Tinted > 0)
		{
			Scan.Meta.RemoveFromEnd(TEXT("}"));
			Scan.Meta += FString::Printf(TEXT(",\"eye\":{\"name\":\"%s\",\"cells_changed\":%d,\"tints\":%d}}"), *EyeName.ToString(), Changed, Tinted);
		}
		return Changed;
	}

	bool WriteClassView(const FSimCruiseTileScan& Scan, FString* OutPath)
	{
		TArray<FColor> Px;
		Px.SetNumUninitialized(Stored * Stored);
		for (int32 k = 0; k < Px.Num(); ++k) Px[k] = ClassColour(Scan.Clas.IsValidIndex(k) ? Scan.Clas[k].R : 0);
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Png = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() || !Png->SetRaw(Px.GetData(), Px.Num() * sizeof(FColor), Stored, Stored, ERGBFormat::BGRA, 8)) return false;
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Scan"), TEXT("view"), SimCruiseTileName(Scan.Tile) + TEXT(".png"));
		const TArray64<uint8> Bytes = Png->GetCompressed();
		if (OutPath) *OutPath = Path;
		return FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Bytes.GetData(), Bytes.Num()), *Path);
	}
}

FIntPoint FSimCruiseTileScan::CellOf(const FVector2d& P, double SideM) const
{
	const double C = SideM / SimCruiseScan::Cells;
	return FIntPoint(FMath::Clamp(FMath::FloorToInt(P.X / C) + SimCruiseScan::Guard, 0, SimCruiseScan::Stored - 1),
		FMath::Clamp(FMath::FloorToInt(P.Y / C) + SimCruiseScan::Guard, 0, SimCruiseScan::Stored - 1));
}

TArray<uint8> FSimCruiseTileScan::Serialize() const
{
	TArray<FChunk> Chunks;
	Chunks.Add({ Tag4("CLAS"), Raw(Clas) });
	Chunks.Add({ Tag4("COND"), Raw(Cond) });
	Chunks.Add({ Tag4("ALBD"), Raw(Albd) });
	{
		TArray<uint8> H = Raw(GroundOffset);
		H.Append(Raw(CanopyTop));
		Chunks.Add({ Tag4("HGHT"), MoveTemp(H) });
	}
	Chunks.Add({ Tag4("INST"), Raw(Instances) });
	{
		TArray<uint8> T;
		T.Append(reinterpret_cast<const uint8*>(Tint), sizeof(Tint));
		Chunks.Add({ Tag4("TINT"), MoveTemp(T) });
	}
	{
		TArray<uint8> S;
		for (const FString& Str : Strings)
		{
			const FTCHARToUTF8 U(*Str);
			S.Append(reinterpret_cast<const uint8*>(U.Get()), U.Length());
			S.Add(0);
		}
		Chunks.Add({ Tag4("STRS"), MoveTemp(S) });
	}
	{
		const FTCHARToUTF8 U(*Meta);
		TArray<uint8> M;
		M.Append(reinterpret_cast<const uint8*>(U.Get()), U.Length());
		Chunks.Add({ Tag4("META"), MoveTemp(M) });
	}
	// Rasters go zlib-compressed (the contract names zstd; UE has zlib built in: the codec byte says which).
	TArray<TPair<uint8, TArray<uint8>>> Packed;
	for (FChunk& Ch : Chunks)
	{
		TArray<uint8> Z = Ch.Data.Num() > 4096 ? Compress(Ch.Data) : TArray<uint8>();
		if (Z.Num() > 0 && Z.Num() < Ch.Data.Num()) Packed.Add(TPair<uint8, TArray<uint8>>(CodecZlib, MoveTemp(Z)));
		else Packed.Add(TPair<uint8, TArray<uint8>>(CodecRaw, Ch.Data));
	}
	TArray<uint8> Out;
	FWriter W{ Out };
	W.Put(Magic);
	W.Put(SimCruiseScan::Version);
	W.Put(Zoom);
	W.Put(uint8(0));
	W.Put(Tile.X);
	W.Put(Tile.Y);
	double W0, N0, E0, S0;
	SimCruiseGeo::TileToLonLat(FVector2d(Tile.X, Tile.Y), W0, N0);
	SimCruiseGeo::TileToLonLat(FVector2d(Tile.X + 1, Tile.Y + 1), E0, S0);
	W.Put(W0); W.Put(S0); W.Put(E0); W.Put(N0);
	W.Put(int32(SimCruiseScan::Cells));
	W.Put(int32(SimCruiseScan::Guard));
	W.Put(BuildSeconds);
	W.Put(DataHash);
	W.Put(RulesVersion);
	W.Put(uint32(Chunks.Num()));
	const int32 TableAt = Out.Num();
	const int32 Entry = 4 + 4 + 4 + 4 + 4; // tag, offset, stored size, raw size, codec + pad
	Out.AddZeroed(Entry * Chunks.Num());
	for (int32 c = 0; c < Chunks.Num(); ++c)
	{
		const uint32 Offset = Out.Num();
		W.Bytes(Packed[c].Value.GetData(), Packed[c].Value.Num());
		uint8* E = Out.GetData() + TableAt + c * Entry;
		const uint32 Size = Packed[c].Value.Num(), RawSize = Chunks[c].Data.Num(), Codec = Packed[c].Key;
		FMemory::Memcpy(E, &Chunks[c].Tag, 4);
		FMemory::Memcpy(E + 4, &Offset, 4);
		FMemory::Memcpy(E + 8, &Size, 4);
		FMemory::Memcpy(E + 12, &RawSize, 4);
		FMemory::Memcpy(E + 16, &Codec, 4);
	}
	return Out;
}

TArray<uint8> FSimCruiseTileScan::SerializeWithoutTime() const
{
	FSimCruiseTileScan Copy = *this;
	Copy.BuildSeconds = 0.0;
	return Copy.Serialize();
}

bool FSimCruiseTileScan::Deserialize(const TArray<uint8>& Bytes, FString& OutError)
{
	FReader R{ Bytes };
	if (R.Get<uint32>() != Magic) { OutError = TEXT("not a SCN1 file"); return false; }
	const uint16 Ver = R.Get<uint16>();
	if (Ver != SimCruiseScan::Version) { OutError = FString::Printf(TEXT("scan version %d, want %d"), Ver, SimCruiseScan::Version); return false; }
	Zoom = R.Get<uint8>();
	R.Get<uint8>();
	Tile.X = R.Get<int32>();
	Tile.Y = R.Get<int32>();
	for (int32 b = 0; b < 4; ++b) R.Get<double>();
	const int32 NCells = R.Get<int32>(), NGuard = R.Get<int32>();
	if (NCells != SimCruiseScan::Cells || NGuard != SimCruiseScan::Guard) { OutError = TEXT("grid size differs"); return false; }
	BuildSeconds = R.Get<double>();
	DataHash = R.Get<uint32>();
	RulesVersion = R.Get<int32>();
	const uint32 NChunks = R.Get<uint32>();
	if (!R.bOk || NChunks > 64) { OutError = TEXT("bad header"); return false; }
	for (uint32 c = 0; c < NChunks; ++c)
	{
		const uint32 Tag = R.Get<uint32>(), Offset = R.Get<uint32>(), Size = R.Get<uint32>(), RawSize = R.Get<uint32>(), Codec = R.Get<uint32>();
		// A chunk never holds more than a few rasters' worth (a corrupt table must not ask for gigabytes).
		constexpr uint32 MaxChunk = SimCruiseScan::Stored * SimCruiseScan::Stored * 16u;
		if (!R.bOk || static_cast<int64>(Offset) + Size > Bytes.Num() || RawSize > MaxChunk) { OutError = TEXT("bad chunk table"); return false; }
		TArray<uint8> Data;
		if ((Codec & 0xFF) == CodecZlib)
		{
			Data.SetNumUninitialized(RawSize);
			if (!FCompression::UncompressMemory(NAME_Zlib, Data.GetData(), RawSize, Bytes.GetData() + Offset, Size)) { OutError = TEXT("chunk does not inflate"); return false; }
		}
		else
		{
			Data.Append(Bytes.GetData() + Offset, Size);
		}
		bool bOk = true;
		if (Tag == Tag4("CLAS")) bOk = Unraw(Data, Clas);
		else if (Tag == Tag4("COND")) bOk = Unraw(Data, Cond);
		else if (Tag == Tag4("ALBD")) bOk = Unraw(Data, Albd);
		else if (Tag == Tag4("HGHT"))
		{
			const int32 N = SimCruiseScan::Stored * SimCruiseScan::Stored;
			if (Data.Num() != N * 4) bOk = false;
			else
			{
				GroundOffset.SetNumUninitialized(N);
				CanopyTop.SetNumUninitialized(N);
				FMemory::Memcpy(GroundOffset.GetData(), Data.GetData(), N * 2);
				FMemory::Memcpy(CanopyTop.GetData(), Data.GetData() + N * 2, N * 2);
			}
		}
		else if (Tag == Tag4("INST")) bOk = Unraw(Data, Instances);
		else if (Tag == Tag4("TINT")) { if (Data.Num() == sizeof(Tint)) FMemory::Memcpy(Tint, Data.GetData(), sizeof(Tint)); else bOk = false; }
		else if (Tag == Tag4("STRS"))
		{
			Strings.Reset();
			int32 Start = 0;
			for (int32 k = 0; k < Data.Num(); ++k)
			{
				if (Data[k] != 0) continue;
				Strings.Add(FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Data.GetData() + Start), k - Start)));
				Start = k + 1;
			}
		}
		else if (Tag == Tag4("META")) Meta = FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Data.GetData()), Data.Num()));
		if (!bOk) { OutError = TEXT("chunk size differs"); return false; }
	}
	const int32 N = SimCruiseScan::Stored * SimCruiseScan::Stored;
	if (Clas.Num() != N || Albd.Num() != N) { OutError = TEXT("rasters missing"); return false; }
	return true;
}
