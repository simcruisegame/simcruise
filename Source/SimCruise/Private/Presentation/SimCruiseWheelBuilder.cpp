// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseWheelBuilder.h"

#include "Presentation/SimCruiseCarModelRig.h"

namespace
{
	constexpr double InchCm = 2.54;

	/** Wheel frame: X forward, Y the axle, Z up; a point at angle A (radians), radius R, axial Y. */
	FVector3f At(double A, double R, double Y)
	{
		return FVector3f(static_cast<float>(R * FMath::Cos(A)), static_cast<float>(Y), static_cast<float>(R * FMath::Sin(A)));
	}

	/** Flat-shaded triangles, each wound and lit toward its hint direction. */
	struct FBuilder
	{
		FSimCruiseRigSection& S;
		void Tri(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& Hint)
		{
			FVector3f N = FVector3f::CrossProduct(B - A, C - A);
			if (N.SizeSquared() < 1e-10f) return;
			N.Normalize();
			const bool bFlip = FVector3f::DotProduct(N, Hint) < 0.f;
			// Unreal's front face has (P2-P0)x(P1-P0) outward (see SimCruiseCarModelRig), the reverse of N above.
			const FVector3f P[3] = {A, bFlip ? B : C, bFlip ? C : B};
			if (bFlip) N = -N;
			for (const FVector3f& V : P)
			{
				S.Indices.Add(S.Positions.Num());
				S.Positions.Add(V);
				S.Normals.Add(N);
				// A planar mapping that never collapses a face to a line, and an explicit tangent:
				// the surface detail needs a valid tangent frame on every triangle.
				S.UV0.Add(FVector2f(V.X * 0.05f + V.Y * 0.031f, V.Z * 0.05f + V.Y * 0.017f));
				FVector3f T = FVector3f::CrossProduct(N, FMath::Abs(N.Z) < 0.9f ? FVector3f(0.f, 0.f, 1.f) : FVector3f(1.f, 0.f, 0.f)).GetSafeNormal();
				S.Tangents.Add(FVector4f(T.X, T.Y, T.Z, 1.f));
			}
		}
		void Quad(const FVector3f& A, const FVector3f& B, const FVector3f& C, const FVector3f& D, const FVector3f& Hint)
		{
			Tri(A, B, C, Hint);
			Tri(A, C, D, Hint);
		}
	};

	FSimCruiseRigSection& SectionFor(TArray<FSimCruiseRigSection>& Out, int32 Material, ESimCruiseCarMaterialRole Role)
	{
		for (FSimCruiseRigSection& S : Out)
		{
			if (S.MaterialIndex == Material) return S;
		}
		FSimCruiseRigSection& S = Out.AddDefaulted_GetRef();
		S.MaterialIndex = Material;
		S.Role = Role;
		return S;
	}

	/** A surface of revolution through a profile of (radius, axial) points; Outward hints which side faces out. */
	void Revolve(FBuilder& B, const TArray<FVector2D>& Profile, int32 Segments, const FVector2D& Inside)
	{
		for (int32 I = 0; I < Segments; ++I)
		{
			const double A0 = UE_DOUBLE_TWO_PI * I / Segments, A1 = UE_DOUBLE_TWO_PI * (I + 1) / Segments;
			for (int32 K = 0; K + 1 < Profile.Num(); ++K)
			{
				const FVector2D& P = Profile[K];
				const FVector2D& Q = Profile[K + 1];
				// The hint: away from the profile's inside point, in the plane of this segment.
				const FVector2D Mid = (P + Q) * 0.5;
				const FVector2D Away = (Mid - Inside).GetSafeNormal();
				const double Am = (A0 + A1) * 0.5;
				const FVector3f Hint(static_cast<float>(Away.X * FMath::Cos(Am)), static_cast<float>(Away.Y), static_cast<float>(Away.X * FMath::Sin(Am)));
				B.Quad(At(A0, P.X, P.Y), At(A1, P.X, P.Y), At(A1, Q.X, Q.Y), At(A0, Q.X, Q.Y), Hint);
			}
		}
	}

	/** A flat ring at axial Y from radius R0 to R1, facing Facing (+1 or -1 along Y). */
	void Annulus(FBuilder& B, double R0, double R1, double Y, double Facing, int32 Segments)
	{
		const FVector3f Hint(0.f, static_cast<float>(Facing), 0.f);
		for (int32 I = 0; I < Segments; ++I)
		{
			const double A0 = UE_DOUBLE_TWO_PI * I / Segments, A1 = UE_DOUBLE_TWO_PI * (I + 1) / Segments;
			B.Quad(At(A0, R0, Y), At(A1, R0, Y), At(A1, R1, Y), At(A0, R1, Y), Hint);
		}
	}

	/** A straight bar of rectangular section between two points in the wheel's face (YFront to YBack). */
	void Bar(FBuilder& B, double A0, double R0, double A1, double R1, double HalfWidth, double YFront, double YBack)
	{
		const FVector3f P0 = At(A0, R0, 0.0), P1 = At(A1, R1, 0.0);
		FVector3f Along = P1 - P0;
		Along.Y = 0.f;
		Along.Normalize();
		const FVector3f Side = FVector3f::CrossProduct(Along, FVector3f(0.f, 1.f, 0.f)).GetSafeNormal() * static_cast<float>(HalfWidth);
		const FVector3f F(0.f, static_cast<float>(YFront), 0.f), K(0.f, static_cast<float>(YBack), 0.f);
		const FVector3f Out(0.f, static_cast<float>(FMath::Sign(YFront - YBack)), 0.f);
		// Front face, the two sides, the back.
		B.Quad(P0 + Side + F, P1 + Side + F, P1 - Side + F, P0 - Side + F, Out);
		B.Quad(P0 + Side + F, P1 + Side + F, P1 + Side + K, P0 + Side + K, Side);
		B.Quad(P0 - Side + F, P1 - Side + F, P1 - Side + K, P0 - Side + K, -Side);
		B.Quad(P0 + Side + K, P1 + Side + K, P1 - Side + K, P0 - Side + K, -Out);
	}
}

void SimCruiseWheelBuilder::BuildMeshWheel(const FSimCruiseWheelDesign& Design, double TyreRadiusCm, double TyreWidthCm, double OutboardSign,
	const FMaterials& Materials, TArray<FSimCruiseRigSection>& OutSections)
{
	constexpr int32 Segments = 72;
	const double S = OutboardSign >= 0.0 ? 1.0 : -1.0;
	const double RimR = Design.RimDiameterIn * InchCm * 0.5;
	const double HalfRim = Design.RimWidthIn * InchCm * 0.5;
	const double R = FMath::Max(TyreRadiusCm, RimR + 4.0);
	const double HalfTyre = FMath::Max(TyreWidthCm * 0.5, HalfRim * 0.9);
	const double Lip = Design.LipIn * InchCm;
	const double FaceY = S * (HalfRim - Design.DishIn * InchCm);
	const double SpokeOuter = RimR - Lip;

	if (Materials.Tyre != INDEX_NONE)
	{
		// Bead on the rim flange, sidewall, rounded shoulder, flat tread (a low, slightly stretched profile).
		FBuilder B{SectionFor(OutSections, Materials.Tyre, ESimCruiseCarMaterialRole::Rubber)};
		const double Shoulder = FMath::Min(2.2, (R - RimR) * 0.4);
		const TArray<FVector2D> Profile = {
			{RimR + 0.4, -HalfRim + 0.3}, {RimR + 1.5, -HalfTyre}, {R - Shoulder, -HalfTyre - 0.3}, {R - 0.4, -HalfTyre + Shoulder * 0.6},
			{R, -HalfTyre + Shoulder}, {R, HalfTyre - Shoulder}, {R - 0.4, HalfTyre - Shoulder * 0.6}, {R - Shoulder, HalfTyre + 0.3},
			{RimR + 1.5, HalfTyre}, {RimR + 0.4, HalfRim - 0.3}};
		Revolve(B, Profile, Segments, FVector2D(RimR, 0.0));
	}
	if (Materials.Lip != INDEX_NONE)
	{
		// The polished lip: the flange face, the flat lip, and the step down to the spoke face.
		FBuilder B{SectionFor(OutSections, Materials.Lip, ESimCruiseCarMaterialRole::Chrome)};
		Annulus(B, SpokeOuter, RimR + 1.0, S * HalfRim, S, Segments);
		Revolve(B, {{RimR + 1.0, S * HalfRim}, {RimR + 1.0, S * (HalfRim - 1.2)}}, Segments, FVector2D(0.0, 0.0));
		Revolve(B, {{SpokeOuter, S * HalfRim}, {SpokeOuter, FaceY}}, Segments, FVector2D(RimR * 2.0, FaceY));
	}
	if (Materials.Face != INDEX_NONE)
	{
		FBuilder B{SectionFor(OutSections, Materials.Face, ESimCruiseCarMaterialRole::WheelAlloy)};
		// Barrel behind the face (seen through the mesh) and the inner flange.
		Revolve(B, {{RimR - 0.6, FaceY}, {RimR - 0.6, -S * HalfRim}}, Segments, FVector2D(RimR * 2.0, 0.0));
		Annulus(B, RimR - 0.6, RimR + 1.0, -S * HalfRim, -S, Segments);
		// Outer ring tying the spoke tips, and the centre hub.
		const double Depth = 1.8;
		Annulus(B, SpokeOuter - 1.4, SpokeOuter + 0.2, FaceY, S, Segments);
		const double HubR = 5.6;
		Annulus(B, 3.2, HubR + 0.6, FaceY + S * 0.4, S, Segments);
		Revolve(B, {{HubR + 0.6, FaceY + S * 0.4}, {HubR + 0.6, FaceY - S * Depth}}, Segments, FVector2D(0.0, FaceY));
		// The mesh: from each hub point two bars run out to the neighbouring rim points, crossing
		// their neighbours' bars half way, plus a short radial rib at each crossing.
		const int32 N = FMath::Clamp(Design.Spokes, 5, 16);
		const double Half = UE_DOUBLE_PI / N;
		for (int32 I = 0; I < N; ++I)
		{
			const double A = UE_DOUBLE_TWO_PI * I / N;
			Bar(B, A, HubR, A + Half, SpokeOuter, 0.8, FaceY, FaceY - S * Depth);
			Bar(B, A, HubR, A - Half, SpokeOuter, 0.8, FaceY, FaceY - S * Depth);
			Bar(B, A + Half, (HubR + SpokeOuter) * 0.5, A + Half, SpokeOuter, 0.4, FaceY - S * 0.2, FaceY - S * Depth);
		}
	}
	if (Materials.Cap != INDEX_NONE)
	{
		FBuilder B{SectionFor(OutSections, Materials.Cap, ESimCruiseCarMaterialRole::PlasticGrained)};
		Annulus(B, 0.0, 4.6, FaceY + S * 1.1, S, 36);
		Revolve(B, {{4.6, FaceY + S * 1.1}, {4.6, FaceY + S * 0.3}}, 36, FVector2D(0.0, FaceY));
	}
}
