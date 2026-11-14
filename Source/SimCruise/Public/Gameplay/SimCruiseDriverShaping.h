// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruiseVehicleControl.h"

/**
 * Driver-side input shaping: what a person's hands and feet do between the
 * controller and the car's controls. Nothing here touches the contact forces,
 * yaw inertia or grip; it only decides which rack angle and pedal positions a
 * thumb or a key asks for, and how fast a hand can move the wheel.
 *
 * Ported from godot-final:game/Powertrain.gd (available_steering, thumbstick_command,
 * advance_steering, key_rack_rate) with the audited corrections of
 * docs/research/plan-2026-11-10/04-pad-steering-pedals.md:
 *  - the rack is a state in radians slewed in rad/s, not a fraction of a cap
 *    that moves with speed, so a closing cap never makes the rack jump;
 *  - countersteer allowance keyed on the front axle's velocity angle beta_f
 *    alone (no yaw-rate state machine): toward sign(beta_f) the range opens to
 *    |beta_f| + the limit slip surplus, never past the rack lock, and a hand
 *    may move the rack that way at up to 40 deg/s;
 *  - releasing the stick still returns to exactly zero (no self-centring aid);
 *  - key pedals ramp (a foot is not a switch);
 *  - the right trigger follows a power curve (simcruise.TriggerGamma, default
 *    1.8): a trigger has about 1 cm of travel against a pedal's 6 cm, so the
 *    first half of the trigger gives the fine part-throttle a foot has. Full
 *    trigger is still full throttle. The brake trigger stays linear.
 */
namespace SimCruiseShaping
{
	constexpr double StandardGravity = 9.80665;
	constexpr double LockBlendRad = 3.0 * UE_DOUBLE_PI / 180.0;
	constexpr double SteerDeadBand = 0.01;
	constexpr double PadApplyRatePerS = 2.75;   // fractions of the available rack per second
	constexpr double PadReturnRatePerS = 4.4;
	constexpr double KeyRackRateHighwayRadPerS = 12.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyReturnRateMinRadPerS = 24.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyReturnFactor = 1.6;
	constexpr double PadCentreCorneringG = 0.89;
	constexpr double CountersteerRateRadPerS = 40.0 * UE_DOUBLE_PI / 180.0;
	constexpr double CountersteerMinBetaRad = 2.0 * UE_DOUBLE_PI / 180.0;
	constexpr double BetaMinForwardSpeedMps = 5.0;
	// The hand on the rim: how fast a driver turns a real steering wheel (deg/s of the rim). Parking
	// and slow corners take it up to about 450 deg/s (a turn and a quarter a second, hand over hand),
	// at speed about 300; letting the wheel unwind through the hands is quicker (600 / 400), and a
	// slide is caught at up to 700. NHTSA's test drivers reached 963 deg/s sustained for one second
	// in emergency manoeuvres (ESV 2005 paper on steering inputs), the ceiling none of these pass.
	constexpr double HandApplyParkingDegPerS = 450.0;
	constexpr double HandApplyHighwayDegPerS = 300.0;
	constexpr double HandReturnParkingDegPerS = 600.0;
	constexpr double HandReturnHighwayDegPerS = 400.0;
	constexpr double HandCatchDegPerS = 700.0;

	/** Quilez polynomial smooth minimum (Powertrain.smooth_min). */
	SIMCRUISE_API double SmoothMin(double A, double B, double K);

	/** Rack angle a full press asks for at this speed (Assisted map): the
	 *  Ackermann angle for the assist lateral g plus the slip surplus at the
	 *  limit, blended into the parking lock. */
	SIMCRUISE_API double AvailableSteeringRad(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps);

	/** Front-axle velocity angle relative to the body (rad, positive = the front
	 *  axle moving to the right). Zero below 5 m/s forward and in reverse,
	 *  where the angle is noise or flips sign. */
	SIMCRUISE_API double FrontAxleSlipAngleRad(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps, double LateralSpeedMps, double YawRateRadPerS);

	/** Range toward one side: Direction +1 right, -1 left. */
	SIMCRUISE_API double RangeTowardRad(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps, double BetaFrontRad, double Direction);

	/** Stick-to-command: the centre of the stick's travel goes to ordinary
	 *  lane corrections, the ends still reach the full range. Returns -1..1. */
	SIMCRUISE_API double ThumbstickCommand(const FSimCruiseVehicleSetup& Setup, double Stick, double ForwardSpeedMps);

	/** How fast a held key moves the rack, rad/s. */
	SIMCRUISE_API double KeyRackRateRadPerS(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps);

	/** Move Current toward Target, unwinding through centre first at ReturnRate
	 *  and spending only the remaining time at ApplyRate (Powertrain.advance_steering). */
	SIMCRUISE_API double AdvanceSteering(double Current, double Target, double DeltaTimeS, double ApplyRate, double ReturnRate);

	/** A key held (Pressed) or released moves the pedal at a foot's pace. */
	SIMCRUISE_API double KeyPedal(double Current, bool bPressed, double RisePerS, double DeltaTimeS);

	/** A dead zone at the stick's centre (0..0.25 of its travel), the rest rescaled to reach full lock. */
	SIMCRUISE_API double StickDeadZone(double Stick, double DeadZone);

	/** Right trigger (0..1) to accelerator pedal (0..1): Trigger^Gamma, Gamma clamped to 1..3. */
	SIMCRUISE_API double TriggerThrottle(double Trigger, double Gamma);
	constexpr double DefaultTriggerGamma = 1.8;
	constexpr double KeyThrottleRisePerS = 3.0;
	constexpr double KeyBrakeRisePerS = 2.5;
	constexpr double KeyPedalReleasePerS = 8.0;
	constexpr double ClutchPressPerS = 14.0;
	constexpr double ClutchReleasePerS = 2.8;
}

/** Everything the shell knows about the controls in one frame. */
struct FSimCruiseRawControls
{
	double PadSteer = 0.0;      // left stick X after its dead zone, -1..1
	double KeySteer = 0.0;      // -1, 0 or 1
	double PadThrottle = 0.0;   // right trigger after its dead zone
	double PadBrake = 0.0;      // left trigger
	bool bKeyThrottle = false;
	bool bKeyBrake = false;
	bool bFullServiceBrake = false;
	bool bParkingBrake = false;
	bool bClutch = false;
	/**
	 * A USB racing wheel (FSimCruiseWheelDevice): its rim angle turned into the rack angle at
	 * the car's real steering ratio, applied 1:1 with no speed shaping or filtering, and its
	 * pedals as they are (no trigger curve). Wheel pedals add to the pad's and keys'.
	 */
	bool bWheel = false;
	double WheelRackRad = 0.0;
	double WheelThrottle = 0.0;
	double WheelBrake = 0.0;
	/** The wheel's clutch pedal, used when the wheel has one (bWheelClutch). */
	bool bWheelClutch = false;
	double WheelClutch = 0.0;
};

/** Stateful steering rack and pedal positions for one driver. */
class SIMCRUISE_API FSimCruiseDriverShaper
{
public:
	void Reset();

	/** Advance by DeltaTimeS and return the command for the vehicle. Telemetry
	 *  provides speed, lateral speed and yaw rate for the speed-aware limits. */
	FSimCruiseDriverCommand Update(const FSimCruiseVehicleSetup& Setup, const FSimCruiseDriveTelemetry& Telemetry,
		const FSimCruiseRawControls& Raw, double DeltaTimeS);

	double GetRackAngleRad() const { return RackRad; }
	bool IsUsingThumbstick() const { return bUsingThumbstick; }
	/** Last range toward each side (rad), for a telemetry readout. */
	double GetRangeLeftRad() const { return RangeLeft; }
	double GetRangeRightRad() const { return RangeRight; }

private:
	double RackRad = 0.0;
	double KeyThrottle = 0.0;
	double KeyBrake = 0.0;
	double Clutch = 0.0;
	double RangeLeft = 0.0;
	double RangeRight = 0.0;
	bool bUsingThumbstick = false;
};
