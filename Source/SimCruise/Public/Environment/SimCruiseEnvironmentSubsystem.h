// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCruiseEnvironmentSubsystem.generated.h"

class ASimCruiseEnvironment;
class ASimCruiseLightingLab;
class ULevel;

/**
 * Gives every game world its SimCruise sky. When play begins it spawns one
 * ASimCruiseEnvironment (unless the level already has one), turns off any other sun,
 * sky, cloud or fog in the level, and applies the command line:
 *
 *   -SimCruiseLat=25.78 -SimCruiseLon=-80.13 -SimCruiseElevation=2
 *   -SimCruiseUtc=2025-04-24T16:20:00Z  -SimCruiseTimeScale=60
 *   -SimCruiseConditions=<file written by helper/conditions.mjs>
 *   -SimCruiseCloud=0.44 -SimCruiseVisibilityKm=16 -SimCruiseRain
 *   -SimCruiseGraphics=auto|low|medium|high|ultra
 *   -SimCruiseLab            build the lighting lab street (see ASimCruiseLightingLab)
 *   -SimCruiseNoEnvironment  leave the level's own lighting alone
 */
UCLASS()
class SIMCRUISE_API USimCruiseEnvironmentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "SimCruise|Environment")
	ASimCruiseEnvironment* GetEnvironment() const { return Environment; }

	static USimCruiseEnvironmentSubsystem* Get(const UWorld* World);

private:
	/** A streamed level or World Partition cell arrived: its own sky parts are turned off too. */
	void HandleLevelAdded(ULevel* Level, UWorld* World);

	FDelegateHandle LevelAddedHandle;

	UPROPERTY(Transient)
	TObjectPtr<ASimCruiseEnvironment> Environment;

	UPROPERTY(Transient)
	TObjectPtr<ASimCruiseLightingLab> Lab;
};
