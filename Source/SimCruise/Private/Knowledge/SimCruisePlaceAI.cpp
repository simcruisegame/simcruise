// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruisePlaceAI.h"

#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileData.h"

namespace
{
	TAutoConsoleVariable<FString> CVarOllama(TEXT("simcruise.Knowledge.OllamaUrl"), TEXT("http://127.0.0.1:11434"),
		TEXT("The local Ollama server the place graph's AI reads with (CPU only)."));

	FCriticalSection AreaLock;
	TMap<FIntPoint, FSimCruiseAreaPrior>& AreaCache() { static TMap<FIntPoint, FSimCruiseAreaPrior> C; return C; }

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)) Root.Reset();
		return Root;
	}

	FString Write(const TSharedRef<FJsonObject>& Obj)
	{
		FString Text;
		FJsonSerializer::Serialize(Obj, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}

	TSharedPtr<FJsonValue> Enum(const TArray<FString>& Values)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("type"), TEXT("string"));
		TArray<TSharedPtr<FJsonValue>> E;
		for (const FString& V : Values) E.Add(MakeShared<FJsonValueString>(V));
		O->SetArrayField(TEXT("enum"), E);
		return MakeShared<FJsonValueObject>(O);
	}

	TArray<FString> StyleTokens(const FSimCruisePlaceRules& Rules)
	{
		TArray<FString> Out;
		for (const auto& KV : Rules.StyleAliases) Out.AddUnique(KV.Value);
		for (const TCHAR* Extra : { TEXT("ranch"), TEXT("minimal_traditional"), TEXT("creole_townhouse"), TEXT("vernacular") }) Out.AddUnique(Extra);
		Out.Sort();
		return Out;
	}

	TArray<FString> PaletteNames(const FSimCruisePlaceRules& Rules)
	{
		TArray<FString> Out;
		for (const auto& KV : Rules.Palettes)
		{
			FString S = KV.Key.ToString();
			S.RemoveFromStart(TEXT("palette:"));
			Out.Add(S);
		}
		Out.Sort();
		Out.Add(TEXT("unknown"));
		return Out;
	}
}

namespace SimCruisePlaceAI
{
	FString OllamaUrl() { return CVarOllama.GetValueOnAnyThread(); }

	void AreaCentre(const FIntPoint& Area, double& OutLat, double& OutLon)
	{
		// A zoom-14 tile is 4 x 4 zoom-16 tiles: its centre in zoom-16 units.
		SimCruiseGeo::TileToLonLat(FVector2d(Area.X * 4 + 2.0, Area.Y * 4 + 2.0), OutLon, OutLat);
	}

	FString GeosearchUrl(double Lat, double Lon)
	{
		return FString::Printf(TEXT("https://en.wikipedia.org/w/api.php?action=query&list=geosearch&gscoord=%.5f%%7C%.5f&gsradius=1500&gslimit=12&format=json"), Lat, Lon);
	}

	bool ParseGeosearch(const FString& Json, TArray<int64>& OutPageIds, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(Json);
		const TSharedPtr<FJsonObject>* Query = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("query"), Query) || !(*Query)->TryGetArrayField(TEXT("geosearch"), List))
		{
			OutError = TEXT("Wikipedia geosearch: no query.geosearch");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *List)
		{
			const TSharedPtr<FJsonObject>* P = nullptr;
			int64 Id = 0;
			if (V->TryGetObject(P) && (*P)->TryGetNumberField(TEXT("pageid"), Id)) OutPageIds.Add(Id);
		}
		return true;
	}

	FString ExtractsUrl(const TArray<int64>& PageIds)
	{
		TArray<FString> Ids;
		for (const int64 Id : PageIds) Ids.Add(FString::Printf(TEXT("%lld"), Id));
		return TEXT("https://en.wikipedia.org/w/api.php?action=query&prop=extracts&exintro=1&explaintext=1&exlimit=20&format=json&pageids=")
			+ FString::Join(Ids, TEXT("%7C"));
	}

	bool ParseExtracts(const FString& Json, const TArray<int64>& PageIds, TArray<FString>& OutLines, TArray<FString>& OutTitles, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(Json);
		const TSharedPtr<FJsonObject>* Query = nullptr;
		const TSharedPtr<FJsonObject>* Pages = nullptr;
		if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("query"), Query) || !(*Query)->TryGetObjectField(TEXT("pages"), Pages))
		{
			OutError = TEXT("Wikipedia extracts: no query.pages");
			return false;
		}
		for (const int64 Id : PageIds)
		{
			const TSharedPtr<FJsonObject>* P = nullptr;
			if (!(*Pages)->TryGetObjectField(FString::Printf(TEXT("%lld"), Id), P)) continue;
			FString Title, Extract;
			(*P)->TryGetStringField(TEXT("title"), Title);
			(*P)->TryGetStringField(TEXT("extract"), Extract);
			Extract = Extract.TrimStartAndEnd().Replace(TEXT("\n"), TEXT(" "));
			if (Extract.IsEmpty()) continue;
			OutLines.Add(FString::Printf(TEXT("%s: %s"), *Title, *Extract.Left(500)));
			OutTitles.Add(Title);
		}
		return true;
	}

	FString ChatRequest(const FSimCruisePlaceRules& Rules, const FString& Model, const FString& PlaceLabel, double Lat, double Lon, const TArray<FString>& Lines)
	{
		// The schema: only the graph's own words can come back.
		TSharedRef<FJsonObject> StyleItem = MakeShared<FJsonObject>();
		{
			StyleItem->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			Props->SetField(TEXT("style"), Enum(StyleTokens(Rules)));
			TSharedRef<FJsonObject> Share = MakeShared<FJsonObject>();
			Share->SetStringField(TEXT("type"), TEXT("number"));
			Props->SetObjectField(TEXT("share"), Share);
			StyleItem->SetObjectField(TEXT("properties"), Props);
			StyleItem->SetArrayField(TEXT("required"), { MakeShared<FJsonValueString>(TEXT("style")), MakeShared<FJsonValueString>(TEXT("share")) });
		}
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		{
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> Styles = MakeShared<FJsonObject>();
			Styles->SetStringField(TEXT("type"), TEXT("array"));
			Styles->SetObjectField(TEXT("items"), StyleItem);
			Props->SetObjectField(TEXT("styles"), Styles);
			TSharedRef<FJsonObject> Decade = MakeShared<FJsonObject>();
			Decade->SetStringField(TEXT("type"), TEXT("integer"));
			Props->SetObjectField(TEXT("main_decade"), Decade);
			Props->SetField(TEXT("palette"), Enum(PaletteNames(Rules)));
			Props->SetField(TEXT("roofs"), Enum({ TEXT("flat"), TEXT("pitched"), TEXT("mixed"), TEXT("unknown") }));
			Schema->SetObjectField(TEXT("properties"), Props);
			Schema->SetArrayField(TEXT("required"), { MakeShared<FJsonValueString>(TEXT("styles")), MakeShared<FJsonValueString>(TEXT("main_decade")),
				MakeShared<FJsonValueString>(TEXT("palette")), MakeShared<FJsonValueString>(TEXT("roofs")) });
		}
		FString Context = FString::Join(Lines, TEXT("\n"));
		if (Context.Len() > 5000) Context = Context.Left(5000);
		const FString System = TEXT("You read encyclopedia extracts about places near a point and describe what its ordinary buildings look like. "
			"Use only what the extracts support; where they say nothing, give an empty style list, main_decade -1, palette unknown and roofs unknown.");
		const FString User = FString::Printf(TEXT("Point: %s (%.4f, %.4f).\nExtracts:\n%s\n\nWhich architectural styles dominate the buildings here (shares 0-1, sum at most 1), "
			"the main decade they were built, the wall palette and the roof form?"), *PlaceLabel, Lat, Lon, *Context);

		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("model"), Model);
		Body->SetBoolField(TEXT("stream"), false);
		Body->SetBoolField(TEXT("think"), false);
		TSharedRef<FJsonObject> Options = MakeShared<FJsonObject>();
		Options->SetNumberField(TEXT("num_gpu"), 0); // CPU and system RAM only: the GPU keeps rendering
		Options->SetNumberField(TEXT("temperature"), 0.2);
		Options->SetNumberField(TEXT("num_ctx"), 4096);
		Options->SetNumberField(TEXT("num_thread"), 4); // leave cores for the game
		Body->SetObjectField(TEXT("options"), Options);
		Body->SetObjectField(TEXT("format"), Schema);
		TArray<TSharedPtr<FJsonValue>> Messages;
		for (const TPair<const TCHAR*, FString>& M : { TPair<const TCHAR*, FString>(TEXT("system"), System), TPair<const TCHAR*, FString>(TEXT("user"), User) })
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("role"), M.Key);
			O->SetStringField(TEXT("content"), M.Value);
			Messages.Add(MakeShared<FJsonValueObject>(O));
		}
		Body->SetArrayField(TEXT("messages"), Messages);
		return Write(Body);
	}

	bool ParseChat(const FSimCruisePlaceRules& Rules, const FString& Json, FSimCruiseAreaPrior& Out, FString& OutError)
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(Json);
		const TSharedPtr<FJsonObject>* Message = nullptr;
		FString Content;
		if (!Root.IsValid() || !Root->TryGetObjectField(TEXT("message"), Message) || !(*Message)->TryGetStringField(TEXT("content"), Content))
		{
			OutError = TEXT("Ollama: no message.content");
			return false;
		}
		Root->TryGetStringField(TEXT("model"), Out.Model);
		const TSharedPtr<FJsonObject> A = ParseObject(Content);
		if (!A.IsValid())
		{
			OutError = TEXT("Ollama: the answer is not JSON");
			return false;
		}
		const TArray<FString> Tokens = StyleTokens(Rules);
		const TArray<TSharedPtr<FJsonValue>>* Styles = nullptr;
		double Sum = 0.0;
		if (A->TryGetArrayField(TEXT("styles"), Styles))
		{
			for (const TSharedPtr<FJsonValue>& V : *Styles)
			{
				const TSharedPtr<FJsonObject>* S = nullptr;
				FString Style;
				double Share = 0.0;
				if (!V->TryGetObject(S) || !(*S)->TryGetStringField(TEXT("style"), Style) || !(*S)->TryGetNumberField(TEXT("share"), Share)) continue;
				if (Share > 1.0) Share /= 100.0; // a percentage
				if (!Tokens.Contains(Style) || Share <= 0.02) continue;
				Out.Styles.Emplace(Style, static_cast<float>(FMath::Min(Share, 1.0)));
				Sum += Share;
			}
		}
		if (Sum > 1.0) for (TPair<FString, float>& S : Out.Styles) S.Value = static_cast<float>(S.Value / Sum);
		int32 Decade = -1;
		A->TryGetNumberField(TEXT("main_decade"), Decade);
		Out.Decade = (Decade >= 1600 && Decade <= 2030) ? (Decade / 10) * 10 : 0;
		A->TryGetStringField(TEXT("palette"), Out.Palette);
		if (Out.Palette == TEXT("unknown") || !Rules.Palettes.Contains(FName(*(TEXT("palette:") + Out.Palette)))) Out.Palette.Reset();
		A->TryGetStringField(TEXT("roofs"), Out.Roofs);
		if (Out.Roofs != TEXT("flat") && Out.Roofs != TEXT("pitched") && Out.Roofs != TEXT("mixed")) Out.Roofs.Reset();
		return true;
	}

	FString PickModel(const FString& TagsJson)
	{
		const TSharedPtr<FJsonObject> Root = ParseObject(TagsJson);
		const TArray<TSharedPtr<FJsonValue>>* Models = nullptr;
		if (!Root.IsValid() || !Root->TryGetArrayField(TEXT("models"), Models)) return FString();
		FString Best;
		double BestSize = 8.0e9; // larger models do not fit in RAM beside the game
		for (const TSharedPtr<FJsonValue>& V : *Models)
		{
			const TSharedPtr<FJsonObject>* M = nullptr;
			FString Name;
			double Size = 0.0;
			if (!V->TryGetObject(M) || !(*M)->TryGetStringField(TEXT("name"), Name) || !(*M)->TryGetNumberField(TEXT("size"), Size)) continue;
			if (Name.Contains(TEXT("embed"))) continue;
			const TArray<TSharedPtr<FJsonValue>>* Caps = nullptr;
			if ((*M)->TryGetArrayField(TEXT("capabilities"), Caps))
			{
				bool bChat = false;
				for (const TSharedPtr<FJsonValue>& C : *Caps) bChat |= C->AsString() == TEXT("completion");
				if (!bChat) continue;
			}
			// Prefer a model of 1.5-6 GB: big enough to read, small enough for the CPU.
			if (Size < 1.0e9) continue;
			if (Size < BestSize) { BestSize = Size; Best = Name; }
		}
		return Best;
	}

	void ToFacts(const FSimCruiseAreaPrior& Prior, FSimCruiseFactSet& Out)
	{
		const FString Why = FString::Printf(TEXT("local AI (%s) reading Wikipedia: %s"), *Prior.Model,
			*FString::Join(TArray<FString>(Prior.Sources.GetData(), FMath::Min(3, Prior.Sources.Num())), TEXT(", ")));
		for (const TPair<FString, float>& S : Prior.Styles)
		{
			Out.Add(FName(*(TEXT("style:") + S.Key)), 0.6f * S.Value, Why);
		}
		if (Prior.Decade > 0)
		{
			FSimCruiseFactSet Era;
			SimCruisePlaceGraph::EraFacts(Prior.Decade + 5, Why, Era);
			for (const auto& KV : Era.Facts) Out.Add(KV.Key, 0.4f, Why);
		}
		if (!Prior.Palette.IsEmpty()) Out.Add(FName(*(TEXT("palette:") + Prior.Palette)), 0.4f, Why);
		if (Prior.Roofs == TEXT("flat")) Out.Add(FName(TEXT("detail:flat_roof")), 0.4f, Why);
		if (Prior.Roofs == TEXT("pitched")) Out.Add(FName(TEXT("detail:pitched_roof")), 0.4f, Why);
	}

	FString ToJson(const FIntPoint& Area, const FSimCruiseAreaPrior& P)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("area"), FString::Printf(TEXT("z14 %d_%d"), Area.X, Area.Y));
		O->SetStringField(TEXT("made"), FDateTime::UtcNow().ToIso8601());
		O->SetStringField(TEXT("model"), P.Model);
		O->SetStringField(TEXT("licence"), TEXT("Facts read from Wikipedia lead extracts (CC BY-SA 4.0) by a local language model"));
		TArray<TSharedPtr<FJsonValue>> Styles;
		for (const TPair<FString, float>& S : P.Styles)
		{
			TSharedRef<FJsonObject> X = MakeShared<FJsonObject>();
			X->SetStringField(TEXT("style"), S.Key);
			X->SetNumberField(TEXT("share"), FMath::RoundToDouble(S.Value * 100.0) / 100.0);
			Styles.Add(MakeShared<FJsonValueObject>(X));
		}
		O->SetArrayField(TEXT("styles"), Styles);
		O->SetNumberField(TEXT("main_decade"), P.Decade);
		O->SetStringField(TEXT("palette"), P.Palette);
		O->SetStringField(TEXT("roofs"), P.Roofs);
		TArray<TSharedPtr<FJsonValue>> Sources;
		for (const FString& S : P.Sources) Sources.Add(MakeShared<FJsonValueString>(S));
		O->SetArrayField(TEXT("sources"), Sources);
		return Write(O);
	}

	bool FromJson(const FString& Json, FSimCruiseAreaPrior& Out)
	{
		const TSharedPtr<FJsonObject> O = ParseObject(Json);
		if (!O.IsValid()) return false;
		const TArray<TSharedPtr<FJsonValue>>* Styles = nullptr;
		if (O->TryGetArrayField(TEXT("styles"), Styles))
		{
			for (const TSharedPtr<FJsonValue>& V : *Styles)
			{
				const TSharedPtr<FJsonObject>* S = nullptr;
				FString Style;
				double Share = 0.0;
				if (V->TryGetObject(S) && (*S)->TryGetStringField(TEXT("style"), Style) && (*S)->TryGetNumberField(TEXT("share"), Share)) Out.Styles.Emplace(Style, static_cast<float>(Share));
			}
		}
		O->TryGetNumberField(TEXT("main_decade"), Out.Decade);
		O->TryGetStringField(TEXT("palette"), Out.Palette);
		O->TryGetStringField(TEXT("roofs"), Out.Roofs);
		O->TryGetStringField(TEXT("model"), Out.Model);
		O->TryGetStringArrayField(TEXT("sources"), Out.Sources);
		return true;
	}

	FString BundledPath(const FIntPoint& Area)
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data/knowledge/areas"), FString::Printf(TEXT("%d_%d.json"), Area.X, Area.Y));
	}

	FString CachePath(const FIntPoint& Area)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Knowledge/areas"), FString::Printf(TEXT("%d_%d.json"), Area.X, Area.Y));
	}

	bool ForArea(const FIntPoint& Area, FSimCruiseAreaPrior& Out)
	{
		{
			FScopeLock Lock(&AreaLock);
			if (const FSimCruiseAreaPrior* P = AreaCache().Find(Area)) { Out = *P; return true; }
		}
		for (const FString& Path : { CachePath(Area), BundledPath(Area) })
		{
			FString Text;
			FSimCruiseAreaPrior P;
			if (!FFileHelper::LoadFileToString(Text, *Path) || !FromJson(Text, P)) continue;
			FScopeLock Lock(&AreaLock);
			AreaCache().Add(Area, P);
			Out = MoveTemp(P);
			return true;
		}
		return false;
	}

	bool IsKnown(const FIntPoint& Area)
	{
		{
			FScopeLock Lock(&AreaLock);
			if (AreaCache().Contains(Area)) return true;
		}
		return IFileManager::Get().FileExists(*CachePath(Area)) || IFileManager::Get().FileExists(*BundledPath(Area));
	}

	void Store(const FIntPoint& Area, const FSimCruiseAreaPrior& Prior)
	{
		FFileHelper::SaveStringToFile(ToJson(Area, Prior), *CachePath(Area), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FScopeLock Lock(&AreaLock);
		AreaCache().Add(Area, Prior);
	}
}
