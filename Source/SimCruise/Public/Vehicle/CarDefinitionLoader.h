// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CarDefinition.h"

/** Strict reader for the canonical game/cars/*.json files. */
class FSimCruiseCarDefinitionLoader
{
public:
	/** Load a car by its canonical JSON id from the repository's game/cars folder. */
	static bool LoadById(const FString& CarId, FSimCruiseCarDefinition& OutCar, FString& OutError);

	/** Load and validate one car JSON file, preserving its full source document. */
	static bool LoadFromFile(const FString& FilePath, FSimCruiseCarDefinition& OutCar, FString& OutError);

	/** Parse in-memory JSON with the same path-specific diagnostics and validation. */
	static bool ParseJson(const FString& JsonText, const FString& SourcePath, FSimCruiseCarDefinition& OutCar, FString& OutError);

	/** Return the sorted ids present in the canonical source directory. */
	static TArray<FString> FindAvailableIds();
};
