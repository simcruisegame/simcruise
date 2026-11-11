// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SimCruiseBuildWorldMaterialsCommandlet.generated.h"

/**
 * Saves the code-defined world materials as assets for cooked builds:
 *   UnrealEditor-Cmd.exe SimCruise.uproject -run=SimCruiseBuildWorldMaterials
 * writes Content/World/Materials/M_SimCruise{Ground,Asphalt,Concrete,Paint,Water}.uasset.
 * The definitions live in SimCruiseWorldResources.cpp; the assets are outputs.
 */
UCLASS()
class SIMCRUISE_API USimCruiseBuildWorldMaterialsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	USimCruiseBuildWorldMaterialsCommandlet();
	virtual int32 Main(const FString& Params) override;
};
