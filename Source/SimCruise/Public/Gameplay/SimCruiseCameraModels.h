// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Gameplay/SimCruiseVehicleControl.h"

/** Camera views in C / Y order. The game opens in the driver's seat. */
enum class ESimCruiseView : uint8
{
	Chase = 0,
	Driver = 1,
	Scenic = 2,
};

/** The driver's lens (godot-final:game/ViewLens.gd): vertical field of view; the
 *  horizontal angle follows the window's aspect ratio. */
enum class ESimCruiseLens : uint8
{
	Natural,   // 45 deg vertical: about a 30 mm lens on full frame
	Screen,    // true FOV: the angle the physical screen subtends at the eye (the default)
	Wide,      // 67 deg: the previous Godot view, for tight streets
};

namespace SimCruiseLens
{
	constexpr double NaturalVerticalDeg = 45.0;
	constexpr double WideVerticalDeg = 67.0;
	/** Chase and scenic: 50 deg vertical (77 deg across 16:9). Godot used 62,
	 *  which with its speed-growing trail left the car a speck at highway pace. */
	constexpr double ChaseVerticalDeg = 50.0;

	/** Vertical FOV of a lens; Screen uses the screen's physical height and
	 *  the eye distance (cvars simcruise.Lens.ScreenHeightCm / ViewDistanceCm). */
	SIMCRUISE_API double VerticalFovDeg(ESimCruiseLens Lens, double ScreenHeightCm, double ViewDistanceCm);
	/** Unreal takes a horizontal FOV; convert for the viewport's aspect (width/height). */
	SIMCRUISE_API double HorizontalFromVerticalDeg(double VerticalDeg, double AspectRatio);
	SIMCRUISE_API const TCHAR* Label(ESimCruiseLens Lens);

	/** A screen's picture height from its diagonal (inches) and aspect (width/height), cm. */
	SIMCRUISE_API double ScreenHeightCm(double DiagonalInches, double AspectRatio);
	/**
	 * The true horizontal field of view: the angle the screen's width subtends at the eye,
	 * 2 atan(width / (2 distance)); 15.6 in 16:9 at 60 cm is 32.1 degrees.
	 */
	SIMCRUISE_API double TrueHorizontalFovDeg(double DiagonalInches, double AspectRatio, double ViewDistanceCm);
}

/** Driver eye pose in world space. */
struct FSimCruiseEyePose
{
	FVector Location = FVector::ZeroVector;   // cm
	FVector Forward = FVector::ForwardVector;
	FVector Up = FVector::UpVector;

	FRotator ToRotator() const;
};

/**
 * Driver head model, ported from godot-final:game/Cockpit.gd head_view(). The eye is not
 * welded to the chassis: a critically damped spring (9 rad/s) moves it against
 * the felt body acceleration (3 cm per g lateral, 2 cm per g longitudinal,
 * clamped), braking dips the gaze 1.5 deg per g, the gaze leads up to 4 deg
 * into the corner from 14 m/s, road roughness adds millimetre vibration, and
 * the view keeps 70 % of the body roll (the horizon is partly held, as a
 * person's vestibular reflex does). HeadMotion 0..1 scales every term except
 * the horizon. Gaze lead is normalised by the car's own available rack rather
 * than the NB's 0.10 rad (plan-2026-11-10/15 correction).
 */
class SIMCRUISE_API FSimCruiseHeadModel
{
public:
	static constexpr double HeadOmega = 9.0;
	static constexpr double LateralMPerG = 0.030;
	static constexpr double LongitudinalMPerG = 0.020;
	static constexpr double LateralMaxM = 0.030;
	static constexpr double LongitudinalMaxM = 0.020;
	static constexpr double BrakePitchDeg = 1.5;
	static constexpr double GazeLeadDeg = 4.0;
	static constexpr double HorizonFollow = 0.70;
	static constexpr double VibrationM = 0.0022;
	static constexpr double AccelFilterS = 0.06;

	FSimCruiseHeadModel();
	void Reset();

	/** Advance one rendered frame. Body is the pawn transform; WorldVelocityMps
	 *  the pawn's world velocity in m/s (Unreal axes). */
	FSimCruiseEyePose Update(const FTransform& Body, const FVector& WorldVelocityMps, const FSimCruiseVehicleSetup& Setup,
		const FSimCruiseDriveTelemetry& Telemetry, double DeltaTimeS);

	double HeadMotion = 1.0;

	/** Inspection for tests and the HUD. Body axes, metres / degrees / m/s^2. */
	const FVector& GetHeadOffsetM() const { return HeadOffset; }
	const FVector& GetFeltSpecificForce() const { return Felt; }
	double GetBrakePitchDeg() const { return BrakePitch; }
	double GetGazeDeg() const { return Gaze; }

private:
	FVector HeadOffset = FVector::ZeroVector;
	FVector HeadVelocity = FVector::ZeroVector;
	FVector Felt = FVector::ZeroVector;
	FVector PreviousVelocity = FVector::ZeroVector;
	FVector LastAcceleration = FVector::ZeroVector;
	double PreviousSimTimeS = 0.0;
	FVector Vibration = FVector::ZeroVector;
	double Travel[4] = {0.0, 0.0, 0.0, 0.0};
	double TravelSlow = 0.0;
	double Roughness = 0.0;
	double Gaze = 0.0;
	double BrakePitch = 0.0;
	bool bStarted = false;
	FRandomStream Rng;
};

/**
 * Chase and scenic framing. Godot's chase sat at a body-fixed point, so a slide
 * never read on screen. Here the camera rides a yaw-only heading frame swung
 * toward the direction of travel by ChaseSlipShare of the body slip angle
 * beta = atan2(v_lat, |v_long|), clamped to +-35 deg and faded in from 2 to
 * 8 m/s forward, so the car is seen sideways in a drift. Reversing gives no
 * swing, a spin past 90 deg holds the last swing, and one 0.3 s lag smooths it
 * (plan-2026-11-10/15 corrected design (c)). Position offsets match
 * godot-final:game/Drive.gd: chase 5.8 m behind, 1.94 m up, 1.92 m right easing to 0.35 m
 * with speed, looking 3.9 m ahead at 0.70 m; scenic 7.2 m behind, 4.9 m right,
 * 3.45 m up.
 */
class SIMCRUISE_API FSimCruiseChaseFrame
{
public:
	static constexpr double ChaseSlipShare = 0.4;
	static constexpr double MaxBetaRad = 35.0 * UE_DOUBLE_PI / 180.0;
	static constexpr double LagS = 0.3;
	static constexpr double PositionRate = 10.0;

	/** Target swing (rad, positive = camera yaws right) for a body velocity;
	 *  bOutHold true when the car is spinning and the last swing should stay. */
	static double TargetSwingRad(const FVector& BodyVelocityMps, bool& bOutHold);

	void Reset();
	/** Advance and return the desired camera location (cm) and look target (cm)
	 *  before collision; bSnap places it without the position lag. */
	void Update(ESimCruiseView View, const FTransform& Body, const FVector& BodyVelocityMps, double DeltaTimeS, bool bSnap,
		FVector& OutLocation, FVector& OutLookAt);

	double GetSwingRad() const { return Swing; }
	/** After collision moves the camera, keep the smoothed position there. */
	void OverrideLocation(const FVector& Location) { SmoothedOffset = Location - LastPivot; }

private:
	double Swing = 0.0;
	FVector SmoothedOffset = FVector::ZeroVector;
	FVector LastPivot = FVector::ZeroVector;
	bool bHasLocation = false;
};

/** First-order filtered g readout for the HUD's g-meter (body axes, g). */
class SIMCRUISE_API FSimCruiseGMeter
{
public:
	void Reset() { bStarted = false; Value = FVector2D::ZeroVector; }
	/** Returns (longitudinal, lateral) g, positive = accelerating / turning right. */
	/** SimTimeS: the physics time of WorldVelocityMps (0 = unknown: use the frame time). */
	FVector2D Update(const FTransform& Body, const FVector& WorldVelocityMps, double DeltaTimeS, double SimTimeS = 0.0);
	const FVector2D& Get() const { return Value; }

private:
	FVector PreviousVelocity = FVector::ZeroVector;
	FVector LastAcceleration = FVector::ZeroVector;
	double PreviousSimTimeS = 0.0;
	FVector2D Value = FVector2D::ZeroVector;
	bool bStarted = false;
};
