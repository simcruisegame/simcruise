// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Audio/SimCruiseInlineFourSynth.h"
#include "Audio/SimCruiseRoadNoiseSynth.h"
#include "SimCruiseEngineSoundComponent.generated.h"

/**
 * Plays FSimCruiseInlineFourSynth: the game thread sets the engine's state each frame
 * (SetEngineState), the audio render thread pulls samples (mono, 22.05 kHz). Not
 * spatialised: like the Godot build, the pickup (cabin or outside) is a filter choice.
 */
UCLASS(ClassGroup = SimCruise, meta = (BlueprintSpawnableComponent))
class SIMCRUISE_API USimCruiseEngineSoundComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	USimCruiseEngineSoundComponent(const FObjectInitializer& ObjectInitializer);

	/** Game thread: the latest engine state (read by the next audio buffer). */
	void SetEngineState(const FSimCruiseInlineFourSynth::FInputs& Inputs);

	/** Before Start: a cross-plane V8's voice instead of the inline four (see FSimCruiseInlineFourSynth). */
	void ConfigureCrossPlaneV8(const TArray<int32>& BankOfEvent, double NearGain, double FarGain) { Synth.ConfigureCrossPlaneV8(BankOfEvent, NearGain, FarGain); }
	bool IsV8() const { return Synth.IsV8(); }

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	FSimCruiseInlineFourSynth Synth;
	/** The cabin's 2.4 kHz low-pass, which the engine passes through as the road does. */
	FSimCruiseCabinFilter Cabin;
	bool bCabinSet = false;
	FCriticalSection InputsLock;
	FSimCruiseInlineFourSynth::FInputs Latest;
};

/** Plays FSimCruiseRoadNoiseSynth (stereo, 22.05 kHz): tyres, road and wind. */
UCLASS(ClassGroup = SimCruise, meta = (BlueprintSpawnableComponent))
class SIMCRUISE_API USimCruiseRoadNoiseComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	USimCruiseRoadNoiseComponent(const FObjectInitializer& ObjectInitializer);

	/** Game thread: the latest road state (read by the next audio buffer). */
	void SetRoadState(const FSimCruiseRoadNoiseSynth::FInputs& Inputs);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	FSimCruiseRoadNoiseSynth Synth;
	FCriticalSection InputsLock;
	FSimCruiseRoadNoiseSynth::FInputs Latest;
};
