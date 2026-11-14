// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The steering column to the rack: the hand wheel's angle for a road-wheel (rack) angle.
 *
 * A road car's overall ratio is quoted on centre (the NB's 15:1), but its lock-to-lock turns
 * often take less rim than that ratio would: the linkage (and a variable-ratio rack, as the C8's)
 * quickens toward lock. With both figures the column is theta = R d - c d^3: the quoted ratio R
 * on centre, the quoted turns at the lock d_L (c from theta(d_L) = turns x 180 deg), kept
 * monotonic. Without a lock-to-lock figure (or when it is slower than R) the ratio is constant.
 */
namespace SimCruiseSteering
{
	/** The cubic's coefficient (rad of rim per rad^3 of rack); 0 for a constant ratio. */
	inline double Quickening(double Ratio, double LockRad, double LockTurns)
	{
		if (Ratio <= 0.0 || LockRad <= 0.0 || LockTurns <= 0.0)
		{
			return 0.0;
		}
		const double RimAtLock = LockTurns * UE_DOUBLE_PI;
		const double C = (Ratio * LockRad - RimAtLock) / (LockRad * LockRad * LockRad);
		// Keep the column turning the same way at the lock (local ratio at least a third of R).
		return FMath::Clamp(C, 0.0, 2.0 * Ratio / (9.0 * LockRad * LockRad));
	}

	/** The hand wheel's angle (rad) for a rack angle (rad, mean road-wheel angle). */
	inline double HandWheelRad(double RackRad, double Ratio, double LockRad, double LockTurns)
	{
		const double C = Quickening(Ratio, LockRad, LockTurns);
		if (C <= 0.0)
		{
			return RackRad * Ratio;
		}
		const double D = FMath::Clamp(RackRad, -LockRad, LockRad);
		const double Beyond = RackRad - D;
		const double SlopeAtLock = Ratio - 3.0 * C * LockRad * LockRad;
		return Ratio * D - C * D * D * D + Beyond * SlopeAtLock;
	}

	/** The local ratio (rim per road wheel) at a rack angle: R on centre, quicker toward lock. */
	inline double LocalRatio(double RackRad, double Ratio, double LockRad, double LockTurns)
	{
		const double C = Quickening(Ratio, LockRad, LockTurns);
		const double D = FMath::Min(FMath::Abs(RackRad), LockRad > 0.0 ? LockRad : FMath::Abs(RackRad));
		return FMath::Max(Ratio - 3.0 * C * D * D, 1.0);
	}

	/** The rack angle a hand-wheel angle gives (the inverse, for a USB wheel's rim). */
	inline double RackRadForHandWheel(double HandRad, double Ratio, double LockRad, double LockTurns)
	{
		if (Ratio <= 0.0)
		{
			return 0.0;
		}
		double D = HandRad / Ratio;
		if (Quickening(Ratio, LockRad, LockTurns) <= 0.0)
		{
			return D;
		}
		for (int32 Iteration = 0; Iteration < 8; ++Iteration)
		{
			D -= (HandWheelRad(D, Ratio, LockRad, LockTurns) - HandRad) / LocalRatio(D, Ratio, LockRad, LockTurns);
		}
		return D;
	}
}
