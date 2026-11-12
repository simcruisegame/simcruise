// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SimCruiseLatencyProbe.generated.h"

/**
 * -SimCruiseLatencyProbe: measures the delay from a stick input to the picture.
 *
 * When the car stands at its start with its tiles built, the probe gives the
 * left stick a full step through the real input stack (InjectAxis, so the
 * dead zone, the steering shaper, the rack rate and the asynchronous physics
 * step all count) and finds the first game frame whose vehicle state, the
 * state the frame draws, shows the rack moving. The delay is
 *
 *   (start of that frame - time of the input) + that frame's game-to-render time
 *
 * where the game-to-render time is the NVIDIA Reflex marker latency (game
 * frame start to the end of GPU rendering) when the Reflex plugin is available,
 * else an estimate: (1 + r.OneFrameThreadLag) game frames plus one GPU frame.
 * The display's scan-out (half a refresh on average) is not included; add it
 * for a photon figure. Twenty steps, alternately left and right; one
 * "SimCruiseLatency:" line each and a summary, then the game quits (unless
 * -SimCruiseKeepOpen).
 */
UCLASS()
class SIMCRUISE_API ASimCruiseLatencyProbe : public AActor
{
	GENERATED_BODY()

public:
	ASimCruiseLatencyProbe();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EStage : uint8 { WaitCar, Settle, Rest, Step, Done };
	EStage Stage = EStage::WaitCar;
	double StageStarted = 0.0;
	int32 SettleFrames = 0;
	int32 Trial = 0;
	float StickSign = 1.f;
	double InputSeconds = 0.0;
	uint64 InputFrame = 0;
	double RestRackRad = 0.0;
	TArray<double> Samples;
	TArray<double> FrameSamples;

	void Finish();
	/** Game frame start to GPU render end for the latest frame, ms; false when no latency markers. */
	static bool MarkerLatencyMs(float& OutMs);
};
