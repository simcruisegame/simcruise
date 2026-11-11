// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseWorldPreview.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "UnrealClient.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"
#include "World/SimCruiseWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruisePreview, Log, All);

ASimCruiseWorldPreview::ASimCruiseWorldPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseWorldPreview::Begin(ASimCruiseWorldStreamer* InStreamer, const FString& ShotList)
{
	Streamer = InStreamer;
	ShotList.ParseIntoArray(Shots, TEXT(","));
	OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("SimCruise"));
	StartCm = Streamer ? Streamer->GetFrame().LonLatToWorldCm(USimCruiseWorldSubsystem::BundledLon, USimCruiseWorldSubsystem::BundledLat) : FVector::ZeroVector;
	double Lat = 0.0, Lon = 0.0;
	FString Start;
	if (FParse::Value(FCommandLine::Get(), TEXT("SimCruiseStart="), Start, /*bShouldStopOnSeparator=*/ false))
	{
		TArray<FString> Parts;
		Start.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 2) { Lat = FCString::Atod(*Parts[0]); Lon = FCString::Atod(*Parts[1]); StartCm = Streamer->GetFrame().LonLatToWorldCm(Lon, Lat); }
	}
	SpawnLightingIfMissing();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Camera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(FRotator(-30, 0, 0), StartCm + FVector(0, 0, 3000)), Params);
	Camera->GetCameraComponent()->SetFieldOfView(70.f);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	if (Streamer)
	{
		Streamer->OnRebased.AddWeakLambda(this, [this](const FVector& Offset)
		{
			if (Camera) Camera->AddActorWorldOffset(Offset);
			StartCm += Offset;
			TargetCm += Offset;
			if (DropBody) DropBody->AddActorWorldOffset(Offset, false, nullptr, ETeleportType::TeleportPhysics);
		});
		Streamer->SetFocus(StartCm, FVector::ZeroVector);
	}
	Stage = EStage::WaitStart;
	StageStarted = FPlatformTime::Seconds();
	UE_LOG(LogSimCruisePreview, Display, TEXT("Preview: %d shots -> %s"), Shots.Num(), *OutputDir);
}

void ASimCruiseWorldPreview::SpawnLightingIfMissing()
{
	UWorld* World = GetWorld();
	// The SimCruise environment (Environment/) gives every game world its real sun
	// and sky unless -SimCruiseNoEnvironment turns it off.
	if (!FParse::Param(FCommandLine::Get(), TEXT("SimCruiseNoEnvironment")))
	{
		return;
	}
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		return; // the map brings its own sun
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// Miami Beach, 10 November, mid-afternoon: sun at azimuth ~215 deg, elevation ~38 deg.
	// Light travels away from the sun: toward azimuth 35 deg; X east, Y south.
	const double TravelAzimuth = FMath::DegreesToRadians(35.0);
	const FVector Toward(FMath::Sin(TravelAzimuth), -FMath::Cos(TravelAzimuth), 0.0);
	const FRotator SunRotation(-38.0, FMath::RadiansToDegrees(FMath::Atan2(Toward.Y, Toward.X)), 0.0);
	ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(SunRotation), Params);
	if (UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
	{
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetIntensity(10.f);
		Light->SetAtmosphereSunLight(true);
		Light->SetDynamicShadowDistanceMovableLight(20000.f);
		Light->SetCastShadows(true);
	}
	AActor* Sky = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(Sky);
	Sky->SetRootComponent(Atmosphere);
	Atmosphere->RegisterComponent();
	UVolumetricCloudComponent* Clouds = NewObject<UVolumetricCloudComponent>(Sky);
	Clouds->SetupAttachment(Atmosphere);
	Clouds->RegisterComponent();
	ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, Params);
	if (USkyLightComponent* Light = SkyLight->GetLightComponent())
	{
		Light->SetMobility(EComponentMobility::Movable);
		Light->bRealTimeCapture = true;
		Light->SetIntensity(1.f);
		Light->RecaptureSky();
	}
	AExponentialHeightFog* Fog = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity, Params);
	if (UExponentialHeightFogComponent* F = Fog->GetComponent())
	{
		F->SetFogDensity(0.006f);
		F->SetFogHeightFalloff(0.15f);
	}
	if (IConsoleVariable* GI = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod"))) GI->Set(1, ECVF_SetByCode);
	if (IConsoleVariable* Refl = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod"))) Refl->Set(1, ECVF_SetByCode);
	UE_LOG(LogSimCruisePreview, Display, TEXT("Preview lighting: sun, sky atmosphere, clouds, sky light, height fog"));
}

bool ASimCruiseWorldPreview::PlaceShot(const FString& Shot)
{
	FVector Location;
	FRotator Rotation;
	UCameraComponent* Cam = Camera->GetCameraComponent();
	Cam->SetFieldOfView(70.f);
	if (Shot == TEXT("ocean") || Shot == TEXT("collins") || Shot == TEXT("drop"))
	{
		const FString Road = Shot == TEXT("collins") ? TEXT("Collins Avenue") : TEXT("Ocean Drive");
		if (!Streamer->FindRoadPose(Road, StartCm, 900.0, Location, Rotation))
		{
			UE_LOG(LogSimCruisePreview, Warning, TEXT("No %s near the start"), *Road);
			return false;
		}
		if (Shot == TEXT("drop"))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AStaticMeshActor* Box = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(Rotation, Location + FVector(0, 0, 150)), Params);
			UStaticMeshComponent* M = Box->GetStaticMeshComponent();
			M->SetMobility(EComponentMobility::Movable);
			M->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Box->SetActorScale3D(FVector(4.0, 1.7, 1.3)); // a car-sized block
			M->SetSimulatePhysics(true);
			M->SetMassOverrideInKg(NAME_None, 1000.f, true);
			DropBody = Box;
			TargetCm = Location;
			Camera->SetActorLocationAndRotation(Location - Rotation.Vector() * 900.0 + FVector(0, 0, 250), (Rotation.Vector() * 900.0 - FVector(0, 0, 250)).Rotation());
			return true;
		}
		// Driver's eye: 1.1 m over the lane, a little left of the lane centre.
		const FVector Left = FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y) * -35.0;
		Camera->SetActorLocationAndRotation(Location + Left + FVector(0, 0, 110), Rotation + FRotator(-2.0, 0, 0));
		TargetCm = Location;
		return true;
	}
	if (Shot == TEXT("top") || Shot == TEXT("seam"))
	{
		FVector Over = StartCm;
		double Height = 12000.0;
		if (Shot == TEXT("seam"))
		{
			// Where Ocean Drive crosses a tile edge nearest the start: two tiles built that section.
			double Best = TNumericLimits<double>::Max();
			for (int32 dy = -1; dy <= 1; ++dy)
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				const ASimCruiseWorldTile* Tile = Streamer->GetTile(Streamer->GetFrame().TileAtWorldCm(StartCm) + FIntPoint(dx, dy));
				if (!Tile || !Tile->GetBuild()) continue;
				for (const FSimCruiseRibbon& Ribbon : Tile->GetBuild()->Roads.Ribbons)
				{
					if (Ribbon.Name != TEXT("Ocean Drive")) continue;
					for (int32 End = 0; End < 2; ++End)
					{
						if (!(End == 0 ? Ribbon.bStartOnBoundary : Ribbon.bEndOnBoundary)) continue;
						const FVector2d C = (End == 0 ? Ribbon.St[0] : Ribbon.St.Last()).C;
						const FVector W = Tile->GetActorLocation() + FVector(C.X * 100.0, C.Y * 100.0, 0.0);
						const double D = FVector::Dist2D(W, StartCm);
						if (D < Best) { Best = D; Over = W; }
					}
				}
			}
			Height = 3500.0;
		}
		double Z = 0.0;
		Streamer->SurfaceHeightAt(Over, Z);
		Camera->SetActorLocationAndRotation(FVector(Over.X, Over.Y, Z + Height), FRotator(-90.0, -90.0, 0.0));
		Cam->SetFieldOfView(60.f);
		TargetCm = Over;
		return true;
	}
	if (Shot == TEXT("aerial"))
	{
		double Z = 0.0;
		Streamer->SurfaceHeightAt(StartCm, Z);
		const FVector From = StartCm + FVector(-9000.0, 9000.0, 7000.0);
		Camera->SetActorLocationAndRotation(FVector(From.X, From.Y, Z + 7000.0), (FVector(StartCm.X, StartCm.Y, Z) - FVector(From.X, From.Y, Z + 7000.0)).Rotation());
		TargetCm = StartCm;
		return true;
	}
	UE_LOG(LogSimCruisePreview, Warning, TEXT("Unknown shot %s"), *Shot);
	return false;
}

void ASimCruiseWorldPreview::Finish()
{
	Stage = EStage::Done;
	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseKeepOpen")))
	{
		return;
	}
	UE_LOG(LogSimCruisePreview, Display, TEXT("Preview finished; quitting"));
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		PC->ConsoleCommand(TEXT("quit"));
	}
	else
	{
		FPlatformMisc::RequestExit(false);
	}
}

void ASimCruiseWorldPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Streamer || !Camera || Stage == EStage::Done)
	{
		return;
	}
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (PC->GetViewTarget() != Camera)
		{
			PC->SetViewTarget(Camera);
			if (APawn* Pawn = PC->GetPawn()) Pawn->SetActorHiddenInGame(true);
		}
	}
	const double Now = FPlatformTime::Seconds();
	const bool bShadersBusy = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
	switch (Stage)
	{
	case EStage::WaitStart:
		if (Streamer->AreTilesReadyAround(StartCm, 1) || Now - StageStarted > 180.0)
		{
			Stage = EStage::Place;
		}
		break;
	case EStage::Place:
		if (ShotIndex >= Shots.Num())
		{
			Finish();
			return;
		}
		if (!PlaceShot(Shots[ShotIndex]))
		{
			++ShotIndex;
			return;
		}
		Streamer->SetFocus(TargetCm, FVector::ZeroVector);
		Stage = EStage::WaitTiles;
		StageStarted = Now;
		break;
	case EStage::WaitTiles:
		if ((Streamer->AreTilesReadyAround(TargetCm, 1) && !bShadersBusy) || Now - StageStarted > 240.0)
		{
			Stage = EStage::Settle;
			Frames = 0;
			StageStarted = Now;
		}
		break;
	case EStage::Settle:
		// Exposure, temporal AA, Lumen and virtual shadow pages converge over a few seconds.
		if (bShadersBusy)
		{
			StageStarted = Now;
			Frames = 0;
			break;
		}
		if (++Frames > 60 && Now - StageStarted > 6.0)
		{
			Stage = EStage::Shoot;
		}
		break;
	case EStage::Shoot:
	{
		const FString Name = Shots[ShotIndex];
		if (Name == TEXT("drop") && DropBody)
		{
			double Road = 0.0;
			const FVector P = DropBody->GetActorLocation();
			Streamer->SurfaceHeightAt(P, Road);
			const double HalfHeight = 0.5 * 1.3 * 100.0;
			UE_LOG(LogSimCruisePreview, Display, TEXT("DROP box rests at z=%.1f cm; road surface %.1f cm; bottom %.1f cm above the road; moved %.1f cm sideways"),
				P.Z, Road, P.Z - HalfHeight - Road, FVector::Dist2D(P, TargetCm));
		}
		const FString File = FPaths::Combine(OutputDir, Name + TEXT(".png"));
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogSimCruisePreview, Display, TEXT("SHOT %s -> %s (camera %s)"), *Name, *File, *Camera->GetActorLocation().ToString());
		Stage = EStage::AfterShot;
		Frames = 0;
		break;
	}
	case EStage::AfterShot:
		if (++Frames > 10)
		{
			++ShotIndex;
			Stage = EStage::Place;
		}
		break;
	default:
		break;
	}
}
