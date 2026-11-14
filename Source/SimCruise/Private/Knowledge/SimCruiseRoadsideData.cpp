// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruiseRoadsideData.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SimCruiseGeo.h"

namespace
{
	FCriticalSection RoadsideLock;
	TMap<FIntPoint, FSimCruiseRoadside>& RoadsideCache() { static TMap<FIntPoint, FSimCruiseRoadside> C; return C; }
	/** Tiles whose cache file was looked for and not found (Store clears an entry). */
	TSet<FIntPoint>& MissingFiles() { static TSet<FIntPoint> M; return M; }

	FString Tag(const TSharedPtr<FJsonObject>& Tags, const TCHAR* Key)
	{
		FString V;
		if (Tags.IsValid()) Tags->TryGetStringField(Key, V);
		return V;
	}

	/** The highway classes a car drives (the tile's road ribbons). */
	const TCHAR* DrivableRegex = TEXT("^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street|service|road|motorway_link|trunk_link|primary_link|secondary_link|tertiary_link)$");

	bool IsDrivable(const FString& Highway)
	{
		static const TSet<FString> Set = { TEXT("motorway"), TEXT("trunk"), TEXT("primary"), TEXT("secondary"), TEXT("tertiary"), TEXT("unclassified"),
			TEXT("residential"), TEXT("living_street"), TEXT("service"), TEXT("road"), TEXT("motorway_link"), TEXT("trunk_link"), TEXT("primary_link"),
			TEXT("secondary_link"), TEXT("tertiary_link") };
		return Set.Contains(Highway);
	}

	/**
	 * A per-side tag in OSM's forms: key=both|left|right|yes|no|none|separate, key:both=*,
	 * key:left=*, key:right=* (the side ones win). Out[0] left, Out[1] right of the way's
	 * drawing direction; empty when not mapped.
	 */
	void Sides(const TSharedPtr<FJsonObject>& T, const FString& Key, FString Out[2], bool bValueIsSide)
	{
		Out[0].Reset();
		Out[1].Reset();
		const FString V = Tag(T, *Key);
		if (bValueIsSide)
		{
			if (V == TEXT("both") || V == TEXT("yes")) { Out[0] = Out[1] = TEXT("yes"); }
			else if (V == TEXT("left")) { Out[0] = TEXT("yes"); Out[1] = TEXT("no"); }
			else if (V == TEXT("right")) { Out[0] = TEXT("no"); Out[1] = TEXT("yes"); }
			else if (V == TEXT("no") || V == TEXT("none")) { Out[0] = Out[1] = TEXT("no"); }
			else if (V == TEXT("separate")) { Out[0] = Out[1] = TEXT("separate"); }
		}
		else if (!V.IsEmpty())
		{
			Out[0] = Out[1] = V;
		}
		const FString Both = Tag(T, *(Key + TEXT(":both")));
		if (!Both.IsEmpty()) Out[0] = Out[1] = Both;
		const FString L = Tag(T, *(Key + TEXT(":left"))), R = Tag(T, *(Key + TEXT(":right")));
		if (!L.IsEmpty()) Out[0] = L;
		if (!R.IsEmpty()) Out[1] = R;
	}

	/** Splits an Overpass "geometry" array at its gaps (nodes outside the clip box come back as null). */
	void Polylines(const TSharedPtr<FJsonObject>& E, TArray<TArray<FVector2d>>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Geometry = nullptr;
		if (!E->TryGetArrayField(TEXT("geometry"), Geometry)) return;
		TArray<FVector2d> Run;
		for (const TSharedPtr<FJsonValue>& G : *Geometry)
		{
			const TSharedPtr<FJsonObject>* P = nullptr;
			double Lat = 0.0, Lon = 0.0;
			if (G.IsValid() && G->TryGetObject(P) && (*P)->TryGetNumberField(TEXT("lat"), Lat) && (*P)->TryGetNumberField(TEXT("lon"), Lon))
			{
				Run.Add(FVector2d(Lon, Lat));
				continue;
			}
			if (Run.Num() >= 2) Out.Add(MoveTemp(Run));
			Run.Reset();
		}
		if (Run.Num() >= 2) Out.Add(MoveTemp(Run));
	}

	TArray<TSharedPtr<FJsonValue>> Flat(const TArray<FVector2d>& LonLat)
	{
		TArray<TSharedPtr<FJsonValue>> A;
		A.Reserve(LonLat.Num() * 2);
		for (const FVector2d& P : LonLat)
		{
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.X * 1e7) / 1e7));
			A.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Y * 1e7) / 1e7));
		}
		return A;
	}

	void Unflat(const TSharedPtr<FJsonValue>& V, TArray<FVector2d>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (!V.IsValid() || !V->TryGetArray(A)) return;
		for (int32 i = 0; i + 1 < A->Num(); i += 2) Out.Add(FVector2d((*A)[i]->AsNumber(), (*A)[i + 1]->AsNumber()));
	}

	uint32 HashString(uint32 H, const FString& S) { return HashCombine(H, GetTypeHash(S)); }
}

uint32 FSimCruiseRoadside::Hash() const
{
	uint32 H = 0x52D51DE;
	auto Way = [&H](const FSimCruiseRoadsideWay& W)
	{
		H = HashCombine(H, GetTypeHash(W.Id));
		H = HashCombine(H, GetTypeHash(W.Kind));
		for (const FString* S : { &W.Name, &W.Surface, &W.Smoothness, &W.Sidewalk[0], &W.Sidewalk[1], &W.Shoulder[0], &W.Shoulder[1], &W.Kerb[0], &W.Kerb[1], &W.Lit }) H = HashString(H, *S);
		H = HashCombine(H, GetTypeHash(W.MaxSpeedKph));
		H = HashCombine(H, GetTypeHash(W.LonLat.Num()));
		// Coordinates as the cache file keeps them (1e-7 degrees), so a reloaded file hashes the same.
		for (const FVector2d& P : W.LonLat) H = HashCombine(H, HashCombine(GetTypeHash(FMath::RoundToInt64(P.X * 1e7)), GetTypeHash(FMath::RoundToInt64(P.Y * 1e7))));
	};
	for (const FSimCruiseRoadsideWay& W : Roads) Way(W);
	for (const FSimCruiseRoadsideWay& W : Barriers) Way(W);
	for (const FSimCruiseRoadsideWay& W : Lines) Way(W);
	for (const FSimCruiseRoadsidePoint& P : Points)
	{
		H = HashCombine(H, GetTypeHash(P.Id));
		H = HashCombine(H, GetTypeHash(P.Kind));
		H = HashString(HashString(HashString(H, P.Name), P.Brand), P.Category);
		H = HashCombine(H, HashCombine(GetTypeHash(FMath::RoundToInt64(P.Lat * 1e7)), GetTypeHash(FMath::RoundToInt64(P.Lon * 1e7))));
	}
	return H;
}

namespace SimCruiseRoadsideData
{
	FString OverpassPointLines(const FString& B)
	{
		return FString::Printf(TEXT("node[\"highway\"~\"^(street_lamp|traffic_signals|stop|give_way)$\"]%s;node[\"power\"~\"^(pole|tower)$\"]%s;")
			TEXT("node[\"man_made\"=\"utility_pole\"]%s;"), *B, *B, *B);
	}

	FString OverpassBusinessStatement(const FString& B)
	{
		// Its own union and cap, so a busy downtown cannot push the buildings past theirs.
		return FString::Printf(TEXT("(nwr[\"shop\"][\"name\"]%s;nwr[\"amenity\"][\"name\"]%s;nwr[\"office\"][\"name\"]%s;);out tags center 3000;"), *B, *B, *B);
	}

	FString OverpassGeometryStatements(const FString& B)
	{
		// Geometry clipped to the tile (out geom(box)): a long road returns only its piece here.
		// Barriers by the tile's box (an around-filter would walk every road's whole length).
		return FString::Printf(TEXT("way[\"highway\"~\"%s\"]%s;out tags geom%s;")
			TEXT("(way[\"barrier\"~\"^(guard_rail|wall|retaining_wall)$\"]%s;way[\"power\"~\"^(line|minor_line)$\"]%s;);out tags geom%s;"),
			DrivableRegex, *B, *B, *B, *B, *B);
	}

	int32 ParseMaxSpeedKph(const FString& Text)
	{
		FString T = Text.TrimStartAndEnd().ToLower();
		if (T.IsEmpty() || !FChar::IsDigit(T[0])) return 0; // "none", "signals", "walk", "US:urban" ...
		int32 Digits = 0;
		while (Digits < T.Len() && (FChar::IsDigit(T[Digits]) || T[Digits] == TEXT('.'))) ++Digits;
		const double V = FCString::Atod(*T.Left(Digits));
		const bool bMph = T.Contains(TEXT("mph"));
		return FMath::RoundToInt(bMph ? V * 1.609344 : V);
	}

	bool ParseOverpass(const FString& Json, FSimCruiseRoadside& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("elements"), Elements))
		{
			OutError = TEXT("Overpass: no elements");
			return false;
		}
		TSet<int64> SeenBusiness;
		for (const TSharedPtr<FJsonValue>& V : *Elements)
		{
			const TSharedPtr<FJsonObject>* E = nullptr;
			const TSharedPtr<FJsonObject>* TagsPtr = nullptr;
			FString Type;
			if (!V->TryGetObject(E) || !(*E)->TryGetStringField(TEXT("type"), Type) || !(*E)->TryGetObjectField(TEXT("tags"), TagsPtr)) continue;
			const TSharedPtr<FJsonObject>& T = *TagsPtr;
			int64 Id = 0;
			(*E)->TryGetNumberField(TEXT("id"), Id);

			// Ways with geometry: roads, barriers, power lines.
			if (Type == TEXT("way") && (*E)->HasField(TEXT("geometry")))
			{
				TArray<TArray<FVector2d>> Lines;
				Polylines(*E, Lines);
				if (Lines.Num() == 0) continue;
				FSimCruiseRoadsideWay W;
				W.Id = Id;
				TArray<FSimCruiseRoadsideWay>* Into = nullptr;
				const FString Highway = Tag(T, TEXT("highway"));
				const FString Barrier = Tag(T, TEXT("barrier"));
				const FString Power = Tag(T, TEXT("power"));
				if (IsDrivable(Highway))
				{
					W.Kind = FName(*Highway);
					W.Name = Tag(T, TEXT("name"));
					W.Surface = Tag(T, TEXT("surface"));
					W.Smoothness = Tag(T, TEXT("smoothness"));
					Sides(T, TEXT("sidewalk"), W.Sidewalk, true);
					Sides(T, TEXT("shoulder"), W.Shoulder, true);
					Sides(T, TEXT("kerb"), W.Kerb, false);
					// The kerb is often mapped on the sidewalk side: sidewalk:left:kerb=raised.
					for (int32 s = 0; s < 2; ++s)
					{
						if (!W.Kerb[s].IsEmpty()) continue;
						W.Kerb[s] = Tag(T, s == 0 ? TEXT("sidewalk:left:kerb") : TEXT("sidewalk:right:kerb"));
						if (W.Kerb[s].IsEmpty()) W.Kerb[s] = Tag(T, TEXT("sidewalk:both:kerb"));
					}
					W.Lit = Tag(T, TEXT("lit"));
					W.MaxSpeedKph = ParseMaxSpeedKph(Tag(T, TEXT("maxspeed")));
					Into = &Out.Roads;
				}
				else if (Barrier == TEXT("guard_rail") || Barrier == TEXT("fence") || Barrier == TEXT("wall") || Barrier == TEXT("retaining_wall"))
				{
					W.Kind = FName(*Barrier);
					Into = &Out.Barriers;
				}
				else if (Power == TEXT("line") || Power == TEXT("minor_line"))
				{
					W.Kind = FName(*Power);
					Into = &Out.Lines;
				}
				if (!Into) continue;
				for (TArray<FVector2d>& L : Lines)
				{
					FSimCruiseRoadsideWay Piece = W;
					Piece.LonLat = MoveTemp(L);
					Into->Add(MoveTemp(Piece));
				}
				continue;
			}

			// Points: nodes, and named businesses mapped as building outlines (their centre).
			FSimCruiseRoadsidePoint P;
			P.Id = Id;
			const TSharedPtr<FJsonObject>* Centre = nullptr;
			if ((*E)->TryGetObjectField(TEXT("center"), Centre))
			{
				(*Centre)->TryGetNumberField(TEXT("lat"), P.Lat);
				(*Centre)->TryGetNumberField(TEXT("lon"), P.Lon);
			}
			else if (!(*E)->TryGetNumberField(TEXT("lat"), P.Lat) || !(*E)->TryGetNumberField(TEXT("lon"), P.Lon))
			{
				continue;
			}
			const bool bNode = Type == TEXT("node");
			const FString Highway = Tag(T, TEXT("highway"));
			const FString Power = Tag(T, TEXT("power"));
			const FString Name = Tag(T, TEXT("name"));
			if (bNode && (Highway == TEXT("street_lamp") || Highway == TEXT("traffic_signals") || Highway == TEXT("stop") || Highway == TEXT("give_way")))
			{
				P.Kind = FName(*Highway);
			}
			else if (bNode && (Power == TEXT("pole") || Power == TEXT("tower")))
			{
				P.Kind = FName(*(TEXT("power_") + Power));
			}
			else if (bNode && Tag(T, TEXT("man_made")) == TEXT("utility_pole"))
			{
				P.Kind = FName(TEXT("utility_pole"));
			}
			else if (!Name.IsEmpty())
			{
				FString Category;
				for (const TCHAR* Key : { TEXT("shop"), TEXT("amenity"), TEXT("office") })
				{
					const FString Value = Tag(T, Key);
					if (!Value.IsEmpty()) { Category = FString::Printf(TEXT("%s=%s"), Key, *Value); break; }
				}
				if (Category.IsEmpty()) continue;
				// The same place answers once per query line (shop and amenity): keep one.
				const int64 Key = (Type == TEXT("node") ? 0 : Type == TEXT("way") ? (1ll << 60) : (2ll << 60)) ^ Id;
				if (SeenBusiness.Contains(Key)) continue;
				SeenBusiness.Add(Key);
				P.Kind = FName(TEXT("business"));
				P.Name = Name;
				P.Category = Category;
				P.Brand = Tag(T, TEXT("brand"));
				P.BrandWikidata = Tag(T, TEXT("brand:wikidata"));
			}
			else
			{
				continue;
			}
			P.Colour = Tag(T, TEXT("colour"));
			if (P.Colour.IsEmpty()) P.Colour = Tag(T, TEXT("brand:colour"));
			P.Direction = Tag(T, TEXT("direction"));
			if (P.Direction.IsEmpty()) P.Direction = Tag(T, TEXT("traffic_sign:direction"));
			Out.Points.Add(MoveTemp(P));
		}
		return true;
	}

	FString ToJson(const FIntPoint& Tile, const FSimCruiseRoadside& D)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("tile"), SimCruiseTileName(Tile));
		Root->SetStringField(TEXT("licence"), TEXT("OpenStreetMap data (c) OpenStreetMap contributors, ODbL 1.0"));
		Root->SetStringField(TEXT("format"), TEXT("roads [id, highway, name, surface, smoothness, sidewalk L|R, shoulder L|R, kerb L|R, lit, maxspeed km/h, [lon, lat ...]]; ")
			TEXT("barriers and lines [id, kind, [lon, lat ...]]; points [id, lat, lon, kind, name, category, brand, brand:wikidata, colour, direction]"));
		auto Pair = [](const FString P[2]) { return P[0] + TEXT("|") + P[1]; };
		TArray<TSharedPtr<FJsonValue>> Roads;
		for (const FSimCruiseRoadsideWay& W : D.Roads)
		{
			TArray<TSharedPtr<FJsonValue>> Row;
			Row.Add(MakeShared<FJsonValueNumber>(static_cast<double>(W.Id)));
			Row.Add(MakeShared<FJsonValueString>(W.Kind.ToString()));
			for (const FString& S : { W.Name, W.Surface, W.Smoothness, Pair(W.Sidewalk), Pair(W.Shoulder), Pair(W.Kerb), W.Lit }) Row.Add(MakeShared<FJsonValueString>(S));
			Row.Add(MakeShared<FJsonValueNumber>(W.MaxSpeedKph));
			Row.Add(MakeShared<FJsonValueArray>(Flat(W.LonLat)));
			Roads.Add(MakeShared<FJsonValueArray>(Row));
		}
		Root->SetArrayField(TEXT("roads"), Roads);
		for (const TPair<const TCHAR*, const TArray<FSimCruiseRoadsideWay>*>& L : { TPair<const TCHAR*, const TArray<FSimCruiseRoadsideWay>*>(TEXT("barriers"), &D.Barriers),
			TPair<const TCHAR*, const TArray<FSimCruiseRoadsideWay>*>(TEXT("lines"), &D.Lines) })
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			for (const FSimCruiseRoadsideWay& W : *L.Value)
			{
				TArray<TSharedPtr<FJsonValue>> Row;
				Row.Add(MakeShared<FJsonValueNumber>(static_cast<double>(W.Id)));
				Row.Add(MakeShared<FJsonValueString>(W.Kind.ToString()));
				Row.Add(MakeShared<FJsonValueArray>(Flat(W.LonLat)));
				Rows.Add(MakeShared<FJsonValueArray>(Row));
			}
			Root->SetArrayField(L.Key, Rows);
		}
		TArray<TSharedPtr<FJsonValue>> Points;
		for (const FSimCruiseRoadsidePoint& P : D.Points)
		{
			TArray<TSharedPtr<FJsonValue>> Row;
			Row.Add(MakeShared<FJsonValueNumber>(static_cast<double>(P.Id)));
			Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Lat * 1e7) / 1e7));
			Row.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(P.Lon * 1e7) / 1e7));
			for (const FString& S : { P.Kind.ToString(), P.Name, P.Category, P.Brand, P.BrandWikidata, P.Colour, P.Direction }) Row.Add(MakeShared<FJsonValueString>(S));
			Points.Add(MakeShared<FJsonValueArray>(Row));
		}
		Root->SetArrayField(TEXT("points"), Points);
		FString Text;
		FJsonSerializer::Serialize(Root, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}

	bool FromJson(const FString& Json, FSimCruiseRoadside& Out)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()) return false;
		auto Split = [](const FString& S, FString Out2[2]) { if (!S.Split(TEXT("|"), &Out2[0], &Out2[1])) { Out2[0] = S; Out2[1].Reset(); } };
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (Root->TryGetArrayField(TEXT("roads"), Rows))
		{
			for (const TSharedPtr<FJsonValue>& V : *Rows)
			{
				const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
				if (!V->TryGetArray(R) || R->Num() < 11) continue;
				FSimCruiseRoadsideWay W;
				W.Id = static_cast<int64>((*R)[0]->AsNumber());
				W.Kind = FName(*(*R)[1]->AsString());
				W.Name = (*R)[2]->AsString();
				W.Surface = (*R)[3]->AsString();
				W.Smoothness = (*R)[4]->AsString();
				Split((*R)[5]->AsString(), W.Sidewalk);
				Split((*R)[6]->AsString(), W.Shoulder);
				Split((*R)[7]->AsString(), W.Kerb);
				W.Lit = (*R)[8]->AsString();
				W.MaxSpeedKph = static_cast<int32>((*R)[9]->AsNumber());
				Unflat((*R)[10], W.LonLat);
				Out.Roads.Add(MoveTemp(W));
			}
		}
		for (const TPair<const TCHAR*, TArray<FSimCruiseRoadsideWay>*>& L : { TPair<const TCHAR*, TArray<FSimCruiseRoadsideWay>*>(TEXT("barriers"), &Out.Barriers),
			TPair<const TCHAR*, TArray<FSimCruiseRoadsideWay>*>(TEXT("lines"), &Out.Lines) })
		{
			if (!Root->TryGetArrayField(L.Key, Rows)) continue;
			for (const TSharedPtr<FJsonValue>& V : *Rows)
			{
				const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
				if (!V->TryGetArray(R) || R->Num() < 3) continue;
				FSimCruiseRoadsideWay W;
				W.Id = static_cast<int64>((*R)[0]->AsNumber());
				W.Kind = FName(*(*R)[1]->AsString());
				Unflat((*R)[2], W.LonLat);
				L.Value->Add(MoveTemp(W));
			}
		}
		if (Root->TryGetArrayField(TEXT("points"), Rows))
		{
			for (const TSharedPtr<FJsonValue>& V : *Rows)
			{
				const TArray<TSharedPtr<FJsonValue>>* R = nullptr;
				if (!V->TryGetArray(R) || R->Num() < 10) continue;
				FSimCruiseRoadsidePoint P;
				P.Id = static_cast<int64>((*R)[0]->AsNumber());
				P.Lat = (*R)[1]->AsNumber();
				P.Lon = (*R)[2]->AsNumber();
				P.Kind = FName(*(*R)[3]->AsString());
				P.Name = (*R)[4]->AsString();
				P.Category = (*R)[5]->AsString();
				P.Brand = (*R)[6]->AsString();
				P.BrandWikidata = (*R)[7]->AsString();
				P.Colour = (*R)[8]->AsString();
				P.Direction = (*R)[9]->AsString();
				Out.Points.Add(MoveTemp(P));
			}
		}
		return true;
	}

	FString CachePath(const FIntPoint& Tile)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Knowledge/roadside"), SimCruiseTileName(Tile) + TEXT(".json"));
	}

	bool ForTile(const FIntPoint& Tile, FSimCruiseRoadside& Out)
	{
		{
			FScopeLock Lock(&RoadsideLock);
			if (const FSimCruiseRoadside* C = RoadsideCache().Find(Tile)) { Out = *C; return true; }
		}
		FString Text;
		FSimCruiseRoadside Data;
		if (!FFileHelper::LoadFileToString(Text, *CachePath(Tile), FFileHelper::EHashOptions::None, FILEREAD_Silent) || !FromJson(Text, Data)) return false;
		FScopeLock Lock(&RoadsideLock);
		RoadsideCache().Add(Tile, Data);
		Out = MoveTemp(Data);
		return true;
	}

	bool IsKnown(const FIntPoint& Tile)
	{
		{
			FScopeLock Lock(&RoadsideLock);
			if (RoadsideCache().Contains(Tile)) return true;
			if (MissingFiles().Contains(Tile)) return false;
		}
		const bool bFile = FPaths::FileExists(CachePath(Tile));
		if (!bFile)
		{
			FScopeLock Lock(&RoadsideLock);
			MissingFiles().Add(Tile);
		}
		return bFile;
	}

	void Store(const FIntPoint& Tile, const FSimCruiseRoadside& Data, bool bSave)
	{
		if (bSave) FFileHelper::SaveStringToFile(ToJson(Tile, Data), *CachePath(Tile), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		FScopeLock Lock(&RoadsideLock);
		RoadsideCache().Add(Tile, Data);
		MissingFiles().Remove(Tile);
		// Keep the tiles around the car only.
		if (RoadsideCache().Num() > 96)
		{
			TArray<FIntPoint> Keys;
			RoadsideCache().GetKeys(Keys);
			Keys.Sort([&](const FIntPoint& A, const FIntPoint& B) { return (A - Tile).SizeSquared() > (B - Tile).SizeSquared(); });
			for (int32 i = 0; i < Keys.Num() - 64; ++i) RoadsideCache().Remove(Keys[i]);
		}
	}
}
