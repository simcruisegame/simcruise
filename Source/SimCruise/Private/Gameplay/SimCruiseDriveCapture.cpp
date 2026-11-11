// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseDriveCapture.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruiseHUD.h"
#include "Gameplay/SimCruisePlayerCameraManager.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "World/SimCruiseWorldStreamer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseDriveCapture, Log, All);

ASimCruiseDriveCapture::ASimCruiseDriveCapture()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseDriveCapture::BeginPlay()
{
	Super::BeginPlay();
	OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("SimCruise"), TEXT("Drive"));
	FParse::Value(FCommandLine::Get(), TEXT("SimCruiseDriveCaptureOut="), OutputDir);
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	StageStarted = FPlatformTime::Seconds();
	UE_LOG(LogSimCruiseDriveCapture, Display, TEXT("Drive capture: waiting for the car -> %s"), *OutputDir);
}

ASimCruisePlayerController* ASimCruiseDriveCapture::GetPlayer() const
{
	return Cast<ASimCruisePlayerController>(UGameplayStatics::GetPlayerController(this, 0));
}

void ASimCruiseDriveCapture::Shoot(const FString& Name, bool bWithHud)
{
	const FString File = FPaths::Combine(OutputDir, Name + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(File, bWithHud, false);
	FString Sky;
	if (const USimCruiseEnvironmentSubsystem* Environments = USimCruiseEnvironmentSubsystem::Get(GetWorld()))
	{
		if (const ASimCruiseEnvironment* Environment = Environments->GetEnvironment())
		{
			Sky = Environment->Describe();
		}
	}
	const ASimCruisePlayerController* Player = GetPlayer();
	const FSimCruiseDriveTelemetry* T = Player ? &Player->GetTelemetry() : nullptr;
	int32 Contacts = 0;
	if (T)
	{
		for (const FSimCruiseDriveWheelTelemetry& W : T->Wheels)
		{
			Contacts += W.bContact ? 1 : 0;
		}
	}
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	UE_LOG(LogSimCruiseDriveCapture, Display, TEXT("SHOT %s -> %s: %.1f mph, gear %d, %.0f rpm, %d wheels on the ground, car at %s yaw %.1f; %s"),
		*Name, *File, T ? T->BodyVelocityMps.X * 2.236936 : 0.0, T ? T->Gear : 0, T ? T->EngineSpeedRpm : 0.0, Contacts,
		Pawn ? *Pawn->GetActorLocation().ToString() : TEXT("-"), Pawn ? Pawn->GetActorRotation().Yaw : 0.0, *Sky);
}

void ASimCruiseDriveCapture::Finish()
{
	Stage = EStage::Done;
	UE_LOG(LogSimCruiseDriveCapture, Display, TEXT("Drive capture finished"));
	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseKeepOpen")))
	{
		return;
	}
	if (APlayerController* Player = GetPlayer())
	{
		Player->ConsoleCommand(TEXT("quit"));
	}
	else
	{
		FPlatformMisc::RequestExit(false);
	}
}

void ASimCruiseDriveCapture::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASimCruisePlayerController* Player = GetPlayer();
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const double Now = FPlatformTime::Seconds();
	const bool bShadersBusy = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;

	switch (Stage)
	{
	case EStage::WaitCar:
	{
		const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
		const bool bPlaced = Mode ? Mode->IsDriveReady() : Pawn != nullptr;
		const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
		const bool bTiles = !Stream || (Pawn && Stream->AreTilesReadyAround(Pawn->GetActorLocation(), 1));
		if ((Pawn && bPlaced && bTiles) || Now - StageStarted > 400.0)
		{
			Stage = EStage::Settle;
			StageStarted = Now;
			SettleFrames = 0;
		}
		break;
	}
	case EStage::Settle:
		// The car settles on its springs; exposure, TSR, Lumen and virtual shadow pages converge.
		if (bShadersBusy)
		{
			StageStarted = Now;
			SettleFrames = 0;
			break;
		}
		if (++SettleFrames > 90 && Now - StageStarted > 8.0)
		{
			Stage = EStage::Shots;
			Clock = 0.f;
			NextShot = 0;
		}
		break;
	case EStage::Shots:
	{
		if (!Player || !Pawn)
		{
			Finish();
			break;
		}
		const float Before = Clock;
		Clock += DeltaSeconds;
		auto Crossed = [Before, this](float T) { return Before < T && Clock >= T; };
		ASimCruisePlayerCameraManager* Camera = Cast<ASimCruisePlayerCameraManager>(Player->PlayerCameraManager);
		ASimCruiseHUD* Hud = Cast<ASimCruiseHUD>(Player->GetHUD());

		// Pedals through the real input stack, as a pad would press them.
		const float Speed = static_cast<float>(Player->GetTelemetry().BodyVelocityMps.X);
		float Throttle = 0.f;
		float Brake = 0.f;
		if (Clock > 7.6f && Clock <= 17.2f) { Throttle = 0.45f; }
		if (Clock > 17.2f && Speed > 0.3f) { Brake = 0.55f; }
		Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, Throttle);
		Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, Brake);
		Player->InjectAxis(EKeys::Gamepad_LeftX, 0.f);

		if (Crossed(0.01f))
		{
			if (Camera) { Camera->SetView(ESimCruiseView::Driver); }
			if (Hud) { Hud->SetLevel(ESimCruiseHudLevel::Off); }
		}
		if (Crossed(2.0f)) { Shoot(TEXT("drive-cockpit"), false); }
		if (Crossed(2.2f) && Camera) { Camera->SetView(ESimCruiseView::Chase); }
		if (Crossed(4.6f)) { Shoot(TEXT("drive-chase"), false); }
		if (Crossed(4.8f))
		{
			// Straight down on the road, the car's nose to the top of the picture.
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Over = Pawn->GetActorLocation() + FVector(0.0, 0.0, 3500.0);
			TopCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(FRotator(-90.0, Pawn->GetActorRotation().Yaw, 0.0), Over), Params);
			if (TopCamera)
			{
				TopCamera->GetCameraComponent()->SetFieldOfView(60.f);
				TopCamera->GetCameraComponent()->bConstrainAspectRatio = false;
				Player->SetViewTarget(TopCamera);
			}
		}
		if (Crossed(7.3f)) { Shoot(TEXT("drive-top"), false); }
		if (Crossed(7.5f))
		{
			Player->SetViewTarget(Pawn);
			if (Camera) { Camera->SetView(ESimCruiseView::Driver); Camera->SnapNextFrame(); }
			if (TopCamera) { TopCamera->Destroy(); TopCamera = nullptr; }
		}
		if (Crossed(14.3f) && Hud) { Hud->SetLevel(ESimCruiseHudLevel::Glance); }
		if (Crossed(15.0f)) { Shoot(TEXT("drive-cockpit-moving"), true); }
		if (Crossed(15.2f) && Camera) { Camera->SetView(ESimCruiseView::Chase); }
		if (Crossed(17.0f)) { Shoot(TEXT("drive-chase-moving"), true); }
		if (Clock > 18.0f && FMath::Abs(Speed) < 0.3f)
		{
			Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, 0.f);
			UE_LOG(LogSimCruiseDriveCapture, Display, TEXT("Stopped %.1f s after braking began"), Clock - 17.2f);
			if (Camera) { Camera->SetView(ESimCruiseView::Driver); }
			Finish();
		}
		else if (Clock > 40.0f)
		{
			Finish();
		}
		break;
	}
	default:
		break;
	}
}
