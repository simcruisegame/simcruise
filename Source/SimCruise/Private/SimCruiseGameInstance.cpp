// Copyright SimCruise contributors. All Rights Reserved.

#include "SimCruiseGameInstance.h"

#include "Vehicle/CarDefinitionLoader.h"

void USimCruiseGameInstance::Init()
{
	Super::Init();
	CarDefinitions.Reset();

	const TArray<FString> CarIds = FSimCruiseCarDefinitionLoader::FindAvailableIds();
	for (const FString& CarId : CarIds)
	{
		FSimCruiseCarDefinition Car;
		FString Error;
		if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Car, Error))
		{
			UE_LOG(LogTemp, Error, TEXT("SimCruise rejected car definition: %s"), *Error);
			continue;
		}

		UE_LOG(LogTemp, Display, TEXT("Loaded %s (%s): %.1f kg, %.3f m wheelbase, %d gears; estimates: %s"),
			*Car.Name, *Car.Id, Car.MassKg, Car.WheelbaseM, Car.ForwardGearRatios.Num(), *Car.SourcesEstimates);
		CarDefinitions.Add(Car.Id, MoveTemp(Car));
	}

	if (CarDefinitions.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("SimCruise found no valid car definitions under data/cars."));
	}
}

TArray<FString> USimCruiseGameInstance::GetAvailableCarIds() const
{
	TArray<FString> Ids;
	CarDefinitions.GetKeys(Ids);
	Ids.Sort();
	return Ids;
}

bool USimCruiseGameInstance::HasCarDefinition(const FString& CarId) const
{
	return CarDefinitions.Contains(CarId);
}

const FSimCruiseCarDefinition* USimCruiseGameInstance::FindCarDefinition(const FString& CarId) const
{
	return CarDefinitions.Find(CarId);
}

bool USimCruiseGameInstance::ReloadCarDefinition(const FString& CarId, FString& OutError)
{
	FSimCruiseCarDefinition Car;
	if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Car, OutError))
	{
		UE_LOG(LogTemp, Warning, TEXT("SimCruise kept the last good %s: %s"), *CarId, *OutError);
		return false;
	}
	CarDefinitions.Add(Car.Id, MoveTemp(Car));
	return true;
}
