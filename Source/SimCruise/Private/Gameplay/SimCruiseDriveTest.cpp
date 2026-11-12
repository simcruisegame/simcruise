// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseDriveTest.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"
#include "World/SimCruiseWorldStreamer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseDriveTest, Log, All);

ASimCruiseDriveTest::ASimCruiseDriveTest()
{
	PrimaryActorTick.bCanEverTick = true;
	// Before the player controller reads its input this frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseDriveTest::BeginPlay()
{
	Super::BeginPlay();
	OutputFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DriveTest"), TEXT("drive-test.csv"));
	FParse::Value(FCommandLine::Get(), TEXT("SimCruiseDriveTestOut="), OutputFile);
	StageStarted = FPlatformTime::Seconds();
	bPad = FParse::Param(FCommandLine::Get(), TEXT("SimCruiseDriveTestPad"));
	bLogOnly = FParse::Param(FCommandLine::Get(), TEXT("SimCruiseDriveLog"));
	if (bLogOnly && !FParse::Value(FCommandLine::Get(), TEXT("SimCruiseDriveTestOut="), OutputFile))
	{
		OutputFile = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DriveTest"),
			FString::Printf(TEXT("drive-log-%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
	}
	FString Header = TEXT("t,frame_dt,phase,stick,rt,lt,shaper_rack_deg,rack_deg,vx,vy,yaw_rate_dps,roll_rate_dps,beta_deg,gear,rpm,throttle,tc_cut,brake,contacts,x_m,y_m,z_m,yaw_deg,phys_steps,rack_mz_nm");
	static const TCHAR* Names[4] = {TEXT("fl"), TEXT("fr"), TEXT("rl"), TEXT("rr")};
	for (const TCHAR* N : Names)
	{
		Header += FString::Printf(TEXT(",%s_load,%s_alpha_deg,%s_kappa,%s_fx,%s_fy,%s_use,%s_steer_deg,%s_omega,%s_nx,%s_ny,%s_nz,%s_susp_cm,%s_grip"),
			N, N, N, N, N, N, N, N, N, N, N, N, N);
	}
	Rows.Add(Header);
	UE_LOG(LogSimCruiseDriveTest, Display, TEXT("Drive test: waiting for the car -> %s"), *OutputFile);
}

ASimCruisePlayerController* ASimCruiseDriveTest::GetPlayer() const
{
	return Cast<ASimCruisePlayerController>(UGameplayStatics::GetPlayerController(this, 0));
}

void ASimCruiseDriveTest::ClosePhase()
{
	if (!Phase.IsEmpty())
	{
		UE_LOG(LogSimCruiseDriveTest, Display, TEXT("DRIVETEST %s: peak %.0f km/h, peak rear-axle slip %.1f deg, peak yaw rate %.1f deg/s%s"),
			*Phase, PeakSpeedKph, PeakBetaDeg, PeakYawDegPerS, PeakBetaDeg > 15.0 ? TEXT(" -- SPUN") : TEXT(""));
	}
	PeakBetaDeg = 0.0;
	PeakYawDegPerS = 0.0;
	PeakSpeedKph = 0.0;
}

void ASimCruiseDriveTest::WriteRow(float DeltaSeconds, float Stick, float Throttle, float Brake)
{
	const ASimCruisePlayerController* Player = GetPlayer();
	const ASimCruiseVehicle* Car = Player ? Cast<ASimCruiseVehicle>(Player->GetPawn()) : nullptr;
	if (!Car || !Car->GetVehicleMovement())
	{
		return;
	}
	FSimCruiseVehicleTelemetry T;
	Car->GetVehicleMovement()->CopyTelemetry(T);
	const FVector V = T.BodyVelocityLocalMps;
	const FVector W = T.BodyAngularVelocityLocalRadPerSecond;
	const double Beta = V.X > 1.0 ? FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)) : 0.0;
	// A slide shows at the rear axle: in a tight turn at walking pace the body
	// slip at the centre of mass is large by geometry alone.
	const FSimCruiseVehicleSetup Setup = Car->GetVehicleSetup();
	const double ToRearAxleM = Setup.WheelbaseM - Setup.CenterOfMassToFrontAxleM;
	if (V.X > 5.0)
	{
		const double RearSlipDeg = FMath::RadiansToDegrees(FMath::Atan2(V.Y - W.Z * ToRearAxleM, V.X));
		PeakBetaDeg = FMath::Max(PeakBetaDeg, FMath::Abs(RearSlipDeg));
	}
	PeakYawDegPerS = FMath::Max(PeakYawDegPerS, FMath::Abs(FMath::RadiansToDegrees(W.Z)));
	PeakSpeedKph = FMath::Max(PeakSpeedKph, T.SpeedKph);
	const FVector P = Car->GetActorLocation() / 100.0;
	FString Row = FString::Printf(TEXT("%.4f,%.4f,%s,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%d,%.0f,%.3f,%.3f,%.3f,%d,%.2f,%.2f,%.3f,%.2f,%lld,%.1f"),
		Clock, DeltaSeconds, *Phase, Stick, Throttle, Brake, FMath::RadiansToDegrees(Player->GetShaper().GetRackAngleRad()), T.SteeringAngleDeg,
		V.X, V.Y, FMath::RadiansToDegrees(W.Z), FMath::RadiansToDegrees(W.X), Beta, T.Gear, T.EngineRpm, T.Throttle, T.TractionControlCut,
		T.BrakeAmount, T.WheelContactCount, P.X, P.Y, P.Z, Car->GetActorRotation().Yaw, T.PhysicsSteps, T.RackAligningTorqueNm);
	for (int32 I = 0; I < 4; ++I)
	{
		const FSimCruiseWheelTelemetry Wh = T.Wheels.IsValidIndex(I) ? T.Wheels[I] : FSimCruiseWheelTelemetry();
		Row += FString::Printf(TEXT(",%.0f,%.3f,%.4f,%.0f,%.0f,%.3f,%.2f,%.2f,%.4f,%.4f,%.4f,%.2f,%.2f"),
			Wh.LoadN, Wh.SlipAngleDeg, Wh.SlipRatio, Wh.LongitudinalForceN, Wh.LateralForceN, Wh.CombinedGripUse, Wh.SteerAngleDeg,
			Wh.AngularSpeedRadPerSecond, Wh.ContactNormal.X, Wh.ContactNormal.Y, Wh.ContactNormal.Z, Wh.SuspensionLengthCm, Wh.SurfaceGrip);
	}
	Rows.Add(Row);
}

void ASimCruiseDriveTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bLogOnly && Rows.Num() > 1)
	{
		FFileHelper::SaveStringArrayToFile(Rows, *OutputFile);
		UE_LOG(LogSimCruiseDriveTest, Display, TEXT("Drive log: %d rows -> %s"), Rows.Num() - 1, *OutputFile);
	}
	Super::EndPlay(EndPlayReason);
}

void ASimCruiseDriveTest::Finish()
{
	ClosePhase();
	Stage = EStage::Done;
	FFileHelper::SaveStringArrayToFile(Rows, *OutputFile);
	UE_LOG(LogSimCruiseDriveTest, Display, TEXT("Drive test finished: %d rows -> %s"), Rows.Num() - 1, *OutputFile);
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

void ASimCruiseDriveTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ASimCruisePlayerController* Player = GetPlayer();
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	if (bLogOnly)
	{
		// A person drives; only record. The pedal and rack columns are the shaped command.
		if (Pawn && Stage != EStage::Done)
		{
			Clock += DeltaSeconds;
			Phase = TEXT("play");
			const FSimCruiseDriverCommand& Command = Player->GetLastCommand();
			WriteRow(DeltaSeconds, static_cast<float>(FMath::RadiansToDegrees(Command.RackAngleRad)), Command.Throttle, Command.Brake);
			if (Clock - LastFlush > 10.f)
			{
				LastFlush = Clock;
				FFileHelper::SaveStringArrayToFile(Rows, *OutputFile);
			}
		}
		return;
	}
	const double Now = FPlatformTime::Seconds();

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
		if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
		{
			StageStarted = Now;
			SettleFrames = 0;
			break;
		}
		if (bPad && PadStep == 0 && SettleFrames > 60 && Now - StageStarted > 5.0)
		{
			SpawnPad(Pawn);
			PadStep = -1;
			StageStarted = Now;
		}
		if (++SettleFrames > 60 && Now - StageStarted > 5.0 && (!bPad || PadStep == -1))
		{
			Stage = EStage::Drive;
			Clock = 0.f;
			PadStep = 0;
		}
		break;
	case EStage::Drive:
	{
		if (!Player || !Pawn)
		{
			Finish();
			break;
		}
		Clock += DeltaSeconds;
		if (bPad)
		{
			TickPad(Player, DeltaSeconds);
			break;
		}
		const float S = Clock;
		const double Speed = Player->GetTelemetry().BodyVelocityMps.X;
		// Hold a speed with the right trigger, as a person would by feel.
		auto Hold = [Speed](double TargetMps) { return static_cast<float>(FMath::Clamp(0.25 + 0.25 * (TargetMps - Speed), 0.0, 1.0)); };
		float Stick = 0.f;
		float Throttle = 0.f;
		float Brake = 0.f;
		FString Now_Phase;
		if (S <= 7.f) { Now_Phase = TEXT("launch"); Throttle = 1.f; }
		else if (S <= 11.f) { Now_Phase = TEXT("cruise50"); Throttle = Hold(13.9); }
		else if (S <= 15.f)
		{
			Now_Phase = TEXT("lanechange");
			Throttle = Hold(13.9);
			if (S <= 11.8f) { Stick = 0.35f; }
			else if (S <= 12.6f) { Stick = -0.35f; }
		}
		else if (S <= 18.f)
		{
			Now_Phase = TEXT("flick");
			Throttle = Hold(13.9);
			if (S <= 15.4f) { Stick = 1.f; }
			else if (S <= 15.8f) { Stick = -1.f; }
		}
		else if (S <= 23.f)
		{
			Now_Phase = TEXT("lift");
			if (S <= 20.f) { Throttle = 1.f; }
			if (S > 19.5f && S <= 21.5f) { Stick = 0.25f; }
		}
		else
		{
			Now_Phase = TEXT("brake");
			Brake = Speed > 0.3 ? 1.f : 0.f;
			Stick = S <= 24.f ? 0.15f : 0.f;
		}
		if (Now_Phase != Phase)
		{
			ClosePhase();
			Phase = Now_Phase;
		}
		Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, Throttle);
		Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, Brake);
		Player->InjectAxis(EKeys::Gamepad_LeftX, Stick);
		WriteRow(DeltaSeconds, Stick, Throttle, Brake);
		if ((S > 24.f && FMath::Abs(Speed) < 0.3) || S > 40.f)
		{
			Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, 0.f);
			Finish();
		}
		break;
	}
	default:
		break;
	}
}

void ASimCruiseDriveTest::SpawnPad(APawn* Pawn)
{
	ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(Pawn);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Car || !Cube)
	{
		UE_LOG(LogSimCruiseDriveTest, Error, TEXT("Drive test pad: no car or no cube mesh"));
		return;
	}
	// The engine cube is 100 cm; 4 km square, 1 m thick, its top 60 m above the car.
	const FVector Top = Car->GetActorLocation() + FVector(0.0, 0.0, 6000.0);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Pad = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
		FTransform(FRotator::ZeroRotator, Top - FVector(0.0, 0.0, 50.0), FVector(4000.0, 4000.0, 1.0)), Params);
	if (Pad)
	{
		// A registered static component refuses a new mesh; the pad never moves anyway.
		Pad->SetMobility(EComponentMobility::Movable);
		Pad->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Pad->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		UE_LOG(LogSimCruiseDriveTest, Display, TEXT("Drive test pad mesh %s"), *GetNameSafe(Pad->GetStaticMeshComponent()->GetStaticMesh()));
	}
	Car->ResetAt(Top, Car->GetActorRotation().Yaw);
	UE_LOG(LogSimCruiseDriveTest, Display, TEXT("Drive test pad at %s"), *Top.ToString());
}

void ASimCruiseDriveTest::TickPad(ASimCruisePlayerController* Player, float DeltaSeconds)
{
	struct FStep { const TCHAR* Name; double EntryMps; float Seconds; };
	static const FStep Steps[] = {
		{TEXT("launch_turn"), 0.0, 4.f},
		{TEXT("launch_turn_trig70"), 0.0, 4.f},
		{TEXT("junction20_trig70"), 5.6, 3.5f},
		{TEXT("junction30_trig70"), 8.3, 4.f},
		{TEXT("junction20_gas"), 5.6, 3.5f},
		{TEXT("junction30"), 8.3, 4.f},
		{TEXT("circle50_lift"), 13.9, 7.f},
		{TEXT("turnin50_full"), 13.9, 3.f},
		{TEXT("brake_turn70"), 19.4, 4.f},
		{TEXT("lanechange80"), 22.2, 4.f},
	};
	constexpr int32 StepCount = UE_ARRAY_COUNT(Steps);
	if (PadStep >= StepCount)
	{
		Finish();
		return;
	}
	const FStep& Step = Steps[PadStep];
	const double Speed = Player->GetTelemetry().BodyVelocityMps.X;
	const double Yaw = FMath::Abs(Player->GetTelemetry().BodyAngularVelocityRadPerS.Z);
	auto Hold = [Speed](double TargetMps) { return static_cast<float>(FMath::Clamp(0.25 + 0.25 * (TargetMps - Speed), 0.0, 1.0)); };
	float Stick = 0.f, Throttle = 0.f, Brake = 0.f;
	StepClock += DeltaSeconds;
	if (!bActing)
	{
		// Straight approach at the entry speed until it is steady.
		const FString Name = FString::Printf(TEXT("approach_%s"), Step.Name);
		if (Phase != Name) { ClosePhase(); Phase = Name; }
		if (Speed > Step.EntryMps + 0.4) { Brake = static_cast<float>(FMath::Clamp(0.2 * (Speed - Step.EntryMps), 0.1, 0.6)); }
		else if (Step.EntryMps > 0.0) { Throttle = Hold(Step.EntryMps); }
		const bool bSteady = FMath::Abs(Speed - Step.EntryMps) < 0.5 && Yaw < 0.02;
		SteadyClock = bSteady ? SteadyClock + DeltaSeconds : 0.f;
		if (SteadyClock > 1.5f || StepClock > 25.f)
		{
			bActing = true;
			StepClock = 0.f;
			ClosePhase();
			Phase = Step.Name;
		}
	}
	else
	{
		const float S = StepClock;
		const FString Name(Step.Name);
		if (Name == TEXT("launch_turn")) { Stick = 1.f; Throttle = 1.f; }
		else if (Name == TEXT("launch_turn_trig70")) { Stick = 1.f; Throttle = 0.7f; }
		else if (Name == TEXT("junction20_trig70")) { Stick = 0.8f; Throttle = S > 0.8f ? 0.7f : 0.2f; }
		else if (Name == TEXT("junction30_trig70")) { Stick = 1.f; Throttle = S > 1.5f ? 0.7f : 0.3f; }
		else if (Name == TEXT("junction20_gas")) { Stick = 0.8f; Throttle = S > 0.8f ? 1.f : 0.2f; }
		else if (Name == TEXT("junction30")) { Stick = 1.f; Throttle = S > 1.5f ? 1.f : 0.3f; }
		else if (Name == TEXT("circle50_lift")) { Stick = FMath::Min(S, 1.f) * 0.5f; Throttle = S > 5.f ? 0.f : Hold(13.9); }
		else if (Name == TEXT("turnin50_full")) { Stick = 1.f; Throttle = Hold(13.9); }
		else if (Name == TEXT("brake_turn70")) { Stick = 0.4f; Brake = S > 0.6f && Speed > 0.3 ? 1.f : 0.f; Throttle = S <= 0.6f ? Hold(19.4) : 0.f; }
		else if (Name == TEXT("lanechange80")) { Stick = S <= 0.6f ? 0.6f : (S <= 1.2f ? -0.6f : 0.f); Throttle = Hold(22.2); }
		if (StepClock > Step.Seconds)
		{
			bActing = false;
			StepClock = 0.f;
			SteadyClock = 0.f;
			++PadStep;
		}
	}
	Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, Throttle);
	Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, Brake);
	Player->InjectAxis(EKeys::Gamepad_LeftX, Stick);
	WriteRow(DeltaSeconds, Stick, Throttle, Brake);
}
