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

	TArray<FVector> RimOuterEdge(const TArray<FVector>& Points)
	{
		auto Sector = [](const FVector& P) { return int32(FMath::FloorToDouble((FMath::Atan2(P.Z, P.Y) + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI * 64.0)) & 63; };
		// The rim's radius: the median of the farthest point per sector, near the rim's plane.
		double MaxR[64];
		for (int32 K = 0; K < 64; ++K) MaxR[K] = -1.0;
		for (const FVector& P : Points)
		{
			if (FMath::Abs(P.X) > 6.0) continue;
			const int32 K = Sector(P);
			MaxR[K] = FMath::Max(MaxR[K], FVector2D(P.Y, P.Z).Size());
		}
		TArray<double> Sorted;
		for (int32 K = 0; K < 64; ++K) if (MaxR[K] > 0.0) Sorted.Add(MaxR[K]);
		TArray<FVector> Edge;
		if (Sorted.IsEmpty()) return Edge;
		Sorted.Sort();
		const double RimR = Sorted[Sorted.Num() / 2];
		double EdgeR[64];
		FVector EdgeP[64];
		for (int32 K = 0; K < 64; ++K) EdgeR[K] = -1.0;
		for (const FVector& P : Points)
		{
			const double R = FVector2D(P.Y, P.Z).Size();
			if (R < 0.7 * RimR || R > 1.25 * RimR || FMath::Abs(P.X) > 6.0) continue;
			const int32 K = Sector(P);
			if (R > EdgeR[K]) { EdgeR[K] = R; EdgeP[K] = P; }
		}
		for (int32 K = 0; K < 64; ++K) if (EdgeR[K] > 0.0) Edge.Add(EdgeP[K]);
		return Edge;
	}

	bool MeasureRim(const TArray<FVector>& Points, double& OutTiltDeg, double& OutAxisOffsetCm, double& OutRadiusCm, int32& OutEdgePoints)
	{
		const TArray<FVector> Edge = RimOuterEdge(Points);
		OutEdgePoints = Edge.Num();
		if (Edge.Num() < 24) return false;
		const double N = Edge.Num();
		double Sy = 0, Sz = 0, Sx = 0, Syy = 0, Szz = 0, Syz = 0, Sxy = 0, Sxz = 0;
		for (const FVector& P : Edge) { Sy += P.Y; Sz += P.Z; Sx += P.X; Syy += P.Y * P.Y; Szz += P.Z * P.Z; Syz += P.Y * P.Z; Sxy += P.X * P.Y; Sxz += P.X * P.Z; }
		const double My = Sy / N, Mz = Sz / N, Mx = Sx / N;
		const double Cyy = Syy / N - My * My, Czz = Szz / N - Mz * Mz, Cyz = Syz / N - My * Mz, Cxy = Sxy / N - Mx * My, Cxz = Sxz / N - Mx * Mz;
		const double Det = Cyy * Czz - Cyz * Cyz;
		const double A = Det > 1e-9 ? (Cxy * Czz - Cxz * Cyz) / Det : 0.0;
		const double B = Det > 1e-9 ? (Cxz * Cyy - Cxy * Cyz) / Det : 0.0;
		OutTiltDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Sqrt(A * A + B * B)));
		// Kasa circle in Y-Z about the points' mean.
		double Suu = 0, Svv = 0, Suv = 0, Suuu = 0, Svvv = 0, Suvv = 0, Svuu = 0;
		for (const FVector& P : Edge)
		{
			const double U = P.Y - My, V = P.Z - Mz;
			Suu += U * U; Svv += V * V; Suv += U * V; Suuu += U * U * U; Svvv += V * V * V; Suvv += U * V * V; Svuu += V * U * U;
		}
		const double KDet = Suu * Svv - Suv * Suv;
		const double Ra = 0.5 * (Suuu + Suvv), Rb = 0.5 * (Svvv + Svuu);
		const double Uc = KDet > 1e-9 ? (Ra * Svv - Rb * Suv) / KDet : 0.0, Vc = KDet > 1e-9 ? (Rb * Suu - Ra * Suv) / KDet : 0.0;
		OutAxisOffsetCm = FVector2D(My + Uc, Mz + Vc).Size();
		OutRadiusCm = FMath::Sqrt(Uc * Uc + Vc * Vc + (Suu + Svv) / N);
		return true;
	}

	bool FitRimCircle(const TArray<FVector>& Points, FVector& OutNormal, FVector& OutCentre, double& OutRadius, int32& OutKept)
	{
		TArray<FVector> Kept = Points;
		constexpr int32 Passes = 5;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			if (Kept.Num() < 8) return false;
			const FVector N = PlaneNormal(Kept);
			if (N.IsNearlyZero()) return false;
			FVector Mean = FVector::ZeroVector;
			for (const FVector& P : Kept) Mean += P;
			Mean /= Kept.Num();
			const FVector U = FVector::CrossProduct(N, FMath::Abs(N.Z) < 0.9 ? FVector::ZAxisVector : FVector::YAxisVector).GetSafeNormal();
			const FVector V = FVector::CrossProduct(N, U);
			// x^2 + y^2 + D x + E y + F = 0 by least squares (normal equations, 3 x 3).
			double A[3][3] = {}, B[3] = {};
			for (const FVector& P : Kept)
			{
				const double X = FVector::DotProduct(P - Mean, U), Y = FVector::DotProduct(P - Mean, V);
				const double Row[3] = {X, Y, 1.0};
				const double Rhs = -(X * X + Y * Y);
				for (int32 I = 0; I < 3; ++I) { B[I] += Row[I] * Rhs; for (int32 J = 0; J < 3; ++J) A[I][J] += Row[I] * Row[J]; }
			}
			// Gaussian elimination with partial pivoting.
			for (int32 C = 0; C < 3; ++C)
			{
				int32 Best = C;
				for (int32 R = C + 1; R < 3; ++R) if (FMath::Abs(A[R][C]) > FMath::Abs(A[Best][C])) Best = R;
				if (FMath::Abs(A[Best][C]) < 1e-12) return false;
				for (int32 J = 0; J < 3; ++J) Swap(A[C][J], A[Best][J]);
				Swap(B[C], B[Best]);
				for (int32 R = C + 1; R < 3; ++R)
				{
					const double K = A[R][C] / A[C][C];
					for (int32 J = C; J < 3; ++J) A[R][J] -= K * A[C][J];
					B[R] -= K * B[C];
				}
			}
			double S[3];
			for (int32 R = 2; R >= 0; --R) { double Sum = B[R]; for (int32 J = R + 1; J < 3; ++J) Sum -= A[R][J] * S[J]; S[R] = Sum / A[R][R]; }
			const double Cx = -S[0] / 2.0, Cy = -S[1] / 2.0;
			const double R2 = Cx * Cx + Cy * Cy - S[2];
			if (R2 <= 0.0) return false;
			OutNormal = N;
			OutCentre = Mean + U * Cx + V * Cy;
			OutRadius = FMath::Sqrt(R2);
			OutKept = Kept.Num();
			if (Pass == Passes - 1) break;
			// First a wide cut (15 % of the radius), then a cut at 2.5 times the median residual (at least
			// 0.4 cm, the rim tube's own scatter): a flat bottom's straight part and the spokes' ends go.
			TArray<double> Res;
			for (const FVector& P : Kept) Res.Add(FMath::Abs((P - OutCentre - N * FVector::DotProduct(P - OutCentre, N)).Size() - OutRadius));
			TArray<double> Sorted = Res;
			Sorted.Sort();
			const double Cut = Pass == 0 ? 0.15 * OutRadius : FMath::Max(0.4, 2.5 * Sorted[Sorted.Num() / 2]);
			TArray<FVector> Next;
			for (int32 I = 0; I < Kept.Num(); ++I)
			{
				const double H = FMath::Abs(FVector::DotProduct(Kept[I] - OutCentre, N));
				if (H < 1.5 && Res[I] < Cut) Next.Add(Kept[I]);
			}
			Kept = MoveTemp(Next);
		}
		return true;
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
				CarLockTurns = Car->GetRealCar().SteeringLockTurns;
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
		// The wheel hidden for 40 frames (TSR's history settles), then the second picture.
		if (Stage == EStage::ShotNoWheel && !bShotPending && ++NoWheelFrames == 40)
		{
			RequestShot();
		}
		if (Now - StageStarted > 10.0)
		{
			// No picture came: go on without it (the image check then has nothing to judge).
			bShotPending = false;
			if (UStaticMeshComponent* Wheel = FindPart(GetModel(Player), TEXT("steering_wheel"))) { Wheel->SetVisibility(true); Wheel->SetCastShadow(bWheelCastShadow); }
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
			// Hidden with no shadow (a hidden mesh can still cast one), then TSR settles before the picture.
			bWheelCastShadow = Wheel->CastShadow;
			Wheel->SetVisibility(false);
			Wheel->SetCastShadow(false);
			Wheel->bCastHiddenShadow = false;
			Wheel->MarkRenderStateDirty();
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
		if (Wheel) { Wheel->SetVisibility(true); Wheel->SetCastShadow(bWheelCastShadow); }
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
	double MaxFixedMove = 0.0, MaxFixedTurn = 0.0, MaxWheelOffAxis = 0.0, MaxHubMove = 0.0, MaxBoundsMove = 0.0, MaxNeedleTurn = 0.0;
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
				// A point on the axis (the pivot the rig puts on the column) must stay; the mesh's bounds
				// centre moves on an asymmetric wheel (three spokes, a flat bottom) that turns true, so it is only reported.
				MaxHubMove = FMath::Max(MaxHubMove, FVector::Dist(Part.World.GetLocation(), C->World.GetLocation()));
				MaxBoundsMove = FMath::Max(MaxBoundsMove, FVector::Dist(Part.BoundsCentre, C->BoundsCentre));
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
	if (MaxHubMove > MaxHubMoveCm) Reasons.Add(FString::Printf(TEXT("wheel-axis-moves-%.2fcm"), MaxHubMove));

	// The rim plane against the column: from the mesh's own vertices, when the CPU still has them.
	double RimTiltDeg = -1.0;
	double RimAxisOffsetCm = -1.0;
	double RimRadiusCm = -1.0;
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
					// In cm along the component's own axes (its scale applied): the column is local X through the pivot.
					const FVector P = FVector(Pos.VertexPosition(V)) * Wheel->GetComponentScale();
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
				// The rim's outer edge only (64 sectors, as the rig check): its plane against the column, its
				// circle's centre against the axis. A fit over every far vertex took in the spokes' ends and
				// the horn pad's rim on the Miata (12.9 deg where the frames show none).
				double Radius = 0.0;
				int32 EdgePoints = 0;
				if (MeasureRim(All, RimTiltDeg, RimAxisOffsetCm, Radius, EdgePoints))
				{
					RimRadiusCm = Radius;
					if (RimTiltDeg > MaxRimTiltDeg) Reasons.Add(FString::Printf(TEXT("rim-tilted-%.1fdeg-on-the-column"), RimTiltDeg));
					if (RimAxisOffsetCm > MaxRimAxisOffsetCm) Reasons.Add(FString::Printf(TEXT("rim-centre-%.2fcm-off-the-column"), RimAxisOffsetCm));
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
	// The car's real lock-to-lock turns when its data has them: the column quickens toward lock, so the
	// on-centre ratio times the road wheels' lock is not the rim's angle at full lock.
	const double ExpectedLock = CarLockTurns > 0.5 ? CarLockTurns * 180.0 : FullLockRoadWheelDeg * Ratio;
	const double MeasuredRatio = FMath::Abs(WheelLeft) > 1.0 ? FMath::Abs(LockLeft / WheelLeft) : 0.0;
	const double LockToLockTurns = (FMath::Abs(LockLeft) + FMath::Abs(LockRight)) / 360.0;
	if (ExpectedLock > 1.0 && (FMath::Abs(FMath::Abs(LockLeft) - ExpectedLock) > MaxLockError * ExpectedLock || FMath::Abs(FMath::Abs(LockRight) - ExpectedLock) > MaxLockError * ExpectedLock))
	{
		Reasons.Add(FString::Printf(TEXT("rim-at-full-lock-%.0f/%.0fdeg-not-%.0fdeg"), LockLeft, LockRight, ExpectedLock));
	}
	// Both + to the left: the rim turns the way the road wheels do.
	if ((LockLeft * WheelLeft < 0.0) || (LockRight * WheelRight < 0.0)) Reasons.Add(TEXT("the-wheel-turns-the-wrong-way"));

	// Image: the cluster with the wheel hidden, centre against full lock. The anti-aliasing changes a
	// few edge pixels from frame to frame anyway: the noise is the change between two centre poses,
	// and full lock may change no more than that plus MaxClusterChangedPercent.
	double MaxChanged = -1.0;
	double Noise = 0.0;
	auto ChangedShare = [](const FPose& A, const FPose& B, const FIntRect& R)
	{
		int32 Changed = 0, Total = 0;
		for (int32 Y = R.Min.Y; Y < R.Max.Y; ++Y)
		{
			for (int32 X = R.Min.X; X < R.Max.X; ++X)
			{
				const FColor& Ca = A.PixelsNoWheel[Y * A.Width + X];
				const FColor& Cb = B.PixelsNoWheel[Y * B.Width + X];
				Changed += FMath::Max3(FMath::Abs(Ca.R - Cb.R), FMath::Abs(Ca.G - Cb.G), FMath::Abs(Ca.B - Cb.B)) > 25 ? 1 : 0;
				++Total;
			}
		}
		return Total ? 100.0 * Changed / Total : 0.0;
	};
	for (const FPose& P : Poses)
	{
		if (Centre && P.Name.StartsWith(TEXT("centre-")) && P.PixelsNoWheel.Num() == Centre->PixelsNoWheel.Num() && !P.PixelsNoWheel.IsEmpty() && Centre->Cluster.Area() > 0)
		{
			Noise = FMath::Max(Noise, ChangedShare(*Centre, P, Centre->Cluster));
		}
	}
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
		// A second number only: the needles' own angles (the parts' transforms above) decide.
	}

	const bool bPass = Reasons.IsEmpty();
	const FString ReasonText = bPass ? FString(TEXT("ok")) : FString::Join(Reasons, TEXT(","));
	UE_LOG(LogSimCruiseCockpit, Display, TEXT("COCKPIT %s car=%s lock=%.0f/%.0fdeg (car data %.0f) ratio=%.2f (car data %.2f) lock_to_lock=%.2f gauges_move=%.3fcm/%.2fdeg wheel_off_axis=%.2fdeg hub_move=%.3fcm rim_tilt=%.1fdeg rim_axis_offset=%.2fcm rim_radius=%.1fcm bounds_move=%.2fcm outside_rim=%.1f%% cluster_changed=%.2f%% (noise %.2f%%) reasons=%s"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *CarId, LockLeft, LockRight, ExpectedLock, MeasuredRatio, Ratio, LockToLockTurns, MaxFixedMove, MaxFixedTurn,
		MaxWheelOffAxis, MaxHubMove, RimTiltDeg, RimAxisOffsetCm, RimRadiusCm, MaxBoundsMove, ForeignShare, MaxChanged, Noise, *ReasonText);
	FString PoseJson;
	for (const FPose& P : Poses)
	{
		PoseJson += FString::Printf(TEXT("%s{\"name\": \"%s\", \"stick\": %.2f, \"rim_deg\": %.2f, \"road_wheel_deg\": %.2f, \"cluster\": [%d, %d, %d, %d]}"),
			PoseJson.IsEmpty() ? TEXT("") : TEXT(", "), *P.Name, P.Stick, P.RimDeg, P.WheelDeg, P.Cluster.Min.X, P.Cluster.Min.Y, P.Cluster.Max.X, P.Cluster.Max.Y);
	}
	const FString Json = FString::Printf(TEXT("{\n  \"result\": \"%s\", \"car\": \"%s\", \"reasons\": \"%s\",\n")
		TEXT("  \"rim_full_lock_left_deg\": %.2f, \"rim_full_lock_right_deg\": %.2f, \"expected_full_lock_deg\": %.2f, \"ratio\": %.3f, \"car_data_ratio\": %.3f, \"lock_to_lock_turns\": %.3f,\n")
		TEXT("  \"gauges_max_move_cm\": %.4f, \"gauges_max_turn_deg\": %.3f, \"gauges_worst\": \"%s\", \"needle_max_turn_at_rest_deg\": %.2f, \"needle_worst\": \"%s\", \"wheel_off_axis_deg\": %.3f, \"hub_move_cm\": %.4f, \"rim_tilt_deg\": %.2f, \"rim_axis_offset_cm\": %.3f, \"rim_radius_cm\": %.2f, \"bounds_move_cm\": %.3f, \"outside_rim_percent\": %.2f,\n")
		TEXT("  \"cluster_changed_percent\": %.3f, \"cluster_noise_percent\": %.3f,\n  \"limits\": {\"fixed_move_cm\": %.2f, \"fixed_turn_deg\": %.2f, \"wheel_off_axis_deg\": %.2f, \"hub_move_cm\": %.2f, \"rim_tilt_deg\": %.1f, \"lock_error\": %.2f, \"cluster_changed_percent\": %.1f},\n")
		TEXT("  \"poses\": [%s]\n}\n"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *CarId, *ReasonText, LockLeft, LockRight, ExpectedLock, MeasuredRatio, Ratio, LockToLockTurns,
		MaxFixedMove, MaxFixedTurn, *WorstFixed, MaxNeedleTurn, *WorstNeedle, MaxWheelOffAxis, MaxHubMove, RimTiltDeg, RimAxisOffsetCm, RimRadiusCm, MaxBoundsMove, ForeignShare, MaxChanged, Noise,
		MaxFixedMoveCm, MaxFixedTurnDeg, MaxWheelOffAxisDeg, MaxHubMoveCm, MaxRimTiltDeg, MaxLockError, MaxClusterChangedPercent, *PoseJson);
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(OutputDir, TEXT("cockpit.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
