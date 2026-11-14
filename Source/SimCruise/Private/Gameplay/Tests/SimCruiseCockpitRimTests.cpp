// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruiseCockpitTest.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarModelRig.h"
#include "Presentation/SimCruiseGltfModel.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

/*
 * The cockpit steer test's rim measure (SimCruiseCockpitRules::MeasureRim) on the three cars'
 * rigged steering wheels, headless: it must find the rim square to the column and centred on it,
 * as the rig check does (SimCruise.Presentation: WHEELAXIS lines). Before the outer-edge rule the
 * cockpit test read the Miata at 12.9 deg / 2.2 cm, where the rig check and the frames show
 * about 1 deg / 0.05 cm. Logs "COCKPITRIM <car> tilt .. deg, centre .. cm off the axis".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseCockpitRimOnRigTest, "SimCruise.Gameplay.Cockpit.RimOnTheRiggedWheels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseCockpitRimOnRigTest::RunTest(const FString& Parameters)
{
	for (const FString& CarId : {FString(TEXT("nb1999")), FString(TEXT("mustang65")), FString(TEXT("meridian_z"))})
	{
		FSimCruiseCarDefinition Car;
		FSimCruiseCarPresentationProfile Profile;
		FSimCruiseGltfModel Model;
		FSimCruiseRiggedCar Rigged;
		FString Error;
		if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Car, Error) || !SimCruisePresentationProfiles::Load(CarId, Profile, Error) ||
			!SimCruiseGltf::Load(FPaths::Combine(FPaths::ProjectDir(), Profile.ModelPath), Model, Error) ||
			!SimCruiseCarModelRig::Build(Model, Profile, FSimCruiseRigInputs::FromCar(Car), Rigged, Error))
		{
			AddError(CarId + TEXT(": ") + Error);
			continue;
		}
		if (!Rigged.bHasSteeringWheel)
		{
			AddWarning(CarId + TEXT(": no steering wheel"));
			continue;
		}
		// Every vertex of the wheel in its own frame (X along the column, the pivot at the origin, cm).
		TArray<FVector> Points;
		for (const FSimCruiseRigSection& Section : Rigged.SteeringWheel.Sections)
		{
			for (const FVector3f& P : Section.Positions) Points.Add(FVector(P));
		}
		double TiltDeg = 0.0, OffsetCm = 0.0, RadiusCm = 0.0;
		int32 Edge = 0;
		const bool bOk = SimCruiseCockpitRules::MeasureRim(Points, TiltDeg, OffsetCm, RadiusCm, Edge);
		AddInfo(FString::Printf(TEXT("COCKPITRIM %s: tilt %.2f deg, centre %.2f cm off the axis, radius %.1f cm (rig %.1f cm), %d edge points of %d vertices"),
			*CarId, TiltDeg, OffsetCm, RadiusCm, Rigged.SteeringWheelRadiusM * 100.0, Edge, Points.Num()));
		TestTrue(CarId + TEXT(": the rim's outer edge is found"), bOk);
		TestTrue(FString::Printf(TEXT("%s: the rim radius is the rig's within 1.5 cm (%.1f against %.1f)"), *CarId, RadiusCm, Rigged.SteeringWheelRadiusM * 100.0),
			FMath::Abs(RadiusCm - Rigged.SteeringWheelRadiusM * 100.0) <= 1.5);
		TestTrue(FString::Printf(TEXT("%s: the rim is square to the column (%.2f deg, at most %.1f)"), *CarId, TiltDeg, SimCruiseCockpitRules::MaxRimTiltDeg), TiltDeg <= SimCruiseCockpitRules::MaxRimTiltDeg);
		TestTrue(FString::Printf(TEXT("%s: the rim is centred on the column (%.2f cm, at most %.1f)"), *CarId, OffsetCm, SimCruiseCockpitRules::MaxRimAxisOffsetCm), OffsetCm <= SimCruiseCockpitRules::MaxRimAxisOffsetCm);
	}
	return true;
}

#endif
