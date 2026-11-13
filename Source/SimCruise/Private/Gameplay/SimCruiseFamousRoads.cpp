// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseFamousRoads.h"

namespace SimCruiseFamousRoads
{
	namespace
	{
		FSimCruiseRouteStop Stop(const TCHAR* Title, double Lon, double Lat)
		{
			FSimCruiseRouteStop S;
			S.Title = Title;
			S.Lon = Lon;
			S.Lat = Lat;
			return S;
		}

		FSimCruiseFamousRoad Road(const TCHAR* Title, const TCHAR* Note, const FSimCruisePlace& Start, TArray<FSimCruiseRouteStop> Stops, double Miles)
		{
			FSimCruiseFamousRoad R;
			R.Title = Title;
			R.Note = Note;
			R.Start = Start;
			R.Stops = MoveTemp(Stops);
			R.LengthMi = Miles;
			return R;
		}
	}

	const TArray<FSimCruiseFamousRoad>& All()
	{
		// Lengths are the router's (helper/route.mjs) on the OpenStreetMap roads, November 2026.
		static const TArray<FSimCruiseFamousRoad> List = {
			Road(TEXT("Ocean Drive, Miami Beach"), TEXT("Art Deco hotels and the beach, from Lummus Park to Collins Park"),
				SimCruisePlaces::Fallback(),
				{ Stop(TEXT("Collins Park"), -80.1290, 25.7930) }, 1.3),
			Road(TEXT("Tail of the Dragon, US 129"), TEXT("318 curves in 11 miles, from Deals Gap to the Tabcat Creek bridge"),
				FSimCruisePlace::Make(-83.9208, 35.4740, TEXT("Deals Gap · US 129, NC/TN")),
				{ Stop(TEXT("Tabcat Creek bridge"), -83.98870, 35.52013) }, 10.8),
			Road(TEXT("Big Sur, California 1"), TEXT("Cliffs over the Pacific, from Bixby Creek Bridge to Big Sur"),
				FSimCruisePlace::Make(-121.9020, 36.3716, TEXT("Bixby Creek Bridge · CA 1"), TEXT("Cabrillo Highway")),
				{ Stop(TEXT("Big Sur"), -121.7838, 36.2465) }, 13.2),
			Road(TEXT("Monument Valley, US 163"), TEXT("The straight road to the buttes, from Forrest Gump Point"),
				FSimCruisePlace::Make(-109.990814, 37.101444, TEXT("Forrest Gump Point · US 163, UT"), FString(), 230.0),
				{ Stop(TEXT("Monument Valley"), -110.08301, 37.05853) }, 6.2),
		};
		return List;
	}
}
