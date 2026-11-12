// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruiseFurnitureData.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileData.h"

namespace
{
	FCriticalSection FurnitureLock;
	TMap<FIntPoint, TArray<FSimCruiseFurniture>>& FurnitureCache() { static TMap<FIntPoint, TArray<FSimCruiseFurniture>> C; return C; }

	FString Tag(const TSharedPtr<FJsonObject>& Tags, const TCHAR* Key)
	{
		FString V;
		Tags->TryGetStringField(Key, V);
		return V;
	}
}

namespace SimCruiseFurniture
{
	FString OverpassLines(const FString& B)
	{
		return FString::Printf(TEXT("node[\"amenity\"~\"^(bench|waste_basket|bicycle_parking|post_box)$\"]%s;node[\"emergency\"=\"fire_hydrant\"]%s;")
			TEXT("node[\"barrier\"=\"bollard\"]%s;node[\"highway\"=\"bus_stop\"]%s;node[\"leisure\"=\"picnic_table\"]%s;"), *B, *B, *B, *B, *B);
	}

	bool ParseOverpass(const FString& Json, TArray<FSimCruiseFurniture>& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("elements"), Elements))
		{
			OutError = TEXT("Overpass: no elements");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Elements)
		{
			const TSharedPtr<FJsonObject>* E = nullptr;
			const TSharedPtr<FJsonObject>* Tags = nullptr;
			FString Type;
			if (!V->TryGetObject(E) || !(*E)->TryGetStringField(TEXT("type"), Type) || Type != TEXT("node") || !(*E)->TryGetObjectField(TEXT("tags"), Tags)) continue;
			FSimCruiseFurniture F;
			if (!(*E)->TryGetNumberField(TEXT("lat"), F.Lat) || !(*E)->TryGetNumberField(TEXT("lon"), F.Lon)) continue;
			const FString Amenity = Tag(*Tags, TEXT("amenity"));
			if (Amenity == TEXT("bench") || Amenity == TEXT("waste_basket") || Amenity == TEXT("bicycle_parking") || Amenity == TEXT("post_box")) F.Kind = FName(*Amenity);
			else if (Tag(*Tags, TEXT("emergency")) == TEXT("fire_hydrant"))
			{
				// Underground and wall hydrants are not posts by the kerb.
				const FString HType = Tag(*Tags, TEXT("fire_hydrant:type"));
				if (HType == TEXT("underground") || HType == TEXT("wall") || HType == TEXT("pipe")) continue;
				F.Kind = FName(TEXT("fire_hydrant"));
				F.Colour = Tag(*Tags, TEXT("colour"));
			}
			else if (Tag(*Tags, TEXT("barrier")) == TEXT("bollard")) F.Kind = FName(TEXT("bollard"));
			else if (Tag(*Tags, TEXT("highway")) == TEXT("bus_stop"))
			{
				F.Kind = FName(TEXT("bus_stop"));
				F.bShelter = Tag(*Tags, TEXT("shelter")) == TEXT("yes");
				F.bBench = Tag(*Tags, TEXT("bench")) == TEXT("yes");
			}
			else if (Tag(*Tags, TEXT("leisure")) == TEXT("picnic_table")) F.Kind = FName(TEXT("picnic_table"));
			else continue;
			if (F.Colour.IsEmpty()) F.Colour = Tag(*Tags, TEXT("colour"));
			const FString Dir = Tag(*Tags, TEXT("direction"));
			if (!Dir.IsEmpty() && Dir.IsNumeric()) F.DirectionDeg = FCString::Atof(*Dir);
			Out.Add(MoveTemp(F));
		}
		return true;
	}

	FString ToJson(const FIntPoint& Tile, const TArray<FSimCruiseFurniture>& Items)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("tile"), SimCruiseTileName(Tile));
		Root->SetStringField(TEXT("licence"), TEXT("OpenStreetMap data (c) OpenStreetMap contributors, ODbL 1.0"));
		Root->SetStringField(TEXT("format"), TEXT("[lat, lon, kind, flags (s shelter, b bench), direction or -1, colour]"));
		TArray<TSharedPtr<FJsonValue>> Rows;
		for (const FSimCruiseFurniture& F : Items)
		{
			TArray<TSharedPtr<FJsonValue>> Row;
			Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(F.Lat * 1e7) / 1e7));
			Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(F.Lon * 1e7) / 1e7));
			Row.Add(MakeShared<FJsonValueString>(F.Kind.ToString()));
			Row.Add(MakeShared<FJsonValueString>(FString(F.bShelter ? TEXT("s") : TEXT("")) + (F.bBench ? TEXT("b") : TEXT(""))));
			Row.Add(MakeShared<FJsonValueNumber>(F.DirectionDeg));
			Row.Add(MakeShared<FJsonValueString>(F.Colour));
			Rows.Add(MakeShared<FJsonValueArray>(Row));
		}
		Root->SetArrayField(TEXT("items"), Rows);
		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}

	bool FromJson(const FString& Json, TArray<FSimCruiseFurniture>& Out)
	{
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("items"), Rows)) return false;
		for (const TSharedPtr<FJsonValue>& V : *Rows)
		{
			const TArray<TSharedPtr<FJsonValue>>* Row = nullptr;
			if (!V->TryGetArray(Row) || Row->Num() < 6) continue;
			FSimCruiseFurniture F;
			F.Lat = (*Row)[0]->AsNumber();
			F.Lon = (*Row)[1]->AsNumber();
			F.Kind = FName(*(*Row)[2]->AsString());
			const FString Flags = (*Row)[3]->AsString();
			F.bShelter = Flags.Contains(TEXT("s"));
			F.bBench = Flags.Contains(TEXT("b"));
			F.DirectionDeg = static_cast<float>((*Row)[4]->AsNumber());
			F.Colour = (*Row)[5]->AsString();
			Out.Add(MoveTemp(F));
		}
		return true;
	}

	FString BundledPath(const FIntPoint& Tile)
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data/knowledge/furniture"), SimCruiseTileName(Tile) + TEXT(".json"));
	}

	FString CachePath(const FIntPoint& Tile)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Knowledge/furniture"), SimCruiseTileName(Tile) + TEXT(".json"));
	}

	bool ForTile(const FIntPoint& Tile, TArray<FSimCruiseFurniture>& Out)
	{
		{
			FScopeLock Lock(&FurnitureLock);
			if (const TArray<FSimCruiseFurniture>* C = FurnitureCache().Find(Tile)) { Out = *C; return true; }
		}
		for (const FString& Path : { CachePath(Tile), BundledPath(Tile) })
		{
			FString Text;
			TArray<FSimCruiseFurniture> Items;
			if (!FFileHelper::LoadFileToString(Text, *Path) || !FromJson(Text, Items)) continue;
			FScopeLock Lock(&FurnitureLock);
			FurnitureCache().Add(Tile, Items);
			Out = MoveTemp(Items);
			return true;
		}
		return false;
	}

	void Store(const FIntPoint& Tile, const TArray<FSimCruiseFurniture>& Items, bool bSave)
	{
		if (bSave) FFileHelper::SaveStringToFile(ToJson(Tile, Items), *CachePath(Tile), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FScopeLock Lock(&FurnitureLock);
		FurnitureCache().Add(Tile, Items);
	}
}
