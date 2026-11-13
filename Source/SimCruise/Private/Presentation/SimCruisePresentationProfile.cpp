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

	const TSharedPtr<FJsonObject>* SoundObject = nullptr;
	if (Car->TryGetObjectField(TEXT("engine_sound"), SoundObject) && SoundObject)
	{
		FSimCruiseCarPresentationProfile::FEngineSound Sound;
		const FJsonObject& J = **SoundObject;
		FString Layout;
		J.TryGetStringField(TEXT("layout"), Layout);
		const TArray<TSharedPtr<FJsonValue>>* Banks = nullptr;
		if (Layout != TEXT("v8_crossplane") || !J.TryGetArrayField(TEXT("bank_of_event"), Banks) || !Banks || Banks->Num() != 8)
		{
			OutError = Where + TEXT(": 'engine_sound' needs layout 'v8_crossplane' and 8 'bank_of_event' values (0 or 1)");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& V : *Banks) Sound.BankOfEvent.Add(FMath::Clamp(static_cast<int32>(V->AsNumber()), 0, 1));
		J.TryGetNumberField(TEXT("near_gain"), Sound.NearGain);
		J.TryGetNumberField(TEXT("far_gain"), Sound.FarGain);
		J.TryGetStringField(TEXT("source"), Sound.Source);
		OutProfile.EngineSound = Sound;
	}

	const TSharedPtr<FJsonObject>* ParkedObject = nullptr;
	if (Car->TryGetObjectField(TEXT("parked"), ParkedObject) && ParkedObject)
	{
		FSimCruiseCarPresentationProfile::FParkedDimensions D;
		const FJsonObject& P = **ParkedObject;
		P.TryGetNumberField(TEXT("wheelbase_m"), D.WheelbaseM);
		P.TryGetNumberField(TEXT("front_track_m"), D.FrontTrackM);
		P.TryGetNumberField(TEXT("rear_track_m"), D.RearTrackM);
		P.TryGetNumberField(TEXT("tyre_radius_m"), D.TyreRadiusM);
		P.TryGetNumberField(TEXT("tyre_width_m"), D.TyreWidthM);
		P.TryGetStringField(TEXT("source"), D.Source);
		if (D.WheelbaseM < 1.5 || D.WheelbaseM > 5.0 || D.FrontTrackM < 1.0 || D.RearTrackM < 1.0 || D.TyreRadiusM < 0.2 || D.TyreRadiusM > 0.6)
		{
			OutError = Where + TEXT(": 'parked' needs wheelbase_m, front_track_m, rear_track_m and tyre_radius_m in a car's range");
			return false;
		}
		OutProfile.Parked = D;
	}

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
		(*ClockObject)->TryGetNumberField(TEXT("radio_mhz"), Clock.RadioMHz);
		OutProfile.Clock = Clock;
	}

	const TSharedPtr<FJsonObject>* ScreenObject = nullptr;
	if (Car->TryGetObjectField(TEXT("screen"), ScreenObject) && ScreenObject)
	{
		FSimCruiseScreenSpec Screen;
		auto Pair = [&](const TCHAR* Name, FVector2D& Out) -> bool
		{
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (!(*ScreenObject)->TryGetArrayField(Name, A) || A->Num() != 2) return false;
			Out = FVector2D((*A)[0]->AsNumber(), (*A)[1]->AsNumber());
			return true;
		};
		const TArray<TSharedPtr<FJsonValue>>* Box = nullptr;
		if (!(*ScreenObject)->TryGetStringField(TEXT("material"), Screen.Material) || !Pair(TEXT("tach_centre_px"), Screen.TachCentre) || !Pair(TEXT("tach_band_px"), Screen.TachBand)
			|| !(*ScreenObject)->TryGetArrayField(TEXT("speed_box_px"), Box) || Box->Num() != 4)
		{
			OutError = Where + TEXT(": screen needs 'material', 'tach_centre_px', 'tach_band_px' and a four-number 'speed_box_px'");
			return false;
		}
		Screen.SpeedBox = FVector4((*Box)[0]->AsNumber(), (*Box)[1]->AsNumber(), (*Box)[2]->AsNumber(), (*Box)[3]->AsNumber());
		Pair(TEXT("texture_px"), Screen.TextureSize);
		(*ScreenObject)->TryGetBoolField(TEXT("flip_v"), Screen.bFlipV);
		(*ScreenObject)->TryGetNumberField(TEXT("tach_start_deg"), Screen.TachStartDeg);
		(*ScreenObject)->TryGetNumberField(TEXT("tach_deg_per_1000"), Screen.TachDegPerThousand);
		(*ScreenObject)->TryGetNumberField(TEXT("tach_painted_end_deg"), Screen.TachPaintedEndDeg);
		(*ScreenObject)->TryGetNumberField(TEXT("backlight_nits"), Screen.Backlight);
		OutProfile.Screen = Screen;
	}

	const TArray<TSharedPtr<FJsonValue>>* Dials = nullptr;
	if (Car->TryGetArrayField(TEXT("dials"), Dials) && Dials)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Dials)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Object) || !Object)
			{
				continue;
			}
			const TSharedPtr<FJsonObject>& D = *Object;
			FSimCruiseDialSpec Dial;
			const TArray<TSharedPtr<FJsonValue>>* Centre = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Normal = nullptr;
			if (!D->TryGetStringField(TEXT("quantity"), Dial.Quantity) || !D->TryGetArrayField(TEXT("centre_m"), Centre) || Centre->Num() != 3
				|| !D->TryGetNumberField(TEXT("radius_m"), Dial.Radius))
			{
				OutError = Where + TEXT(": a dial needs 'quantity', a three-number 'centre_m' and 'radius_m'");
				return false;
			}
			Dial.Centre = FVector((*Centre)[0]->AsNumber(), (*Centre)[1]->AsNumber(), (*Centre)[2]->AsNumber());
			if (D->TryGetArrayField(TEXT("normal"), Normal) && Normal->Num() == 3)
			{
				Dial.Normal = FVector((*Normal)[0]->AsNumber(), (*Normal)[1]->AsNumber(), (*Normal)[2]->AsNumber()).GetSafeNormal();
			}
			D->TryGetNumberField(TEXT("offset_m"), Dial.Offset);
			D->TryGetNumberField(TEXT("min"), Dial.MinValue);
			D->TryGetNumberField(TEXT("max"), Dial.MaxValue);
			D->TryGetNumberField(TEXT("start_deg"), Dial.StartDeg);
			D->TryGetNumberField(TEXT("sweep_deg"), Dial.SweepDeg);
			D->TryGetNumberField(TEXT("major_step"), Dial.MajorStep);
			D->TryGetNumberField(TEXT("minor_per_major"), Dial.MinorPerMajor);
			D->TryGetNumberField(TEXT("label_divisor"), Dial.LabelDivisor);
			D->TryGetNumberField(TEXT("red_from"), Dial.RedFrom);
			D->TryGetStringField(TEXT("start_label"), Dial.StartLabel);
			D->TryGetStringField(TEXT("end_label"), Dial.EndLabel);
			D->TryGetStringField(TEXT("caption"), Dial.Caption);
		D->TryGetBoolField(TEXT("face"), Dial.bFace);
		D->TryGetStringField(TEXT("hide_model_needle"), Dial.HideModelNeedle);
		D->TryGetNumberField(TEXT("length_m"), Dial.LengthM);
			OutProfile.Dials.Add(Dial);
		}
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
	const TSharedPtr<FJsonObject>* Design = nullptr;
	if (Car->TryGetObjectField(TEXT("wheel_design"), Design) && Design)
	{
		FSimCruiseWheelDesign& W = OutProfile.WheelDesign;
		(*Design)->TryGetStringField(TEXT("style"), W.Style);
		(*Design)->TryGetNumberField(TEXT("rim_diameter_in"), W.RimDiameterIn);
		(*Design)->TryGetNumberField(TEXT("rim_width_in"), W.RimWidthIn);
		(*Design)->TryGetNumberField(TEXT("lip_in"), W.LipIn);
		(*Design)->TryGetNumberField(TEXT("dish_in"), W.DishIn);
		(*Design)->TryGetNumberField(TEXT("spokes"), W.Spokes);
		(*Design)->TryGetStringField(TEXT("source"), W.Source);
		if (W.Style != TEXT("mesh"))
		{
			OutError = Where + TEXT(": wheel_design style must be \"mesh\"");
			return false;
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* AddonList = nullptr;
	if (Car->TryGetArrayField(TEXT("addons"), AddonList) && AddonList)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddonList)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() ? Value->AsObject() : nullptr;
			if (!Object.IsValid()) continue;
			FSimCruiseCarPresentationProfile::FAddon Addon;
			Object->TryGetStringField(TEXT("name"), Addon.Name);
			const TSharedPtr<FJsonObject>* Tube = nullptr;
			const TSharedPtr<FJsonObject>* Box = nullptr;
			if (Object->TryGetObjectField(TEXT("tube"), Tube) && Tube && ReadVector(*Tube, TEXT("from"), Addon.FromM) && ReadVector(*Tube, TEXT("to"), Addon.ToM))
			{
				(*Tube)->TryGetNumberField(TEXT("radius"), Addon.RadiusM);
			}
			else if (Object->TryGetObjectField(TEXT("box"), Box) && Box && ReadVector(*Box, TEXT("centre"), Addon.CentreM) && ReadVector(*Box, TEXT("size"), Addon.SizeM))
			{
				Addon.bBox = true;
				(*Box)->TryGetNumberField(TEXT("pitch_deg"), Addon.PitchDeg);
			}
			else
			{
				OutError = Where + TEXT(": an addon needs a tube (from, to, radius) or a box (centre, size)");
				return false;
			}
			ReadColorValue(Object->TryGetField(TEXT("colour")), Addon.Colour);
			Object->TryGetNumberField(TEXT("metallic"), Addon.Metallic);
			Object->TryGetNumberField(TEXT("roughness"), Addon.Roughness);
			OutProfile.Addons.Add(Addon);
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
