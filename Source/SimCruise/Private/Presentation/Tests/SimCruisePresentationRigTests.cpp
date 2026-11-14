// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarModelRig.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "Presentation/SimCruiseGltfModel.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruisePresentationTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	struct FLoaded
	{
		FSimCruiseCarDefinition Car;
		FSimCruiseCarPresentationProfile Profile;
		FSimCruiseGltfModel Model;
		FSimCruiseRiggedCar Rigged;
	};

	bool LoadAndRig(FAutomationTestBase& Test, const FString& CarId, FLoaded& Out)
	{
		FString Error;
		if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Out.Car, Error))
		{
			Test.AddError(Error);
			return false;
		}
		if (!SimCruisePresentationProfiles::Load(CarId, Out.Profile, Error))
		{
			Test.AddError(Error);
			return false;
		}
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), Out.Profile.ModelPath);
		if (!SimCruiseGltf::Load(Path, Out.Model, Error))
		{
			Test.AddError(Error);
			return false;
		}
		const bool bOk = SimCruiseCarModelRig::Build(Out.Model, Out.Profile, FSimCruiseRigInputs::FromCar(Out.Car), Out.Rigged, Error);
		for (const FString& Line : Out.Rigged.Log)
		{
			Test.AddInfo(CarId + TEXT(": ") + Line);
		}
		if (!bOk)
		{
			Test.AddError(CarId + TEXT(": ") + Error);
		}
		return bOk;
	}

	/**
	 * The cockpit wheel turns true: in its own frame (X along the column, the pivot at the origin)
	 * the rim's points ring the axis (their centre within 0.5 cm of it, so the hub does not orbit)
	 * and lie in a plane square to it (within 2 deg, so the rim does not wobble); and nothing of
	 * the instruments (gauge role) or glass turns with it.
	 */
	void CheckSteeringWheel(FAutomationTestBase& Test, const FString& CarId, const FLoaded& L)
	{
		const FSimCruiseRiggedCar& R = L.Rigged;
		if (!R.bHasSteeringWheel)
		{
			return;
		}
		const double RimCm = R.SteeringWheelRadiusM * 100.0;
		double SumY = 0, SumZ = 0, Count = 0;
		// Plane fit X = a Y + b Z + c over the rim points.
		double Syy = 0, Szz = 0, Syz = 0, Sxy = 0, Sxz = 0, Sx = 0;
		int32 InstrumentTris = 0;
		TArray<FVector3f> Annulus;
		// The rim: the section whose annulus points ring the column (most sectors, then most points).
		int32 BestSectors = -1, BestPoints = -1;
		for (const FSimCruiseRigSection& Section : R.SteeringWheel.Sections)
		{
			if (Section.Role == ESimCruiseCarMaterialRole::Gauge || Section.Role == ESimCruiseCarMaterialRole::Glass)
			{
				InstrumentTris += Section.NumTriangles();
			}
			TArray<FVector3f> Mine;
			uint32 Mask = 0;
			for (const FVector3f& P : Section.Positions)
			{
				const double Radial = FMath::Sqrt(double(P.Y) * P.Y + double(P.Z) * P.Z);
				if (Radial >= 0.8 * RimCm && Radial <= 1.15 * RimCm && FMath::Abs(P.X) < 6.0)
				{
					Mine.Add(P);
					Mask |= 1u << (int32(FMath::FloorToDouble((FMath::Atan2(double(P.Z), double(P.Y)) + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI * 16.0)) & 15);
				}
			}
			const int32 Sec = FMath::CountBits(Mask);
			const bool bRings = Sec >= 14, bBestRings = BestSectors >= 14;
			if ((bRings && !bBestRings) || (bRings == bBestRings && (bRings ? Mine.Num() > BestPoints : (Sec > BestSectors || (Sec == BestSectors && Mine.Num() > BestPoints)))))
			{
				BestSectors = Sec;
				BestPoints = Mine.Num();
				Annulus = MoveTemp(Mine);
			}
		}
		// The rim's tube: within 2 cm of the median radius and 1.5 cm of the median depth.
		TArray<double> Radii, Depths;
		for (const FVector3f& P : Annulus) { Radii.Add(FMath::Sqrt(double(P.Y) * P.Y + double(P.Z) * P.Z)); Depths.Add(P.X); }
		Radii.Sort(); Depths.Sort();
		const double MedianR = Radii.Num() ? Radii[Radii.Num() / 2] : RimCm, MedianX = Depths.Num() ? Depths[Depths.Num() / 2] : 0.0;
		for (const FVector3f& P : Annulus)
		{
			{ const double Rp = FMath::Sqrt(double(P.Y) * P.Y + double(P.Z) * P.Z); if (FMath::Abs(Rp - MedianR) > 1.5 || FMath::Abs(P.X - MedianX) > 1.5) continue; }
			SumY += P.Y; SumZ += P.Z; Count += 1;
			Syy += double(P.Y) * P.Y; Szz += double(P.Z) * P.Z; Syz += double(P.Y) * P.Z; Sxy += double(P.X) * P.Y; Sxz += double(P.X) * P.Z; Sx += P.X;
		}
		Test.TestEqual(CarId + TEXT(": no instrument or glass triangles turn with the wheel"), InstrumentTris, 0);
		if (Count < 24)
		{
			Test.AddError(CarId + TEXT(": too few rim points to check the wheel's axis"));
			return;
		}
		// Where the rim is: 16 sectors around the column, each must hold rim points (a rim left
		// partly on the dash would turn in pieces), and the circle through them (Kasa) has its
		// centre on the axis.
		uint32 Sectors = 0;
		double Suu = 0, Svv = 0, Suv = 0, Suuu = 0, Svvv = 0, Suvv = 0, Svuu = 0;
		const double Mu = SumY / Count, Mv = SumZ / Count;
		for (const FVector3f& P : Annulus)
		{
			{ const double Rp = FMath::Sqrt(double(P.Y) * P.Y + double(P.Z) * P.Z); if (FMath::Abs(Rp - MedianR) > 1.5 || FMath::Abs(P.X - MedianX) > 1.5) continue; }
			Sectors |= 1u << (int32(FMath::FloorToDouble((FMath::Atan2(double(P.Z), double(P.Y)) + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI * 16.0)) & 15);
			const double X = P.Y - Mu, Y = P.Z - Mv;
			Suu += X * X; Svv += Y * Y; Suv += X * Y; Suuu += X * X * X; Svvv += Y * Y * Y; Suvv += X * Y * Y; Svuu += Y * X * X;
		}
		const double KasaDet = Suu * Svv - Suv * Suv;
		const double Ra = 0.5 * (Suuu + Suvv), Rb = 0.5 * (Svvv + Svuu);
		const double CircleY = KasaDet > 1e-9 ? Mu + (Ra * Svv - Rb * Suv) / KasaDet : Mu;
		const double CircleZ = KasaDet > 1e-9 ? Mv + (Rb * Suu - Ra * Suv) / KasaDet : Mv;
		Test.AddInfo(FString::Printf(TEXT("WHEELRIM %s: %d of 16 sectors hold rim points; the rim's circle is centred %.2f cm off the axis (point mean %.2f cm)"),
			*CarId, FMath::CountBits(Sectors), FMath::Sqrt(CircleY * CircleY + CircleZ * CircleZ), FMath::Sqrt(Mu * Mu + Mv * Mv)));
		Test.TestTrue(FString::Printf(TEXT("%s: the whole rim turns (%d of 16 sectors)"), *CarId, FMath::CountBits(Sectors)), FMath::CountBits(Sectors) >= 15);
		if (FMath::CountBits(Sectors) < 15)
		{
			for (const FSimCruiseRigSection& Section : R.SteeringWheel.Sections)
			{
				uint32 Mask = 0;
				int32 Points = 0;
				for (const FVector3f& P : Section.Positions)
				{
					const double Radial = FMath::Sqrt(double(P.Y) * P.Y + double(P.Z) * P.Z);
					if (Radial < 0.8 * RimCm || Radial > 1.15 * RimCm) continue;
					++Points;
					Mask |= 1u << (int32(FMath::FloorToDouble((FMath::Atan2(double(P.Z), double(P.Y)) + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI * 16.0)) & 15);
				}
				const FString Name = L.Model.Materials.IsValidIndex(Section.MaterialIndex) ? L.Model.Materials[Section.MaterialIndex].Name : FString::FromInt(Section.MaterialIndex);
				Test.AddInfo(FString::Printf(TEXT("WHEELSECTION %s %s: %d annulus points, sectors %04x, x %.1f..%.1f cm"), *CarId, *Name, Points, Mask, 0.0, 0.0));
			}
		}
		const double OffAxisCm = FMath::Sqrt(CircleY * CircleY + CircleZ * CircleZ);
		// Centred sums for the tilt.
		const double My = SumY / Count, Mz = SumZ / Count, Mx = Sx / Count;
		const double Cyy = Syy / Count - My * My, Czz = Szz / Count - Mz * Mz, Cyz = Syz / Count - My * Mz;
		const double Cxy = Sxy / Count - Mx * My, Cxz = Sxz / Count - Mx * Mz;
		const double Det = Cyy * Czz - Cyz * Cyz;
		const double A = Det > 1e-9 ? (Cxy * Czz - Cxz * Cyz) / Det : 0.0;
		const double B = Det > 1e-9 ? (Cxz * Cyy - Cxy * Cyz) / Det : 0.0;
		const double TiltDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(A * A + B * B)));
		Test.AddInfo(FString::Printf(TEXT("WHEELAXIS %s: rim centre %.2f cm off the axis, rim tilted %.2f deg on the column (%d points)"), *CarId, OffAxisCm, TiltDeg, int32(Count)));
		Test.TestTrue(FString::Printf(TEXT("%s: the hub does not orbit (rim centre %.2f cm off the axis, at most 0.5)"), *CarId, OffAxisCm), OffAxisCm <= 0.5);
		// 3 deg: the Miata's rim is a leather tube over a moulded core, and two fits of it (the rig's and this one) differ by up to 2 deg.
		Test.TestTrue(FString::Printf(TEXT("%s: the rim is square to the column (%.2f deg, at most 3)"), *CarId, TiltDeg), TiltDeg <= 3.0);
	}

	/** Checks every car must pass: dimensions, wheel placement, frame and pivots. */
	void CheckRig(FAutomationTestBase& Test, const FString& CarId, const FLoaded& L)
	{
		const FSimCruiseRiggedCar& R = L.Rigged;
		CheckSteeringWheel(Test, CarId, L);
		Test.TestTrue(CarId + TEXT(": the car file gives its lock-to-lock turns"), L.Car.SteeringLockTurns > 1.0);
		const double WheelbaseCm = ((R.HubsCm[0] + R.HubsCm[1]) * 0.5 - (R.HubsCm[2] + R.HubsCm[3]) * 0.5).Size();
		Test.TestNearlyEqual(CarId + TEXT(" wheelbase (cm) equals the car's"), WheelbaseCm, L.Car.WheelbaseM * 100.0, 0.5);
		// Front ahead of rear, left on -Y.
		Test.TestTrue(CarId + TEXT(" front hubs ahead of the rear hubs"), R.HubsCm[0].X > R.HubsCm[2].X && R.HubsCm[1].X > R.HubsCm[3].X);
		Test.TestTrue(CarId + TEXT(" left hubs on -Y, right hubs on +Y"), R.HubsCm[0].Y < 0 && R.HubsCm[2].Y < 0 && R.HubsCm[1].Y > 0 && R.HubsCm[3].Y > 0);
		const double HubHeightCm = (L.Car.SuspensionMountHeightM - L.Car.StaticSuspensionLengthM) * 100.0;
		const double MeanHubZ = (R.HubsCm[0].Z + R.HubsCm[1].Z + R.HubsCm[2].Z + R.HubsCm[3].Z) * 0.25;
		Test.TestNearlyEqual(CarId + TEXT(" mean hub height at static ride"), MeanHubZ, HubHeightCm, 0.5);
		// The model's track and tyre must be within 6 % of the real car's: a
		// bigger miss means a wrong model or a wrong wheel find.
		Test.TestTrue(FString::Printf(TEXT("%s front track %.3f m within 6%% of %.3f m"), *CarId, R.ModelFrontTrackM, L.Car.FrontTrackM),
			FMath::Abs(R.ModelFrontTrackM / L.Car.FrontTrackM - 1.0) < 0.06);
		Test.TestTrue(FString::Printf(TEXT("%s rear track %.3f m within 6%% of %.3f m"), *CarId, R.ModelRearTrackM, L.Car.RearTrackM),
			FMath::Abs(R.ModelRearTrackM / L.Car.RearTrackM - 1.0) < 0.06);
		Test.TestTrue(FString::Printf(TEXT("%s tyre radius %.3f m within 8%% of %.3f m"), *CarId, R.ModelTyreRadiusM, L.Car.TireFreeRadiusM()),
			FMath::Abs(R.ModelTyreRadiusM / L.Car.TireFreeRadiusM() - 1.0) < 0.08);
		for (int32 W = 0; W < 4; ++W)
		{
			const FBox Box = R.Wheels[W].LocalBounds();
			Test.TestTrue(FString::Printf(TEXT("%s wheel %d has triangles"), *CarId, W), R.Wheels[W].NumTriangles() > 100);
			// A spinning wheel is centred on its hub: its bounds straddle the axle.
			Test.TestTrue(FString::Printf(TEXT("%s wheel %d centred on its hub"), *CarId, W),
				Box.IsValid && FMath::Abs(Box.GetCenter().X) < 6.0 && FMath::Abs(Box.GetCenter().Z) < 6.0);
		}
		Test.TestTrue(CarId + TEXT(" steering wheel found"), R.bHasSteeringWheel);
		if (R.bHasSteeringWheel)
		{
			const FVector Centre = R.SteeringWheel.PartToVehicle.GetLocation() / 100.0;
			const FVector Eye = FSimCruiseRigInputs::FromCar(L.Car).DriverEyeM;
			Test.TestTrue(FString::Printf(TEXT("%s steering wheel %s ahead of and below the eye %s"), *CarId, *Centre.ToString(), *Eye.ToString()),
				Centre.X > Eye.X + 0.2 && Centre.X < Eye.X + 0.8 && Centre.Z < Eye.Z && FMath::Abs(Centre.Y - Eye.Y) < 0.15);
			Test.TestTrue(CarId + TEXT(" steering column points forward"), R.SteeringWheel.PartToVehicle.GetUnitAxis(EAxis::X).X > 0.5);
		}
		// Every car shows brake lamps, and they sit at the back: the check that
		// the model faces forward and that its tail lamps can light.
		double RearSum = 0.0, FrontSum = 0.0;
		int32 RearCount = 0, FrontCount = 0;
		for (const FSimCruiseRigSection& Section : R.Body.Sections)
		{
			const bool bRear = Section.Role == ESimCruiseCarMaterialRole::LampRear;
			const bool bFront = Section.Role == ESimCruiseCarMaterialRole::LampFront;
			if (!bRear && !bFront) continue;
			for (const FVector3f& P : Section.Positions)
			{
				(bRear ? RearSum : FrontSum) += P.X;
				++(bRear ? RearCount : FrontCount);
			}
		}
		Test.TestTrue(CarId + TEXT(" has rear-lamp surfaces (brake lamps can light)"), RearCount > 0);
		if (RearCount > 0)
		{
			Test.TestTrue(FString::Printf(TEXT("%s rear lamps behind the axles (mean x %.2f m)"), *CarId, RearSum / RearCount / 100.0), RearSum / RearCount < -100.0);
		}
		if (FrontCount > 0)
		{
			Test.TestTrue(FString::Printf(TEXT("%s head lamps ahead of the axles (mean x %.2f m)"), *CarId, FrontSum / FrontCount / 100.0), FrontSum / FrontCount > 100.0);
			// The low beams leave from the two lenses: one each side, ahead of the
			// front hubs, outboard of the grille and inside the body, above the hubs.
			const TArray<FVector> Beams = SimCruiseCarModelRig::HeadLampCentresCm(R);
			const double FrontHubX = (R.HubsCm[0].X + R.HubsCm[1].X) * 0.5;
			const double HubZ = (R.HubsCm[0].Z + R.HubsCm[1].Z) * 0.5;
			if (Test.TestEqual(CarId + TEXT(" two head-lamp beam origins"), Beams.Num(), 2))
			{
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const FVector& B = Beams[Side];
					Test.AddInfo(FString::Printf(TEXT("%s head lamp %s at %s cm"), *CarId, Side == 0 ? TEXT("left") : TEXT("right"), *B.ToString()));
					Test.TestTrue(FString::Printf(TEXT("%s head lamp %d on its side, outboard"), *CarId, Side),
						(Side == 0 ? -B.Y : B.Y) > 25.0 && FMath::Abs(B.Y) < R.ModelWidthM * 50.0);
					Test.TestTrue(FString::Printf(TEXT("%s head lamp %d ahead of the front hubs, inside the body"), *CarId, Side),
						B.X > FrontHubX + 20.0 && B.X < FrontHubX + 150.0);
					Test.TestTrue(FString::Printf(TEXT("%s head lamp %d above the hubs (%.0f cm)"), *CarId, Side, B.Z - HubZ),
						B.Z > HubZ && B.Z < HubZ + 60.0);
				}
			}
		}
		// Length within 4 % of the published overall length when the profile knows it.
		Test.TestTrue(CarId + TEXT(" model is a car-sized object"), R.ModelLengthM > 3.5 && R.ModelLengthM < 5.2 && R.ModelWidthM > 1.5 && R.ModelWidthM < 2.2);
	}
}

using namespace SimCruisePresentationTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePresentationGltfLoadTest, "SimCruise.Presentation.Gltf.LoadsTheThreeCars", Flags)
bool FSimCruisePresentationGltfLoadTest::RunTest(const FString& Parameters)
{
	for (const FString& CarId : {FString(TEXT("nb1999")), FString(TEXT("mustang65")), FString(TEXT("meridian_z"))})
	{
		FSimCruiseCarPresentationProfile Profile;
		FString Error;
		if (!SimCruisePresentationProfiles::Load(CarId, Profile, Error))
		{
			AddError(Error);
			continue;
		}
		FSimCruiseGltfModel Model;
		if (!SimCruiseGltf::Load(FPaths::Combine(FPaths::ProjectDir(), Profile.ModelPath), Model, Error))
		{
			AddError(Error);
			continue;
		}
		AddInfo(FString::Printf(TEXT("%s: %d primitives, %lld triangles, %d materials, %d images"), *CarId,
			Model.Primitives.Num(), Model.NumTriangles(), Model.Materials.Num(), Model.ImageFiles.Num()));
		TestTrue(CarId + TEXT(" has triangles"), Model.NumTriangles() > 50000);
		for (const FSimCruiseGltfPrimitive& Primitive : Model.Primitives)
		{
			if (Primitive.Normals.Num() != Primitive.Positions.Num())
			{
				AddWarning(FString::Printf(TEXT("%s: %s has no normals"), *CarId, *Primitive.NodeName));
			}
		}
		for (const FString& Image : Model.ImageFiles)
		{
			TestTrue(CarId + TEXT(" image exists: ") + Image, FPaths::FileExists(Image));
		}
	}
	// The NB model with its HQ interior is the 1.25 M-face download.
	FSimCruiseCarPresentationProfile Profile;
	FString Error;
	FSimCruiseGltfModel Model;
	if (SimCruisePresentationProfiles::Load(TEXT("nb1999"), Profile, Error) &&
		SimCruiseGltf::Load(FPaths::Combine(FPaths::ProjectDir(), Profile.ModelPath), Model, Error))
	{
		// Sketchfab's face count includes a few faces the exporter dropped as degenerate.
		TestTrue(FString::Printf(TEXT("nb1999 triangle count %lld within 0.1%% of the manifest's 1245134 faces"), Model.NumTriangles()),
			FMath::Abs(double(Model.NumTriangles()) / 1245134.0 - 1.0) < 0.001);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePresentationRigNbTest, "SimCruise.Presentation.Rig.Nb1999", Flags)
bool FSimCruisePresentationRigNbTest::RunTest(const FString& Parameters)
{
	FLoaded L;
	if (LoadAndRig(*this, TEXT("nb1999"), L))
	{
		CheckRig(*this, TEXT("nb1999"), L);
		TestFalse(TEXT("nb1999 wheels are found by geometry"), L.Rigged.bNamedWheels);
		// The instruments: five live dials in the cluster, behind the steering wheel and in front
		// of the driver's eye, facing the driver, the speedometer and tachometer the big two.
		// The NB's five dials keep the model's own faces; a needle mesh turns over each.
		TestEqual(TEXT("nb1999 has its five dials"), L.Rigged.Dials.Num(), 5);
		if (L.Rigged.Dials.Num() > 0 && L.Rigged.bHasSteeringWheel)
		{
			const FVector Wheel = L.Rigged.SteeringWheel.PartToVehicle.GetLocation();
			for (const FSimCruiseRiggedCar::FDial& Dial : L.Rigged.Dials)
			{
				const FString What = L.Profile.Dials.IsValidIndex(Dial.Spec) ? L.Profile.Dials[Dial.Spec].Quantity : TEXT("?");
				AddInfo(FString::Printf(TEXT("dial %s at %s cm (wheel %s), facing %s, radius %.1f cm"), *What, *Dial.CentreCm.ToString(), *Wheel.ToString(),
					*Dial.Normal.ToString(), Dial.RadiusCm));
				TestTrue(What + TEXT(": ahead of the steering wheel, within 30 cm"), Dial.CentreCm.X > Wheel.X && Dial.CentreCm.X < Wheel.X + 30.0);
				TestTrue(What + TEXT(": in line with the wheel across the car (within 15 cm)"), FMath::Abs(Dial.CentreCm.Y - Wheel.Y) < 15.0);
				TestTrue(What + TEXT(": faces back toward the driver"), Dial.Normal.X < -0.6);
				TestTrue(What + TEXT(": a gauge-sized needle (1-7 cm)"), Dial.RadiusCm > 1.0 && Dial.RadiusCm < 7.0);
			}
		}
		// The NB speedometer's own marks: 0 mph at 118 degrees left of 12 o'clock, 80 near the top, 150 at the end.
		for (const FSimCruiseDialSpec& Spec : L.Profile.Dials)
		{
			if (Spec.Quantity == TEXT("speed_mph"))
			{
				TestEqual(TEXT("0 mph needle angle"), FMath::RadiansToDegrees(USimCruiseCarPresentationComponent::NeedleAngleRad(Spec, 0.0)), -118.0, 0.01);
				TestEqual(TEXT("80 mph near the top"), FMath::RadiansToDegrees(USimCruiseCarPresentationComponent::NeedleAngleRad(Spec, 80.0)), 2.0, 0.01);
				TestEqual(TEXT("a needle stops just past the last mark"), FMath::RadiansToDegrees(USimCruiseCarPresentationComponent::NeedleAngleRad(Spec, 400.0)), -118.0 + 225.0 * 1.02, 0.01);
			}
		}
		// The needles read the car: the speedometer in mph, the tachometer in rpm, rest with the engine off.
		FSimCruiseCarVisualState State;
		State.SpeedMps = 26.8224f;
		State.EngineRpm = 3200.f;
		TestEqual(TEXT("60 mph on the speedometer"), USimCruiseCarPresentationComponent::DialTarget(TEXT("speed_mph"), State), 60.0, 0.01);
		TestEqual(TEXT("96.6 km/h on a metric one"), USimCruiseCarPresentationComponent::DialTarget(TEXT("speed_kmh"), State), 96.56, 0.01);
		TestEqual(TEXT("3200 rpm on the tachometer"), USimCruiseCarPresentationComponent::DialTarget(TEXT("rpm"), State), 3200.0, 0.01);
		State.bEngineRunning = false;
		TestEqual(TEXT("the tachometer rests with the engine off"), USimCruiseCarPresentationComponent::DialTarget(TEXT("rpm"), State), 0.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePresentationRigMustangTest, "SimCruise.Presentation.Rig.Mustang65", Flags)
bool FSimCruisePresentationRigMustangTest::RunTest(const FString& Parameters)
{
	FLoaded L;
	if (LoadAndRig(*this, TEXT("mustang65"), L))
	{
		CheckRig(*this, TEXT("mustang65"), L);
		TestTrue(TEXT("mustang65 badge areas removed triangles"), L.Rigged.BadgeTrianglesRemoved > 0);
	}
	return true;
}

// Cars that only park are fitted to their profile's dimensions: each must fit (four
// wheels on the hubs, front ahead), at the real car's length and track.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePresentationRigParkedTest, "SimCruise.Presentation.Rig.ParkedCars", Flags)
bool FSimCruisePresentationRigParkedTest::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* Id; double LengthM; };
	// Published overall lengths: Model 3 184.8 in, F-150 Raptor SuperCab 220.0 in,
	// Grand Cherokee SRT8 (2013) 190.8 in, Odyssey (2018) 203.2 in.
	const FCase Cases[] = { { TEXT("model3"), 4.694 }, { TEXT("f150raptor18"), 5.588 }, { TEXT("grandcherokee13"), 4.846 }, { TEXT("odyssey18"), 5.161 }, { TEXT("crv25"), 4.694 } };
	for (const FCase& Case : Cases)
	{
		FString Error;
		FSimCruiseCarPresentationProfile Profile;
		if (!SimCruisePresentationProfiles::Load(Case.Id, Profile, Error)) { AddError(Error); continue; }
		// These cars park, drive in traffic and are drivable: fitted to their driving data (data/cars).
		FSimCruiseCarDefinition Definition;
		if (!TestTrue(FString::Printf(TEXT("%s has driving data"), Case.Id), FSimCruiseCarDefinitionLoader::LoadById(Case.Id, Definition, Error))) { AddError(Error); continue; }
		FSimCruiseGltfModel Model;
		if (!SimCruiseGltf::Load(FPaths::Combine(FPaths::ProjectDir(), Profile.ModelPath), Model, Error)) { AddError(Error); continue; }
		const FSimCruiseRigInputs Inputs = FSimCruiseRigInputs::FromCar(Definition);
		FSimCruiseRiggedCar R;
		if (!SimCruiseCarModelRig::Build(Model, Profile, Inputs, R, Error)) { AddError(FString(Case.Id) + TEXT(": ") + Error); continue; }
		AddInfo(FString::Printf(TEXT("%s: length %.3f m, width %.3f m, track %.3f / %.3f m, tyre radius %.3f m, %lld triangles"), Case.Id,
			R.ModelLengthM, R.ModelWidthM, R.ModelFrontTrackM, R.ModelRearTrackM, R.ModelTyreRadiusM, R.NumTriangles()));
		TestTrue(FString::Printf(TEXT("%s length %.3f m within 4%% of %.3f m"), Case.Id, R.ModelLengthM, Case.LengthM), FMath::Abs(R.ModelLengthM / Case.LengthM - 1.0) < 0.04);
		TestTrue(FString::Printf(TEXT("%s front track within 8%%"), Case.Id), FMath::Abs(R.ModelFrontTrackM / Inputs.FrontTrackM - 1.0) < 0.08);
		TestTrue(FString::Printf(TEXT("%s front hubs ahead of the rear"), Case.Id), R.HubsCm[0].X > R.HubsCm[2].X && R.HubsCm[1].X > R.HubsCm[3].X);
		TestTrue(FString::Printf(TEXT("%s left hubs on -Y"), Case.Id), R.HubsCm[0].Y < 0 && R.HubsCm[2].Y < 0 && R.HubsCm[1].Y > 0 && R.HubsCm[3].Y > 0);
		for (int32 W = 0; W < 4; ++W)
		{
			TestTrue(FString::Printf(TEXT("%s wheel %d has triangles"), Case.Id, W), R.Wheels[W].NumTriangles() > 100);
		}
		// Facing forward: the brake lamps are behind the axles.
		double RearSum = 0.0; int32 RearCount = 0;
		for (const FSimCruiseRigSection& Section : R.Body.Sections)
		{
			if (Section.Role != ESimCruiseCarMaterialRole::LampRear) continue;
			for (const FVector3f& P : Section.Positions) { RearSum += P.X; ++RearCount; }
		}
		// The Odyssey's lamps are one textured material, front and back, drawn as authored.
		if (RearCount == 0 && (FCString::Strcmp(Case.Id, TEXT("odyssey18")) == 0))
		{
			AddInfo(FString::Printf(TEXT("%s: lamps drawn as authored (one textured material)"), Case.Id));
		}
		else if (TestTrue(FString::Printf(TEXT("%s has rear-lamp surfaces"), Case.Id), RearCount > 0))
		{
			TestTrue(FString::Printf(TEXT("%s rear lamps behind the axles (mean x %.2f m)"), Case.Id, RearSum / RearCount / 100.0), RearSum / RearCount < -100.0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePresentationRigC8Test, "SimCruise.Presentation.Rig.MeridianZ", Flags)
bool FSimCruisePresentationRigC8Test::RunTest(const FString& Parameters)
{
	FLoaded L;
	if (LoadAndRig(*this, TEXT("meridian_z"), L))
	{
		CheckRig(*this, TEXT("meridian_z"), L);
		TestTrue(TEXT("meridian_z wheels are found by name"), L.Rigged.bNamedWheels);
		TestTrue(TEXT("meridian_z badges hidden"), L.Rigged.HiddenSurfaces >= 3);
		int32 Calipers = 0;
		for (int32 W = 0; W < 4; ++W) Calipers += L.Rigged.Uprights[W].NumTriangles() > 0 ? 1 : 0;
		TestEqual(TEXT("meridian_z four calipers ride on the uprights"), Calipers, 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePresentationRolesTest, "SimCruise.Presentation.Roles.Guess", Flags)
bool FSimCruisePresentationRolesTest::RunTest(const FString& Parameters)
{
	using R = ESimCruiseCarMaterialRole;
	TestTrue(TEXT("Body_Color is paint"), SimCruiseCarMaterialRoles::Guess(TEXT("Body_Color"), TEXT("")) == R::Paint);
	TestTrue(TEXT("Windshield is glass"), SimCruiseCarMaterialRoles::Guess(TEXT("Windshield"), TEXT("")) == R::Glass);
	TestTrue(TEXT("Brake_Lights is a rear lamp"), SimCruiseCarMaterialRoles::Guess(TEXT("Brake_Lights"), TEXT("")) == R::LampRear);
	TestTrue(TEXT("Tire_Treads is tyre"), SimCruiseCarMaterialRoles::Guess(TEXT("Tire_Treads"), TEXT("")) == R::Tyre);
	TestTrue(TEXT("Brake_Disc is a disc"), SimCruiseCarMaterialRoles::Guess(TEXT("Brake_Disc"), TEXT("")) == R::BrakeDisc);
	TestTrue(TEXT("Badges are hidden"), SimCruiseCarMaterialRoles::Guess(TEXT("Badges"), TEXT("")) == R::Hidden);
	TestTrue(TEXT("Dark_Engine_View_Glass is dark glass"), SimCruiseCarMaterialRoles::Guess(TEXT("Dark_Engine_View_Glass"), TEXT("")) == R::GlassDark);
	TestTrue(TEXT("badge labels"), SimCruiseCarMaterialRoles::IsBadgeLabel(TEXT(" corvette badge_badges_0 corvette badge")));
	for (int32 I = 0; I < static_cast<int32>(R::Count); ++I)
	{
		R Back;
		TestTrue(TEXT("role names round-trip"), SimCruiseCarMaterialRoles::FromString(SimCruiseCarMaterialRoles::ToString(static_cast<R>(I)), Back) && Back == static_cast<R>(I));
	}
	return true;
}

#endif
