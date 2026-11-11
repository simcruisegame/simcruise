// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/SimCruiseCarMaterialRoles.h"

struct FSimCruiseGltfModel;
struct FSimCruiseCarPresentationProfile;
struct FSimCruiseCarDefinition;

/** Wheel order shared with the physics: front left, front right, rear left, rear right. */
enum class ESimCruiseWheel : uint8 { FrontLeft = 0, FrontRight = 1, RearLeft = 2, RearRight = 3 };

/** The car dimensions the rig fits a model to (SI units). */
struct FSimCruiseRigInputs
{
	double WheelbaseM = 2.266;
	double FrontTrackM = 1.414;
	double RearTrackM = 1.44;
	double TireRadiusM = 0.288;
	double TireWidthM = 0.195;
	/** Hub height above the vehicle origin at static ride (mount height minus static suspension length). */
	double HubHeightM = 0.293;
	/** Driver's eye, vehicle frame (X forward, Y right, Z up), metres. */
	FVector DriverEyeM = FVector(-0.503, -0.347, 1.009);

	SIMCRUISE_API static FSimCruiseRigInputs FromCar(const FSimCruiseCarDefinition& Car);
};

/** One material's triangles in a part's local frame (Unreal axes, centimetres). */
struct FSimCruiseRigSection
{
	int32 MaterialIndex = INDEX_NONE;
	ESimCruiseCarMaterialRole Role = ESimCruiseCarMaterialRole::Authored;
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	/** Tangent xyz and binormal sign (Unreal convention), or empty to compute. */
	TArray<FVector4f> Tangents;
	TArray<FVector2f> UV0;
	TArray<uint32> Indices;

	int32 NumTriangles() const { return Indices.Num() / 3; }
};

/** A rigid piece of the car with its own pivot in the vehicle frame. */
struct FSimCruiseRigPart
{
	FName Name;
	/** Pivot in the vehicle frame (Unreal axes, centimetres). */
	FTransform PartToVehicle = FTransform::Identity;
	TArray<FSimCruiseRigSection> Sections;

	int32 NumTriangles() const;
	FBox LocalBounds() const;
	bool IsEmpty() const { return Sections.Num() == 0; }
};

/**
 * A downloaded car model fitted to a car's dimensions, in the vehicle frame
 * used by the presentation component: X forward, Y right, Z up, centimetres,
 * origin midway between the axles at the height of the physics body origin
 * (hubs sit at FSimCruiseRigInputs::HubHeightM at static ride).
 */
struct FSimCruiseRiggedCar
{
	FSimCruiseRigPart Body;
	/** Spinning parts (tyre, rim, disc), origin at the hub, axle along Y. */
	FSimCruiseRigPart Wheels[4];
	/** Non-spinning parts that steer and travel with the wheel (calipers). */
	FSimCruiseRigPart Uprights[4];
	/** Origin at the rim centre, X along the column away from the driver, Z toward the rim's top. */
	FSimCruiseRigPart SteeringWheel;
	bool bHasSteeringWheel = false;
	double SteeringWheelRadiusM = 0.0;

	/** Hub centres at static ride, vehicle frame, centimetres. */
	FVector HubsCm[4];
	bool bNamedWheels = false;
	double ScaleMetresPerUnit = 1.0;
	/** Extra scale across the car applied to match the published track (1 = none). */
	double LateralScale = 1.0;
	/** Measured on the fitted model, for comparison with the car's data. */
	double ModelFrontTrackM = 0.0;
	double ModelRearTrackM = 0.0;
	double ModelTyreRadiusM = 0.0;
	double ModelLengthM = 0.0;
	double ModelWidthM = 0.0;
	double ModelHeightM = 0.0;
	int32 BadgeTrianglesRemoved = 0;
	int32 HiddenSurfaces = 0;
	/**
	 * Materials the rig adds after the model's own (index Model.Materials.Num() + I):
	 * copies of an author material given another role by a profile role area.
	 */
	struct FExtraMaterial
	{
		int32 Source = INDEX_NONE;
		ESimCruiseCarMaterialRole Role = ESimCruiseCarMaterialRole::Authored;
	};
	TArray<FExtraMaterial> ExtraMaterials;
	TArray<FString> Log;

	int64 NumTriangles() const;
};

namespace SimCruiseCarModelRig
{
	/**
	 * Fit a model: find the wheels (by node name, else by the four ground
	 * contacts refined by the tyre surfaces), scale to the wheelbase, face
	 * forward, set the static hub height, cut the wheels and calipers out,
	 * hide badges, find the steering wheel and convert to Unreal axes.
	 */
	SIMCRUISE_API bool Build(const FSimCruiseGltfModel& Model, const FSimCruiseCarPresentationProfile& Profile,
		const FSimCruiseRigInputs& Inputs, FSimCruiseRiggedCar& OutCar, FString& OutError);

	/**
	 * Where the head-lamp beams leave the car: the centre of the lit head-lamp
	 * lenses (LampFront surfaces ahead of the front axle) on each side, vehicle
	 * frame, cm, left first. Empty when the model has no head-lamp surfaces;
	 * one side found alone is mirrored.
	 */
	SIMCRUISE_API TArray<FVector> HeadLampCentresCm(const FSimCruiseRiggedCar& Car);

	/** Godot vehicle frame (X right, Y up, Z rearward, metres) to Unreal (X forward, Y right, Z up, centimetres). */
	inline FVector GodotToUnrealCm(const FVector& G) { return FVector(-G.Z, G.X, G.Y) * 100.0; }
	inline FVector GodotToUnrealDir(const FVector& G) { return FVector(-G.Z, G.X, G.Y); }
	inline FVector UnrealMToGodot(const FVector& U) { return FVector(U.Y, U.Z, -U.X); }
}
