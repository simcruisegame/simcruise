// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "Vehicle/CarDefinition.h"
#include "SimCruiseGameInstance.generated.h"

/** Loads and owns the canonical, source-annotated car roster for the game. */
UCLASS()
class SIMCRUISE_API USimCruiseGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;

	UFUNCTION(BlueprintPure, Category = "SimCruise|Garage")
	TArray<FString> GetAvailableCarIds() const;

	UFUNCTION(BlueprintPure, Category = "SimCruise|Garage")
	bool HasCarDefinition(const FString& CarId) const;

	const FSimCruiseCarDefinition* FindCarDefinition(const FString& CarId) const;

private:
	TMap<FString, FSimCruiseCarDefinition> CarDefinitions;
};
