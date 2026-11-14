// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "SimCruiseCarPresentationComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FSimCruiseBuiltCar;
struct FSimCruiseDialSpec;

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

	/** For the instruments: road speed (m/s, as the speedometer reads it) and engine speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	float SpeedMps = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	float EngineRpm = 0.f;

	/** The hand wheel's angle (rad, positive left as the wheels' steer), when the car gives it (bHasSteeringWheel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	float SteeringWheelRad = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	bool bHasSteeringWheel = false;

	/** The engine is running (the gauges rest when it is not). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimCruise|Presentation")
	bool bEngineRunning = true;
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
	/** The ratio the cockpit wheel turns at: the driver's tuned one when set (Tune car), else the real car's. */
	double GetWheelSteeringRatio() const { return TunedSteeringRatio > 1.0 ? TunedSteeringRatio : SteeringRatio; }
	void SetTunedSteeringRatio(double Ratio) { TunedSteeringRatio = Ratio; }
	/** The steering-wheel angle shown for the current state, radians (+ = anticlockwise as the driver sees it). */
	double GetSteeringWheelAngleRad() const { return SteeringWheelAngle; }
	/** The roll a road wheel is drawn with (its spinner's pitch), radians in (-pi, pi]; 0 without wheels. */
	double GetDrawnSpinRad(int32 Wheel) const;
	/** The steer a front wheel is drawn with (its knuckle's yaw), radians, positive to the left. */
	double GetDrawnSteerRad(int32 Wheel) const;

	/** Driver eye, component space, cm (from the car data). */
	FVector GetDriverEye() const { return DriverEyeCm; }

	/** Head-lamp beam origins (centres of the head-lamp lenses), component space, cm, left then right; empty when the model has none. */
	TArray<FVector> GetHeadLampLocations() const;

	/** The car's presentation profile (null before InitializeForCar succeeds). */
	const struct FSimCruiseCarPresentationProfile* GetProfile() const;

	/**
	 * Repaints the body (Paint surfaces whose colour is a plain base colour, not a texture
	 * atlas), linear colour; false when the car has no such surface.
	 */
	bool SetPaintColor(const FLinearColor& Linear);

	/**
	 * A parked car's cheaper drawing: no tick, nothing past DrawDistanceCm, no part in
	 * Lumen's scene (it neither bounces nor blocks the sky's light), and beyond
	 * GlassNearCm its glass and lenses swap to an opaque copy (dark glossy glass, lenses
	 * in their colour) instead of two translucent passes each, itself gone past
	 * GlassFarCm (a window there is a few pixels of the body's colour).
	 */
	void SetParked(float GlassNearCm = 2500.f, float GlassFarCm = 8000.f, float DrawDistanceCm = 15000.f);
	/** Lines describing how the model was fitted (for logs and the turntable report). */
	const TArray<FString>& GetRigLog() const { return RigLog; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Mirrors that show the road: a 768 x 320 capture looking back from the driver's eye
	 * (attached to Parent at RelativeEyeCm), refreshed every other frame, which the mirror
	 * glass reflects into (SimCruiseCarMirror). Off: the glass shows a plain sky-grey.
	 */
	void SetMirrorsLive(bool bLive, USceneComponent* Parent = nullptr, const FVector& RelativeEyeCm = FVector::ZeroVector);

	/** Steering-wheel angle for a pair of front steer angles (mean times the overall ratio). */
	static double SteeringWheelAngleFor(double FrontLeftSteerRad, double FrontRightSteerRad, double Ratio);
	/** Knuckle (steer, camber, travel) and spinner (roll) transforms relative to the static hub. */
	static FTransform KnuckleTransform(const FVector& StaticHubCm, const FSimCruiseWheelVisualState& Wheel, bool bLeftSide);
	static FRotator SpinRotation(float SpinAngleRad);
	/** Rotation of the steering wheel about its column (local X, away from the driver). */
	static FQuat SteeringWheelTurn(double AngleRad);

private:
	void ClearParts();
	/** The profile's add-on parts (roll bar, wing) from the engine's basic shapes. */
	void AddAddons();
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
	double TunedSteeringRatio = 0.0;
	double SteeringWheelAngle = 0.0;
	/** World time of the last rim update (-1: none yet), for the hand-rate limit. */
	double SteeringWheelTime = -1.0;
	int32 LampLevel = -1;

	/** The dashboard clock's material, when the profile has one; its minutes follow the place's local time. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ClockMaterial;
	/** The digital cluster's screen (the C8's): live speed and tachometer drawn by its material. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ScreenMaterial;
	double ScreenRpm = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MirrorMaterials;
	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> MirrorCapture;
	/** Sky fill in the cabin (driver's view only): see SetMirrorsLive. */
	UPROPERTY(Transient)
	TObjectPtr<class UPointLightComponent> CabinFill;
	void UpdateCabinFill();
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> MirrorTarget;
	uint32 MirrorFrame = 0;
	int32 ClockMinutes = -1;
	float ClockCheck = 0.f;
	FString LastError;
	TArray<FString> RigLog;

	/** The live dial faces (one render target each) and the values their needles show. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> DialTargets;
	TArray<double> DialShown;
	/** Each dial's needle mesh (turned about its dial's axis every frame) and its rest transform. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Needles;
	TArray<FTransform> NeedleRest;
	FSimCruiseCarVisualState LastState;
	void UpdateDials(float DeltaTime);
	/** The face (ticks, numerals, red zone, captions), drawn once: the needle is its own mesh. */
	void DrawDialFace(int32 Index);

public:
	/** A dial's needle angle for a value: clockwise from 12 o'clock, radians. */
	static double NeedleAngleRad(const FSimCruiseDialSpec& Dial, double Value);

public:
	/** The value a dial's needle shows now (tests and the pre-flight check), NaN when the car has no such dial. */
	double GetDialValue(const FString& Quantity) const;
	int32 GetDialCount() const { return DialTargets.Num(); }
	/** The value a dial should show for a car state (speed in its unit, rpm, or 0..1 for the small gauges). */
	static double DialTarget(const FString& Quantity, const FSimCruiseCarVisualState& State);
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
