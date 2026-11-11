// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/CarDefinition.h"

double FSimCruiseCarDefinition::TireFreeRadiusM() const
{
	return (RimDiameterIn * 0.0254 * 0.5) + (TireSectionWidthM * TireAspectRatio);
}

bool FSimCruiseCarDefinition::IsDrivenWheel(const int32 WheelIndex) const
{
	if (WheelIndex < 0 || WheelIndex > 3)
	{
		return false;
	}
	if (DrivetrainLayout == TEXT("awd"))
	{
		return true;
	}
	return DrivetrainLayout == TEXT("fwd") ? WheelIndex < 2 : WheelIndex > 1;
}
