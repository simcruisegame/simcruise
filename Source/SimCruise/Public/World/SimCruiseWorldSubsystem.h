// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SimCruiseWorldSubsystem.generated.h"

class ASimCruiseWorldStreamer;

/**
 * Entry point of real-world streaming for a game world.
 *
 * Gameplay code calls StartDrive(lon, lat, title). From the command line:
 *   -SimCruiseWorld              start at the bundled Miami Beach place (offline)
 *   -SimCruiseStart=lat,lon      start somewhere else (needs the helper / network)
 *   -SimCruisePreview=<shots>    also spawn the preview rig (sun, sky, camera) and
 *                                capture the listed shots (see ASimCruiseWorldPreview)
 */
UCLASS()
class SIMCRUISE_API USimCruiseWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** Start (or restart) a drive at a place; spawns the streamer on first use. */
	ASimCruiseWorldStreamer* StartDrive(double LonDeg, double LatDeg, const FString& Title);

	ASimCruiseWorldStreamer* GetStreamer() const { return Streamer.Get(); }

	/** The bundled offline place (game/StartPool.gd FALLBACK). */
	static constexpr double BundledLon = -80.13025;
	static constexpr double BundledLat = 25.78000;

private:
	UPROPERTY(Transient)
	TObjectPtr<ASimCruiseWorldStreamer> Streamer;
};
