// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseSignals.generated.h"

class ASimCruiseWorldStreamer;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;

/** One signalised junction: where it is and the two directions its phases serve. */
struct FSimCruiseSignalJunction
{
	FVector CentreCm = FVector::ZeroVector;
	/** Unit directions of the two crossing roads (world frame); phase 0 serves Axis[0] both ways. */
	FVector2D Axis[2] = { FVector2D(1, 0), FVector2D(0, 1) };
	/** Half widths of the two roads (m), for the stop lines. */
	double HalfW[2] = { 4.0, 4.0 };
	/** Seconds into the cycle at time zero (junctions are not all in step). */
	double Offset = 0.0;
};

/** What a signal shows an approach. */
enum class ESimCruiseSignalAspect : uint8 { Green, Yellow, Red };

/**
 * Traffic signals at the junctions of arterial roads (godot-final:game/SignalControls.gd
 * placed them from mapped highway=traffic_signals nodes; the helper only has those
 * with an operator's own Overpass server, so here they follow the US practice the
 * MUTCD warrants describe: where two through roads of collector class or above
 * cross, the junction is signalised). Each approach gets a pole on the far right
 * corner with a three-lamp head 5.2 m up facing the oncoming traffic, cycling
 * green 26 s, yellow 4 s (35 mph clearance), all-red 2 s, per axis.
 * IsStopFor answers whether a car heading a given way must stop, for traffic.
 * -SimCruiseNoSignals turns them off.
 */
UCLASS(NotPlaceable, Transient)
class SIMCRUISE_API ASimCruiseSignals : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseSignals();
	virtual void Tick(float DeltaSeconds) override;

	TWeakObjectPtr<ASimCruiseWorldStreamer> Streamer;

	static ASimCruiseSignals* Find(const UWorld* World);

	/** The aspect an approach heading Dir (unit, world) sees at a junction now. */
	ESimCruiseSignalAspect AspectFor(const FSimCruiseSignalJunction& J, const FVector2D& Dir) const;

	/**
	 * Whether a car at PositionCm heading Dir must stop for a signal within LookAheadM:
	 * true with the stop line (world cm) when the next signalised junction ahead shows red,
	 * or yellow while the car is still more than YellowCommitM from the line.
	 */
	bool IsStopFor(const FVector& PositionCm, const FVector2D& Dir, double LookAheadM, FVector& OutStopLineCm) const;

	const TArray<FSimCruiseSignalJunction>& GetJunctions() const { return Junctions; }

	/** Phase of a cycle: 0..1 green on axis 0, then yellow, all red, axis 1 ... (seconds). */
	static ESimCruiseSignalAspect AspectAt(double CycleSeconds, int32 Axis);
	static constexpr double GreenS = 26.0;
	static constexpr double YellowS = 4.0;
	static constexpr double AllRedS = 2.0;
	static constexpr double CycleS = 2.0 * (GreenS + YellowS + AllRedS);

	static UMaterial* BuildMaterial(UObject* Outer, FName Name, EObjectFlags Flags);
	static FString MaterialPath();

private:
	void Rebuild();
	void UpdateLamps();

	TArray<FSimCruiseSignalJunction> Junctions;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Mesh;

	/** 0 housing and poles; per axis three lamps lit or dark: 1..6 = axis 0 R,Y,G, axis 1 R,Y,G. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;

	TSet<FIntPoint> ShownTiles;
	FIntPoint ShownOrigin = FIntPoint(MAX_int32, MAX_int32);
	double Clock = 0.0;
	ESimCruiseSignalAspect Shown[2] = { ESimCruiseSignalAspect::Red, ESimCruiseSignalAspect::Red };
};
