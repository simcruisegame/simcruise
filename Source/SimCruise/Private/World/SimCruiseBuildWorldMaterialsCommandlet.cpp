// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseBuildWorldMaterialsCommandlet.h"

#include "Materials/Material.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "World/SimCruiseRegionalTerrain.h"
#include "World/SimCruiseTrees.h"
#include "World/SimCruiseWorldResources.h"
#include "Presentation/SimCruiseCarClock.h"
#include "Presentation/SimCruiseCarMaterialLibrary.h"
#include "Presentation/SimCruiseCarMirror.h"
#include "Presentation/SimCruiseLowBeamPattern.h"

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
	// A fresh copy of each: the old files go first (re-saving over a package the engine has
	// only partly loaded is refused).
	for (const TCHAR* Dir : { TEXT("World/Materials"), TEXT("Vehicles/Materials"), TEXT("Vehicle/Materials") })
	{
		TArray<FString> Old;
		const FString Folder = FPaths::Combine(FPaths::ProjectContentDir(), Dir);
		IFileManager::Get().FindFiles(Old, *FPaths::Combine(Folder, TEXT("*.uasset")), true, false);
		for (const FString& File : Old) IFileManager::Get().Delete(*FPaths::Combine(Folder, File));
	}
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

	// The car's: its three masters, the dashboard clock, the mirror glass and the low-beam
	// light function (each built from the code, then saved as a copy).
	FString Report;
	if (!SimCruiseCarMaterials::SaveMasters(Report)) ++Failures;
	UE_LOG(LogSimCruiseMaterials, Display, TEXT("%s"), *Report);
	auto SaveCopy = [&](UMaterialInterface* Built, const FString& PackagePath)
	{
		UMaterial* Source = Built ? Built->GetMaterial() : nullptr;
		if (!Source)
		{
			UE_LOG(LogSimCruiseMaterials, Error, TEXT("Could not build %s"), *PackagePath);
			++Failures;
			return;
		}
		const FString Name = FPackageName::GetLongPackageAssetName(PackagePath);
		UPackage* Package = CreatePackage(*PackagePath);
		UMaterial* Saved = DuplicateObject<UMaterial>(Source, Package, *Name);
		SimCruiseCarMaterials::PrepareSavedCopy(Saved);
		Saved->PostEditChange();
		Package->MarkPackageDirty();
		const FString File = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, Saved, *File, Args))
		{
			UE_LOG(LogSimCruiseMaterials, Display, TEXT("Saved %s"), *File);
		}
		else
		{
			UE_LOG(LogSimCruiseMaterials, Error, TEXT("Could not save %s"), *File);
			++Failures;
		}
	};
	SaveCopy(SimCruiseCarClock::Get(), TEXT("/Game/Vehicle/Materials/M_SimCruiseCarClock"));
	SaveCopy(SimCruiseCarMirror::Get(), TEXT("/Game/Vehicle/Materials/M_SimCruiseCarMirror"));
	SaveCopy(SimCruiseLowBeamPattern::Get(), TEXT("/Game/Vehicle/Materials/LF_SimCruiseLowBeam"));
	// The land to the horizon (World/SimCruiseRegionalTerrain).
	SaveCopy(ASimCruiseRegionalTerrain::BuildMaterial(GetTransientPackage(), TEXT("M_SimCruiseLandscapeBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseRegionalTerrain::MaterialPath()));
	// Trees anywhere (World/SimCruiseTrees).
	SaveCopy(ASimCruiseTrees::BuildLeafMaterial(GetTransientPackage(), TEXT("M_SimCruiseTreeLeafBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseTrees::LeafMaterialPath()));
	SaveCopy(ASimCruiseTrees::BuildBarkMaterial(GetTransientPackage(), TEXT("M_SimCruiseTreeBarkBuild"), RF_Transient),
		FPackageName::ObjectPathToPackageName(ASimCruiseTrees::BarkMaterialPath()));
	return Failures == 0 ? 0 : 1;
#else
	return 1;
#endif
}
