// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "SimCruiseCarPresentationComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
struct FSimCruiseBuiltCar;

/** One road wheel as the physics sees it this frame (vehicle frame, Unreal axes). */
USTRUCT(BlueprintType)
struct SIMCRUISE_API FSimCruiseWheelVisualState
{
	GENERATED_BODY()

	/** Road-wheel steer angle, radians, positive = turning left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	float SteerAngleRad = 0.f;

	/** Camber, radians, positive = top of the wheel leans outboard. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	float CamberRad = 0.f;

	/**
	 * Roll angle, radians, increasing when the car rolls forward. Providers
	 * should keep it wrapped to one turn (e.g. FMath::Fmod with 2 pi): a float
	 * accumulated over a long drive loses the precision a spinning wheel needs
	 * (after an hour at 100 km/h it would step by about 0.03 rad).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	float SpinAngleRad = 0.f;

	/** Hub travel from static ride, centimetres, positive = wheel up toward the body (bump). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	float SuspensionTravelCm = 0.f;
};

/** Everything the car body shows from the simulation. */
USTRUCT(BlueprintType)
struct SIMCRUISE_API FSimCruiseCarVisualState
{
	GENERATED_BODY()

	/** Front left, front right, rear left, rear right. */
	UPROPERTY(EditAnywhere, Category = "SimCruise|Presentation")
	FSimCruiseWheelVisualState Wheels[4];

	/** Service brake applied: brake lamps lit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	bool bBrake = false;

	/** Tail and head lamps on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	bool bLamps = false;
};

UINTERFACE(MinimalAPI, BlueprintType)
class USimCruiseWheelStateProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by the vehicle pawn (or its movement component): the
 * presentation polls it every frame after physics and poses the wheels,
 * calipers, steering wheel and lamps from it.
 */
class SIMCRUISE_API ISimCruiseWheelStateProvider
{
	GENERATED_BODY()

public:
	/** Fill the current visual state; return false when there is none yet. */
	virtual bool GetCarVisualState(FSimCruiseCarVisualState& OutState) const = 0;
};

/**
 * The visible car: the downloaded model fitted to the car's data (body,
 * glass, four spinning wheels on steering knuckles with their calipers, a
 * turning steering wheel, lamps), all with SimCruise's physically based
 * material roles.
 *
 * Frame: the component's origin is the physics body origin of the Godot
 * car frame converted to Unreal axes (X forward, Y right, Z up, cm); the
 * hubs sit at the car's mount height minus its static suspension length.
 * Attach it to the vehicle body with an identity relative transform.
 */
UCLASS(ClassGroup = (SimCruise), meta = (BlueprintSpawnableComponent))
class SIMCRUISE_API USimCruiseCarPresentationComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	USimCruiseCarPresentationComponent();

	/** Build (or reuse) the car's meshes and materials and create the parts. Blocking on the first use of a car. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Presentation")
	bool InitializeForCar(const FString& InCarId);

	/** Poll this provider every tick (pass nullptr to stop). */
	void SetWheelStateProvider(TScriptInterface<ISimCruiseWheelStateProvider> Provider);

	/** Pose the car once (also what the tick does with a provider's state). */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Presentation")
	void ApplyVisualState(const FSimCruiseCarVisualState& State);

	UFUNCTION(BlueprintPure, Category = "SimCruise|Presentation")
	FString GetCarId() const { return CarId; }

	UFUNCTION(BlueprintPure, Category = "SimCruise|Presentation")
	FString GetLastError() const { return LastError; }

	/** Static-ride hub centre of a wheel (0 FL, 1 FR, 2 RL, 3 RR), component space, cm. */
	FVector GetStaticHubLocation(int32 Wheel) const;

	/** Rim centre and rotation of the steering wheel at rest, component space. */
	FTransform GetSteeringWheelRestTransform() const { return SteeringRest; }
	bool HasSteeringWheel() const { return SteeringWheelMesh != nullptr; }
	double GetSteeringRatio() const { return SteeringRatio; }
	/** The steering-wheel angle shown for the current state, radians (+ = anticlockwise as the driver sees it). */
	double GetSteeringWheelAngleRad() const { return SteeringWheelAngle; }

	/** Driver eye, component space, cm (from the car data). */
	FVector GetDriverEye() const { return DriverEyeCm; }
	/** Lines describing how the model was fitted (for logs and the turntable report). */
	const TArray<FString>& GetRigLog() const { return RigLog; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Steering-wheel angle for a pair of front steer angles (mean times the overall ratio). */
	static double SteeringWheelAngleFor(double FrontLeftSteerRad, double FrontRightSteerRad, double Ratio);
	/** Knuckle (steer, camber, travel) and spinner (roll) transforms relative to the static hub. */
	static FTransform KnuckleTransform(const FVector& StaticHubCm, const FSimCruiseWheelVisualState& Wheel, bool bLeftSide);
	static FRotator SpinRotation(float SpinAngleRad);
	/** Rotation of the steering wheel about its column (local X, away from the driver). */
	static FQuat SteeringWheelTurn(double AngleRad);

private:
	void ClearParts();
	UStaticMeshComponent* MakeMeshComponent(USceneComponent* Parent, FName Name, class UStaticMesh* Mesh, const FTransform& Relative,
		const TArray<int32>& SectionMaterials);
	UMaterialInstanceDynamic* MaterialFor(int32 MaterialIndex);

	UPROPERTY(VisibleAnywhere, Category = "SimCruise|Presentation")
	FString CarId;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Parts;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> Knuckles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Spinners;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> SteeringWheelMesh;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> Materials;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RearLamps;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FrontLamps;

	UPROPERTY()
	TScriptInterface<ISimCruiseWheelStateProvider> StateProvider;

	TSharedPtr<FSimCruiseBuiltCar> Built;
	FVector StaticHubs[4];
	FTransform SteeringRest = FTransform::Identity;
	FVector DriverEyeCm = FVector::ZeroVector;
	double SteeringRatio = 15.0;
	double SteeringWheelAngle = 0.0;
	int32 LampLevel = -1;
	FString LastError;
	TArray<FString> RigLog;
};

/** A car body on its own (turntable, showroom, parked cars): a presentation component as root. */
UCLASS()
class SIMCRUISE_API ASimCruiseCarPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseCarPresentationActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SimCruise|Presentation")
	TObjectPtr<USimCruiseCarPresentationComponent> Presentation;
};
