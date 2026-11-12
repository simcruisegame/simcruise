// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Vehicle/CarDefinition.h"

/**
 * A driver's setup changes on top of the real car, in the units a workshop uses. Applied to a
 * copy of the car's definition (the car file never changes) and saved per car in
 * Saved/SimCruise/tuning-<car id>.json. Each value has a safe range around the real car's.
 */
struct SIMCRUISE_API FSimCruiseTuning
{
	/** Coil spring rates (lb/in), front and rear. */
	double FrontSpringLbIn = 0.0;
	double RearSpringLbIn = 0.0;
	/** Damper force, % of the real car's at every shaft speed. */
	double DampingPct = 100.0;
	/** Ride height change (mm, + higher). */
	double RideHeightMm = 0.0;
	/** Anti-roll bar rates, % of the real car's (0 = disconnected). */
	double FrontBarPct = 100.0;
	double RearBarPct = 100.0;
	/** Engine (or motor) torque, % of the real car's across the curve. */
	double TorquePct = 100.0;
	/** Final drive ratio. */
	double FinalDrive = 0.0;
	/** Steering ratio (steering-wheel turns per road-wheel turn, :1); 0 when the car has none. */
	double SteeringRatio = 0.0;

	/** The real car's values (the "reset" state). StockSteeringRatio comes from the car's model. */
	static FSimCruiseTuning FromCar(const FSimCruiseCarDefinition& Car, double StockSteeringRatio);

	/** Keep every value inside its safe range around the real car's. */
	void Clamp(const FSimCruiseTuning& Stock);

	/** The tuned car: Stock is the real car's tuning (FromCar), Real the real car's definition. */
	FSimCruiseCarDefinition Apply(const FSimCruiseCarDefinition& Real, const FSimCruiseTuning& Stock) const;

	/** Body roll per g of lateral acceleration (deg/g) from the springs, bars, track and centre of
	 *  mass height (the sim's roll axis runs through the contact patches). */
	static double RollGradientDegPerG(const FSimCruiseCarDefinition& Car);

	bool Load(const FString& CarId);
	bool Save(const FString& CarId) const;
	static FString PathFor(const FString& CarId);
};
