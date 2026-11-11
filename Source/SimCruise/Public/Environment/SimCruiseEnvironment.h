// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Environment/SimCruiseSolarPosition.h"
#include "Environment/SimCruiseWeather.h"
#include "GameFramework/Actor.h"
#include "SimCruiseEnvironment.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UVolumetricCloudComponent;

/**
 * The one sky of a SimCruise world, built entirely in C++ at runtime:
 * a sun and a moon placed from the real latitude, longitude and UTC time, a
 * physically based atmosphere, volumetric clouds, a height fog for mist, a sky light
 * that captures the rendered sky every frame (time-sliced), and the camera's
 * physical auto-exposure band.
 *
 * Light is in physical units: the sun is the extraterrestrial 127.8 klx scaled by the
 * Earth-Sun distance and attenuated by SkyAtmosphere; the camera exposes in EV100.
 * World frame: +X east, +Y south, +Z up (see FSimCruiseSolarPosition).
 */
UCLASS(BlueprintType, Blueprintable)
class SIMCRUISE_API ASimCruiseEnvironment : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseEnvironment();

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Moves the observer. ElevationM is the height above sea level of the world origin. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Environment")
	void SetPlace(double LatitudeDeg, double LongitudeDeg, double ElevationM = 0.0);

	/** Sets the clock (UTC) and stops following the system clock. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Environment")
	void SetUtcTime(const FDateTime& Utc);

	/** Follows the computer's clock (the default for ordinary play). */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Environment")
	void FollowSystemClock();

	/** Applies observed or chosen weather: cloud cover, haze and fog, precipitation. */
	UFUNCTION(BlueprintCallable, Category = "SimCruise|Environment")
	void SetWeather(const FSimCruiseWeatherConditions& Conditions);

	UFUNCTION(BlueprintPure, Category = "SimCruise|Environment")
	FDateTime GetUtcTime() const { return UtcTime; }

	UFUNCTION(BlueprintPure, Category = "SimCruise|Environment")
	const FSimCruiseWeatherConditions& GetWeather() const { return Weather; }

	/** Road surface water 0 (dry) to 1 (soaked), for road materials and tyre grip. */
	UFUNCTION(BlueprintPure, Category = "SimCruise|Environment")
	float GetRoadWetness() const { return RoadWetness; }

	/** Unit vector toward the sun in the world frame. */
	UFUNCTION(BlueprintPure, Category = "SimCruise|Environment")
	FVector GetSunDirection() const { return SunDirection; }

	/** The camera's reference exposure: EV100 of an 18 % grey horizontal card under this sky. */
	UFUNCTION(BlueprintPure, Category = "SimCruise|Environment")
	float GetReferenceEV100() const { return ReferenceEV100; }

	const FSimCruiseSkyPosition& GetSunPosition() const { return SunPosition; }
	const FSimCruiseSkyPosition& GetMoonPosition() const { return MoonPosition; }

	/** One line with place, time, sun, moon, exposure and weather for logs and captures. */
	FString Describe() const;

	/** Recomputes sun, moon, sky and exposure now. */
	void UpdateSky();

	/** Look development: a cloud material scalar or one of the named atmosphere/exposure knobs (see SimCruise.Env.Look). */
	void SetLookParameter(FName Name, float Value);

	/**
	 * Turns off any other sun, sky, cloud, fog or sky light in the world, or only in OnlyLevel
	 * (a streamed level or World Partition cell that has just been added).
	 */
	void TakeOverLevelLighting(const ULevel* OnlyLevel = nullptr);

	// Place and time

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Place")
	double LatitudeDeg = 25.78;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Place")
	double LongitudeDeg = -80.13;

	/** Height of the world origin above sea level; lowers the atmosphere's ground to match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Place")
	double ElevationM = 0.0;

	/** Degrees the map's north is turned from -Y toward +X (0 for the Web Mercator map). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Place")
	double NorthYawDeg = 0.0;

	/** Follow the system clock; otherwise UtcTime advances by TimeScale per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	bool bFollowSystemClock = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	float TimeScale = 1.0f;

	/** Seconds of sky time between full sky updates (the sky light re-captures continuously). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Time")
	float SkyUpdateSeconds = 2.0f;

	// Camera

	/** EV the auto exposure may open up below the reference (dark streets, under trees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float ExposureOpenStops = 3.0f;

	/**
	 * EV above the reference the camera may close down to. Negative: real cameras on sunny
	 * Ocean Drive exposed 1.1-1.6 EV below a grey-card reading (Commons EXIF), never above it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float ExposureCloseStops = -0.5f;

	/** Exposure compensation, calibrated in the lighting lab against Ocean Drive photographs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float ExposureCompensation = 0.7f;

	/** Colour saturation of the camera rendering in daylight (1 = neutral); night desaturation multiplies it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float CameraSaturation = 1.0f;

	/** Ground albedo the atmosphere's planet surface uses for bounce and below the horizon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	FLinearColor GroundAlbedo = FLinearColor(0.12f, 0.12f, 0.11f);

	/** Aerosol scale height, km (marine boundary layer about 1-1.5 km). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	float AerosolScaleHeightKm = 1.2f;

	/** Fraction of aerosol extinction that is scattering (maritime about 0.97, urban 0.85). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere")
	float AerosolSingleScatteringAlbedo = 0.93f;

	// Components

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDirectionalLightComponent> MoonLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UVolumetricCloudComponent> Clouds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UExponentialHeightFogComponent> HeightFog;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPostProcessComponent> Camera;

protected:
	virtual void BeginPlay() override;

private:
	void ApplyAtmosphere();
	void ApplyClouds();
	void ApplyExposure();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMaterial;

	FDateTime UtcTime;
	FSimCruiseWeatherConditions Weather;
	FSimCruiseSkyPosition SunPosition;
	FSimCruiseSkyPosition MoonPosition;
	FVector SunDirection = FVector::UpVector;
	FVector MoonDirection = FVector::UpVector;
	float ReferenceEV100 = 15.0f;
	float RoadWetness = 0.0f;
	float SecondsSinceSkyUpdate = 0.0f;
	double SimulatedSeconds = 0.0;
};
