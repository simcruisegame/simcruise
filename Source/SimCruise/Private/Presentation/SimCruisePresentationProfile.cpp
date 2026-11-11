// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruisePresentationProfile.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

ESimCruiseCarMaterialRole FSimCruiseCarPresentationProfile::RoleFor(const FString& MaterialName, const FString& OwnerLabel) const
{
	if (const ESimCruiseCarMaterialRole* Role = Roles.Find(MaterialName))
	{
		return *Role;
	}
	return SimCruiseCarMaterialRoles::Guess(MaterialName, OwnerLabel);
}

FString SimCruisePresentationProfiles::ProfilePath()
{
	return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Vehicles"), TEXT("CarPresentation.json"));
}

bool SimCruisePresentationProfiles::ParseColor(const FString& Text, FLinearColor& OutColor)
{
	FString Hex = Text.TrimStartAndEnd();
	if (Hex.StartsWith(TEXT("#")))
	{
		Hex.RightChopInline(1);
	}
	if (Hex.Len() != 6)
	{
		return false;
	}
	for (TCHAR C : Hex)
	{
		if (!FChar::IsHexDigit(C))
		{
			return false;
		}
	}
	OutColor = FLinearColor(FColor::FromHex(Hex));
	return true;
}

namespace
{
	bool ReadColorValue(const TSharedPtr<FJsonValue>& Value, FLinearColor& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		FString Text;
		if (Value->TryGetString(Text))
		{
			return SimCruisePresentationProfiles::ParseColor(Text, Out);
		}
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array) && Array && Array->Num() >= 3)
		{
			Out = FLinearColor((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
			return true;
		}
		return false;
	}

	bool ReadVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Key, Array) && Array && Array->Num() == 3)
		{
			Out = FVector((*Array)[0]->AsNumber(), (*Array)[1]->AsNumber(), (*Array)[2]->AsNumber());
			return true;
		}
		return false;
	}

	bool LoadRoot(TSharedPtr<FJsonObject>& OutCars, FString& OutError)
	{
		const FString Path = SimCruisePresentationProfiles::ProfilePath();
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = FString::Printf(TEXT("%s: cannot read the presentation profile"), *Path);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s: invalid JSON: %s"), *Path, *Reader->GetErrorMessage());
			return false;
		}
		const TSharedPtr<FJsonObject>* Cars = nullptr;
		if (!Root->TryGetObjectField(TEXT("cars"), Cars) || !Cars)
		{
			OutError = FString::Printf(TEXT("%s: missing object 'cars'"), *Path);
			return false;
		}
		OutCars = *Cars;
		return true;
	}
}

TArray<FString> SimCruisePresentationProfiles::AvailableIds()
{
	TArray<FString> Ids;
	TSharedPtr<FJsonObject> Cars;
	FString Error;
	if (LoadRoot(Cars, Error))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Cars->Values)
		{
			Ids.Add(Pair.Key);
		}
		Ids.Sort();
	}
	return Ids;
}

bool SimCruisePresentationProfiles::Load(const FString& CarId, FSimCruiseCarPresentationProfile& OutProfile, FString& OutError)
{
	OutProfile = FSimCruiseCarPresentationProfile();
	OutProfile.CarId = CarId;
	TSharedPtr<FJsonObject> Cars;
	if (!LoadRoot(Cars, OutError))
	{
		return false;
	}
	const FString Where = ProfilePath() + TEXT(": cars.") + CarId;
	const TSharedPtr<FJsonObject>* CarObject = nullptr;
	if (!Cars->TryGetObjectField(CarId, CarObject) || !CarObject)
	{
		OutError = Where + TEXT(": no presentation profile for this car");
		return false;
	}
	const TSharedPtr<FJsonObject> Car = *CarObject;
	if (!Car->TryGetStringField(TEXT("model"), OutProfile.ModelPath) || OutProfile.ModelPath.IsEmpty())
	{
		OutError = Where + TEXT(": 'model' (glTF path relative to the project) is required");
		return false;
	}
	Car->TryGetStringField(TEXT("credit"), OutProfile.Credit);
	bool bFlip = false;
	if (Car->TryGetBoolField(TEXT("flip"), bFlip))
	{
		OutProfile.bFlip = bFlip;
	}
	Car->TryGetBoolField(TEXT("relabel_wheels"), OutProfile.bRelabelWheels);
	Car->TryGetBoolField(TEXT("fit_track"), OutProfile.bFitTrack);
	Car->TryGetBoolField(TEXT("auto_badges"), OutProfile.bAutoBadges);
	Car->TryGetNumberField(TEXT("steering_ratio"), OutProfile.SteeringRatio);
	Car->TryGetStringField(TEXT("steering_ratio_source"), OutProfile.SteeringRatioSource);
	if (!(OutProfile.SteeringRatio > 1.0 && OutProfile.SteeringRatio < 40.0))
	{
		OutError = Where + TEXT(": steering_ratio must be between 1 and 40");
		return false;
	}
	Car->TryGetStringField(TEXT("hide_pattern"), OutProfile.HidePattern);

	const TSharedPtr<FJsonObject>* ClockObject = nullptr;
	if (Car->TryGetObjectField(TEXT("clock"), ClockObject) && ClockObject)
	{
		FSimCruiseClockSpec Clock;
		const TArray<TSharedPtr<FJsonValue>>* Box = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Clear = nullptr;
		if (!(*ClockObject)->TryGetStringField(TEXT("material"), Clock.Material) || !(*ClockObject)->TryGetArrayField(TEXT("box"), Box) || Box->Num() != 4)
		{
			OutError = Where + TEXT(": clock needs 'material' and a four-number 'box'");
			return false;
		}
		Clock.Box = FVector4((*Box)[0]->AsNumber(), (*Box)[1]->AsNumber(), (*Box)[2]->AsNumber(), (*Box)[3]->AsNumber());
		if ((*ClockObject)->TryGetArrayField(TEXT("clear"), Clear) && Clear->Num() == 2)
		{
			Clock.Clear = FVector2D((*Clear)[0]->AsNumber(), (*Clear)[1]->AsNumber());
		}
		(*ClockObject)->TryGetBoolField(TEXT("flip_v"), Clock.bFlipV);
		OutProfile.Clock = Clock;
	}

	const TSharedPtr<FJsonObject>* Paint = nullptr;
	if (Car->TryGetObjectField(TEXT("paint"), Paint) && Paint)
	{
		FSimCruisePaintFinish& Finish = OutProfile.Paint;
		(*Paint)->TryGetStringField(TEXT("name"), Finish.Name);
		(*Paint)->TryGetStringField(TEXT("source"), Finish.Source);
		if (!ReadColorValue((*Paint)->TryGetField(TEXT("base")), Finish.BaseColor))
		{
			OutError = Where + TEXT(".paint: 'base' must be \"#rrggbb\" (sRGB) or [r,g,b] (linear)");
			return false;
		}
		double Value = 0.0;
		if ((*Paint)->TryGetNumberField(TEXT("metallic"), Value)) Finish.Metallic = Value;
		if ((*Paint)->TryGetNumberField(TEXT("base_roughness"), Value)) Finish.BaseRoughness = Value;
		if ((*Paint)->TryGetNumberField(TEXT("flake"), Value)) Finish.Flake = Value;
		if ((*Paint)->TryGetNumberField(TEXT("clear_coat_roughness"), Value)) Finish.ClearCoatRoughness = Value;
		if ((*Paint)->TryGetNumberField(TEXT("orange_peel"), Value)) Finish.OrangePeel = Value;
	}

	const TSharedPtr<FJsonObject>* Roles = nullptr;
	if (Car->TryGetObjectField(TEXT("roles"), Roles) && Roles)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Roles)->Values)
		{
			ESimCruiseCarMaterialRole Role;
			if (!SimCruiseCarMaterialRoles::FromString(Pair.Value->AsString(), Role))
			{
				OutError = FString::Printf(TEXT("%s.roles.%s: unknown role '%s'"), *Where, *Pair.Key, *Pair.Value->AsString());
				return false;
			}
			OutProfile.Roles.Add(Pair.Key, Role);
		}
	}
	const TSharedPtr<FJsonObject>* Tints = nullptr;
	if (Car->TryGetObjectField(TEXT("tints"), Tints) && Tints)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Tints)->Values)
		{
			ESimCruiseCarMaterialRole Role;
			FLinearColor Color;
			if (!SimCruiseCarMaterialRoles::FromString(Pair.Key, Role) || !ReadColorValue(Pair.Value, Color))
			{
				OutError = FString::Printf(TEXT("%s.tints.%s: needs a known role and a colour"), *Where, *Pair.Key);
				return false;
			}
			OutProfile.RoleTints.Add(Role, Color);
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Badges = nullptr;
	if (Car->TryGetArrayField(TEXT("badges"), Badges) && Badges)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Badges)
		{
			if (!Value.IsValid() || Value->Type != EJson::Object)
			{
				continue; // brand names ("mazda") are notes, not areas
			}
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			FSimCruiseBadgeArea& Area = OutProfile.BadgeAreas.AddDefaulted_GetRef();
			Object->TryGetStringField(TEXT("note"), Area.Note);
			Object->TryGetStringField(TEXT("material"), Area.Material);
			const TArray<TSharedPtr<FJsonValue>>* Box = nullptr;
			if (Object->TryGetArrayField(TEXT("box"), Box) && Box && Box->Num() == 2)
			{
				FVector A, B;
				const TArray<TSharedPtr<FJsonValue>>& First = (*Box)[0]->AsArray();
				const TArray<TSharedPtr<FJsonValue>>& Second = (*Box)[1]->AsArray();
				if (First.Num() != 3 || Second.Num() != 3)
				{
					OutError = Where + TEXT(".badges: box needs two [x,y,z] corners");
					return false;
				}
				A = FVector(First[0]->AsNumber(), First[1]->AsNumber(), First[2]->AsNumber());
				B = FVector(Second[0]->AsNumber(), Second[1]->AsNumber(), Second[2]->AsNumber());
				Area.Box = FBox(A.ComponentMin(B), A.ComponentMax(B));
				Area.bHasBox = true;
			}
			if (!Area.bHasBox && Area.Material.IsEmpty())
			{
				OutError = Where + TEXT(".badges: an area needs a 'material', a 'box' or both");
				return false;
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* RoleAreas = nullptr;
	if (Car->TryGetArrayField(TEXT("role_areas"), RoleAreas) && RoleAreas)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RoleAreas)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			FString RoleName;
			const TArray<TSharedPtr<FJsonValue>>* Box = nullptr;
			FSimCruiseRoleArea Area;
			if (!Object.IsValid() || !Object->TryGetStringField(TEXT("role"), RoleName) || !SimCruiseCarMaterialRoles::FromString(RoleName, Area.Role) ||
				!Object->TryGetArrayField(TEXT("box"), Box) || !Box || Box->Num() != 2 ||
				(*Box)[0]->AsArray().Num() != 3 || (*Box)[1]->AsArray().Num() != 3)
			{
				OutError = Where + TEXT(".role_areas: each area needs a known 'role' and a 'box' of two [x,y,z] corners");
				return false;
			}
			Object->TryGetStringField(TEXT("note"), Area.Note);
			Object->TryGetStringField(TEXT("material"), Area.Material);
			const TArray<TSharedPtr<FJsonValue>>& First = (*Box)[0]->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& Second = (*Box)[1]->AsArray();
			const FVector A(First[0]->AsNumber(), First[1]->AsNumber(), First[2]->AsNumber());
			const FVector B(Second[0]->AsNumber(), Second[1]->AsNumber(), Second[2]->AsNumber());
			Area.Box = FBox(A.ComponentMin(B), A.ComponentMax(B));
			OutProfile.RoleAreas.Add(Area);
		}
	}
	const TSharedPtr<FJsonObject>* Steering = nullptr;
	if (Car->TryGetObjectField(TEXT("steering_wheel"), Steering) && Steering)
	{
		(*Steering)->TryGetBoolField(TEXT("none"), OutProfile.bSteeringWheelNone);
		(*Steering)->TryGetStringField(TEXT("node"), OutProfile.SteeringWheelNode);
		if (ReadVector(*Steering, TEXT("centre"), OutProfile.SteeringWheelCentreM) &&
			ReadVector(*Steering, TEXT("axis"), OutProfile.SteeringWheelAxis))
		{
			OutProfile.bHasSteeringWheelRim = true;
			(*Steering)->TryGetNumberField(TEXT("radius"), OutProfile.SteeringWheelRadiusM);
		}
	}
	return true;
}
