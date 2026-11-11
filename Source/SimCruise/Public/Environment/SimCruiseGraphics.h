// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SimCruiseGraphics.generated.h"

/** The four graphics presets (Unreal scalability levels 0-3 plus SimCruise's own settings). */
UENUM(BlueprintType)
enum class ESimCruiseGraphicsPreset : uint8
{
	Low,
	Medium,
	High,
	Ultra
};

/** What a preset sets on top of Unreal's scalability groups. */
struct SIMCRUISE_API FSimCruisePresetSpec
{
	/** Unreal scalability level for every group (0 Low .. 3 Epic). */
	int32 ScalabilityLevel = 2;
	/** 3D resolution before TSR upscales to the output, percent of the output's height. */
	float ScreenPercentage = 67.0f;
	/** Volumetric clouds: ray-march sample scale for the main view. */
	float CloudViewSampleScale = 1.0f;
	/** Volumetric clouds: render at reduced resolution (r.VolumetricCloud.ReflectionRaySampleMaxCount aside). */
	bool bCloudHalfResolution = true;
	/** Lumen reflections on (else the sky light and screen-space reflections). */
	bool bLumenReflections = true;
	/** Lumen global illumination on (else the sky light with distance-field AO). */
	bool bLumenGI = true;
	/** Frame-rate the preset is tuned to hold, for the docs and the auto-pick. */
	float TargetFps = 60.0f;
};

struct SIMCRUISE_API FSimCruiseGraphics
{
	static FString Name(ESimCruiseGraphicsPreset Preset);

	/** "low" / "medium" / "high" / "ultra" (any case). */
	static bool Parse(const FString& Text, ESimCruiseGraphicsPreset& OutPreset);

	static FSimCruisePresetSpec Spec(ESimCruiseGraphicsPreset Preset);

	/**
	 * The preset this GPU should start on. Integrated and software adapters get Low;
	 * names are matched against the tiers measured in docs/unreal-architecture.md, with
	 * laptop (Max-Q / Laptop) parts one tier below their desktop name. Unknown adapters fall back
	 * to Unreal's synthetic benchmark when bAllowBenchmark is true, otherwise Medium.
	 */
	static ESimCruiseGraphicsPreset AutoPick(const FString& AdapterName, bool bIntegrated, int64 DedicatedVideoMemoryMB, bool bAllowBenchmark = false);

	/** AutoPick for the adapter the renderer actually opened. */
	static ESimCruiseGraphicsPreset AutoPickForThisMachine(bool bAllowBenchmark = true);

	/** Applies a preset now (scalability groups, screen percentage, TSR and the extras). */
	static void Apply(ESimCruiseGraphicsPreset Preset);

	/** The preset last applied (High before any Apply). */
	static ESimCruiseGraphicsPreset Current();
};
