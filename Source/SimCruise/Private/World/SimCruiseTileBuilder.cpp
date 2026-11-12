// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseTileBuilder.h"

#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "StaticMeshResources.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseTileDetail.h"

namespace
{
	using namespace SimCruiseRoadSection;

	constexpr double FineCellTargetM = 2.2;     // terrain vertex spacing
	constexpr double UnderDepthM = 0.08;        // ground this far under every paved surface
	constexpr double OuterRampM = 0.5;          // ground rises to just under the outer edge over this width
	constexpr double OuterGapM = 0.03;
	constexpr double FadeM = 6.0;               // conditioned ground returns to the natural ground over this
	constexpr double EndFadeM = 3.0;
	constexpr double MarkLiftM = 0.006;
	constexpr double OceanLevelM = -0.1;        // mean sea level is ~0.1 m under NAVD88 zero on the US east coast

	double Smooth01(double T)
	{
		T = FMath::Clamp(T, 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	FVector Cm(const FVector2d& P, double Z) { return FVector(P.X * 100.0, P.Y * 100.0, Z * 100.0); }

	/** A cross-section's carriageway point, lifted. */
	struct FSection
	{
		FVector2d C, T, R;
		double HalfW = 4.0, HL = 0.0, HC = 0.0, HR = 0.0, S = 0.0;
		float Sidewalk = 1.8f;
		double Height(double L) const
		{
			const double U = FMath::Clamp(L / FMath::Max(HalfW, 0.01), -1.0, 1.0);
			return U < 0.0 ? FMath::Lerp(HC, HL, -U) : FMath::Lerp(HC, HR, U);
		}
	};

	FSection SectionAt(const FSimCruiseRibbon& Ribbon, int32 I, int32 Side)
	{
		const FSimCruiseRibbonStation& St = Ribbon.St[I];
		FSection S;
		S.C = St.C; S.T = St.T; S.R = St.R; S.HalfW = St.HalfW; S.HL = St.HL; S.HC = St.HC; S.HR = St.HR; S.S = St.S;
		S.Sidewalk = Ribbon.Sides[Side].SidewalkM[I];
		return S;
	}

	FSection LerpSection(const FSection& A, const FSection& B, double T)
	{
		FSection S;
		S.C = FMath::Lerp(A.C, B.C, T);
		S.T = FMath::Lerp(A.T, B.T, T).GetSafeNormal();
		S.R = FMath::Lerp(A.R, B.R, T).GetSafeNormal();
		S.HalfW = FMath::Lerp(A.HalfW, B.HalfW, T);
		S.HL = FMath::Lerp(A.HL, B.HL, T);
		S.HC = FMath::Lerp(A.HC, B.HC, T);
		S.HR = FMath::Lerp(A.HR, B.HR, T);
		S.S = FMath::Lerp(A.S, B.S, T);
		S.Sidewalk = A.Sidewalk;
		return S;
	}

	/** Profile beyond the paved edge: (offset beyond the edge, height above the edge). */
	void SideProfile(ESimCruiseRoadEdge Edge, double Sidewalk, TArray<FVector2d>& Out, TArray<uint8>& BandLayer)
	{
		Out.Reset();
		BandLayer.Reset();
		switch (Edge)
		{
		case ESimCruiseRoadEdge::Kerb:
		{
			const double Top = -GutterDropM + CurbFaceM;
			Out.Add(FVector2d(0.0, 0.0));
			Out.Add(FVector2d(GutterM, -GutterDropM));
			Out.Add(FVector2d(GutterM + CurbBatterM, Top));
			Out.Add(FVector2d(GutterM + CurbBatterM + CurbTopM, Top + 0.005));
			Out.Add(FVector2d(GutterM + CurbBatterM + CurbTopM + Sidewalk, Top + 0.005 + SidewalkSlope * Sidewalk));
			Out.Add(FVector2d(GutterM + CurbBatterM + CurbTopM + Sidewalk + 0.02, Top + 0.005 + SidewalkSlope * Sidewalk - SkirtM));
			for (int32 b = 0; b < 5; ++b) BandLayer.Add(static_cast<uint8>(ESimCruiseTileLayer::Concrete));
			break;
		}
		case ESimCruiseRoadEdge::Rural:
			Out.Add(FVector2d(0.0, 0.0));
			Out.Add(FVector2d(RuralShoulderM, -RuralShoulderDropM));
			Out.Add(FVector2d(RuralShoulderM + 0.03, -RuralShoulderDropM - SkirtM));
			BandLayer.Add(static_cast<uint8>(ESimCruiseTileLayer::Asphalt));
			BandLayer.Add(static_cast<uint8>(ESimCruiseTileLayer::Asphalt));
			break;
		default:
			Out.Add(FVector2d(0.0, 0.0));
			Out.Add(FVector2d(TownEdgeM, -TownEdgeDropM));
			Out.Add(FVector2d(TownEdgeM + 0.03, -TownEdgeDropM - SkirtM));
			BandLayer.Add(static_cast<uint8>(ESimCruiseTileLayer::Asphalt));
			BandLayer.Add(static_cast<uint8>(ESimCruiseTileLayer::Asphalt));
			break;
		}
	}

	/**
	 * Collapse points of an offset line that would run backwards on the inside of
	 * a tight bend (the offset curve folds when the radius is under the offset).
	 */
	void Unfold(TArray<FVector2d>& Line, const TArray<FVector2d>& Tangents)
	{
		for (int32 i = 1; i < Line.Num(); ++i)
		{
			const FVector2d Step = Line[i] - Line[i - 1];
			if (FVector2d::DotProduct(Step, Tangents[i - 1]) < 0.01 && FVector2d::DotProduct(Step, Tangents[i]) < 0.01)
			{
				Line[i] = Line[i - 1];
			}
		}
	}

	/** A strip of Rows x Cols positions (metres, already in place) with smooth normals along the rows. */
	void EmitStrip(FSimCruiseMeshData& Mesh, const TArray<TArray<FVector>>& Rows, const TArray<FVector>& RowReference, const FLinearColor& Color, bool bReferenceIsUp)
	{
		const int32 NR = Rows.Num();
		if (NR < 2) return;
		const int32 NC = Rows[0].Num();
		TArray<int32> Index;
		Index.SetNum(NR * NC);
		for (int32 r = 0; r < NR; ++r)
		{
			const FVector& Reference = RowReference[FMath::Min(r, RowReference.Num() - 1)];
			for (int32 c = 0; c < NC; ++c)
			{
				const FVector& P = Rows[r][c];
				const FVector Along = Rows[FMath::Min(r + 1, NR - 1)][c] - Rows[FMath::Max(r - 1, 0)][c];
				const FVector Across = Rows[r][FMath::Min(c + 1, NC - 1)] - Rows[r][FMath::Max(c - 1, 0)];
				FVector N = FVector::CrossProduct(Along, Across).GetSafeNormal();
				if (N.IsNearlyZero()) N = bReferenceIsUp ? FVector::UpVector : Reference;
				if (FVector::DotProduct(N, Reference) < 0.0) N = -N;
				Index[r * NC + c] = Mesh.Add(P, N, Color);
			}
		}
		for (int32 r = 0; r + 1 < NR; ++r)
		{
			const FVector& Reference = RowReference[FMath::Min(r, RowReference.Num() - 1)];
			for (int32 c = 0; c + 1 < NC; ++c)
			{
				const int32 A = Index[r * NC + c], B = Index[(r + 1) * NC + c], C = Index[(r + 1) * NC + c + 1], D = Index[r * NC + c + 1];
				const FVector Facing = FVector::CrossProduct(Rows[r + 1][c] - Rows[r][c], Rows[r][c + 1] - Rows[r][c]);
				FVector F = Facing.GetSafeNormal();
				if (F.IsNearlyZero()) continue;
				if (FVector::DotProduct(F, Reference) < 0.0) F = -F;
				Mesh.Quad(A, B, C, D, F);
			}
		}
	}

	// ------------------------------------------------------------------ rasteriser

	struct FCoverage
	{
		int32 Size = 0;
		TArray<uint8> Bits;   // supersampled binary
		void Init(int32 InSize) { Size = InSize; Bits.Init(0, Size * Size); }

		/** Even-odd fill of a polygon (all rings together) in tile units. */
		void FillPolygon(const TArray<TArray<FVector2d>>& Rings)
		{
			struct FEdge { double X0, Y0, X1, Y1; };
			TArray<FEdge> Edges;
			double MinY = TNumericLimits<double>::Max(), MaxY = TNumericLimits<double>::Lowest();
			for (const TArray<FVector2d>& Ring : Rings)
			{
				for (int32 i = 0; i < Ring.Num(); ++i)
				{
					const FVector2d A = Ring[i] * Size, B = Ring[(i + 1) % Ring.Num()] * Size;
					if (A.Y == B.Y) continue;
					Edges.Add({ A.X, A.Y, B.X, B.Y });
					MinY = FMath::Min3(MinY, A.Y, B.Y);
					MaxY = FMath::Max3(MaxY, A.Y, B.Y);
				}
			}
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt(MinY)), Y1 = FMath::Min(Size - 1, FMath::CeilToInt(MaxY));
			TArray<double> Xs;
			for (int32 y = Y0; y <= Y1; ++y)
			{
				const double Yc = y + 0.5;
				Xs.Reset();
				for (const FEdge& E : Edges)
				{
					if ((Yc >= E.Y0) != (Yc >= E.Y1))
					{
						Xs.Add(E.X0 + (Yc - E.Y0) / (E.Y1 - E.Y0) * (E.X1 - E.X0));
					}
				}
				Xs.Sort();
				for (int32 k = 0; k + 1 < Xs.Num(); k += 2)
				{
					const int32 XA = FMath::Max(0, FMath::CeilToInt(Xs[k] - 0.5)), XB = FMath::Min(Size - 1, FMath::FloorToInt(Xs[k + 1] - 0.5));
					for (int32 x = XA; x <= XB; ++x) Bits[y * Size + x] = 1;
				}
			}
		}

		/** A polyline of the given width (tile units). */
		void StrokeLine(const TArray<FVector2d>& Points, double Width)
		{
			const double HalfPx = 0.5 * Width * Size;
			for (int32 i = 1; i < Points.Num(); ++i)
			{
				const FVector2d A = Points[i - 1] * Size, B = Points[i] * Size;
				const int32 X0 = FMath::Max(0, FMath::FloorToInt(FMath::Min(A.X, B.X) - HalfPx - 1));
				const int32 X1 = FMath::Min(Size - 1, FMath::CeilToInt(FMath::Max(A.X, B.X) + HalfPx + 1));
				const int32 Y0 = FMath::Max(0, FMath::FloorToInt(FMath::Min(A.Y, B.Y) - HalfPx - 1));
				const int32 Y1 = FMath::Min(Size - 1, FMath::CeilToInt(FMath::Max(A.Y, B.Y) + HalfPx + 1));
				const FVector2d D = B - A;
				const double L2 = FMath::Max(D.SizeSquared(), 1e-12);
				for (int32 y = Y0; y <= Y1; ++y)
				for (int32 x = X0; x <= X1; ++x)
				{
					const FVector2d P(x + 0.5, y + 0.5);
					const double T = FMath::Clamp(FVector2d::DotProduct(P - A, D) / L2, 0.0, 1.0);
					if (FVector2d::DistSquared(P, A + D * T) <= HalfPx * HalfPx) Bits[y * Size + x] = 1;
				}
			}
		}

		/** Box-filtered coverage of a 2x supersampled grid as 0..255 at half size. */
		uint8 Down(int32 X, int32 Y) const
		{
			const int32 S = Size;
			const int32 Sum = Bits[(2 * Y) * S + 2 * X] + Bits[(2 * Y) * S + 2 * X + 1] + Bits[(2 * Y + 1) * S + 2 * X] + Bits[(2 * Y + 1) * S + 2 * X + 1];
			return static_cast<uint8>(Sum * 255 / 4);
		}
	};

	double Cross2(const FVector2d& A, const FVector2d& B) { return A.X * B.Y - A.Y * B.X; }

	bool InTriangle(const FVector2d& P, const FVector2d& A, const FVector2d& B, const FVector2d& C)
	{
		const double D1 = Cross2(B - A, P - A), D2 = Cross2(C - B, P - B), D3 = Cross2(A - C, P - C);
		return D1 >= 0.0 && D2 >= 0.0 && D3 >= 0.0;
	}

	/** Ear clipping of a simple polygon (no holes); triangles in counter-clockwise (positive area) order. */
	bool EarClip(const TArray<FVector2d>& P, TArray<int32>& Out)
	{
		const int32 N = P.Num();
		if (N < 3) return false;
		double Area = 0.0;
		for (int32 i = 0; i < N; ++i) Area += Cross2(P[i], P[(i + 1) % N]);
		TArray<int32> V;
		for (int32 i = 0; i < N; ++i) V.Add(Area >= 0.0 ? i : N - 1 - i);
		int32 Guard = 0;
		while (V.Num() > 3 && Guard++ < 4 * N)
		{
			bool bClipped = false;
			for (int32 k = 0; k < V.Num(); ++k)
			{
				const int32 A = V[(k + V.Num() - 1) % V.Num()], Bi = V[k], C = V[(k + 1) % V.Num()];
				if (Cross2(P[Bi] - P[A], P[C] - P[Bi]) <= 1e-9) continue;
				bool bInside = false;
				for (const int32 M : V)
				{
					if (M == A || M == Bi || M == C) continue;
					if (InTriangle(P[M], P[A], P[Bi], P[C])) { bInside = true; break; }
				}
				if (bInside) continue;
				Out.Add(A); Out.Add(Bi); Out.Add(C);
				V.RemoveAt(k);
				bClipped = true;
				break;
			}
			if (!bClipped) return false;
		}
		if (V.Num() == 3) { Out.Add(V[0]); Out.Add(V[1]); Out.Add(V[2]); }
		return true;
	}

	bool IsVegetation(const FSimCruiseLandPolygon& L)
	{
		if (L.Layer == TEXT("park")) return true;
		if (L.Layer != TEXT("landcover")) return false;
		return L.Class == TEXT("grass") || L.Class == TEXT("wood") || L.Class == TEXT("farmland") || L.Class == TEXT("forest")
			|| L.Class == TEXT("scrub") || L.Class == TEXT("wetland") || L.Class == TEXT("meadow");
	}

	bool IsSand(const FSimCruiseLandPolygon& L)
	{
		return L.Layer == TEXT("landcover") && (L.Class == TEXT("sand") || L.Class == TEXT("beach"));
	}

	bool IsOpenWater(const FSimCruiseLandPolygon& L)
	{
		return L.Layer == TEXT("water") && L.Class != TEXT("swimming_pool");
	}

	bool IsSea(const FSimCruiseLandPolygon& L)
	{
		return L.Layer == TEXT("water") && (L.Class == TEXT("ocean") || L.Class == TEXT("sea") || L.Class == TEXT("bay"));
	}
}

int32 FSimCruiseMeshData::Add(const FVector& P, const FVector& N, const FLinearColor& C)
{
	Positions.Add(P);
	Normals.Add(N);
	Colors.Add(C);
	return Positions.Num() - 1;
}

bool SimCruiseTileBuilder::Triangulate(const TArray<FVector2d>& Ring, TArray<int32>& OutIndices)
{
	return EarClip(Ring, OutIndices);
}

TUniquePtr<FStaticMeshRenderData> SimCruiseTileBuilder::MakeRenderData(const TArray<const FSimCruiseMeshData*>& Parts)
{
	int32 VertexCount = 0, IndexCount = 0;
	for (const FSimCruiseMeshData* Part : Parts)
	{
		VertexCount += Part->Positions.Num();
		IndexCount += Part->Indices.Num();
	}
	if (VertexCount == 0)
	{
		return nullptr;
	}
	TUniquePtr<FStaticMeshRenderData> Data = MakeUnique<FStaticMeshRenderData>();
	Data->AllocateLODResources(1);
	FStaticMeshLODResources& LOD = Data->LODResources[0];
	// Built here, not loaded from the derived-data cache.
	LOD.bBuffersInlined = true;
	TArray<FStaticMeshBuildVertex> Vertices;
	Vertices.Reserve(VertexCount);
	TArray<uint32> Indices;
	Indices.Reserve(IndexCount);
	FBox Box(ForceInit);
	for (int32 S = 0; S < Parts.Num(); ++S)
	{
		const FSimCruiseMeshData& Part = *Parts[S];
		const uint32 Base = static_cast<uint32>(Vertices.Num());
		const bool bUVs = Part.UV0.Num() == Part.Positions.Num() && Part.UV1.Num() == Part.Positions.Num();
		for (int32 V = 0; V < Part.Positions.Num(); ++V)
		{
			FStaticMeshBuildVertex Vertex;
			Vertex.Position = FVector3f(Part.Positions[V]);
			const FVector N = Part.Normals[V];
			// A tangent square to every normal (as the procedural sections had).
			FVector T = FMath::Abs(N.Z) < 0.9 ? FVector::CrossProduct(FVector::UpVector, N).GetSafeNormal() : FVector::CrossProduct(N, FVector::RightVector).GetSafeNormal() * -1.0;
			if (T.IsNearlyZero()) T = FVector::ForwardVector;
			Vertex.TangentZ = FVector3f(N);
			Vertex.TangentX = FVector3f(T);
			Vertex.TangentY = FVector3f(FVector::CrossProduct(N, T));
			Vertex.UVs[0] = bUVs ? FVector2f(Part.UV0[V]) : FVector2f::ZeroVector;
			Vertex.UVs[1] = bUVs ? FVector2f(Part.UV1[V]) : FVector2f::ZeroVector;
			Vertex.Color = Part.Colors.IsValidIndex(V) ? Part.Colors[V].ToFColor(false) : FColor::White;
			Vertices.Add(Vertex);
			Box += Part.Positions[V];
		}
		FStaticMeshSection& Section = LOD.Sections.AddDefaulted_GetRef();
		Section.MaterialIndex = S;
		Section.FirstIndex = Indices.Num();
		Section.NumTriangles = Part.Indices.Num() / 3;
		Section.MinVertexIndex = Base;
		Section.MaxVertexIndex = Base + FMath::Max(Part.Positions.Num() - 1, 0);
		Section.bEnableCollision = false;
		Section.bCastShadow = true;
		for (const int32 I : Part.Indices) Indices.Add(Base + static_cast<uint32>(I));
	}
	// GPU only: the CPU copies would stay in memory for nothing (collision is the procedural mesh's).
	LOD.VertexBuffers.PositionVertexBuffer.Init(Vertices, false);
	LOD.VertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(true);
	LOD.VertexBuffers.StaticMeshVertexBuffer.Init(Vertices, 2, false);
	LOD.VertexBuffers.ColorVertexBuffer.Init(Vertices, false);
	// The materials read the vertex colours (paint, stucco, gravel and gutter masks).
	LOD.bHasColorVertexData = true;
	LOD.IndexBuffer.SetIndices(Indices, EIndexBufferStride::Force32Bit);
	Data->Bounds = FBoxSphereBounds(Box);
	Data->ScreenSize[0].Default = 1.0f;
	return Data;
}

int32 FSimCruiseMeshData::Add(const FVector& P, const FVector& N, const FLinearColor& C, const FVector2D& InUV0, const FVector2D& InUV1)
{
	UV0.Add(InUV0);
	UV1.Add(InUV1);
	return Add(P, N, C);
}

void FSimCruiseMeshData::Tri(int32 A, int32 B, int32 C, const FVector& Facing)
{
	// Unreal's front face: (C - A) x (B - A) points out of it.
	const FVector Front = FVector::CrossProduct(Positions[C] - Positions[A], Positions[B] - Positions[A]);
	if (FVector::DotProduct(Front, Facing) >= 0.0)
	{
		Indices.Add(A); Indices.Add(B); Indices.Add(C);
	}
	else
	{
		Indices.Add(A); Indices.Add(C); Indices.Add(B);
	}
}

int32 FSimCruiseTileBuild::NumTriangles() const
{
	int32 N = 0;
	for (const FSimCruiseMeshData& M : Layers) N += M.NumTriangles();
	return N;
}

double FSimCruiseTileBuild::TerrainHeightAt(const FVector2d& P) const
{
	if (TerrainN <= 0) return 0.0;
	const double Cell = TileSizeM / TerrainN;
	const double Gx = FMath::Clamp(P.X / Cell, 0.0, static_cast<double>(TerrainN));
	const double Gy = FMath::Clamp(P.Y / Cell, 0.0, static_cast<double>(TerrainN));
	const int32 X = FMath::Min(static_cast<int32>(Gx), TerrainN - 1);
	const int32 Y = FMath::Min(static_cast<int32>(Gy), TerrainN - 1);
	const double U = Gx - X, V = Gy - Y;
	const int32 Stride = TerrainN + 1;
	const int32 I = Y * Stride + X;
	const double H00 = TerrainHeights[I], H10 = TerrainHeights[I + 1], H01 = TerrainHeights[I + Stride], H11 = TerrainHeights[I + Stride + 1];
	if (U + V <= 1.0) return H00 + (H10 - H00) * U + (H01 - H00) * V;
	return H11 + (H10 - H11) * (1.0 - V) + (H01 - H11) * (1.0 - U);
}

namespace SimCruiseTileBuilder
{
	void UnfoldLine(TArray<FVector2d>& Line, const TArray<FVector2d>& Tangents)
	{
		Unfold(Line, Tangents);
	}

	FVector2d TextureAnchorM(const FIntPoint& Tile, double TileSizeM, double PeriodM)
	{
		return FVector2d(FMath::Fmod(static_cast<double>(Tile.X) * TileSizeM, PeriodM), FMath::Fmod(static_cast<double>(Tile.Y) * TileSizeM, PeriodM));
	}

	TArray<FSimCruiseSeamEnd> NeighbourContracts(const FIntPoint& Tile, double TileSizeM, TFunctionRef<const FSimCruiseTileBuild*(const FIntPoint&)> Find)
	{
		TArray<FSimCruiseSeamEnd> Out;
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			const FSimCruiseTileBuild* Other = Find(Tile + Step);
			if (!Other) continue;
			const FVector2d Shift(Step.X * TileSizeM, Step.Y * TileSizeM);
			for (FSimCruiseSeamEnd End : Other->SeamEnds)
			{
				End.P += Shift;
				// Only the neighbour's ends on the shared edge.
				const bool bShared = Step.X == 1 ? FMath::Abs(End.P.X - TileSizeM) < 1e-3
					: Step.X == -1 ? FMath::Abs(End.P.X) < 1e-3
					: Step.Y == 1 ? FMath::Abs(End.P.Y - TileSizeM) < 1e-3
					: FMath::Abs(End.P.Y) < 1e-3;
				if (bShared) Out.Add(End);
			}
		}
		return Out;
	}

	TSharedRef<FSimCruiseTileBuild> Build(const FSimCruiseTileData& Data, double TileSizeM, const TArray<FSimCruiseSeamEnd>& NeighbourEnds)
	{
		const double Started = FPlatformTime::Seconds();
		TSharedRef<FSimCruiseTileBuild> Out = MakeShared<FSimCruiseTileBuild>();
		FSimCruiseTileBuild& B = *Out;
		B.Tile = Data.Id;
		B.TileSizeM = TileSizeM;
		B.AerialKey = Data.AerialKey;
		const double Side = TileSizeM;

		// ---------------------------------------------------------------- ground mask
		const int32 MaskSize = SimCruiseGroundMask::Size;
		FCoverage Veg, Sand, Paved, Water;
		Veg.Init(MaskSize * 2); Sand.Init(MaskSize * 2); Paved.Init(MaskSize * 2); Water.Init(MaskSize * 2);
		for (const FSimCruiseLandPolygon& L : Data.Land)
		{
			if (IsVegetation(L)) Veg.FillPolygon(L.Rings);
			else if (IsSand(L)) Sand.FillPolygon(L.Rings);
			else if (L.Layer == TEXT("water")) Water.FillPolygon(L.Rings);
		}
		for (const FSimCruisePathRow& Path : Data.Pedestrian)
		{
			Paved.StrokeLine(Path.Points, FMath::Clamp(Path.WidthM, 1.0, 6.0) / Side);
		}
		B.Mask.SetNumUninitialized(MaskSize * MaskSize);
		for (int32 y = 0; y < MaskSize; ++y)
		for (int32 x = 0; x < MaskSize; ++x)
		{
			B.Mask[y * MaskSize + x] = FColor(Veg.Down(x, y), Sand.Down(x, y), Paved.Down(x, y), Water.Down(x, y));
		}

		// ---------------------------------------------------------------- roads
		B.Roads.Build(Data, Side, NeighbourEnds);
		B.Roads.CollectSeamEnds(B.SeamEnds);
		const FSimCruiseRoadNetwork& Roads = B.Roads;

		// ---------------------------------------------------------------- terrain grid
		const int32 Upsample = FMath::Max(1, FMath::RoundToInt(Side / Data.Grid / FineCellTargetM));
		const int32 N = Data.Grid * Upsample;
		const double Cell = Side / N;
		B.TerrainN = N;
		const int32 Stride = N + 1;
		B.NaturalHeights.SetNumUninitialized(Stride * Stride);
		B.TerrainHeights.SetNumUninitialized(Stride * Stride);
		for (int32 j = 0; j <= N; ++j)
		for (int32 i = 0; i <= N; ++i)
		{
			B.NaturalHeights[j * Stride + i] = static_cast<float>(Data.HeightAtUnit(FVector2d(static_cast<double>(i) / N, static_cast<double>(j) / N)));
		}

		// Inland open water: a level per polygon (its low ground), ground lowered under it.
		struct FWaterBody { double Level; bool bSea; };
		TArray<FWaterBody> Bodies;
		TArray<int32> VertexBody; VertexBody.Init(INDEX_NONE, Stride * Stride);
		for (const FSimCruiseLandPolygon& L : Data.Land)
		{
			if (!IsOpenWater(L)) continue;
			FCoverage Fill; Fill.Init(N + 1);
			// Vertex-centred fill: scale rings so pixel centres land on grid vertices.
			TArray<TArray<FVector2d>> Rings = L.Rings;
			for (TArray<FVector2d>& Ring : Rings) for (FVector2d& P : Ring) P = (P * N + FVector2d(0.5, 0.5)) / (N + 1);
			Fill.FillPolygon(Rings);
			TArray<float> Inside;
			for (int32 k = 0; k < Stride * Stride; ++k) if (Fill.Bits[k]) Inside.Add(B.NaturalHeights[k]);
			if (Inside.Num() == 0) continue;
			Inside.Sort();
			const bool bSea = IsSea(L);
			const double Level = bSea ? OceanLevelM : Inside[Inside.Num() / 20] - 0.05;
			const int32 Body = Bodies.Add({ Level, bSea });
			for (int32 k = 0; k < Stride * Stride; ++k) if (Fill.Bits[k]) VertexBody[k] = Body;
		}

		// Road conditioning: ground under every paved footprint sits just below it.
		TArray<TPair<int32, int32>> Segments;
		for (int32 j = 0; j <= N; ++j)
		for (int32 i = 0; i <= N; ++i)
		{
			const int32 K = j * Stride + i;
			double H = B.NaturalHeights[K];
			const FVector2d P(i * Cell, j * Cell);
			const bool bBoundary = i == 0 || j == 0 || i == N || j == N;
			// Water polygons are clipped from each tile's own vector source, so seam
			// vertices keep the shared ground (neighbours would disagree on the shore).
			if (VertexBody[K] != INDEX_NONE && !bBoundary)
			{
				H = FMath::Min(H, Bodies[VertexBody[K]].Level - 0.6);
			}
			// Seam vertices keep the ground both tiles share: no tile sees every road near
			// its edge (a road can run along it on one side only). Roads crossing a seam
			// are lifted just clear of that ground there instead (FSimCruiseRoadNetwork).
			if (bBoundary)
			{
				B.TerrainHeights[K] = static_cast<float>(H);
				continue;
			}

			double InsideTarget = TNumericLimits<double>::Max();
			bool bInside = false;
			double BestWeight = 0.0, BestTarget = H;

			auto Consider = [&](double AbsL, double HalfW, double Outer, double Lowest, double OuterH, double Along)
			{
				const double RampStart = Outer - OuterRampM;
				const double InTarget = AbsL <= RampStart ? Lowest - UnderDepthM
					: FMath::Lerp(Lowest - UnderDepthM, OuterH - OuterGapM, (AbsL - RampStart) / OuterRampM);
				const double AlongW = 1.0 - Smooth01(FMath::Max(Along, 0.0) / EndFadeM);
				if (AbsL <= Outer && Along <= 0.0)
				{
					bInside = true;
					InsideTarget = FMath::Min(InsideTarget, InTarget);
					return;
				}
				const double LatW = AbsL <= Outer ? 1.0 : 1.0 - Smooth01((AbsL - Outer) / FadeM);
				const double W = LatW * AlongW;
				const double Target = AbsL <= Outer ? InTarget : OuterH - OuterGapM;
				if (W > BestWeight) { BestWeight = W; BestTarget = Target; }
			};

			for (const FSimCruiseRoadPad& Pad : Roads.Pads)
			{
				const double R = Pad.RadiusAt(P);
				const double D = FVector2d::Distance(P, Pad.Centre);
				if (D > R + FadeM) continue;
				Consider(D, R, R, Pad.Height, Pad.Height, 0.0);
			}
			{
				Roads.GatherSegments(P, 22.0, Segments);
				for (const TPair<int32, int32>& Seg : Segments)
				{
					const FSimCruiseRibbon& Ribbon = Roads.Ribbons[Seg.Key];
					// Under a bridge the ground stays where it is: the river, the road or the valley below.
					if (Ribbon.St[Seg.Value].bBridge && Ribbon.St[Seg.Value + 1].bBridge) continue;
					const FSimCruiseRoadHit Hit = Roads.Project(Seg.Key, Seg.Value, P);
					const bool bFirst = Seg.Value == 0 && Hit.T <= 0.0;
					const bool bLast = Seg.Value + 2 == Ribbon.St.Num() && Hit.T >= 1.0;
					double Along = 0.0;
					if (bFirst || bLast)
					{
						Along = FMath::Abs(Hit.Along) - 0.01;
						const bool bOpenEnd = bFirst ? Ribbon.bStartOnBoundary : Ribbon.bEndOnBoundary;
						if (bOpenEnd) Along = FMath::Abs(Hit.Along) > 0.5 ? 1e9 : 0.0;
					}
					else if (FMath::Abs(Hit.Along) > 1.2)
					{
						continue;
					}
					const int32 SideIndex = Hit.Lateral >= 0.0 ? 1 : 0;
					double OuterA, OuterHA, OuterB, OuterHB;
					Roads.OuterEdge(Seg.Key, Seg.Value, SideIndex, OuterA, OuterHA);
					Roads.OuterEdge(Seg.Key, Seg.Value + 1, SideIndex, OuterB, OuterHB);
					// A cut side (another road's carriageway) only reaches its paved edge.
					if (Ribbon.Sides[SideIndex].Cut[Seg.Value] && Ribbon.Sides[SideIndex].Cut[Seg.Value + 1])
					{
						OuterA = Ribbon.St[Seg.Value].HalfW; OuterB = Ribbon.St[Seg.Value + 1].HalfW;
					}
					const double Outer = FMath::Lerp(OuterA, OuterB, Hit.T);
					const double OuterH = FMath::Lerp(OuterHA, OuterHB, Hit.T);
					const double Lowest = FMath::Min(Roads.LowestSurface(Seg.Key, Seg.Value), Roads.LowestSurface(Seg.Key, Seg.Value + 1));
					Consider(FMath::Abs(Hit.Lateral), Hit.HalfW, Outer, Lowest, OuterH, Along);
				}
			}

			if (bInside) H = InsideTarget;
			else if (BestWeight > 0.0) H = FMath::Lerp(H, BestTarget, BestWeight);
			if (FMath::Abs(H - B.NaturalHeights[K]) > 0.001) ++B.ConditionedVertices;
			B.TerrainHeights[K] = static_cast<float>(H);
		}

		// ---------------------------------------------------------------- terrain mesh
		{
			FSimCruiseMeshData& Mesh = B.Layer(ESimCruiseTileLayer::Terrain);
			Mesh.Positions.Reserve(Stride * Stride);
			B.MinHeightM = TNumericLimits<double>::Max();
			B.MaxHeightM = TNumericLimits<double>::Lowest();
			for (int32 j = 0; j <= N; ++j)
			for (int32 i = 0; i <= N; ++i)
			{
				const double H = B.TerrainHeights[j * Stride + i];
				B.MinHeightM = FMath::Min(B.MinHeightM, H);
				B.MaxHeightM = FMath::Max(B.MaxHeightM, H);
				const double Hx0 = B.TerrainHeights[j * Stride + FMath::Max(i - 1, 0)], Hx1 = B.TerrainHeights[j * Stride + FMath::Min(i + 1, N)];
				const double Hy0 = B.TerrainHeights[FMath::Max(j - 1, 0) * Stride + i], Hy1 = B.TerrainHeights[FMath::Min(j + 1, N) * Stride + i];
				const double Dx = (Hx1 - Hx0) / ((FMath::Min(i + 1, N) - FMath::Max(i - 1, 0)) * Cell);
				const double Dy = (Hy1 - Hy0) / ((FMath::Min(j + 1, N) - FMath::Max(j - 1, 0)) * Cell);
				Mesh.Add(Cm(FVector2d(i * Cell, j * Cell), H), FVector(-Dx, -Dy, 1.0).GetSafeNormal(), FLinearColor::White);
			}
			const FVector Up = FVector::UpVector;
			for (int32 j = 0; j < N; ++j)
			for (int32 i = 0; i < N; ++i)
			{
				const int32 A = j * Stride + i, Bx = A + 1, Cy = A + Stride, D = Cy + 1;
				// The NE-SW diagonal, as the helper's grid interpolates.
				Mesh.Tri(A, Bx, Cy, Up);
				Mesh.Tri(D, Bx, Cy, Up);
			}
		}

		// ---------------------------------------------------------------- water
		{
			FSimCruiseMeshData& Mesh = B.Layer(ESimCruiseTileLayer::Water);
			const int32 WaterStep = Upsample; // coarse cells
			for (int32 j = 0; j < N; j += WaterStep)
			for (int32 i = 0; i < N; i += WaterStep)
			{
				int32 Body = INDEX_NONE;
				for (int32 dj = 0; dj <= WaterStep && Body == INDEX_NONE; dj += WaterStep / FMath::Max(1, WaterStep / 2))
				for (int32 di = 0; di <= WaterStep && Body == INDEX_NONE; di += WaterStep / FMath::Max(1, WaterStep / 2))
				{
					Body = VertexBody[FMath::Min(j + dj, N) * Stride + FMath::Min(i + di, N)];
				}
				if (Body == INDEX_NONE) continue;
				const double Z = Bodies[Body].Level;
				const int32 I1 = FMath::Min(i + WaterStep, N), J1 = FMath::Min(j + WaterStep, N);
				const FLinearColor Tag(Bodies[Body].bSea ? 1.f : 0.f, 0.f, 0.f, 1.f);
				const int32 A = Mesh.Add(Cm(FVector2d(i * Cell, j * Cell), Z), FVector::UpVector, Tag);
				const int32 Bq = Mesh.Add(Cm(FVector2d(I1 * Cell, j * Cell), Z), FVector::UpVector, Tag);
				const int32 C = Mesh.Add(Cm(FVector2d(I1 * Cell, J1 * Cell), Z), FVector::UpVector, Tag);
				const int32 D = Mesh.Add(Cm(FVector2d(i * Cell, J1 * Cell), Z), FVector::UpVector, Tag);
				Mesh.Quad(A, Bq, C, D, FVector::UpVector);
			}
		}

		// ---------------------------------------------------------------- road ribbons
		FSimCruiseMeshData& Asphalt = B.Layer(ESimCruiseTileLayer::Asphalt);
		FSimCruiseMeshData& Concrete = B.Layer(ESimCruiseTileLayer::Concrete);

		// Turnaround pads: a paved disc and its skirt.
		for (const FSimCruiseRoadPad& Pad : Roads.Pads)
		{
			const int32 NP = Pad.Radii.Num();
			const FLinearColor Color(0.f, 0.f, 0.f, 1.f);
			const int32 Hub = Asphalt.Add(Cm(Pad.Centre, Pad.Height), FVector::UpVector, Color);
			B.CarriagewayPoints.Add(Cm(Pad.Centre, Pad.Height));
			TArray<int32> Rim;
			for (int32 k = 0; k < NP; ++k)
			{
				const FVector P = Cm(Pad.OutlinePoint(k), Pad.Height);
				Rim.Add(Asphalt.Add(P, FVector::UpVector, Color));
				B.CarriagewayPoints.Add(P);
			}
			for (int32 k = 0; k < NP; ++k) Asphalt.Tri(Hub, Rim[k], Rim[(k + 1) % NP], FVector::UpVector);
			TArray<TArray<FVector>> Skirt;
			TArray<FVector> Outward;
			Skirt.SetNum(2);
			for (int32 k = 0; k <= NP; ++k)
			{
				const FVector2d Q = Pad.OutlinePoint(k % NP);
				Skirt[0].Add(Cm(Q, Pad.Height));
				Skirt[1].Add(Cm(Q, Pad.Height - SkirtM));
				const FVector2d Dir = (Q - Pad.Centre).GetSafeNormal();
				Outward.Add(FVector(Dir.X, Dir.Y, 0.0));
			}
			// Rows are the two rings; references per column are approximated by the first ring point.
			TArray<TArray<FVector>> Columns;
			Columns.SetNum(NP + 1);
			for (int32 k = 0; k <= NP; ++k) Columns[k] = { Skirt[0][k], Skirt[1][k] };
			EmitStrip(Asphalt, Columns, Outward, Color, false);
		}
		for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
		{
			const FSimCruiseRibbon& Ribbon = Roads.Ribbons[r];
			const int32 NS = Ribbon.St.Num();
			if (NS < 2) continue;
			TArray<FVector2d> Tangents;
			for (const FSimCruiseRibbonStation& St : Ribbon.St) Tangents.Add(St.T);

			// Carriageway: five columns across (edges, quarter points, centre).
			TArray<FVector2d> Cols[5];
			for (int32 c = 0; c < 5; ++c)
			{
				for (const FSimCruiseRibbonStation& St : Ribbon.St) Cols[c].Add(St.C + St.R * (FSimCruiseRibbonStation::ColumnOffset(c) * St.HalfW));
				if (c != 2) Unfold(Cols[c], Tangents);
			}
			TArray<TArray<FVector>> Rows;
			Rows.SetNum(NS);
			for (int32 i = 0; i < NS; ++i)
			{
				const FSimCruiseRibbonStation& St = Ribbon.St[i];
				for (int32 c = 0; c < 5; ++c) Rows[i].Add(Cm(Cols[c][i], St.Column(c)));
				for (const FVector& P : Rows[i]) B.CarriagewayPoints.Add(P);
			}
			EmitStrip(Asphalt, Rows, { FVector::UpVector }, FLinearColor(0.f, 0.f, 0.f, 1.f), true);

			// Open ends inside the tile get a skirt so a dead end never shows a gap.
			for (int32 End = 0; End < 2; ++End)
			{
				if (End == 0 ? Ribbon.bStartOnBoundary : Ribbon.bEndOnBoundary) continue;
				const int32 I = End == 0 ? 0 : NS - 1;
				const FVector Facing = FVector(Ribbon.St[I].T.X, Ribbon.St[I].T.Y, 0.0) * (End == 0 ? -1.0 : 1.0);
				TArray<TArray<FVector>> Skirt;
				Skirt.SetNum(2);
				for (int32 c = 0; c < 5; ++c)
				{
					Skirt[0].Add(Rows[I][c]);
					Skirt[1].Add(Rows[I][c] - FVector(0, 0, SkirtM * 100.0));
				}
				EmitStrip(Asphalt, Skirt, { Facing }, FLinearColor(0.f, 0.f, 0.f, 1.f), false);
			}

			// Sides: runs of one edge treatment that no other carriageway crosses.
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				const double Sign = SideIndex == 0 ? -1.0 : 1.0;
				const FSimCruiseRibbonSide& RS = Ribbon.Sides[SideIndex];
				auto CutAt = [&](const FSection& S, ESimCruiseRoadEdge Edge)
				{
					TArray<FVector2d> Profile; TArray<uint8> Layers;
					SideProfile(Edge, S.Sidewalk, Profile, Layers);
					const double Margin = Edge == ESimCruiseRoadEdge::Kerb ? GutterM : 0.1;
					for (const double L : { S.HalfW + 0.2, S.HalfW + 0.5 * Profile.Last().X, S.HalfW + Profile.Last().X })
					{
						FSimCruiseRoadHit Hit;
						if (Roads.FindCarriageway(S.C + S.R * (Sign * L), Hit, r, Margin)) return true;
					}
					return false;
				};
				int32 i = 0;
				while (i < NS)
				{
					if (RS.Cut[i]) { ++i; continue; }
					const ESimCruiseRoadEdge Edge = RS.Edge[i];
					int32 j = i;
					while (j + 1 < NS && !RS.Cut[j + 1] && RS.Edge[j + 1] == Edge) ++j;
					// The run spans stations i..j; extend to the exact crossing where a cut follows or precedes it.
					TArray<FSection> Run;
					if (i > 0 && RS.Cut[i - 1])
					{
						const FSection A = SectionAt(Ribbon, i - 1, SideIndex), Bs = SectionAt(Ribbon, i, SideIndex);
						double Lo = 0.0, Hi = 1.0;
						for (int32 it = 0; it < 7; ++it) { const double M = 0.5 * (Lo + Hi); if (CutAt(LerpSection(A, Bs, M), Edge)) Lo = M; else Hi = M; }
						if (Hi < 0.999) Run.Add(LerpSection(A, Bs, Hi));
					}
					for (int32 k = i; k <= j; ++k) Run.Add(SectionAt(Ribbon, k, SideIndex));
					if (j + 1 < NS && RS.Cut[j + 1])
					{
						const FSection A = SectionAt(Ribbon, j, SideIndex), Bs = SectionAt(Ribbon, j + 1, SideIndex);
						double Lo = 0.0, Hi = 1.0;
						for (int32 it = 0; it < 7; ++it) { const double M = 0.5 * (Lo + Hi); if (CutAt(LerpSection(A, Bs, M), Edge)) Hi = M; else Lo = M; }
						if (Lo > 0.001) Run.Add(LerpSection(A, Bs, Lo));
					}
					// The next run of another treatment starts at this run's last station.
					const int32 Next = j + 1;
					if (Next < NS && !RS.Cut[Next] && RS.Edge[Next] != Edge && Run.Num() > 0)
					{
						Run.Add(SectionAt(Ribbon, Next, SideIndex));
					}
					i = Next;
					if (Run.Num() < 2) continue;
					for (FSection& S : Run) if (Edge == ESimCruiseRoadEdge::Kerb) S.Sidewalk = RS.SidewalkM[FMath::Min(j, NS - 1)];

					TArray<FVector2d> Profile; TArray<uint8> BandLayers;
					SideProfile(Edge, Run[0].Sidewalk, Profile, BandLayers);
					const int32 NP = Profile.Num();
					// Offset lines of every profile point, unfolded on the inside of bends.
					TArray<FVector2d> RunT;
					for (const FSection& S : Run) RunT.Add(S.T);
					TArray<TArray<FVector2d>> Lines;
					Lines.SetNum(NP);
					for (int32 p = 0; p < NP; ++p)
					{
						for (const FSection& S : Run) Lines[p].Add(S.C + S.R * (Sign * (S.HalfW + Profile[p].X)));
						Unfold(Lines[p], RunT);
					}
					auto PointAt = [&](int32 Row, int32 p)
					{
						const FSection& S = Run[Row];
						const double EdgeH = SideIndex == 0 ? S.HL : S.HR;
						return Cm(Lines[p][Row], EdgeH + Profile[p].Y);
					};
					for (int32 Band = 0; Band + 1 < NP; ++Band)
					{
						TArray<TArray<FVector>> BandRows;
						BandRows.SetNum(Run.Num());
						TArray<FVector> Reference;
						const bool bLast = Band == NP - 2;
						const bool bFace = Edge == ESimCruiseRoadEdge::Kerb && Band == 1;
						for (int32 Row = 0; Row < Run.Num(); ++Row)
						{
							BandRows[Row] = { PointAt(Row, Band), PointAt(Row, Band + 1) };
							const FVector Outward = FVector(Run[Row].R.X, Run[Row].R.Y, 0.0) * Sign;
							Reference.Add(bLast ? Outward : (bFace ? -Outward : FVector::UpVector));
						}
						// Vertex colour: R gravel, G gutter grime, B curb (for the concrete shader).
						FLinearColor Color(0.f, 0.f, 0.f, 1.f);
						if (Edge == ESimCruiseRoadEdge::Rural) Color.R = 1.f;
						if (Edge == ESimCruiseRoadEdge::Kerb && Band == 0) Color.G = 1.f;
						if (Edge == ESimCruiseRoadEdge::Kerb && (Band == 1 || Band == 2)) Color.B = 1.f;
						FSimCruiseMeshData& Mesh = BandLayers[Band] == static_cast<uint8>(ESimCruiseTileLayer::Concrete) ? Concrete : Asphalt;
						if (bLast || bFace)
						{
							// Per-face normals along a vertical band keep the strip readable.
							EmitStrip(Mesh, BandRows, Reference, Color, false);
						}
						else
						{
							EmitStrip(Mesh, BandRows, Reference, Color, true);
						}
					}
					// End caps where a curb run stops (junction cuts, a change of treatment).
					if (Edge == ESimCruiseRoadEdge::Kerb)
					{
						for (int32 End = 0; End < 2; ++End)
						{
							const int32 Row = End == 0 ? 0 : Run.Num() - 1;
							const FVector2d T = Run[Row].T * (End == 0 ? -1.0 : 1.0);
							const FVector Facing(T.X, T.Y, 0.0);
							const double Bottom = PointAt(Row, NP - 1).Z;
							for (int32 p = 0; p + 2 < NP; ++p)
							{
								const FVector A = PointAt(Row, p), Bp = PointAt(Row, p + 1);
								const int32 Ia = Concrete.Add(A, Facing), Ib = Concrete.Add(Bp, Facing);
								const int32 Ic = Concrete.Add(FVector(Bp.X, Bp.Y, Bottom), Facing), Id = Concrete.Add(FVector(A.X, A.Y, Bottom), Facing);
								Concrete.Quad(Ia, Ib, Ic, Id, Facing);
							}
						}
					}
				}
			}
		}

		// ---------------------------------------------------------------- buildings, palms, street lights
		// Recipes by region on footprints and heights from the tile (OpenStreetMap / Overture),
		// palms on the lawns where coconut palms grow, lights behind the curbs (SimCruiseTileDetail).
		{
			double Lon = 0.0, Lat = 0.0;
			SimCruiseGeo::TileToLonLat(FVector2d(Data.Id.X + 0.5, Data.Id.Y + 0.5), Lon, Lat);
			auto Sample = [&](const FCoverage& Coverage, const FVector2d& P)
			{
				const int32 X = FMath::Clamp(FMath::FloorToInt(P.X / Side * Coverage.Size), 0, Coverage.Size - 1);
				const int32 Y = FMath::Clamp(FMath::FloorToInt(P.Y / Side * Coverage.Size), 0, Coverage.Size - 1);
				return Coverage.Bits[Y * Coverage.Size + X] != 0;
			};
			SimCruiseTileDetail::FContext Context{ Data, B, Side, Lat, Lon,
				[&](const FVector2d& P) { return Sample(Veg, P); },
				[&](const FVector2d& P) { return Sample(Paved, P); },
				[&](const FVector2d& P) { return Sample(Water, P); } };
			SimCruiseTileDetail::BuildBuildings(Context);
			SimCruiseTileDetail::BuildPalms(Context);
			SimCruiseTileDetail::BuildStreetLights(Context);
		}

		// ---------------------------------------------------------------- markings
		{
			FSimCruiseMeshData& Paint = B.Layer(ESimCruiseTileLayer::Paint);
			const FLinearColor White(0.62f, 0.62f, 0.60f, 1.f);
			const FLinearColor Yellow(0.70f, 0.48f, 0.08f, 1.f);
			constexpr double DashM = 3.05, PeriodM = 12.2, LineM = 0.12, WideM = 0.15, DoubleGapM = 0.1;
			auto LaneWidth = [](const FString& Class) { return SimCruiseLanes::LaneWidthM(Class); };
			// A painted line: lateral offset and width, and, for dashes, the painted
			// length in each period (MUTCD: 10 ft dashes in 40 ft; parking bay ticks of
			// 4 in every 22 ft stall).
			struct FLine { double L; double Width; bool bDashed; bool bYellow; double Dash; double Period; };
			auto Ln = [](double L, double Width, bool bDashed, bool bYellow, double Dash = 3.05, double Period = 12.2) { return FLine{ L, Width, bDashed, bYellow, Dash, Period }; }; // DashM, PeriodM
			constexpr double StallM = 6.7, TickM = 0.1, TickLengthM = 0.6;
			auto Kerbed = [](const FSimCruiseRibbon& R, int32 SideIndex, int32 i)
			{
				return R.Sides[SideIndex].Edge.IsValidIndex(i) && R.Sides[SideIndex].Edge[i] != ESimCruiseRoadEdge::Rural;
			};
			for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
			{
				const FSimCruiseRibbon& Ribbon = Roads.Ribbons[r];
				if (!Ribbon.bMajor || Ribbon.St.Num() < 2) continue;
				const bool bArterial = Ribbon.Class == TEXT("motorway") || Ribbon.Class == TEXT("trunk") || Ribbon.Class == TEXT("primary");
				const double Lane = LaneWidth(Ribbon.Class);
				// Dashes anchored to the world along a canonical direction, so a straight
				// road's dashes run on across the seam whichever way each tile chained it.
				const FVector2d T0 = Ribbon.St[0].T;
				const double Canon = (T0.X > 1e-6 || (FMath::Abs(T0.X) <= 1e-6 && T0.Y > 0.0)) ? 1.0 : -1.0;
				const FVector2d World0(static_cast<double>(Data.Id.X) * Side + Ribbon.St[0].C.X, static_cast<double>(Data.Id.Y) * Side + Ribbon.St[0].C.Y);
				const double Anchor = FVector2d::DotProduct(World0, T0) * Canon;
				auto Phase = [&](double S) { return Anchor + Canon * (S - Ribbon.St[0].S); };
				auto LinesAt = [&](int32 i, TArray<FLine>& Out)
				{
					const FSimCruiseRibbonStation& St = Ribbon.St[i];
					Out.Reset();
					const SimCruiseLanes::FParking Park = SimCruiseLanes::Parking(Ribbon.Class, St.HalfW, St.Travel, Kerbed(Ribbon, 0, i), Kerbed(Ribbon, 1, i));
					const double Travelled = 2.0 * St.HalfW;
					if (St.Travel == 0)
					{
						if (Travelled < 4.9) return;
						const double Half = St.HalfW - Park.Right;
						const int32 PerDir = FMath::Clamp(FMath::FloorToInt((Half - 0.6) / Lane), 1, 3);
						const double Lw = FMath::Min(Lane, (Half - 0.3) / PerDir);
						for (const double S : { -1.0, 1.0 })
						{
							Out.Add(Ln(S * (0.5 * DoubleGapM + 0.5 * LineM), LineM, false, true));
							for (int32 k = 1; k < PerDir; ++k) Out.Add(Ln(S * k * Lw, LineM, true, false));
							if (bArterial) Out.Add(Ln(S * FMath::Min(PerDir * Lw + 0.1, St.HalfW - 0.2), WideM, false, false));
						}
					}
					else
					{
						const double LeftP = St.Travel > 0 ? Park.Left : Park.Right;
						const double RightP = St.Travel > 0 ? Park.Right : Park.Left;
						const double Width = Travelled - LeftP - RightP;
						const int32 Lanes = Width >= 2.0 * 2.75 + 0.6 ? 2 : 1;
						const double Lw = FMath::Min(Lane, (Width - 0.8) / Lanes);
						const double Total = Lanes * Lw;
						const double Centre = 0.5 * (LeftP - RightP);                 // driver's frame
						const double LeftL = St.Travel * (Centre - 0.5 * Total);       // the driver's left edge, station frame
						for (int32 k = 1; k < Lanes; ++k) Out.Add(Ln(LeftL + St.Travel * k * Lw, LineM, true, false));
						if (bArterial)
						{
							Out.Add(Ln(LeftL, WideM, false, true));
							Out.Add(Ln(LeftL + St.Travel * Total, WideM, false, false));
						}
					}
					// The parking lane's line, and the stall ticks reaching from it toward the curb.
					for (const double Sgn : { -1.0, 1.0 })
					{
						const double P = Sgn < 0.0 ? Park.Left : Park.Right;
						if (P <= 0.0) continue;
						const double L = Sgn * (St.HalfW - P);
						Out.Add(Ln(L, LineM, false, false));
						Out.Add(Ln(L + Sgn * (0.5 * TickLengthM + 0.5 * LineM), TickLengthM, true, false, TickM, StallM));
					}
				};
				// A stripe stops where a road of higher rank, or one passing through, crosses it.
				TArray<uint8> Cut; Cut.Init(0, Ribbon.St.Num());
				for (int32 i = 0; i < Ribbon.St.Num(); ++i)
				{
					const FSimCruiseRibbonStation& St = Ribbon.St[i];
					for (const double L : { -0.5 * St.HalfW, 0.0, 0.5 * St.HalfW })
					{
						FSimCruiseRoadHit Hit;
						if (!Roads.FindCarriageway(St.C + St.R * L, Hit, r, 0.0, Ribbon.Rank)) continue;
						if (Hit.Ribbon < 0) { Cut[i] = 1; break; } // a turnaround pad
						const FSimCruiseRibbon& Other = Roads.Ribbons[Hit.Ribbon];
						if (Other.Rank > Ribbon.Rank) { Cut[i] = 1; break; }
						const double FromStart = Other.St[Hit.Station].S + Hit.T * (Other.St[Hit.Station + 1].S - Other.St[Hit.Station].S) - Other.St[0].S;
						const double FromEnd = Other.Length() - FromStart;
						const double Stem = St.HalfW + 1.5;
						const bool bStemEnd = (!Other.bStartOnBoundary && FromStart < Stem) || (!Other.bEndOnBoundary && FromEnd < Stem);
						if (!bStemEnd) { Cut[i] = 1; break; }
					}
				}
				// No parking within 12 m of a crossing (MUTCD: 20 ft from a crosswalk, which
				// stands about 4 m off the crossing road's edge).
				TArray<uint8> NearCut; NearCut.Init(0, Ribbon.St.Num());
				for (int32 i = 0; i < Ribbon.St.Num(); ++i)
				{
					if (!Cut[i]) continue;
					for (int32 j = 0; j < Ribbon.St.Num(); ++j)
					{
						if (FMath::Abs(Ribbon.St[j].S - Ribbon.St[i].S) < 12.0) NearCut[j] = 1;
					}
				}
				TArray<FLine> LA, LB;
				for (int32 i = 0; i + 1 < Ribbon.St.Num(); ++i)
				{
					if (Cut[i] || Cut[i + 1]) continue;
					const FSimCruiseRibbonStation& A = Ribbon.St[i];
					const FSimCruiseRibbonStation& Bs = Ribbon.St[i + 1];
					if (A.Travel != Bs.Travel) continue;
					LinesAt(i, LA);
					LinesAt(i + 1, LB);
					if (LA.Num() != LB.Num()) continue;

					// Parked cars: one stall between each pair of ticks, a little over half of
					// them taken (a stable hash of the stall's world position), a car 25 cm off
					// the curb facing the traffic on its side.
					if (!NearCut[i] && !NearCut[i + 1])
					{
						const SimCruiseLanes::FParking Park = SimCruiseLanes::Parking(Ribbon.Class, A.HalfW, A.Travel, Kerbed(Ribbon, 0, i), Kerbed(Ribbon, 1, i));
						const double U0 = Phase(A.S), U1 = Phase(Bs.S);
						const double Lo = FMath::Min(U0, U1), Hi = FMath::Max(U0, U1);
						const double Mid = TickM + 0.5 * (StallM - TickM);
						for (double U = FMath::CeilToDouble((Lo - Mid) / StallM) * StallM + Mid; U < Hi && FMath::Abs(U1 - U0) > 1e-6; U += StallM)
						{
							if (U < Lo) continue;
							const double T = (U - U0) / (U1 - U0);
							for (const double Sgn : { -1.0, 1.0 })
							{
								const double P = Sgn < 0.0 ? Park.Left : Park.Right;
								if (P < 2.0) continue;
								const FVector2d Cc = FMath::Lerp(A.C, Bs.C, T);
								const FVector2d Rr = FMath::Lerp(A.R, Bs.R, T).GetSafeNormal();
								const FVector2d Tt = FMath::Lerp(A.T, Bs.T, T).GetSafeNormal();
								const double HalfW = FMath::Lerp(A.HalfW, Bs.HalfW, T);
								const FVector2d WorldM(static_cast<double>(Data.Id.X) * Side + Cc.X, static_cast<double>(Data.Id.Y) * Side + Cc.Y);
								uint32 Hash = HashCombine(GetTypeHash(FMath::RoundToInt64(WorldM.X * 2.0)), GetTypeHash(FMath::RoundToInt64(WorldM.Y * 2.0)));
								Hash = HashCombine(Hash, GetTypeHash(static_cast<int32>(Sgn)));
								Hash = Hash * 2654435761u ^ (Hash >> 15);
								if ((Hash % 100u) >= 58u) continue;
								const double OffCurb = 0.25 + 0.15 * static_cast<double>((Hash >> 8) & 0xFF) / 255.0;
								const double L = Sgn * (HalfW - OffCurb - 0.9);
								const double Hgt = FMath::Lerp(Ribbon.CarriagewayHeight(i, L), Ribbon.CarriagewayHeight(i + 1, L), T);
								const FVector2d Heading = Tt * (A.Travel != 0 ? static_cast<double>(A.Travel) : Sgn);
								FSimCruiseParkedCar Car;
								Car.LocationCm = Cm(Cc + Rr * L, Hgt);
								Car.YawDeg = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Heading.Y, Heading.X)) + (static_cast<double>((Hash >> 16) & 0xFF) / 255.0 - 0.5) * 3.0);
								Car.Pick = static_cast<uint8>(Hash >> 24);
								Car.Colour = static_cast<uint8>(Hash >> 4);
								B.ParkedCars.Add(Car);
							}
						}
					}
					for (int32 l = 0; l < LA.Num(); ++l)
					{
						const FLine& Line = LA[l];
						// Pieces of this segment that are painted: [t0, t1] in 0..1.
						TArray<FVector2d, TInlineAllocator<4>> Pieces;
						if (!Line.bDashed)
						{
							Pieces.Add(FVector2d(0.0, 1.0));
						}
						else
						{
							const double U0 = Phase(A.S), U1 = Phase(Bs.S);
							const double Lo = FMath::Min(U0, U1), Hi = FMath::Max(U0, U1);
							const double Span = FMath::Max(Hi - Lo, 1e-9);
							for (double Start = FMath::FloorToDouble(Lo / Line.Period) * Line.Period; Start < Hi; Start += Line.Period)
							{
								const double D0 = FMath::Max(Start, Lo), D1 = FMath::Min(Start + Line.Dash, Hi);
								if (D1 <= D0) continue;
								double T0p = (D0 - Lo) / Span, T1p = (D1 - Lo) / Span;
								if (U1 < U0) { const double Tmp = 1.0 - T1p; T1p = 1.0 - T0p; T0p = Tmp; }
								Pieces.Add(FVector2d(T0p, T1p));
							}
						}
						for (const FVector2d& Piece : Pieces)
						{
							FVector Q[4];
							for (int32 e = 0; e < 2; ++e)
							{
								const double T = e == 0 ? Piece.X : Piece.Y;
								const FVector2d C = FMath::Lerp(A.C, Bs.C, T);
								const FVector2d R = FMath::Lerp(A.R, Bs.R, T).GetSafeNormal();
								const double L = FMath::Lerp(Line.L, LB[l].L, T);
								for (int32 s = 0; s < 2; ++s)
								{
									const double Ls = L + (s == 0 ? -0.5 : 0.5) * Line.Width;
									const double H = FMath::Lerp(Ribbon.CarriagewayHeight(i, Ls), Ribbon.CarriagewayHeight(i + 1, Ls), T) + MarkLiftM;
									Q[e * 2 + s] = Cm(C + R * Ls, H);
								}
							}
							const FLinearColor& Color = Line.bYellow ? Yellow : White;
							const int32 I0 = Paint.Add(Q[0], FVector::UpVector, Color), I1 = Paint.Add(Q[1], FVector::UpVector, Color);
							const int32 I2 = Paint.Add(Q[3], FVector::UpVector, Color), I3 = Paint.Add(Q[2], FVector::UpVector, Color);
							Paint.Quad(I0, I1, I2, I3, FVector::UpVector);
							++B.MarkingQuads;
						}
					}
				}

				// Continental crosswalks on each approach to a junction in town (curbed
				// streets): 24 in bars with 24 in gaps across the carriageway, 10 ft deep,
				// set back 1 m from the crossing road (MUTCD 3B.18, FHWA high-visibility).
				auto PointAt = [&](double S, double L, FVector& OutCm)
				{
					for (int32 k = 0; k + 1 < Ribbon.St.Num(); ++k)
					{
						const FSimCruiseRibbonStation& P0 = Ribbon.St[k];
						const FSimCruiseRibbonStation& P1 = Ribbon.St[k + 1];
						if (S < P0.S || S > P1.S) continue;
						const double T = (S - P0.S) / FMath::Max(P1.S - P0.S, 1e-6);
						const FVector2d C = FMath::Lerp(P0.C, P1.C, T);
						const FVector2d R = FMath::Lerp(P0.R, P1.R, T).GetSafeNormal();
						const double H = FMath::Lerp(Ribbon.CarriagewayHeight(k, L), Ribbon.CarriagewayHeight(k + 1, L), T) + MarkLiftM;
						OutCm = Cm(C + R * L, H);
						return true;
					}
					return false;
				};
				const bool bTownClass = !bArterial || Ribbon.Class == TEXT("primary");
				for (int32 i = 0; bTownClass && i + 1 < Ribbon.St.Num(); ++i)
				{
					if (Cut[i] == Cut[i + 1]) continue;
					const bool bBefore = Cut[i + 1] != 0;            // the junction starts at i + 1
					const int32 Edge = bBefore ? i + 1 : i;
					if (!(Kerbed(Ribbon, 0, Edge) || Kerbed(Ribbon, 1, Edge))) continue;
					const double SCut = Ribbon.St[Edge].S;
					const double Dir = bBefore ? -1.0 : 1.0;
					const double S0 = SCut + Dir * 1.0, S1 = SCut + Dir * 4.05;
					if (FMath::Min(S0, S1) < Ribbon.St[0].S || FMath::Max(S0, S1) > Ribbon.St.Last().S) continue;
					const double HalfW = Ribbon.St[Edge].HalfW;
					for (double L = -HalfW + 0.35; L + 0.6 <= HalfW - 0.2; L += 1.2)
					{
						FVector Q[4];
						if (!PointAt(S0, L, Q[0]) || !PointAt(S0, L + 0.6, Q[1]) || !PointAt(S1, L + 0.6, Q[2]) || !PointAt(S1, L, Q[3])) continue;
						const int32 I0 = Paint.Add(Q[0], FVector::UpVector, White), I1 = Paint.Add(Q[1], FVector::UpVector, White);
						const int32 I2 = Paint.Add(Q[2], FVector::UpVector, White), I3 = Paint.Add(Q[3], FVector::UpVector, White);
						Paint.Quad(I0, I1, I2, I3, FVector::UpVector);
						++B.MarkingQuads;
					}
				}
			}
		}

		// Stop lines and stop signs where an approach stops (the rule the traffic follows):
		// an 18 in white bar across the approach lanes 4 ft before the crosswalk, and an
		// R1-1 sign on the driver's right at the line.
		{
			FSimCruiseMeshData& Paint = B.Layer(ESimCruiseTileLayer::Paint);
			FSimCruiseMeshData& Props = B.Layer(ESimCruiseTileLayer::Props);
			const FLinearColor White(0.62f, 0.62f, 0.60f, 1.f);
			auto Kerbed = [](const FSimCruiseRibbon& R, int32 SideIndex, int32 i)
			{
				return R.Sides[SideIndex].Edge.IsValidIndex(i) && R.Sides[SideIndex].Edge[i] != ESimCruiseRoadEdge::Rural;
			};
			for (int32 r = 0; r < Roads.Ribbons.Num(); ++r)
			{
				const FSimCruiseRibbon& Ribbon = Roads.Ribbons[r];
				if (Ribbon.bService || Ribbon.Class == TEXT("service") || Ribbon.St.Num() < 3) continue;
				// Where another public carriageway crosses this one's centre line.
				TArray<uint8> Inside; Inside.Init(0, Ribbon.St.Num());
				TArray<int32> CrossRank; CrossRank.Init(MIN_int32, Ribbon.St.Num());
				for (int32 i = 0; i < Ribbon.St.Num(); ++i)
				{
					TArray<TPair<int32, int32>> Segs;
					Roads.GatherSegments(Ribbon.St[i].C, 30.0, Segs);
					for (const TPair<int32, int32>& Seg : Segs)
					{
						if (Seg.Key == r) continue;
						const FSimCruiseRibbon& Other = Roads.Ribbons[Seg.Key];
						if (Other.bService || Other.Class == TEXT("service") || !Other.St.IsValidIndex(Seg.Value + 1)) continue;
						const FVector2d A = Other.St[Seg.Value].C, Bb = Other.St[Seg.Value + 1].C;
						const double Tt = FMath::Clamp(FVector2d::DotProduct(Ribbon.St[i].C - A, Bb - A) / FMath::Max((Bb - A).SizeSquared(), 1e-9), 0.0, 1.0);
						if (FVector2d::Distance(Ribbon.St[i].C, FMath::Lerp(A, Bb, Tt)) > Other.St[Seg.Value].HalfW) continue;
						// Not this street carrying on in line with it.
						if (FMath::Abs(FVector2d::DotProduct(Other.St[Seg.Value].T, Ribbon.St[i].T)) > 0.94 && Other.Name == Ribbon.Name) continue;
						Inside[i] = 1;
						CrossRank[i] = FMath::Max(CrossRank[i], Other.Rank);
					}
				}
				for (int32 i = 0; i + 1 < Ribbon.St.Num(); ++i)
				{
					if (Inside[i] == Inside[i + 1]) continue;
					const bool bEntering = Inside[i + 1] != 0;      // travelling +S enters at i + 1
					const int32 Edge = bEntering ? i + 1 : i;
					const int32 Approach = bEntering ? 1 : -1;
					const FSimCruiseRibbonStation& E = Ribbon.St[Edge];
					if (E.Travel != 0 && E.Travel != Approach) continue;
					if (!SimCruiseLanes::ApproachStops(Ribbon.Rank, Ribbon.bMajor, CrossRank[Edge])) continue;
					const double SLine = E.S - Approach * SimCruiseLanes::StopLineSetbackM;
					const double SBack = SLine - Approach * 0.45;
					if (FMath::Min(SLine, SBack) < Ribbon.St[0].S + 1.0 || FMath::Max(SLine, SBack) > Ribbon.St.Last().S - 1.0) continue;
					// Station frame at the line.
					int32 k = 0;
					while (k + 2 < Ribbon.St.Num() && Ribbon.St[k + 1].S < SLine) ++k;
					const FSimCruiseRibbonStation& P0 = Ribbon.St[k];
					const FSimCruiseRibbonStation& P1 = Ribbon.St[k + 1];
					const double T = FMath::Clamp((SLine - P0.S) / FMath::Max(P1.S - P0.S, 1e-6), 0.0, 1.0);
					const double HalfW = FMath::Lerp(P0.HalfW, P1.HalfW, T);
					const SimCruiseLanes::FParking Park = SimCruiseLanes::Parking(Ribbon.Class, HalfW, E.Travel, Kerbed(Ribbon, 0, k), Kerbed(Ribbon, 1, k));
					// The approach lanes, in station frame (R): two way, the half on the driver's right.
					double L0, L1;
					if (E.Travel == 0)
					{
						L0 = Approach > 0 ? 0.15 : -(HalfW - Park.Left - 0.1);
						L1 = Approach > 0 ? HalfW - Park.Right - 0.1 : -0.15;
					}
					else
					{
						L0 = -(HalfW - Park.Left - 0.1);
						L1 = HalfW - Park.Right - 0.1;
					}
					if (L1 - L0 < 2.0) continue;
					auto Point = [&](double S, double L)
					{
						int32 j = 0;
						while (j + 2 < Ribbon.St.Num() && Ribbon.St[j + 1].S < S) ++j;
						const double U = FMath::Clamp((S - Ribbon.St[j].S) / FMath::Max(Ribbon.St[j + 1].S - Ribbon.St[j].S, 1e-6), 0.0, 1.0);
						const FVector2d C = FMath::Lerp(Ribbon.St[j].C, Ribbon.St[j + 1].C, U);
						const FVector2d Rn = FMath::Lerp(Ribbon.St[j].R, Ribbon.St[j + 1].R, U).GetSafeNormal();
						const double Hh = FMath::Lerp(Ribbon.CarriagewayHeight(j, L), Ribbon.CarriagewayHeight(j + 1, L), U) + MarkLiftM;
						return Cm(C + Rn * L, Hh);
					};
					const int32 I0 = Paint.Add(Point(SBack, L0), FVector::UpVector, White), I1 = Paint.Add(Point(SBack, L1), FVector::UpVector, White);
					const int32 I2 = Paint.Add(Point(SLine, L1), FVector::UpVector, White), I3 = Paint.Add(Point(SLine, L0), FVector::UpVector, White);
					Paint.Quad(I0, I1, I2, I3, FVector::UpVector);
					++B.MarkingQuads;
					// The sign on the driver's right, behind the curb.
					const double Sgn = static_cast<double>(Approach); // the driver's right is +R travelling +S
					const int32 SideIndex = Sgn > 0.0 ? 1 : 0;
					const bool bKerb = Ribbon.Sides[SideIndex].Edge.IsValidIndex(k) && Ribbon.Sides[SideIndex].Edge[k] == ESimCruiseRoadEdge::Kerb;
					const FVector2d C = FMath::Lerp(P0.C, P1.C, T);
					const FVector2d Rn = FMath::Lerp(P0.R, P1.R, T).GetSafeNormal();
					const FVector2d Tn = FMath::Lerp(P0.T, P1.T, T).GetSafeNormal();
					const double SignOut = HalfW + (bKerb ? SimCruiseRoadSection::GutterM + SimCruiseRoadSection::CurbBatterM + SimCruiseRoadSection::CurbTopM + 0.45 : SimCruiseRoadSection::TownEdgeM + 0.4);
					const FVector2d Base2 = C + Rn * (Sgn * SignOut);
					const double Ground = FMath::Lerp(P0.Height(HalfW * Sgn), P1.Height(HalfW * Sgn), T) + (bKerb ? -SimCruiseRoadSection::GutterDropM + SimCruiseRoadSection::CurbFaceM : 0.0);
					SimCruiseTileDetail::AddStopSign(Props, Base2, Ground, -Tn * Sgn);
					++B.StopSigns;
				}
			}
		}

		// The drawn mesh's GPU buffers, filled here so the game thread only uploads them.
		if (FApp::CanEverRender())
		{
			TArray<const FSimCruiseMeshData*> Parts;
			for (int32 i = 0; i < static_cast<int32>(ESimCruiseTileLayer::Count); ++i)
			{
				if (!B.Layers[i].IsEmpty())
				{
					Parts.Add(&B.Layers[i]);
					B.RenderSlots.Add(i);
				}
			}
			B.RenderData = MakeRenderData(Parts);
		}

		B.BuildMs = (FPlatformTime::Seconds() - Started) * 1000.0;
		return Out;
	}
}
