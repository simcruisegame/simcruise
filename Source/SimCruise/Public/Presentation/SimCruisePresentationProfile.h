// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/SimCruiseWheelBuilder.h"
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

/**
 * A box (vehicle frame, metres) whose wholly contained parts take another
 * role: for models that draw several kinds of surface with one texture-atlas
 * material (a tail-lamp lens inside the body's atlas cannot light otherwise).
 */
struct FSimCruiseRoleArea
{
	FString Note;
	/** Only parts of this author material, or any material when empty. */
	FString Material;
	ESimCruiseCarMaterialRole Role = ESimCruiseCarMaterialRole::Authored;
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
 * data stays in data/cars/<id>.json; this file holds only how the car looks.
 */
/** A dashboard clock drawn over the model's display texture (SimCruiseCarClock). */
struct FSimCruiseClockSpec
{
	/** Author material name of the display that carries the painted time. */
	FString Material;
	/** The digits' box in the texture: u0, v0, u1, v1 (0..1, v down). */
	FVector4 Box = FVector4(0.0, 0.0, 1.0, 1.0);
	/** A texture point of plain LCD background. */
	FVector2D Clear = FVector2D(0.5, 0.5);
	/** The mesh shows the texture upside down. */
	bool bFlipV = false;
	/**
	 * A radio display sharing the texture on its wrapped copy (V below 0): the FM station
	 * it is tuned to, MHz (0: the copy shows the clock too).
	 */
	double RadioMHz = 0.0;
};

/**
 * A digital instrument screen (the C8's cluster): the model's painted picture with the live speed in
 * digits and the tachometer's lit arc drawn over it by a material (no per-frame drawing). Positions
 * are texture pixels as the screen reads (v up when bFlipV), angles clockwise from 12 o'clock.
 */
struct FSimCruiseScreenSpec
{
	FString Material;
	FVector2D TextureSize = FVector2D(1024.0, 512.0);
	bool bFlipV = false;
	/** The tachometer: centre, the lit band's inner and outer radius, the angle of 0 r/min and degrees per 1000 r/min. */
	FVector2D TachCentre = FVector2D::ZeroVector;
	FVector2D TachBand = FVector2D(0.0, 0.0);
	double TachStartDeg = -135.0;
	double TachDegPerThousand = 37.5;
	/** The painted (static) lit arc to erase first, from TachStartDeg to this angle. */
	double TachPaintedEndDeg = -90.0;
	/** The speed digits' box: x0, y0, x1, y1. */
	FVector4 SpeedBox = FVector4(0.0, 0.0, 0.0, 0.0);
	/** Nits of the screen's own light. */
	double Backlight = 120.0;
};

/**
 * A dashboard dial drawn live over the model's own (whose needle is part of the dash mesh): a face
 * with ticks, numerals and a needle at the car's value, just in front of the model's dial hub.
 * Centre, normal, radius and offset in the vehicle frame (Godot axes: x right, y up, z rearward; metres), as a steering-wheel rim is given.
 */
struct FSimCruiseDialSpec
{
	/** speed_mph, speed_kmh, rpm, coolant, fuel, oil_pressure. */
	FString Quantity;
	FVector Centre = FVector::ZeroVector;
	/** Toward the driver. */
	FVector Normal = FVector(0, 0, 1);
	/** The face's radius, m. */
	double Radius = 0.05;
	/** Where the face is drawn along the normal from the centre (the model's hub), m. */
	double Offset = 0.0015;
	/** Scale: the value at the start and the end of the sweep. */
	double MinValue = 0.0;
	double MaxValue = 1.0;
	/** Needle sweep: start angle clockwise from 12 o'clock as the driver sees it, and its span, degrees. */
	double StartDeg = -135.0;
	double SweepDeg = 270.0;
	/** Ticks: major every MajorStep (labelled, divided by LabelDivisor), MinorPerMajor between. */
	double MajorStep = 1.0;
	int32 MinorPerMajor = 1;
	double LabelDivisor = 1.0;
	/** Red from this value (0 = none). */
	double RedFrom = 0.0;
	/** Words at the ends instead of numerals (C/H, E/F, L/H), and a caption under the hub. */
	FString StartLabel;
	FString EndLabel;
	FString Caption;
	/** Draw a face over the model's dial (its own needle is welded in); false keeps the model's face and adds only the needle. */
	bool bFace = true;
	/** The model material its own (fixed) needle is made of: the rig removes that needle (parts of it touching the hub). */
	FString HideModelNeedle;
	/** Above 0: a straight (ribbon) scale this long (m) across the dial's face, the needle a bar that slides along it; Radius is then half the face's height. */
	double LengthM = 0.0;
	bool IsLinear() const { return LengthM > 0.0; }
};
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
	TArray<FSimCruiseRoleArea> RoleAreas;
	/** Extra case-insensitive regex applied to surface labels to hide them. */
	FString HidePattern;

	/**
	 * For a car that only parks (no driving data in data/cars): the dimensions the model
	 * is fitted to (metres), from the maker's specifications.
	 */
	struct FParkedDimensions
	{
		double WheelbaseM = 0.0, FrontTrackM = 0.0, RearTrackM = 0.0, TyreRadiusM = 0.0, TyreWidthM = 0.0;
		FString Source;
	};
	TOptional<FParkedDimensions> Parked;

	/**
	 * The engine's voice when it is not the default inline four: a cross-plane V8, the
	 * bank of each firing event in firing order (0 the driver's side, whose pipe is
	 * nearer), and how loud each bank's pipe reaches the listener.
	 */
	struct FEngineSound
	{
		TArray<int32> BankOfEvent;
		double NearGain = 1.0;
		double FarGain = 0.62;
		FString Source;
	};
	TOptional<FEngineSound> EngineSound;

	/** Set when the model's dashboard has a clock to drive. */
	TOptional<FSimCruiseClockSpec> Clock;
	/** A digital instrument screen with live speed and tachometer. */
	TOptional<FSimCruiseScreenSpec> Screen;

	/** Dials drawn live over the model's (speedometer, tachometer, gauges). */
	TArray<FSimCruiseDialSpec> Dials;

	/** Steering-wheel override: node-name substring, explicit rim (vehicle frame, metres) or none. */
	FString SteeringWheelNode;
	bool bSteeringWheelNone = false;
	bool bHasSteeringWheelRim = false;
	FVector SteeringWheelCentreM = FVector::ZeroVector;
	/** Column axis toward the driver. */
	FVector SteeringWheelAxis = FVector(-1, 0, 0);
	double SteeringWheelRadiusM = 0.19;

	/**
	 * Parts the model lacks, drawn from simple shapes (an owner's roll bar, wing): a tube
	 * between two points, or a box at a centre, in the vehicle frame (X forward, Y right,
	 * Z up, metres, origin midway between the axles on the ground).
	 */
	struct FAddon
	{
		FString Name;
		bool bBox = false;
		FVector FromM = FVector::ZeroVector;
		FVector ToM = FVector::ZeroVector;
		double RadiusM = 0.02;
		FVector CentreM = FVector::ZeroVector;
		FVector SizeM = FVector(0.1);
		/** Box rotation about the car's Y axis, degrees (a wing's angle of attack, nose down positive). */
		double PitchDeg = 0.0;
		FLinearColor Colour = FLinearColor(0.02f, 0.02f, 0.02f);
		double Metallic = 0.0;
		double Roughness = 0.45;
	};
	TArray<FAddon> Addons;

	/** A wheel design that replaces the model's rims and tyres (the brake discs stay). */
	FSimCruiseWheelDesign WheelDesign;

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
