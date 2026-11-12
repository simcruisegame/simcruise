// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruisePlaceGraph.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseTileData.h"

namespace
{
	uint32 Mix(uint32 A, uint32 B) { return HashCombine(A * 2654435761u, B + 0x9E3779B9u); }
	double Unit01(uint32 H) { return static_cast<double>((H >> 8) & 0xFFFFFF) / 16777216.0; }

	FLinearColor Srgb(const FString& Hex) { return FLinearColor::FromSRGBColor(FColor::FromHex(Hex)); }

	FString Slug(const FString& In)
	{
		FString Out;
		bool bSep = false;
		for (const TCHAR C : In.ToLower())
		{
			if (FChar::IsAlnum(C)) { if (bSep && !Out.IsEmpty()) Out.AppendChar(TEXT('_')); Out.AppendChar(C); bSep = false; }
			else bSep = true;
		}
		return Out;
	}

	FName Token(const TCHAR* Kind, const FString& Value) { return FName(*FString::Printf(TEXT("%s:%s"), Kind, *Value)); }

	bool KindOf(FName Fact, FString& OutKind)
	{
		const FString S = Fact.ToString();
		int32 Colon;
		if (!S.FindChar(TEXT(':'), Colon)) return false;
		OutKind = S.Left(Colon);
		return true;
	}

	void ReadPairs(const TSharedPtr<FJsonObject>& Obj, TArray<TPair<FName, float>>& Out)
	{
		if (!Obj.IsValid()) return;
		for (const auto& KV : Obj->Values)
		{
			double V = 0.0;
			if (KV.Value.IsValid() && KV.Value->TryGetNumber(V)) Out.Emplace(FName(*KV.Key), static_cast<float>(V));
		}
	}

	void ReadNames(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<FName>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Field, Arr)) return;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) Out.Add(FName(*S));
		}
	}

	bool ReadRules(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, TArray<FSimCruisePlaceRule>& Out, FString& OutError)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Root->TryGetArrayField(Field, Arr)) return true;
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* RuleObj = nullptr;
			if (!(*Arr)[i]->TryGetObject(RuleObj))
			{
				OutError = FString::Printf(TEXT("%s[%d] is not an object"), Field, i);
				return false;
			}
			FSimCruisePlaceRule R;
			ReadNames(*RuleObj, TEXT("if"), R.If);
			ReadNames(*RuleObj, TEXT("unless"), R.Unless);
			const TSharedPtr<FJsonObject>* Then = nullptr;
			if ((*RuleObj)->TryGetObjectField(TEXT("then"), Then)) ReadPairs(*Then, R.Then);
			(*RuleObj)->TryGetStringField(TEXT("why"), R.Why);
			if (R.If.Num() == 0 || R.Then.Num() == 0)
			{
				OutError = FString::Printf(TEXT("%s[%d] needs 'if' and 'then'"), Field, i);
				return false;
			}
			Out.Add(MoveTemp(R));
		}
		return true;
	}

	bool PointInRing(const TArray<FVector2d>& R, const FVector2d& P)
	{
		bool bIn = false;
		for (int32 i = 0, j = R.Num() - 1; i < R.Num(); j = i++)
		{
			if (((R[i].Y > P.Y) != (R[j].Y > P.Y)) && (P.X < (R[j].X - R[i].X) * (P.Y - R[i].Y) / (R[j].Y - R[i].Y) + R[i].X)) bIn = !bIn;
		}
		return bIn;
	}

	double DistanceToRing(const TArray<FVector2d>& R, const FVector2d& P)
	{
		if (PointInRing(R, P)) return 0.0;
		double Best = TNumericLimits<double>::Max();
		for (int32 i = 0; i < R.Num(); ++i)
		{
			Best = FMath::Min(Best, FVector2d::Distance(P, FMath::ClosestPointOnSegment2D(P, R[i], R[(i + 1) % R.Num()])));
		}
		return Best;
	}

	double RingArea(const TArray<FVector2d>& R)
	{
		double A = 0.0;
		for (int32 i = 0, j = R.Num() - 1; i < R.Num(); j = i++) A += (R[j].X + R[i].X) * (R[j].Y - R[i].Y);
		return FMath::Abs(0.5 * A);
	}

	FCriticalSection RulesLock;
}

// ---------------------------------------------------------------------------------------------
// Fact sets

void FSimCruiseFactSet::Add(FName Fact, float Confidence, const FString& Why)
{
	Confidence = FMath::Clamp(Confidence, 0.0f, 1.0f);
	if (Confidence <= 0.0f) return;
	FEntry& E = Facts.FindOrAdd(Fact);
	if (Confidence > E.Confidence)
	{
		E.Confidence = Confidence;
		E.Why = Why;
	}
}

void FSimCruiseFactSet::Append(const FSimCruiseFactSet& Other)
{
	for (const auto& KV : Other.Facts) Add(KV.Key, KV.Value.Confidence, KV.Value.Why);
}

FName FSimCruiseFactSet::Strongest(const TCHAR* Kind, float* OutConfidence) const
{
	const FString Prefix = FString(Kind) + TEXT(":");
	FName Best;
	float BestC = 0.0f;
	for (const auto& KV : Facts)
	{
		if (KV.Value.Confidence > BestC && KV.Key.ToString().StartsWith(Prefix))
		{
			Best = KV.Key;
			BestC = KV.Value.Confidence;
		}
	}
	if (OutConfidence) *OutConfidence = BestC;
	return Best;
}

FString FSimCruiseFactSet::Describe(const TCHAR* Kind) const
{
	TArray<TPair<FName, FEntry>> Sorted;
	const FString Prefix = Kind ? FString(Kind) + TEXT(":") : FString();
	for (const auto& KV : Facts)
	{
		if (Prefix.IsEmpty() || KV.Key.ToString().StartsWith(Prefix)) Sorted.Emplace(KV.Key, KV.Value);
	}
	Sorted.Sort([](const TPair<FName, FEntry>& A, const TPair<FName, FEntry>& B)
	{
		return A.Value.Confidence != B.Value.Confidence ? A.Value.Confidence > B.Value.Confidence : A.Key.LexicalLess(B.Key);
	});
	FString Out;
	for (const auto& KV : Sorted)
	{
		Out += FString::Printf(TEXT("  %-28s %.2f  %s\n"), *KV.Key.ToString(), KV.Value.Confidence, *KV.Value.Why);
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------
// Rules

bool FSimCruisePlaceRules::Parse(const FString& JsonText, FString& OutError)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("not JSON");
		return false;
	}
	*this = FSimCruisePlaceRules();
	Root->TryGetNumberField(TEXT("version"), Version);

	const TSharedPtr<FJsonObject>* Aliases = nullptr;
	const TSharedPtr<FJsonObject>* StyleAliasObj = nullptr;
	if (Root->TryGetObjectField(TEXT("aliases"), Aliases) && (*Aliases)->TryGetObjectField(TEXT("style"), StyleAliasObj))
	{
		for (const auto& KV : (*StyleAliasObj)->Values)
		{
			FString V;
			if (KV.Value->TryGetString(V)) StyleAliases.Add(FString(KV.Key).ToLower(), V);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* RegionArr = nullptr;
	if (Root->TryGetArrayField(TEXT("regions"), RegionArr))
	{
		for (int32 i = 0; i < RegionArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Box = nullptr;
			if (!(*RegionArr)[i]->TryGetObject(Obj) || !(*Obj)->TryGetArrayField(TEXT("box"), Box) || Box->Num() != 4)
			{
				OutError = FString::Printf(TEXT("regions[%d] needs a box [lat0, lon0, lat1, lon1]"), i);
				return false;
			}
			FSimCruisePlaceRegion R;
			(*Obj)->TryGetStringField(TEXT("id"), R.Id);
			(*Obj)->TryGetStringField(TEXT("why"), R.Why);
			const double A = (*Box)[0]->AsNumber(), B = (*Box)[1]->AsNumber(), C = (*Box)[2]->AsNumber(), D = (*Box)[3]->AsNumber();
			R.LatMin = FMath::Min(A, C); R.LatMax = FMath::Max(A, C);
			R.LonMin = FMath::Min(B, D); R.LonMax = FMath::Max(B, D);
			const TSharedPtr<FJsonObject>* Facts = nullptr;
			if ((*Obj)->TryGetObjectField(TEXT("facts"), Facts)) ReadPairs(*Facts, R.Facts);
			Regions.Add(MoveTemp(R));
		}
	}

	const TSharedPtr<FJsonObject>* Spread = nullptr;
	if (Root->TryGetObjectField(TEXT("spread"), Spread))
	{
		(*Spread)->TryGetNumberField(TEXT("radius_m"), SpreadRadiusM);
		(*Spread)->TryGetNumberField(TEXT("min_neighbours"), SpreadMinNeighbours);
		double S = SpreadStrength;
		if ((*Spread)->TryGetNumberField(TEXT("strength"), S)) SpreadStrength = static_cast<float>(S);
		(*Spread)->TryGetStringArrayField(TEXT("kinds"), SpreadKinds);
	}

	if (!ReadRules(Root, TEXT("rules"), Rules, OutError)) return false;
	if (!ReadRules(Root, TEXT("live_rules"), LiveRules, OutError)) return false;

	const TSharedPtr<FJsonObject>* PaletteObj = nullptr;
	if (Root->TryGetObjectField(TEXT("palettes"), PaletteObj))
	{
		for (const auto& KV : (*PaletteObj)->Values)
		{
			const TSharedPtr<FJsonObject>* P = nullptr;
			if (!KV.Value->TryGetObject(P)) continue;
			FSimCruisePalette Pal;
			TArray<FString> Hex;
			if ((*P)->TryGetStringArrayField(TEXT("wall"), Hex)) for (const FString& H : Hex) Pal.Wall.Add(Srgb(H));
			Hex.Reset();
			if ((*P)->TryGetStringArrayField(TEXT("accent"), Hex)) for (const FString& H : Hex) Pal.Accent.Add(Srgb(H));
			if (Pal.Wall.Num() == 0)
			{
				OutError = FString::Printf(TEXT("palette %s has no wall colours"), *KV.Key);
				return false;
			}
			Palettes.Add(FName(*FString::Printf(TEXT("palette:%s"), *KV.Key)), MoveTemp(Pal));
		}
	}
	return Rules.Num() > 0;
}

void FSimCruisePlaceRules::RegionFacts(double LatDeg, double LonDeg, FSimCruiseFactSet& Out) const
{
	for (const FSimCruisePlaceRegion& R : Regions)
	{
		if (LatDeg < R.LatMin || LatDeg > R.LatMax || LonDeg < R.LonMin || LonDeg > R.LonMax) continue;
		const FString Why = FString::Printf(TEXT("region %s%s%s"), *R.Id, R.Why.IsEmpty() ? TEXT("") : TEXT(": "), *R.Why);
		for (const TPair<FName, float>& F : R.Facts) Out.Add(F.Key, F.Value, Why);
	}
	// Latitude bands where no region speaks (Koppen, very roughly).
	if (Out.Strongest(TEXT("climate")).IsNone())
	{
		const double A = FMath::Abs(LatDeg);
		const TCHAR* C = A < 23.5 ? TEXT("climate:tropical") : A < 35.0 ? TEXT("climate:humid_subtropical") : A < 55.0 ? TEXT("climate:humid_continental") : TEXT("climate:subarctic");
		Out.Add(FName(C), 0.5f, TEXT("latitude band"));
	}
}

void FSimCruisePlaceRules::Infer(FSimCruiseFactSet& Facts, bool bLive) const
{
	const TArray<FSimCruisePlaceRule>& Set = bLive ? LiveRules : Rules;
	// Plain confidences while chaining; the base facts stay, derived facts are rebuilt each
	// pass from base + last pass (the result does not depend on the order of the rules).
	// Reasons are written once at the end.
	TMap<FName, float> Base;
	Base.Reserve(Facts.Facts.Num());
	for (const auto& KV : Facts.Facts) Base.Add(KV.Key, KV.Value.Confidence);
	TMap<FName, float> Current = Base;
	struct FAcc { float NotPos = 1.0f; float NotNeg = 1.0f; float Best = 0.0f; const FSimCruisePlaceRule* Rule = nullptr; };
	TMap<FName, FAcc> Acc;
	TMap<FName, float> Next;
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		Acc.Reset();
		for (const FSimCruisePlaceRule& R : Set)
		{
			float C = 1.0f;
			for (const FName& F : R.If)
			{
				const float* V = Current.Find(F);
				C = V ? FMath::Min(C, *V) : 0.0f;
				if (C <= 0.0f) break;
			}
			if (C <= 0.0f) continue;
			for (const FName& F : R.Unless) { const float* V = Current.Find(F); if (V) C *= 1.0f - *V; }
			if (C <= 0.01f) continue;
			for (const TPair<FName, float>& T : R.Then)
			{
				FAcc& A = Acc.FindOrAdd(T.Key);
				const float V = C * FMath::Abs(T.Value);
				if (T.Value >= 0.0f)
				{
					A.NotPos *= 1.0f - V;
					if (V > A.Best) { A.Best = V; A.Rule = &R; }
				}
				else
				{
					A.NotNeg *= 1.0f - V;
				}
			}
		}
		Next = Base;
		for (const auto& KV : Acc)
		{
			const float* OwnP = Base.Find(KV.Key);
			const float V = FMath::Max(OwnP ? *OwnP : 0.0f, 1.0f - KV.Value.NotPos) * KV.Value.NotNeg;
			if (V <= 0.005f) Next.Remove(KV.Key);
			else Next.Add(KV.Key, V);
		}
		bool bSame = Next.Num() == Current.Num();
		if (bSame)
		{
			for (const auto& KV : Next)
			{
				const float* C = Current.Find(KV.Key);
				if (!C || !FMath::IsNearlyEqual(*C, KV.Value, 1e-4f)) { bSame = false; break; }
			}
		}
		Swap(Current, Next);
		if (bSame) break;
	}
	// Reasons: the rule that gave each derived fact its strongest support (Acc holds the last pass).
	FSimCruiseFactSet Out;
	Out.Facts.Reserve(Current.Num());
	for (const auto& KV : Current)
	{
		FSimCruiseFactSet::FEntry E;
		E.Confidence = KV.Value;
		const FAcc* A = Acc.Find(KV.Key);
		const float* OwnP = Base.Find(KV.Key);
		const float Own = OwnP ? *OwnP : 0.0f;
		if (A && A->Rule && (1.0f - A->NotPos) > Own)
		{
			TArray<FString> Ifs;
			for (const FName& F : A->Rule->If) Ifs.Add(F.ToString());
			E.Why = FString::Printf(TEXT("<- %s"), *FString::Join(Ifs, TEXT(" + ")));
			if (!A->Rule->Why.IsEmpty()) E.Why += TEXT(": ") + A->Rule->Why;
		}
		else if (const FSimCruiseFactSet::FEntry* Old = Facts.Facts.Find(KV.Key))
		{
			E.Why = Old->Why;
		}
		if (A && A->NotNeg < 0.999f) E.Why += FString::Printf(TEXT(" (suppressed x%.2f)"), A->NotNeg);
		Out.Facts.Add(KV.Key, MoveTemp(E));
	}
	Facts = MoveTemp(Out);
}

FString FSimCruisePlaceRules::StyleToken(const FString& Label) const
{
	const FString L = Label.TrimStartAndEnd().ToLower();
	if (const FString* A = StyleAliases.Find(L)) return *A;
	FString S = L;
	S.RemoveFromEnd(TEXT(" architecture"));
	if (const FString* A = StyleAliases.Find(S)) return *A;
	return Slug(S);
}

// ---------------------------------------------------------------------------------------------
// Plans

float FSimCruiseBuildingPlan::P(FName Detail, float Default) const
{
	const FSimCruiseFactSet::FEntry* E = Facts.Facts.Find(Detail);
	return E ? E->Confidence : Default;
}

bool FSimCruiseBuildingPlan::IsRecipe(const TCHAR* Recipe) const
{
	float C = 0.0f;
	const FName Best = Facts.Strongest(TEXT("recipe"), &C);
	return C >= 0.5f && Best == FName(*FString::Printf(TEXT("recipe:%s"), Recipe));
}

FLinearColor FSimCruiseBuildingPlan::Weathered(const FLinearColor& InWall) const
{
	if (Wear <= 0.0f || bColourFromData) return InWall;
	const float K = Wear * static_cast<float>(0.10 + 0.08 * Unit01(Mix(Seed, 3)));
	return FMath::Lerp(InWall, FLinearColor(0.32f, 0.29f, 0.24f), K);
}

FString FSimCruiseBuildingPlan::Explain() const
{
	FString Out = FString::Printf(TEXT("Building %d%s%s: style %s (%.2f)"), Building, Name.IsEmpty() ? TEXT("") : TEXT(" "), *Name,
		Style.IsNone() ? TEXT("unknown") : *Style.ToString(), StyleConfidence);
	if (Year > 0) Out += FString::Printf(TEXT(", built %d"), Year);
	if (!Architect.IsEmpty()) Out += FString::Printf(TEXT(", architect %s"), *Architect);
	if (!Palette.IsNone()) Out += FString::Printf(TEXT(", %s%s"), *Palette.ToString(), bColourFromData ? TEXT(" (colour from the map)") : TEXT(""));
	Out += FString::Printf(TEXT(", wear %.2f"), Wear);
	Out += TEXT("\n");
	Out += Facts.Describe();
	return Out;
}

namespace SimCruisePlaceGraph
{
	FString RulesPath()
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data/knowledge/place-graph.json"));
	}

	const FSimCruisePlaceRules& Rules()
	{
		static FSimCruisePlaceRules Loaded;
		static bool bLoaded = false;
		FScopeLock Lock(&RulesLock);
		if (!bLoaded)
		{
			bLoaded = true;
			FString Text, Error;
			if (!FFileHelper::LoadFileToString(Text, *RulesPath()) || !Loaded.Parse(Text, Error))
			{
				UE_LOG(LogTemp, Warning, TEXT("SimCruise place graph: %s not loaded (%s); buildings keep their default recipe."), *RulesPath(), *Error);
			}
		}
		return Loaded;
	}

	void EraFacts(int32 Year, const FString& Why, FSimCruiseFactSet& Out)
	{
		if (Year < 1600 || Year > 2100) return;
		Out.Add(Token(TEXT("era"), FString::Printf(TEXT("%ds"), (Year / 10) * 10)), 1.0f, Why);
		if (Year < 1900) Out.Add(FName(TEXT("era:before_1900")), 1.0f, Why);
		Out.Add(FName(Year < 1945 ? TEXT("era:before_1945") : Year <= 1975 ? TEXT("era:1945_1975") : TEXT("era:after_1975")), 1.0f, Why);
	}

	FName PoiFact(const FString& K)
	{
		static const TMap<FString, FName> Map = []
		{
			TMap<FString, FName> M;
			for (const TCHAR* S : { TEXT("lodging"), TEXT("hotel"), TEXT("motel"), TEXT("hostel"), TEXT("guest_house"), TEXT("apartment") }) M.Add(S, FName(TEXT("poi:hotel")));
			for (const TCHAR* S : { TEXT("bar"), TEXT("nightclub"), TEXT("pub"), TEXT("beer"), TEXT("cinema"), TEXT("theatre") }) M.Add(S, FName(TEXT("poi:bar")));
			for (const TCHAR* S : { TEXT("restaurant"), TEXT("cafe"), TEXT("fast_food"), TEXT("ice_cream"), TEXT("bakery") }) M.Add(S, FName(TEXT("poi:restaurant")));
			for (const TCHAR* S : { TEXT("shop"), TEXT("clothing_store"), TEXT("grocery"), TEXT("supermarket"), TEXT("convenience"), TEXT("pharmacy"),
				TEXT("alcohol_shop"), TEXT("hairdresser"), TEXT("laundry"), TEXT("bicycle_rental"), TEXT("bicycle"), TEXT("car"), TEXT("art_gallery"), TEXT("bank") })
			{
				M.Add(S, FName(TEXT("poi:shop")));
			}
			for (const TCHAR* S : { TEXT("school"), TEXT("college"), TEXT("university"), TEXT("library") }) M.Add(S, FName(TEXT("poi:school")));
			for (const TCHAR* S : { TEXT("museum"), TEXT("town_hall"), TEXT("post"), TEXT("post_office"), TEXT("police"), TEXT("fire_station"), TEXT("place_of_worship") }) M.Add(S, FName(TEXT("poi:civic")));
			M.Add(TEXT("fuel"), FName(TEXT("poi:fuel")));
			M.Add(TEXT("office"), FName(TEXT("poi:office")));
			M.Add(TEXT("hospital"), FName(TEXT("poi:hospital")));
			return M;
		}();
		const FName* F = Map.Find(K.ToLower());
		return F ? *F : NAME_None;
	}

	bool ParseColour(const FString& Text, FLinearColor& Out)
	{
		FString T = Text.TrimStartAndEnd().ToLower();
		if (T.IsEmpty()) return false;
		static const TMap<FString, FString> Named = {
			{ TEXT("white"), TEXT("F2F0EA") }, { TEXT("beige"), TEXT("E6D6B8") }, { TEXT("cream"), TEXT("F0E6CC") }, { TEXT("yellow"), TEXT("EED98A") },
			{ TEXT("pink"), TEXT("F1C8C4") }, { TEXT("red"), TEXT("9F4A3A") }, { TEXT("brown"), TEXT("7A5A44") }, { TEXT("tan"), TEXT("CDB08A") },
			{ TEXT("grey"), TEXT("A9ABA8") }, { TEXT("gray"), TEXT("A9ABA8") }, { TEXT("blue"), TEXT("A8BED6") }, { TEXT("lightblue"), TEXT("C9DCEB") },
			{ TEXT("green"), TEXT("A9C2A0") }, { TEXT("orange"), TEXT("E6A66A") }, { TEXT("black"), TEXT("3A3A3A") }, { TEXT("salmon"), TEXT("F0A890") },
			{ TEXT("turquoise"), TEXT("8FD0C8") }, { TEXT("teal"), TEXT("5BA8A0") }, { TEXT("purple"), TEXT("B8A0C8") }, { TEXT("lavender"), TEXT("D8CCE8") },
		};
		if (const FString* H = Named.Find(T)) { Out = Srgb(*H); return true; }
		T.RemoveFromStart(TEXT("#"));
		if ((T.Len() == 6 || T.Len() == 3))
		{
			for (const TCHAR C : T) if (!FChar::IsHexDigit(C)) return false;
			if (T.Len() == 3) T = FString::Printf(TEXT("%c%c%c%c%c%c"), T[0], T[0], T[1], T[1], T[2], T[2]);
			Out = Srgb(T);
			return true;
		}
		return false;
	}

	void RecordFacts(const FSimCruisePlaceRules& R, const FSimCruisePlaceRecord& Rec, FSimCruiseFactSet& Out)
	{
		const FString Src = Rec.Wikidata.IsEmpty() ? FString::Printf(TEXT("OSM %s"), *Rec.OsmId) : FString::Printf(TEXT("Wikidata %s"), *Rec.Wikidata);
		for (const FString& S : Rec.Styles)
		{
			const FString T = R.StyleToken(S);
			if (!T.IsEmpty()) Out.Add(Token(TEXT("style"), T), 1.0f, FString::Printf(TEXT("%s: %s"), *Src, *S));
		}
		if (Rec.Year > 0) EraFacts(Rec.Year, FString::Printf(TEXT("%s: built %d"), *Src, Rec.Year), Out);
		if (!Rec.Architect.IsEmpty()) Out.Add(Token(TEXT("architect"), Slug(Rec.Architect)), 1.0f, Src);
		if (!Rec.Heritage.IsEmpty())
		{
			Out.Add(FName(TEXT("heritage:listed")), 1.0f, FString::Printf(TEXT("%s: %s"), *Src, *Rec.Heritage));
			if (Rec.Heritage.Contains(TEXT("National Register"))) Out.Add(FName(TEXT("heritage:nrhp")), 1.0f, Src);
		}
		if (!Rec.Material.IsEmpty()) Out.Add(Token(TEXT("material"), Slug(Rec.Material)), 1.0f, Src);
		if (!Rec.RoofShape.IsEmpty()) Out.Add(Token(TEXT("roof"), Slug(Rec.RoofShape)), 1.0f, Src);
		if (!Rec.Colour.IsEmpty()) Out.Add(FName(Rec.ColourSource.IsEmpty() ? TEXT("colour:mapped") : TEXT("colour:from_photo")), 1.0f,
			Rec.ColourSource.IsEmpty() ? FString::Printf(TEXT("%s: building:colour %s"), *Src, *Rec.Colour) : FString::Printf(TEXT("%s %s"), *Rec.Colour, *Rec.ColourSource));
		if (!Rec.Building.IsEmpty() && Rec.Building != TEXT("yes")) Out.Add(Token(TEXT("use"), Slug(Rec.Building)), 1.0f, Src);
		if (Rec.Building == TEXT("hotel")) Out.Add(FName(TEXT("poi:hotel")), 1.0f, Src);
		const FName Poi = PoiFact(Rec.Amenity);
		if (!Poi.IsNone()) Out.Add(Poi, 1.0f, FString::Printf(TEXT("%s: %s"), *Src, *Rec.Amenity));
	}

	void BakeTile(const FSimCruisePlaceRules& Rules, const FSimCruiseTileData& Data, double LatDeg, double LonDeg, double SideM,
		const TArray<FSimCruisePlaceRecord>& Records, FSimCruiseTilePlan& Out, const FSimCruiseFactSet* AreaFacts)
	{
		const double T0 = FPlatformTime::Seconds();
		Out = FSimCruiseTilePlan();
		Out.Tile = Data.Id;
		Out.Buildings.SetNum(Data.Buildings.Num());

		// Tile facts: regions and climate, water on the tile, districts from the open data.
		Rules.RegionFacts(LatDeg, LonDeg, Out.TileFacts);
		// The local AI's reading of the area (weak: below a building's own data and the spread).
		if (AreaFacts) Out.TileFacts.Append(*AreaFacts);
		for (const FSimCruiseLandPolygon& L : Data.Land)
		{
			if (L.Layer == TEXT("water") || L.Class == TEXT("ocean") || L.Class == TEXT("sea") || L.Class == TEXT("lake"))
			{
				Out.TileFacts.Add(FName(TEXT("near:water")), 1.0f, TEXT("water on the tile"));
				break;
			}
		}
		for (const FSimCruisePlaceRecord& Rec : Records)
		{
			if (!Rec.bDistrict) continue;
			FSimCruiseFactSet D;
			RecordFacts(Rules, Rec, D);
			for (const auto& KV : D.Facts)
			{
				FString Kind;
				if (!KindOf(KV.Key, Kind) || (Kind != TEXT("style") && Kind != TEXT("era") && Kind != TEXT("heritage"))) continue;
				const FString V = KV.Key.ToString().Mid(Kind.Len() + 1);
				const FName Fact = Kind == TEXT("heritage") ? FName(TEXT("district:historic")) : Token(TEXT("district"), V);
				Out.TileFacts.Add(Fact, 0.8f, FString::Printf(TEXT("district %s"), *Rec.Name));
			}
		}

		// Footprints in metres (y south, as the tile units).
		const int32 NB = Data.Buildings.Num();
		TArray<TArray<FVector2d>> Rings;
		TArray<FVector2d> Centres;
		Rings.SetNum(NB);
		Centres.SetNum(NB);
		for (int32 b = 0; b < NB; ++b)
		{
			for (const FVector2d& U : Data.Buildings[b].Ring) Rings[b].Add(U * SideM);
			FVector2d C = FVector2d::ZeroVector;
			for (const FVector2d& P : Rings[b]) C += P;
			Centres[b] = Rings[b].Num() > 0 ? C / Rings[b].Num() : C;
		}
		auto NearestBuilding = [&](const FVector2d& P, double MaxD)
		{
			int32 Best = INDEX_NONE;
			double BestD = MaxD;
			for (int32 b = 0; b < NB; ++b)
			{
				if (Rings[b].Num() < 3 || Data.Buildings[b].MinHeightM > 0.5) continue;
				if (FVector2d::Distance(Centres[b], P) > BestD + 250.0) continue;
				const double D = DistanceToRing(Rings[b], P);
				if (D < BestD || (D == 0.0 && BestD > 0.0)) { BestD = D; Best = b; if (D == 0.0) break; }
			}
			return Best;
		};

		// Base facts per building: form, size, named places on it, its own records.
		TArray<FSimCruiseFactSet> Base;
		Base.SetNum(NB);
		for (int32 b = 0; b < NB; ++b)
		{
			const FSimCruiseBuildingRow& Row = Data.Buildings[b];
			FSimCruiseFactSet& F = Base[b];
			const double H = Row.HeightM;
			const TCHAR* Form = H > 60.0 ? TEXT("form:highrise") : H > 26.0 ? TEXT("form:tower") : H > 12.5 ? TEXT("form:midrise") : TEXT("form:lowrise");
			F.Add(FName(Form), 1.0f, FString::Printf(TEXT("height %.0f m"), H));
			if (FCString::Strcmp(Form, TEXT("form:highrise")) == 0) F.Add(FName(TEXT("form:tower")), 1.0f, FString::Printf(TEXT("height %.0f m"), H));
			const double Area = Rings[b].Num() >= 3 ? RingArea(Rings[b]) : 0.0;
			if (Area > 0.0) F.Add(FName(Area < 150.0 ? TEXT("size:small") : Area > 2500.0 ? TEXT("size:large") : TEXT("size:medium")), 1.0f, FString::Printf(TEXT("footprint %.0f m2"), Area));
			if (!Row.Use.IsEmpty()) F.Add(Token(TEXT("use"), Slug(Row.Use)), 1.0f, TEXT("Overture building class"));
			Out.Buildings[b].Building = b;
			Out.Buildings[b].CentreM = Centres[b];
			Out.Buildings[b].HeightM = static_cast<float>(H);
		}
		for (const FSimCruisePoi& Poi : Data.Pois)
		{
			const FName Fact = PoiFact(Poi.Kind);
			if (Fact.IsNone()) continue;
			const int32 b = NearestBuilding(Poi.Unit * SideM, 12.0);
			if (b == INDEX_NONE) continue;
			Base[b].Add(Fact, 1.0f, FString::Printf(TEXT("named place %s (%s)"), *Poi.Name, *Poi.Kind));
			if (Fact == FName(TEXT("poi:hotel")) && Out.Buildings[b].Name.IsEmpty()) Out.Buildings[b].Name = Poi.Name;
		}
		TArray<bool> Own;
		Own.Init(false, NB);
		for (const FSimCruisePlaceRecord& Rec : Records)
		{
			if (Rec.bDistrict) continue;
			const FVector2d U = SimCruiseGeo::LonLatToTile(Rec.Lon, Rec.Lat) - FVector2d(Data.Id.X, Data.Id.Y);
			if (U.X < -0.05 || U.Y < -0.05 || U.X > 1.05 || U.Y > 1.05) continue;
			const int32 b = NearestBuilding(U * SideM, 18.0);
			if (b == INDEX_NONE) continue;
			++Out.Records;
			RecordFacts(Rules, Rec, Base[b]);
			FSimCruiseBuildingPlan& P = Out.Buildings[b];
			if (!Rec.Name.IsEmpty() && (P.Name.IsEmpty() || !Rec.Wikidata.IsEmpty())) P.Name = Rec.Name;
			if (Rec.Year > 0) P.Year = Rec.Year;
			if (!Rec.Architect.IsEmpty()) P.Architect = Rec.Architect;
			FLinearColor C;
			if (ParseColour(Rec.Colour, C)) { P.Wall = C; P.bHasColour = true; P.bColourFromData = true; }
			if (!Own[b] && (Rec.Styles.Num() > 0 || Rec.Year > 0)) { Own[b] = true; ++Out.Matched; }
		}

		// Spread: an undated building takes the style and era of its dated neighbours.
		// Each dated building's votes per kind are listed once (only the decade era:1930s,
		// not the bands; those follow from it below).
		TArray<FSimCruiseFactSet> Spread;
		Spread.SetNum(NB);
		const double Radius = Rules.SpreadRadiusM;
		const int32 NK = Rules.SpreadKinds.Num();
		TArray<int32> Dated;
		TArray<TArray<TArray<FName>>> VotesOf; // [building][kind] -> facts
		VotesOf.SetNum(NB);
		for (int32 o = 0; o < NB; ++o)
		{
			if (!Own[o]) continue;
			Dated.Add(o);
			VotesOf[o].SetNum(NK);
			for (const auto& KV : Base[o].Facts)
			{
				if (KV.Value.Confidence < 0.9f) continue;
				FString K;
				if (!KindOf(KV.Key, K)) continue;
				const int32 k = Rules.SpreadKinds.IndexOfByKey(K);
				if (k == INDEX_NONE) continue;
				if (K == TEXT("era") && !KV.Key.ToString().EndsWith(TEXT("0s"))) continue;
				VotesOf[o][k].Add(KV.Key);
			}
		}
		for (int32 b = 0; b < NB && Dated.Num() > 0; ++b)
		{
			if (Own[b] || Rings[b].Num() < 3) continue;
			for (int32 k = 0; k < NK; ++k)
			{
				const FString& Kind = Rules.SpreadKinds[k];
				if (!Base[b].Strongest(*Kind).IsNone()) continue;
				TMap<FName, double> Votes;
				double Total = 0.0;
				int32 Count = 0;
				for (const int32 o : Dated)
				{
					if (VotesOf[o][k].Num() == 0) continue;
					const double D = FVector2d::Distance(Centres[b], Centres[o]);
					if (D > Radius) continue;
					const double W = 1.0 - 0.6 * D / Radius;
					for (const FName& F : VotesOf[o][k]) Votes.FindOrAdd(F) += W;
					Total += W;
					++Count;
				}
				if (Count < Rules.SpreadMinNeighbours || Total <= 0.0) continue;
				for (const auto& V : Votes)
				{
					const float C = static_cast<float>(Rules.SpreadStrength * V.Value / Total);
					const FString Why = FString::Printf(TEXT("spread from %d dated neighbours within %.0f m (%.0f%%)"), Count, Radius, 100.0 * V.Value / Total);
					Spread[b].Add(V.Key, C, Why);
					if (Kind == TEXT("era"))
					{
						const int32 Decade = FCString::Atoi(*V.Key.ToString().Mid(4));
						FSimCruiseFactSet Bands;
						EraFacts(Decade + 5, Why, Bands);
						for (const auto& BKV : Bands.Facts) if (!BKV.Key.ToString().EndsWith(TEXT("0s"))) Spread[b].Add(BKV.Key, C, Why);
					}
				}
			}
		}

		// Inference, then the colours.
		for (int32 b = 0; b < NB; ++b)
		{
			FSimCruiseBuildingPlan& P = Out.Buildings[b];
			FSimCruiseFactSet F = Out.TileFacts;
			F.Append(Spread[b]);
			F.Append(Base[b]);
			Rules.Infer(F);
			P.Facts = MoveTemp(F);
			P.Style = P.Facts.Strongest(TEXT("style"), &P.StyleConfidence);
			float PalC = 0.0f;
			P.Palette = P.Facts.Strongest(TEXT("palette"), &PalC);
			const uint32 H = Mix(GetTypeHash(Data.Id), static_cast<uint32>(b) * 7919u + 17u);
			if (const FSimCruisePalette* Pal = Rules.Palettes.Find(P.Palette))
			{
				if (!P.bColourFromData)
				{
					P.Wall = Pal->Wall[Mix(H, 1) % static_cast<uint32>(Pal->Wall.Num())];
					P.bHasColour = PalC >= 0.3f;
				}
				P.Accent = Pal->Accent.Num() > 0 ? Pal->Accent[Mix(H, 2) % static_cast<uint32>(Pal->Accent.Num())] : P.Wall;
			}
			// Weathering: age, salt air and soot; restoration undoes most of it.
			P.Wear = FMath::Clamp(P.P(TEXT("detail:wear")) + 0.5f * P.P(TEXT("detail:salt_wear")) + 0.5f * P.P(TEXT("detail:soot_grime")), 0.0f, 1.0f)
				* (1.0f - 0.7f * P.P(TEXT("detail:restored")));
			P.Seed = H;
		}
		Out.BakeMs = (FPlatformTime::Seconds() - T0) * 1000.0;
	}

	void LiveView(const FSimCruisePlaceRules& Rules, const FSimCruiseBuildingPlan& Plan, const FSimCruiseFactSet& Live, FSimCruiseFactSet& Out)
	{
		Out = Plan.Facts;
		Out.Append(Live);
		Rules.Infer(Out, true);
	}
}
