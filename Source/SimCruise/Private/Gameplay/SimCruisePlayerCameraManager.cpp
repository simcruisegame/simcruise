// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePlayerCameraManager.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/SimCruisePreferences.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "HAL/IConsoleManager.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"

static TAutoConsoleVariable<float> CVarScreenInches(
	TEXT("simcruise.Lens.ScreenInches"), 15.6f,
	TEXT("The screen's diagonal in inches, for the true-FOV lens (its height follows the window's aspect)."));
static TAutoConsoleVariable<float> CVarScreenHeightCm(
	TEXT("simcruise.Lens.ScreenHeightCm"), 0.f,
	TEXT("The picture's physical height in cm for the true-FOV lens; 0 derives it from simcruise.Lens.ScreenInches."));
static TAutoConsoleVariable<float> CVarViewDistanceCm(
	TEXT("simcruise.Lens.ViewDistanceCm"), 60.f,
	TEXT("Eye-to-screen distance in cm for the 'true to screen' lens."));
static TAutoConsoleVariable<float> CVarHeadMotion(
	TEXT("simcruise.Camera.HeadMotion"), 1.f,
	TEXT("Driver head motion 0..1 (0 welds the eye to the seat; the horizon hold stays)."));

void FSimCruiseSeat::Clamp()
{
	UpCm = FMath::Clamp(UpCm, -MaxUpCm, MaxUpCm);
	ForwardCm = FMath::Clamp(ForwardCm, -MaxForwardCm, MaxForwardCm);
	PitchDeg = FMath::Clamp(PitchDeg, -MaxPitchDeg, MaxPitchDeg);
	FovDeg = FovDeg <= 0.0 ? 0.0 : FMath::Clamp(FovDeg, MinFovDeg, MaxFovDeg);
}

FSimCruiseSeat FSimCruiseSeat::Load(const FString& CarId)
{
	// "up,forward,pitch,fov" in mm, mm, tenths of a degree and tenths of a degree.
	FSimCruiseSeat Seat;
	TArray<FString> Parts;
	SimCruisePreferences::GetString(TEXT("seat_") + CarId, FString()).ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() == 4)
	{
		Seat.UpCm = FCString::Atoi(*Parts[0]) / 10.0;
		Seat.ForwardCm = FCString::Atoi(*Parts[1]) / 10.0;
		Seat.PitchDeg = FCString::Atoi(*Parts[2]) / 10.0;
		Seat.FovDeg = FCString::Atoi(*Parts[3]) / 10.0;
		Seat.Clamp();
	}
	return Seat;
}

void FSimCruiseSeat::Save(const FString& CarId) const
{
	SimCruisePreferences::SetString(TEXT("seat_") + CarId, FString::Printf(TEXT("%d,%d,%d,%d"),
		FMath::RoundToInt(UpCm * 10.0), FMath::RoundToInt(ForwardCm * 10.0), FMath::RoundToInt(PitchDeg * 10.0), FMath::RoundToInt(FovDeg * 10.0)));
}

ASimCruisePlayerCameraManager::ASimCruisePlayerCameraManager()
{
	DefaultFOV = 90.f;
	ViewPitchMin = -89.f;
	ViewPitchMax = 89.f;
}

const TCHAR* ASimCruisePlayerCameraManager::ViewName(ESimCruiseView InView)
{
	switch (InView)
	{
	case ESimCruiseView::Hood: return TEXT("Hood view");
	case ESimCruiseView::Chase: return TEXT("Near chase view");
	case ESimCruiseView::FarChase: return TEXT("Far chase view");
	case ESimCruiseView::Scenic: return TEXT("Scenic view");
	default: return TEXT("Driver view");
	}
}

void ASimCruisePlayerCameraManager::CycleView()
{
	SetView(static_cast<ESimCruiseView>((static_cast<uint8>(View) + 1) % SimCruiseViewCount));
}

void ASimCruisePlayerCameraManager::SetView(ESimCruiseView InView)
{
	View = InView;
	bSnap = true;
}

void ASimCruisePlayerCameraManager::CycleLens()
{
	Lens = static_cast<ESimCruiseLens>((static_cast<uint8>(Lens) + 1) % 3);
}

void ASimCruisePlayerCameraManager::UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime)
{
	APawn* Pawn = Cast<APawn>(OutVT.Target);
	ISimCruiseVehicleControl* Vehicle = Cast<ISimCruiseVehicleControl>(Pawn);
	if (!Vehicle)
	{
		Super::UpdateViewTarget(OutVT, DeltaTime);
		return;
	}

	FSimCruiseDriveTelemetry Telemetry;
	Vehicle->GetVehicleTelemetry(Telemetry);
	const FSimCruiseVehicleSetup Setup = Vehicle->GetVehicleSetup();
	const FTransform Body = Pawn->GetActorTransform();
	const FVector WorldVelocityMps = Body.GetRotation().RotateVector(Telemetry.BodyVelocityMps);

	// A recover, a car change or a travel moves the car on the physics thread a frame
	// or more after the request, so a snap on the request frame came too early and the
	// chase camera swooped from the old place. Snap on the frame the car is seen to jump:
	// farther than it could drive in the frame, with 5 m to spare.
	const FVector BodyLocation = Body.GetLocation();
	if (bHaveLastBody)
	{
		const double JumpCm = FVector::Dist(BodyLocation, LastBodyLocation);
		const double DriveCm = (WorldVelocityMps.Size() * 2.0 + 10.0) * 100.0 * FMath::Max(static_cast<double>(DeltaTime), 1.0 / 30.0);
		// A recover in place turns the car upright: more than 45 degrees in one frame.
		const double TurnDeg = FMath::RadiansToDegrees(Body.GetRotation().AngularDistance(LastBodyRotation));
		if (JumpCm > DriveCm + 500.0 || TurnDeg > 45.0)
		{
			bSnap = true;
		}
	}
	LastBodyLocation = BodyLocation;
	LastBodyRotation = Body.GetRotation();
	bHaveLastBody = true;

	double Aspect = 16.0 / 9.0;
	if (PCOwner)
	{
		int32 SizeX = 0;
		int32 SizeY = 0;
		PCOwner->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0)
		{
			Aspect = static_cast<double>(SizeX) / SizeY;
		}
	}

	// A model without a cabin (no steering wheel found: an empty shell, as the
	// downloaded CR-V and Odyssey are) is watched from the chase view instead.
	const USimCruiseCarPresentationComponent* Car = Pawn->FindComponentByClass<USimCruiseCarPresentationComponent>();
	const bool bHasCabin = !Car || Car->GetCarId().IsEmpty() || Car->HasSteeringWheel();
	const ESimCruiseView Shown = (View == ESimCruiseView::Driver && !bHasCabin) ? ESimCruiseView::Chase : View;
	// Free look: the head turns toward the stick quickly (about 0.15 s) and back to the road a
	// little slower when it is let go, as a glance does.
	{
		const double Rate = 1.0 - FMath::Exp(-FMath::Max(static_cast<double>(DeltaTime), 0.0) / (LookInput.IsNearlyZero(0.05) ? 0.22 : 0.15));
		const FVector2D Clamped(FMath::Clamp(LookInput.X, -1.0, 1.0), FMath::Clamp(LookInput.Y, -1.0, 1.0));
		LookYawDeg += (Clamped.X * LookMaxYawDeg - LookYawDeg) * Rate;
		LookPitchDeg += (Clamped.Y * LookMaxPitchDeg - LookPitchDeg) * Rate;
	}
	// A little wider with speed (up to 6 % at 130 km/h), as the eye's attention spreads: the speed reads.
	const double SpeedWiden = 1.0 + 0.06 * FMath::Clamp(FMath::Abs(Telemetry.BodyVelocityMps.X) / 36.0, 0.0, 1.0);
	ShownView = Shown;

	if (Shown == ESimCruiseView::Driver)
	{
		if (bSnap)
		{
			Head.Reset();
		}
		Head.HeadMotion = CVarHeadMotion.GetValueOnGameThread();
		// The driver's own seat: the eye moved in the car's frame (X forward, Z up).
		FSimCruiseVehicleSetup Seated = Setup;
		Seated.DriverEyeLocalCm += FVector(Seat.ForwardCm, 0.0, Seat.UpCm);
		const FSimCruiseEyePose Eye = Head.Update(Body, WorldVelocityMps, Seated, Telemetry, DeltaTime);
		OutVT.POV.Location = Eye.Location;
		// The view tilted down (negative) or up about the eye's own side axis.
		const FQuat Tilt(FVector::CrossProduct(Eye.Up, Eye.Forward).GetSafeNormal(), FMath::DegreesToRadians(-Seat.PitchDeg));
		OutVT.POV.Rotation = Looked((Tilt * Eye.ToRotator().Quaternion()).Rotator(), Body);
		const double HeightCm = CVarScreenHeightCm.GetValueOnGameThread() > 0.f ? CVarScreenHeightCm.GetValueOnGameThread()
			: SimCruiseLens::ScreenHeightCm(CVarScreenInches.GetValueOnGameThread(), Aspect);
		const double Vertical = SpeedWiden * (Seat.FovDeg > 0.0 ? Seat.FovDeg : SimCruiseLens::VerticalFovDeg(Lens, HeightCm, CVarViewDistanceCm.GetValueOnGameThread()));
		OutVT.POV.FOV = static_cast<float>(SimCruiseLens::HorizontalFromVerticalDeg(Vertical, Aspect));
		bSnap = false;
		return;
	}
	if (Shown == ESimCruiseView::Hood)
	{
		// On the car's centre line just ahead of the windscreen, a little under the eye's height (115 cm ahead,
		// 8 cm lower), past the windscreen's foot: the hood fills the foot of the picture. Rides the body, half its roll.
		const FVector Local = FVector(Setup.DriverEyeLocalCm.X + 115.0, 0.0, Setup.DriverEyeLocalCm.Z - 8.0);
		OutVT.POV.Location = Body.TransformPosition(Local);
		FRotator Rotation = Body.Rotator();
		Rotation.Roll *= 0.5;
		OutVT.POV.Rotation = Looked(Rotation, Body);
		OutVT.POV.FOV = static_cast<float>(SimCruiseLens::HorizontalFromVerticalDeg(SpeedWiden * SimCruiseLens::NaturalVerticalDeg, Aspect));
		bSnap = false;
		return;
	}

	FVector Goal;
	FVector LookAt;
	Chase.Update(Shown, Body, Telemetry.BodyVelocityMps, DeltaTime, bSnap, Goal, LookAt);
	bSnap = false;

	if (UWorld* World = GetWorld())
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruiseChaseCamera), false, Pawn);
		// Never below the ground: 55 cm clearance, as Drive.gd keeps. The probe
		// starts only 2.5 m above the camera, Drive.surface()'s rule for decks:
		// under an overpass the deck above must not lift the camera onto it.
		FHitResult Ground;
		if (World->LineTraceSingleByChannel(Ground, Goal + FVector(0.0, 0.0, 250.0), Goal - FVector(0.0, 0.0, 1000.0), ECC_Visibility, Params))
		{
			Goal.Z = FMath::Max(Goal.Z, Ground.ImpactPoint.Z + 55.0);
		}
		// Retract in front of a wall rather than pass through it.
		const FVector Pivot = Body.TransformPosition(FVector(0.0, 0.0, 112.0));
		FHitResult Wall;
		if (World->LineTraceSingleByChannel(Wall, Pivot, Goal, ECC_Camera, Params))
		{
			Goal = Wall.ImpactPoint + (Pivot - Goal).GetSafeNormal() * 22.0;
			Chase.OverrideLocation(Goal);
		}
	}

	OutVT.POV.Location = Goal;
	OutVT.POV.Rotation = (LookAt - Goal).Rotation();
	OutVT.POV.Rotation.Roll = 0.0;
	OutVT.POV.FOV = static_cast<float>(SimCruiseLens::HorizontalFromVerticalDeg(SpeedWiden * SimCruiseLens::ChaseVerticalDeg, Aspect));
}

FRotator ASimCruisePlayerCameraManager::Looked(const FRotator& Gaze, const FTransform& Body) const
{
	if (FMath::Abs(LookYawDeg) < 0.01 && FMath::Abs(LookPitchDeg) < 0.01)
	{
		return Gaze;
	}
	const FQuat Yaw(Body.GetRotation().GetUpVector(), FMath::DegreesToRadians(LookYawDeg));
	const FQuat Turned = Yaw * Gaze.Quaternion();
	const FQuat Pitch(Turned.GetRightVector(), FMath::DegreesToRadians(-LookPitchDeg));
	return (Pitch * Turned).Rotator();
}