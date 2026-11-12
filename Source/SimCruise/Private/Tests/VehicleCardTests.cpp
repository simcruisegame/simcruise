// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

// A car's data card ("card" in data/cars/<id>.json) holds what the real car measures outside
// the CarTrack bench: turning circle, curb mass and front share, steering ratio, with sources.
// These tests hold the driving file and the presentation profile to it.

namespace
{
	constexpr EAutomationTestFlags CardFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	TSharedPtr<FJsonObject> LoadCard(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FString Text;
		const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("cars"), FString(CarId) + TEXT(".json"));
		TSharedPtr<FJsonObject> Root;
		if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			Test.AddError(FString::Printf(TEXT("%s does not read"), *Path));
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* Card = nullptr;
		if (!Root->TryGetObjectField(TEXT("card"), Card) || !Card)
		{
			Test.AddError(FString::Printf(TEXT("%s has no data card"), CarId));
			return nullptr;
		}
		return *Card;
	}

	/**
	 * Kerb-to-kerb turning circle from the geometry: the outer front wheel's path at full lock.
	 * The rack angle is the bicycle-model angle (SimCruiseVehicleSim: turn radius = wheelbase /
	 * tan(rack) at the rear axle), so the outer front tyre runs half a track further out and a
	 * wheelbase ahead, plus half its width to the kerb.
	 */
	double TurningCircleM(const FSimCruiseCarDefinition& Car)
	{
		const double Rack = FMath::DegreesToRadians(Car.ParkingRackAngleDeg);
		const double RearRadius = Car.WheelbaseM / FMath::Tan(Rack);
		const double Outer = FMath::Sqrt(FMath::Square(RearRadius + Car.FrontTrackM * 0.5) + FMath::Square(Car.WheelbaseM));
		return 2.0 * (Outer + Car.TireSectionWidthM * 0.5);
	}

	bool RunCardTest(FAutomationTestBase& Test, const TCHAR* CarId)
	{
		FSimCruiseCarDefinition Car;
		FString Error;
		if (!Test.TestTrue(TEXT("car loads"), FSimCruiseCarDefinitionLoader::LoadById(CarId, Car, Error)))
		{
			Test.AddError(Error);
			return false;
		}
		const TSharedPtr<FJsonObject> Card = LoadCard(Test, CarId);
		if (!Card.IsValid())
		{
			return false;
		}
		double Value = 0.0;
		if (Card->TryGetNumberField(TEXT("turning_circle_m"), Value))
		{
			const double Model = TurningCircleM(Car);
			const FString Line = FString::Printf(TEXT("CARD %-11s turning circle %.2f m (real %.2f m)"), CarId, Model, Value);
			Test.AddInfo(Line);
			UE_LOG(LogTemp, Display, TEXT("%s"), *Line);
			Test.TestTrue(FString::Printf(TEXT("turning circle %.2f m within 4 %% of the real %.2f m"), Model, Value), FMath::Abs(Model / Value - 1.0) < 0.04);
		}
		if (Card->TryGetNumberField(TEXT("curb_mass_kg"), Value))
		{
			// The driving file's mass is as tested (with fuel and often a driver): heavier than curb, by less than 10 %.
			Test.TestTrue(FString::Printf(TEXT("mass %.0f kg is curb %.0f kg plus at most 10 %%"), Car.MassKg, Value),
				Car.MassKg >= Value * 0.99 && Car.MassKg <= Value * 1.10);
		}
		if (Card->TryGetNumberField(TEXT("front_share"), Value))
		{
			Test.TestTrue(FString::Printf(TEXT("front share %.3f within 0.015 of the real %.3f"), Car.FrontWeightFraction, Value),
				FMath::Abs(Car.FrontWeightFraction - Value) <= 0.015);
		}
		if (Card->TryGetNumberField(TEXT("steering_ratio"), Value))
		{
			FSimCruiseCarPresentationProfile Profile;
			if (SimCruisePresentationProfiles::Load(CarId, Profile, Error))
			{
				Test.TestEqual(TEXT("the cockpit wheel turns at the real steering ratio"), Profile.SteeringRatio, Value, 0.05);
			}
		}
		FString Source;
		Test.TestTrue(TEXT("the card names its sources"), Card->TryGetStringField(TEXT("sources"), Source) && !Source.IsEmpty());
		return true;
	}
}

#define SIMCRUISE_CARD_TEST(CarIdent) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleCard_##CarIdent, "SimCruise.Vehicle.Card." #CarIdent, CardFlags) \
	bool FSimCruiseVehicleCard_##CarIdent::RunTest(const FString& Parameters) { return RunCardTest(*this, TEXT(#CarIdent)); }

SIMCRUISE_CARD_TEST(nb2001)
SIMCRUISE_CARD_TEST(mustang65)
SIMCRUISE_CARD_TEST(meridian_z)

#undef SIMCRUISE_CARD_TEST

#endif
