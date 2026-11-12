// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/CarDefinitionLoader.h"

#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SimCruiseCarData
{
	static bool Fail(FString& OutError, const FString& Path, const FString& Field, const FString& Reason)
	{
		OutError = FString::Printf(TEXT("%s: %s: %s"), *Path, *Field, *Reason);
		return false;
	}

	static const TSharedPtr<FJsonValue>* FindValue(const FJsonObject& Json, const TCHAR* Key)
	{
		return Json.Values.Find(Key);
	}

	static bool ReadNumberIfPresent(const FJsonObject& Json, const TCHAR* Key, double& OutValue,
		const FString& Path, FString& OutError)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Json, Key);
		if (!Value)
		{
			return true;
		}
		if (!Value->IsValid() || (*Value)->Type != EJson::Number)
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be a JSON number"));
		}
		OutValue = (*Value)->AsNumber();
		if (!FMath::IsFinite(OutValue))
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be finite"));
		}
		return true;
	}

	static bool ReadIntegerIfPresent(const FJsonObject& Json, const TCHAR* Key, int32& OutValue,
		const FString& Path, FString& OutError)
	{
		double Number = static_cast<double>(OutValue);
		if (!ReadNumberIfPresent(Json, Key, Number, Path, OutError))
		{
			return false;
		}
		if (!FindValue(Json, Key))
		{
			return true;
		}
		if (Number < 0.0 || Number > static_cast<double>(MAX_int32))
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be a non-negative 32-bit integer"));
		}
		const int32 Integer = static_cast<int32>(Number);
		if (static_cast<double>(Integer) != Number)
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be an integer"));
		}
		OutValue = Integer;
		return true;
	}

	static bool ReadStringIfPresent(const FJsonObject& Json, const TCHAR* Key, FString& OutValue,
		const FString& Path, FString& OutError)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Json, Key);
		if (!Value)
		{
			return true;
		}
		if (!Value->IsValid() || (*Value)->Type != EJson::String)
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be a JSON string"));
		}
		OutValue = (*Value)->AsString();
		return true;
	}

	static bool ReadBoolIfPresent(const FJsonObject& Json, const TCHAR* Key, bool& OutValue,
		const FString& Path, FString& OutError)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Json, Key);
		if (!Value)
		{
			return true;
		}
		if (!Value->IsValid() || (*Value)->Type != EJson::Boolean)
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be a JSON boolean"));
		}
		OutValue = (*Value)->AsBool();
		return true;
	}

	static bool ReadNumberArrayIfPresent(const FJsonObject& Json, const TCHAR* Key, TArray<double>& OutValues,
		const FString& Path, FString& OutError)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Json, Key);
		if (!Value)
		{
			return true;
		}
		if (!Value->IsValid() || (*Value)->Type != EJson::Array)
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be a JSON array"));
		}
		TArray<double> Candidate;
		for (int32 Index = 0; Index < (*Value)->AsArray().Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Item = (*Value)->AsArray()[Index];
			if (!Item.IsValid() || Item->Type != EJson::Number || !FMath::IsFinite(Item->AsNumber()))
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field '%s[%d]'"), Key, Index), TEXT("must be a finite JSON number"));
			}
			Candidate.Add(Item->AsNumber());
		}
		OutValues = MoveTemp(Candidate);
		return true;
	}

	static bool ReadStringArrayIfPresent(const FJsonObject& Json, const TCHAR* Key, TArray<FString>& OutValues,
		const FString& Path, FString& OutError)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Json, Key);
		if (!Value)
		{
			return true;
		}
		if (!Value->IsValid() || (*Value)->Type != EJson::Array)
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must be a JSON array"));
		}
		TArray<FString> Candidate;
		for (int32 Index = 0; Index < (*Value)->AsArray().Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Item = (*Value)->AsArray()[Index];
			if (!Item.IsValid() || Item->Type != EJson::String)
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field '%s[%d]'"), Key, Index), TEXT("must be a JSON string"));
			}
			Candidate.Add(Item->AsString());
		}
		OutValues = MoveTemp(Candidate);
		return true;
	}

	static bool ReadVector3IfPresent(const FJsonObject& Json, const TCHAR* Key, FVector3d& OutValue,
		const FString& Path, FString& OutError)
	{
		TArray<double> Values;
		if (!ReadNumberArrayIfPresent(Json, Key, Values, Path, OutError))
		{
			return false;
		}
		if (!FindValue(Json, Key))
		{
			return true;
		}
		if (Values.Num() != 3)
		{
			return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Key), TEXT("must contain exactly three values"));
		}
		OutValue = FVector3d(Values[0], Values[1], Values[2]);
		return true;
	}

	static bool ReadTorqueCurveIfPresent(const FJsonObject& Json, TArray<FSimCruiseTorqueCurvePoint>& OutCurve,
		const FString& Path, FString& OutError)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Json, TEXT("torque_curve"));
		if (!Value)
		{
			return true;
		}
		if (!Value->IsValid() || (*Value)->Type != EJson::Array)
		{
			return Fail(OutError, Path, TEXT("field 'torque_curve'"), TEXT("must be an array of [rpm, Nm] pairs"));
		}
		TArray<FSimCruiseTorqueCurvePoint> Candidate;
		for (int32 Index = 0; Index < (*Value)->AsArray().Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Row = (*Value)->AsArray()[Index];
			if (!Row.IsValid() || Row->Type != EJson::Array || Row->AsArray().Num() != 2 ||
				!Row->AsArray()[0].IsValid() || Row->AsArray()[0]->Type != EJson::Number ||
				!Row->AsArray()[1].IsValid() || Row->AsArray()[1]->Type != EJson::Number)
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field 'torque_curve[%d]'"), Index), TEXT("must be a numeric [rpm, Nm] pair"));
			}
			Candidate.Add({Row->AsArray()[0]->AsNumber(), Row->AsArray()[1]->AsNumber()});
		}
		OutCurve = MoveTemp(Candidate);
		return true;
	}

	static bool ReadDamperDynoIfPresent(const FJsonObject& Json,
		TArray<TArray<FSimCruiseDamperDynoPoint>>& OutTables, const FString& Path, FString& OutError)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Json, TEXT("damper_dyno"));
		if (!Value)
		{
			return true;
		}
		if (!Value->IsValid() || (*Value)->Type != EJson::Array)
		{
			return Fail(OutError, Path, TEXT("field 'damper_dyno'"), TEXT("must be an array with front and rear tables"));
		}
		TArray<TArray<FSimCruiseDamperDynoPoint>> Candidate;
		for (int32 Axle = 0; Axle < (*Value)->AsArray().Num(); ++Axle)
		{
			const TSharedPtr<FJsonValue>& Table = (*Value)->AsArray()[Axle];
			if (!Table.IsValid() || Table->Type != EJson::Array)
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field 'damper_dyno[%d]'"), Axle), TEXT("must be an array of [in/s, compression lbf, rebound lbf] rows"));
			}
			TArray<FSimCruiseDamperDynoPoint> Points;
			for (int32 PointIndex = 0; PointIndex < Table->AsArray().Num(); ++PointIndex)
			{
				const TSharedPtr<FJsonValue>& Row = Table->AsArray()[PointIndex];
				if (!Row.IsValid() || Row->Type != EJson::Array || Row->AsArray().Num() != 3)
				{
					return Fail(OutError, Path, FString::Printf(TEXT("field 'damper_dyno[%d][%d]'"), Axle, PointIndex), TEXT("must contain three values"));
				}
				for (const TSharedPtr<FJsonValue>& Number : Row->AsArray())
				{
					if (!Number.IsValid() || Number->Type != EJson::Number || !FMath::IsFinite(Number->AsNumber()))
					{
						return Fail(OutError, Path, FString::Printf(TEXT("field 'damper_dyno[%d][%d]'"), Axle, PointIndex), TEXT("must contain finite numbers"));
					}
				}
				Points.Add({Row->AsArray()[0]->AsNumber(), Row->AsArray()[1]->AsNumber(), Row->AsArray()[2]->AsNumber()});
			}
			Candidate.Add(MoveTemp(Points));
		}
		OutTables = MoveTemp(Candidate);
		return true;
	}

	static bool RequireFields(const FJsonObject& Json, const FString& Path, FString& OutError)
	{
		static const TCHAR* Required[] = {
			TEXT("id"), TEXT("name"), TEXT("make"), TEXT("body"), TEXT("seats"),
			TEXT("mass_kg"), TEXT("front_weight"), TEXT("cg_height_m"), TEXT("inertia"),
			TEXT("wheelbase_m"), TEXT("track_front_m"), TEXT("track_rear_m"),
			TEXT("tire_width_m"), TEXT("tire_aspect_ratio"), TEXT("rim_in"),
			TEXT("static_mu"), TEXT("engine"), TEXT("torque_curve"), TEXT("gears"),
			TEXT("reverse_ratio"), TEXT("final_drive"), TEXT("transmission"), TEXT("layout"),
			TEXT("target_0_60_s"), TEXT("target_70_0_ft"), TEXT("target_skidpad_g"), TEXT("sources")
		};
		for (const TCHAR* Field : Required)
		{
			if (!Json.Values.Contains(Field))
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Field), TEXT("is required"));
			}
		}
		return true;
	}

	static bool CheckAxlePair(const TArray<double>& Values, const TCHAR* Field, const FString& Path, FString& OutError)
	{
		return Values.Num() == 2 || Fail(OutError, Path, FString::Printf(TEXT("field '%s'"), Field), TEXT("must contain [front, rear] values"));
	}

	static bool Validate(const FSimCruiseCarDefinition& Car, const FString& Path, FString& OutError)
	{
		if (Car.Id.IsEmpty() || Car.Name.IsEmpty() || Car.Body.IsEmpty())
		{
			return Fail(OutError, Path, TEXT("identity"), TEXT("id, name, and body must not be empty"));
		}
		if (Car.SeatCount <= 0 || Car.MassKg <= 0.0 || Car.CenterOfGravityHeightM <= 0.0 || Car.WheelbaseM <= 0.0 ||
			Car.FrontTrackM <= 0.0 || Car.RearTrackM <= 0.0 || Car.TireSectionWidthM <= 0.0 ||
			Car.TireAspectRatio <= 0.0 || Car.RimDiameterIn <= 0.0)
		{
			return Fail(OutError, Path, TEXT("physical dimensions"), TEXT("mass, dimensions, tire size, and seat count must be positive"));
		}
		if (Car.FrontWeightFraction <= 0.0 || Car.FrontWeightFraction >= 1.0)
		{
			return Fail(OutError, Path, TEXT("field 'front_weight'"), TEXT("must be a fraction strictly between 0 and 1"));
		}
		if (Car.InertiaKgM2.X <= 0.0 || Car.InertiaKgM2.Y <= 0.0 || Car.InertiaKgM2.Z <= 0.0 ||
			Car.CollisionBoxSizeM.X <= 0.0 || Car.CollisionBoxSizeM.Y <= 0.0 || Car.CollisionBoxSizeM.Z <= 0.0)
		{
			return Fail(OutError, Path, TEXT("inertia/collision_box"), TEXT("all three SI values must be positive"));
		}
		if (Car.PeakLongitudinalFrictionCoefficient <= 0.0 || Car.MinimumFrictionCoefficient < 0.0 ||
			Car.MaximumFrictionCoefficient < Car.MinimumFrictionCoefficient || Car.ReferenceCornerLoadN <= 0.0 ||
			Car.WheelRotationalInertiaKgM2 <= 0.0 || Car.TireRelaxationLengthM <= 0.0)
		{
			return Fail(OutError, Path, TEXT("tire data"), TEXT("friction, reference load, wheel inertia, or relaxation length is outside its valid range"));
		}
		if (Car.DrivelineEfficiencyFraction <= 0.0 || Car.DrivelineEfficiencyFraction > 1.0 ||
			Car.ReverseGearRatio <= 0.0 || Car.FinalDriveRatio <= 0.0 || Car.ForwardGearRatios.IsEmpty())
		{
			return Fail(OutError, Path, TEXT("gearbox data"), TEXT("ratios must be positive and efficiency must be in (0, 1]"));
		}
		if (Car.DrivetrainLayout != TEXT("rwd") && Car.DrivetrainLayout != TEXT("fwd") && Car.DrivetrainLayout != TEXT("awd"))
		{
			return Fail(OutError, Path, TEXT("field 'layout'"), TEXT("must be rwd, fwd, or awd"));
		}
		if (Car.EngineKind != TEXT("combustion") && Car.EngineKind != TEXT("electric"))
		{
			return Fail(OutError, Path, TEXT("field 'engine'"), TEXT("must be combustion or electric"));
		}
		if (Car.EngineKind == TEXT("electric") && Car.ElectricMotorPeakTorqueNm <= 0.0 && Car.ElectricMotorPeakPowerW <= 0.0)
		{
			return Fail(OutError, Path, TEXT("motor data"), TEXT("electric cars require positive peak torque or peak power"));
		}
		if (Car.EngineKind == TEXT("combustion") && Car.EngineTorqueCurve.Num() < 2)
		{
			return Fail(OutError, Path, TEXT("field 'torque_curve'"), TEXT("combustion cars require at least two curve points"));
		}
		for (int32 Index = 0; Index < Car.ForwardGearRatios.Num(); ++Index)
		{
			if (Car.ForwardGearRatios[Index] <= 0.0)
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field 'gears[%d]'"), Index), TEXT("must be a positive ratio"));
			}
		}
		for (int32 Index = 0; Index < Car.EngineTorqueCurve.Num(); ++Index)
		{
			const FSimCruiseTorqueCurvePoint& Point = Car.EngineTorqueCurve[Index];
			if (Point.EngineSpeedRpm < 0.0 || Point.EngineTorqueNm < 0.0 ||
				(Index > 0 && Point.EngineSpeedRpm <= Car.EngineTorqueCurve[Index - 1].EngineSpeedRpm))
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field 'torque_curve[%d]'"), Index), TEXT("rpm must increase and rpm/torque must be non-negative"));
			}
		}
		if (Car.TargetZeroToSixtySeconds <= 0.0 || Car.TargetSeventyToZeroFeet <= 0.0 || Car.TargetSkidpadG <= 0.0)
		{
			return Fail(OutError, Path, TEXT("calibration targets"), TEXT("all three targets must be positive"));
		}

		const TArray<double>* AxlePairs[] = {
			&Car.AxleGripMultiplier, &Car.CoilSpringRateLbfPerInByAxle, &Car.SpringLeverRatioByAxle,
			&Car.WheelSpringRateNPerMByAxle, &Car.AntiRollBarDiameterMByAxle,
			&Car.AntiRollBarTorsionLengthMByAxle, &Car.AntiRollBarArmLengthMByAxle,
			&Car.AntiRollBarLeverRatioByAxle, &Car.AntiRollBarRateNmPerMByAxle,
			&Car.StaticCamberDegByAxle, &Car.CamberGainDegPerMByAxle,
			&Car.DamperReboundCriticalFractionByAxle, &Car.DamperCompressionCriticalFractionByAxle,
			&Car.DamperKneeSpeedMPerSecondByAxle, &Car.DamperKneeSlopeByAxle
		};
		const TCHAR* AxleFieldNames[] = {
			TEXT("axle_grip"), TEXT("coil_lb_in"), TEXT("lever_ratio"), TEXT("wheel_rate_n_m"),
			TEXT("bar_diameter_m"), TEXT("bar_torsion_length_m"), TEXT("bar_arm_m"), TEXT("bar_lever_ratio"),
			TEXT("bar_rate_n_m"), TEXT("static_camber_deg"), TEXT("camber_per_m"),
			TEXT("damper_ratio_rebound"), TEXT("damper_ratio_compression"),
			TEXT("damper_knee_mps"), TEXT("damper_knee_slope")
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(AxlePairs); ++Index)
		{
			if (!CheckAxlePair(*AxlePairs[Index], AxleFieldNames[Index], Path, OutError))
			{
				return false;
			}
		}
		if (Car.DamperModeByAxle.Num() != 2 || Car.DamperDynoByAxle.Num() != 2)
		{
			return Fail(OutError, Path, TEXT("damper data"), TEXT("must contain front and rear axle entries"));
		}
		for (int32 Axle = 0; Axle < 2; ++Axle)
		{
			if (Car.DamperModeByAxle[Axle] != TEXT("ratio") && Car.DamperModeByAxle[Axle] != TEXT("dyno"))
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field 'damper[%d]'"), Axle), TEXT("must be ratio or dyno"));
			}
			double PreviousSpeed = -1.0;
			for (int32 PointIndex = 0; PointIndex < Car.DamperDynoByAxle[Axle].Num(); ++PointIndex)
			{
				const FSimCruiseDamperDynoPoint& Point = Car.DamperDynoByAxle[Axle][PointIndex];
				if (Point.ShaftSpeedInPerSecond < 0.0 || Point.CompressionForceLbf < 0.0 || Point.ReboundForceLbf < 0.0 ||
					Point.ShaftSpeedInPerSecond <= PreviousSpeed)
				{
					return Fail(OutError, Path, FString::Printf(TEXT("field 'damper_dyno[%d][%d]'"), Axle, PointIndex), TEXT("speed must increase and all source values must be non-negative"));
				}
				PreviousSpeed = Point.ShaftSpeedInPerSecond;
			}
			if (Car.DamperModeByAxle[Axle] == TEXT("dyno") && Car.DamperDynoByAxle[Axle].Num() < 2)
			{
				return Fail(OutError, Path, FString::Printf(TEXT("field 'damper_dyno[%d]'"), Axle), TEXT("dyno mode needs at least two points"));
			}
		}
		if (Car.MinimumSuspensionTravelM < 0.0 || Car.MaximumSuspensionTravelM <= Car.MinimumSuspensionTravelM ||
			Car.StaticSuspensionLengthM < Car.MinimumSuspensionTravelM || Car.StaticSuspensionLengthM > Car.MaximumSuspensionTravelM ||
			Car.MaximumTireContactLoadN <= 0.0 || Car.UnsprungMassKgPerCorner < 0.0)
		{
			return Fail(OutError, Path, TEXT("suspension data"), TEXT("travel, contact-load, or unsprung-mass values are inconsistent"));
		}
		if (!Car.SourceAnnotations.IsValid() || Car.SourcesEstimates.IsEmpty())
		{
			return Fail(OutError, Path, TEXT("field 'sources.estimates'"), TEXT("source and estimates annotations are required"));
		}
		if (Car.Id != TEXT("nb1999") && Car.ModelScene.IsEmpty())
		{
			return Fail(OutError, Path, TEXT("field 'model_scene'"), TEXT("a model identifier is required for non-built-in car bodies"));
		}
		return true;
	}
}

namespace SimCruiseCarData
{
	/** Physical fields a calibration overlay may set even when the car file leaves them at their default. */
	static bool IsKnownOverlayField(const FString& Key)
	{
		static const TCHAR* const Fields[] = {
			TEXT("mass_kg"), TEXT("front_weight"), TEXT("cg_height_m"), TEXT("inertia"), TEXT("angular_damp"),
			TEXT("static_mu"), TEXT("lateral_grip_scale"), TEXT("load_sensitivity"), TEXT("reference_corner_load_n"), TEXT("axle_grip"),
			TEXT("mu_min"), TEXT("mu_max"), TEXT("slip_stiffness"), TEXT("slip_falloff"), TEXT("slip_falloff_from"), TEXT("slip_falloff_to"),
			TEXT("camber_thrust"), TEXT("longitudinal_stiffness"), TEXT("relaxation_m"), TEXT("slip_ratio_peak"), TEXT("slip_ratio_falloff"),
			TEXT("relaxation_load_exponent"), TEXT("wheel_inertia"), TEXT("rolling_resistance"), TEXT("brake_front_nm"), TEXT("brake_rear_nm"),
			TEXT("parking_brake_nm"), TEXT("drag_area_m2"), TEXT("engine_inertia"), TEXT("engine_friction_nm"), TEXT("engine_friction_per_rpm"),
			TEXT("driveline_efficiency"), TEXT("shift_seconds"), TEXT("clutch_stiffness"), TEXT("clutch_capacity_nm"), TEXT("clutch_back_nm"),
			TEXT("launch_rpm"), TEXT("launch_rpm_per_throttle"), TEXT("converter_stall_ratio"), TEXT("converter_coupling_ratio"), TEXT("launch_control_rpm"), TEXT("manual_clutch_stiffness"), TEXT("front_diff_bias"), TEXT("rear_diff_bias"),
			TEXT("center_bias"), TEXT("tc_target_slip"), TEXT("tc_gain"), TEXT("wheel_rate_n_m"), TEXT("bar_rate_n_m"), TEXT("bump_gap_m"),
			TEXT("bump_stiffness"), TEXT("static_camber_deg"), TEXT("camber_per_m"), TEXT("caster_deg"), TEXT("toe_per_wheel_deg"),
			TEXT("unsprung_corner_kg"), TEXT("damper_ratio_rebound"), TEXT("damper_ratio_compression"), TEXT("damper_knee_mps"),
			TEXT("damper_knee_slope") };
		for (const TCHAR* Field : Fields)
		{
			if (Key == Field)
			{
				return true;
			}
		}
		return false;
	}
}

bool FSimCruiseCarDefinitionLoader::LoadById(const FString& CarId,
	FSimCruiseCarDefinition& OutCar, FString& OutError)
{
	OutError.Reset();
	bool bContainsInvalidCharacter = false;
	for (const TCHAR Character : CarId)
	{
		if (!(FChar::IsAlnum(Character) || Character == TEXT('_') || Character == TEXT('-')))
		{
			bContainsInvalidCharacter = true;
			break;
		}
	}
	if (CarId.IsEmpty() || bContainsInvalidCharacter)
	{
		return SimCruiseCarData::Fail(OutError, TEXT("data/cars"), TEXT("car id"), TEXT("may contain only letters, digits, underscores, and hyphens"));
	}
	const FString CarPath = FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("cars"), CarId + TEXT(".json"));
	if (!LoadFromFile(CarPath, OutCar, OutError))
	{
		return false;
	}
	if (OutCar.Id != CarId)
	{
		return SimCruiseCarData::Fail(OutError, CarPath, TEXT("field 'id'"),
			FString::Printf(TEXT("does not match requested id '%s'"), *CarId));
	}

	// Unreal calibration overlay: Content/Vehicle/Calibration/<id>.json replaces
	// named fields of the canonical car file (estimated fields only, each with
	// its reason in "_note"). SIMCRUISE_CALIBRATION_DIR points experiments at
	// another folder instead (development builds only: a shipped car's physics
	// does not change with the environment).
	FString OverlayDirectory;
#if !UE_BUILD_SHIPPING
	OverlayDirectory = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_CALIBRATION_DIR"));
#endif
	if (OverlayDirectory.IsEmpty())
	{
		OverlayDirectory = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Vehicle"), TEXT("Calibration"));
	}
	const FString OverlayPath = FPaths::Combine(OverlayDirectory, CarId + TEXT(".json"));
	if (!FPaths::FileExists(OverlayPath))
	{
		return true;
	}
	FString OverlayText;
	TSharedPtr<FJsonObject> Overlay;
	if (!FFileHelper::LoadFileToString(OverlayText, *OverlayPath) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OverlayText), Overlay) || !Overlay.IsValid())
	{
		return SimCruiseCarData::Fail(OutError, OverlayPath, TEXT("JSON"), TEXT("calibration overlay could not be read"));
	}
	TSharedRef<FJsonObject> Merged = MakeShared<FJsonObject>(*OutCar.OriginalDocument);
	TArray<FString> Changes;
	for (const auto& Field : Overlay->Values)
	{
		const FString Key(*Field.Key);
		if (Key.StartsWith(TEXT("_")))
		{
			continue;
		}
		if (!Merged->HasField(Key) && !SimCruiseCarData::IsKnownOverlayField(Key))
		{
			return SimCruiseCarData::Fail(OutError, OverlayPath, FString::Printf(TEXT("field '%s'"), *Key), TEXT("is not a car field"));
		}
		FString Before = TEXT("default");
		if (const TSharedPtr<FJsonValue> Existing = Merged->TryGetField(Key))
		{
			Before = Existing->Type == EJson::Number ? FString::SanitizeFloat(Existing->AsNumber()) : TEXT("(value)");
		}
		const FString After = Field.Value->Type == EJson::Number ? FString::SanitizeFloat(Field.Value->AsNumber()) : TEXT("(value)");
		Changes.Add(FString::Printf(TEXT("%s %s -> %s"), *Key, *Before, *After));
		Merged->SetField(Key, Field.Value);
	}
	FString MergedText;
	FJsonSerializer::Serialize(Merged, TJsonWriterFactory<>::Create(&MergedText));
	const FString BaseText = OutCar.OriginalJson;
	if (!ParseJson(MergedText, CarPath + TEXT(" + ") + OverlayPath, OutCar, OutError))
	{
		return false;
	}
	OutCar.OriginalJson = BaseText;
	OutCar.CalibrationOverlayPath = OverlayPath;
	OutCar.CalibrationOverlayChanges = FString::Join(Changes, TEXT("; "));
	Overlay->TryGetStringField(TEXT("_note"), OutCar.CalibrationOverlayNote);
	return true;
}

bool FSimCruiseCarDefinitionLoader::LoadFromFile(const FString& FilePath,
	FSimCruiseCarDefinition& OutCar, FString& OutError)
{
	FString JsonText;
	if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
	{
		return SimCruiseCarData::Fail(OutError, FilePath, TEXT("file"), TEXT("could not be read"));
	}
	return ParseJson(JsonText, FilePath, OutCar, OutError);
}

bool FSimCruiseCarDefinitionLoader::ParseJson(const FString& JsonText, const FString& SourcePath,
	FSimCruiseCarDefinition& OutCar, FString& OutError)
{
	OutError.Reset();
	const FString Path = SourcePath.IsEmpty() ? TEXT("<memory>") : SourcePath;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return SimCruiseCarData::Fail(OutError, Path, TEXT("JSON"), Reader->GetErrorMessage());
	}
	if (!SimCruiseCarData::RequireFields(*Root, Path, OutError))
	{
		return false;
	}

	FSimCruiseCarDefinition Candidate;
#define SIMCRUISE_READ_NUMBER(JsonKey, Member) \
	if (!SimCruiseCarData::ReadNumberIfPresent(*Root, TEXT(JsonKey), Candidate.Member, Path, OutError)) return false
#define SIMCRUISE_READ_INTEGER(JsonKey, Member) \
	if (!SimCruiseCarData::ReadIntegerIfPresent(*Root, TEXT(JsonKey), Candidate.Member, Path, OutError)) return false
#define SIMCRUISE_READ_STRING(JsonKey, Member) \
	if (!SimCruiseCarData::ReadStringIfPresent(*Root, TEXT(JsonKey), Candidate.Member, Path, OutError)) return false
#define SIMCRUISE_READ_BOOL(JsonKey, Member) \
	if (!SimCruiseCarData::ReadBoolIfPresent(*Root, TEXT(JsonKey), Candidate.Member, Path, OutError)) return false
#define SIMCRUISE_READ_ARRAY(JsonKey, Member) \
	if (!SimCruiseCarData::ReadNumberArrayIfPresent(*Root, TEXT(JsonKey), Candidate.Member, Path, OutError)) return false

	SIMCRUISE_READ_STRING("id", Id);
	SIMCRUISE_READ_STRING("name", Name);
	SIMCRUISE_READ_STRING("make", Make);
	SIMCRUISE_READ_STRING("body", Body);
	SIMCRUISE_READ_STRING("model_scene", ModelScene);
	SIMCRUISE_READ_INTEGER("seats", SeatCount);
	if (!SimCruiseCarData::ReadVector3IfPresent(*Root, TEXT("driver_eye"), Candidate.DriverEyeM, Path, OutError) ||
		!SimCruiseCarData::ReadVector3IfPresent(*Root, TEXT("inertia"), Candidate.InertiaKgM2, Path, OutError) ||
		!SimCruiseCarData::ReadVector3IfPresent(*Root, TEXT("collision_box"), Candidate.CollisionBoxSizeM, Path, OutError))
	{
		return false;
	}

	SIMCRUISE_READ_NUMBER("mass_kg", MassKg);
	SIMCRUISE_READ_NUMBER("front_weight", FrontWeightFraction);
	SIMCRUISE_READ_NUMBER("cg_height_m", CenterOfGravityHeightM);
	SIMCRUISE_READ_NUMBER("collision_box_y", CollisionShapeCenterYM);
	SIMCRUISE_READ_NUMBER("angular_damp", AngularDampingPerSecond);
	SIMCRUISE_READ_NUMBER("wheelbase_m", WheelbaseM);
	SIMCRUISE_READ_NUMBER("track_front_m", FrontTrackM);
	SIMCRUISE_READ_NUMBER("track_rear_m", RearTrackM);
	SIMCRUISE_READ_NUMBER("mount_height_m", SuspensionMountHeightM);
	SIMCRUISE_READ_NUMBER("hub_drop_m", VisualHubDropM);
	SIMCRUISE_READ_NUMBER("tire_width_m", TireSectionWidthM);
	SIMCRUISE_READ_NUMBER("tire_aspect_ratio", TireAspectRatio);
	SIMCRUISE_READ_NUMBER("rim_in", RimDiameterIn);
	SIMCRUISE_READ_NUMBER("static_mu", PeakLongitudinalFrictionCoefficient);
	SIMCRUISE_READ_NUMBER("lateral_grip_scale", LateralGripScale);
	SIMCRUISE_READ_NUMBER("load_sensitivity", TireLoadSensitivityFraction);
	SIMCRUISE_READ_NUMBER("reference_corner_load_n", ReferenceCornerLoadN);
	SIMCRUISE_READ_ARRAY("axle_grip", AxleGripMultiplier);
	SIMCRUISE_READ_NUMBER("mu_min", MinimumFrictionCoefficient);
	SIMCRUISE_READ_NUMBER("mu_max", MaximumFrictionCoefficient);
	SIMCRUISE_READ_NUMBER("slip_stiffness", SlipAngleStiffnessPerRad);
	SIMCRUISE_READ_NUMBER("slip_falloff", SlipAngleFalloffFraction);
	SIMCRUISE_READ_NUMBER("slip_falloff_from", SlipAngleFalloffStartRad);
	SIMCRUISE_READ_NUMBER("slip_falloff_to", SlipAngleFalloffEndRad);
	SIMCRUISE_READ_NUMBER("camber_thrust", CamberThrustCoefficientPerRad);
	SIMCRUISE_READ_NUMBER("longitudinal_stiffness", LongitudinalSlipStiffness);
	SIMCRUISE_READ_NUMBER("relaxation_m", TireRelaxationLengthM);
	SIMCRUISE_READ_NUMBER("slip_ratio_peak", PeakSlipRatio);
	SIMCRUISE_READ_NUMBER("slip_ratio_falloff", SlipRatioFalloffFraction);
	SIMCRUISE_READ_NUMBER("relaxation_load_exponent", RelaxationLoadExponent);
	SIMCRUISE_READ_BOOL("abs_enabled", bAbsEnabled);
	SIMCRUISE_READ_NUMBER("wheel_inertia", WheelRotationalInertiaKgM2);
	SIMCRUISE_READ_NUMBER("rolling_resistance", RollingResistanceCoefficient);
	SIMCRUISE_READ_NUMBER("parking_rack_deg", ParkingRackAngleDeg);
	SIMCRUISE_READ_NUMBER("road_reference_deg", RoadReferenceSteerAngleDeg);
	SIMCRUISE_READ_NUMBER("ackermann_blend", AckermannBlendFraction);
	SIMCRUISE_READ_NUMBER("assist_lateral_g", SteeringAssistLateralG);
	SIMCRUISE_READ_NUMBER("limit_slip_surplus_deg", SteeringSlipLimitSurplusDeg);
	SIMCRUISE_READ_NUMBER("brake_front_nm", FrontBrakeTorqueNm);
	SIMCRUISE_READ_NUMBER("brake_rear_nm", RearBrakeTorqueNm);
	SIMCRUISE_READ_NUMBER("parking_brake_nm", ParkingBrakeTorqueNm);
	SIMCRUISE_READ_NUMBER("drag_area_m2", DragAreaM2);

	SIMCRUISE_READ_STRING("engine", EngineKind);
	if (!SimCruiseCarData::ReadTorqueCurveIfPresent(*Root, Candidate.EngineTorqueCurve, Path, OutError)) return false;
	SIMCRUISE_READ_NUMBER("torque_past_curve_nm", TorquePastCurveNm);
	SIMCRUISE_READ_NUMBER("idle_rpm", IdleSpeedRpm);
	SIMCRUISE_READ_NUMBER("idle_omega", RestartEngineSpeedRadPerSecond);
	SIMCRUISE_READ_NUMBER("governor_rpm", GovernorSpeedRpm);
	SIMCRUISE_READ_NUMBER("fuel_cut_rpm", FuelCutSpeedRpm);
	SIMCRUISE_READ_NUMBER("max_gear_rpm", MaximumGearSpeedRpm);
	SIMCRUISE_READ_NUMBER("engine_inertia", EngineRotationalInertiaKgM2);
	SIMCRUISE_READ_NUMBER("engine_friction_nm", EngineFrictionTorqueNm);
	SIMCRUISE_READ_NUMBER("engine_friction_per_rpm", EngineFrictionTorqueNmPerRpm);
	SIMCRUISE_READ_NUMBER("min_omega_auto", MinimumAutomaticEngineSpeedRadPerSecond);
	SIMCRUISE_READ_NUMBER("max_omega", MaximumEngineSpeedRadPerSecond);
	SIMCRUISE_READ_NUMBER("stall_omega", StallEngineSpeedRadPerSecond);
	SIMCRUISE_READ_NUMBER("motor_peak_torque_nm", ElectricMotorPeakTorqueNm);
	SIMCRUISE_READ_NUMBER("motor_peak_power_w", ElectricMotorPeakPowerW);
	SIMCRUISE_READ_NUMBER("motor_max_rpm", ElectricMotorMaximumSpeedRpm);
	SIMCRUISE_READ_NUMBER("regen_torque_nm", ElectricRegenerativeTorqueNm);
	SIMCRUISE_READ_ARRAY("gears", ForwardGearRatios);
	SIMCRUISE_READ_NUMBER("reverse_ratio", ReverseGearRatio);
	SIMCRUISE_READ_NUMBER("final_drive", FinalDriveRatio);
	SIMCRUISE_READ_NUMBER("driveline_efficiency", DrivelineEfficiencyFraction);
	SIMCRUISE_READ_NUMBER("shift_seconds", GearShiftDurationS);
	SIMCRUISE_READ_NUMBER("upshift_rpm", AutomaticUpshiftSpeedRpm);
	SIMCRUISE_READ_NUMBER("upshift_min_omega", AutomaticUpshiftMinimumEngineSpeedRadPerSecond);
	SIMCRUISE_READ_NUMBER("downshift_rpm", AutomaticDownshiftSpeedRpm);
	SIMCRUISE_READ_NUMBER("upshift_rpm_light", AutomaticLightUpshiftSpeedRpm);
	SIMCRUISE_READ_NUMBER("downshift_rpm_light", AutomaticLightDownshiftSpeedRpm);
	SIMCRUISE_READ_NUMBER("kickdown_rpm", AutomaticKickdownSpeedRpm);
	SIMCRUISE_READ_NUMBER("clutch_stiffness", ClutchCouplingNmPerRadPerSecond);
	SIMCRUISE_READ_NUMBER("clutch_capacity_nm", ClutchCapacityNm);
	SIMCRUISE_READ_NUMBER("clutch_back_nm", ClutchBackTorqueNm);
	SIMCRUISE_READ_NUMBER("launch_rpm", LaunchSpeedRpm);
	SIMCRUISE_READ_NUMBER("launch_rpm_per_throttle", LaunchSpeedRpmPerThrottleFraction);
	SIMCRUISE_READ_NUMBER("converter_stall_ratio", TorqueConverterStallRatio);
	SIMCRUISE_READ_NUMBER("converter_coupling_ratio", TorqueConverterCouplingSpeedRatio);
	SIMCRUISE_READ_NUMBER("launch_control_rpm", LaunchControlRpm);
	SIMCRUISE_READ_NUMBER("manual_clutch_stiffness", ManualClutchCouplingNmPerRadPerSecond);
	SIMCRUISE_READ_STRING("transmission", TransmissionKind);
	SIMCRUISE_READ_STRING("layout", DrivetrainLayout);
	SIMCRUISE_READ_NUMBER("awd_front_share", AwdFrontTorqueShareFraction);
	SIMCRUISE_READ_NUMBER("front_diff_bias", FrontDifferentialBiasRatio);
	SIMCRUISE_READ_NUMBER("rear_diff_bias", RearDifferentialBiasRatio);
	SIMCRUISE_READ_NUMBER("center_bias", CenterDifferentialBiasRatio);
	SIMCRUISE_READ_BOOL("traction_control", bTractionControlEnabled);
	SIMCRUISE_READ_NUMBER("tc_target_slip", TractionControlTargetSlipRatio);
	SIMCRUISE_READ_NUMBER("tc_gain", TractionControlIntegralGainPerSecond);

	SIMCRUISE_READ_ARRAY("coil_lb_in", CoilSpringRateLbfPerInByAxle);
	SIMCRUISE_READ_ARRAY("lever_ratio", SpringLeverRatioByAxle);
	SIMCRUISE_READ_ARRAY("wheel_rate_n_m", WheelSpringRateNPerMByAxle);
	SIMCRUISE_READ_ARRAY("bar_diameter_m", AntiRollBarDiameterMByAxle);
	SIMCRUISE_READ_ARRAY("bar_torsion_length_m", AntiRollBarTorsionLengthMByAxle);
	SIMCRUISE_READ_ARRAY("bar_arm_m", AntiRollBarArmLengthMByAxle);
	SIMCRUISE_READ_ARRAY("bar_lever_ratio", AntiRollBarLeverRatioByAxle);
	SIMCRUISE_READ_ARRAY("bar_rate_n_m", AntiRollBarRateNmPerMByAxle);
	SIMCRUISE_READ_NUMBER("static_length_m", StaticSuspensionLengthM);
	SIMCRUISE_READ_NUMBER("min_travel_m", MinimumSuspensionTravelM);
	SIMCRUISE_READ_NUMBER("max_travel_m", MaximumSuspensionTravelM);
	SIMCRUISE_READ_NUMBER("bump_gap_m", BumpStopGapM);
	SIMCRUISE_READ_NUMBER("bump_stiffness", BumpStopStiffnessNPerM2);
	SIMCRUISE_READ_NUMBER("max_contact_load_n", MaximumTireContactLoadN);
	SIMCRUISE_READ_ARRAY("static_camber_deg", StaticCamberDegByAxle);
	SIMCRUISE_READ_ARRAY("camber_per_m", CamberGainDegPerMByAxle);
	SIMCRUISE_READ_NUMBER("caster_deg", CasterAngleDeg);
	SIMCRUISE_READ_NUMBER("toe_per_wheel_deg", ToeAngleDegPerWheel);
	SIMCRUISE_READ_NUMBER("unsprung_corner_kg", UnsprungMassKgPerCorner);
	if (!SimCruiseCarData::ReadStringArrayIfPresent(*Root, TEXT("damper"), Candidate.DamperModeByAxle, Path, OutError) ||
		!SimCruiseCarData::ReadDamperDynoIfPresent(*Root, Candidate.DamperDynoByAxle, Path, OutError))
	{
		return false;
	}
	SIMCRUISE_READ_ARRAY("damper_ratio_rebound", DamperReboundCriticalFractionByAxle);
	SIMCRUISE_READ_ARRAY("damper_ratio_compression", DamperCompressionCriticalFractionByAxle);
	SIMCRUISE_READ_ARRAY("damper_knee_mps", DamperKneeSpeedMPerSecondByAxle);
	SIMCRUISE_READ_ARRAY("damper_knee_slope", DamperKneeSlopeByAxle);
	SIMCRUISE_READ_NUMBER("target_0_60_s", TargetZeroToSixtySeconds);
	SIMCRUISE_READ_NUMBER("target_70_0_ft", TargetSeventyToZeroFeet);
	SIMCRUISE_READ_NUMBER("target_skidpad_g", TargetSkidpadG);
	SIMCRUISE_READ_STRING("calibration_open", CalibrationOpen);

#undef SIMCRUISE_READ_NUMBER
#undef SIMCRUISE_READ_INTEGER
#undef SIMCRUISE_READ_STRING
#undef SIMCRUISE_READ_BOOL
#undef SIMCRUISE_READ_ARRAY

	if (const TSharedPtr<FJsonValue>* Sources = Root->Values.Find(TEXT("sources")))
	{
		if (!Sources->IsValid() || (*Sources)->Type != EJson::Object)
		{
			return SimCruiseCarData::Fail(OutError, Path, TEXT("field 'sources'"), TEXT("must be a JSON object"));
		}
		Candidate.SourceAnnotations = (*Sources)->AsObject();
		if (const TSharedPtr<FJsonValue>* Estimates = Candidate.SourceAnnotations->Values.Find(TEXT("estimates")))
		{
			if (!Estimates->IsValid() || (*Estimates)->Type != EJson::String)
			{
				return SimCruiseCarData::Fail(OutError, Path, TEXT("field 'sources.estimates'"), TEXT("must be a JSON string"));
			}
			Candidate.SourcesEstimates = (*Estimates)->AsString();
		}
	}
	if (const TSharedPtr<FJsonValue>* ModelRig = Root->Values.Find(TEXT("model_rig")))
	{
		if (!ModelRig->IsValid() || (*ModelRig)->Type != EJson::Object)
		{
			return SimCruiseCarData::Fail(OutError, Path, TEXT("field 'model_rig'"), TEXT("must be a JSON object"));
		}
		Candidate.ModelRigOptions = (*ModelRig)->AsObject();
		if (!SimCruiseCarData::ReadBoolIfPresent(*Candidate.ModelRigOptions, TEXT("fit"), Candidate.bModelRigFit, Path, OutError) ||
			!SimCruiseCarData::ReadBoolIfPresent(*Candidate.ModelRigOptions, TEXT("flip"), Candidate.bModelRigFlip, Path, OutError))
		{
			return false;
		}
	}
	Candidate.OriginalJson = JsonText;
	Candidate.OriginalDocument = Root;

	if (!SimCruiseCarData::Validate(Candidate, Path, OutError))
	{
		return false;
	}
	OutCar = MoveTemp(Candidate);
	return true;
}

TArray<FString> FSimCruiseCarDefinitionLoader::FindAvailableIds()
{
	const FString CarsDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT("data"), TEXT("cars"));
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *(CarsDirectory / TEXT("*.json")), true, false);
	for (FString& FileName : FileNames)
	{
		FileName = FPaths::GetBaseFilename(FileName);
	}
	FileNames.Sort();
	return FileNames;
}
