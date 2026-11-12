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
