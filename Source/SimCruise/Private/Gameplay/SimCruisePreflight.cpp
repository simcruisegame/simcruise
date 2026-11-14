// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePreflight.h"

#include "CollisionQueryParams.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruiseReview.h"
#include "Gameplay/SimCruiseCockpitTest.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "HAL/IConsoleManager.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"
#include "Gameplay/SimCruiseViewCheck.h"
#include "HAL/FileManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "ProceduralMeshComponent.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "World/SimCruiseBridges.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseRoadNetwork.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTraffic.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruisePreflight, Log, All);

namespace
{
	constexpr double ReadyTimeoutS = 600.0;
	constexpr float SettleS = 2.f;
	constexpr float FrameIntervalS = 0.1f;
	constexpr int32 SkyGridX = 16;
	constexpr int32 SkyGridY = 9;
	constexpr int32 ThumbW = 64;
	constexpr int32 ThumbH = 36;
	/** Comfortable lateral and braking acceleration of the robot (m/s^2). */
	constexpr double LateralAccel = 3.0;
	constexpr double BrakeAccel = 3.0;
	constexpr double MaxSpeedMps = 30.0;

	bool IsRoadLayer(ESimCruiseTileLayer Layer)
	{
		return Layer == ESimCruiseTileLayer::Asphalt || Layer == ESimCruiseTileLayer::Concrete;
	}

	bool IsCarActor(const AActor* Actor)
	{
		for (const AActor* Each = Actor; Each; Each = Each->GetAttachParentActor())
		{
			if (Each->IsA<ASimCruiseTrafficCar>() || Each->IsA<ASimCruiseCarPresentationActor>()) return true;
		}
		return false;
	}

	float Luma(const FColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; }
}

namespace SimCruisePreflightRules
{
	FString Judge(const FSimCruisePreflightStats& S)
	{
		TArray<FString> R;
		if (S.DistanceM < MinDistanceM) R.Add(FString::Printf(TEXT("robot-moved-only-%.0fm"), S.DistanceM));
		if (S.OffRoadSeconds > MaxOffRoadSeconds) R.Add(FString::Printf(TEXT("left-the-road-%.1fs"), S.OffRoadSeconds));
		if (S.bFellBack) R.Add(TEXT("start-fell-back-to-bundled-place"));
		if (S.bViewCheckRan && !S.bViewCheckPassed) R.Add(FString::Printf(TEXT("viewcheck-fail(%s)"), *S.ViewCheckReasons.Replace(TEXT(","), TEXT("+"))));
		if (S.SkySamples >= 50 && S.SkyWhitePercent > MaxSkyWhitePercent) R.Add(FString::Printf(TEXT("white-sky-%.0f%%"), S.SkyWhitePercent));
		if (S.TilePopIns > 0) R.Add(FString::Printf(TEXT("tile-pop-in-%d"), S.TilePopIns));
		if (S.CarPopIns > MaxCarPopIns) R.Add(FString::Printf(TEXT("car-pop-in-%d"), S.CarPopIns));
		if (S.FlashFrames > MaxFlashFrames) R.Add(FString::Printf(TEXT("flash-frames-%d"), S.FlashFrames));
		if (S.RoadSteps > 0) R.Add(FString::Printf(TEXT("road-steps-%d-max-%.0fcm"), S.RoadSteps, S.MaxStepCm));
		if (S.RoadGaps > 0) R.Add(FString::Printf(TEXT("road-gaps-%d"), S.RoadGaps));
		if (S.BorderJolts > 0) R.Add(FString::Printf(TEXT("jolts-at-tile-borders-%d"), S.BorderJolts));
		if (S.StuckTraffic > 0) R.Add(FString::Printf(TEXT("stuck-traffic-%d"), S.StuckTraffic));
		if (S.DriveSeconds > 0.f && S.Frames < S.DriveSeconds / 0.1f * 0.5f) R.Add(FString::Printf(TEXT("few-frames-%d"), S.Frames));
		if (S.PeakRearSlipDeg > MaxRearSlipDeg || S.ReversedSeconds > MaxReversedSeconds)
		{
			R.Add(FString::Printf(TEXT("spin-rear-slip-%.0fdeg-backwards-%.1fs%s"), S.PeakRearSlipDeg, S.ReversedSeconds, S.bForcedSpin ? TEXT("-forced") : TEXT("")));
		}
		if (S.OffRibbonSeconds > MaxOffRibbonSeconds) R.Add(FString::Printf(TEXT("ran-off-%.1fm-past-the-edge-for-%.1fs"), S.MaxOffRibbonM, S.OffRibbonSeconds));
		if (S.TimedFrames >= MinTimedFrames && S.FrameP95Ms > MaxFrameP95Ms) R.Add(FString::Printf(TEXT("slow-frames-p95-%.0fms"), S.FrameP95Ms));
		if (S.TimedFrames >= MinTimedFrames && S.Hitches > MaxHitches) R.Add(FString::Printf(TEXT("hitches-%d-over-100ms"), S.Hitches));
		return FString::Join(R, TEXT(","));
	}

	int32 CountSteps(const TArray<FVector2f>& D, float& OutMaxStepCm, float MaxGapM)
	{
		OutMaxStepCm = 0.f;
		int32 Count = 0;
		int32 LastCounted = -10;
		for (int32 I = 1; I + 1 < D.Num(); ++I)
		{
			const FVector2f& A = D[I - 1];
			const FVector2f& B = D[I];
			const FVector2f& C = D[I + 1];
			const float AB = B.X - A.X;
			const float BC = C.X - B.X;
			if (AB < 0.02f || BC < 0.02f || AB > MaxGapM || BC > MaxGapM) continue;
			// The height change from B to C that the slope from A to B does not explain.
			const float JumpCm = FMath::Abs((C.Y - B.Y) - (B.Y - A.Y) * BC / AB) * 100.f;
			OutMaxStepCm = FMath::Max(OutMaxStepCm, JumpCm);
			if (JumpCm > StepCm && I > LastCounted + 2)
			{
				++Count;
				LastCounted = I;
			}
		}
		return Count;
	}

	void MeasureFrames(const TArray<float>& TimedMs, FSimCruisePreflightStats& Stats)
	{
		Stats.TimedFrames = TimedMs.Num();
		Stats.FrameP95Ms = 0.f;
		Stats.SlowFrames = 0;
		Stats.Hitches = 0;
		if (TimedMs.IsEmpty())
		{
			return;
		}
		TArray<float> Sorted = TimedMs;
		Sorted.Sort();
		Stats.FrameP95Ms = Sorted[FMath::Clamp(FMath::CeilToInt32(0.95 * Sorted.Num()) - 1, 0, Sorted.Num() - 1)];
		for (const float Ms : TimedMs)
		{
			Stats.SlowFrames += Ms > 50.f ? 1 : 0;
			Stats.Hitches += Ms > 100.f ? 1 : 0;
		}
	}

	int32 CountFlashes(const TArray<float>& Diffs, const TArray<bool>& Turning, float& OutMedian, float Factor, float Floor)
	{
		TArray<float> Steady;
		for (int32 I = 0; I < Diffs.Num(); ++I)
		{
			if (!Turning.IsValidIndex(I) || !Turning[I]) Steady.Add(Diffs[I]);
		}
		OutMedian = 0.f;
		if (Steady.IsEmpty()) return 0;
		Steady.Sort();
		OutMedian = Steady[Steady.Num() / 2];
		const float Limit = FMath::Max(Floor, Factor * OutMedian);
		int32 Count = 0;
		for (const float D : Steady)
		{
			Count += D > Limit ? 1 : 0;
		}
		return Count;
	}
}

ASimCruisePreflight::ASimCruisePreflight()
{
	PrimaryActorTick.bCanEverTick = true;
	// Before the player controller reads its input this frame.
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruisePreflight::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("SimCruisePreflightSeconds="), DriveSeconds);
	DriveSeconds = FMath::Clamp(DriveSeconds, 5.f, 600.f);
	bNoFrames = FParse::Param(Cmd, TEXT("SimCruisePreflightNoFrames"));
	bForceSpin = FParse::Param(Cmd, TEXT("SimCruisePreflightForceSpin"));
	double Kmh = 0.0;
	if (FParse::Value(Cmd, TEXT("SimCruisePreflightKmh="), Kmh) && Kmh > 0.0)
	{
		SpeedOverrideMps = FMath::Clamp(Kmh, 10.0, 300.0) / 3.6;
	}
	// -SimCruisePreflightKeen: a keen driver on an empty road (the owner's acceptance drive): no
	// speed limit, corners at 75 % of the car's own skidpad grip, brakes at 70 % of it, up to 45 m/s,
	// looking 200 m ahead, traffic off.
	bKeen = FParse::Param(Cmd, TEXT("SimCruisePreflightKeen"));
	if (bKeen)
	{
		if (IConsoleVariable* Cars = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Traffic.Cars"))) Cars->Set(0, ECVF_SetByCode);
	}
	FParse::Value(Cmd, TEXT("SimCruisePreflightRoad="), RoadName);
	FParse::Value(Cmd, TEXT("SimCruiseCar="), CarId);
	if (!FParse::Value(Cmd, TEXT("SimCruisePreflightOut="), OutputDir))
	{
		OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Preflight"), FString::Printf(TEXT("manual-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
	}
	OutputDir = FPaths::ConvertRelativePathToFull(OutputDir);
	IFileManager::Get().MakeDirectory(*FPaths::Combine(OutputDir, TEXT("frames")), true);
	// -SimCruiseReview: full-size frames, sidecars and close-ups for a person to look at (scripts\launch-review.ps1).
	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseReview")))
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("SimCruisePreflightNoFrames")))
		{
			UE_LOG(LogSimCruisePreflight, Warning, TEXT("Pre-flight: -SimCruiseReview needs the frames; no review evidence with -SimCruisePreflightNoFrames"));
		}
		else
		{
			Review.Begin(OutputDir);
		}
	}
	FString Start;
	if (FParse::Value(Cmd, TEXT("SimCruiseStart="), Start, /*bShouldStopOnSeparator=*/ false))
	{
		FString A, B;
		if (Start.Split(TEXT(","), &A, &B))
		{
			RequestedLat = FCString::Atod(*A.TrimStartAndEnd());
			RequestedLon = FCString::Atod(*B.TrimStartAndEnd());
			bRequestedStart = true;
		}
	}
	Started = FPlatformTime::Seconds();
	StageStarted = Started;
	UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight: %s with %s, %.0f s -> %s"), *RoadName, *CarId, DriveSeconds, *OutputDir);
}

void ASimCruisePreflight::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FrameHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(FrameHandle);
		FrameHandle.Reset();
	}
	if (ASimCruiseWorldStreamer* Stream = RebaseStream.Get())
	{
		Stream->OnRebased.Remove(RebaseHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ASimCruisePreflight::OnRebased(const FVector& OffsetCm)
{
	LastCarCm += OffsetCm;
	for (TPair<TWeakObjectPtr<AActor>, FVector>& Car : KnownCars)
	{
		Car.Value += OffsetCm;
	}
}

void ASimCruisePreflight::GatherLines(const ASimCruiseWorldStreamer* Stream, const FVector& CarCm)
{
	Lines.Reset();
	const FIntPoint Here = Stream->GetFrame().TileAtWorldCm(CarCm);
	for (int32 DX = -1; DX <= 1; ++DX)
	{
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			const ASimCruiseWorldTile* Tile = Stream->GetTile(Here + FIntPoint(DX, DY));
			const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
			if (!Build) continue;
			const FVector2d Origin(Tile->GetActorLocation().X, Tile->GetActorLocation().Y);
			for (const FSimCruiseRibbon& Ribbon : Build->Roads.Ribbons)
			{
				if (Ribbon.St.Num() < 2 || Ribbon.bService) continue;
				FLine& Line = Lines.AddDefaulted_GetRef();
				Line.Class = Ribbon.Class;
				Line.Travel = Ribbon.St[0].Travel;
				Line.Ribbon = &Ribbon;
				for (const FSimCruiseRibbonStation& S : Ribbon.St)
				{
					Line.P.Add(Origin + S.C * 100.0);
					Line.HalfW.Add(S.HalfW);
				}
			}
		}
	}
}

ASimCruisePreflight::FCursor ASimCruisePreflight::FindCursor(const FVector2d& CarXY, const FVector2d& Forward) const
{
	FCursor Best;
	double BestScore = 3000.0;
	for (int32 L = 0; L < Lines.Num(); ++L)
	{
		const TArray<FVector2d>& P = Lines[L].P;
		for (int32 I = 0; I + 1 < P.Num(); ++I)
		{
			const FVector2d AB = P[I + 1] - P[I];
			const double Len2 = AB.SquaredLength();
			if (Len2 < 1.0) continue;
			const double T = FMath::Clamp(FVector2d::DotProduct(CarXY - P[I], AB) / Len2, 0.0, 1.0);
			const double Dist = FVector2d::Distance(CarXY, P[I] + AB * T);
			const double Align = FVector2d::DotProduct(AB / FMath::Sqrt(Len2), Forward);
			if (FMath::Abs(Align) < 0.5) continue;
			// Near and parallel wins; a crossing road under the car loses to the one it drives along.
			const double Score = Dist + (1.0 - FMath::Abs(Align)) * 800.0;
			if (Score < BestScore)
			{
				BestScore = Score;
				Best.Line = L;
				Best.Seg = I;
				Best.T = T;
				Best.Dir = Align >= 0.0 ? 1 : -1;
			}
		}
	}
	return Best;
}

FVector2d ASimCruisePreflight::PointAt(const FCursor& C) const
{
	const TArray<FVector2d>& P = Lines[C.Line].P;
	return P[C.Seg] + (P[C.Seg + 1] - P[C.Seg]) * C.T;
}

FVector2d ASimCruisePreflight::TangentAt(const FCursor& C) const
{
	const TArray<FVector2d>& P = Lines[C.Line].P;
	return (P[C.Seg + 1] - P[C.Seg]).GetSafeNormal() * static_cast<double>(C.Dir);
}

bool ASimCruisePreflight::Walk(FCursor& C, double DistanceCm) const
{
	double Left = DistanceCm;
	for (int32 Guard = 0; Guard < 400 && C.IsValid(); ++Guard)
	{
		const TArray<FVector2d>& P = Lines[C.Line].P;
		const double SegLen = FMath::Max(FVector2d::Distance(P[C.Seg], P[C.Seg + 1]), 1.0);
		const double Room = (C.Dir > 0 ? 1.0 - C.T : C.T) * SegLen;
		if (Left <= Room)
		{
			C.T += C.Dir * Left / SegLen;
			return true;
		}
		Left -= Room;
		if (C.Dir > 0 && C.Seg + 2 < P.Num()) { ++C.Seg; C.T = 0.0; continue; }
		if (C.Dir < 0 && C.Seg > 0) { --C.Seg; C.T = 1.0; continue; }
		// The end of this ribbon: go on along the ribbon that starts or ends here, straightest first.
		C.T = C.Dir > 0 ? 1.0 : 0.0;
		const FVector2d End = PointAt(C);
		const FVector2d Heading = TangentAt(C);
		FCursor Next;
		double BestDot = -0.2;
		for (int32 L = 0; L < Lines.Num(); ++L)
		{
			if (L == C.Line) continue;
			const TArray<FVector2d>& Q = Lines[L].P;
			if (FVector2d::Distance(Q[0], End) < 300.0)
			{
				const double Dot = FVector2d::DotProduct((Q[1] - Q[0]).GetSafeNormal(), Heading);
				if (Dot > BestDot) { BestDot = Dot; Next.Line = L; Next.Seg = 0; Next.T = 0.0; Next.Dir = 1; }
			}
			if (FVector2d::Distance(Q.Last(), End) < 300.0)
			{
				const double Dot = FVector2d::DotProduct((Q[Q.Num() - 2] - Q.Last()).GetSafeNormal(), Heading);
				if (Dot > BestDot) { BestDot = Dot; Next.Line = L; Next.Seg = Q.Num() - 2; Next.T = 1.0; Next.Dir = -1; }
			}
		}
		if (!Next.IsValid())
		{
			return false;
		}
		C = Next;
	}
	return C.IsValid();
}

void ASimCruisePreflight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;

	switch (Stage)
	{
	case EStage::WaitReady:
	{
		const bool bShadersBusy = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
		const bool bTiles = !Stream || (Pawn && Stream->AreTilesReadyAround(Pawn->GetActorLocation(), 1));
		const bool bPlaced = Pawn && (!Mode || Mode->IsDriveReady());
		bool bViewDone = true;
		for (TActorIterator<ASimCruiseViewCheck> It(GetWorld()); It; ++It)
		{
			bViewDone &= It->IsFinished();
		}
		// The cockpit steer test first (the car at rest), then the drive.
		for (TActorIterator<ASimCruiseCockpitTest> It(GetWorld()); It; ++It)
		{
			bViewDone &= It->IsFinished();
		}
		if ((bPlaced && bTiles && !bShadersBusy && bViewDone) || Now - Started > ReadyTimeoutS)
		{
			Stage = EStage::Settle;
			StageStarted = Now;
		}
		break;
	}
	case EStage::Settle:
		if (Now - StageStarted > SettleS && Pawn && Stream)
		{
			// What is in the world when the drive starts is not a pop-in.
			const FIntPoint Here = Stream->GetFrame().TileAtWorldCm(Pawn->GetActorLocation());
			for (int32 DX = -3; DX <= 3; ++DX)
			{
				for (int32 DY = -3; DY <= 3; ++DY)
				{
					const ASimCruiseWorldTile* Tile = Stream->GetTile(Here + FIntPoint(DX, DY));
					if (Tile && Tile->GetBuild()) KnownTiles.Add(Here + FIntPoint(DX, DY));
				}
			}
			for (TActorIterator<AActor> It(GetWorld()); It; ++It)
			{
				if (It->IsA<ASimCruiseTrafficCar>() || It->IsA<ASimCruiseCarPresentationActor>()) KnownCars.Add(*It, It->GetActorLocation());
			}
			RebaseStream = Stream;
			RebaseHandle = Stream->OnRebased.AddUObject(this, &ASimCruisePreflight::OnRebased);
			FrameHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ASimCruisePreflight::OnFrame);
			LastYawDeg = Pawn->GetActorRotation().Yaw;
			Stage = EStage::Drive;
			StageStarted = Now;
			Stats.DriveSeconds = 0.f;
		}
		else if (Now - StageStarted > 60.0)
		{
			Finish();
		}
		break;
	case EStage::Drive:
		if (!Player || !Pawn)
		{
			Finish();
			break;
		}
		Stats.DriveSeconds += DeltaSeconds;
		DriveTick(Player, DeltaSeconds);
		if (bForceSpin)
		{
			// The negative test: at speed, a full yank of the stick with the handbrake on for 2 s.
			// The run must FAIL with a spin; it overrides what the robot asked for this frame.
			// It starts once the car is above 12 m/s (or after 25 s) and then holds for 2.5 s whatever the speed.
			const bool bAtSpeed = Player->GetTelemetry().BodyVelocityMps.X > 12.0 || Stats.DriveSeconds > 25.f;
			const bool bYank = (ForceSpinClock > 0.f || bAtSpeed) && ForceSpinClock < 2.5f;
			if (bYank)
			{
				// A handbrake turn alone does not always turn the car round: on the first frame the body
				// also gets a 240 deg/s yaw kick, so the test always has a spin to find.
				if (ForceSpinClock == 0.f)
				{
					if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Pawn->GetRootComponent()))
					{
						Body->SetPhysicsAngularVelocityInDegrees(FVector(0.0, 0.0, 240.0), true);
					}
				}
				ForceSpinClock += DeltaSeconds;
				Stats.bForcedSpin = true;
				Player->InjectAxis(EKeys::Gamepad_LeftX, 1.f);
				Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, 0.f);
				Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, 0.f);
			}
			if (bYank != bForceSpinBrake)
			{
				Player->InjectKey(EKeys::Gamepad_FaceButton_Right, bYank);
				bForceSpinBrake = bYank;
				UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight: forced spin %s at %.0f m"), bYank ? TEXT("starts") : TEXT("ends"), PathM);
			}
		}
		MeasureTick(Player, DeltaSeconds);
		{
			// The real frame time (DeltaSeconds can be clamped); a long one is a stall the player sees.
			const float Ms = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
			FrameMs.Add(Ms);
			// The rules judge the frames after the warm-up that no screenshot of the robot's own touched.
			if (CaptureSkip > 0)
			{
				--CaptureSkip;
			}
			else if (Stats.DriveSeconds > SimCruisePreflightRules::FrameWarmupS)
			{
				TimedFrameMs.Add(Ms);
			}
			if (Ms > 50.f)
			{
				UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight: stall %.0f ms at %.0f m (%.0f km/h)"), Ms, PathM, Player->GetTelemetry().BodyVelocityMps.X * 3.6);
			}
			if (Ms > 100.f && Stats.DriveSeconds > SimCruisePreflightRules::FrameWarmupS && CaptureSkip == 0)
			{
				Review.Event(FString::Printf(TEXT("hitch %.0f ms"), Ms), Stats.DriveSeconds);
			}
		}
		Review.Tick(*Player, Stats.DriveSeconds, DeltaSeconds, Stick);
		FrameClock += DeltaSeconds;
		if (!bNoFrames && FrameClock >= FrameIntervalS && !bFramePending)
		{
			FrameClock = 0.f;
			RequestFrame(Player);
		}
		if (Stats.DriveSeconds >= DriveSeconds)
		{
			Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, 0.f);
			Player->InjectAxis(EKeys::Gamepad_LeftX, 0.f);
			Finish();
		}
		break;
	default:
		break;
	}
}

void ASimCruisePreflight::DriveTick(ASimCruisePlayerController* Player, float DeltaSeconds)
{
	APawn* Pawn = Player->GetPawn();
	const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	if (!Stream || !Pawn)
	{
		return;
	}
	const FVector CarCm = Pawn->GetActorLocation();
	const FVector2d Car(CarCm.X, CarCm.Y);
	const FVector2d Forward = FVector2d(Pawn->GetActorForwardVector().X, Pawn->GetActorForwardVector().Y).GetSafeNormal();
	const double Speed = Player->GetTelemetry().BodyVelocityMps.X;
	const double YawRate = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(LastYawDeg, Pawn->GetActorRotation().Yaw)) / FMath::Max(DeltaSeconds, 1e-3f);
	LastYawDeg = Pawn->GetActorRotation().Yaw;
	GatherLines(Stream, CarCm);
	const FCursor Here = FindCursor(Car, Forward);
	float Throttle = 0.f, Brake = 0.f;
	if (!Here.IsValid())
	{
		// No road near: stop and wait.
		Brake = Speed > 0.5 ? 0.4f : 0.f;
		Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, 0.f);
		Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, Brake);
		Player->InjectAxis(EKeys::Gamepad_LeftX, 0.f);
		return;
	}

	// Steering: pure pursuit to the right lane's centre ahead, as a yaw rate the stick follows.
	const double LookM = FMath::Clamp(6.0 + 0.8 * FMath::Max(Speed, 0.0), 8.0, 30.0);
	FCursor Look = Here;
	Walk(Look, LookM * 100.0);
	const FLine& Line = Lines[Look.Line];
	// The rightmost travel lane as the traffic drives it: inside any parking lane, along the station's R.
	const int32 Station = Look.Dir > 0 ? Look.Seg : Look.Seg + 1;
	const double LaneM = SimCruiseTrafficModel::LaneOffsetM(*Line.Ribbon, Station, Look.Dir);
	const FVector2d Target = PointAt(Look) + Line.Ribbon->St[Station].R * LaneM * 100.0;
	const FVector2d ToTarget = Target - Car;
	const double Alpha = FMath::Atan2(Forward.X * ToTarget.Y - Forward.Y * ToTarget.X, FVector2d::DotProduct(Forward, ToTarget));
	const double Curvature = 2.0 * FMath::Sin(Alpha) / LookM;
	const double YawError = Speed * Curvature - YawRate;
	if (Speed < 1.0)
	{
		SteerIntegral = 0.f;
	}
	else
	{
		SteerIntegral = FMath::Clamp(SteerIntegral + static_cast<float>(1.5 * YawError * DeltaSeconds), -1.f, 1.f);
	}
	// Feed-forward, per car: the road-wheel angle of the curve at this car's wheelbase, over the
	// angle full stick gives now (the shaper's range at this speed, else the car's full lock).
	double WheelbaseM = 2.4;
	double FullLockRad = 0.61;
	if (const ISimCruiseVehicleControl* Control = Cast<ISimCruiseVehicleControl>(Pawn))
	{
		const FSimCruiseVehicleSetup Setup = Control->GetVehicleSetup();
		WheelbaseM = Setup.WheelbaseM;
		FullLockRad = Setup.ParkingRackAngleRad;
	}
	const double WheelRad = FMath::Atan(Curvature * WheelbaseM);
	const double RangeRad = WheelRad >= 0.0 ? Player->GetShaper().GetRangeRightRad() : Player->GetShaper().GetRangeLeftRad();
	const double FeedForward = WheelRad / FMath::Max(FMath::Abs(RangeRad) > 0.01 ? FMath::Abs(RangeRad) : FullLockRad, 0.05);
	Stick = FMath::Clamp(static_cast<float>(FeedForward + SteerIntegral + 0.4 * YawError), -1.f, 1.f);

	// Speed: the road's limit, slower for the curves ahead and for a car ahead.
	// The keen pace from the car's own grip (its published skidpad).
	double Grip = 1.0;
	if (const ASimCruiseVehicle* Driven = Cast<ASimCruiseVehicle>(Player->GetPawn())) Grip = FMath::Clamp(Driven->GetRealCar().TargetSkidpadG, 0.5, 1.3);
	const double Lat = bKeen ? 0.7 * Grip * 9.80665 : LateralAccel;
	// Braking done early, mostly in a straight line, as a quick driver does on a road.
	const double Decel = bKeen ? 0.55 * Grip * 9.80665 : BrakeAccel;
	double TargetMps = bKeen ? 45.0 : FMath::Min(SimCruiseTrafficModel::SpeedLimitMps(Line.Class), MaxSpeedMps);
	if (TargetMps <= 1.0) TargetMps = 13.4;
	if (SpeedOverrideMps > 0.0) TargetMps = SpeedOverrideMps;
	FCursor Ahead = Here;
	FVector2d PrevTangent = TangentAt(Ahead);
	for (int32 Step = 1; Step <= (bKeen ? 40 : 16); ++Step)
	{
		const double S = Step * 5.0;
		if (!Walk(Ahead, 500.0))
		{
			// A dead end: be able to stop before it.
			TargetMps = FMath::Min(TargetMps, FMath::Sqrt(2.0 * Decel * FMath::Max(0.0, S - 10.0)));
			break;
		}
		const FVector2d T = TangentAt(Ahead);
		const double Turn = FMath::Acos(FMath::Clamp(FVector2d::DotProduct(PrevTangent, T), -1.0, 1.0));
		PrevTangent = T;
		const double Kappa = FMath::Max(Turn / 5.0, 1e-4);
		const double CurveMps = FMath::Sqrt(Lat / Kappa);
		TargetMps = FMath::Min(TargetMps, FMath::Sqrt(CurveMps * CurveMps + 2.0 * Decel * FMath::Max(0.0, S - 5.0)));
	}
	// Traffic ahead in this lane (traffic cars do not all block traces): keep a 2 s gap plus 6 m.
	for (TActorIterator<ASimCruiseTrafficCar> It(GetWorld()); It; ++It)
	{
		const FVector2d D = FVector2d(It->GetActorLocation().X, It->GetActorLocation().Y) - Car;
		const double AlongM = FVector2d::DotProduct(D, Forward) / 100.0;
		const double SideM = FMath::Abs(Forward.X * D.Y - Forward.Y * D.X) / 100.0;
		if (AlongM < 1.0 || AlongM > 80.0 || SideM > 2.2) continue;
		const double GapM = AlongM - 4.5;
		const double Wanted = 6.0 + 2.0 * Speed;
		TargetMps = FMath::Min(TargetMps, FMath::Max(0.0, It->Speed + 0.5 * (GapM - Wanted)));
	}
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruisePreflightAhead), false);
		Params.AddIgnoredActor(Pawn);
		const FVector2d D2 = ToTarget.GetSafeNormal();
		const FVector Dir(D2.X, D2.Y, 0.0);
		const FVector From = CarCm + FVector(Forward.X, Forward.Y, 0.0) * 250.0 + FVector(0.0, 0.0, 60.0);
		const double Range = FMath::Max(2000.0, 300.0 * Speed);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, From, From + Dir * Range, ECC_Visibility, Params) && IsCarActor(Hit.GetActor()))
		{
			TargetMps = FMath::Min(TargetMps, FMath::Max(0.0, (Hit.Distance / 100.0 - 6.0) / 1.8));
		}
	}
	const double Error = TargetMps - Speed;
	if (Error > 0.0)
	{
		Throttle = static_cast<float>(FMath::Clamp(0.2 + 0.35 * Error, 0.0, 1.0));
	}
	else if (Error < -0.8 && Speed > 0.5)
	{
		Brake = static_cast<float>(FMath::Clamp(0.2 * -Error, 0.05, 1.0));
	}
	// What a driver does at the limit: no harder braking than the grip the corner leaves (the
	// friction circle), and a lift as soon as the car starts to slide.
	const FVector Body = Player->GetTelemetry().BodyVelocityMps;
	const double LateralG = FMath::Abs(Speed * YawRate) / 9.80665;
	const double SlipDeg = Speed > 3.0 ? FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Body.Y), FMath::Abs(Body.X))) : 0.0;
	if (bKeen)
	{
		const double Used = FMath::Clamp(LateralG / FMath::Max(Grip, 0.5), 0.0, 1.0);
		Brake = FMath::Min(Brake, static_cast<float>(FMath::Sqrt(FMath::Max(0.0, 1.0 - Used * Used))));
		if (SlipDeg > 4.0) Throttle = 0.f;
		else if (Used > 0.6) Throttle = FMath::Min(Throttle, 0.55f);
	}
	TelemetryClock += DeltaSeconds;
	if (TelemetryClock >= 0.25f)
	{
		TelemetryClock = 0.f;
		const FSimCruiseDriveTelemetry& T = Player->GetTelemetry();
		UE_LOG(LogSimCruisePreflight, Display, TEXT("ROBOT t %.2f v %.1f target %.1f stick %+.2f thr %.2f brk %.2f yaw %+.1f deg/s lat %.2f g slip %.1f deg gear %d rpm %.0f"),
			GetWorld()->GetTimeSeconds(), Speed, TargetMps, Stick, Throttle, Brake, FMath::RadiansToDegrees(YawRate), LateralG, SlipDeg, T.Gear, T.EngineSpeedRpm);
	}
	Player->InjectAxis(EKeys::Gamepad_RightTriggerAxis, Throttle);
	Player->InjectAxis(EKeys::Gamepad_LeftTriggerAxis, Brake);
	Player->InjectAxis(EKeys::Gamepad_LeftX, Stick);
}

void ASimCruisePreflight::MeasureTick(ASimCruisePlayerController* Player, float DeltaSeconds)
{
	APawn* Pawn = Player->GetPawn();
	const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	if (!Stream || !Pawn)
	{
		return;
	}
	const FVector CarCm = Pawn->GetActorLocation();
	const FVector Forward = FVector(Pawn->GetActorForwardVector().X, Pawn->GetActorForwardVector().Y, 0.0).GetSafeNormal();
	const double Speed = Player->GetTelemetry().BodyVelocityMps.X;
	if (bHasLastCar)
	{
		PathM += FVector::Dist2D(CarCm, LastCarCm) / 100.0;
	}
	LastCarCm = CarCm;
	bHasLastCar = true;
	SpeedSum += Speed;
	++SpeedSamples;

	// A spin, as the drive robot measures it: the rear axle's slip angle above 5 m/s, and a slide backwards.
	if (const ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(Pawn); Car && Car->GetVehicleMovement())
	{
		FSimCruiseVehicleTelemetry T;
		Car->GetVehicleMovement()->CopyTelemetry(T);
		const FVector V = T.BodyVelocityLocalMps;
		const FVector W = T.BodyAngularVelocityLocalRadPerSecond;
		const FSimCruiseVehicleSetup Setup = Car->GetVehicleSetup();
		const double ToRearAxleM = Setup.WheelbaseM - Setup.CenterOfMassToFrontAxleM;
		if (V.X > 5.0)
		{
			const double RearSlipDeg = FMath::Abs(FMath::RadiansToDegrees(FMath::Atan2(V.Y - W.Z * ToRearAxleM, V.X)));
			if (RearSlipDeg > Stats.PeakRearSlipDeg && RearSlipDeg > SimCruisePreflightRules::MaxRearSlipDeg)
			{
				UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight: rear slip %.0f deg at %.0f m (frame %d)"), RearSlipDeg, PathM, FrameIndex);
				Review.Event(TEXT("spin"), Stats.DriveSeconds);
			}
			Stats.PeakRearSlipDeg = FMath::Max(Stats.PeakRearSlipDeg, static_cast<float>(RearSlipDeg));
		}
		Stats.PeakYawDps = FMath::Max(Stats.PeakYawDps, static_cast<float>(FMath::Abs(FMath::RadiansToDegrees(W.Z))));
		// Backwards counts once the car has driven (a roll-back on a slope at the start is not a spin).
		bHasDriven = bHasDriven || V.X > 5.0;
		ReverseNow = bHasDriven && V.X < -2.0 ? ReverseNow + DeltaSeconds : 0.f;
		Stats.ReversedSeconds = FMath::Max(Stats.ReversedSeconds, ReverseNow);
	}

	// A run-off: how far the car's centre is past the paved edge of the ribbon it drives along
	// (the lines DriveTick gathered this frame); no aligned road within 30 m counts as 30 m.
	if (!Lines.IsEmpty())
	{
		const FVector2d Car2(CarCm.X, CarCm.Y);
		const FCursor Near = FindCursor(Car2, FVector2d(Forward.X, Forward.Y));
		const double PastEdgeM = Near.IsValid() ? FVector2d::Distance(Car2, PointAt(Near)) / 100.0 - Lines[Near.Line].HalfW[Near.Seg] : 30.0;
		Stats.MaxOffRibbonM = FMath::Max(Stats.MaxOffRibbonM, static_cast<float>(PastEdgeM));
		const bool bOff = PastEdgeM > SimCruisePreflightRules::MaxOffRibbonM;
		if (bOff && OffRibbonNow == 0.f)
		{
			UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight: %.1f m past the road edge at %.0f m (frame %d)"), PastEdgeM, PathM, FrameIndex);
			Review.Event(TEXT("run-off"), Stats.DriveSeconds);
		}
		OffRibbonNow = bOff ? OffRibbonNow + DeltaSeconds : 0.f;
		Stats.OffRibbonSeconds = FMath::Max(Stats.OffRibbonSeconds, OffRibbonNow);
	}

	// The road surface: the tile's own collision under a point, or a bridge deck above it.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruisePreflightGround), true);
	Params.bReturnFaceIndex = true;
	Params.AddIgnoredActor(Pawn);
	auto SurfaceAt = [&](const FVector& At, double& OutZm) -> bool
	{
		const FVector From = At + FVector(0.0, 0.0, 300.0);
		const FVector To = At - FVector(0.0, 0.0, 800.0);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params) && Hit.GetActor() && Hit.GetActor()->IsA<ASimCruiseBridges>())
		{
			OutZm = Hit.ImpactPoint.Z / 100.0;
			return true;
		}
		// The tile under the point, and its neighbours: within a few centimetres of a tile edge the
		// point's own tile can miss while the road there is the neighbour's mesh (it read as a
		// 1.3 m "gap" at a border on CA-1 in the v2.0.2 pre-flight). Take the highest hit.
		const FIntPoint Home = Stream->GetFrame().TileAtWorldCm(At);
		bool bAny = false, bRoad = false;
		double Best = TNumericLimits<double>::Lowest();
		for (int32 DY = -1; DY <= 1; ++DY)
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			const ASimCruiseWorldTile* Tile = Stream->GetTile(Home + FIntPoint(DX, DY));
			if (!Tile || !Tile->Mesh) continue;
			FHitResult TileHit;
			if (!Tile->Mesh->LineTraceComponent(TileHit, From, To, Params)) continue;
			if (TileHit.ImpactPoint.Z / 100.0 <= Best) continue;
			Best = TileHit.ImpactPoint.Z / 100.0;
			bAny = true;
			bRoad = IsRoadLayer(Tile->LayerAtFace(TileHit.FaceIndex));
		}
		if (!bAny) return false;
		OutZm = Best;
		return bRoad;
	};
	// The surface under the car's centre: the path the wheels ride. A point straight ahead
	// leaves the paved edge in a hairpin and reads as a gap that is not there.
	double UnderZm = 0.0;
	const bool bUnderRoad = SurfaceAt(CarCm, UnderZm);
	Surface.Add(FVector2f(static_cast<float>(PathM), static_cast<float>(UnderZm)));
	SurfaceIsRoad.Add(bUnderRoad ? 1 : 0);
	OffRoadNow = bUnderRoad ? 0.f : OffRoadNow + DeltaSeconds;
	Stats.OffRoadSeconds = FMath::Max(Stats.OffRoadSeconds, OffRoadNow);

	// Jolts: vertical acceleration of the body, from its velocity.
	const double Vz = Player->GetTelemetry().BodyVelocityMps.Z;
	if (bHasLastVz && DeltaSeconds > 1e-3f && DeltaSeconds < 0.1f && JoltCooldown <= 0.f)
	{
		const double Az = (Vz - LastVz) / DeltaSeconds;
		if (FMath::Abs(Az) > SimCruisePreflightRules::JoltMps2)
		{
			++Stats.Jolts;
			JoltCooldown = 0.3f;
			const FSimCruiseWorldFrame& Frame = Stream->GetFrame();
			const bool bBorder = Frame.TileAtWorldCm(CarCm + Forward * 800.0) != Frame.TileAtWorldCm(CarCm - Forward * 800.0);
			Stats.BorderJolts += bBorder ? 1 : 0;
			UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight jolt %.1f m/s^2 at %.0f m%s (frame %d)"), Az, PathM, bBorder ? TEXT(" at a tile border") : TEXT(""), FrameIndex);
			Review.Event(TEXT("jolt"), Stats.DriveSeconds);
		}
	}
	JoltCooldown -= DeltaSeconds;
	LastVz = Vz;
	bHasLastVz = true;

	// Pop-in and stuck traffic, ten times a second.
	PopClock += DeltaSeconds;
	if (PopClock < 0.1f)
	{
		return;
	}
	PopClock = 0.f;
	const FVector Eye = Player->PlayerCameraManager ? Player->PlayerCameraManager->GetCameraLocation() : CarCm;
	const FVector LookDir = Player->PlayerCameraManager ? Player->PlayerCameraManager->GetCameraRotation().Vector() : Forward;
	const double RangeCm = SimCruisePreflightRules::PopInRangeM * 100.0;
	const FSimCruiseWorldFrame& Frame = Stream->GetFrame();
	const FIntPoint Here = Frame.TileAtWorldCm(CarCm);
	for (int32 DX = -2; DX <= 2; ++DX)
	{
		for (int32 DY = -2; DY <= 2; ++DY)
		{
			const FIntPoint Id = Here + FIntPoint(DX, DY);
			const ASimCruiseWorldTile* Tile = Stream->GetTile(Id);
			if (!Tile || !Tile->GetBuild() || KnownTiles.Contains(Id)) continue;
			KnownTiles.Add(Id);
			Review.Event(FString::Printf(TEXT("tile built %d_%d"), Id.X, Id.Y), Stats.DriveSeconds);
			const FVector A = Frame.TileOriginCm(Id);
			const FVector B = Frame.TileOriginCm(Id + FIntPoint(1, 1));
			const FBox2D Box(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
			const double Dist = FMath::Sqrt(Box.ComputeSquaredDistanceToPoint(FVector2D(Eye.X, Eye.Y)));
			if (Dist < RangeCm)
			{
				++Stats.TilePopIns;
				Review.Event(TEXT("tile pop-in"), Stats.DriveSeconds);
				UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight pop-in: tile %d_%d built %.0f m from the camera (frame %d)"), Id.X, Id.Y, Dist / 100.0, FrameIndex);
			}
		}
	}
	FCollisionQueryParams SightParams(SCENE_QUERY_STAT(SimCruisePreflightSight), false);
	SightParams.AddIgnoredActor(Pawn);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Car = *It;
		ASimCruiseTrafficCar* Traffic = Cast<ASimCruiseTrafficCar>(Car);
		if (!Traffic && !Car->IsA<ASimCruiseCarPresentationActor>()) continue;
		if (Traffic && Traffic->StandS > 20.0 && Traffic->StopState != 2 && Traffic->Why != 3 && !StuckCars.Contains(Car))
		{
			StuckCars.Add(Car);
			UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight: traffic car %s stuck %.0f s (reason %d) %.0f m away (frame %d)"), *Car->GetName(), Traffic->StandS, Traffic->Why, FVector::Dist(Car->GetActorLocation(), CarCm) / 100.0, FrameIndex);
		}
		const FVector Pos = Car->GetActorLocation();
		const FVector* Known = KnownCars.Find(Car);
		const bool bAppeared = !Known || FVector::Dist(*Known, Pos) > 3000.0;
		KnownCars.Add(Car, Pos);
		if (!bAppeared) continue;
		const FVector To = Pos + FVector(0.0, 0.0, 80.0) - Eye;
		const double Dist = To.Size();
		if (Dist > RangeCm || FVector::DotProduct(To / FMath::Max(Dist, 1.0), LookDir) < FMath::Cos(FMath::DegreesToRadians(55.0))) continue;
		FCollisionQueryParams Sight = SightParams;
		Sight.AddIgnoredActor(Car);
		FHitResult Block;
		if (GetWorld()->LineTraceSingleByChannel(Block, Eye, Eye + To * 0.95, ECC_Visibility, Sight) && !IsCarActor(Block.GetActor())) continue;
		++Stats.CarPopIns;
		Review.Event(TEXT("car pop-in"), Stats.DriveSeconds);
		UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight pop-in: car %s appeared %.0f m in front of the camera (frame %d)"), *Car->GetName(), Dist / 100.0, FrameIndex);
	}
}

void ASimCruisePreflight::RequestFrame(APlayerController* Player)
{
	// Where the sky is: view rays that hit nothing (the car itself counts as a hit: its roof is not sky).
	PendingSky.Reset();
	int32 SizeX = 0, SizeY = 0;
	Player->GetViewportSize(SizeX, SizeY);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruisePreflightSky), false);
	for (int32 J = 0; J < SkyGridY; ++J)
	{
		for (int32 I = 0; I < SkyGridX; ++I)
		{
			const FVector2D Screen((I + 0.5) / SkyGridX * SizeX, (J + 0.5) / SkyGridY * SizeY);
			FVector Origin, Direction;
			if (!UGameplayStatics::DeprojectScreenToWorld(Player, Screen, Origin, Direction) || Direction.Z <= 0.0) continue;
			FHitResult Hit;
			if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * 200000.0, ECC_Visibility, Params))
			{
				PendingSky.Add(FVector2D(Screen.X / FMath::Max(SizeX, 1), Screen.Y / FMath::Max(SizeY, 1)));
			}
		}
	}
	PendingRotation = Player->PlayerCameraManager ? Player->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
	bFramePending = true;
	// The read-back and the JPEG slow the next frames: the frame-time rules do not judge them.
	CaptureSkip = FMath::Max(CaptureSkip, 2);
	if (Review.WantsShot())
	{
		if (ASimCruisePlayerController* SimPlayer = Cast<ASimCruisePlayerController>(Player))
		{
			Review.PrepareShot(*SimPlayer, Stats.DriveSeconds);
		}
	}
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputDir, TEXT("frames"), TEXT("request.png")), true, false);
}

void ASimCruisePreflight::OnFrame(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (!bFramePending || Width < ThumbW || Height < ThumbH || Pixels.Num() < Width * Height)
	{
		bFramePending = false;
		return;
	}
	bFramePending = false;
	CaptureSkip = FMath::Max(CaptureSkip, 1);
	// The JPEG on a worker thread: on the game thread it stalled the frame a person watches.
	{
		TArray<FColor> Copy = Pixels;
		const FString JpegPath = FPaths::Combine(OutputDir, TEXT("frames"), FString::Printf(TEXT("%05d.jpg"), FrameIndex));
		JpegJobs.Add(Async(EAsyncExecution::ThreadPool, [Copy = MoveTemp(Copy), Width, Height, JpegPath]()
		{
			TArray64<uint8> Jpeg;
			if (FImageUtils::CompressImage(Jpeg, TEXT("jpg"), FImageView(Copy.GetData(), Width, Height), 85))
			{
				FFileHelper::SaveArrayToFile(Jpeg, *JpegPath);
			}
		}));
		JpegJobs.RemoveAll([](const TFuture<void>& Job) { return Job.IsReady(); });
	}
	Review.OnFrame(Width, Height, Pixels);
	++FrameIndex;
	Stats.Frames = FrameIndex;

	// A small luminance picture for the change between frames (four pixels a cell).
	TArray<float> Thumb;
	Thumb.SetNumZeroed(ThumbW * ThumbH);
	for (int32 TY = 0; TY < ThumbH; ++TY)
	{
		for (int32 TX = 0; TX < ThumbW; ++TX)
		{
			const int32 X0 = TX * Width / ThumbW, Y0 = TY * Height / ThumbH;
			const int32 X1 = FMath::Min(X0 + Width / ThumbW / 2, Width - 1), Y1 = FMath::Min(Y0 + Height / ThumbH / 2, Height - 1);
			Thumb[TY * ThumbW + TX] = 0.25f * (Luma(Pixels[Y0 * Width + X0]) + Luma(Pixels[Y0 * Width + X1]) + Luma(Pixels[Y1 * Width + X0]) + Luma(Pixels[Y1 * Width + X1]));
		}
	}
	if (LastThumb.Num() == Thumb.Num())
	{
		// The share (%) of the picture that changed by more than 40 of 255: a flash or an exposure
		// jump changes most of it at once; a car passing close beside a still camera does not.
		int32 Changed = 0;
		for (int32 K = 0; K < Thumb.Num(); ++K) Changed += FMath::Abs(Thumb[K] - LastThumb[K]) > 40.f ? 1 : 0;
		FrameDiffs.Add(100.f * Changed / Thumb.Num());
		const FRotator Delta = (PendingRotation - LastFrameRotation).GetNormalized();
		FrameTurning.Add(FMath::Abs(Delta.Yaw) > 6.0 || FMath::Abs(Delta.Pitch) > 4.0);
	}
	LastThumb = MoveTemp(Thumb);
	LastFrameRotation = PendingRotation;

	// White sky: the mean of 3 x 3 pixels round each sky ray.
	for (const FVector2D& UV : PendingSky)
	{
		const int32 CX = FMath::Clamp(FMath::RoundToInt32(UV.X * Width), 1, Width - 2);
		const int32 CY = FMath::Clamp(FMath::RoundToInt32(UV.Y * Height), 1, Height - 2);
		float L = 0.f;
		for (int32 DY = -1; DY <= 1; ++DY)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				L += Luma(Pixels[(CY + DY) * Width + CX + DX]);
			}
		}
		SkyWhite += L / 9.f >= SimCruisePreflightRules::WhiteLuma ? 1 : 0;
		++Stats.SkySamples;
	}
}

void ASimCruisePreflight::Finish()
{
	// Every frame on disk before the script makes the video.
	for (TFuture<void>& Job : JpegJobs) Job.Wait();
	JpegJobs.Reset();
	if (Stage == EStage::Done)
	{
		return;
	}
	Stage = EStage::Done;
	if (FrameHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(FrameHandle);
		FrameHandle.Reset();
	}
	using namespace SimCruisePreflightRules;
	Stats.DistanceM = static_cast<float>(PathM);
	Stats.MeanSpeedMps = SpeedSamples > 0 ? static_cast<float>(SpeedSum / SpeedSamples) : 0.f;
	Stats.SkyWhitePercent = Stats.SkySamples > 0 ? 100.f * SkyWhite / Stats.SkySamples : 0.f;
	Stats.FlashFrames = CountFlashes(FrameDiffs, FrameTurning, Stats.MedianFrameDiff);
	Stats.StuckTraffic = StuckCars.Num();

	// Steps along the road samples only; a gap is a short run of samples off the road between road samples.
	TArray<FVector2f> RoadOnly;
	for (int32 I = 0; I < Surface.Num(); ++I)
	{
		if (SurfaceIsRoad[I]) RoadOnly.Add(Surface[I]);
	}
	Stats.RoadSteps = CountSteps(RoadOnly, Stats.MaxStepCm);
	for (int32 I = 1; I < Surface.Num(); ++I)
	{
		if (SurfaceIsRoad[I] || !SurfaceIsRoad[I - 1]) continue;
		int32 J = I;
		while (J < Surface.Num() && !SurfaceIsRoad[J]) ++J;
		if (J < Surface.Num() && Surface[J].X - Surface[I - 1].X < 1.5f)
		{
			++Stats.RoadGaps;
			UE_LOG(LogSimCruisePreflight, Display, TEXT("Pre-flight: no road surface for %.2f m at %.0f m"), Surface[J].X - Surface[I - 1].X, Surface[I].X);
		}
		I = J;
	}

	const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	FString Place;
	if (Mode)
	{
		Place = Mode->GetPlace().Title;
		if (bRequestedStart)
		{
			// About 111 km a degree; a start 3 km off the asked point is another place.
			const double DLat = (Mode->GetPlace().Lat - RequestedLat) * 111.0;
			const double DLon = (Mode->GetPlace().Lon - RequestedLon) * 111.0 * FMath::Cos(FMath::DegreesToRadians(RequestedLat));
			Stats.bFellBack = FMath::Sqrt(DLat * DLat + DLon * DLon) > 3.0;
		}
	}
	for (TActorIterator<ASimCruiseViewCheck> It(GetWorld()); It; ++It)
	{
		Stats.bViewCheckRan = It->IsFinished();
		Stats.bViewCheckPassed = It->HasPassed();
		Stats.ViewCheckReasons = It->GetReasons();
	}
	if (const USimCruiseEnvironmentSubsystem* Environments = USimCruiseEnvironmentSubsystem::Get(GetWorld()))
	{
		if (const ASimCruiseEnvironment* Environment = Environments->GetEnvironment())
		{
			Stats.ReferenceEV100 = Environment->GetReferenceEV100();
		}
	}

	// Frame times: the median, the 1 % worst, the worst, and how many frames a player sees as a hitch.
	FString FrameLine = TEXT("none");
	if (FrameMs.Num() > 0)
	{
		TArray<float> Sorted = FrameMs;
		Sorted.Sort();
		auto Pct = [&](double Q) { return Sorted[FMath::Clamp(FMath::FloorToInt32(Q * (Sorted.Num() - 1)), 0, Sorted.Num() - 1)]; };
		int32 Over33 = 0, Over50 = 0, Over100 = 0;
		for (float Ms : FrameMs) { Over33 += Ms > 33.4f; Over50 += Ms > 50.f; Over100 += Ms > 100.f; }
		FrameLine = FString::Printf(TEXT("frames=%d p50=%.1fms p99=%.1fms worst=%.0fms over33=%d over50=%d over100=%d"),
			FrameMs.Num(), Pct(0.5), Pct(0.99), Sorted.Last(), Over33, Over50, Over100);
	}
	UE_LOG(LogSimCruisePreflight, Display, TEXT("PREFLIGHT FRAMES road=%s car=%s mean=%.1fm/s %s"), *RoadName, *CarId, Stats.MeanSpeedMps, *FrameLine);

	MeasureFrames(TimedFrameMs, Stats);
	const FString Reasons = Judge(Stats);
	const bool bPass = Reasons.IsEmpty();
	Review.Finish(bPass ? TEXT("PASS") : TEXT("FAIL"), Reasons);
	UE_LOG(LogSimCruisePreflight, Display,
		TEXT("PREFLIGHT %s road=%s car=%s place=\"%s\" distance=%.0fm mean=%.1fm/s offroad=%.1fs rear_slip=%.1fdeg backwards=%.1fs past_edge=%.1fm/%.1fs frame_p95=%.1fms hitches=%d sky_white=%.0f%% tile_popin=%d car_popin=%d flashes=%d steps=%d max_step=%.1fcm gaps=%d jolts=%d border_jolts=%d stuck=%d frames=%d reasons=%s"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *RoadName, *CarId, *Place, Stats.DistanceM, Stats.MeanSpeedMps, Stats.OffRoadSeconds,
		Stats.PeakRearSlipDeg, Stats.ReversedSeconds, Stats.MaxOffRibbonM, Stats.OffRibbonSeconds, Stats.FrameP95Ms, Stats.Hitches, Stats.SkyWhitePercent,
		Stats.TilePopIns, Stats.CarPopIns, Stats.FlashFrames, Stats.RoadSteps, Stats.MaxStepCm, Stats.RoadGaps, Stats.Jolts, Stats.BorderJolts,
		Stats.StuckTraffic, Stats.Frames, bPass ? TEXT("ok") : *Reasons);

	auto B = [](bool bValue) { return bValue ? TEXT("true") : TEXT("false"); };
	const FString Json = FString::Printf(TEXT("{\n")
		TEXT("  \"result\": \"%s\", \"road\": \"%s\", \"car\": \"%s\", \"place\": \"%s\",\n")
		TEXT("  \"drive_seconds\": %.1f, \"distance_m\": %.1f, \"mean_speed_mps\": %.2f, \"offroad_seconds\": %.2f, \"fell_back\": %s,\n")
		TEXT("  \"viewcheck_ran\": %s, \"viewcheck_passed\": %s, \"viewcheck_reasons\": \"%s\",\n")
		TEXT("  \"sky_samples\": %d, \"sky_white_percent\": %.1f, \"reference_ev100\": %.2f,\n")
		TEXT("  \"tile_popins\": %d, \"car_popins\": %d, \"flash_frames\": %d, \"median_frame_diff\": %.2f,\n")
		TEXT("  \"road_steps\": %d, \"max_step_cm\": %.1f, \"road_gaps\": %d, \"jolts\": %d, \"border_jolts\": %d,\n")
		TEXT("  \"stuck_traffic\": %d, \"frames\": %d,\n")
		TEXT("  \"peak_rear_slip_deg\": %.1f, \"peak_yaw_dps\": %.1f, \"backwards_seconds\": %.2f, \"forced_spin\": %s,\n")
		TEXT("  \"max_past_edge_m\": %.2f, \"past_edge_seconds\": %.2f,\n")
		TEXT("  \"timed_frames\": %d, \"frame_p95_ms\": %.1f, \"frames_over_50ms\": %d, \"hitches_over_100ms\": %d,\n")
		TEXT("  \"reasons\": \"%s\"\n}\n"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *RoadName, *CarId, *Place.ReplaceCharWithEscapedChar(),
		Stats.DriveSeconds, Stats.DistanceM, Stats.MeanSpeedMps, Stats.OffRoadSeconds, B(Stats.bFellBack),
		B(Stats.bViewCheckRan), B(Stats.bViewCheckPassed), *Stats.ViewCheckReasons,
		Stats.SkySamples, Stats.SkyWhitePercent, Stats.ReferenceEV100,
		Stats.TilePopIns, Stats.CarPopIns, Stats.FlashFrames, Stats.MedianFrameDiff,
		Stats.RoadSteps, Stats.MaxStepCm, Stats.RoadGaps, Stats.Jolts, Stats.BorderJolts,
		Stats.StuckTraffic, Stats.Frames,
		Stats.PeakRearSlipDeg, Stats.PeakYawDps, Stats.ReversedSeconds, B(Stats.bForcedSpin),
		Stats.MaxOffRibbonM, Stats.OffRibbonSeconds,
		Stats.TimedFrames, Stats.FrameP95Ms, Stats.SlowFrames, Stats.Hitches,
		bPass ? TEXT("ok") : *Reasons);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDir, TEXT("preflight.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseKeepOpen")))
	{
		return;
	}
	if (APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0))
	{
		Player->ConsoleCommand(TEXT("quit"));
	}
	else
	{
		FPlatformMisc::RequestExit(false);
	}
}
