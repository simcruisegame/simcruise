// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePlayerCameraManager.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
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
	case ESimCruiseView::Chase: return TEXT("Chase view");
	case ESimCruiseView::Scenic: return TEXT("Scenic view");
	default: return TEXT("Driver view");
	}
}

void ASimCruisePlayerCameraManager::CycleView()
{
	SetView(static_cast<ESimCruiseView>((static_cast<uint8>(View) + 1) % 3));
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
	ShownView = Shown;

	if (Shown == ESimCruiseView::Driver)
	{
		if (bSnap)
		{
			Head.Reset();
		}
		Head.HeadMotion = CVarHeadMotion.GetValueOnGameThread();
		const FSimCruiseEyePose Eye = Head.Update(Body, WorldVelocityMps, Setup, Telemetry, DeltaTime);
		OutVT.POV.Location = Eye.Location;
		OutVT.POV.Rotation = Eye.ToRotator();
		const double HeightCm = CVarScreenHeightCm.GetValueOnGameThread() > 0.f ? CVarScreenHeightCm.GetValueOnGameThread()
			: SimCruiseLens::ScreenHeightCm(CVarScreenInches.GetValueOnGameThread(), Aspect);
		const double Vertical = SimCruiseLens::VerticalFovDeg(Lens, HeightCm, CVarViewDistanceCm.GetValueOnGameThread());
		OutVT.POV.FOV = static_cast<float>(SimCruiseLens::HorizontalFromVerticalDeg(Vertical, Aspect));
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
	OutVT.POV.FOV = static_cast<float>(SimCruiseLens::HorizontalFromVerticalDeg(SimCruiseLens::ChaseVerticalDeg, Aspect));
}
