// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * A minimal, strict glTF 2.0 reader for the downloaded car models
 * (game/art/traffic/<id>/scene.gltf + scene.bin + textures/).
 *
 * It keeps the data in glTF's own space (right-handed, +Y up, metres) with
 * every node transform applied, so the car rig can port game/ModelRig.gd's
 * geometry logic line for line. Unreal's axes and centimetres are applied
 * only when the rig writes its parts (SimCruiseCarModelRig).
 *
 * Supported: .gltf with external or data-URI buffers and .glb; float, byte
 * and short accessors (normalized or not); matrix or TRS nodes; triangles,
 * triangle strips and fans; KHR_materials_clearcoat and
 * KHR_materials_transmission factors. Sparse accessors, morph targets and
 * skins are not used by car models and are rejected or ignored.
 */
struct FSimCruiseGltfMaterial
{
	FString Name;
	/** Linear RGBA, as glTF stores it. */
	FLinearColor BaseColorFactor = FLinearColor::White;
	float MetallicFactor = 1.f;
	float RoughnessFactor = 1.f;
	FLinearColor EmissiveFactor = FLinearColor::Black;
	/** Image indices into FSimCruiseGltfModel::ImageFiles, or INDEX_NONE. */
	int32 BaseColorImage = INDEX_NONE;
	int32 MetallicRoughnessImage = INDEX_NONE;
	int32 NormalImage = INDEX_NONE;
	int32 OcclusionImage = INDEX_NONE;
	int32 EmissiveImage = INDEX_NONE;
	int32 TransmissionImage = INDEX_NONE;
	float NormalScale = 1.f;
	/** "OPAQUE", "MASK" or "BLEND". */
	FString AlphaMode = TEXT("OPAQUE");
	float AlphaCutoff = 0.5f;
	bool bDoubleSided = false;
	float ClearcoatFactor = 0.f;
	float ClearcoatRoughnessFactor = 0.f;
	float TransmissionFactor = 0.f;
};

struct FSimCruiseGltfPrimitive
{
	int32 MaterialIndex = INDEX_NONE;
	/** glTF node name of the mesh's node. */
	FString NodeName;
	/** Lower-case " node parent grandparent ... mesh" names, like ModelRig.gd's owner_name. */
	FString OwnerLabel;
	/** Node-to-root transform (row-vector convention: v' = v * M), glTF space. */
	FMatrix NodeToRoot = FMatrix::Identity;
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	/** xyz tangent, w handedness (glTF convention), or empty. */
	TArray<FVector4f> Tangents;
	TArray<FVector2f> UV0;
	/** Triangle list indices into Positions. */
	TArray<uint32> Indices;

	int32 NumTriangles() const { return Indices.Num() / 3; }
};

struct FSimCruiseGltfModel
{
	FString SourcePath;
	TArray<FSimCruiseGltfMaterial> Materials;
	TArray<FSimCruiseGltfPrimitive> Primitives;
	/** Absolute paths of the images (external files only). */
	TArray<FString> ImageFiles;
	/** asset.extras: title/author/license/source when the exporter wrote them. */
	TMap<FString, FString> AssetExtras;

	int64 NumTriangles() const;
};

namespace SimCruiseGltf
{
	/** Load a .gltf (with its buffers) or .glb. Returns false with a path-specific error. */
	SIMCRUISE_API bool Load(const FString& Path, FSimCruiseGltfModel& OutModel, FString& OutError);
}
