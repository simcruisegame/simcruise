// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/SimCruiseCarMaterialRoles.h"

/** A box (vehicle frame: X forward, Y right, Z up, metres) whose wholly contained parts are badges. */
struct FSimCruiseBadgeArea
{
	FString Note;
	/** Only parts of this author material, or any material when empty. */
	FString Material;
	bool bHasBox = false;
	FBox Box = FBox(ForceInit);
};

/** The finish a car's paint role is given. Colours are linear. */
struct FSimCruisePaintFinish
{
	FString Name = TEXT("Paint");
	FLinearColor BaseColor = FLinearColor(0.3f, 0.3f, 0.3f);
	/** 0 solid colour .. 1 strongly metallic base coat. */
	float Metallic = 0.f;
	/** Base-coat roughness under the clear coat. */
	float BaseRoughness = 0.4f;
	/** Visibility of the metallic/mica flakes, 0..1. */
	float Flake = 0.f;
	float ClearCoatRoughness = 0.03f;
	/** Orange-peel strength of the clear coat, 0..1. */
	float OrangePeel = 0.35f;
	FString Source;
};

/**
 * Per-car presentation data read from Content/Vehicles/CarPresentation.json:
 * which downloaded model the car uses, how its author materials map to
 * material roles, its paint, badge areas and steering-wheel ratio. Physics
 * data stays in game/cars/<id>.json; this file holds only how the car looks.
 */
struct FSimCruiseCarPresentationProfile
{
	FString CarId;
	/** glTF path relative to the project directory. */
	FString ModelPath;
	FString Credit;
	/** Set when the model faces backwards relative to geometric wheel discovery. */
	TOptional<bool> bFlip;
	bool bRelabelWheels = false;
	/** Scale the model across the car so its track matches the car data (within 10 %). */
	bool bFitTrack = true;
	/** Find and remove maker emblems on the nose, tail and wheel centres by shape and place. */
	bool bAutoBadges = true;
	/** Steering-wheel turns per road-wheel turn (the maker's overall ratio). */
	double SteeringRatio = 15.0;
	FString SteeringRatioSource;
	FSimCruisePaintFinish Paint;
	/** Author material name (exact) -> role. */
	TMap<FString, ESimCruiseCarMaterialRole> Roles;
	/** Per-role base colour overrides (linear), e.g. cloth or interior plastic shade. */
	TMap<ESimCruiseCarMaterialRole, FLinearColor> RoleTints;
	TArray<FSimCruiseBadgeArea> BadgeAreas;
	/** Extra case-insensitive regex applied to surface labels to hide them. */
	FString HidePattern;

	/** Steering-wheel override: node-name substring, explicit rim (vehicle frame, metres) or none. */
	FString SteeringWheelNode;
	bool bSteeringWheelNone = false;
	bool bHasSteeringWheelRim = false;
	FVector SteeringWheelCentreM = FVector::ZeroVector;
	/** Column axis toward the driver. */
	FVector SteeringWheelAxis = FVector(-1, 0, 0);
	double SteeringWheelRadiusM = 0.19;

	/** The role for an author material: the profile's override, else the name guess. */
	ESimCruiseCarMaterialRole RoleFor(const FString& MaterialName, const FString& OwnerLabel) const;
};

namespace SimCruisePresentationProfiles
{
	/** Path of the profile file (Content/Vehicles/CarPresentation.json). */
	SIMCRUISE_API FString ProfilePath();
	SIMCRUISE_API bool Load(const FString& CarId, FSimCruiseCarPresentationProfile& OutProfile, FString& OutError);
	SIMCRUISE_API TArray<FString> AvailableIds();
	/** "#rrggbb" (sRGB) or [r,g,b] linear -> linear colour. */
	SIMCRUISE_API bool ParseColor(const FString& Text, FLinearColor& OutColor);
}
