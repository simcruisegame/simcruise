// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruisePlaceFacts.h"

#include "Knowledge/SimCruisePlacePhotos.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileData.h"

namespace
{
	FCriticalSection CacheLock;
	TMap<FIntPoint, TArray<FSimCruisePlaceRecord>>& Cache()
	{
		static TMap<FIntPoint, TArray<FSimCruisePlaceRecord>> C;
		return C;
	}

	void Bounds(const FIntPoint& Tile, double& S, double& W, double& N, double& E)
	{
		SimCruiseGeo::TileToLonLat(FVector2d(Tile.X, Tile.Y), W, N);
		SimCruiseGeo::TileToLonLat(FVector2d(Tile.X + 1, Tile.Y + 1), E, S);
	}

	/** The first plausible year in a date ("1939", "~1930", "1930s", "c. 1925-03"). */
	int32 YearOf(const FString& Text)
	{
		for (int32 i = 0; i + 4 <= Text.Len(); ++i)
		{
			if (!FChar::IsDigit(Text[i]) || (i > 0 && FChar::IsDigit(Text[i - 1]))) continue;
			bool bFour = true;
			for (int32 k = 0; k < 4; ++k) bFour &= FChar::IsDigit(Text[i + k]);
			if (!bFour) continue;
			const int32 Y = FCString::Atoi(*Text.Mid(i, 4));
			if (Y >= 1600 && Y <= 2100) return Y;
		}
		return 0;
	}

	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* F)
	{
		FString S;
		if (O.IsValid()) O->TryGetStringField(F, S);
		return S;
	}

	void AddUnique(TArray<FString>& A, const FString& S)
	{
		if (S.IsEmpty()) return;
		for (const FString& X : A) if (X.Equals(S, ESearchCase::IgnoreCase)) return;
		A.Add(S);
	}

	/** Wikidata gives unlabelled items as their id ("Q16067363"): not a name. */
	bool IsBareId(const FString& S) { return S.Len() > 1 && S[0] == TEXT('Q') && S.Mid(1).IsNumeric(); }
}

namespace SimCruisePlaceFacts
{
	FString OverpassQuery(const FIntPoint& Tile)
	{
		double S, W, N, E;
		Bounds(Tile, S, W, N, E);
		const FString B = FString::Printf(TEXT("(%.6f,%.6f,%.6f,%.6f)"), S, W, N, E);
		return FString::Printf(TEXT("[out:json][timeout:25];(way[\"building\"]%s;relation[\"building\"]%s;node[\"wikidata\"]%s;node[\"building:architecture\"]%s;way[\"historic\"=\"district\"]%s;relation[\"historic\"=\"district\"]%s;);out tags center 3000;"),
			*B, *B, *B, *B, *B, *B);
	}

	FString WikidataQuery(const FIntPoint& Tile)
	{
		double S, W, N, E;
		Bounds(Tile, S, W, N, E);
		const double Lat = 0.5 * (S + N), Lon = 0.5 * (W + E);
		const double RadiusKm = 0.5 * SimCruiseGeo::TileSizeMetres(Lat) * 1.42 / 1000.0;
		return FString::Printf(TEXT(
			"SELECT ?item ?itemLabel ?loc ?styleLabel ?inception ?architectLabel ?heritageLabel ?materialLabel ?typeLabel ?image WHERE { "
			"SERVICE wikibase:around { ?item wdt:P625 ?loc . bd:serviceParam wikibase:center \"Point(%.6f %.6f)\"^^geo:wktLiteral . bd:serviceParam wikibase:radius \"%.3f\" . } "
			"OPTIONAL { ?item wdt:P149 ?style } OPTIONAL { ?item wdt:P571 ?inception } OPTIONAL { ?item wdt:P84 ?architect } "
			"OPTIONAL { ?item wdt:P1435 ?heritage } OPTIONAL { ?item wdt:P186 ?material } OPTIONAL { ?item wdt:P31 ?type } OPTIONAL { ?item wdt:P18 ?image } "
			"SERVICE wikibase:label { bd:serviceParam wikibase:language \"en\". } } LIMIT 3000"), Lon, Lat, RadiusKm);
	}

	const TArray<FString>& OverpassUrls()
	{
		static const TArray<FString> Urls = { TEXT("https://overpass-api.de/api/interpreter"), TEXT("https://overpass.private.coffee/api/interpreter"),
			TEXT("https://maps.mail.ru/osm/tools/overpass/api/interpreter"), TEXT("https://overpass.kumi.systems/api/interpreter") };
		return Urls;
	}

	FString OverpassUrl(int32 Attempt) { return OverpassUrls()[FMath::Abs(Attempt) % OverpassUrls().Num()]; }

	FString WikidataUrl(const FString& Query)
	{
		return TEXT("https://query.wikidata.org/sparql?format=json&query=") + FGenericPlatformHttp::UrlEncode(Query);
	}

	bool ParseOverpass(const FString& Json, TArray<FSimCruisePlaceRecord>& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			OutError = TEXT("Overpass: not JSON");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
		if (!Root->TryGetArrayField(TEXT("elements"), Elements))
		{
			OutError = TEXT("Overpass: no elements");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Elements)
		{
			const TSharedPtr<FJsonObject>* E = nullptr;
			const TSharedPtr<FJsonObject>* Tags = nullptr;
			if (!V->TryGetObject(E) || !(*E)->TryGetObjectField(TEXT("tags"), Tags)) continue;
			FSimCruisePlaceRecord R;
			const TSharedPtr<FJsonObject>* Centre = nullptr;
			if ((*E)->TryGetObjectField(TEXT("center"), Centre))
			{
				(*Centre)->TryGetNumberField(TEXT("lat"), R.Lat);
				(*Centre)->TryGetNumberField(TEXT("lon"), R.Lon);
			}
			else if (!(*E)->TryGetNumberField(TEXT("lat"), R.Lat) || !(*E)->TryGetNumberField(TEXT("lon"), R.Lon))
			{
				continue;
			}
			int64 Id = 0;
			(*E)->TryGetNumberField(TEXT("id"), Id);
			R.OsmId = FString::Printf(TEXT("%s/%lld"), *Str(*E, TEXT("type")), Id);
			const TSharedPtr<FJsonObject>& T = *Tags;
			R.Name = Str(T, TEXT("name"));
			R.Wikidata = Str(T, TEXT("wikidata"));
			AddUnique(R.Styles, Str(T, TEXT("building:architecture")).Replace(TEXT("_"), TEXT(" ")));
			AddUnique(R.Styles, Str(T, TEXT("architecture")).Replace(TEXT("_"), TEXT(" ")));
			R.Year = YearOf(Str(T, TEXT("start_date")));
			if (R.Year == 0) R.Year = YearOf(Str(T, TEXT("building:start_date")));
			if (R.Year == 0) R.Year = YearOf(Str(T, TEXT("construction_date")));
			R.Architect = Str(T, TEXT("architect"));
			const FString Heritage = Str(T, TEXT("heritage"));
			const FString HeritageOp = Str(T, TEXT("heritage:operator"));
			if (!Heritage.IsEmpty() || !Str(T, TEXT("ref:nrhp")).IsEmpty())
			{
				R.Heritage = HeritageOp.Equals(TEXT("nrhp"), ESearchCase::IgnoreCase) || !Str(T, TEXT("ref:nrhp")).IsEmpty()
					? TEXT("National Register of Historic Places") : FString::Printf(TEXT("heritage %s"), *Heritage);
			}
			R.Material = Str(T, TEXT("building:material"));
			R.Colour = Str(T, TEXT("building:colour"));
			R.RoofShape = Str(T, TEXT("roof:shape"));
			R.Levels = FCString::Atoi(*Str(T, TEXT("building:levels")));
			R.Building = Str(T, TEXT("building"));
			R.Amenity = Str(T, TEXT("tourism"));
			if (R.Amenity.IsEmpty()) R.Amenity = Str(T, TEXT("amenity"));
			if (R.Amenity.IsEmpty() && !Str(T, TEXT("shop")).IsEmpty()) R.Amenity = TEXT("shop");
			R.bDistrict = Str(T, TEXT("historic")) == TEXT("district");
			Out.Add(MoveTemp(R));
		}
		return true;
	}

	bool ParseWikidata(const FString& Json, TArray<FSimCruisePlaceRecord>& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			OutError = TEXT("Wikidata: not JSON");
			return false;
		}
		const TSharedPtr<FJsonObject>* Results = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Root->TryGetObjectField(TEXT("results"), Results) || !(*Results)->TryGetArrayField(TEXT("bindings"), Rows))
		{
			OutError = TEXT("Wikidata: no results.bindings");
			return false;
		}
		TMap<FString, int32> Index;
		for (const TSharedPtr<FJsonValue>& V : *Rows)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!V->TryGetObject(Row)) continue;
			auto Val = [&](const TCHAR* F)
			{
				const TSharedPtr<FJsonObject>* C = nullptr;
				return (*Row)->TryGetObjectField(F, C) ? Str(*C, TEXT("value")) : FString();
			};
			FString Item = Val(TEXT("item"));
			int32 Slash;
			if (!Item.FindLastChar(TEXT('/'), Slash)) continue;
			Item = Item.Mid(Slash + 1);
			int32* Found = Index.Find(Item);
			if (!Found)
			{
				// "Point(lon lat)"
				FString Loc = Val(TEXT("loc"));
				Loc.RemoveFromStart(TEXT("Point("));
				Loc.RemoveFromEnd(TEXT(")"));
				FString Lon, Lat;
				if (!Loc.Split(TEXT(" "), &Lon, &Lat)) continue;
				FSimCruisePlaceRecord R;
				R.Wikidata = Item;
				R.Lat = FCString::Atod(*Lat);
				R.Lon = FCString::Atod(*Lon);
				R.Name = Val(TEXT("itemLabel"));
				if (IsBareId(R.Name)) R.Name.Reset();
				Found = &Index.Add(Item, Out.Add(MoveTemp(R)));
			}
			FSimCruisePlaceRecord& R = Out[*Found];
			const FString Style = Val(TEXT("styleLabel"));
			if (!IsBareId(Style)) AddUnique(R.Styles, Style);
			const int32 Year = YearOf(Val(TEXT("inception")));
			if (Year > 0 && (R.Year == 0 || Year < R.Year)) R.Year = Year;
			const FString Architect = Val(TEXT("architectLabel"));
			if (R.Architect.IsEmpty() && !IsBareId(Architect)) R.Architect = Architect;
			const FString Heritage = Val(TEXT("heritageLabel"));
			if (R.Heritage.IsEmpty() && !IsBareId(Heritage)) R.Heritage = Heritage;
			const FString Material = Val(TEXT("materialLabel"));
			if (R.Material.IsEmpty() && !IsBareId(Material)) R.Material = Material;
			// The newest-looking photograph: Commons names often carry the year; keep the later one.
			const FString Image = SimCruisePlacePhotos::FileFromCommonsUrl(Val(TEXT("image")));
			if (!Image.IsEmpty() && (R.Image.IsEmpty() || Image > R.Image)) R.Image = Image;
			const FString Type = Val(TEXT("typeLabel")).ToLower();
			if (Type.Contains(TEXT("historic district")) || Type == TEXT("district") || Type.Contains(TEXT("architectural district"))) R.bDistrict = true;
			if (Type.Contains(TEXT("hotel"))) R.Building = TEXT("hotel");
		}
		return true;
	}

	void Merge(const TArray<FSimCruisePlaceRecord>& Osm, const TArray<FSimCruisePlaceRecord>& Wikidata, TArray<FSimCruisePlaceRecord>& Out)
	{
		Out = Osm;
		TMap<FString, int32> ByItem;
		for (int32 i = 0; i < Out.Num(); ++i) if (!Out[i].Wikidata.IsEmpty()) ByItem.Add(Out[i].Wikidata, i);
		for (const FSimCruisePlaceRecord& W : Wikidata)
		{
			if (const int32* I = ByItem.Find(W.Wikidata))
			{
				// The OSM element keeps its position (the footprint's centre) and gains the item's facts.
				FSimCruisePlaceRecord& R = Out[*I];
				for (const FString& S : W.Styles) AddUnique(R.Styles, S);
				if (W.Year > 0 && (R.Year == 0 || W.Year < R.Year)) R.Year = W.Year;
				if (R.Architect.IsEmpty()) R.Architect = W.Architect;
				if (R.Heritage.IsEmpty()) R.Heritage = W.Heritage;
				if (R.Material.IsEmpty()) R.Material = W.Material;
				if (R.Image.IsEmpty()) R.Image = W.Image;
				if (R.Name.IsEmpty()) R.Name = W.Name;
				R.bDistrict |= W.bDistrict;
				if (R.Building.IsEmpty() || R.Building == TEXT("yes")) { if (!W.Building.IsEmpty()) R.Building = W.Building; }
			}
			else
			{
				Out.Add(W);
			}
		}
	}

	bool IsUseful(const FSimCruisePlaceRecord& R)
	{
		return R.Styles.Num() > 0 || R.Year > 0 || !R.Architect.IsEmpty() || !R.Heritage.IsEmpty() || !R.Material.IsEmpty()
			|| !R.Colour.IsEmpty() || !R.RoofShape.IsEmpty() || !R.Image.IsEmpty() || R.bDistrict || (!R.Building.IsEmpty() && R.Building != TEXT("yes"));
	}

	FString ToJson(const FIntPoint& Tile, const TArray<FSimCruisePlaceRecord>& Records, const FString& Sources)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("tile"), SimCruiseTileName(Tile));
		Root->SetStringField(TEXT("fetched"), FDateTime::UtcNow().ToIso8601());
		Root->SetStringField(TEXT("sources"), Sources);
		Root->SetStringField(TEXT("licence"), TEXT("OpenStreetMap data (c) OpenStreetMap contributors, ODbL 1.0; Wikidata CC0"));
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FSimCruisePlaceRecord& R : Records)
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("lat"), FMath::RoundToDouble(R.Lat * 1e6) / 1e6);
			O->SetNumberField(TEXT("lon"), FMath::RoundToDouble(R.Lon * 1e6) / 1e6);
			auto Set = [&](const TCHAR* F, const FString& V) { if (!V.IsEmpty()) O->SetStringField(F, V); };
			Set(TEXT("name"), R.Name);
			Set(TEXT("osm"), R.OsmId);
			Set(TEXT("wikidata"), R.Wikidata);
			if (R.Styles.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> S;
				for (const FString& X : R.Styles) S.Add(MakeShared<FJsonValueString>(X));
				O->SetArrayField(TEXT("style"), S);
			}
			if (R.Year > 0) O->SetNumberField(TEXT("year"), R.Year);
			Set(TEXT("architect"), R.Architect);
			Set(TEXT("heritage"), R.Heritage);
			Set(TEXT("material"), R.Material);
			Set(TEXT("colour"), R.Colour);
			Set(TEXT("colour_source"), R.ColourSource);
			Set(TEXT("image"), R.Image);
			Set(TEXT("roof"), R.RoofShape);
			if (R.Levels > 0) O->SetNumberField(TEXT("levels"), R.Levels);
			Set(TEXT("building"), R.Building);
			Set(TEXT("amenity"), R.Amenity);
			if (R.bDistrict) O->SetBoolField(TEXT("district"), true);
			Arr.Add(MakeShared<FJsonValueObject>(O));
		}
		Root->SetArrayField(TEXT("records"), Arr);
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Root, Writer);
		return Text;
	}

	bool FromJson(const FString& Json, TArray<FSimCruisePlaceRecord>& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("records"), Arr))
		{
			OutError = TEXT("place records: not JSON or no records");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!V->TryGetObject(O)) continue;
			FSimCruisePlaceRecord R;
			(*O)->TryGetNumberField(TEXT("lat"), R.Lat);
			(*O)->TryGetNumberField(TEXT("lon"), R.Lon);
			R.Name = Str(*O, TEXT("name"));
			R.OsmId = Str(*O, TEXT("osm"));
			R.Wikidata = Str(*O, TEXT("wikidata"));
			(*O)->TryGetStringArrayField(TEXT("style"), R.Styles);
			(*O)->TryGetNumberField(TEXT("year"), R.Year);
			R.Architect = Str(*O, TEXT("architect"));
			R.Heritage = Str(*O, TEXT("heritage"));
			R.Material = Str(*O, TEXT("material"));
			R.Colour = Str(*O, TEXT("colour"));
			R.ColourSource = Str(*O, TEXT("colour_source"));
			R.Image = Str(*O, TEXT("image"));
			R.RoofShape = Str(*O, TEXT("roof"));
			(*O)->TryGetNumberField(TEXT("levels"), R.Levels);
			R.Building = Str(*O, TEXT("building"));
			R.Amenity = Str(*O, TEXT("amenity"));
			(*O)->TryGetBoolField(TEXT("district"), R.bDistrict);
			Out.Add(MoveTemp(R));
		}
		return true;
	}

	FString BundledPath(const FIntPoint& Tile)
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data/knowledge/places"), SimCruiseTileName(Tile) + TEXT(".json"));
	}

	FString CachePath(const FIntPoint& Tile)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Knowledge/places"), SimCruiseTileName(Tile) + TEXT(".json"));
	}

	bool ForTile(const FIntPoint& Tile, TArray<FSimCruisePlaceRecord>& Out)
	{
		{
			FScopeLock Lock(&CacheLock);
			if (const TArray<FSimCruisePlaceRecord>* C = Cache().Find(Tile)) { Out = *C; return true; }
		}
		for (const FString& Path : { CachePath(Tile), BundledPath(Tile) })
		{
			FString Text, Error;
			TArray<FSimCruisePlaceRecord> Records;
			if (!FFileHelper::LoadFileToString(Text, *Path) || !FromJson(Text, Records, Error)) continue;
			FScopeLock Lock(&CacheLock);
			Cache().Add(Tile, Records);
			Out = MoveTemp(Records);
			return true;
		}
		return false;
	}

	void Store(const FIntPoint& Tile, const TArray<FSimCruisePlaceRecord>& Records, const FString& Sources)
	{
		FFileHelper::SaveStringToFile(ToJson(Tile, Records, Sources), *CachePath(Tile), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FScopeLock Lock(&CacheLock);
		Cache().Add(Tile, Records);
	}

	bool IsKnown(const FIntPoint& Tile)
	{
		{
			FScopeLock Lock(&CacheLock);
			if (Cache().Contains(Tile)) return true;
		}
		return IFileManager::Get().FileExists(*CachePath(Tile)) || IFileManager::Get().FileExists(*BundledPath(Tile));
	}
}
