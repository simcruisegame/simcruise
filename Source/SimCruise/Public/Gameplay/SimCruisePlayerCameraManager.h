// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Gameplay/SimCruiseCameraModels.h"
#include "SimCruisePlayerCameraManager.generated.h"

/**
 * Driver, chase and scenic views for a pawn that implements
 * ISimCruiseVehicleControl. Driver: the lagged head model at the car's
 * driver_eye through the chosen lens (natural 45 deg vertical by default).
 * Chase and scenic: the slip-showing heading frame, kept above the ground and
 * pulled in front of walls by a line trace instead of passing through them.
 */
UCLASS()
class SIMCRUISE_API ASimCruisePlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	ASimCruisePlayerCameraManager();

	virtual void UpdateViewTarget(FTViewTarget& OutVT, float DeltaTime) override;

	void CycleView();
	void SetView(ESimCruiseView InView);
	ESimCruiseView GetView() const { return View; }
	/** The view drawn last frame: the chase view stands in for the seat of a car without a cabin. */
	ESimCruiseView GetShownView() const { return ShownView; }
	static const TCHAR* ViewName(ESimCruiseView InView);

	void CycleLens();
	ESimCruiseLens GetLens() const { return Lens; }

	/** Forget motion history after a teleport (recover, restart). */
	void SnapNextFrame() { bSnap = true; }

	FSimCruiseHeadModel& GetHeadModel() { return Head; }
	const FSimCruiseChaseFrame& GetChaseFrame() const { return Chase; }

private:
	ESimCruiseView View = ESimCruiseView::Driver;
	ESimCruiseView ShownView = ESimCruiseView::Driver;
	/** The lens and angle last written to the log (a line when either changes). */
	ESimCruiseLens LoggedLens = static_cast<ESimCruiseLens>(255);
	float LoggedFov = 0.f;
	/**
	 * Natural by default (45 deg tall, 74 across 16:9): on a laptop the true FOV (32 deg across)
	 * is geometrically exact but reads as a head pushed against the windscreen, since the eye's
	 * own field is far wider than the screen. True FOV and Wide stay on F6 / D-pad left.
	 */
	ESimCruiseLens Lens = ESimCruiseLens::Natural;
	bool bSnap = true;
	FSimCruiseHeadModel Head;
	FSimCruiseChaseFrame Chase;
};
