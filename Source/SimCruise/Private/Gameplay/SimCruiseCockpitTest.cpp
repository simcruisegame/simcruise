// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseCockpitTest.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/SimCruiseDriveTest.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Gameplay/SimCruiseViewCheck.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "Rendering/PositionVertexBuffer.h"
#include "StaticMeshResources.h"
#include "UnrealClient.h"
#include "Vehicle/SimCruiseVehicle.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseCockpit, Log, All);

namespace
{
	constexpr float SettleS = 3.f;
	constexpr float HoldS = 2.f;
	constexpr double ReadyTimeoutS = 400.0;

	struct FPoseSpec { const TCHAR* Name; float Stick; };
	// + is right on the stick.
	const FPoseSpec PoseSpecs[] = {
		{TEXT("centre"), 0.f}, {TEXT("half-left"), -0.5f}, {TEXT("full-left"), -1.f}, {TEXT("centre-2"), 0.f},
		{TEXT("half-right"), 0.5f}, {TEXT("full-right"), 1.f}, {TEXT("centre-3"), 0.f},
	};

	bool IsCockpitPart(const FString& Name)
	{
		return Name.StartsWith(TEXT("steering_wheel")) || Name.StartsWith(TEXT("dial_")) || Name.StartsWith(TEXT("body"));
	}
}

namespace SimCruiseCockpitRules
{
	double OffAxisDeg(const FQuat& Rotation, const FVector& Axis)
	{
		FVector RotAxis;
		double Angle = 0.0;
		Rotation.GetNormalized().ToAxisAndAngle(RotAxis, Angle);
		Angle = FMath::Abs(FMath::UnwindRadians(Angle));
		if (Angle < 1e-9) return 0.0;
		const double Along = FMath::Clamp(FMath::Abs(FVector::DotProduct(RotAxis.GetSafeNormal(), Axis.GetSafeNormal())), 0.0, 1.0);
		// The part of the turn that is not about Axis.
		return FMath::RadiansToDegrees(Angle * FMath::Sqrt(FMath::Max(0.0, 1.0 - Along * Along)));
	}

	FVector PlaneNormal(const TArray<FVector>& Points)
	{
		if (Points.Num() < 3) return FVector::ZeroVector;
		FVector Mean = FVector::ZeroVector;
		for (const FVector& P : Points) Mean += P;
		Mean /= Points.Num();
		double C[3][3] = {};
		for (const FVector& P : Points)
		{
			const FVector D = P - Mean;
			const double V[3] = {D.X, D.Y, D.Z};
			for (int32 I = 0; I < 3; ++I) for (int32 J = 0; J < 3; ++J) C[I][J] += V[I] * V[J];
		}
		// The smallest eigenvector of C: the largest of (trace x I - C), by power iteration.
		const double Tr = C[0][0] + C[1][1] + C[2][2];
		double M[3][3];
		for (int32 I = 0; I < 3; ++I) for (int32 J = 0; J < 3; ++J) M[I][J] = (I == J ? Tr : 0.0) - C[I][J];
		FVector N(0.577, 0.577, 0.577);
		for (int32 It = 0; It < 64; ++It)
		{
			const FVector Next(M[0][0] * N.X + M[0][1] * N.Y + M[0][2] * N.Z, M[1][0] * N.X + M[1][1] * N.Y + M[1][2] * N.Z, M[2][0] * N.X + M[2][1] * N.Y + M[2][2] * N.Z);
			if (Next.IsNearlyZero()) break;
			N = Next.GetSafeNormal();
		}
		return N;
	}
}

ASimCruiseCockpitTest::ASimCruiseCockpitTest()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseCockpitTest::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* Cmd = FCommandLine::Get();
	bQuitWhenDone = !FParse::Param(Cmd, TEXT("SimCruisePreflight")) && !FParse::Param(Cmd, TEXT("SimCruiseKeepOpen"));
	FString PreflightOut;
	if (!FParse::Value(Cmd, TEXT("SimCruiseCockpitOut="), OutputDir))
	{
		OutputDir = FParse::Value(Cmd, TEXT("SimCruisePreflightOut="), PreflightOut) ? FPaths::Combine(PreflightOut, TEXT("cockpit"))
			: FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Review"), FString::Printf(TEXT("cockpit-%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
	}
	OutputDir = FPaths::ConvertRelativePathToFull(OutputDir);
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	Started = FPlatformTime::Seconds();
	// A fixed camera: no head motion.
	if (IConsoleVariable* Head = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Camera.HeadMotion"))) Head->Set(0.f, ECVF_SetByCode);
	UE_LOG(LogSimCruiseCockpit, Display, TEXT("Cockpit steer test -> %s"), *OutputDir);
}

void ASimCruiseCockpitTest::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ShotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ShotHandle);
		ShotHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

USimCruiseCarPresentationComponent* ASimCruiseCockpitTest::GetModel(ASimCruisePlayerController* Player) const
{
	ASimCruiseVehicle* Car = Player ? Cast<ASimCruiseVehicle>(Player->GetPawn()) : nullptr;
	return Car ? Car->GetCarModel() : nullptr;
}

UStaticMeshComponent* ASimCruiseCockpitTest::FindPart(USimCruiseCarPresentationComponent* Model, const FString& Prefix) const
{
	TArray<USceneComponent*> ModelParts;
	if (Model) Model->GetChildrenComponents(true, ModelParts);
	for (USceneComponent* C : ModelParts)
	{
		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(C);
		if (Mesh && Mesh->GetName().StartsWith(Prefix) && !Mesh->GetName().Contains(TEXT("_glass"))) return Mesh;
	}
	return nullptr;
}

void ASimCruiseCockpitTest::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	auto Next = [this, Now](EStage S) { Stage = S; StageStarted = Now; };
	switch (Stage)
	{
	case EStage::WaitReady:
	{
		bool bViewDone = true;
		for (TActorIterator<ASimCruiseViewCheck> It(GetWorld()); It; ++It) bViewDone &= It->IsFinished();
		if (Pawn && SimCruiseDriveRobot::IsReadyToDrive(GetWorld(), Pawn) && bViewDone && GetModel(Player))
		{
			Player->InjectKey(EKeys::Gamepad_FaceButton_Right, true);
			if (ASimCruiseVehicle* Car = Cast<ASimCruiseVehicle>(Pawn))
			{
				CarId = Car->GetRealCar().Id;
				FullLockRoadWheelDeg = FMath::RadiansToDegrees(Car->GetVehicleSetup().ParkingRackAngleRad);
				Ratio = GetModel(Player)->GetSteeringRatio();
			}
			Next(EStage::Settle);
		}
		else if (Now - Started > ReadyTimeoutS)
		{
			UE_LOG(LogSimCruiseCockpit, Display, TEXT("COCKPIT FAIL car=- reasons=the-car-was-not-ready-in-%.0fs"), ReadyTimeoutS);
			Stage = EStage::Done;
		}
		break;
	}
	case EStage::Settle:
		if (Player) Player->InjectAxis(EKeys::Gamepad_LeftX, 0.f);
		if (Now - StageStarted > SettleS) { PoseIndex = 0; Next(EStage::Hold); }
		break;
	case EStage::Hold:
		if (Player) Player->InjectAxis(EKeys::Gamepad_LeftX, PoseSpecs[PoseIndex].Stick);
		if (Now - StageStarted > HoldS && Player)
		{
			FPose& Pose = Poses.AddDefaulted_GetRef();
			Pose.Name = PoseSpecs[PoseIndex].Name;
			Pose.Stick = PoseSpecs[PoseIndex].Stick;
			Record(*Player, Pose);
			RequestShot();
			Next(EStage::ShotWheel);
		}
		break;
	case EStage::ShotWheel:
	case EStage::ShotNoWheel:
		if (Player) Player->InjectAxis(EKeys::Gamepad_LeftX, PoseSpecs[PoseIndex].Stick);
		// The wheel hidden for three frames, then the second picture.
		if (Stage == EStage::ShotNoWheel && !bShotPending && ++NoWheelFrames == 3)
		{
			RequestShot();
		}
		if (Now - StageStarted > 10.0)
		{
			// No picture came: go on without it (the image check then has nothing to judge).
			bShotPending = false;
			if (UStaticMeshComponent* Wheel = FindPart(GetModel(Player), TEXT("steering_wheel"))) Wheel->SetVisibility(true);
			if (++PoseIndex >= UE_ARRAY_COUNT(PoseSpecs)) { Judge(); Stage = EStage::Done; } else { Next(EStage::Hold); }
		}
		break;
	default:
		break;
	}
	if (Stage == EStage::Done && Player)
	{
		Player->InjectAxis(EKeys::Gamepad_LeftX, 0.f);
		Player->InjectKey(EKeys::Gamepad_FaceButton_Right, false);
		SetActorTickEnabled(false);
		if (bQuitWhenDone) Player->ConsoleCommand(TEXT("quit"));
	}
}

void ASimCruiseCockpitTest::Record(ASimCruisePlayerController& Player, FPose& Pose)
{
	USimCruiseCarPresentationComponent* Model = GetModel(&Player);
	if (!Model) return;
	Pose.RimDeg = FMath::RadiansToDegrees(Model->GetSteeringWheelAngleRad());
	Pose.WheelDeg = FMath::RadiansToDegrees(0.5 * (Model->GetDrawnSteerRad(0) + Model->GetDrawnSteerRad(1)));
	Pose.SpeedShown = FMath::IsFinite(Model->GetDialValue(TEXT("speed_mph"))) ? Model->GetDialValue(TEXT("speed_mph")) : Model->GetDialValue(TEXT("speed_kmh"));
	Pose.RpmShown = Model->GetDialValue(TEXT("rpm"));
	TArray<USceneComponent*> ModelParts;
	Model->GetChildrenComponents(true, ModelParts);
	FBox2D Cluster(ForceInit);
	for (USceneComponent* C : ModelParts)
	{
		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(C);
		if (!Mesh || !IsCockpitPart(Mesh->GetName())) continue;
		FPartPose& Part = Pose.Parts.AddDefaulted_GetRef();
		Part.Name = Mesh->GetName();
		// In the car's own frame: the car may creep or settle a little between poses, and that is not the cockpit moving.
		Part.World = Mesh->GetComponentTransform().GetRelativeTransform(Model->GetComponentTransform());
		Part.BoundsCentre = Mesh->GetStaticMesh() ? Part.World.TransformPosition(Mesh->GetStaticMesh()->GetBounds().Origin) : Part.World.GetLocation();
		for (const USceneComponent* P = Mesh->GetAttachParent(); P && P != Model; P = P->GetAttachParent()) Part.Parent += P->GetName() + TEXT("/");
		// The cluster on screen: the dial faces' and needles' boxes.
		if (Part.Name.StartsWith(TEXT("dial_")))
		{
			const FBox B = Mesh->Bounds.GetBox();
			for (int32 K = 0; K < 8; ++K)
			{
				const FVector Corner((K & 1) ? B.Max.X : B.Min.X, (K & 2) ? B.Max.Y : B.Min.Y, (K & 4) ? B.Max.Z : B.Min.Z);
				FVector2D S;
				if (Player.ProjectWorldLocationToScreen(Corner, S)) Cluster += S;
			}
		}
	}
	int32 W = 0, H = 0;
	Player.GetViewportSize(W, H);
	if (!Cluster.bIsValid)
	{
		// No live dials (a digital cluster or the model's own): the area behind the wheel's upper half.
		if (UStaticMeshComponent* Wheel = FindPart(Model, TEXT("steering_wheel")))
		{
			FVector2D C;
			if (Player.ProjectWorldLocationToScreen(Wheel->Bounds.Origin, C))
			{
				const double R = 0.12 * H;
				Cluster = FBox2D(C - FVector2D(1.3 * R, 1.6 * R), C + FVector2D(1.3 * R, 0.1 * R));
			}
		}
	}
	if (Cluster.bIsValid)
	{
		Pose.Cluster = FIntRect(FMath::Clamp(FMath::FloorToInt32(Cluster.Min.X) - 8, 0, W), FMath::Clamp(FMath::FloorToInt32(Cluster.Min.Y) - 8, 0, H),
			FMath::Clamp(FMath::CeilToInt32(Cluster.Max.X) + 8, 0, W), FMath::Clamp(FMath::CeilToInt32(Cluster.Max.Y) + 8, 0, H));
	}
}

void ASimCruiseCockpitTest::RequestShot()
{
	if (!ShotHandle.IsValid())
	{
		ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ASimCruiseCockpitTest::OnShot);
	}
	bShotPending = true;
	FScreenshotRequest::RequestScreenshot(FPaths::Combine(OutputDir, TEXT("request.png")), false, false);
}

void ASimCruiseCockpitTest::OnShot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (!bShotPending || Poses.IsEmpty()) return;
	bShotPending = false;
	FPose& Pose = Poses.Last();
	ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	UStaticMeshComponent* Wheel = FindPart(GetModel(Player), TEXT("steering_wheel"));
	Pose.Width = Width;
	Pose.Height = Height;
	if (Stage == EStage::ShotWheel)
	{
		Pose.Pixels = Pixels;
		SavePng(Pose.Name, Width, Height, Pixels);
		// The same pose without the steering wheel: what stays is the cluster behind it. The request
		// waits for the next frames (one made inside this callback is dropped by the engine).
		if (Wheel)
		{
			Wheel->SetVisibility(false);
			NoWheelFrames = 0;
			Stage = EStage::ShotNoWheel;
			StageStarted = FPlatformTime::Seconds();
			return;
		}
	}
	else
	{
		Pose.PixelsNoWheel = Pixels;
		SavePng(Pose.Name + TEXT("-nowheel"), Width, Height, Pixels);
		if (Wheel) Wheel->SetVisibility(true);
	}
	if (++PoseIndex >= UE_ARRAY_COUNT(PoseSpecs))
	{
		Judge();
		Stage = EStage::Done;
	}
	else
	{
		Stage = EStage::Hold;
		StageStarted = FPlatformTime::Seconds();
	}
}

void ASimCruiseCockpitTest::SavePng(const FString& Name, int32 Width, int32 Height, const TArray<FColor>& Pixels) const
{
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	FFileHelper::SaveArrayToFile(Png, *FPaths::Combine(OutputDir, Name + TEXT(".png")));
}

void ASimCruiseCockpitTest::Judge()
{
	using namespace SimCruiseCockpitRules;
	TArray<FString> Reasons;
	if (Poses.Num() < 2)
	{
		Reasons.Add(TEXT("no-poses"));
	}
	const FPose* Centre = Poses.Num() ? &Poses[0] : nullptr;
	double MaxFixedMove = 0.0, MaxFixedTurn = 0.0, MaxWheelOffAxis = 0.0, MaxHubMove = 0.0, MaxNeedleTurn = 0.0;
	FString WorstFixed, WorstNeedle;
	constexpr double MaxNeedleTurnAtRest = 0.5;
	TSet<FString> Hanging;
	for (const FPose& Pose : Poses)
	{
		for (const FPartPose& Part : Pose.Parts)
		{
			const FPartPose* C = Centre ? Centre->Parts.FindByPredicate([&Part](const FPartPose& X) { return X.Name == Part.Name; }) : nullptr;
			if (!C) continue;
			const FQuat Rel = C->World.GetRotation().Inverse() * Part.World.GetRotation();
			if (Part.Name.StartsWith(TEXT("steering_wheel")))
			{
				// The column is the wheel's local X (SteeringWheelTurn).
				MaxWheelOffAxis = FMath::Max(MaxWheelOffAxis, OffAxisDeg(Rel, FVector::XAxisVector));
				MaxHubMove = FMath::Max(MaxHubMove, FVector::Dist(Part.BoundsCentre, C->BoundsCentre));
				continue;
			}
			if (Part.Name.StartsWith(TEXT("dial_")) && Part.Parent.Contains(TEXT("steering_wheel"))) Hanging.Add(Part.Name);
			// A needle's mesh origin is not its pivot: turning about the pivot moves the origin, so only its turn axis is judged.
			const double Move = Part.Name.Contains(TEXT("_needle")) ? 0.0 : FVector::Dist(Part.World.GetLocation(), C->World.GetLocation());
			// A needle turns about its own +Z by the value it shows; nothing else may turn.
			const double Turn = Part.Name.Contains(TEXT("_needle")) ? OffAxisDeg(Rel, FVector::ZAxisVector) : FMath::RadiansToDegrees(Rel.GetAngle());
			if (Move > MaxFixedMove || Turn > MaxFixedTurn) WorstFixed = FString::Printf(TEXT("%s at %s"), *Part.Name, *Pose.Name);
			MaxFixedMove = FMath::Max(MaxFixedMove, Move);
			MaxFixedTurn = FMath::Max(MaxFixedTurn, Turn);
			// A needle turns only with what it shows: with the car at rest the speed and rpm hardly change.
			if (Part.Name.Contains(TEXT("_needle")))
			{
				auto Same = [](double A, double B) { return !FMath::IsFinite(A) || !FMath::IsFinite(B) || FMath::Abs(A - B) <= FMath::Max(1.0, 0.02 * FMath::Abs(B)); };
				const double NeedleTurn = FMath::RadiansToDegrees(Rel.GetAngle());
				if (NeedleTurn > MaxNeedleTurnAtRest && Same(Pose.SpeedShown, Centre->SpeedShown) && Same(Pose.RpmShown, Centre->RpmShown))
				{
					MaxNeedleTurn = FMath::Max(MaxNeedleTurn, NeedleTurn);
					WorstNeedle = FString::Printf(TEXT("%s-%.1fdeg-at-%s"), *Part.Name, NeedleTurn, *Pose.Name);
				}
			}
		}
	}
	for (const FString& H : Hanging) Reasons.Add(FString::Printf(TEXT("gauge-%s-hangs-on-the-steering-wheel"), *H));
	if (!WorstNeedle.IsEmpty()) Reasons.Add(FString::Printf(TEXT("gauge-needles-turn-with-the-steering(%s)"), *WorstNeedle));
	if (MaxFixedMove > MaxFixedMoveCm || MaxFixedTurn > MaxFixedTurnDeg)
	{
		Reasons.Add(FString::Printf(TEXT("gauges-or-dash-move-with-the-steering-%.2fcm-%.2fdeg(%s)"), MaxFixedMove, MaxFixedTurn, *WorstFixed.Replace(TEXT(" "), TEXT("-"))));
	}
	if (MaxWheelOffAxis > MaxWheelOffAxisDeg) Reasons.Add(FString::Printf(TEXT("wheel-turns-off-its-column-%.2fdeg"), MaxWheelOffAxis));
	if (MaxHubMove > MaxHubMoveCm) Reasons.Add(FString::Printf(TEXT("wheel-hub-moves-%.2fcm"), MaxHubMove));

	// The rim plane against the column: from the mesh's own vertices, when the CPU still has them.
	double RimTiltDeg = -1.0;
	double ForeignShare = -1.0;
	ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(UGameplayStatics::GetPlayerController(this, 0));
	if (UStaticMeshComponent* Wheel = FindPart(GetModel(Player), TEXT("steering_wheel")))
	{
		const UStaticMesh* Mesh = Wheel->GetStaticMesh();
		const FStaticMeshRenderData* Render = Mesh ? Mesh->GetRenderData() : nullptr;
		if (Render && Render->LODResources.Num() > 0)
		{
			const FPositionVertexBuffer& Pos = Render->LODResources[0].VertexBuffers.PositionVertexBuffer;
			if (Pos.GetNumVertices() > 0 && Pos.GetVertexData() != nullptr)
			{
				// Distance from the column (local X through the pivot) of each vertex.
				TArray<FVector> All;
				double RMax = 0.0;
				for (uint32 V = 0; V < Pos.GetNumVertices(); ++V)
				{
					const FVector P(Pos.VertexPosition(V));
					All.Add(P);
					RMax = FMath::Max(RMax, FVector2D(P.Y, P.Z).Size());
				}
				TArray<FVector> Rim;
				TArray<double> RimR;
				for (const FVector& P : All)
				{
					const double R = FVector2D(P.Y, P.Z).Size();
					if (R > 0.7 * RMax) { Rim.Add(P); RimR.Add(R); }
				}
				const FVector N = PlaneNormal(Rim);
				if (!N.IsNearlyZero())
				{
					RimTiltDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Abs(FVector::DotProduct(N, FVector::XAxisVector)), 0.0, 1.0)));
					if (RimTiltDeg > MaxRimTiltDeg) Reasons.Add(FString::Printf(TEXT("rim-tilted-%.1fdeg-on-the-column"), RimTiltDeg));
				}
				// Geometry well outside the rim (a cluster or a cowl in the wheel's mesh turns with it).
				RimR.Sort();
				const double RimRadius = RimR.Num() ? RimR[RimR.Num() / 2] : RMax;
				int32 Outside = 0;
				for (const FVector& P : All) Outside += FVector2D(P.Y, P.Z).Size() > 1.25 * RimRadius ? 1 : 0;
				ForeignShare = All.Num() ? 100.0 * Outside / All.Num() : 0.0;
				if (ForeignShare > 2.0) Reasons.Add(FString::Printf(TEXT("the-wheel-mesh-holds-other-parts-%.0f%%-of-its-vertices-outside-the-rim"), ForeignShare));
			}
		}
	}

	// Lock: the rim at full lock against the car data's ratio and lock, and the direction.
	double LockLeft = 0.0, LockRight = 0.0, WheelLeft = 0.0, WheelRight = 0.0;
	for (const FPose& P : Poses)
	{
		if (P.Name == TEXT("full-left")) { LockLeft = P.RimDeg; WheelLeft = P.WheelDeg; }
		if (P.Name == TEXT("full-right")) { LockRight = P.RimDeg; WheelRight = P.WheelDeg; }
	}
	const double ExpectedLock = FullLockRoadWheelDeg * Ratio;
	const double MeasuredRatio = FMath::Abs(WheelLeft) > 1.0 ? FMath::Abs(LockLeft / WheelLeft) : 0.0;
	const double LockToLockTurns = (FMath::Abs(LockLeft) + FMath::Abs(LockRight)) / 360.0;
	if (ExpectedLock > 1.0 && (FMath::Abs(FMath::Abs(LockLeft) - ExpectedLock) > MaxLockError * ExpectedLock || FMath::Abs(FMath::Abs(LockRight) - ExpectedLock) > MaxLockError * ExpectedLock))
	{
		Reasons.Add(FString::Printf(TEXT("rim-at-full-lock-%.0f/%.0fdeg-not-%.0fdeg"), LockLeft, LockRight, ExpectedLock));
	}
	// Both + to the left: the rim turns the way the road wheels do.
	if ((LockLeft * WheelLeft < 0.0) || (LockRight * WheelRight < 0.0)) Reasons.Add(TEXT("the-wheel-turns-the-wrong-way"));

	// Image: the cluster with the wheel hidden, centre against full lock.
	double MaxChanged = -1.0;
	for (const FPose& P : Poses)
	{
		if (!Centre || (P.Name != TEXT("full-left") && P.Name != TEXT("full-right"))) continue;
		if (P.PixelsNoWheel.Num() != Centre->PixelsNoWheel.Num() || P.PixelsNoWheel.IsEmpty() || Centre->Cluster.Area() <= 0) continue;
		const FIntRect R = Centre->Cluster;
		int32 Changed = 0, Total = 0;
		TArray<FColor> Diff;
		Diff.Init(FColor::Black, P.PixelsNoWheel.Num());
		for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
		{
			for (int32 X = R.Min.X; X < R.Max.X; ++X)
			{
				const int32 I = Y * P.Width + X;
				const FColor& A = Centre->PixelsNoWheel[I];
				const FColor& B = P.PixelsNoWheel[I];
				const int32 D = FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B));
				const uint8 V = static_cast<uint8>(FMath::Min(255, D * 4));
				Diff[I] = D > 25 ? FColor(255, V / 2, 0) : FColor(V, V, V);
				Changed += D > 25 ? 1 : 0;
				++Total;
			}
		}
		const double Share = Total ? 100.0 * Changed / Total : 0.0;
		MaxChanged = FMath::Max(MaxChanged, Share);
		SavePng(TEXT("diff-") + P.Name, P.Width, P.Height, Diff);
		if (Share > MaxClusterChangedPercent) Reasons.Add(FString::Printf(TEXT("cluster-pixels-change-%.1f%%-at-%s"), Share, *P.Name));
	}

	const bool bPass = Reasons.IsEmpty();
	const FString ReasonText = bPass ? FString(TEXT("ok")) : FString::Join(Reasons, TEXT(","));
	UE_LOG(LogSimCruiseCockpit, Display, TEXT("COCKPIT %s car=%s lock=%.0f/%.0fdeg (car data %.0f) ratio=%.2f (car data %.2f) lock_to_lock=%.2f gauges_move=%.3fcm/%.2fdeg wheel_off_axis=%.2fdeg hub_move=%.3fcm rim_tilt=%.1fdeg outside_rim=%.1f%% cluster_changed=%.2f%% reasons=%s"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *CarId, LockLeft, LockRight, ExpectedLock, MeasuredRatio, Ratio, LockToLockTurns, MaxFixedMove, MaxFixedTurn,
		MaxWheelOffAxis, MaxHubMove, RimTiltDeg, ForeignShare, MaxChanged, *ReasonText);
	FString PoseJson;
	for (const FPose& P : Poses)
	{
		PoseJson += FString::Printf(TEXT("%s{\"name\": \"%s\", \"stick\": %.2f, \"rim_deg\": %.2f, \"road_wheel_deg\": %.2f, \"cluster\": [%d, %d, %d, %d]}"),
			PoseJson.IsEmpty() ? TEXT("") : TEXT(", "), *P.Name, P.Stick, P.RimDeg, P.WheelDeg, P.Cluster.Min.X, P.Cluster.Min.Y, P.Cluster.Max.X, P.Cluster.Max.Y);
	}
	const FString Json = FString::Printf(TEXT("{\n  \"result\": \"%s\", \"car\": \"%s\", \"reasons\": \"%s\",\n")
		TEXT("  \"rim_full_lock_left_deg\": %.2f, \"rim_full_lock_right_deg\": %.2f, \"expected_full_lock_deg\": %.2f, \"ratio\": %.3f, \"car_data_ratio\": %.3f, \"lock_to_lock_turns\": %.3f,\n")
		TEXT("  \"gauges_max_move_cm\": %.4f, \"gauges_max_turn_deg\": %.3f, \"gauges_worst\": \"%s\", \"needle_max_turn_at_rest_deg\": %.2f, \"needle_worst\": \"%s\", \"wheel_off_axis_deg\": %.3f, \"hub_move_cm\": %.4f, \"rim_tilt_deg\": %.2f, \"outside_rim_percent\": %.2f,\n")
		TEXT("  \"cluster_changed_percent\": %.3f,\n  \"limits\": {\"fixed_move_cm\": %.2f, \"fixed_turn_deg\": %.2f, \"wheel_off_axis_deg\": %.2f, \"hub_move_cm\": %.2f, \"rim_tilt_deg\": %.1f, \"lock_error\": %.2f, \"cluster_changed_percent\": %.1f},\n")
		TEXT("  \"poses\": [%s]\n}\n"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *CarId, *ReasonText, LockLeft, LockRight, ExpectedLock, MeasuredRatio, Ratio, LockToLockTurns,
		MaxFixedMove, MaxFixedTurn, *WorstFixed, MaxNeedleTurn, *WorstNeedle, MaxWheelOffAxis, MaxHubMove, RimTiltDeg, ForeignShare, MaxChanged,
		MaxFixedMoveCm, MaxFixedTurnDeg, MaxWheelOffAxisDeg, MaxHubMoveCm, MaxRimTiltDeg, MaxLockError, MaxClusterChangedPercent, *PoseJson);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDir, TEXT("cockpit.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
