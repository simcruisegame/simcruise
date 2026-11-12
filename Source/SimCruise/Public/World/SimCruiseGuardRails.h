// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseGuardRails.generated.h"

class ASimCruiseWorldStreamer;
class UMaterialInterface;
class UProceduralMeshComponent;

/**
 * Guard rails where a road engineer would put them (godot-final:game/GuardRails.gd,
 * the inferred half): the AASHTO Roadside Design Guide warrants a barrier where
 * the ground beside the travelled way falls away steeply. For each road ribbon
 * every station's shoulder is compared with the ground 8 m farther out; a drop
 * of more than 3 m (steeper than a 1:3 non-recoverable slope) is a hazard.
 * Hazards shorter than 12 m are ignored, runs start 8 m before and end 8 m
 * after, and close runs merge. Each run is a galvanised W-beam (310 mm deep,
 * top 0.79 m above the shoulder) on posts every 1.905 m, 0.35 m outside the
 * paved edge, and it is solid: a car is caught, not launched.
 * -SimCruiseNoGuardRails turns them off.
 */
UCLASS(NotPlaceable, Transient)
class SIMCRUISE_API ASimCruiseGuardRails : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseGuardRails();
	virtual void Tick(float DeltaSeconds) override;

	TWeakObjectPtr<ASimCruiseWorldStreamer> Streamer;

	static UMaterial* BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags);
	static FString MaterialPath();

	/** Rails built on the tiles round the car (metres of rail). */
	double MetresOfRail() const { return RailM; }

	/** One side of one ribbon: which stations need a rail, given the drop at each (m). Returns [first, last] station pairs. */
	static TArray<FIntPoint> HazardRuns(const TArray<double>& StationS, const TArray<double>& DropM);

private:
	void BuildTile(const FIntPoint& Id);

	UPROPERTY(Transient)
	TMap<FIntPoint, TObjectPtr<UProceduralMeshComponent>> Built;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Material;

	FString SeenTitle;
	double RailM = 0.0;
};
