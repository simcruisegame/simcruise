// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCruiseRigSection;

/**
 * A wheel design the model does not have, built from numbers: a multi-piece cross-spoke
 * ("mesh") wheel with a polished lip, as on many aftermarket NB Miata wheels, and its tyre.
 */
struct FSimCruiseWheelDesign
{
	/** "mesh" is the one style so far. */
	FString Style;
	double RimDiameterIn = 15.0;
	double RimWidthIn = 8.0;
	/** Radial width of the polished lip on the face, inches (a deep-dish look). */
	double LipIn = 2.0;
	/** How far the spoke face sits inside the lip, inches. */
	double DishIn = 1.6;
	/** Spoke pairs round the wheel (each pair crosses its neighbours: the mesh). */
	int32 Spokes = 10;
	FString Source;
	bool IsSet() const { return !Style.IsEmpty(); }
};

namespace SimCruiseWheelBuilder
{
	/** Material indices for each surface of the wheel (any may be INDEX_NONE to leave it out). */
	struct FMaterials
	{
		int32 Face = INDEX_NONE;
		int32 Lip = INDEX_NONE;
		int32 Tyre = INDEX_NONE;
		int32 Cap = INDEX_NONE;
	};

	/**
	 * The wheel and tyre in a wheel part's frame: origin at the hub, axle along Y, centimetres;
	 * OutboardSign -1 for the left wheels (the face toward -Y), +1 for the right.
	 */
	SIMCRUISE_API void BuildMeshWheel(const FSimCruiseWheelDesign& Design, double TyreRadiusCm, double TyreWidthCm, double OutboardSign,
		const FMaterials& Materials, TArray<FSimCruiseRigSection>& OutSections);
}
