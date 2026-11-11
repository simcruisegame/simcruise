// Copyright SimCruise contributors. All Rights Reserved.

#include "Audio/SimCruiseEngineSoundComponent.h"

#include "Misc/ScopeLock.h"

USimCruiseEngineSoundComponent::USimCruiseEngineSoundComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 1;
	bAllowSpatialization = false;
	bAutoActivate = false;
}

void USimCruiseEngineSoundComponent::SetEngineState(const FSimCruiseInlineFourSynth::FInputs& Inputs)
{
	FScopeLock Lock(&InputsLock);
	Latest = Inputs;
}

bool USimCruiseEngineSoundComponent::Init(int32& SampleRate)
{
	SampleRate = FSimCruiseInlineFourSynth::SampleRate;
	NumChannels = 1;
	return true;
}

int32 USimCruiseEngineSoundComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	FSimCruiseInlineFourSynth::FInputs Inputs;
	{
		FScopeLock Lock(&InputsLock);
		Inputs = Latest;
	}
	Synth.Render(OutAudio, NumSamples, Inputs);
	return NumSamples;
}
