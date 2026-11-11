// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

/**
 * Car mirrors that show the road behind. One low-resolution capture looks back
 * from the driver's head with a wide lens; the mirror material reflects the
 * view ray off each mirror pixel and looks that direction up in the capture,
 * so the rear-view and both door mirrors share it (parallax is small for what a
 * mirror shows: the street metres and more behind). Without a capture (the
 * turntable, the Low preset) the glass shows a plain sky-grey.
 */
namespace SimCruiseCarMirror
{
	/** The mirror material (editor builds make it in memory; null without one). */
	SIMCRUISE_API UMaterialInterface* Get();
}
