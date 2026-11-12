// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Live/SimCruiseLiveData.h"
#include "SimCruiseLiveSky.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * The real aircraft over the place, now. Each report from the ADS-B feed places one
 * aircraft; between reports it flies on by dead reckoning, and a new report pulls it
 * back over two seconds, so it never jumps. Positions come from latitude, longitude and
 * height through the drive's own map frame, lowered by the Earth's curvature with
 * distance, so a jet on approach 20 km away sits where it sits in the real sky.
 *
 * Each aircraft is a simple body (fuselage, wings, tail) sized from its ADS-B category,
 * and its lights: red and green wing tip lights, a white tail light, a red beacon that
 * flashes once a second, white strobes that double-flash, and landing lights below
 * 3 km on approach and climb. The lights hold a minimum size of about two pixels with
 * their intensity kept (area times luminance), so a strobe 30 km away still shows as
 * the point it is in the real night sky.
 */
UCLASS()
class SIMCRUISE_API ASimCruiseLiveSky : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseLiveSky();

	virtual void Tick(float DeltaSeconds) override;

	/** A new feed read: ReceivedUtc is when it arrived (each row carries its own position age). */
	void SetAircraft(const TArray<FSimCruiseLiveAircraft>& Aircraft, const FDateTime& ReceivedUtc);

	/** The world origin moved (floating origin): keep the drawn positions continuous. */
	void ShiftOrigin(const FVector& OffsetCm);

	int32 NumAircraft() const { return Planes.Num(); }

	/**
	 * Test shot: from the player's camera toward the aircraft highest in the sky (above 3
	 * degrees of elevation), a scene capture with the given field of view; after 2 s (exposure settles)
	 * the frame is saved to Saved/Screenshots/<Name>.png. Returns a description, or empty.
	 */
	FString StartShot(const FString& Name, float FovDeg);

	/** Elevation of the world origin above sea level (m), from the environment. */
	double OriginElevationM = 0.0;

	/**
	 * Sun elevation, degrees. Lights hold their two-pixel minimum only once the sky is dark
	 * (sun below -4 degrees); by day a sub-pixel lamp is not seen, and an enlarged one would
	 * draw as a dark disc against the bright sky.
	 */
	double SunElevationDeg = -90.0;

	/** The main camera's display EV100 now, for test shots. */
	float ShotEV100 = 10.0f;

private:
	struct FPlane
	{
		FSimCruiseLiveAircraft Report;
		FDateTime ReportUtc;
		FVector SmoothingOffsetCm = FVector::ZeroVector;
		FVector LastWorldCm = FVector::ZeroVector;
		bool bPlaced = false;
		float Phase = 0.0f;
		TObjectPtr<USceneComponent> Root;
		TArray<TObjectPtr<UStaticMeshComponent>> Body;
		/** Wing tip red, wing tip green, tail white, beacon, strobe left, strobe right, landing. */
		TArray<TObjectPtr<UStaticMeshComponent>> Lights;
		TArray<TObjectPtr<UMaterialInstanceDynamic>> LightMaterials;
	};

	FPlane& MakePlane(const FSimCruiseLiveAircraft& Report);
	void DestroyPlane(FPlane& Plane);
	void UpdatePlane(FPlane& Plane, const FDateTime& NowUtc, const FVector& ViewCm, float DeltaSeconds);
	bool ResolveAssets();

	TMap<FString, FPlane> Planes;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Cube;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Cylinder;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> Sphere;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> Master;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Keeps every runtime component alive (the plane records are plain structs). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Owned;

	double ClockSeconds = 0.0;

	void TickShot(float DeltaSeconds, const FVector& ViewCm);

	UPROPERTY(Transient)
	TObjectPtr<class USceneCaptureComponent2D> ShotCapture;

	UPROPERTY(Transient)
	TObjectPtr<class UTextureRenderTarget2D> ShotTarget;

	FString ShotName;
	FString ShotHex;
	float ShotSeconds = 0.0f;
};
