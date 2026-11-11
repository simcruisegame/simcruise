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
	if (!bCabinSet)
	{
		Cabin.Set(FSimCruiseRoadNoiseSynth::CabinCutoffHz, FSimCruiseInlineFourSynth::SampleRate);
		bCabinSet = true;
	}
	for (int32 I = 0; I < NumSamples; ++I)
	{
		const float Filtered = Cabin.Process(OutAudio[I]);
		if (Inputs.bCabin) OutAudio[I] = Filtered;
	}
	return NumSamples;
}

USimCruiseRoadNoiseComponent::USimCruiseRoadNoiseComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumChannels = 2;
	bAllowSpatialization = false;
	bAutoActivate = false;
}

void USimCruiseRoadNoiseComponent::SetRoadState(const FSimCruiseRoadNoiseSynth::FInputs& Inputs)
{
	FScopeLock Lock(&InputsLock);
	Latest = Inputs;
}

bool USimCruiseRoadNoiseComponent::Init(int32& SampleRate)
{
	SampleRate = FSimCruiseRoadNoiseSynth::SampleRate;
	NumChannels = 2;
	return true;
}

int32 USimCruiseRoadNoiseComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	FSimCruiseRoadNoiseSynth::FInputs Inputs;
	{
		FScopeLock Lock(&InputsLock);
		Inputs = Latest;
	}
	// NumSamples counts every channel's samples.
	Synth.Render(OutAudio, NumSamples / 2, Inputs);
	return NumSamples;
}
