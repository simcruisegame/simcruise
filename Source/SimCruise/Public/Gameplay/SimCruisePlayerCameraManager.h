// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Gameplay/SimCruiseCameraModels.h"
#include "SimCruisePlayerCameraManager.generated.h"

/**
 * The driver's own seat and view for one car (the seat & view menu), saved in preferences.json
 * as "seat_<car id>": the eye moved from the car's driver_eye, the view's tilt, and its own
 * vertical field of view (0 = the lens's).
 */
struct SIMCRUISE_API FSimCruiseSeat
{
	double UpCm = 0.0;
	double ForwardCm = 0.0;
	double PitchDeg = 0.0;
	double FovDeg = 0.0;

	static constexpr double MaxUpCm = 10.0;
	static constexpr double MaxForwardCm = 15.0;
	static constexpr double MaxPitchDeg = 8.0;
	static constexpr double MinFovDeg = 30.0;
	static constexpr double MaxFovDeg = 75.0;

	void Clamp();
	bool IsDefault() const { return UpCm == 0.0 && ForwardCm == 0.0 && PitchDeg == 0.0 && FovDeg == 0.0; }
	static FSimCruiseSeat Load(const FString& CarId);
	void Save(const FString& CarId) const;
};

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

	/** The driver's seat and view for the car being driven (applied every frame, live). */
	FSimCruiseSeat Seat;

	/** Free look from the seat or the hood: the right stick (-1..1 each way); the head turns
	 *  toward it and comes back to the road when it is let go. */
	void SetLookInput(const FVector2D& Stick) { LookInput = Stick; }
	static constexpr double LookMaxYawDeg = 110.0;
	static constexpr double LookMaxPitchDeg = 35.0;
	FVector2D GetLookDeg() const { return FVector2D(LookYawDeg, LookPitchDeg); }

private:
	FVector2D LookInput = FVector2D::ZeroVector;
	double LookYawDeg = 0.0;
	double LookPitchDeg = 0.0;
	/** Turn the eye's view by the free look (about the car's up, then the eye's side). */
	FRotator Looked(const FRotator& Gaze, const FTransform& Body) const;
public:

private:
	ESimCruiseView View = ESimCruiseView::Driver;
	ESimCruiseView ShownView = ESimCruiseView::Driver;
	/**
	 * Natural by default (45 deg tall, 74 across 16:9): on a laptop the true FOV (32 deg across)
	 * is geometrically exact but reads as a head pushed against the windscreen, since the eye's
	 * own field is far wider than the screen. True FOV and Wide stay on F6.
	 */
	ESimCruiseLens Lens = ESimCruiseLens::Natural;
	bool bSnap = true;
	/** Where the car was in the last frame, to see a teleport the frame it lands. */
	FVector LastBodyLocation = FVector::ZeroVector;
	FQuat LastBodyRotation = FQuat::Identity;
	bool bHaveLastBody = false;
	FSimCruiseHeadModel Head;
	FSimCruiseChaseFrame Chase;
};
