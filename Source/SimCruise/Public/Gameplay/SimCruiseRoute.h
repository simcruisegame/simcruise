// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

/**
 * The GPS route: stops, the legs between them and the turns along each leg,
 * as the map helper's `route` command returns them (helper/route.mjs), and the
 * car's progress along the active leg.
 *
 * The model has no world: the legs are in longitude and latitude, and the
 * caller gives a projection to local metres (the drive's world frame) with
 * Project(). Progress, the distance to the next turn, off-route and arrival
 * then follow from the car's position in the same metres. Automation tests
 * drive it with a flat projection.
 */

enum class ESimCruiseTurn : uint8
{
	Depart,
	Continue,
	SlightRight,
	Right,
	SharpRight,
	SlightLeft,
	Left,
	SharpLeft,
	UTurn,
	Arrive,
};

namespace SimCruiseRouteText
{
	/** The helper's step kind ("slight right", ...) as a turn. */
	SIMCRUISE_API ESimCruiseTurn ParseTurn(const FString& Kind);
	/** "Right", "Slight left", "Make a U-turn", "Continue", "Arrive". */
	SIMCRUISE_API FString TurnVerb(ESimCruiseTurn Turn);
	/** "Right onto 5th Street", "Continue on Collins Avenue", "Turn right" when the street has no name. */
	SIMCRUISE_API FString Instruction(ESimCruiseTurn Turn, const FString& Street);
	/** US road distance: feet under 0.1 mi (rounded to 10 ft, 50 ft from 500 ft), else miles. */
	SIMCRUISE_API FString Distance(double Metres);
	/** Split as number and unit ("0.3", "mi") for the large HUD readout. */
	SIMCRUISE_API void DistanceParts(double Metres, FString& OutNumber, FString& OutUnit);
	/** "4 min", "1 h 12 min". */
	SIMCRUISE_API FString Duration(double Seconds);
	/** A 12-hour clock: "6:51 PM". */
	SIMCRUISE_API FString Clock(const FDateTime& Local);
}

struct SIMCRUISE_API FSimCruiseRouteStop
{
	FString Title;
	double Lon = 0.0;
	double Lat = 0.0;
};

struct SIMCRUISE_API FSimCruiseRouteStep
{
	ESimCruiseTurn Turn = ESimCruiseTurn::Continue;
	/** Heading change, degrees, + to the right. */
	double AngleDeg = 0.0;
	/** The street the step turns onto. */
	FString Street;
	/** Distance along the leg, metres. */
	double AtM = 0.0;
	/** Index of the leg point where the step is. */
	int32 Index = 0;
};

struct SIMCRUISE_API FSimCruiseRouteLeg
{
	/** Longitude, latitude of each point. */
	TArray<FVector2D> LonLat;
	/** The street of the segment that starts at each point (one fewer than points). */
	TArray<FString> Streets;
	TArray<FSimCruiseRouteStep> Steps;
	double LengthM = 0.0;
	double DurationS = 0.0;

	/** Filled by Project(): points in local metres and the distance to each from the start. */
	TArray<FVector2D> M;
	TArray<double> CumM;
};

/** The car's place on the route this frame. */
struct SIMCRUISE_API FSimCruiseRouteProgress
{
	/** Distance along the active leg, metres. */
	double AlongM = 0.0;
	/** Signed distance from the route line, metres, + right of the direction of travel. */
	double LateralM = 0.0;
	/** The next step that is not behind the car (a turn, or the arrival), -1 when none. */
	int32 NextStep = -1;
	double ToNextStepM = 0.0;
	double LegRemainingM = 0.0;
	double LegRemainingS = 0.0;
	/** The car has been more than OffRouteM from the line for OffRouteSeconds: ask for a new route. */
	bool bOffRoute = false;
};

DECLARE_DELEGATE_RetVal_TwoParams(FVector2D, FSimCruiseLonLatToM, double /*Lon*/, double /*Lat*/);

class SIMCRUISE_API FSimCruiseRoute
{
public:
	static constexpr double OffRouteM = 30.0;
	static constexpr double OffRouteSeconds = 3.0;
	/** The car is at a stop within this distance of the leg's end. */
	static constexpr double ArriveM = 25.0;
	static constexpr double ArrivalMessageSeconds = 4.0;

	/** Read the helper's `route` document. Stops are the route's stops; the first is where it starts. */
	bool ParseHelperJson(const TSharedPtr<FJsonValue>& Json, const TArray<FSimCruiseRouteStop>& InStops, FString* OutError = nullptr);
	bool ParseHelperString(const FString& Raw, const TArray<FSimCruiseRouteStop>& InStops, FString* OutError = nullptr);

	/** Build the local-metre points of every leg (call again after the world frame changes). */
	void Project(const FSimCruiseLonLatToM& ToMetres);

	bool IsActive() const { return Legs.Num() > 0 && ActiveLeg < Legs.Num(); }
	void Clear();

	/**
	 * Follow the car (local metres, heading as a unit direction). Returns the
	 * progress; on arrival at a stop the next leg becomes active and
	 * GetArrivalMessage() names the stop for four seconds.
	 */
	const FSimCruiseRouteProgress& Update(const FVector2D& CarM, double DeltaS);

	const FSimCruiseRouteProgress& GetProgress() const { return Progress; }
	int32 GetActiveLeg() const { return ActiveLeg; }
	const TArray<FSimCruiseRouteLeg>& GetLegs() const { return Legs; }
	const TArray<FSimCruiseRouteStop>& GetStops() const { return Stops; }
	const FSimCruiseRouteLeg* GetLeg() const { return IsActive() ? &Legs[ActiveLeg] : nullptr; }
	/** The stop the active leg goes to. */
	const FSimCruiseRouteStop* GetNextStop() const { return IsActive() && Stops.IsValidIndex(ActiveLeg + 1) ? &Stops[ActiveLeg + 1] : nullptr; }
	const FSimCruiseRouteStep* GetStep(int32 Index) const;
	/** The step after the next one, when it comes soon after it (the "Then" part of the sign). */
	const FSimCruiseRouteStep* GetThenStep(double WithinM = 500.0) const;
	/** "You are at <stop>" while the arrival message lasts, else empty. */
	FString GetArrivalMessage() const { return ArrivalTime > 0.0 ? ArrivalText : FString(); }
	bool IsFinished() const { return bFinished; }

	/** Point and unit direction on the active leg at a distance along it (clamped to the leg). */
	bool PointAt(double AlongM, FVector2D& OutM, FVector2D& OutDir) const;
	/** The remaining stops from the active leg's target on (for a new route from the car). */
	TArray<FSimCruiseRouteStop> RemainingStops() const;

	double TotalLengthM() const;
	double TotalDurationS() const;
	/** Lowered by the caller once it has asked for a new route. */
	void ResetOffRoute() { OffTimer = 0.0; Progress.bOffRoute = false; }

private:
	/** Nearest point on the active leg to the car, searched near the last place first. */
	void Locate(const FSimCruiseRouteLeg& Leg, const FVector2D& CarM, double& OutAlong, double& OutLateral, int32& OutSegment) const;

	TArray<FSimCruiseRouteStop> Stops;
	TArray<FSimCruiseRouteLeg> Legs;
	int32 ActiveLeg = 0;
	int32 LastSegment = 0;
	double OffTimer = 0.0;
	double ArrivalTime = 0.0;
	FString ArrivalText;
	bool bFinished = false;
	FSimCruiseRouteProgress Progress;
};
