// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Presentation/SimCruiseCarMaterialRoles.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
struct FSimCruiseGltfMaterial;
struct FSimCruiseGltfModel;
struct FSimCruiseCarPresentationProfile;

/** The three shared master materials every car surface is an instance of. */
enum class ESimCruiseCarMaster : uint8
{
	/** Clear-coat lit, opaque, two-sided, world-space normal with procedural surface detail. */
	Opaque,
	/** As Opaque but alpha-masked (grilles, meshes with cut-outs). */
	Masked,
	/** Thin translucent glass and lamp lenses: coloured transmittance and Fresnel reflection. */
	Glass,
};

/**
 * Builds and caches the car master materials and makes role instances.
 *
 * The masters are generated from C++ (a material graph around two HLSL
 * custom nodes) so they merge like code: in editor builds they are built in
 * memory on first use; cooked games load the saved copies under
 * /Game/Vehicles/Materials (SimCruise.SaveCarMaterials writes them).
 *
 * Surface detail is procedural in each part's local space (centimetres):
 * paint flakes and orange peel under/over the clear coat, pebble grain on
 * plastics, leather grain, cloth weave, brushed metal, lathe marks on brake
 * discs, tyre rubber. Every detail fades into roughness as it shrinks below
 * a pixel, so distant panels stay clean instead of shimmering.
 */
namespace SimCruiseCarMaterials
{
	SIMCRUISE_API UMaterialInterface* GetMaster(ESimCruiseCarMaster Master);

	/** The master a role draws with (glass, lamps and dark glass use Glass). */
	SIMCRUISE_API ESimCruiseCarMaster MasterFor(ESimCruiseCarMaterialRole Role, const FSimCruiseGltfMaterial* Author);

	/**
	 * A material instance for one author material in its role. Textures are
	 * loaded from the glTF's image files (sRGB for colour, linear for normal
	 * and metallic-roughness maps) and cached by path.
	 */
	SIMCRUISE_API UMaterialInstanceDynamic* MakeInstance(UObject* Outer, ESimCruiseCarMaterialRole Role,
		const FSimCruiseGltfModel& Model, int32 MaterialIndex, const FSimCruiseCarPresentationProfile& Profile);

	/** Turn a rear-lamp (or any lamp) instance on: 0 off, 1 tail lamp, 2 brake lamp. */
	SIMCRUISE_API void SetLampLevel(UMaterialInstanceDynamic* Lamp, ESimCruiseCarMaterialRole Role, float Level);

	/** Load an image file as a texture (editor: with mips and compression; game: transient). */
	SIMCRUISE_API UTexture2D* LoadTexture(const FString& File, bool bSRGB, bool bNormalMap);

#if WITH_EDITOR
	/** Save the generated masters as assets under /Game/Vehicles/Materials for cooking. */
	SIMCRUISE_API bool SaveMasters(FString& OutReport);
#endif
}
