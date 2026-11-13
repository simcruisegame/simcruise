// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseEnvironment.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Environment/SimCruiseDaylightModel.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseEnvironment, Log, All);

namespace
{
	/** Visibility below which the remaining extinction is drawn as a ground fog layer rather than haze. */
	constexpr double HazeFloorVisibilityKm = 8.0;
	/** Scale height of mist and fog near the ground, metres. */
	constexpr double GroundFogScaleHeightM = 150.0;
	/** Angular diameter of the sun and moon, degrees. */
	constexpr float SunAngularDiameterDeg = 0.533f;
	constexpr float MoonAngularDiameterDeg = 0.518f;
	/** Angstrom exponent of the aerosol: marine about 0.5, continental 1.3. */
	constexpr float AerosolAngstrom = 0.7f;

	FLinearColor AerosolSpectralShape()
	{
		// Relative extinction at the red, green and blue primaries (about 610, 550, 465 nm).
		return FLinearColor(FMath::Pow(550.0f / 610.0f, AerosolAngstrom), 1.0f, FMath::Pow(550.0f / 465.0f, AerosolAngstrom));
	}

	FRotator LightRotationToward(const FVector& DirectionToBody)
	{
		// A directional light shines along its forward (+X) axis, away from the body.
		return FRotationMatrix::MakeFromX(-DirectionToBody).Rotator();
	}
}

ASimCruiseEnvironment::ASimCruiseEnvironment()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetActorEnableCollision(false);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	RootComponent = Root;

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(Root);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetAtmosphereSunLightIndex(0);
	Sun->SetLightSourceAngle(SunAngularDiameterDeg);
	Sun->bUseTemperature = false;
	Sun->SetLightColor(FLinearColor::White);
	Sun->SetCastShadows(true);
	Sun->bCastCloudShadows = true;
	Sun->CloudShadowStrength = 1.0f;
	Sun->CloudShadowOnSurfaceStrength = 1.0f;
	// 20 km: the haze hides cloud shadows beyond it from the street, and the High preset's 192-texel map
	// then has 100 m texels (DefaultScalability.ini).
	Sun->CloudShadowExtent = 20.0f;
	Sun->CloudShadowMapResolutionScale = 1.0f;
	Sun->bCastShadowsOnClouds = true;
	Sun->bCastShadowsOnAtmosphere = true;
	Sun->bPerPixelAtmosphereTransmittance = true;
	Sun->SetIntensity(static_cast<float>(FSimCruiseDaylightModel::ExtraterrestrialSunLux()));

	MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	MoonLight->SetupAttachment(Root);
	MoonLight->SetMobility(EComponentMobility::Movable);
	MoonLight->SetAtmosphereSunLight(true);
	MoonLight->SetAtmosphereSunLightIndex(1);
	MoonLight->SetLightSourceAngle(MoonAngularDiameterDeg);
	// Moonlight is physically a little redder than sunlight, but at night the eye shifts toward blue
	// (Purkinje); the atmosphere still reddens it near the horizon. Slightly cool here, as seen.
	MoonLight->SetLightColor(FLinearColor(0.86f, 0.93f, 1.0f));
	MoonLight->SetCastShadows(false);
	MoonLight->bCastCloudShadows = true;
	MoonLight->SetIntensity(0.0f);
	MoonLight->ForwardShadingPriority = 0;

	Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
	Atmosphere->SetupAttachment(Root);
	Atmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetTopAtComponentTransform;

	Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
	Clouds->SetupAttachment(Root);

	HeightFog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("HeightFog"));
	HeightFog->SetupAttachment(Root);
	HeightFog->SetFogDensity(0.0f);
	HeightFog->SetFogInscatteringColor(FLinearColor::Black);
	HeightFog->SetDirectionalInscatteringColor(FLinearColor::Black);
	HeightFog->SetVolumetricFog(true);
	HeightFog->SetVolumetricFogScatteringDistribution(0.6f);
	HeightFog->SetVolumetricFogAlbedo(FColor::White);
	HeightFog->SetVolumetricFogDistance(3000.0f * 100.0f);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(Root);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = SLS_CapturedScene;
	// Below the horizon the capture sees the atmosphere's lit planet surface (GroundAlbedo).
	SkyLight->bLowerHemisphereIsBlack = false;
	SkyLight->CubemapResolution = 128;
	SkyLight->bCloudAmbientOcclusion = true;
	SkyLight->CloudAmbientOcclusionExtent = 50.0f;
	SkyLight->SetIntensity(1.0f);

	Camera = CreateDefaultSubobject<UPostProcessComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->bUnbound = true;
	Camera->Priority = -1.0f; // a level or camera volume can still override the look

	UtcTime = FDateTime::UtcNow();
}

void ASimCruiseEnvironment::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	UpdateSky();
}

void ASimCruiseEnvironment::BeginPlay()
{
	Super::BeginPlay();
	if (Clouds && !CloudMaterial)
	{
		if (UMaterialInterface* Base = Clouds->GetMaterial())
		{
			CloudMaterial = UMaterialInstanceDynamic::Create(Base, this);
			Clouds->SetMaterial(CloudMaterial);
		}
	}
	if (bFollowSystemClock)
	{
		UtcTime = FDateTime::UtcNow();
	}
	ApplyAtmosphere();
	ApplyClouds();
	UpdateSky();
}

void ASimCruiseEnvironment::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bFollowSystemClock)
	{
		UtcTime = FDateTime::UtcNow();
	}
	else if (TimeScale != 0.0f)
	{
		SimulatedSeconds += static_cast<double>(DeltaSeconds) * TimeScale;
		if (FMath::Abs(SimulatedSeconds) >= 0.25)
		{
			UtcTime += FTimespan::FromSeconds(SimulatedSeconds);
			SimulatedSeconds = 0.0;
		}
	}

	RoadWetness = FSimCruiseWeather::StepRoadWetness(RoadWetness, Weather.bRaining, DeltaSeconds * FMath::Max(FMath::Abs(TimeScale), 1.0f));

	// Sky time moves slowly; a fast clock (TimeScale) updates every frame so the sun glides.
	SecondsSinceSkyUpdate += DeltaSeconds * FMath::Max(FMath::Abs(TimeScale), 1.0f);
	if (SecondsSinceSkyUpdate >= SkyUpdateSeconds || FMath::Abs(TimeScale) > 30.0f)
	{
		SecondsSinceSkyUpdate = 0.0f;
		UpdateSky();
	}
}

void ASimCruiseEnvironment::SetPlace(double InLatitudeDeg, double InLongitudeDeg, double InElevationM)
{
	LatitudeDeg = FMath::Clamp(InLatitudeDeg, -90.0, 90.0);
	LongitudeDeg = FMath::Wrap(InLongitudeDeg, -180.0, 180.0);
	ElevationM = InElevationM;
	if (bDaylightStart)
	{
		UtcTime = MorningUtc(LongitudeDeg, FDateTime::UtcNow());
		SimulatedSeconds = 0.0;
	}
	ApplyAtmosphere();
	UpdateSky();
}

FDateTime ASimCruiseEnvironment::MorningUtc(double LongitudeDeg, const FDateTime& NowUtc, double LocalSolarHour)
{
	const FTimespan Offset = FTimespan::FromHours(LongitudeDeg / 15.0);
	const FDateTime LocalDate = (NowUtc + Offset).GetDate();
	return LocalDate + FTimespan::FromHours(LocalSolarHour) - Offset;
}

void ASimCruiseEnvironment::StartInDaylight()
{
	bFollowSystemClock = false;
	bDaylightStart = true;
	UtcTime = MorningUtc(LongitudeDeg, FDateTime::UtcNow());
	SimulatedSeconds = 0.0;
	UpdateSky();
}

void ASimCruiseEnvironment::SetUtcTime(const FDateTime& Utc)
{
	bDaylightStart = false;
	bFollowSystemClock = false;
	UtcTime = Utc;
	SimulatedSeconds = 0.0;
	UpdateSky();
}

void ASimCruiseEnvironment::FollowSystemClock()
{
	bFollowSystemClock = true;
	bDaylightStart = false;
	UtcTime = FDateTime::UtcNow();
	UpdateSky();
}

void ASimCruiseEnvironment::SetWeather(const FSimCruiseWeatherConditions& Conditions)
{
	Weather = Conditions;
	Weather.CloudCover = FMath::Clamp(Weather.CloudCover, 0.0f, 1.0f);
	Weather.VisibilityM = FMath::Max(Weather.VisibilityM, 50.0f);
	ApplyAtmosphere();
	ApplyClouds();
	UpdateSky();
}

void ASimCruiseEnvironment::ApplyAtmosphere()
{
	if (!Atmosphere)
	{
		return;
	}
	// The world origin stands ElevationM above sea level: put the planet's surface that far below it.
	Atmosphere->SetRelativeLocation(FVector(0.0, 0.0, -ElevationM * 100.0));
	Atmosphere->SetGroundAlbedo(GroundAlbedo.ToFColor(true));

	// Haze from visibility: clean-air Rayleigh is the atmosphere's own; the rest is aerosol.
	const double VisibilityKm = Weather.VisibilityM / 1000.0;
	const double HazeVisibilityKm = FMath::Max(VisibilityKm, HazeFloorVisibilityKm);
	double Aerosol = FSimCruiseDaylightModel::AerosolExtinctionPerKm(HazeVisibilityKm);
	if (Weather.AerosolOpticalDepth > 0.0f && VisibilityKm >= 10.0)
	{
		// A measured column (CAMS aerosol optical depth) beats a visibility guess in clear air:
		// for an exponential profile the surface extinction is the column over its scale height.
		Aerosol = FSimCruiseDaylightModel::AerosolForOpticalDepth(Weather.AerosolOpticalDepth, AerosolScaleHeightKm);
	}
	const float Albedo = FMath::Clamp(AerosolSingleScatteringAlbedo, 0.5f, 1.0f);
	Atmosphere->SetMieScattering(AerosolSpectralShape());
	Atmosphere->SetMieAbsorption(AerosolSpectralShape());
	Atmosphere->SetMieScatteringScale(static_cast<float>(Aerosol * Albedo));
	Atmosphere->SetMieAbsorptionScale(static_cast<float>(Aerosol * (1.0f - Albedo)));
	Atmosphere->SetMieExponentialDistribution(FMath::Max(AerosolScaleHeightKm, 0.1f));
	// Continental and marine aerosols scatter strongly forward (asymmetry g about 0.7-0.8).
	Atmosphere->SetMieAnisotropy(0.76f);

	ApplyFog();
}

void ASimCruiseEnvironment::SetSkyGlowLuminance(double Nits)
{
	Nits = FMath::Max(Nits, 0.0);
	if (FMath::Abs(Nits - SkyGlowNits) <= 0.05 * FMath::Max(Nits, SkyGlowNits) + 1.0e-5)
	{
		return;
	}
	SkyGlowNits = Nits;
	ApplyFog();
}

void ASimCruiseEnvironment::ApplyFog()
{
	if (!HeightFog)
	{
		return;
	}
	// Below the haze floor the rest of the Koschmieder extinction is mist/fog near the ground.
	const double VisibilityKm = Weather.VisibilityM / 1000.0;
	const double HazeVisibilityKm = FMath::Max(VisibilityKm, HazeFloorVisibilityKm);
	const double Total = FSimCruiseDaylightModel::ExtinctionPerKmForVisibility(VisibilityKm);
	const double Haze = FSimCruiseDaylightModel::ExtinctionPerKmForVisibility(HazeVisibilityKm);
	const double FogPerKm = FMath::Max(Total - Haze, 0.0);

	// A city's sky glow: light from the streets scattered back by the low air, brightest toward
	// the horizon. Drawn as a fog layer whose in-scattering is the glow's luminance at the
	// horizon; its optical depth straight up (0.1/km over a 1.6 km scale height, 0.16) sets the
	// zenith to about 15 % of that: 9 mcd/m2 under a 0.06 cd/m2 horizon, a sky-quality-meter
	// reading near 17.9 mag/arcsec2, as measured over Miami. Nearby it barely shows (3 % at
	// 300 m). It shows once the sky is darker than it (after civil twilight).
	const double Night = FMath::SmoothStep(0.0, 1.0, FMath::Clamp((-4.0 - SunPosition.ApparentElevationDeg) / 8.0, 0.0, 1.0));
	const double Glow = SkyGlowNits * Night;
	const double GlowPerKm = Glow > 1.0e-5 ? 0.1 : 0.0;

	// Unreal stores fog density x1000 per unreal unit (cm): density = sigma[1/km] / 100.
	HeightFog->SetFogDensity(static_cast<float>((FogPerKm + GlowPerKm) / 100.0));
	HeightFog->SetFogHeightFalloff(static_cast<float>(10.0 / (FogPerKm > GlowPerKm ? GroundFogScaleHeightM : 1600.0)));
	// Warm: high-pressure sodium and 3000 K LEDs.
	HeightFog->SetFogInscatteringColor(FLinearColor(1.0f, 0.72f, 0.45f) * static_cast<float>(Glow));
	// Volumetric fog (lit by the street lights and the moon) only for real mist: a glow layer alone costs nothing.
	HeightFog->SetVolumetricFog(FogPerKm > 0.0);
	HeightFog->SetVolumetricFogExtinctionScale(1.0f);
}

void ASimCruiseEnvironment::ApplyClouds()
{
	if (!Clouds)
	{
		return;
	}
	// Fair-weather cumulus over Florida bases at about 0.8-1.5 km; tops reach several km.
	Clouds->SetLayerBottomAltitude(1.2f);
	Clouds->SetLayerHeight(6.0f);
	if (CloudMaterial)
	{
		CloudMaterial->SetScalarParameterValue(TEXT("Cloud_GlobalCoverage"), FSimCruiseWeather::CloudMaterialCoverage(Weather.CloudCover));
	}
}

void ASimCruiseEnvironment::SetLookParameter(FName Name, float Value)
{
	if (Name == TEXT("AerosolScaleHeightKm") || Name == TEXT("AerosolAlbedo") || Name == TEXT("Saturation") || Name == TEXT("ExposureCompensation")
		|| Name == TEXT("OpenStops") || Name == TEXT("CloseStops") || Name == TEXT("MultiScattering") || Name == TEXT("MieAnisotropy"))
	{
		if (Name == TEXT("AerosolScaleHeightKm")) { AerosolScaleHeightKm = Value; }
		if (Name == TEXT("AerosolAlbedo")) { AerosolSingleScatteringAlbedo = Value; }
		if (Name == TEXT("ExposureCompensation")) { ExposureCompensation = Value; }
		if (Name == TEXT("OpenStops")) { ExposureOpenStops = Value; }
		if (Name == TEXT("CloseStops")) { ExposureCloseStops = Value; }
		ApplyAtmosphere();
		ApplyExposure();
		if (Name == TEXT("MultiScattering") && Atmosphere) { Atmosphere->SetMultiScatteringFactor(Value); }
		if (Name == TEXT("MieAnisotropy") && Atmosphere) { Atmosphere->SetMieAnisotropy(Value); }
		if (Name == TEXT("Saturation"))
		{
			CameraSaturation = Value;
			ApplyExposure();
		}
	}
	else if (Clouds && Name == TEXT("LayerBottomKm"))
	{
		Clouds->SetLayerBottomAltitude(Value);
	}
	else if (Clouds && Name == TEXT("LayerHeightKm"))
	{
		Clouds->SetLayerHeight(Value);
	}
	else if (CloudMaterial)
	{
		CloudMaterial->SetScalarParameterValue(Name, Value);
	}
}

void ASimCruiseEnvironment::UpdateSky()
{
	SunPosition = FSimCruiseSolarPosition::Sun(LatitudeDeg, LongitudeDeg, UtcTime);
	MoonPosition = FSimCruiseSolarPosition::Moon(LatitudeDeg, LongitudeDeg, UtcTime);
	SunDirection = FSimCruiseSolarPosition::ToWorldDirection(SunPosition.AzimuthDeg, SunPosition.ApparentElevationDeg, NorthYawDeg);
	MoonDirection = FSimCruiseSolarPosition::ToWorldDirection(MoonPosition.AzimuthDeg, MoonPosition.ApparentElevationDeg, NorthYawDeg);

	const double MoonLux = FSimCruiseDaylightModel::MoonIlluminanceLux(MoonPosition.PhaseAngleDeg, MoonPosition.Distance);
	const bool bSunUp = SunPosition.ApparentElevationDeg > -1.0;
	const bool bMoonUp = MoonPosition.ApparentElevationDeg > -1.0;

	if (Sun)
	{
		Sun->SetWorldRotation(LightRotationToward(SunDirection));
		Sun->SetIntensity(static_cast<float>(FSimCruiseDaylightModel::ExtraterrestrialSunLux(SunPosition.Distance)));
		// Below the horizon the sun still lights the upper atmosphere (twilight), but casts no shadows.
		Sun->SetCastShadows(bSunUp);
		Sun->SetVisibility(SunPosition.ApparentElevationDeg > -18.0);
	}
	if (MoonLight)
	{
		MoonLight->SetWorldRotation(LightRotationToward(MoonDirection));
		MoonLight->SetIntensity(static_cast<float>(MoonLux));
		// Only one directional light pays for virtual shadow maps: the moon once the sun has set.
		MoonLight->SetCastShadows(!bSunUp && bMoonUp);
		MoonLight->SetVisibility(bMoonUp && SunPosition.ApparentElevationDeg < 10.0);
	}

	ApplyFog();

	NaturalGroundLuxFromMoon = bMoonUp ? MoonLux * FMath::Max(FMath::Sin(FMath::DegreesToRadians(MoonPosition.ApparentElevationDeg)), 0.0) : 0.0;
	// The daylight model adds this term to the sky's illuminance: moonlight and any artificial light.
	ReferenceEV100 = static_cast<float>(FSimCruiseDaylightModel::ReferenceEV100(SunPosition.ApparentElevationDeg, Weather.CloudCover,
		NaturalGroundLuxFromMoon + ArtificialLux));
	ApplyExposure();
}

void ASimCruiseEnvironment::SetArtificialIlluminanceLux(double Lux)
{
	Lux = FMath::Max(Lux, 0.0);
	// Only a real change re-meters (lamps switching), not float noise.
	if (FMath::Abs(Lux - ArtificialLux) <= 0.05 * FMath::Max(Lux, ArtificialLux) + 1.0e-3)
	{
		return;
	}
	ArtificialLux = Lux;
	ReferenceEV100 = static_cast<float>(FSimCruiseDaylightModel::ReferenceEV100(SunPosition.ApparentElevationDeg, Weather.CloudCover,
		NaturalGroundLuxFromMoon + ArtificialLux));
	ApplyExposure();
}

void ASimCruiseEnvironment::ApplyExposure()
{
	if (!Camera)
	{
		return;
	}
	FPostProcessSettings& S = Camera->Settings;

	// A photographer's camera: histogram metering, but only within a band around what a
	// meter reads off a grey card under this sky, so tunnels open up a little and the
	// sky never drags the street into darkness.
	S.bOverride_AutoExposureMethod = true;
	S.AutoExposureMethod = AEM_Histogram;
	S.bOverride_AutoExposureMinBrightness = true;
	const float Target = static_cast<float>(FSimCruiseDaylightModel::DisplayEV100(ReferenceEV100));
	S.AutoExposureMinBrightness = Target - ExposureOpenStops;
	S.bOverride_AutoExposureMaxBrightness = true;
	S.AutoExposureMaxBrightness = Target + ExposureCloseStops;
	S.bOverride_AutoExposureBias = true;
	const float Day = FMath::SmoothStep(8.f, 11.f, static_cast<float>(ReferenceEV100));
	S.AutoExposureBias = ExposureCompensation + DaylightCompensation * Day;
	S.bOverride_AutoExposureSpeedUp = true;
	S.AutoExposureSpeedUp = 2.0f;
	S.bOverride_AutoExposureSpeedDown = true;
	S.AutoExposureSpeedDown = 1.0f;
	S.bOverride_AutoExposureLowPercent = true;
	S.AutoExposureLowPercent = 10.0f;
	S.bOverride_AutoExposureHighPercent = true;
	S.AutoExposureHighPercent = 90.0f;
	S.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	S.AutoExposureApplyPhysicalCameraExposure = false;

	// Lens: no game-style flares or heavy vignette; a modern camera corrects most falloff.
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = 0.0f;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = 0.15f;
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = 0.25f;
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = 0.0f;

	// Mesopic and scotopic vision: colour fades as the light goes (rods take over below
	// about 3 cd/m2). Full colour down to reference EV 6 (deep civil twilight), a third of it
	// by EV -2 (moonlight).
	const float Night = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp((6.0f - ReferenceEV100) / 8.0f, 0.0f, 1.0f));
	S.bOverride_ColorSaturation = true;
	S.ColorSaturation = FVector4(1.0, 1.0, 1.0, CameraSaturation * FMath::Lerp(1.0f, 0.35f, Night));
}

void ASimCruiseEnvironment::TakeOverLevelLighting(const ULevel* OnlyLevel)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	TArray<AActor*> Candidates;
	if (OnlyLevel)
	{
		Candidates.Reserve(OnlyLevel->Actors.Num());
		for (AActor* Actor : OnlyLevel->Actors)
		{
			if (IsValid(Actor))
			{
				Candidates.Add(Actor);
			}
		}
	}
	else
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Candidates.Add(*It);
		}
	}
	int32 Disabled = 0;
	for (AActor* Actor : Candidates)
	{
		if (Actor == this || Actor->IsA<ASimCruiseEnvironment>())
		{
			continue;
		}
		TInlineComponentArray<USceneComponent*> Components(Actor);
		for (USceneComponent* Component : Components)
		{
			const bool bSkyPart = Component->IsA<UDirectionalLightComponent>() || Component->IsA<USkyLightComponent>()
				|| Component->IsA<USkyAtmosphereComponent>() || Component->IsA<UVolumetricCloudComponent>()
				|| Component->IsA<UExponentialHeightFogComponent>();
			if (bSkyPart && Component->IsVisible())
			{
				Component->SetVisibility(false);
				Component->SetHiddenInGame(true);
				++Disabled;
			}
		}
	}
	if (Disabled > 0)
	{
		UE_LOG(LogSimCruiseEnvironment, Display, TEXT("SimCruise environment took over the level's lighting (%d components turned off%s)."), Disabled,
			OnlyLevel ? *FString::Printf(TEXT(" in level %s"), *OnlyLevel->GetOuter()->GetName()) : TEXT(""));
	}
}

FString ASimCruiseEnvironment::Describe() const
{
	return FString::Printf(
		TEXT("place %.5f,%.5f elev %.0f m | UTC %s | sun az %.2f el %.2f | moon az %.1f el %.1f lit %.0f%% | EV100 ref %.2f (band %.2f..%.2f) | cloud %.2f vis %.1f km rain %d wet %.2f"),
		LatitudeDeg, LongitudeDeg, ElevationM, *UtcTime.ToIso8601(),
		SunPosition.AzimuthDeg, SunPosition.ApparentElevationDeg,
		MoonPosition.AzimuthDeg, MoonPosition.ApparentElevationDeg, MoonPosition.IlluminatedFraction * 100.0,
		ReferenceEV100, FSimCruiseDaylightModel::DisplayEV100(ReferenceEV100) - ExposureOpenStops, FSimCruiseDaylightModel::DisplayEV100(ReferenceEV100) + ExposureCloseStops,
		Weather.CloudCover, Weather.VisibilityM / 1000.0f, Weather.bRaining ? 1 : 0, RoadWetness);
}
