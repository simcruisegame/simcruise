// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePlaces.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "World/SimCruiseTileData.h"

bool FSimCruisePlace::IsValid() const
{
	return !Title.TrimStartAndEnd().IsEmpty() && FMath::Abs(Lat) <= 82.0 && FMath::Abs(Lon) <= 180.0
		&& FMath::IsFinite(Lat) && FMath::IsFinite(Lon);
}

FSimCruisePlace FSimCruisePlace::Make(double Lon, double Lat, const FString& Title, const FString& Street, double BearingDeg)
{
	FSimCruisePlace Place;
	Place.Lon = Lon;
	Place.Lat = Lat;
	Place.Title = Title;
	Place.Street = Street;
	Place.bHasBearing = BearingDeg >= 0.0;
	Place.BearingDeg = Place.bHasBearing ? BearingDeg : 0.0;
	return Place;
}

namespace SimCruisePlaces
{
	FSimCruisePlace Fallback()
	{
		return FSimCruisePlace::Make(-80.13025, 25.78000, TEXT("Ocean Drive, Miami Beach · Art Deco District"), TEXT("Ocean Drive"));
	}

	static FSimCruisePlace DavidsonHall()
	{
		return FSimCruisePlace::Make(-70.79325725, 44.40776522, TEXT("Davidson Hall · Bethel, ME"), TEXT("Church Street"), 341.23);
	}

	static FSimCruisePlace PrincetonGravel()
	{
		return FSimCruisePlace::Make(-96.5909, 40.56724, TEXT("Princeton Road · Lancaster County, NE"), TEXT("Princeton Road"), 90.0);
	}

	const TArray<FSimCruisePlace>& Presets()
	{
		static const TArray<FSimCruisePlace> List = {
			FSimCruisePlace::Make(-109.990814, 37.101444, TEXT("Monument Valley · US 163, UT"), FString(), 230.0),
			FSimCruisePlace::Make(-104.81944137, 41.13906643, TEXT("Capitol Avenue · Cheyenne, WY"), TEXT("Capitol Avenue"), 334.0),
			FSimCruisePlace::Make(-81.630658, 38.348449, TEXT("Clay Center · Charleston, WV"), TEXT("Lee Street East"), 136.44),
			PrincetonGravel(),
			DavidsonHall(),
			FSimCruisePlace::Make(-96.675002778, 40.808013889, TEXT("J Street · Lincoln, NE"), TEXT("J Street"), 91.0),
			FSimCruisePlace::Make(-80.208896, 25.748401, TEXT("South Miami Avenue, Miami · lidar roofs"), TEXT("South Miami Avenue"), 52.21),
			FSimCruisePlace::Make(-71.134311, 42.365183, TEXT("Cambridge, MA · surveyed building heights")),
			Fallback(),
		};
		return List;
	}

	FString StartPoolPath()
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("start"), TEXT("start-pool.json"));
	}

	TArray<FSimCruisePlace> ParseStartPool(const FString& Json)
	{
		TArray<FSimCruisePlace> Pool;
		TArray<TSharedPtr<FJsonValue>> Entries;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Entries))
		{
			return Pool;
		}
		for (const TSharedPtr<FJsonValue>& Value : Entries)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object)
			{
				continue;
			}
			FSimCruisePlace Place;
			double Bearing = -1.0;
			if (!(*Object)->TryGetStringField(TEXT("title"), Place.Title)
				|| !(*Object)->TryGetNumberField(TEXT("lon"), Place.Lon)
				|| !(*Object)->TryGetNumberField(TEXT("lat"), Place.Lat)
				|| !(*Object)->TryGetNumberField(TEXT("bearing"), Bearing))
			{
				continue;
			}
			(*Object)->TryGetStringField(TEXT("street"), Place.Street);
			if (!Place.IsValid() || Bearing < 0.0 || Bearing >= 360.0)
			{
				continue;
			}
			Place.bHasBearing = true;
			Place.BearingDeg = Bearing;
			Place.bLoose = true;
			Pool.Add(MoveTemp(Place));
		}
		return Pool;
	}

	TArray<FSimCruisePlace> LoadStartPool(const FString& Path)
	{
		FString Json;
		return FFileHelper::LoadFileToString(Json, *Path) ? ParseStartPool(Json) : TArray<FSimCruisePlace>();
	}

	TArray<FSimCruisePlace> Sequence(const TArray<FSimCruisePlace>& Pool, FRandomStream& Rng, int32 Count, const FString& Avoid)
	{
		TArray<FSimCruisePlace> Order;
		for (const FSimCruisePlace& Place : Pool)
		{
			if (Place.Title != Avoid) Order.Add(Place);
		}
		if (Order.IsEmpty()) Order = Pool;
		TArray<FSimCruisePlace> Picks;
		const int32 N = FMath::Min(Count, Order.Num());
		for (int32 i = 0; i < N; ++i)
		{
			const int32 j = Rng.RandRange(i, Order.Num() - 1);
			Order.Swap(i, j);
			Picks.Add(Order[i]);
		}
		return Picks;
	}

	FQueryAnswer ParseQuery(const FString& Query)
	{
		FQueryAnswer Answer;
		const FString Q = Query.TrimStartAndEnd();
		if (Q.IsEmpty())
		{
			return Answer;
		}
		TArray<FString> Parts;
		Q.ParseIntoArray(Parts, TEXT(","), /*CullEmpty=*/ false);
		if (Parts.Num() == 2)
		{
			const FString A = Parts[0].TrimStartAndEnd();
			const FString B = Parts[1].TrimStartAndEnd();
			if (A.IsNumeric() && B.IsNumeric())
			{
				const double Lat = FCString::Atod(*A);
				const double Lon = FCString::Atod(*B);
				if (FMath::Abs(Lat) <= 82.0 && FMath::Abs(Lon) <= 180.0)
				{
					Answer.Kind = EQueryKind::Places;
					Answer.Places.Add(FSimCruisePlace::Make(Lon, Lat, FString::Printf(TEXT("%.5f, %.5f"), Lat, Lon)));
					Answer.Note = TEXT("Choose this location.");
					return Answer;
				}
			}
		}
		const FString Lower = Q.ToLower();
		auto Is = [&Lower](std::initializer_list<const TCHAR*> Names)
		{
			for (const TCHAR* Name : Names) if (Lower == Name) return true;
			return false;
		};
		if (Is({TEXT("surprise me"), TEXT("surprise"), TEXT("random")}))
		{
			Answer.Kind = EQueryKind::Surprise;
			return Answer;
		}
		if (Is({TEXT("davidson hall"), TEXT("gould academy"), TEXT("gould academy, bethel"), TEXT("bethel, me")}))
		{
			Answer.Kind = EQueryKind::Places;
			Answer.Places.Add(DavidsonHall());
			Answer.Note = TEXT("Northbound on Church Street beside Davidson Hall.");
			return Answer;
		}
		if (Is({TEXT("princeton gravel"), TEXT("princeton road, ne"), TEXT("princeton road, nebraska"), TEXT("princeton road, lancaster county, ne")}))
		{
			Answer.Kind = EQueryKind::Places;
			Answer.Places.Add(PrincetonGravel());
			Answer.Note = TEXT("Eastbound on the gravel section of Princeton Road.");
			return Answer;
		}
		if (Is({TEXT("miami beach"), TEXT("south beach"), TEXT("ocean drive"), TEXT("ocean drive, miami beach"), TEXT("art deco district")}))
		{
			Answer.Kind = EQueryKind::Places;
			Answer.Places.Add(Fallback());
			Answer.Note = TEXT("Bundled neighborhood · works offline");
			return Answer;
		}
		Answer.Kind = EQueryKind::Search;
		Answer.Note = TEXT("Searching…");
		return Answer;
	}

	TArray<FSimCruisePlace> ParseSearchResults(const TSharedPtr<FJsonValue>& Json)
	{
		TArray<FSimCruisePlace> Places;
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!Json.IsValid() || !Json->TryGetArray(Entries) || !Entries)
		{
			return Places;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Entries)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object) continue;
			FSimCruisePlace Place;
			if (!(*Object)->TryGetStringField(TEXT("title"), Place.Title)
				|| !(*Object)->TryGetNumberField(TEXT("lon"), Place.Lon)
				|| !(*Object)->TryGetNumberField(TEXT("lat"), Place.Lat))
			{
				continue;
			}
			(*Object)->TryGetStringField(TEXT("street"), Place.Street);
			if (Place.IsValid()) Places.Add(MoveTemp(Place));
		}
		return Places;
	}

	double BearingToYawDeg(double BearingDeg)
	{
		// Bearing b points (sin b, -cos b) in (east, south): yaw = b - 90.
		return FRotator::NormalizeAxis(BearingDeg - 90.0);
	}

	double YawToBearingDeg(double YawDeg)
	{
		return FMath::Fmod(FMath::Fmod(YawDeg + 90.0, 360.0) + 360.0, 360.0);
	}
}
