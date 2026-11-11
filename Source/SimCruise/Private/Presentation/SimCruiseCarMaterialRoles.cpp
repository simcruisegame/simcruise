// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarMaterialRoles.h"

#include "Internationalization/Regex.h"

namespace
{
	struct FRoleName
	{
		ESimCruiseCarMaterialRole Role;
		const TCHAR* Name;
	};

	const FRoleName GRoleNames[] = {
		{ESimCruiseCarMaterialRole::Authored, TEXT("authored")},
		{ESimCruiseCarMaterialRole::Paint, TEXT("paint")},
		{ESimCruiseCarMaterialRole::Glass, TEXT("glass")},
		{ESimCruiseCarMaterialRole::GlassDark, TEXT("glass_dark")},
		{ESimCruiseCarMaterialRole::Chrome, TEXT("chrome")},
		{ESimCruiseCarMaterialRole::BareMetal, TEXT("bare_metal")},
		{ESimCruiseCarMaterialRole::WheelAlloy, TEXT("wheel_alloy")},
		{ESimCruiseCarMaterialRole::Tyre, TEXT("tyre")},
		{ESimCruiseCarMaterialRole::BrakeDisc, TEXT("brake_disc")},
		{ESimCruiseCarMaterialRole::BrakeCaliper, TEXT("brake_caliper")},
		{ESimCruiseCarMaterialRole::PlasticGrained, TEXT("plastic_grained")},
		{ESimCruiseCarMaterialRole::TrimGloss, TEXT("trim_gloss")},
		{ESimCruiseCarMaterialRole::Rubber, TEXT("rubber")},
		{ESimCruiseCarMaterialRole::LampRear, TEXT("lamp_rear")},
		{ESimCruiseCarMaterialRole::LampFront, TEXT("lamp_front")},
		{ESimCruiseCarMaterialRole::LampAmber, TEXT("lamp_amber")},
		{ESimCruiseCarMaterialRole::MirrorGlass, TEXT("mirror_glass")},
		{ESimCruiseCarMaterialRole::InteriorPlastic, TEXT("interior_plastic")},
		{ESimCruiseCarMaterialRole::Cloth, TEXT("cloth")},
		{ESimCruiseCarMaterialRole::Leather, TEXT("leather")},
		{ESimCruiseCarMaterialRole::Carpet, TEXT("carpet")},
		{ESimCruiseCarMaterialRole::Gauge, TEXT("gauge")},
		{ESimCruiseCarMaterialRole::Plate, TEXT("plate")},
		{ESimCruiseCarMaterialRole::Underbody, TEXT("underbody")},
		{ESimCruiseCarMaterialRole::Hidden, TEXT("hidden")},
	};
	static_assert(UE_ARRAY_COUNT(GRoleNames) == static_cast<int32>(ESimCruiseCarMaterialRole::Count), "every role needs a name");

	bool Matches(const FString& Text, const TCHAR* Pattern)
	{
		const FRegexPattern Regex(Pattern, ERegexPatternFlags::CaseInsensitive);
		FRegexMatcher Matcher(Regex, Text);
		return Matcher.FindNext();
	}
}

const TCHAR* SimCruiseCarMaterialRoles::ToString(ESimCruiseCarMaterialRole Role)
{
	const int32 Index = static_cast<int32>(Role);
	return Index >= 0 && Index < UE_ARRAY_COUNT(GRoleNames) ? GRoleNames[Index].Name : TEXT("authored");
}

bool SimCruiseCarMaterialRoles::FromString(const FString& Name, ESimCruiseCarMaterialRole& OutRole)
{
	for (const FRoleName& Entry : GRoleNames)
	{
		if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
		{
			OutRole = Entry.Role;
			return true;
		}
	}
	return false;
}

bool SimCruiseCarMaterialRoles::IsBadgeLabel(const FString& Label)
{
	// godot-final:game/ModelRig.gd (cars/rig-infra) BADGE_PATTERN.
	return Matches(Label, TEXT("badge|badging|logo|emblem|brand|invisible|unimportant"));
}

ESimCruiseCarMaterialRole SimCruiseCarMaterialRoles::Guess(const FString& MaterialName, const FString& OwnerLabel)
{
	const FString Name = MaterialName.ToLower();
	using R = ESimCruiseCarMaterialRole;
	// Order matters: the most specific patterns first.
	if (IsBadgeLabel(Name)) return R::Hidden;
	if (Matches(Name, TEXT("brake.?light|taillight|tail.?light|rear.?light|stop.?lamp|redglass|red.?glass"))) return R::LampRear;
	if (Matches(Name, TEXT("indicator|turn.?light|amber|orangeglass|orange.?glass|blinker"))) return R::LampAmber;
	if (Matches(Name, TEXT("headlight|head.?lamp|day.?light|drl|front.?light"))) return R::LampFront;
	if (Matches(Name, TEXT("dark.*glass|privacy|tinted"))) return R::GlassDark;
	if (Matches(Name, TEXT("windshield|windscreen|glass|window"))) return R::Glass;
	if (Matches(Name, TEXT("^mirror$|mirror.?glass"))) return R::MirrorGlass;
	if (Matches(Name, TEXT("tire|tyre"))) return R::Tyre;
	if (Matches(Name, TEXT("brake.?dis[ck]|rotor"))) return R::BrakeDisc;
	if (Matches(Name, TEXT("caliper"))) return R::BrakeCaliper;
	if (Matches(Name, TEXT("body.?colou?r|car.?paint|^paint|paint$|body_paint"))) return R::Paint;
	if (Matches(Name, TEXT("chrome"))) return R::Chrome;
	if (Matches(Name, TEXT("alumin|brushed|metal"))) return R::BareMetal;
	if (Matches(Name, TEXT("rim|wheel|alloy"))) return R::WheelAlloy;
	if (Matches(Name, TEXT("carpet"))) return R::Carpet;
	if (Matches(Name, TEXT("leather|seat"))) return R::Leather;
	if (Matches(Name, TEXT("cloth|fabric"))) return R::Cloth;
	if (Matches(Name, TEXT("gauge|display|screen|dial"))) return R::Gauge;
	if (Matches(Name, TEXT("lic.?plate|license|licence|number.?plate"))) return R::Plate;
	if (Matches(Name, TEXT("interior|dash"))) return R::InteriorPlastic;
	if (Matches(Name, TEXT("rubber|seal"))) return R::Rubber;
	if (Matches(Name, TEXT("plastic"))) return R::PlasticGrained;
	if (Matches(Name, TEXT("painted.?black|gloss.?black|piano"))) return R::TrimGloss;
	// Fall back on the node label for merged-by-material exports.
	if (Matches(OwnerLabel, TEXT("tire|tyre"))) return R::Tyre;
	return R::Authored;
}
