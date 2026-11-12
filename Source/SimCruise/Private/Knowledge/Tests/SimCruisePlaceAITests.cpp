// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruisePlaceAI.h"
#include "Knowledge/SimCruisePlaceGraph.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace SimCruisePlaceAITests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter;

	// Recorded 2026-11-12: Ollama /api/chat (qwen3.5 4B, CPU) reading 12 Wikipedia extracts round Ocean Drive.
	const TCHAR* Chat = TEXT(R"json({"model":"qwen3.5-4b-64k:latest","created_at":"2026-11-12T05:20:00Z","message":{"role":"assistant",
		"content":"{\"styles\":[{\"style\":\"art_deco\",\"share\":0.75},{\"style\":\"mimo\",\"share\":0.25},{\"style\":\"made_up\",\"share\":0.5}],\"main_decade\":1930,\"palette\":\"mimo_white\",\"roofs\":\"flat\"}"},
		"done":true,"eval_count":80})json");

	// A model that answers in percentages and an unknown palette.
	const TCHAR* ChatPercent = TEXT(R"json({"model":"m","message":{"role":"assistant",
		"content":"{\"styles\":[{\"style\":\"neoclassical\",\"share\":35},{\"style\":\"beaux_arts\",\"share\":20}],\"main_decade\":-1,\"palette\":\"unknown\",\"roofs\":\"unknown\"}"}})json");

	const TCHAR* Tags = TEXT(R"json({"models":[
		{"name":"qwen3.8:27b","size":17741872154,"capabilities":["completion","tools"]},
		{"name":"granite-embedding:278m","size":562777301,"capabilities":["embedding"]},
		{"name":"qwen3.5-4b-64k:latest","size":3400000000,"capabilities":["completion"]},
		{"name":"tiny:0.5b","size":400000000,"capabilities":["completion"]}]})json");

	const TCHAR* Geo = TEXT(R"json({"batchcomplete":"","query":{"geosearch":[{"pageid":3383851,"ns":0,"title":"Ocean Drive","lat":25.78,"lon":-80.13,"dist":12.3},
		{"pageid":1234,"ns":0,"title":"Lummus Park","lat":25.78,"lon":-80.13,"dist":120}]}})json");

	const TCHAR* Extracts = TEXT(R"json({"batchcomplete":"","query":{"pages":{"3383851":{"pageid":3383851,"ns":0,"title":"Ocean Drive",
		"extract":"Ocean Drive is a major thoroughfare in Miami Beach.\nIt is lined with Art Deco hotels."},"1234":{"pageid":1234,"ns":0,"title":"Lummus Park","extract":""}}}})json");

	bool Rules(FAutomationTestBase& Test, FSimCruisePlaceRules& Out)
	{
		FString Text, Error;
		if (!FFileHelper::LoadFileToString(Text, *SimCruisePlaceGraph::RulesPath()) || !Out.Parse(Text, Error))
		{
			Test.AddError(TEXT("place-graph.json does not load"));
			return false;
		}
		return true;
	}
}

using namespace SimCruisePlaceAITests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePlaceAITest, "SimCruise.Knowledge.LocalAI", Flags)
bool FSimCruisePlaceAITest::RunTest(const FString&)
{
	FSimCruisePlaceRules R;
	if (!Rules(*this, R)) return false;

	TArray<int64> Ids;
	FString Error;
	TestTrue(TEXT("geosearch parses"), SimCruisePlaceAI::ParseGeosearch(Geo, Ids, Error));
	TestEqual(TEXT("two pages"), Ids.Num(), 2);
	TestTrue(TEXT("extracts url lists both"), SimCruisePlaceAI::ExtractsUrl(Ids).EndsWith(TEXT("3383851%7C1234")));
	TArray<FString> Lines, Titles;
	TestTrue(TEXT("extracts parse"), SimCruisePlaceAI::ParseExtracts(Extracts, Ids, Lines, Titles, Error));
	TestEqual(TEXT("empty extracts are left out"), Lines.Num(), 1);
	TestTrue(TEXT("one line per article"), Lines.Num() == 1 && Lines[0].StartsWith(TEXT("Ocean Drive: ")) && !Lines[0].Contains(TEXT("\n")));

	const FString Body = SimCruisePlaceAI::ChatRequest(R, TEXT("m"), TEXT("Ocean Drive"), 25.78, -80.13, Lines);
	TestTrue(TEXT("CPU only"), Body.Contains(TEXT("\"num_gpu\":0")));
	TestTrue(TEXT("the schema offers the graph's styles"), Body.Contains(TEXT("\"streamline_moderne\"")) && Body.Contains(TEXT("\"deco_pastel\"")));

	FSimCruiseAreaPrior P;
	TestTrue(TEXT("answer parses"), SimCruisePlaceAI::ParseChat(R, Chat, P, Error));
	TestEqual(TEXT("an invented style is dropped"), P.Styles.Num(), 2);
	TestEqual(TEXT("decade"), P.Decade, 1930);
	TestEqual(TEXT("palette"), P.Palette, FString(TEXT("mimo_white")));
	TestEqual(TEXT("model"), P.Model, FString(TEXT("qwen3.5-4b-64k:latest")));

	FSimCruiseAreaPrior Q;
	TestTrue(TEXT("percent answer parses"), SimCruisePlaceAI::ParseChat(R, ChatPercent, Q, Error));
	TestTrue(TEXT("percentages become shares"), Q.Styles.Num() == 2 && FMath::IsNearlyEqual(Q.Styles[0].Value, 0.35f, 0.01f));
	TestTrue(TEXT("unknowns stay empty"), Q.Decade == 0 && Q.Palette.IsEmpty() && Q.Roofs.IsEmpty());

	FSimCruiseFactSet F;
	SimCruisePlaceAI::ToFacts(P, F);
	TestTrue(TEXT("weak style prior"), FMath::IsNearlyEqual(F.Get(TEXT("style:art_deco")), 0.45f, 0.01f));
	TestTrue(TEXT("weak era"), FMath::IsNearlyEqual(F.Get(TEXT("era:1930s")), 0.4f, 0.01f));
	TestTrue(TEXT("weak palette"), FMath::IsNearlyEqual(F.Get(TEXT("palette:mimo_white")), 0.4f, 0.01f));
	TestTrue(TEXT("below the deco recipe threshold on its own"), [&] { FSimCruiseFactSet G = F; G.Add(TEXT("form:lowrise"), 1.0f, TEXT("t")); R.Infer(G); return G.Get(TEXT("recipe:deco")) < 0.5f; }());

	TestEqual(TEXT("the smallest chat model that is big enough to read"), SimCruisePlaceAI::PickModel(Tags), FString(TEXT("qwen3.5-4b-64k:latest")));

	FSimCruiseAreaPrior Back;
	TestTrue(TEXT("cache round-trips"), SimCruisePlaceAI::FromJson(SimCruisePlaceAI::ToJson(FIntPoint(4544, 6976), P), Back) && Back.Styles.Num() == 2 && Back.Decade == 1930);
	TestEqual(TEXT("z14 area of a z16 tile"), SimCruisePlaceAI::AreaOf(FIntPoint(18179, 27907)), FIntPoint(4544, 6976));
	return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Knowledge/SimCruisePlacePhotos.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePlacePhotoTest, "SimCruise.Knowledge.PhotoColour", SimCruisePlaceAITests::Flags)
bool FSimCruisePlacePhotoTest::RunTest(const FString&)
{
	TestEqual(TEXT("file from a Wikidata P18 url"), SimCruisePlacePhotos::FileFromCommonsUrl(TEXT("http://commons.wikimedia.org/wiki/Special:FilePath/Tides%20Hotel%20Miami%20Beach.jpg")),
		FString(TEXT("Tides Hotel Miami Beach.jpg")));

	// Recorded 2026-11-12 (shortened): Commons imageinfo for a 2012 photo and a 1940s postcard.
	const TCHAR* Modern = TEXT(R"json({"query":{"pages":{"1":{"title":"File:Tides Hotel Miami Beach.jpg","imageinfo":[{"thumburl":"https://upload.wikimedia.org/x/480px-Tides.jpg","descriptionurl":"https://commons.wikimedia.org/wiki/File:Tides.jpg",
		"extmetadata":{"DateTimeOriginal":{"value":"2012-11-25"},"LicenseShortName":{"value":"CC BY-SA 3.0"},"Artist":{"value":"<a href=\"x\">Someone</a>"},"Categories":{"value":"Tides Hotel, Miami Beach|Self-published work"}}}]}}}})json");
	const TCHAR* Postcard = TEXT(R"json({"query":{"pages":{"2":{"title":"File:Miami Beach - The Tides Hotel 01.jpg","imageinfo":[{"thumburl":"https://upload.wikimedia.org/x/480px-T01.jpg",
		"extmetadata":{"DateTimeOriginal":{"value":"circa 1940<div style=\"display: none;\">date QS:P571</div>"},"LicenseShortName":{"value":"Public domain"},"Categories":{"value":"Postcards of hotels in Miami Beach, Florida"}}}]}}}})json");
	FSimCruisePhotoInfo A, B;
	FString Error;
	TestTrue(TEXT("modern parses"), SimCruisePlacePhotos::ParseImageInfo(Modern, A, Error));
	TestEqual(TEXT("year"), A.Year, 2012);
	TestEqual(TEXT("artist without tags"), A.Artist, FString(TEXT("Someone")));
	TestTrue(TEXT("a 2012 colour photo is used"), SimCruisePlacePhotos::IsUsable(A));
	TestTrue(TEXT("postcard parses"), SimCruisePlacePhotos::ParseImageInfo(Postcard, B, Error));
	TestFalse(TEXT("a 1940 postcard is not"), SimCruisePlacePhotos::IsUsable(B));

	// A synthetic photo: blue sky on top, a pale yellow wall in the middle with dark
	// windows and a palm, grey street at the bottom.
	const int32 W = 320, H = 240;
	TArray<FColor> Px;
	Px.SetNum(W * H);
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			FColor C(240, 228, 170); // the wall
			if (y < H / 4) C = FColor(110, 160, 230);
			else if (y > H * 4 / 5) C = FColor(120, 120, 118);
			else if ((x / 20) % 3 == 0 && (y / 15) % 2 == 0) C = FColor(40, 50, 60); // windows
			else if (x > W * 3 / 4 && x < W * 4 / 5) C = FColor(60, 120, 50); // a palm
			Px[y * W + x] = C;
		}
	}
	FColor Wall;
	float Share = 0.0f;
	TestTrue(TEXT("a wall is found"), SimCruisePlacePhotos::WallColour(Px, W, H, Wall, Share));
	TestTrue(TEXT("it is the yellow stucco"), FMath::Abs(Wall.R - 240) < 12 && FMath::Abs(Wall.G - 228) < 12 && FMath::Abs(Wall.B - 170) < 12);
	TestTrue(TEXT("it covers most of what is left"), Share > 0.6f);

	// Black and white: no colour.
	for (FColor& C : Px) { const uint8 L = static_cast<uint8>((C.R + C.G + C.B) / 3); C = FColor(L, L, L); }
	TestFalse(TEXT("a grey picture gives none"), SimCruisePlacePhotos::WallColour(Px, W, H, Wall, Share));

	FSimCruisePlaceRecord R;
	SimCruisePlacePhotos::Apply(R, FColor(240, 228, 170), 0.7f, A);
	TestEqual(TEXT("hex"), R.Colour, FString(TEXT("#F0E4AA")));
	TestTrue(TEXT("credit"), R.ColourSource.Contains(TEXT("Tides Hotel Miami Beach.jpg")) && R.ColourSource.Contains(TEXT("CC BY-SA 3.0")));
	return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Knowledge/SimCruiseFurnitureData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseFurnitureDataTest, "SimCruise.Knowledge.StreetFurniture", SimCruisePlaceAITests::Flags)
bool FSimCruiseFurnitureDataTest::RunTest(const FString&)
{
	// Recorded 2026-11-12 (shortened): Overpass furniture nodes on Ocean Drive, and a building way.
	const TCHAR* Json = TEXT(R"json({"elements":[
		{"type":"node","id":1,"lat":25.7801,"lon":-80.1301,"tags":{"amenity":"bench","backrest":"yes"}},
		{"type":"node","id":2,"lat":25.7802,"lon":-80.1302,"tags":{"emergency":"fire_hydrant","fire_hydrant:type":"pillar","colour":"yellow"}},
		{"type":"node","id":3,"lat":25.7803,"lon":-80.1303,"tags":{"emergency":"fire_hydrant","fire_hydrant:type":"underground"}},
		{"type":"node","id":4,"lat":25.7804,"lon":-80.1304,"tags":{"highway":"bus_stop","shelter":"yes","bench":"yes","name":"Ocean Dr & 10 St"}},
		{"type":"node","id":5,"lat":25.7805,"lon":-80.1305,"tags":{"amenity":"cafe","name":"News Cafe"}},
		{"type":"way","id":6,"center":{"lat":25.78,"lon":-80.13},"tags":{"building":"yes"}}]})json");
	TArray<FSimCruiseFurniture> Items;
	FString Error;
	TestTrue(TEXT("parses"), SimCruiseFurniture::ParseOverpass(Json, Items, Error));
	TestEqual(TEXT("bench, pillar hydrant, bus stop (not the underground hydrant, the cafe or the building)"), Items.Num(), 3);
	if (Items.Num() == 3)
	{
		TestEqual(TEXT("bench"), Items[0].Kind, FName(TEXT("bench")));
		TestEqual(TEXT("hydrant colour"), Items[1].Colour, FString(TEXT("yellow")));
		TestTrue(TEXT("shelter and bench"), Items[2].bShelter && Items[2].bBench);
	}
	TArray<FSimCruiseFurniture> Back;
	TestTrue(TEXT("round trip"), SimCruiseFurniture::FromJson(SimCruiseFurniture::ToJson(FIntPoint(1, 2), Items), Back) && Back.Num() == Items.Num());
	TestTrue(TEXT("kept to 1 cm"), Back.Num() == 3 && FMath::Abs(Back[1].Lat - 25.7802) < 1e-6 && Back[2].bShelter);
	TestTrue(TEXT("the Overpass lines ask for hydrants"), SimCruiseFurniture::OverpassLines(TEXT("(1,2,3,4)")).Contains(TEXT("fire_hydrant")));
	return true;
}
#endif
