// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/SimCruiseCarMaterialRoles.h"
#include "Presentation/SimCruiseGltfModel.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "UObject/StrongObjectPtr.h"

class UStaticMesh;
struct FSimCruiseRigPart;
struct FSimCruiseRigSection;

/** A rigged part turned into render meshes: opaque/masked surfaces and, separately, glass and lenses. */
struct FSimCruiseBuiltPart
{
	FName Name;
	FTransform PartToVehicle = FTransform::Identity;
	TStrongObjectPtr<UStaticMesh> Mesh;
	/** glTF material index for each material slot of Mesh. */
	TArray<int32> MeshMaterials;
	TStrongObjectPtr<UStaticMesh> Glass;
	TArray<int32> GlassMaterials;
	int32 Triangles = 0;
	bool bNanite = false;
};

/**
 * One car's presentation assets, built once per process from its glTF and
 * shared by every instance of the car (materials are per instance so lamps
 * can light independently).
 */
struct FSimCruiseBuiltCar
{
	FString CarId;
	FSimCruiseCarPresentationProfile Profile;
	/** The model's materials and image paths (geometry is dropped after the build). */
	FSimCruiseGltfModel Model;
	TMap<int32, ESimCruiseCarMaterialRole> Roles;
	FSimCruiseBuiltPart Body;
	FSimCruiseBuiltPart Wheels[4];
	FSimCruiseBuiltPart Uprights[4];
	FSimCruiseBuiltPart SteeringWheel;
	bool bHasSteeringWheel = false;
	FVector HubsCm[4];
	FVector DriverEyeCm = FVector::ZeroVector;
	double SteeringRatio = 15.0;
	TArray<FString> Log;
	double BuildSeconds = 0.0;

	ESimCruiseCarMaterialRole RoleOf(int32 MaterialIndex) const
	{
		const ESimCruiseCarMaterialRole* Role = Roles.Find(MaterialIndex);
		return Role ? *Role : ESimCruiseCarMaterialRole::Authored;
	}
};

namespace SimCruiseCarAssets
{
	/**
	 * The built car for an id (game/cars/<id>.json + its presentation
	 * profile), from the cache or built now. Editor builds use the full mesh
	 * build (Nanite for big parts, cached in the DDC); cooked games use the
	 * fast runtime path.
	 */
	SIMCRUISE_API TSharedPtr<FSimCruiseBuiltCar> Get(const FString& CarId, FString& OutError);

	SIMCRUISE_API void ClearCache();

	/** Build a transient static mesh from rig sections (one material slot per section). */
	SIMCRUISE_API UStaticMesh* BuildStaticMesh(const FString& Name, const TArray<const FSimCruiseRigSection*>& Sections, bool bNanite, bool bFullPrecisionUVs = false);

	/** Triangle count from which a part is drawn with Nanite (editor builds). */
	constexpr int32 NaniteMinTriangles = 20000;
}
