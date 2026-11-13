// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseGraphics.h"

#include "HAL/IConsoleManager.h"
#include "RHIGlobals.h"
#include "Scalability.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseGraphics, Log, All);

namespace
{
	ESimCruiseGraphicsPreset GCurrentPreset = ESimCruiseGraphicsPreset::High;

	ESimCruiseGraphicsPreset StepDown(ESimCruiseGraphicsPreset Preset)
	{
		return Preset == ESimCruiseGraphicsPreset::Low ? Preset : static_cast<ESimCruiseGraphicsPreset>(static_cast<uint8>(Preset) - 1);
	}

	void SetCVar(const TCHAR* Name, const FString& Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(*Value, ECVF_SetByGameSetting);
		}
	}
}

FString FSimCruiseGraphics::Name(ESimCruiseGraphicsPreset Preset)
{
	switch (Preset)
	{
	case ESimCruiseGraphicsPreset::Low: return TEXT("low");
	case ESimCruiseGraphicsPreset::Medium: return TEXT("medium");
	case ESimCruiseGraphicsPreset::High: return TEXT("high");
	case ESimCruiseGraphicsPreset::Ultra: return TEXT("ultra");
	}
	return TEXT("high");
}

bool FSimCruiseGraphics::Parse(const FString& Text, ESimCruiseGraphicsPreset& OutPreset)
{
	const FString Lower = Text.TrimStartAndEnd().ToLower();
	for (uint8 Index = 0; Index <= static_cast<uint8>(ESimCruiseGraphicsPreset::Ultra); ++Index)
	{
		const ESimCruiseGraphicsPreset Candidate = static_cast<ESimCruiseGraphicsPreset>(Index);
		if (Lower == Name(Candidate))
		{
			OutPreset = Candidate;
			return true;
		}
	}
	if (Lower == TEXT("epic"))
	{
		OutPreset = ESimCruiseGraphicsPreset::Ultra;
		return true;
	}
	return false;
}

float FSimCruiseGraphics::SharpenFor(float ScreenPercentage)
{
	return FMath::Clamp(0.4f + 1.2f * (1.0f - ScreenPercentage / 100.0f), 0.4f, 1.0f);
}

FSimCruisePresetSpec FSimCruiseGraphics::Spec(ESimCruiseGraphicsPreset Preset)
{
	// Screen percentages and the resulting frame times on the RTX 2070 Super Max-Q at
	// 1920x1080 are recorded in docs/unreal-architecture.md (Light, sky and rendering).
	FSimCruisePresetSpec Out;
	switch (Preset)
	{
	case ESimCruiseGraphicsPreset::Low:
		Out.ScalabilityLevel = 0;
		Out.ScreenPercentage = 50.0f;
		Out.bLumenGI = false;
		Out.bLumenReflections = false;
		break;
	case ESimCruiseGraphicsPreset::Medium:
		Out.ScalabilityLevel = 1;
		Out.ScreenPercentage = 58.0f;
		Out.bLumenGI = true;
		Out.bLumenReflections = false;
		break;
	case ESimCruiseGraphicsPreset::High:
		Out.ScalabilityLevel = 2;
		Out.ScreenPercentage = 67.0f;
		break;
	case ESimCruiseGraphicsPreset::Ultra:
		Out.ScalabilityLevel = 3;
		Out.ScreenPercentage = 75.0f;
		break;
	}
	return Out;
}

ESimCruiseGraphicsPreset FSimCruiseGraphics::AutoPick(const FString& AdapterName, bool bIntegrated, int64 DedicatedVideoMemoryMB, bool bAllowBenchmark)
{
	const FString Name = AdapterName.ToLower();
	if (Name.Contains(TEXT("microsoft basic")) || Name.Contains(TEXT("llvmpipe")) || Name.Contains(TEXT("swiftshader")) || Name.Contains(TEXT("warp")))
	{
		return ESimCruiseGraphicsPreset::Low;
	}
	if (bIntegrated || (Name.Contains(TEXT("intel")) && !Name.Contains(TEXT("arc"))))
	{
		return ESimCruiseGraphicsPreset::Low;
	}

	const bool bLaptop = Name.Contains(TEXT("max-q")) || Name.Contains(TEXT("laptop")) || Name.Contains(TEXT("mobile"));
	TOptional<ESimCruiseGraphicsPreset> Desktop;
	struct FTier { const TCHAR* Needle; ESimCruiseGraphicsPreset Preset; };
	// Order matters: the first needle found wins.
	static const FTier Tiers[] = {
		{TEXT("rtx 50"), ESimCruiseGraphicsPreset::Ultra}, {TEXT("rtx 40"), ESimCruiseGraphicsPreset::Ultra},
		{TEXT("rtx 3050"), ESimCruiseGraphicsPreset::Medium}, {TEXT("rtx 30"), ESimCruiseGraphicsPreset::Ultra},
		{TEXT("rx 9"), ESimCruiseGraphicsPreset::Ultra}, {TEXT("rx 7"), ESimCruiseGraphicsPreset::Ultra},
		// The desktop 2070/2080 start on Ultra; laptop parts one step lower, Max-Q parts two
		// (measured on the RTX 2070 Super Max-Q, see below).
		{TEXT("rtx 2080"), ESimCruiseGraphicsPreset::Ultra}, {TEXT("rtx 2070"), ESimCruiseGraphicsPreset::Ultra},
		{TEXT("rtx 2060"), ESimCruiseGraphicsPreset::High}, {TEXT("rx 6"), ESimCruiseGraphicsPreset::Ultra},
		{TEXT("arc"), ESimCruiseGraphicsPreset::Medium}, {TEXT("gtx 16"), ESimCruiseGraphicsPreset::Medium},
		{TEXT("gtx 10"), ESimCruiseGraphicsPreset::Low}, {TEXT("rx 5"), ESimCruiseGraphicsPreset::Medium},
		{TEXT("apple m"), ESimCruiseGraphicsPreset::Medium},
	};
	for (const FTier& Tier : Tiers)
	{
		if (Name.Contains(Tier.Needle))
		{
			Desktop = Tier.Preset;
			break;
		}
	}

	ESimCruiseGraphicsPreset Pick = ESimCruiseGraphicsPreset::Medium;
	if (Desktop.IsSet())
	{
		Pick = bLaptop ? StepDown(Desktop.GetValue()) : Desktop.GetValue();
		// A Max-Q part is the low-power (about 80 W, down to 28 W under load) variant: one step
		// more. Measured in the 2.0.0 Shipping build at 1080p on the RTX 2070 Super Max-Q, Ocean
		// Drive from the driver's seat: High 57 fps (1 % low 44), Medium 77 fps (1 % low 61), so
		// High misses the 60 fps / 45 fps target in the city (docs/release/evidence/perf.txt).
		if (Name.Contains(TEXT("max-q")))
		{
			Pick = StepDown(Pick);
		}
	}
	else if (bAllowBenchmark)
	{
		const Scalability::FQualityLevels Measured = Scalability::BenchmarkQualityLevels();
		Pick = static_cast<ESimCruiseGraphicsPreset>(FMath::Clamp(Measured.GlobalIlluminationQuality, 0, 3));
	}

	// Ultra's virtual shadow pages, Lumen surface cache and history need room.
	if (DedicatedVideoMemoryMB > 0 && DedicatedVideoMemoryMB < 7000 && Pick == ESimCruiseGraphicsPreset::Ultra)
	{
		Pick = ESimCruiseGraphicsPreset::High;
	}
	if (DedicatedVideoMemoryMB > 0 && DedicatedVideoMemoryMB < 4500 && Pick >= ESimCruiseGraphicsPreset::High)
	{
		Pick = ESimCruiseGraphicsPreset::Medium;
	}
	return Pick;
}

ESimCruiseGraphicsPreset FSimCruiseGraphics::AutoPickForThisMachine(bool bAllowBenchmark)
{
	const int64 VideoMB = static_cast<int64>(GRHIGlobals.GpuInfo.DedicatedVideoMemory / (1024ull * 1024ull));
	const ESimCruiseGraphicsPreset Pick = AutoPick(GRHIAdapterName, GRHIDeviceIsIntegrated, VideoMB, bAllowBenchmark);
	UE_LOG(LogSimCruiseGraphics, Display, TEXT("Graphics auto-pick: '%s' (%lld MB, integrated %d) -> %s"),
		*GRHIAdapterName, VideoMB, GRHIDeviceIsIntegrated ? 1 : 0, *Name(Pick));
	return Pick;
}

void FSimCruiseGraphics::Apply(ESimCruiseGraphicsPreset Preset)
{
	const FSimCruisePresetSpec S = Spec(Preset);
	Scalability::FQualityLevels Levels;
	Levels.SetFromSingleQualityLevel(S.ScalabilityLevel);
	Levels.ResolutionQuality = S.ScreenPercentage;
	Scalability::SetQualityLevels(Levels, true);

	// TSR on every preset: it is what makes the lower internal resolutions hold detail.
	SetCVar(TEXT("r.AntiAliasingMethod"), TEXT("4"));
	SetCVar(TEXT("r.ScreenPercentage"), FString::SanitizeFloat(S.ScreenPercentage));
	// Sharpen after the upscale, more the lower the internal resolution: TSR's output from a
	// 58 % picture is soft (Aaron: "the graphics look fuzzy"). 100 % gets a light 0.4.
	SetCVar(TEXT("r.Tonemapper.Sharpen"), FString::SanitizeFloat(SharpenFor(S.ScreenPercentage)));
	SetCVar(TEXT("r.Lumen.DiffuseIndirect.Allow"), S.bLumenGI ? TEXT("1") : TEXT("0"));
	SetCVar(TEXT("r.Lumen.Reflections.Allow"), S.bLumenReflections ? TEXT("1") : TEXT("0"));

	GCurrentPreset = Preset;
	UE_LOG(LogSimCruiseGraphics, Display, TEXT("Graphics preset %s: scalability %d, %.0f%% screen percentage with TSR, Lumen GI %d reflections %d"),
		*Name(Preset), S.ScalabilityLevel, S.ScreenPercentage, S.bLumenGI ? 1 : 0, S.bLumenReflections ? 1 : 0);
}

ESimCruiseGraphicsPreset FSimCruiseGraphics::Current()
{
	return GCurrentPreset;
}
