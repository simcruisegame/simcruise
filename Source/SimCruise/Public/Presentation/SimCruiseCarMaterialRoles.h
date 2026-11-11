// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * What a surface of a car is made of. The roles are generic (no car names):
 * each maps to one shared Unreal material with physically based parameters
 * (SimCruiseCarMaterialLibrary), and per-car choices (paint colour, which
 * author material is which role) live in Content/Vehicles/CarPresentation.json.
 */
enum class ESimCruiseCarMaterialRole : uint8
{
	/** Use the author's glTF factors and textures as they are. */
	Authored,
	/** Body paint: base coat, metallic flake and clear coat. */
	Paint,
	/** Laminated or tempered automotive glass: tinted, Fresnel-reflective. */
	Glass,
	/** Privacy/dark glass and engine-view covers. */
	GlassDark,
	/** Polished chrome plating. */
	Chrome,
	/** Bare or brushed aluminium, satin metal. */
	BareMetal,
	/** Painted silver (or machined) alloy road wheel. */
	WheelAlloy,
	/** Tyre rubber: tread and sidewall, with moulded detail. */
	Tyre,
	/** Cast-iron brake disc with radial machining marks. */
	BrakeDisc,
	/** Painted or cast brake caliper. */
	BrakeCaliper,
	/** Textured (grained) black or grey exterior plastic: bumpers, cowl, mirrors. */
	PlasticGrained,
	/** Smooth gloss/satin black trim. */
	TrimGloss,
	/** EPDM door and window seals, soft-top rubber. */
	Rubber,
	/** Rear lamp lens (red), glows as tail/brake lamp. */
	LampRear,
	/** Front headlamp lens (clear polycarbonate). */
	LampFront,
	/** Amber indicator lens. */
	LampAmber,
	/** Rear-view mirror glass. */
	MirrorGlass,
	/** Interior moulded plastic: dash, door cards, console (pebble grain). */
	InteriorPlastic,
	/** Seat and trim fabric. */
	Cloth,
	/** Leather or leatherette (seats, wheel rim, boots). */
	Leather,
	/** Floor carpet. */
	Carpet,
	/** Instrument faces and screens (author texture, faintly lit). */
	Gauge,
	/** Number plate (no lettering is shipped: generic). */
	Plate,
	/** Cast metal under the body: exhaust, suspension, engine parts. */
	Underbody,
	/** Removed from the model (badges, maker lettering, hidden helpers). */
	Hidden,
	Count
};

namespace SimCruiseCarMaterialRoles
{
	SIMCRUISE_API const TCHAR* ToString(ESimCruiseCarMaterialRole Role);
	SIMCRUISE_API bool FromString(const FString& Name, ESimCruiseCarMaterialRole& OutRole);

	/**
	 * The role a material name (with its owner node label) suggests, from
	 * patterns model authors commonly use; Authored when nothing matches.
	 * Per-car overrides from the presentation profile take precedence.
	 */
	SIMCRUISE_API ESimCruiseCarMaterialRole Guess(const FString& MaterialName, const FString& OwnerLabel);

	/** Whether a surface label (material, textures, node names) names a badge, logo or hidden helper. */
	SIMCRUISE_API bool IsBadgeLabel(const FString& Label);
}
