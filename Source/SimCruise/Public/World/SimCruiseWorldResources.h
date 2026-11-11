// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "World/SimCruiseTileBuilder.h"
#include "SimCruiseWorldResources.generated.h"

class UMaterialInterface;
class UMaterial;
class UTexture2D;

/**
 * Materials and shared textures of the streamed world.
 *
 * Materials are defined in code (SimCruiseWorldMaterials::BuildMaterial):
 * one HLSL node per surface (ground, asphalt, concrete, paint, water) reading
 * the photograph, the land-cover mask and CC0 detail photographs, with
 * world-anchored texture coordinates so detail runs on across tile seams.
 * The commandlet SimCruiseBuildWorldMaterials saves them as assets under
 * /Game/World/Materials for cooked builds; an editor build creates them in
 * memory when the assets are absent, so a fresh checkout runs without them.
 *
 * Detail textures are the repository's licensed photographs (Credits.txt):
 * data/art/ground/asphalt.jpg (Poly Haven Asphalt Floor, CC0), data/art/ground/grass-detail.jpg
 * and data/art/ground/mineral-ground.png (Poly Haven Sandy Gravel 02, CC0).
 */
UCLASS()
class SIMCRUISE_API USimCruiseWorldResources : public UObject
{
	GENERATED_BODY()

public:
	/** Load materials and detail textures (game thread). */
	void Initialise();

	UMaterialInterface* GetLayerMaterial(ESimCruiseTileLayer Layer) const;

	/** A cached aerial photograph by key, or null. */
	UTexture2D* FindAerial(const FString& Key) const;
	/** Create and cache an aerial texture from decoded pixels (game thread). */
	UTexture2D* AddAerial(const FString& Key, const TArray<FColor>& Pixels, int32 Width, int32 Height);
	void TrimAerials(const TSet<FString>& Keep);

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInterface>> LayerMaterials;

	UPROPERTY()
	TObjectPtr<UTexture2D> AsphaltTexture;
	UPROPERTY()
	TObjectPtr<UTexture2D> GrassTexture;
	UPROPERTY()
	TObjectPtr<UTexture2D> SandTexture;
	UPROPERTY()
	TObjectPtr<UTexture2D> BlankTexture;

	FLinearColor AsphaltMean = FLinearColor(0.2f, 0.2f, 0.2f);
	FLinearColor GrassMean = FLinearColor(0.2f, 0.2f, 0.2f);
	FLinearColor SandMean = FLinearColor(0.2f, 0.2f, 0.2f);

	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> Aerials;

	/** Decode a JPEG or PNG file to BGRA8 (any thread). */
	static bool DecodeImageFile(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);

	/** A texture with a full box-filtered mip chain (game thread). */
	static UTexture2D* CreateTexture(UObject* Outer, FName Name, const TArray<FColor>& Pixels, int32 Width, int32 Height, bool bSRGB, bool bWrap);

	/** Mean linear colour of sRGB pixels. */
	static FLinearColor MeanLinear(const TArray<FColor>& Pixels);
};

namespace SimCruiseWorldMaterials
{
	/** Asset path of a layer's material (/Game/World/Materials/M_SimCruise<Layer>). */
	SIMCRUISE_API FString AssetPath(ESimCruiseTileLayer Layer);
	SIMCRUISE_API FString LayerName(ESimCruiseTileLayer Layer);

#if WITH_EDITOR
	/** Build a layer's material in code (editor builds). */
	SIMCRUISE_API UMaterial* BuildMaterial(UObject* Outer, FName Name, ESimCruiseTileLayer Layer, EObjectFlags Flags);
#endif
}
