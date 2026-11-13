// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseSpeedUnits.h"

#include "Gameplay/SimCruisePreferences.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSpeedUnits(
		TEXT("simcruise.Hud.Units"), 0,
		TEXT("Speed units on the HUD and the GPS: 0 mph and miles (default), 1 km/h and kilometres."),
		ECVF_Default);

	const TCHAR* PreferenceKey = TEXT("speed_units");
}

namespace SimCruiseSpeedUnits
{
	ESimCruiseSpeedUnits Get()
	{
		return CVarSpeedUnits.GetValueOnGameThread() == 1 ? ESimCruiseSpeedUnits::Kmh : ESimCruiseSpeedUnits::Mph;
	}

	void Set(ESimCruiseSpeedUnits Units)
	{
		// At console priority, so the menu's choice also wins over a value given on the command line.
		CVarSpeedUnits->Set(static_cast<int32>(Units), ECVF_SetByConsole);
	}

	ESimCruiseSpeedUnits Next(ESimCruiseSpeedUnits Units)
	{
		return Units == ESimCruiseSpeedUnits::Mph ? ESimCruiseSpeedUnits::Kmh : ESimCruiseSpeedUnits::Mph;
	}

	void LoadPreference()
	{
		// Without preferences (checks, captures) the console variable keeps what the command line set.
		if (!SimCruisePreferences::IsEnabled()) return;
		const FString Value = SimCruisePreferences::GetString(PreferenceKey, TEXT("mph"));
		Set(Value.Equals(TEXT("kmh"), ESearchCase::IgnoreCase) ? ESimCruiseSpeedUnits::Kmh : ESimCruiseSpeedUnits::Mph);
	}

	void Toggle()
	{
		const ESimCruiseSpeedUnits Units = Next(Get());
		Set(Units);
		SimCruisePreferences::SetString(PreferenceKey, Units == ESimCruiseSpeedUnits::Kmh ? TEXT("kmh") : TEXT("mph"));
	}

	double FromMps(double Mps, ESimCruiseSpeedUnits Units)
	{
		return Mps * (Units == ESimCruiseSpeedUnits::Kmh ? KmhPerMps : MphPerMps);
	}

	int32 Shown(double Mps, ESimCruiseSpeedUnits Units)
	{
		if (!FMath::IsFinite(Mps)) return 0;
		return FMath::Max(0, FMath::RoundToInt(FMath::Abs(FromMps(Mps, Units))));
	}

	const TCHAR* Label(ESimCruiseSpeedUnits Units)
	{
		return Units == ESimCruiseSpeedUnits::Kmh ? TEXT("km/h") : TEXT("mph");
	}

	int32 ShownRpm(double Rpm)
	{
		if (!FMath::IsFinite(Rpm)) return 0;
		return FMath::Max(0, FMath::RoundToInt(Rpm / 50.0) * 50);
	}
}
