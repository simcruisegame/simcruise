// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Gameplay/SimCruisePlaces.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SimCruisePlacesTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;
}

// StartPoolCheck.gd: the bundled pool loads, every entry is valid, damaged entries are dropped.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseStartPoolTest, "SimCruise.Gameplay.Places.StartPool", SimCruisePlacesTest::Flags)
bool FSimCruiseStartPoolTest::RunTest(const FString& Parameters)
{
	const TArray<FSimCruisePlace> Pool = SimCruisePlaces::LoadStartPool();
	TestTrue(TEXT("the bundled start pool has places"), Pool.Num() >= 30);
	for (const FSimCruisePlace& Place : Pool)
	{
		TestTrue(*FString::Printf(TEXT("%s is valid"), *Place.Title), Place.IsValid() && Place.bHasBearing && Place.bLoose);
	}
	const FString Damaged = TEXT("[{\"title\":\"ok\",\"lon\":-80,\"lat\":25,\"bearing\":10},{\"title\":\"\",\"lon\":-80,\"lat\":25,\"bearing\":10},")
		TEXT("{\"title\":\"far north\",\"lon\":-80,\"lat\":85,\"bearing\":10},{\"title\":\"bad bearing\",\"lon\":-80,\"lat\":25,\"bearing\":360},\"junk\"]");
	TestEqual(TEXT("damaged entries are dropped one by one"), SimCruisePlaces::ParseStartPool(Damaged).Num(), 1);
	TestEqual(TEXT("not a list: no places"), SimCruisePlaces::ParseStartPool(TEXT("{}")).Num(), 0);

	// Seeded sequences repeat, are distinct and leave out the current place.
	FRandomStream A(7), B(7);
	const TArray<FSimCruisePlace> First = SimCruisePlaces::Sequence(Pool, A, 4, Pool[0].Title);
	const TArray<FSimCruisePlace> Second = SimCruisePlaces::Sequence(Pool, B, 4, Pool[0].Title);
	TestEqual(TEXT("four picks"), First.Num(), 4);
	TSet<FString> Titles;
	for (int32 i = 0; i < First.Num(); ++i)
	{
		TestEqual(TEXT("same seed, same sequence"), First[i].Title, Second[i].Title);
		TestNotEqual(TEXT("the current place is left out"), First[i].Title, Pool[0].Title);
		Titles.Add(First[i].Title);
	}
	TestEqual(TEXT("picks are distinct"), Titles.Num(), 4);
	return true;
}

// Drive.gd search(): coordinates, aliases and Surprise me are answered offline.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePlaceQueryTest, "SimCruise.Gameplay.Places.Query", SimCruisePlacesTest::Flags)
bool FSimCruisePlaceQueryTest::RunTest(const FString& Parameters)
{
	using namespace SimCruisePlaces;
	FQueryAnswer A = ParseQuery(TEXT(" 37.101444, -109.990814 "));
	TestTrue(TEXT("lat, lon is a place"), A.Kind == EQueryKind::Places && A.Places.Num() == 1);
	if (A.Places.Num() == 1)
	{
		TestEqual(TEXT("latitude"), A.Places[0].Lat, 37.101444, 1e-9);
		TestEqual(TEXT("longitude"), A.Places[0].Lon, -109.990814, 1e-9);
	}
	TestTrue(TEXT("past 82 degrees goes to the helper"), ParseQuery(TEXT("85, 10")).Kind == EQueryKind::Search);
	TestTrue(TEXT("surprise me"), ParseQuery(TEXT("Surprise me")).Kind == EQueryKind::Surprise);
	A = ParseQuery(TEXT("South Beach"));
	TestTrue(TEXT("Miami Beach alias is the bundled place"), A.Kind == EQueryKind::Places && A.Places.Num() == 1 && A.Places[0].Title == Fallback().Title);
	TestTrue(TEXT("Davidson Hall alias"), ParseQuery(TEXT("gould academy")).Places.Num() == 1);
	TestTrue(TEXT("names go to the helper"), ParseQuery(TEXT("Monument Valley")).Kind == EQueryKind::Search);
	TestTrue(TEXT("nothing typed"), ParseQuery(TEXT("   ")).Kind == EQueryKind::Empty);
	TestTrue(TEXT("presets are valid"), Presets().Num() == 9 && Presets().Last().IsValid());

	// Bearings: north is -Y (yaw -90), east is +X (yaw 0) in the world frame.
	TestEqual(TEXT("east"), BearingToYawDeg(90.0), 0.0, 1e-9);
	TestEqual(TEXT("north"), BearingToYawDeg(0.0), -90.0, 1e-9);
	TestEqual(TEXT("south-west"), BearingToYawDeg(230.0), 140.0, 1e-9);
	TestEqual(TEXT("round trip"), YawToBearingDeg(BearingToYawDeg(341.23)), 341.23, 1e-9);

	const FString Helper = TEXT("[{\"title\":\"Monument Valley, Navajo, Arizona\",\"street\":\"\",\"lon\":-110.11,\"lat\":36.98},{\"title\":\"x\"}]");
	TSharedPtr<FJsonValue> Json;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Helper);
	FJsonSerializer::Deserialize(Reader, Json);
	TestEqual(TEXT("helper answers parse, incomplete ones dropped"), ParseSearchResults(Json).Num(), 1);
	return true;
}

#endif
