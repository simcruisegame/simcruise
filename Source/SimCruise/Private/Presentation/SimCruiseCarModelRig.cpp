// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarModelRig.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseGltfModel.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Vehicle/CarDefinition.h"

// The rig works in the Godot vehicle frame (X right, Y up, Z rearward,
// metres, right-handed) so it can follow game/ModelRig.gd and the
// cars/rig-infra steering-wheel search step by step; Unreal axes are applied
// only when the parts are written out.

FSimCruiseRigInputs FSimCruiseRigInputs::FromCar(const FSimCruiseCarDefinition& Car)
{
	FSimCruiseRigInputs Inputs;
	Inputs.WheelbaseM = Car.WheelbaseM;
	Inputs.FrontTrackM = Car.FrontTrackM;
	Inputs.RearTrackM = Car.RearTrackM;
	Inputs.TireRadiusM = Car.TireFreeRadiusM();
	Inputs.TireWidthM = Car.TireSectionWidthM;
	Inputs.HubHeightM = Car.SuspensionMountHeightM - Car.StaticSuspensionLengthM;
	// driver_eye is stored in the Godot vehicle frame.
	Inputs.DriverEyeM = FVector(-Car.DriverEyeM.Z, Car.DriverEyeM.X, Car.DriverEyeM.Y);
	return Inputs;
}

int32 FSimCruiseRigPart::NumTriangles() const
{
	int32 Count = 0;
	for (const FSimCruiseRigSection& Section : Sections)
	{
		Count += Section.NumTriangles();
	}
	return Count;
}

FBox FSimCruiseRigPart::LocalBounds() const
{
	FBox Box(ForceInit);
	for (const FSimCruiseRigSection& Section : Sections)
	{
		for (const FVector3f& P : Section.Positions)
		{
			Box += FVector(P);
		}
	}
	return Box;
}

int64 FSimCruiseRiggedCar::NumTriangles() const
{
	int64 Count = Body.NumTriangles() + SteeringWheel.NumTriangles();
	for (int32 I = 0; I < 4; ++I)
	{
		Count += Wheels[I].NumTriangles() + Uprights[I].NumTriangles();
	}
	return Count;
}

namespace SimCruiseRigPrivate
{
	// Destinations of a triangle.
	enum : int32
	{
		DestBody = 0,
		DestWheel0 = 1,   // 1..4
		DestUpright0 = 5, // 5..8
		DestSteering = 9,
		DestCount = 10
	};

	const TCHAR* WheelKeys[4] = {TEXT("wheel_fl"), TEXT("wheel_fr"), TEXT("wheel_rl"), TEXT("wheel_rr")};

	// game/ModelRig.gd WHEEL_NAMES.
	const TArray<TArray<FString>>& WheelAliases()
	{
		static const TArray<TArray<FString>> Aliases = {
			{TEXT("front left wheel"), TEXT("front_left_wheel"), TEXT("wheel_fl"), TEXT("fl_wheel"), TEXT("wheel.fl"), TEXT("wheelfl"), TEXT("wheel_lf")},
			{TEXT("front right wheel"), TEXT("front_right_wheel"), TEXT("wheel_fr"), TEXT("fr_wheel"), TEXT("wheel.fr"), TEXT("wheelfr"), TEXT("wheel_rf")},
			{TEXT("rear left wheel"), TEXT("rear_left_wheel"), TEXT("wheel_rl"), TEXT("rl_wheel"), TEXT("wheel.rl"), TEXT("wheelrl"), TEXT("wheel_lr")},
			{TEXT("rear right wheel"), TEXT("rear_right_wheel"), TEXT("wheel_rr"), TEXT("rr_wheel"), TEXT("wheel.rr"), TEXT("wheelrr")},
		};
		return Aliases;
	}

	int32 NamedWheel(const FString& Label)
	{
		const TArray<TArray<FString>>& Aliases = WheelAliases();
		for (int32 W = 0; W < 4; ++W)
		{
			for (const FString& Alias : Aliases[W])
			{
				if (Label.Contains(Alias))
				{
					return W;
				}
			}
		}
		return INDEX_NONE;
	}

	struct FJob
	{
		int32 Primitive = INDEX_NONE;
		int32 MaterialIndex = INDEX_NONE;
		ESimCruiseCarMaterialRole Role = ESimCruiseCarMaterialRole::Authored;
		FString MaterialName;
		FString Label;
		int32 NamedWheel = INDEX_NONE;
		bool bMirrored = false;
		TArray<FVector> Pos;
		TArray<FVector> Nrm;
		TArray<FVector4> Tan;
		/** Triangle corner lists per destination. */
		TArray<uint32> Dest[DestCount];
	};

	struct FContext
	{
		const FSimCruiseGltfModel* Model = nullptr;
		const FSimCruiseCarPresentationProfile* Profile = nullptr;
		FSimCruiseRigInputs Inputs;
		TArray<FJob> Jobs;
		FSimCruiseRiggedCar* Out = nullptr;

		void Note(const FString& Line)
		{
			Out->Log.Add(Line);
		}
	};

	FString V3(const FVector& V)
	{
		return FString::Printf(TEXT("(%.3f, %.3f, %.3f)"), V.X, V.Y, V.Z);
	}

	/** A triangle of a job's destination list: job index and offset of its first corner. */
	struct FTriRef
	{
		int32 Job;
		int32 Offset;
	};

	struct FComponent
	{
		TArray<FTriRef> Tris;
		TArray<FVector> Points;
		bool bClipped = false;
		FVector Centroid = FVector::ZeroVector;
	};

	struct FUnionFind
	{
		TArray<int32> Parent;
		int32 Add() { return Parent.Add(Parent.Num()); }
		int32 Find(int32 I)
		{
			while (Parent[I] != I)
			{
				Parent[I] = Parent[Parent[I]];
				I = Parent[I];
			}
			return I;
		}
		void Union(int32 A, int32 B)
		{
			A = Find(A);
			B = Find(B);
			if (A != B)
			{
				Parent[FMath::Max(A, B)] = FMath::Min(A, B);
			}
		}
	};

	/**
	 * Connected parts of the given jobs' triangles in destination Source lying
	 * wholly in a region, with vertices welded by position at 0.5 mm so UV
	 * seams do not split a part. A part touched by a triangle that leaves the
	 * region is marked clipped (cars/rig-infra connected_parts).
	 */
	template <typename FInRegion>
	TArray<FComponent> ConnectedParts(FContext& Context, const TArray<int32>& Which, int32 Source, FInRegion&& InRegion)
	{
		TMap<FIntVector, int32> Weld;
		FUnionFind Sets;
		TArray<FVector> Positions;
		TArray<FTriRef> Tris;
		TArray<int32> TriFirstId;
		TSet<int32> Clipped;
		for (int32 J : Which)
		{
			FJob& Job = Context.Jobs[J];
			const TArray<uint32>& List = Job.Dest[Source];
			TMap<uint32, int32> Local;
			for (int32 K = 0; K + 2 < List.Num(); K += 3)
			{
				int32 Ids[3];
				int32 Outside = 0;
				for (int32 M = 0; M < 3; ++M)
				{
					const uint32 VI = List[K + M];
					int32* Found = Local.Find(VI);
					int32 Id;
					if (Found)
					{
						Id = *Found;
					}
					else
					{
						const FVector& W = Job.Pos[VI];
						if (InRegion(W))
						{
							const FIntVector Key(FMath::RoundToInt(W.X * 2000.0), FMath::RoundToInt(W.Y * 2000.0), FMath::RoundToInt(W.Z * 2000.0));
							if (int32* Existing = Weld.Find(Key))
							{
								Id = *Existing;
							}
							else
							{
								Id = Positions.Add(W);
								Sets.Add();
								Weld.Add(Key, Id);
							}
						}
						else
						{
							Id = -2;
						}
						Local.Add(VI, Id);
					}
					Ids[M] = Id;
					if (Id == -2)
					{
						++Outside;
					}
				}
				if (Outside == 3)
				{
					continue;
				}
				if (Outside > 0)
				{
					for (int32 Id : Ids)
					{
						if (Id >= 0) Clipped.Add(Id);
					}
					continue;
				}
				Tris.Add({J, K});
				TriFirstId.Add(Ids[0]);
				Sets.Union(Ids[0], Ids[1]);
				Sets.Union(Ids[0], Ids[2]);
			}
		}
		TMap<int32, int32> RootToComponent;
		TArray<FComponent> Components;
		for (int32 T = 0; T < Tris.Num(); ++T)
		{
			const int32 Root = Sets.Find(TriFirstId[T]);
			int32* Index = RootToComponent.Find(Root);
			if (!Index)
			{
				Index = &RootToComponent.Add(Root, Components.AddDefaulted());
			}
			Components[*Index].Tris.Add(Tris[T]);
		}
		for (int32 Id = 0; Id < Positions.Num(); ++Id)
		{
			if (const int32* Index = RootToComponent.Find(Sets.Find(Id)))
			{
				FComponent& Component = Components[*Index];
				Component.Points.Add(Positions[Id]);
				Component.Centroid += Positions[Id];
				if (Clipped.Contains(Id))
				{
					Component.bClipped = true;
				}
			}
		}
		for (FComponent& Component : Components)
		{
			if (Component.Points.Num() > 0)
			{
				Component.Centroid /= Component.Points.Num();
			}
		}
		return Components;
	}

	/** Move triangles out of Source, each to its own target, in one pass (offsets stay valid). */
	void MoveTrisTo(FContext& Context, const TArray<TPair<FTriRef, int32>>& Moves, int32 Source)
	{
		TMap<int32, TArray<int8>> Targets;
		for (const TPair<FTriRef, int32>& Move : Moves)
		{
			TArray<int8>& PerTri = Targets.FindOrAdd(Move.Key.Job);
			if (PerTri.Num() == 0)
			{
				PerTri.Init(int8(-1), Context.Jobs[Move.Key.Job].Dest[Source].Num() / 3);
			}
			PerTri[Move.Key.Offset / 3] = int8(Move.Value);
		}
		for (TPair<int32, TArray<int8>>& Pair : Targets)
		{
			FJob& Job = Context.Jobs[Pair.Key];
			TArray<uint32> Keep;
			Keep.Reserve(Job.Dest[Source].Num());
			const TArray<uint32> List = MoveTemp(Job.Dest[Source]);
			for (int32 T = 0; T * 3 + 2 < List.Num(); ++T)
			{
				TArray<uint32>& To = Pair.Value[T] >= 0 ? Job.Dest[Pair.Value[T]] : Keep;
				To.Append({List[T * 3], List[T * 3 + 1], List[T * 3 + 2]});
			}
			Job.Dest[Source] = MoveTemp(Keep);
		}
	}

	void MoveTris(FContext& Context, const TArray<FTriRef>& Tris, int32 Source, int32 Target)
	{
		TArray<TPair<FTriRef, int32>> Moves;
		Moves.Reserve(Tris.Num());
		for (const FTriRef& Tri : Tris)
		{
			Moves.Emplace(Tri, Target);
		}
		MoveTrisTo(Context, Moves, Source);
	}

	/** Eigenvector of the smallest eigenvalue of a symmetric 3x3 matrix (Jacobi). */
	FVector SmallestAxis(double A[3][3])
	{
		double V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		for (int32 Sweep = 0; Sweep < 12; ++Sweep)
		{
			const int32 Pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
			for (const auto& PQ : Pairs)
			{
				const int32 P = PQ[0], Q = PQ[1];
				if (FMath::Abs(A[P][Q]) < 1e-12) continue;
				const double Theta = (A[Q][Q] - A[P][P]) / (2.0 * A[P][Q]);
				const double T = Theta != 0.0 ? FMath::Sign(Theta) / (FMath::Abs(Theta) + FMath::Sqrt(Theta * Theta + 1.0)) : 1.0;
				const double C = 1.0 / FMath::Sqrt(T * T + 1.0), S = T * C;
				for (int32 K = 0; K < 3; ++K)
				{
					const double AKP = A[K][P], AKQ = A[K][Q];
					A[K][P] = C * AKP - S * AKQ;
					A[K][Q] = S * AKP + C * AKQ;
				}
				for (int32 K = 0; K < 3; ++K)
				{
					const double APK = A[P][K], AQK = A[Q][K];
					A[P][K] = C * APK - S * AQK;
					A[Q][K] = S * APK + C * AQK;
				}
				for (int32 K = 0; K < 3; ++K)
				{
					const double VKP = V[K][P], VKQ = V[K][Q];
					V[K][P] = C * VKP - S * VKQ;
					V[K][Q] = S * VKP + C * VKQ;
				}
			}
		}
		int32 I = 0;
		for (int32 K = 1; K < 3; ++K)
		{
			if (A[K][K] < A[I][I]) I = K;
		}
		return FVector(V[0][I], V[1][I], V[2][I]).GetSafeNormal();
	}

	struct FRing
	{
		bool bValid = false;
		FVector Centre = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
		double Radius = 0.0;
		double Quality = 0.0;
		FString Why;
	};

	constexpr double RimRadiusMin = 0.12;
	constexpr double RimRadiusMax = 0.24;

	/** cars/rig-infra ring_of: a flat closed ring of steering-wheel size facing fore/aft. */
	FRing RingOf(const TArray<FVector>& Pts)
	{
		FRing Ring;
		if (Pts.Num() < 12) { Ring.Why = TEXT("few points"); return Ring; }
		FVector C = FVector::ZeroVector;
		for (const FVector& P : Pts) C += P;
		C /= Pts.Num();
		double Cov[3][3] = {};
		for (const FVector& P : Pts)
		{
			const FVector D = P - C;
			for (int32 I = 0; I < 3; ++I)
				for (int32 J = 0; J < 3; ++J)
					Cov[I][J] += D[I] * D[J];
		}
		const FVector N = SmallestAxis(Cov);
		Ring.Centre = C;
		if (FMath::Abs(N.Z) < 0.55) { Ring.Why = FString::Printf(TEXT("plane normal %s does not face fore/aft"), *N.ToString()); return Ring; }
		TArray<double> Radii;
		double Axial = 0.0;
		for (const FVector& P : Pts)
		{
			const FVector D = P - C;
			const double A = D | N;
			Axial += A * A;
			Radii.Add((D - N * A).Size());
		}
		Radii.Sort();
		const double R = Radii.Num() > 20 ? Radii[FMath::Max(0, int32(Radii.Num() * 0.95) - 1)] : Radii.Last();
		if (R < RimRadiusMin || R > RimRadiusMax) { Ring.Why = FString::Printf(TEXT("radius %.3f"), R); return Ring; }
		if (FMath::Sqrt(Axial / Pts.Num()) > FMath::Max(0.045, 0.4 * R)) { Ring.Why = FString::Printf(TEXT("thick %.3f (radius %.3f)"), FMath::Sqrt(Axial / Pts.Num()), R); return Ring; }
		const FVector Up = (FVector(0, 1, 0) - N * (N | FVector(0, 1, 0))).GetSafeNormal();
		const FVector V = N ^ Up;
		double Edge[16] = {};
		TArray<int32> Sector;
		TArray<double> FlatR;
		Sector.SetNum(Pts.Num());
		FlatR.SetNum(Pts.Num());
		for (int32 I = 0; I < Pts.Num(); ++I)
		{
			const FVector D = Pts[I] - C;
			const FVector Flat = D - N * (D | N);
			Sector[I] = int32(FMath::FloorToDouble((FMath::Atan2(Flat | V, Flat | Up) + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI * 16.0)) % 16;
			FlatR[I] = Flat.Size();
			Edge[Sector[I]] = FMath::Max(Edge[Sector[I]], FlatR[I]);
		}
		int32 Bins = 0, OnRim = 0;
		for (int32 B = 0; B < 16; ++B)
		{
			if (Edge[B] >= R * 0.6) ++Bins;
		}
		for (int32 I = 0; I < Pts.Num(); ++I)
		{
			if (Edge[Sector[I]] >= R * 0.6 && FlatR[I] >= Edge[Sector[I]] * 0.72) ++OnRim;
		}
		const double Share = double(OnRim) / Pts.Num();
		if (Share < (Bins == 16 ? 0.25 : 0.4) || Bins < 15) { Ring.Why = FString::Printf(TEXT("rim share %.2f over %d/16 sectors (radius %.3f)"), Share, Bins, R); return Ring; }
		Ring.bValid = true;
		Ring.Centre = C;
		Ring.Normal = N;
		Ring.Radius = R;
		Ring.Quality = Share * Bins / 16.0;
		return Ring;
	}

	/** +1 when the driver sits right of the centreline (right-hand drive), -1 when left (Godot frame, X right). */
	double DriverSide(const FVector& EyeGodot)
	{
		return EyeGodot.X > 0.0 ? 1.0 : -1.0;
	}

	FRing BestRing(const TArray<FComponent>& Parts, const FVector* Eye, TArray<FString>* Debug = nullptr)
	{
		FRing Best;
		double BestScore = 0.0;
		for (const FComponent& Part : Parts)
		{
			if (Part.Points.Num() < 24 || Part.bClipped) continue;
			const FRing R = RingOf(Part.Points);
			if (!R.bValid)
			{
				if (Debug && Part.Points.Num() > 200)
				{
					Debug->Add(FString::Printf(TEXT("  ring rejected at %s (%d points): %s"), *Part.Centroid.ToString(), Part.Points.Num(), *R.Why));
				}
				continue;
			}
			// On the driver's side of the centreline (the eye's side: left-hand or right-hand drive) and in front of him.
			if (Eye && (R.Centre.X * DriverSide(*Eye) < 0.08 || FMath::Abs(R.Centre.X - Eye->X) > 0.45)) continue;
			const double Score = R.Quality * FMath::Pow(double(Part.Points.Num()), 0.25);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = R;
			}
		}
		return Best;
	}

	/**
	 * A rim modelled in several pieces (split at the spokes): fit a circle to
	 * every flat, arc-shaped piece facing fore/aft, group pieces that share a
	 * centre and radius, and accept a group that closes at least 13/16 of the
	 * circle.
	 */
	FRing RingFromArcs(const TArray<FComponent>& Parts, const FVector& Eye, TArray<FString>* Debug = nullptr)
	{
		struct FArcFit { FVector Centre; FVector Normal; double Radius; uint32 Sectors; int32 Points; };
		TArray<FArcFit> Fits;
		for (const FComponent& Part : Parts)
		{
			if (Part.bClipped || Part.Points.Num() < 24) continue;
			double Cov[3][3] = {};
			for (const FVector& P : Part.Points)
			{
				const FVector D = P - Part.Centroid;
				for (int32 I = 0; I < 3; ++I)
					for (int32 J = 0; J < 3; ++J)
						Cov[I][J] += D[I] * D[J];
			}
			const FVector N = SmallestAxis(Cov);
			if (Debug && Part.Points.Num() > 100)
			{
				Debug->Add(FString::Printf(TEXT("  arc candidate at %s (%d points), normal %s"), *Part.Centroid.ToString(), Part.Points.Num(), *N.ToString()));
			}
			if (FMath::Abs(N.Z) < 0.55) continue;
			const FVector U = (FVector(0, 1, 0) - N * (N | FVector(0, 1, 0))).GetSafeNormal();
			const FVector V = N ^ U;
			// Kasa circle fit in the plane: u^2 + v^2 + D u + E v + F = 0.
			double M[3][3] = {}, B[3] = {};
			double Axial = 0.0;
			for (const FVector& P : Part.Points)
			{
				const FVector D = P - Part.Centroid;
				const double X = D | U, Y = D | V, Z = -(X * X + Y * Y);
				const double Row[3] = {X, Y, 1.0};
				for (int32 I = 0; I < 3; ++I)
				{
					for (int32 J = 0; J < 3; ++J) M[I][J] += Row[I] * Row[J];
					B[I] += Row[I] * Z;
				}
				Axial += FMath::Square(D | N);
			}
			if (FMath::Sqrt(Axial / Part.Points.Num()) > 0.03) continue;
			const FMatrix44d Mat(FVector(M[0][0], M[0][1], M[0][2]), FVector(M[1][0], M[1][1], M[1][2]), FVector(M[2][0], M[2][1], M[2][2]), FVector::ZeroVector);
			if (FMath::Abs(Mat.Determinant()) < 1e-12) continue;
			const FVector Sol = Mat.Inverse().TransformVector(FVector(B[0], B[1], B[2]));
			const double Cu = -Sol.X * 0.5, Cv = -Sol.Y * 0.5;
			const double R2 = Cu * Cu + Cv * Cv - Sol.Z;
			if (R2 <= 0.0) continue;
			const double Radius = FMath::Sqrt(R2);
			if (Radius < RimRadiusMin || Radius > RimRadiusMax) continue;
			const FVector Centre = Part.Centroid + U * Cu + V * Cv;
			uint32 Sectors = 0;
			bool bArc = true;
			for (const FVector& P : Part.Points)
			{
				const FVector D = P - Centre;
				const FVector Flat = D - N * (D | N);
				if (FMath::Abs(Flat.Size() - Radius) > 0.03) { bArc = false; break; }
				Sectors |= 1u << (int32(FMath::FloorToDouble((FMath::Atan2(Flat | V, Flat | U) + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI * 16.0)) & 15);
			}
			if (Debug) Debug->Add(FString::Printf(TEXT("    circle centre %s radius %.3f, %s, %d sectors"), *Centre.ToString(), Radius, bArc ? TEXT("arc") : TEXT("not an arc"), FMath::CountBits(Sectors)));
			if (bArc) Fits.Add({Centre, N.Z < 0 ? -N : N, Radius, Sectors, Part.Points.Num()});
		}
		FRing Best;
		for (int32 I = 0; I < Fits.Num(); ++I)
		{
			uint32 Union = 0;
			FVector Centre = FVector::ZeroVector, Normal = FVector::ZeroVector;
			double Radius = 0.0, Weight = 0.0;
			for (const FArcFit& Other : Fits)
			{
				if ((Other.Centre - Fits[I].Centre).Size() < 0.04 && FMath::Abs(Other.Radius - Fits[I].Radius) < 0.02 && (Other.Normal | Fits[I].Normal) > 0.9)
				{
					Union |= Other.Sectors;
					Centre += Other.Centre * Other.Points;
					Normal += Other.Normal * Other.Points;
					Radius += Other.Radius * Other.Points;
					Weight += Other.Points;
				}
			}
			if (FMath::CountBits(Union) < 13 || Weight <= 0.0) continue;
			Centre /= Weight;
			if (Centre.X * DriverSide(Eye) < 0.08 || FMath::Abs(Centre.X - Eye.X) > 0.45) continue;
			if (!Best.bValid || Radius / Weight > Best.Radius)
			{
				Best.bValid = true;
				Best.Centre = Centre;
				Best.Normal = Normal.GetSafeNormal();
				Best.Radius = Radius / Weight;
				Best.Quality = FMath::CountBits(Union) / 16.0;
			}
		}
		return Best;
	}

	bool ReadManifestFlip(const FString& ModelPath, TOptional<bool>& OutFlip)
	{
		// game/art/traffic/manifest.json: the traffic lineup's checked front-facing flag.
		const FString ModelDir = FPaths::GetPath(ModelPath);
		const FString Manifest = FPaths::Combine(FPaths::GetPath(ModelDir), TEXT("manifest.json"));
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Manifest)) return false;
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return false;
		const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
		if (!Root->TryGetArrayField(TEXT("models"), Models) || !Models) return false;
		const FString Id = FPaths::GetCleanFilename(ModelDir);
		for (const TSharedPtr<FJsonValue>& Value : *Models)
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			FString EntryId;
			bool bFlip = false;
			if (Entry.IsValid() && Entry->TryGetStringField(TEXT("id"), EntryId) && EntryId == Id && Entry->TryGetBoolField(TEXT("flip"), bFlip))
			{
				OutFlip = bFlip;
				return true;
			}
		}
		return false;
	}

	FString SurfaceLabel(const FSimCruiseGltfModel& Model, const FSimCruiseGltfPrimitive& Primitive)
	{
		FString Label = Primitive.OwnerLabel;
		if (Model.Materials.IsValidIndex(Primitive.MaterialIndex))
		{
			const FSimCruiseGltfMaterial& M = Model.Materials[Primitive.MaterialIndex];
			Label += TEXT(" ") + M.Name;
			for (int32 Image : {M.BaseColorImage, M.EmissiveImage, M.NormalImage})
			{
				if (Model.ImageFiles.IsValidIndex(Image))
				{
					Label += TEXT(" ") + FPaths::GetCleanFilename(Model.ImageFiles[Image]);
				}
			}
		}
		return Label;
	}
}

bool SimCruiseCarModelRig::Build(const FSimCruiseGltfModel& Model, const FSimCruiseCarPresentationProfile& Profile,
	const FSimCruiseRigInputs& Inputs, FSimCruiseRiggedCar& OutCar, FString& OutError)
{
	using namespace SimCruiseRigPrivate;
	OutCar = FSimCruiseRiggedCar();
	FContext Context;
	Context.Model = &Model;
	Context.Profile = &Profile;
	Context.Inputs = Inputs;
	Context.Out = &OutCar;

	// 1. Every primitive in root space (glTF: +Y up after the exporter's root turn).
	Context.Jobs.Reserve(Model.Primitives.Num());
	for (int32 P = 0; P < Model.Primitives.Num(); ++P)
	{
		const FSimCruiseGltfPrimitive& Primitive = Model.Primitives[P];
		FJob& Job = Context.Jobs.AddDefaulted_GetRef();
		Job.Primitive = P;
		Job.MaterialIndex = Primitive.MaterialIndex;
		Job.MaterialName = Model.Materials.IsValidIndex(Primitive.MaterialIndex) ? Model.Materials[Primitive.MaterialIndex].Name : FString();
		Job.Label = SurfaceLabel(Model, Primitive);
		Job.Role = Profile.RoleFor(Job.MaterialName, Primitive.OwnerLabel);
		Job.NamedWheel = NamedWheel(Primitive.OwnerLabel);
		const FMatrix& M = Primitive.NodeToRoot;
		Job.bMirrored = M.Determinant() < 0.0;
		const FMatrix NormalMatrix = M.Inverse().GetTransposed();
		Job.Pos.SetNumUninitialized(Primitive.Positions.Num());
		for (int32 I = 0; I < Primitive.Positions.Num(); ++I)
		{
			Job.Pos[I] = M.TransformPosition(FVector(Primitive.Positions[I]));
		}
		Job.Nrm.SetNumUninitialized(Primitive.Normals.Num());
		for (int32 I = 0; I < Primitive.Normals.Num(); ++I)
		{
			Job.Nrm[I] = NormalMatrix.TransformVector(FVector(Primitive.Normals[I])).GetSafeNormal();
		}
		Job.Tan.SetNumUninitialized(Primitive.Tangents.Num());
		for (int32 I = 0; I < Primitive.Tangents.Num(); ++I)
		{
			const FVector4f& T = Primitive.Tangents[I];
			const FVector Dir = M.TransformVector(FVector(T.X, T.Y, T.Z)).GetSafeNormal();
			Job.Tan[I] = FVector4(Dir, T.W * (Job.bMirrored ? -1.0 : 1.0));
		}
		Job.Dest[DestBody] = Primitive.Indices;
	}

	auto ApplyBasis = [&Context](const FMatrix& Basis)
	{
		const FMatrix NormalBasis = Basis.Inverse().GetTransposed();
		for (FJob& Job : Context.Jobs)
		{
			for (FVector& P : Job.Pos) P = Basis.TransformPosition(P);
			for (FVector& N : Job.Nrm) N = NormalBasis.TransformVector(N).GetSafeNormal();
			for (FVector4& T : Job.Tan)
			{
				const FVector D = Basis.TransformVector(FVector(T.X, T.Y, T.Z)).GetSafeNormal();
				T = FVector4(D, T.W);
			}
		}
	};
	auto Bounds = [&Context]()
	{
		FBox Box(ForceInit);
		for (const FJob& Job : Context.Jobs)
			for (const FVector& P : Job.Pos) Box += P;
		return Box;
	};

	// 2. Upright (ModelRig.gd upright): the shortest axis is up, tyres sit low.
	{
		const FBox Box = Bounds();
		const FVector Size = Box.GetSize();
		FQuat Turn = FQuat::Identity;
		if (Size.X < Size.Y && Size.X <= Size.Z) Turn = FQuat(FVector(0, 0, -1), UE_DOUBLE_HALF_PI);
		else if (Size.Z < Size.Y && Size.Z < Size.X) Turn = FQuat(FVector(1, 0, 0), -UE_DOUBLE_HALF_PI);
		FVector Tyre = FVector::ZeroVector;
		int32 Tyres = 0;
		for (const FJob& Job : Context.Jobs)
		{
			if (Job.Role != ESimCruiseCarMaterialRole::Tyre) continue;
			for (const FVector& P : Job.Pos)
			{
				Tyre += Turn.RotateVector(P);
				++Tyres;
			}
		}
		if (Tyres > 0 && (Tyre / Tyres).Y > Turn.RotateVector(Box.GetCenter()).Y)
		{
			Turn = FQuat(FVector(1, 0, 0), UE_DOUBLE_PI) * Turn;
		}
		if (!Turn.Equals(FQuat::Identity))
		{
			Context.Note(FString::Printf(TEXT("upright: turned by %s"), *Turn.Rotator().ToString()));
			ApplyBasis(FRotationMatrix::Make(Turn));
		}
	}

	// 3. Root-space bounds and the long axis.
	const FBox RootBox = Bounds();
	const FVector Lo = RootBox.Min, Hi = RootBox.Max, Size = RootBox.GetSize();
	const bool bLongX = Size.X > Size.Z;
	Context.Note(FString::Printf(TEXT("bounds %s..%s size %s, long axis %s, %lld triangles"), *V3(Lo), *V3(Hi), *V3(Size),
		bLongX ? TEXT("X") : TEXT("Z"), Model.NumTriangles()));

	// 4. Wheel centres, by name or by the ground contacts.
	FVector Centres[4];
	bool bNamed = false;
	{
		FBox ByName[4] = {FBox(ForceInit), FBox(ForceInit), FBox(ForceInit), FBox(ForceInit)};
		int32 NamedCount = 0;
		for (const FJob& Job : Context.Jobs)
		{
			if (Job.NamedWheel == INDEX_NONE) continue;
			for (const FVector& P : Job.Pos) ByName[Job.NamedWheel] += P;
		}
		for (int32 W = 0; W < 4; ++W) NamedCount += ByName[W].IsValid ? 1 : 0;
		bNamed = NamedCount == 4;
		if (bNamed)
		{
			for (int32 W = 0; W < 4; ++W) Centres[W] = ByName[W].GetCenter();
		}
	}
	double TyreRadiusModel = 0.0;
	if (!bNamed)
	{
		// ModelRig.gd contact_wheels: the four lowest patches give the axles and track.
		const double Length = bLongX ? Size.X : Size.Z;
		const double GuessR = Inputs.TireRadiusM / (Inputs.WheelbaseM / (Length * 0.58));
		const double Band = Lo.Y + GuessR * 0.12;
		const FVector Mid = RootBox.GetCenter();
		FVector Sum[4] = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
		int32 Count[4] = {};
		for (const FJob& Job : Context.Jobs)
		{
			for (const FVector& W : Job.Pos)
			{
				if (W.Y > Band) continue;
				const double Along = bLongX ? W.X - Mid.X : W.Z - Mid.Z;
				const double Across = bLongX ? W.Z - Mid.Z : W.X - Mid.X;
				const int32 G = (Along < 0 ? 0 : 2) + (Across < 0 ? 0 : 1); // n-, n+, p-, p+
				Sum[G] += W;
				++Count[G];
			}
		}
		for (int32 G = 0; G < 4; ++G)
		{
			if (Count[G] == 0)
			{
				OutError = TEXT("could not find four wheels: a ground-contact quadrant is empty");
				return false;
			}
			Sum[G] /= Count[G];
		}
		const double WB = ((Sum[0] + Sum[1]) * 0.5 - (Sum[2] + Sum[3]) * 0.5).Size();
		TyreRadiusModel = Inputs.TireRadiusM * WB / Inputs.WheelbaseM;
		for (int32 G = 0; G < 4; ++G) Sum[G].Y = Lo.Y + TyreRadiusModel;
		Context.Note(FString::Printf(TEXT("contact patches: %d %d %d %d points, provisional tyre radius %.4f units"),
			Count[0], Count[1], Count[2], Count[3], TyreRadiusModel));
		// Provisional names: front at the positive end (relabelled below).
		Centres[0] = Sum[2];
		Centres[1] = Sum[3];
		Centres[2] = Sum[0];
		Centres[3] = Sum[1];

		// Refinement (new): the tyre surfaces, when a material says tyre, give
		// the true hub centre and radius instead of the spec's radius.
		FBox TyreBox[4] = {FBox(ForceInit), FBox(ForceInit), FBox(ForceInit), FBox(ForceInit)};
		const double Reach = TyreRadiusModel * 1.6;
		for (const FJob& Job : Context.Jobs)
		{
			if (Job.Role != ESimCruiseCarMaterialRole::Tyre) continue;
			for (const FVector& P : Job.Pos)
			{
				for (int32 W = 0; W < 4; ++W)
				{
					const FVector D = P - Centres[W];
					if (FMath::Abs(D.X) < Reach && FMath::Abs(D.Z) < Reach && FMath::Abs(D.Y) < Reach)
					{
						TyreBox[W] += P;
						break;
					}
				}
			}
		}
		int32 Refined = 0;
		double RadiusSum = 0.0;
		for (int32 W = 0; W < 4; ++W)
		{
			if (!TyreBox[W].IsValid) continue;
			const FVector E = TyreBox[W].GetExtent();
			const double Along = bLongX ? E.X : E.Z;
			if (Along < TyreRadiusModel * 0.6 || E.Y < TyreRadiusModel * 0.6) continue;
			Centres[W] = TyreBox[W].GetCenter();
			RadiusSum += FMath::Max(Along, E.Y);
			++Refined;
		}
		if (Refined == 4)
		{
			TyreRadiusModel = RadiusSum / 4.0;
			Context.Note(FString::Printf(TEXT("hubs refined from the tyre surfaces: tyre radius %.4f units"), TyreRadiusModel));
		}
	}
	else
	{
		double Sum = 0.0;
		for (const FJob& Job : Context.Jobs)
		{
			// Named wheels: the tyre (or whole wheel) height gives the radius.
			if (Job.NamedWheel == INDEX_NONE) continue;
			for (const FVector& P : Job.Pos) Sum = FMath::Max(Sum, P.Y - Centres[Job.NamedWheel].Y);
		}
		TyreRadiusModel = Sum;
	}

	const FVector AxleFront = (Centres[0] + Centres[1]) * 0.5;
	const FVector AxleRear = (Centres[2] + Centres[3]) * 0.5;
	const double WheelbaseModel = (AxleFront - AxleRear).Size();
	const double S = Inputs.WheelbaseM / FMath::Max(WheelbaseModel, 1e-6);
	OutCar.bNamedWheels = bNamed;
	OutCar.ScaleMetresPerUnit = S;
	Context.Note(FString::Printf(TEXT("%s wheels, model wheelbase %.4f units, scale %.5f m/unit"),
		bNamed ? TEXT("named") : TEXT("geometric"), WheelbaseModel, S));

	// 5. Frame: forward from the rear axle to the front.
	const FVector MidAxles = (AxleFront + AxleRear) * 0.5;
	FVector Fwd = AxleFront - AxleRear;
	Fwd.Y = 0.0;
	Fwd.Normalize();
	if (Profile.bFlip.IsSet() || bNamed)
	{
		if (Profile.bFlip.Get(false)) Fwd = -Fwd;
	}
	else
	{
		TOptional<bool> ManifestFlip;
		if (ReadManifestFlip(FPaths::Combine(FPaths::ProjectDir(), Profile.ModelPath), ManifestFlip) && ManifestFlip.IsSet())
		{
			// cars/rig-infra: geometric wheels put the front at the positive
			// end of the long axis; turn when the manifest's front is the negative end.
			const bool bFrontPositive = bLongX ? !ManifestFlip.GetValue() : ManifestFlip.GetValue();
			if (!bFrontPositive) Fwd = -Fwd;
			Context.Note(FString::Printf(TEXT("front from the manifest: %s end of %s"), bFrontPositive ? TEXT("positive") : TEXT("negative"), bLongX ? TEXT("X") : TEXT("Z")));
		}
	}
	// Evidence check (new): rear lamps sit behind the axle midpoint. When the
	// model's lamp surfaces say the other end is the back, trust the lamps
	// over the manifest; an explicit profile flip or named wheels still win.
	if (!Profile.bFlip.IsSet() && !bNamed)
	{
		double Along = 0.0;
		int32 Count = 0;
		for (const FJob& Job : Context.Jobs)
		{
			if (Job.Role != ESimCruiseCarMaterialRole::LampRear) continue;
			for (const FVector& P : Job.Pos)
			{
				Along += (P - MidAxles) | Fwd;
				++Count;
			}
		}
		if (Count > 0 && Along / Count > 0.0)
		{
			Fwd = -Fwd;
			Context.Note(FString::Printf(TEXT("rear lamps (%d vertices) sit at the front end: model turned round"), Count));
		}
	}
	const FVector Up(0, 1, 0);
	const FVector Right = (Fwd ^ Up).GetSafeNormal(); // right-handed cross: vehicle +X
	const double CentreY = (Centres[0].Y + Centres[1].Y + Centres[2].Y + Centres[3].Y) * 0.25;
	const FVector OriginModel(MidAxles.X, CentreY, MidAxles.Z);
	const double HubY = Inputs.HubHeightM;
	auto ToVehicle = [&](const FVector& V)
	{
		const FVector D = V - OriginModel;
		return FVector((Right | D) * S, (Up | D) * S + HubY, (-(Fwd | D)) * S);
	};
	for (FJob& Job : Context.Jobs)
	{
		for (FVector& P : Job.Pos) P = ToVehicle(P);
		for (FVector& N : Job.Nrm) N = FVector(Right | N, Up | N, -(Fwd | N)).GetSafeNormal();
		for (FVector4& T : Job.Tan)
		{
			const FVector D(T.X, T.Y, T.Z);
			T = FVector4(FVector(Right | D, Up | D, -(Fwd | D)).GetSafeNormal(), T.W);
		}
	}
	FVector Hub[4];
	for (int32 W = 0; W < 4; ++W) Hub[W] = ToVehicle(Centres[W]);
	if (!bNamed || Profile.bRelabelWheels)
	{
		// Left/right and front/rear as the vehicle sees them.
		FVector Relabelled[4];
		bool bSeen[4] = {};
		for (int32 W = 0; W < 4; ++W)
		{
			const int32 Key = (Hub[W].Z < 0 ? 0 : 2) + (Hub[W].X < 0 ? 0 : 1);
			Relabelled[Key] = Hub[W];
			bSeen[Key] = true;
		}
		if (bSeen[0] && bSeen[1] && bSeen[2] && bSeen[3])
		{
			for (int32 W = 0; W < 4; ++W) Hub[W] = Relabelled[W];
		}
		if (bNamed)
		{
			for (FJob& Job : Context.Jobs)
			{
				if (Job.NamedWheel == INDEX_NONE || Job.Pos.Num() == 0) continue;
				FVector C = FVector::ZeroVector;
				for (const FVector& P : Job.Pos) C += P;
				C /= Job.Pos.Num();
				Job.NamedWheel = (C.Z < 0 ? 0 : 2) + (C.X < 0 ? 0 : 1);
			}
		}
	}
	// Lateral fit (new): many downloads are a few per cent too wide or narrow
	// for their published track. Scale across the car so the mean model track
	// equals the car's, within +-10 % (a bigger miss means a wrong model).
	{
		const double ModelTrack = (FMath::Abs(Hub[1].X - Hub[0].X) + FMath::Abs(Hub[3].X - Hub[2].X)) * 0.5;
		const double CarTrack = (Inputs.FrontTrackM + Inputs.RearTrackM) * 0.5;
		const double Lateral = ModelTrack > 0.1 ? CarTrack / ModelTrack : 1.0;
		if (Profile.bFitTrack && FMath::Abs(Lateral - 1.0) > 0.015 && FMath::Abs(Lateral - 1.0) < 0.10)
		{
			for (FJob& Job : Context.Jobs)
			{
				for (FVector& P : Job.Pos) P.X *= Lateral;
				for (FVector& N : Job.Nrm) N = FVector(N.X / Lateral, N.Y, N.Z).GetSafeNormal();
				for (FVector4& T : Job.Tan) T = FVector4(FVector(T.X * Lateral, T.Y, T.Z).GetSafeNormal(), T.W);
			}
			for (int32 W = 0; W < 4; ++W) Hub[W].X *= Lateral;
			OutCar.LateralScale = Lateral;
			Context.Note(FString::Printf(TEXT("lateral fit: model track %.3f m against the car's %.3f m, scaled across by %.4f"), ModelTrack, CarTrack, Lateral));
		}
	}
	const double TyreRadiusM = TyreRadiusModel * S;
	OutCar.ModelTyreRadiusM = TyreRadiusM;
	OutCar.ModelFrontTrackM = FMath::Abs(Hub[1].X - Hub[0].X);
	OutCar.ModelRearTrackM = FMath::Abs(Hub[3].X - Hub[2].X);
	Context.Note(FString::Printf(TEXT("hubs (Godot frame, m): fl %s fr %s rl %s rr %s"), *V3(Hub[0]), *V3(Hub[1]), *V3(Hub[2]), *V3(Hub[3])));
	Context.Note(FString::Printf(TEXT("model vs car: tyre radius %.3f m (car %.3f), front track %.3f m (car %.3f), rear track %.3f m (car %.3f)"),
		TyreRadiusM, Inputs.TireRadiusM, OutCar.ModelFrontTrackM, Inputs.FrontTrackM, OutCar.ModelRearTrackM, Inputs.RearTrackM));

	// 6. Hidden surfaces: badges, logos and helpers by name or role.
	TOptional<FRegexPattern> ExtraHide;
	if (!Profile.HidePattern.IsEmpty())
	{
		ExtraHide.Emplace(Profile.HidePattern, ERegexPatternFlags::CaseInsensitive);
	}
	for (FJob& Job : Context.Jobs)
	{
		bool bHide = Job.Role == ESimCruiseCarMaterialRole::Hidden || SimCruiseCarMaterialRoles::IsBadgeLabel(Job.Label);
		if (!bHide && ExtraHide.IsSet())
		{
			FRegexMatcher Matcher(ExtraHide.GetValue(), Job.Label);
			bHide = Matcher.FindNext();
		}
		if (bHide)
		{
			OutCar.HiddenSurfaces++;
			OutCar.BadgeTrianglesRemoved += Job.Dest[DestBody].Num() / 3;
			Job.Dest[DestBody].Reset();
			Job.Role = ESimCruiseCarMaterialRole::Hidden;
		}
	}

	// 7. Wheels. Named: whole parts. Geometric: connected parts wholly inside a
	// tyre cylinder (radius + 4 %, within a tyre width of the hub plane); a part
	// that reaches outside (suspension arm, arch liner) stays on the body.
	const double CylinderR = FMath::Max(TyreRadiusM, Inputs.TireRadiusM * 0.85) * 1.04;
	const double HalfWidth = Inputs.TireWidthM * 0.75;
	auto WheelOf = [&](const FVector& P) -> int32
	{
		for (int32 W = 0; W < 4; ++W)
		{
			const FVector D = P - Hub[W];
			if (FMath::Abs(D.X) <= HalfWidth && FMath::Sqrt(D.Y * D.Y + D.Z * D.Z) <= CylinderR) return W;
		}
		return INDEX_NONE;
	};
	int32 MovedToWheels = 0, MovedToUprights = 0;
	TArray<int32> GeometricJobs;
	for (int32 J = 0; J < Context.Jobs.Num(); ++J)
	{
		FJob& Job = Context.Jobs[J];
		if (Job.Dest[DestBody].Num() == 0) continue;
		if (bNamed)
		{
			if (Job.NamedWheel != INDEX_NONE)
			{
				const bool bUpright = Job.Role == ESimCruiseCarMaterialRole::BrakeCaliper;
				Job.Dest[(bUpright ? DestUpright0 : DestWheel0) + Job.NamedWheel] = MoveTemp(Job.Dest[DestBody]);
				Job.Dest[DestBody].Reset();
			}
			else if (Job.Role == ESimCruiseCarMaterialRole::BrakeCaliper || Job.Label.Contains(TEXT("caliper")))
			{
				// Separately named calipers follow the nearest hub.
				FVector C = FVector::ZeroVector;
				for (const FVector& P : Job.Pos) C += P;
				C /= FMath::Max(1, Job.Pos.Num());
				int32 Nearest = 0;
				for (int32 W = 1; W < 4; ++W)
				{
					if ((C - Hub[W]).SizeSquared() < (C - Hub[Nearest]).SizeSquared()) Nearest = W;
				}
				if ((C - Hub[Nearest]).Size() < TyreRadiusM * 1.2)
				{
					Job.Dest[DestUpright0 + Nearest] = MoveTemp(Job.Dest[DestBody]);
					Job.Dest[DestBody].Reset();
				}
			}
			continue;
		}
		// Only jobs with a vertex near a hub can contribute.
		bool bNear = false;
		for (const FVector& P : Job.Pos)
		{
			if (WheelOf(P) != INDEX_NONE)
			{
				bNear = true;
				break;
			}
		}
		if (bNear) GeometricJobs.Add(J);
	}
	if (!bNamed && GeometricJobs.Num() > 0)
	{
		const TArray<FComponent> Parts = ConnectedParts(Context, GeometricJobs, DestBody, [&](const FVector& P) { return WheelOf(P) != INDEX_NONE; });
		TArray<FTriRef> ToWheel[4], ToUpright[4], HubEmblems;
		int32 ClippedParts = 0;
		for (const FComponent& Part : Parts)
		{
			if (Part.bClipped || Part.Points.Num() == 0)
			{
				ClippedParts += Part.bClipped ? 1 : 0;
				continue;
			}
			const int32 W = WheelOf(Part.Points[0]);
			bool bSame = W != INDEX_NONE;
			for (const FVector& P : Part.Points)
			{
				if (WheelOf(P) != W)
				{
					bSame = false;
					break;
				}
			}
			if (!bSame) continue;
			// Spinning or not: a caliper (by role) or any other off-axis part
			// sitting inboard of the wheel's centre plane does not turn with
			// the wheel. Lug nuts and valves sit outboard and do.
			const ESimCruiseCarMaterialRole Role = Context.Jobs[Part.Tris[0].Job].Role;
			const FVector D = Part.Centroid - Hub[W];
			const double Radial = FMath::Sqrt(D.Y * D.Y + D.Z * D.Z);
			double Extent = 0.0;
			for (const FVector& P : Part.Points) Extent = FMath::Max(Extent, (P - Part.Centroid).Size());
			const double Outboard = D.X * FMath::Sign(Hub[W].X);
			const bool bSymmetricRole = Role == ESimCruiseCarMaterialRole::Tyre || Role == ESimCruiseCarMaterialRole::WheelAlloy || Role == ESimCruiseCarMaterialRole::BrakeDisc;
			const bool bUpright = Role == ESimCruiseCarMaterialRole::BrakeCaliper ||
				(!bSymmetricRole && Radial > 0.04 && Radial > 0.5 * Extent && Outboard < 0.01 && Extent > 0.03);
			// A maker's emblem on the centre cap: a small part of another material
			// sitting on the axle at the outer face.
			const bool bEmblem = Profile.bAutoBadges && !bSymmetricRole && Radial < 0.02 && Extent < 0.035 && Outboard > 0.02;
			if (bEmblem)
			{
				HubEmblems.Append(Part.Tris);
				continue;
			}
			(bUpright ? ToUpright[W] : ToWheel[W]).Append(Part.Tris);
		}
		TArray<TPair<FTriRef, int32>> Moves;
		for (int32 W = 0; W < 4; ++W)
		{
			MovedToWheels += ToWheel[W].Num();
			MovedToUprights += ToUpright[W].Num();
			for (const FTriRef& Tri : ToWheel[W]) Moves.Emplace(Tri, DestWheel0 + W);
			for (const FTriRef& Tri : ToUpright[W]) Moves.Emplace(Tri, DestUpright0 + W);
		}
		for (const FTriRef& Tri : HubEmblems) Moves.Emplace(Tri, DestSteering); // a bin, emptied below
		MoveTrisTo(Context, Moves, DestBody);
		for (FJob& Job : Context.Jobs) Job.Dest[DestSteering].Reset();
		OutCar.BadgeTrianglesRemoved += HubEmblems.Num();
		Context.Note(FString::Printf(TEXT("wheel cut: %d connected parts near the hubs (%d reach outside and stay), %d triangles spin, %d steer without spinning, %d centre-cap emblem triangles removed"),
			Parts.Num(), ClippedParts, MovedToWheels, MovedToUprights, HubEmblems.Num()));
	}

	// 7b. Emblems (new, general): makers put their badge on the centreline of
	// the nose and tail. A small, thin, detached part there (not paint, glass
	// or a lamp) is a badge; a part that is joined to anything bigger stays.
	if (Profile.bAutoBadges)
	{
		FBox BodyBox(ForceInit);
		TArray<int32> Candidates;
		for (int32 J = 0; J < Context.Jobs.Num(); ++J)
		{
			const FJob& Job = Context.Jobs[J];
			for (uint32 VI : Job.Dest[DestBody]) BodyBox += Job.Pos[VI];
			using R = ESimCruiseCarMaterialRole;
			const bool bSkip = Job.Role == R::Paint || Job.Role == R::Glass || Job.Role == R::GlassDark || Job.Role == R::LampFront ||
				Job.Role == R::LampRear || Job.Role == R::LampAmber || Job.Role == R::Tyre || Job.Role == R::MirrorGlass;
			if (!bSkip && Job.Dest[DestBody].Num() > 0) Candidates.Add(J);
		}
		const double Length = BodyBox.GetSize().Z;
		const double FrontLimit = BodyBox.Min.Z + Length * 0.13, RearLimit = BodyBox.Max.Z - Length * 0.13;
		const TArray<FComponent> Parts = ConnectedParts(Context, Candidates, DestBody, [&](const FVector& P)
		{
			return FMath::Abs(P.X) < 0.16 && (P.Z < FrontLimit || P.Z > RearLimit) && P.Y > HubY - 0.1;
		});
		TArray<FTriRef> Gone;
		int32 Emblems = 0;
		for (const FComponent& Part : Parts)
		{
			if (Part.bClipped || Part.Points.Num() < 8) continue;
			FBox Box(ForceInit);
			for (const FVector& P : Part.Points) Box += P;
			const FVector PartSize = Box.GetSize();
			const double Largest = FMath::Max3(PartSize.X, PartSize.Y, PartSize.Z);
			double Cov[3][3] = {};
			for (const FVector& P : Part.Points)
			{
				const FVector D = P - Part.Centroid;
				for (int32 I = 0; I < 3; ++I)
					for (int32 J = 0; J < 3; ++J)
						Cov[I][J] += D[I] * D[J];
			}
			const FVector Thin = SmallestAxis(Cov);
			double ThinLo = TNumericLimits<double>::Max(), ThinHi = TNumericLimits<double>::Lowest();
			for (const FVector& P : Part.Points)
			{
				const double A = (P - Part.Centroid) | Thin;
				ThinLo = FMath::Min(ThinLo, A);
				ThinHi = FMath::Max(ThinHi, A);
			}
			const double Thinnest = ThinHi - ThinLo;
			if (FMath::Abs(Part.Centroid.X) < 0.05 && Largest < 0.14 && Largest > 0.015 && Thinnest < 0.025)
			{
				Gone.Append(Part.Tris);
				++Emblems;
				Context.Note(FString::Printf(TEXT("emblem removed: '%s' part %s, %.0f x %.0f x %.0f mm"), *Context.Jobs[Part.Tris[0].Job].MaterialName,
					*V3(GodotToUnrealCm(Part.Centroid) / 100.0), PartSize.Z * 1000.0, PartSize.X * 1000.0, PartSize.Y * 1000.0));
			}
		}
		MoveTris(Context, Gone, DestBody, DestSteering);
		for (FJob& Job : Context.Jobs) Job.Dest[DestSteering].Reset();
		OutCar.BadgeTrianglesRemoved += Gone.Num();
	}

	// 8. Badge areas from the profile (boxes in the Unreal vehicle frame, metres).
	for (const FSimCruiseBadgeArea& Area : Profile.BadgeAreas)
	{
		TArray<int32> Which;
		for (int32 J = 0; J < Context.Jobs.Num(); ++J)
		{
			if (!Area.Material.IsEmpty() && Context.Jobs[J].MaterialName != Area.Material) continue;
			if (Context.Jobs[J].Dest[DestBody].Num() > 0) Which.Add(J);
		}
		int32 Removed = 0;
		if (!Area.bHasBox)
		{
			for (int32 J : Which)
			{
				Removed += Context.Jobs[J].Dest[DestBody].Num() / 3;
				Context.Jobs[J].Dest[DestBody].Reset();
			}
		}
		else
		{
			const FVector A = UnrealMToGodot(Area.Box.Min), B = UnrealMToGodot(Area.Box.Max);
			const FBox Box(A.ComponentMin(B), A.ComponentMax(B));
			const TArray<FComponent> Parts = ConnectedParts(Context, Which, DestBody, [&Box](const FVector& P) { return Box.IsInsideOrOn(P); });
			TArray<FTriRef> Gone;
			for (const FComponent& Part : Parts)
			{
				if (!Part.bClipped) Gone.Append(Part.Tris);
			}
			Removed = Gone.Num();
			// "Destination" 9 is reused as a bin here, then emptied.
			MoveTris(Context, Gone, DestBody, DestSteering);
			for (int32 J : Which) Context.Jobs[J].Dest[DestSteering].Reset();
		}
		OutCar.BadgeTrianglesRemoved += Removed;
		Context.Note(FString::Printf(TEXT("badge '%s': %d triangles removed"), *Area.Note, Removed));
	}

	// 8b. Surfaces that change role: their triangles move to a copy of the
	// author material with the new role, so they get their own instance.
	auto GiveRole = [&Context, &Model, &OutCar](const TArray<FTriRef>& Taken, ESimCruiseCarMaterialRole Role)
	{
		// The steering destination is empty until step 9: use it as the hand-over bin.
		MoveTris(Context, Taken, DestBody, DestSteering);
		const int32 JobCount = Context.Jobs.Num();
		for (int32 J = 0; J < JobCount; ++J)
		{
			if (Context.Jobs[J].Dest[DestSteering].Num() == 0) continue;
			const int32 Source = Context.Jobs[J].MaterialIndex;
			int32 Extra = INDEX_NONE;
			for (int32 E = 0; E < OutCar.ExtraMaterials.Num(); ++E)
			{
				if (OutCar.ExtraMaterials[E].Source == Source && OutCar.ExtraMaterials[E].Role == Role) Extra = Model.Materials.Num() + E;
			}
			if (Extra == INDEX_NONE)
			{
				Extra = Model.Materials.Num() + OutCar.ExtraMaterials.Num();
				OutCar.ExtraMaterials.Add({Source, Role});
			}
			FJob Copy = Context.Jobs[J];
			for (TArray<uint32>& List : Copy.Dest) List.Reset();
			Copy.Dest[DestBody] = MoveTemp(Context.Jobs[J].Dest[DestSteering]);
			Context.Jobs[J].Dest[DestSteering].Reset();
			Copy.MaterialIndex = Extra;
			Copy.Role = Role;
			Copy.MaterialName += FString(TEXT("#")) + SimCruiseCarMaterialRoles::ToString(Role);
			Context.Jobs.Add(MoveTemp(Copy));
		}
	};

	// Role areas from the profile: parts of an atlas material that are really
	// another surface (a tail-lamp lens) take that role and can light.
	for (const FSimCruiseRoleArea& Area : Profile.RoleAreas)
	{
		TArray<int32> Which;
		for (int32 J = 0; J < Context.Jobs.Num(); ++J)
		{
			if (!Area.Material.IsEmpty() && Context.Jobs[J].MaterialName != Area.Material) continue;
			if (Context.Jobs[J].Role == ESimCruiseCarMaterialRole::Hidden) continue;
			if (Context.Jobs[J].Dest[DestBody].Num() > 0) Which.Add(J);
		}
		const FVector A = UnrealMToGodot(Area.Box.Min), B = UnrealMToGodot(Area.Box.Max);
		const FBox Box(A.ComponentMin(B), A.ComponentMax(B));
		const TArray<FComponent> Parts = ConnectedParts(Context, Which, DestBody, [&Box](const FVector& P) { return Box.IsInsideOrOn(P); });
		TArray<FTriRef> Taken;
		int32 TakenParts = 0;
		for (const FComponent& Part : Parts)
		{
			if (Part.bClipped) continue;
			Taken.Append(Part.Tris);
			++TakenParts;
			if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseRigDebug")))
			{
				FBox PartBox(ForceInit);
				for (const FVector& P : Part.Points) PartBox += GodotToUnrealCm(P) / 100.0;
				Context.Note(FString::Printf(TEXT("  role area part %s..%s, %d triangles"), *V3(PartBox.Min), *V3(PartBox.Max), Part.Tris.Num()));
			}
		}
		GiveRole(Taken, Area.Role);
		Context.Note(FString::Printf(TEXT("role area '%s': %d parts, %d triangles -> %s"), *Area.Note, TakenParts, Taken.Num(),
			SimCruiseCarMaterialRoles::ToString(Area.Role)));
	}

	// White lenses behind the axles are reversing (and plate) lamps, not head
	// lamps: they stay clear glass instead of lighting with the head lamps.
	{
		TArray<FTriRef> Reverse;
		for (int32 J = 0; J < Context.Jobs.Num(); ++J)
		{
			const FJob& Job = Context.Jobs[J];
			if (Job.Role != ESimCruiseCarMaterialRole::LampFront) continue;
			const TArray<uint32>& List = Job.Dest[DestBody];
			for (int32 K = 0; K + 2 < List.Num(); K += 3)
			{
				// Godot frame: +Z is rearward of the axle midpoint.
				if (Job.Pos[List[K]].Z > 0.0 && Job.Pos[List[K + 1]].Z > 0.0 && Job.Pos[List[K + 2]].Z > 0.0) Reverse.Add({J, K});
			}
		}
		if (Reverse.Num() > 0)
		{
			GiveRole(Reverse, ESimCruiseCarMaterialRole::Glass);
			Context.Note(FString::Printf(TEXT("white lenses behind the axles (reversing lamps): %d triangles kept unlit"), Reverse.Num()));
		}
	}

	// 9. The cabin steering wheel (cars/rig-infra find_steering_wheel).
	FTransform SteeringPivotG = FTransform::Identity; // Godot frame basis+origin (as a plain matrix carrier)
	FVector SteerCentre = FVector::ZeroVector, SteerN = FVector::ZeroVector, SteerUp = FVector::ZeroVector;
	if (!Profile.bSteeringWheelNone)
	{
		const FVector Eye = UnrealMToGodot(Inputs.DriverEyeM);
		const double FrontZ = (Hub[0].Z + Hub[1].Z) * 0.5;
		const double RearZ = (Hub[2].Z + Hub[3].Z) * 0.5;
		const double HalfW = FMath::Max(FMath::Abs(Hub[0].X), FMath::Abs(Hub[1].X)) + 0.2;
		FRing Ring;
		FString How;
		TArray<int32> BodyJobs;
		for (int32 J = 0; J < Context.Jobs.Num(); ++J)
		{
			if (Context.Jobs[J].Dest[DestBody].Num() > 0) BodyJobs.Add(J);
		}
		if (Profile.bHasSteeringWheelRim)
		{
			Ring.bValid = true;
			Ring.Centre = UnrealMToGodot(Profile.SteeringWheelCentreM);
			Ring.Normal = UnrealMToGodot(Profile.SteeringWheelAxis).GetSafeNormal();
			Ring.Radius = Profile.SteeringWheelRadiusM;
			How = TEXT("set in the presentation profile");
		}
		else
		{
			const FRegexPattern Pattern(TEXT("steer|wheel_s(?![a-z])|volant|lenkrad|lenkung|stuur|volante"), ERegexPatternFlags::CaseInsensitive);
			TArray<int32> NamedParts;
			for (int32 J : BodyJobs)
			{
				const FString& Label = Context.Jobs[J].Label;
				bool bWanted;
				if (!Profile.SteeringWheelNode.IsEmpty())
				{
					bWanted = Label.Contains(Profile.SteeringWheelNode.ToLower());
				}
				else
				{
					FRegexMatcher Matcher(Pattern, Label);
					bWanted = Matcher.FindNext();
				}
				if (bWanted) NamedParts.Add(J);
			}
			if (NamedParts.Num() > 0)
			{
				const TArray<FComponent> Parts = ConnectedParts(Context, NamedParts, DestBody, [](const FVector&) { return true; });
				Ring = BestRing(Parts, nullptr);
				if (!Ring.bValid)
				{
					TArray<FVector> All;
					for (const FComponent& Part : Parts) All.Append(Part.Points);
					Ring = RingOf(All);
				}
				if (Ring.bValid)
				{
					for (int32 J : NamedParts)
					{
						Context.Jobs[J].Dest[DestSteering].Append(Context.Jobs[J].Dest[DestBody]);
						Context.Jobs[J].Dest[DestBody].Reset();
					}
					How = TEXT("by name");
				}
			}
			if (!Ring.bValid)
			{
				// The driver's half of the cabin (left for left-hand drive, right for right-hand drive).
				const double Side = DriverSide(Eye);
				const double InnerX = 0.05 * Side, OuterX = HalfW * Side;
				const FBox Region(FVector(FMath::Min(InnerX, OuterX), HubY + 0.15, FrontZ - 0.1),
					FVector(FMath::Max(InnerX, OuterX), HubY + 1.65, FrontZ - 0.1 + (RearZ - FrontZ) * 0.6));
				const TArray<FComponent> Parts = ConnectedParts(Context, BodyJobs, DestBody, [&Region](const FVector& P) { return Region.IsInsideOrOn(P); });
				TArray<FString> Debug;
				Ring = BestRing(Parts, &Eye, FParse::Param(FCommandLine::Get(), TEXT("SimCruiseRigDebug")) ? &Debug : nullptr);
				for (const FString& Line : Debug) Context.Note(Line);
				How = TEXT("by geometry");
				TArray<FString> ArcDebug;
				const FRing FromArcs = RingFromArcs(Parts, Eye, FParse::Param(FCommandLine::Get(), TEXT("SimCruiseRigDebug")) ? &ArcDebug : nullptr);
				for (const FString& Line : ArcDebug) Context.Note(Line);
				if (FromArcs.bValid && (!Ring.bValid || FromArcs.Radius > Ring.Radius * 1.15))
				{
					Ring = FromArcs;
					How = TEXT("by geometry, from rim arcs");
				}
			}
		}
		if (Ring.bValid)
		{
			FVector N = Ring.Normal;
			if (N.Z < 0.0) N = -N; // toward the driver, who sits behind the wheel
			const FVector Centre = Ring.Centre;
			double R = Ring.Radius;
			// The rim may be split into arcs, or the first ring found may be a
			// horn ring inside it: look for arcs around the same axis that
			// together close a bigger circle, and take the rim's radius.
			{
				TArray<int32> Open;
				for (int32 J = 0; J < Context.Jobs.Num(); ++J)
				{
					if (Context.Jobs[J].Dest[DestBody].Num() > 0) Open.Add(J);
				}
				const FBox Wide(Centre - FVector(0.3), Centre + FVector(0.3));
				const TArray<FComponent> Pieces = ConnectedParts(Context, Open, DestBody, [&Wide](const FVector& P) { return Wide.IsInsideOrOn(P); });
				const FVector U0 = (FVector(0, 1, 0) - N * (N | FVector(0, 1, 0))).GetSafeNormal();
				const FVector V0 = N ^ U0;
				struct FArc { double Radius; uint32 Sectors; };
				TArray<FArc> Arcs;
				for (const FComponent& Piece : Pieces)
				{
					if (Piece.bClipped || Piece.Points.Num() < 12) continue;
					double RMin = 1e9, RMax = 0.0;
					uint32 Sectors = 0;
					bool bFlat = true;
					for (const FVector& V : Piece.Points)
					{
						const FVector D = V - Centre;
						const double A = D | N;
						if (FMath::Abs(A) > 0.07) { bFlat = false; break; }
						const FVector Flat = D - N * A;
						const double Rad = Flat.Size();
						RMin = FMath::Min(RMin, Rad);
						RMax = FMath::Max(RMax, Rad);
						const int32 Sector = int32(FMath::FloorToDouble((FMath::Atan2(Flat | V0, Flat | U0) + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI * 16.0)) & 15;
						Sectors |= 1u << Sector;
					}
					if (!bFlat || RMax > RimRadiusMax * 1.05 || RMax < R * 1.1 || RMin < 0.6 * RMax) continue;
					Arcs.Add({RMax, Sectors});
				}
				Arcs.Sort([](const FArc& A, const FArc& B) { return A.Radius > B.Radius; });
				if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseRigDebug")))
				{
					for (const FArc& Arc : Arcs) Context.Note(FString::Printf(TEXT("  rim arc radius %.3f, %d sectors"), Arc.Radius, FMath::CountBits(Arc.Sectors)));
					Context.Note(FString::Printf(TEXT("  %d pieces near the ring"), Pieces.Num()));
				}
				for (int32 I = 0; I < Arcs.Num(); ++I)
				{
					uint32 Union = 0;
					for (const FArc& Other : Arcs)
					{
						if (FMath::Abs(Other.Radius - Arcs[I].Radius) < 0.02) Union |= Other.Sectors;
					}
					if (FMath::CountBits(Union) >= 13)
					{
						Context.Note(FString::Printf(TEXT("steering wheel rim found outside the first ring: radius %.3f m (was %.3f m)"), Arcs[I].Radius, R));
						R = Arcs[I].Radius;
						break;
					}
				}
			}
			const FBox Near(Centre - FVector(R + 0.15), Centre + FVector(R + 0.15));
			TArray<int32> Remaining;
			for (int32 J = 0; J < Context.Jobs.Num(); ++J)
			{
				if (Context.Jobs[J].Dest[DestBody].Num() > 0) Remaining.Add(J);
			}
			const TArray<FComponent> Parts = ConnectedParts(Context, Remaining, DestBody, [&Near](const FVector& P) { return Near.IsInsideOrOn(P); });
			TArray<FTriRef> Turning;
			int32 CutTriangles = 0;
			auto InDisc = [&](const FVector& V, double AxialMin, double AxialMax, double RadiusLimit)
			{
				const FVector D = V - Centre;
				const double Axial = D | N;
				return Axial >= AxialMin && Axial <= AxialMax && (D - N * Axial).Size() <= RadiusLimit;
			};
			// Parts wholly inside the wheel's cylinder turn with it.
			TArray<const FComponent*> Joined;
			bool bHubTurns = false;
			for (const FComponent& Part : Parts)
			{
				bool bInside = !Part.bClipped;
				for (const FVector& V : Part.Points)
				{
					if (!bInside) break;
					bInside = InDisc(V, -0.12, 0.09, R * 1.08);
				}
				if (!bInside)
				{
					Joined.Add(&Part);
					continue;
				}
				Turning.Append(Part.Tris);
				for (const FVector& V : Part.Points)
				{
					if (InDisc(V, -0.12, 0.09, R * 0.3))
					{
						bHubTurns = true;
						break;
					}
				}
			}
			// Spokes and hub welded to the column (the rim alone was found): cut
			// the triangles inside the wheel's disc from the parts that reach the
			// hub; the column and the dash beyond the disc stay.
			if (!bHubTurns)
			{
				for (const FComponent* Part : Joined)
				{
					bool bReachesHub = false;
					for (const FVector& V : Part->Points)
					{
						if (InDisc(V, -0.12, 0.09, R * 0.3))
						{
							bReachesHub = true;
							break;
						}
					}
					if (!bReachesHub) continue;
					for (const FTriRef& Tri : Part->Tris)
					{
						const FJob& Job = Context.Jobs[Tri.Job];
						const TArray<uint32>& List = Job.Dest[DestBody];
						if (InDisc(Job.Pos[List[Tri.Offset]], -0.12, 0.09, R * 1.02) && InDisc(Job.Pos[List[Tri.Offset + 1]], -0.12, 0.09, R * 1.02) &&
							InDisc(Job.Pos[List[Tri.Offset + 2]], -0.12, 0.09, R * 1.02))
						{
							Turning.Add(Tri);
							++CutTriangles;
						}
					}
				}
			}
			if (CutTriangles > 0)
			{
				Context.Note(FString::Printf(TEXT("steering wheel: %d triangles cut from parts joined to the column"), CutTriangles));
			}
			MoveTris(Context, Turning, DestBody, DestSteering);
			int32 Moved = 0;
			for (const FJob& Job : Context.Jobs) Moved += Job.Dest[DestSteering].Num() / 3;
			SteerCentre = Centre;
			SteerN = N;
			SteerUp = (FVector(0, 1, 0) - N * (N | FVector(0, 1, 0))).GetSafeNormal();
			OutCar.bHasSteeringWheel = Moved > 0;
			OutCar.SteeringWheelRadiusM = R;
			Context.Note(FString::Printf(TEXT("steering wheel %s: centre %s, column %s (%.0f deg from horizontal), rim radius %.3f m, %d triangles"),
				*How, *V3(Centre), *V3(N), FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(N.Y, -1.0, 1.0))), R, Moved));
		}
		else
		{
			Context.Note(TEXT("steering wheel: not found"));
		}
	}

	// 10. Write the parts in Unreal axes (X forward, Y right, Z up, cm).
	for (int32 W = 0; W < 4; ++W)
	{
		OutCar.HubsCm[W] = GodotToUnrealCm(Hub[W]);
		OutCar.Wheels[W].Name = FName(WheelKeys[W]);
		OutCar.Wheels[W].PartToVehicle = FTransform(OutCar.HubsCm[W]);
		OutCar.Uprights[W].Name = FName(FString(WheelKeys[W]) + TEXT("_upright"));
		OutCar.Uprights[W].PartToVehicle = FTransform(OutCar.HubsCm[W]);
	}
	OutCar.Body.Name = TEXT("body");
	OutCar.SteeringWheel.Name = TEXT("steering_wheel");
	if (OutCar.bHasSteeringWheel)
	{
		const FVector X = GodotToUnrealDir(-SteerN);  // along the column, away from the driver
		const FVector Z = GodotToUnrealDir(SteerUp);   // the rim's top
		const FVector Y = Z ^ X;
		OutCar.SteeringWheel.PartToVehicle = FTransform(X, Y, Z, GodotToUnrealCm(SteerCentre));
	}

	FSimCruiseRigPart* Parts[DestCount] = {&OutCar.Body,
		&OutCar.Wheels[0], &OutCar.Wheels[1], &OutCar.Wheels[2], &OutCar.Wheels[3],
		&OutCar.Uprights[0], &OutCar.Uprights[1], &OutCar.Uprights[2], &OutCar.Uprights[3],
		&OutCar.SteeringWheel};
	TArray<int32> Remap;
	FBox VehicleBox(ForceInit);
	for (int32 DestIndex = 0; DestIndex < DestCount; ++DestIndex)
	{
		FSimCruiseRigPart& Part = *Parts[DestIndex];
		const FTransform& Pivot = Part.PartToVehicle;
		TMap<int32, int32> SectionOfMaterial;
		for (FJob& Job : Context.Jobs)
		{
			const TArray<uint32>& List = Job.Dest[DestIndex];
			if (List.Num() == 0) continue;
			const int32* Existing = SectionOfMaterial.Find(Job.MaterialIndex);
			const int32 SectionIndex = Existing ? *Existing : SectionOfMaterial.Add(Job.MaterialIndex, Part.Sections.AddDefaulted());
			FSimCruiseRigSection& Section = Part.Sections[SectionIndex];
			Section.MaterialIndex = Job.MaterialIndex;
			Section.Role = Job.Role;
			const FSimCruiseGltfPrimitive& Primitive = Model.Primitives[Job.Primitive];
			const bool bHasNormals = Job.Nrm.Num() == Job.Pos.Num();
			const bool bHasTangents = Job.Tan.Num() == Job.Pos.Num();
			const bool bHasUV = Primitive.UV0.Num() == Job.Pos.Num();
			// Sections merged from several jobs must agree on optional streams.
			if (Section.Positions.Num() > 0 && (Section.Tangents.Num() > 0) != bHasTangents) Section.Tangents.Reset();
			Remap.Init(INDEX_NONE, Job.Pos.Num());
			// Winding: glTF counter-clockwise fronts stay front faces under the X/Y/Z axis change
			// (Unreal takes (P2-P0)x(P1-P0) as the outward normal; checked with two-sided materials,
			// whose facing sign turns the shading normal). Only a mirrored node flips it.
			const bool bFlip = Job.bMirrored;
			for (int32 K = 0; K + 2 < List.Num(); K += 3)
			{
				const uint32 Corners[3] = {List[K], bFlip ? List[K + 2] : List[K + 1], bFlip ? List[K + 1] : List[K + 2]};
				for (uint32 VI : Corners)
				{
					if (Remap[VI] == INDEX_NONE)
					{
						Remap[VI] = Section.Positions.Num();
						const FVector U = GodotToUnrealCm(Job.Pos[VI]);
						VehicleBox += U;
						Section.Positions.Add(FVector3f(Pivot.InverseTransformPositionNoScale(U)));
						const FVector Nu = bHasNormals ? Pivot.InverseTransformVectorNoScale(GodotToUnrealDir(Job.Nrm[VI])) : FVector::UpVector;
						Section.Normals.Add(FVector3f(Nu));
						if (bHasTangents && (Section.Tangents.Num() > 0 || Section.Positions.Num() == 1))
						{
							const FVector4& T = Job.Tan[VI];
							const FVector Tu = Pivot.InverseTransformVectorNoScale(GodotToUnrealDir(FVector(T.X, T.Y, T.Z)));
							Section.Tangents.Add(FVector4f(Tu.X, Tu.Y, Tu.Z, T.W)); // as Epic's glTF importer: sign kept, green channel flipped
						}
						Section.UV0.Add(bHasUV ? Primitive.UV0[VI] : FVector2f::ZeroVector);
					}
					Section.Indices.Add(Remap[VI]);
				}
			}
			if (Section.Tangents.Num() != Section.Positions.Num()) Section.Tangents.Reset();
		}
	}
	const FVector BoxSize = VehicleBox.GetSize() / 100.0;
	OutCar.ModelLengthM = BoxSize.X;
	OutCar.ModelWidthM = BoxSize.Y;
	OutCar.ModelHeightM = VehicleBox.Max.Z / 100.0 - (Inputs.HubHeightM - TyreRadiusM);
	Context.Note(FString::Printf(TEXT("fitted size %.3f x %.3f m, roof %.3f m above the tyre contact; body %d, wheels %d/%d/%d/%d, uprights %d, steering wheel %d triangles"),
		OutCar.ModelLengthM, OutCar.ModelWidthM, OutCar.ModelHeightM, OutCar.Body.NumTriangles(),
		OutCar.Wheels[0].NumTriangles(), OutCar.Wheels[1].NumTriangles(), OutCar.Wheels[2].NumTriangles(), OutCar.Wheels[3].NumTriangles(),
		OutCar.Uprights[0].NumTriangles() + OutCar.Uprights[1].NumTriangles() + OutCar.Uprights[2].NumTriangles() + OutCar.Uprights[3].NumTriangles(),
		OutCar.SteeringWheel.NumTriangles()));

	// 11. Material census: where each author material ended up (Unreal frame, m).
	for (int32 MaterialIndex = 0; MaterialIndex < Model.Materials.Num(); ++MaterialIndex)
	{
		int32 Tris[DestCount] = {};
		FBox Box(ForceInit);
		ESimCruiseCarMaterialRole Role = ESimCruiseCarMaterialRole::Authored;
		bool bUsed = false;
		for (const FJob& Job : Context.Jobs)
		{
			if (Job.MaterialIndex != MaterialIndex) continue;
			bUsed = true;
			Role = Job.Role;
			for (int32 D = 0; D < DestCount; ++D) Tris[D] += Job.Dest[D].Num() / 3;
			for (uint32 VI : Job.Dest[DestBody]) Box += GodotToUnrealCm(Job.Pos[VI]) / 100.0;
		}
		if (!bUsed) continue;
		Context.Note(FString::Printf(TEXT("material '%s' -> %s: body %d, wheels %d, uprights %d, steering %d; body box %s..%s"),
			*Model.Materials[MaterialIndex].Name, SimCruiseCarMaterialRoles::ToString(Role), Tris[0],
			Tris[1] + Tris[2] + Tris[3] + Tris[4], Tris[5] + Tris[6] + Tris[7] + Tris[8], Tris[9],
			Box.IsValid ? *V3(Box.Min) : TEXT("-"), Box.IsValid ? *V3(Box.Max) : TEXT("-")));
	}
	if (OutCar.Body.IsEmpty())
	{
		OutError = TEXT("the fitted body is empty");
		return false;
	}
	for (int32 W = 0; W < 4; ++W)
	{
		if (OutCar.Wheels[W].IsEmpty())
		{
			OutError = FString::Printf(TEXT("wheel %s received no triangles"), WheelKeys[W]);
			return false;
		}
	}
	return true;
}
