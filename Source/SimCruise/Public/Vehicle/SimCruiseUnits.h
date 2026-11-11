// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The single place where SimCruise's SI vehicle model meets Unreal's units.
 *
 * The vehicle model works in metres, kilograms, seconds, newtons and radians,
 * in Unreal's axes (X forward, Y right, Z up, left-handed). Chaos works in
 * centimetres, kilograms and seconds, so a force is kg·cm/s² and a torque or
 * inertia carries cm². Godot (the calibration reference in game/) is
 * right-handed with Y up and -Z forward; car files store vectors in its axes.
 */
namespace SimCruiseUnits
{
	inline constexpr double CentimetresPerMetre = 100.0;
	inline constexpr double MetresPerCentimetre = 0.01;
	inline constexpr double MetresPerMile = 1609.344;
	inline constexpr double FeetPerMetre = 3.280839895013123;
	inline constexpr double MetresPerSecondPerMph = 0.44704;
	inline constexpr double StandardGravityMps2 = 9.80665;

	/** Length and position: m <-> cm. */
	inline constexpr double ToUnrealLength(double Metres) { return Metres * CentimetresPerMetre; }
	inline constexpr double ToSILength(double Centimetres) { return Centimetres * MetresPerCentimetre; }
	inline FVector3d ToUnrealLength(const FVector3d& Metres) { return Metres * CentimetresPerMetre; }
	inline FVector3d ToSILength(const FVector3d& Centimetres) { return Centimetres * MetresPerCentimetre; }

	/** Velocity and acceleration scale like length. */
	inline FVector3d ToUnrealVelocity(const FVector3d& Mps) { return Mps * CentimetresPerMetre; }
	inline FVector3d ToSIVelocity(const FVector3d& CmPerS) { return CmPerS * MetresPerCentimetre; }

	/** Force: N (kg·m/s²) <-> kg·cm/s². */
	inline constexpr double ToUnrealForce(double Newtons) { return Newtons * CentimetresPerMetre; }
	inline constexpr double ToSIForce(double UnrealForce) { return UnrealForce * MetresPerCentimetre; }
	inline FVector3d ToUnrealForce(const FVector3d& Newtons) { return Newtons * CentimetresPerMetre; }

	/** Torque: N·m (kg·m²/s²) <-> kg·cm²/s². */
	inline constexpr double ToUnrealTorque(double NewtonMetres) { return NewtonMetres * CentimetresPerMetre * CentimetresPerMetre; }
	inline constexpr double ToSITorque(double UnrealTorque) { return UnrealTorque * MetresPerCentimetre * MetresPerCentimetre; }
	inline FVector3d ToUnrealTorque(const FVector3d& NewtonMetres) { return NewtonMetres * (CentimetresPerMetre * CentimetresPerMetre); }

	/** Moment of inertia: kg·m² <-> kg·cm². */
	inline constexpr double ToUnrealInertia(double KgM2) { return KgM2 * CentimetresPerMetre * CentimetresPerMetre; }
	inline constexpr double ToSIInertia(double KgCm2) { return KgCm2 * MetresPerCentimetre * MetresPerCentimetre; }

	/** Angles: Unreal rotators use degrees, the model radians. */
	inline constexpr double ToRadians(double Degrees) { return Degrees * (UE_DOUBLE_PI / 180.0); }
	inline constexpr double ToDegrees(double Radians) { return Radians * (180.0 / UE_DOUBLE_PI); }

	/** Road-test units. */
	inline constexpr double MphToMps(double Mph) { return Mph * MetresPerSecondPerMph; }
	inline constexpr double MpsToMph(double Mps) { return Mps / MetresPerSecondPerMph; }
	inline constexpr double MetresToFeet(double Metres) { return Metres * FeetPerMetre; }

	/**
	 * A Godot vector (X right, Y up, -Z forward) in Unreal axes (X forward,
	 * Y right, Z up). The mapping is a reflection plus a rotation, so it also
	 * flips handedness, which is what Unreal's left-handed frame needs.
	 */
	inline FVector3d GodotToUnrealAxes(const FVector3d& Godot) { return FVector3d(-Godot.Z, Godot.X, Godot.Y); }
	inline FVector3d UnrealToGodotAxes(const FVector3d& Unreal) { return FVector3d(Unreal.Y, Unreal.Z, -Unreal.X); }

	/**
	 * Principal moments stored as Godot's (about X right = pitch, Y up = yaw,
	 * Z back = roll) as Unreal's (about X forward = roll, Y right = pitch,
	 * Z up = yaw). Moments are magnitudes, so no sign changes.
	 */
	inline FVector3d GodotInertiaToUnrealAxes(const FVector3d& GodotMoments) { return FVector3d(GodotMoments.Z, GodotMoments.X, GodotMoments.Y); }
}
