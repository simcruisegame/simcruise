// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseDriveCapture.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "Environment/SimCruiseGraphics.h"
#include "Environment/SimCruisePerfSampler.h"
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
	FString Presets;
	if (FParse::Value(FCommandLine::Get(), TEXT("SimCruiseDrivePerf="), Presets, false))
	{
		TArray<FString> Names;
		Presets.ParseIntoArray(Names, TEXT(","));
		for (const FString& Name : Names)
		{
			PerfSteps.Add(Name + TEXT(":driver"));
			PerfSteps.Add(Name + TEXT(":chase"));
		}
	}
	FString PoseText;
	if (FParse::Value(FCommandLine::Get(), TEXT("SimCruiseRealismPose="), PoseText, false))
	{
		TArray<FString> Entries;
		PoseText.ParseIntoArray(Entries, TEXT("+"));
		for (const FString& Entry : Entries)
		{
			TArray<FString> Fields;
			Entry.ParseIntoArray(Fields, TEXT(","));
			if (Fields.Num() != 5)
			{
				UE_LOG(LogSimCruiseDriveCapture, Error, TEXT("Realism pose '%s' is not name,lat,lon,heading,hfov"), *Entry);
				continue;
			}
			FRealismPose Pose;
			Pose.Name = Fields[0];
			Pose.LatDeg = FCString::Atod(*Fields[1]);
			Pose.LonDeg = FCString::Atod(*Fields[2]);
			Pose.HeadingDeg = FCString::Atod(*Fields[3]);
			Pose.HorizontalFovDeg = FMath::Clamp(FCString::Atof(*Fields[4]), 5.f, 170.f);
			RealismPoses.Add(Pose);
		}
	}
	UE_LOG(LogSimCruiseDriveCapture, Display, TEXT("Drive capture: waiting for the car -> %s (%d realism poses)"), *OutputDir, RealismPoses.Num());
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
		if (SettleFrames > 90 && Now - StageStarted > 8.0 && !RealismPoses.IsEmpty())
		{
			Stage = EStage::Realism;
			RealismIndex = 0;
			RealismFrames = 0;
			StageStarted = Now;
			break;
		}
		if (++SettleFrames > 90 && Now - StageStarted > 8.0 && !PerfSteps.IsEmpty())
		{
			Stage = EStage::Perf;
			PerfIndex = 0;
			bPerfSampling = false;
			StageStarted = Now;
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
	case EStage::Realism:
		TickRealism(DeltaSeconds, Player, Pawn, bShadersBusy);
		break;
	case EStage::Perf:
	{
		// Each step: apply the preset and view, let shaders, exposure and Lumen settle 6 s
		// (longer while shaders compile), then sample 10 s. One "SimCruisePerf:" line each.
		if (bPerfSampling)
		{
			break;
		}
		if (PerfIndex >= PerfSteps.Num() || !Player)
		{
			Finish();
			break;
		}
		FString PresetName, View;
		PerfSteps[PerfIndex].Split(TEXT(":"), &PresetName, &View);
		ASimCruisePlayerCameraManager* Camera = Cast<ASimCruisePlayerCameraManager>(Player->PlayerCameraManager);
		if (Clock == 0.f)
		{
			ESimCruiseGraphicsPreset Preset;
			if (FSimCruiseGraphics::Parse(PresetName, Preset) && FSimCruiseGraphics::Current() != Preset)
			{
				FSimCruiseGraphics::Apply(Preset);
			}
			if (Camera) { Camera->SetView(View == TEXT("chase") ? ESimCruiseView::Chase : ESimCruiseView::Driver); }
			if (ASimCruiseHUD* Hud = Cast<ASimCruiseHUD>(Player->GetHUD())) { Hud->SetLevel(ESimCruiseHudLevel::Off); }
		}
		Clock += DeltaSeconds;
		if (bShadersBusy)
		{
			Clock = FMath::Min(Clock, 0.01f);
			break;
		}
		if (Clock > 6.f)
		{
			bPerfSampling = true;
			FSimCruisePerfSampler::Start(FString::Printf(TEXT("drive_%s_%s"), *PresetName, *View), 10.f,
				[WeakThis = TWeakObjectPtr<ASimCruiseDriveCapture>(this)](const FSimCruisePerfStats&)
				{
					if (ASimCruiseDriveCapture* Self = WeakThis.Get())
					{
						// -SimCruiseDrivePerfProfile: a GPU pass breakdown of the frame after each window.
						if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseDrivePerfProfile")))
						{
							if (APlayerController* P = Self->GetPlayer()) { P->ConsoleCommand(TEXT("ProfileGPU")); }
						}
						Self->bPerfSampling = false;
						Self->Clock = 0.f;
						++Self->PerfIndex;
					}
				});
		}
		break;
	}
	default:
		break;
	}
}

void ASimCruiseDriveCapture::TickRealism(float DeltaSeconds, ASimCruisePlayerController* Player, APawn* Pawn, bool bShadersBusy)
{
	const double Now = FPlatformTime::Seconds();
	const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	if (!Player || !Stream || !Stream->GetFrame().IsValid() || RealismIndex >= RealismPoses.Num())
	{
		if (Pawn) { Pawn->SetActorHiddenInGame(false); }
		if (Player && Pawn) { Player->SetViewTarget(Pawn); }
		if (PoseCamera) { PoseCamera->Destroy(); PoseCamera = nullptr; }
		if (!Stream) { UE_LOG(LogSimCruiseDriveCapture, Error, TEXT("Realism poses need the streamed world (-SimCruiseWorld or -SimCruiseStart)")); }
		Finish();
		return;
	}
	const FRealismPose& Pose = RealismPoses[RealismIndex];

	// Placed again every frame from the world frame, so a floating-origin rebase cannot move it.
	FVector Eye = Stream->GetFrame().LonLatToWorldCm(Pose.LonDeg, Pose.LatDeg);
	double GroundCm = 0.0;
	const bool bGround = Stream->SurfaceHeightAt(Eye, GroundCm);
	Eye.Z = GroundCm + 165.0;
	if (!PoseCamera)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		PoseCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		if (!PoseCamera) { Finish(); return; }
		PoseCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		if (Pawn) { Pawn->SetActorHiddenInGame(true); }
		if (ASimCruiseHUD* Hud = Cast<ASimCruiseHUD>(Player->GetHUD())) { Hud->SetLevel(ESimCruiseHudLevel::Off); }
		Player->SetViewTarget(PoseCamera);
	}
	// Compass heading (0 north, clockwise) to yaw: the world's X runs east and Y south.
	PoseCamera->SetActorLocationAndRotation(Eye, FRotator(0.0, Pose.HeadingDeg - 90.0, 0.0));
	PoseCamera->GetCameraComponent()->SetFieldOfView(Pose.HorizontalFovDeg);

	// Wait for the ground and tiles at the pose, compiled shaders, then 4 s and 120 frames for
	// exposure, TSR, Lumen and virtual shadow pages; give up on a pose after 300 s.
	const bool bReady = bGround && Stream->AreTilesReadyAround(Eye, 1) && !bShadersBusy;
	if (!bReady)
	{
		RealismFrames = 0;
		Clock = 0.f;
		if (Now - StageStarted > 300.0)
		{
			UE_LOG(LogSimCruiseDriveCapture, Error, TEXT("REALISM %s: the world never loaded at %.6f,%.6f; skipped"), *Pose.Name, Pose.LatDeg, Pose.LonDeg);
			++RealismIndex;
			StageStarted = Now;
		}
		return;
	}
	Clock += DeltaSeconds;
	if (++RealismFrames >= 120 && Clock > 4.f)
	{
		Shoot(TEXT("realism-") + Pose.Name, false);
		UE_LOG(LogSimCruiseDriveCapture, Display, TEXT("REALISM %s: lat %.7f lon %.7f heading %.1f hfov %.1f eye %s ground %.2f m"),
			*Pose.Name, Pose.LatDeg, Pose.LonDeg, Pose.HeadingDeg, Pose.HorizontalFovDeg, *Eye.ToString(), GroundCm / 100.0);
		++RealismIndex;
		RealismFrames = 0;
		Clock = 0.f;
		StageStarted = Now;
	}
}
