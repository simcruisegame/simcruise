// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseBuildWorldMaterialsCommandlet.h"

#include "Materials/Material.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "World/SimCruiseWorldResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseMaterials, Log, All);

USimCruiseBuildWorldMaterialsCommandlet::USimCruiseBuildWorldMaterialsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 USimCruiseBuildWorldMaterialsCommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	int32 Failures = 0;
	for (int32 i = 0; i < static_cast<int32>(ESimCruiseTileLayer::Count); ++i)
	{
		const ESimCruiseTileLayer Layer = static_cast<ESimCruiseTileLayer>(i);
		const FString Name = TEXT("M_SimCruise") + SimCruiseWorldMaterials::LayerName(Layer);
		const FString PackagePath = TEXT("/Game/World/Materials/") + Name;
		UPackage* Package = CreatePackage(*PackagePath);
		Package->FullyLoad();
		UMaterial* Material = SimCruiseWorldMaterials::BuildMaterial(Package, FName(*Name), Layer, RF_Public | RF_Standalone);
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Material, *File, Args))
		{
			UE_LOG(LogSimCruiseMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
			continue;
		}
		UE_LOG(LogSimCruiseMaterials, Display, TEXT("Saved %s"), *File);
	}
	return Failures == 0 ? 0 : 1;
#else
	return 1;
#endif
}
