// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

/** One frame of a USB racing wheel (steering wheel, pedals, paddles), read through Windows GameInput. */
struct FSimCruiseWheelReading
{
	/** Wheel rim position, -1 (full lock left) to 1 (full lock right). */
	double Wheel = 0.0;
	/** The wheel's rotation from centre to either lock, degrees (450 for a 900-degree wheel). */
	double HalfRotationDeg = 450.0;
	double Throttle = 0.0;
	double Brake = 0.0;
	double Clutch = 0.0;
	double Handbrake = 0.0;
	/** Paddles or sequential shifter: true on the frame a press starts. */
	bool bNextGear = false;
	bool bPreviousGear = false;
	/** H-pattern shifter gear (-1 reverse, 0 neutral, 1..), when the wheel has one. */
	bool bHasPatternShifter = false;
	int32 PatternShifterGear = 0;
	bool bHasClutch = false;
};

/**
 * The first racing wheel GameInput reports (Logitech, Thrustmaster, Fanatec and other wheels with
 * a GameInput or Xbox-compatible driver). It reads the wheel and its pedals directly, so the rim
 * turns the car's steering 1:1 at its real ratio, and drives the wheel's force-feedback motor: a
 * centring spring and a damper whose strength the caller sets each frame, plus a constant force
 * (the tyres' aligning torque, once the physics provides it). Without GameInput (other
 * platforms, or no wheel connected) Poll returns false and nothing happens.
 */
class SIMCRUISE_API FSimCruiseWheelDevice
{
public:
	FSimCruiseWheelDevice();
	~FSimCruiseWheelDevice();

	/** Read the wheel; false when none is connected. */
	bool Poll(FSimCruiseWheelReading& Out);

	/**
	 * Force feedback, each 0..1 of the motor's strength: Spring pulls the rim to centre in
	 * proportion to its angle, Damper resists its speed, Constant pushes it (+ to the right).
	 */
	void SetForces(double Spring, double Damper, double Constant);

	/** Stop every effect (paused game, menus, no car). */
	void Stop();

	/** A one-line description of the wheel for the log and the HUD; empty with none. */
	const FString& GetName() const { return Name; }

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
	FString Name;
};
