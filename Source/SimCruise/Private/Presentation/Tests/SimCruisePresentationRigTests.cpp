// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarModelRig.h"
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

	/** Checks every car must pass: dimensions, wheel placement, frame and pivots. */
	void CheckRig(FAutomationTestBase& Test, const FString& CarId, const FLoaded& L)
	{
		const FSimCruiseRiggedCar& R = L.Rigged;
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
