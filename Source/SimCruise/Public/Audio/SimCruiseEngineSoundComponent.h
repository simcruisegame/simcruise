// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "Audio/SimCruiseInlineFourSynth.h"
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

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	FSimCruiseInlineFourSynth Synth;
	FCriticalSection InputsLock;
	FSimCruiseInlineFourSynth::FInputs Latest;
};
