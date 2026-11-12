// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace SimCruiseTileDetail { struct FContext; }

/**
 * Street furniture on a tile (Props layer), at real size: what OpenStreetMap maps where it
 * maps it (benches, litter bins, bicycle racks, USPS collection boxes, fire hydrants,
 * bollards, bus stop flags and shelters, picnic tables; SimCruiseFurniture data), moved
 * behind the kerb when mapped in the road, and fire hydrants by rule where the map has
 * none (every 300 ft along curbed streets). Colours follow the place knowledge graph
 * (hydrant:*, bench:*, bin:*, bollard:*, street_metal:* facts of the region), or an
 * object's own OSM colour.
 */
namespace SimCruiseStreetFurniture
{
	struct FStats
	{
		int32 Mapped = 0;
		int32 Inferred = 0;
	};

	void Build(const SimCruiseTileDetail::FContext& Context, FStats* OutStats = nullptr);
	bool IsInsideBuilding(const SimCruiseTileDetail::FContext& Context, const FVector2d& P);
}
