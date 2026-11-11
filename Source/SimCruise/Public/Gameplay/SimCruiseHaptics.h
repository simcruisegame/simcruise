// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruiseVehicleControl.h"
#include "GenericPlatform/IInputInterface.h"

struct FSimCruiseCarDefinition;

/** The four Xbox motors, 0..1 before the player's strength preference. */
struct FSimCruiseRumble
{
	/** Left body motor, low frequency ("strong"): rear tyres, wheelspin, ABS, kerbs. */
	float LowFrequency = 0.f;
	/** Right body motor, high frequency ("weak"): front scrub, road texture, gravel, engine. */
	float HighFrequency = 0.f;
	/** Impulse trigger motors (GameInput only; XInput has no trigger motors). */
	float LeftTrigger = 0.f;
	float RightTrigger = 0.f;

	bool IsQuiet() const { return LowFrequency <= 0.001f && HighFrequency <= 0.001f && LeftTrigger <= 0.001f && RightTrigger <= 0.001f; }
};

/** How Unreal's four force-feedback channels reach the pad. XInput sends
 *  max(LeftLarge, RightLarge) to the low motor and max(LeftSmall, RightSmall)
 *  to the high one; GameInput maps LeftLarge low, RightLarge high and the two
 *  Small channels to the trigger motors. */
enum class ESimCruiseRumbleBackend : uint8
{
	XInput,
	GameInput,
};

/**
 * Grip you can feel, ported from feel/slip-haptics-audio:game/Haptics.gd.
 * Read-only: it reads the vehicle's tyre state after the physics step and adds
 * no force. Normalised slip per tyre, 1 = the end of the full-grip plateau:
 *   s = sqrt((kappa/kappa*)^2 + (alpha/alpha*)^2)
 * Slip rather than friction-circle use, because the lateral law saturates and
 * falls past its peak: a front tyre sliding at 25 deg uses less of its circle
 * than one at the peak, so a use-based cue would fade as understeer grows.
 * Motors: high (weak) = front tyres, broken road, loose surface, engine load;
 * low (strong) = rear tyres and driven-wheel longitudinal slip, the 8 Hz ABS
 * pulse, kerbs and potholes. New for Unreal: the impulse triggers carry the
 * pedals' own story, right = driven-wheel spin under throttle, left = a
 * locking wheel under braking or the ABS pulse. Coasting straight on a smooth
 * road every term is zero.
 */
namespace SimCruiseHaptics
{
	constexpr double Plateau = 0.98;
	constexpr double SlipOnset = 0.75;
	constexpr double SlipLimit = 1.15;
	constexpr double SlipSlide = 2.5;
	constexpr double LimitShare = 0.65;
	constexpr double AbsHz = 8.0;
	constexpr double AbsOn = 0.6;
	constexpr double AbsSlipShare = 0.3;
	constexpr double KerbGain = 0.8;
	constexpr double KerbFrom = 0.45;
	constexpr double TextureGain = 0.2;
	constexpr double TextureFrom = 0.15;
	constexpr double TextureTo = 0.7;
	constexpr double LooseGain = 0.25;
	constexpr double EngineGain = 0.06;
	constexpr double TriggerGain = 0.8;
	constexpr double DefaultPeakSlipAngleRad = 0.18;

	/** Slip angle (rad) where the definition's lateral law
	 *  tanh(alpha k)(1 - falloff smoothstep(from, to, alpha)) first reaches 98 %
	 *  of its maximum (10.6 deg on the 1999 roadster). For vehicles to report as
	 *  FSimCruiseWheelTelemetry::PeakSlipAngleRad. */
	SIMCRUISE_API double PlateauSlipAngleRad(const FSimCruiseCarDefinition& Car);

	SIMCRUISE_API double NormalizedSlip(double Kappa, double PeakKappa, double Alpha, double PeakAlpha);

	/** 0 below the onset, LimitShare at the plateau, 1 deep in a slide; never falls as slip grows. */
	SIMCRUISE_API double SlipFeel(double S);

	/** 8 Hz square pulse while ABS acts: pump and valves, felt through the pedal. */
	SIMCRUISE_API double AbsPulse(bool bActive, double TimeS);

	/** The two body motors from the summarised state (Haptics.gd motors()). */
	SIMCRUISE_API FSimCruiseRumble Motors(double FrontSlip, double RearSlip, double Pulse, double Roughness, double Loose,
		double SpeedMps, double EngineLoad, bool bAbsOn);

	/** Write a rumble into Unreal's channels for the given backend. */
	SIMCRUISE_API FForceFeedbackValues ToChannels(const FSimCruiseRumble& Rumble, ESimCruiseRumbleBackend Backend);
}

/** Per-frame sampler: holds the ABS pulse clock. */
class SIMCRUISE_API FSimCruiseHapticsModel
{
public:
	/** Compute this frame's motors from telemetry. Throttle and brake are the
	 *  pedal positions sent this frame (used by the trigger motors). */
	FSimCruiseRumble Sample(const FSimCruiseVehicleTelemetry& Telemetry, double Throttle, double Brake, double DeltaTimeS);
	void Quiet();

	bool IsAbsActive() const { return bAbsActive; }
	/** Normalised slip per tyre from the last sample. */
	double GetWheelSlip(int32 Index) const { return Slip[FMath::Clamp(Index, 0, 3)]; }

private:
	double Slip[4] = {0.0, 0.0, 0.0, 0.0};
	double PulseTime = 0.0;
	bool bAbsActive = false;
};
