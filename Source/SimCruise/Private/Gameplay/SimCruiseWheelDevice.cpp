// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseWheelDevice.h"

#if SIMCRUISE_WITH_GAMEINPUT
#include "GameInputBaseModule.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseWheel, Log, All);

#if SIMCRUISE_WITH_GAMEINPUT && GAME_INPUT_SUPPORT

struct FSimCruiseWheelDevice::FImpl
{
	TComPtr<IGameInputDevice> Device;
	TComPtr<IGameInputForceFeedbackEffect> Spring;
	TComPtr<IGameInputForceFeedbackEffect> Damper;
	TComPtr<IGameInputForceFeedbackEffect> Constant;
	uint32 LastButtons = 0;
	double HalfRotationDeg = 450.0;
	bool bHasClutch = false;
	bool bHasShifter = false;
	bool bReportedNone = false;

	void ReleaseEffects()
	{
		auto Release = [](TComPtr<IGameInputForceFeedbackEffect>& Effect)
		{
			if (Effect)
			{
				Effect->SetState(GameInputFeedbackStopped);
				Effect.Reset();
			}
		};
		Release(Spring);
		Release(Damper);
		Release(Constant);
	}

	static GameInputForceFeedbackParams Condition(GameInputForceFeedbackEffectKind Kind, float Coefficient)
	{
		GameInputForceFeedbackParams Params = {};
		Params.kind = Kind;
		GameInputForceFeedbackConditionParams& C = Kind == GameInputForceFeedbackSpring ? Params.data.spring : Params.data.damper;
		C.magnitude.linearX = 1.f;
		C.positiveCoefficient = Coefficient;
		C.negativeCoefficient = Coefficient;
		C.maxPositiveMagnitude = 1.f;
		C.maxNegativeMagnitude = 1.f;
		return Params;
	}

	static GameInputForceFeedbackParams ConstantForce(float Magnitude)
	{
		GameInputForceFeedbackParams Params = {};
		Params.kind = GameInputForceFeedbackConstant;
		// An endless effect: sustain for ever at full gain, its magnitude set each frame.
		Params.data.constant.envelope.sustainDuration = TNumericLimits<uint64>::Max();
		Params.data.constant.envelope.attackGain = 1.f;
		Params.data.constant.envelope.sustainGain = 1.f;
		Params.data.constant.envelope.releaseGain = 1.f;
		Params.data.constant.envelope.playCount = 1;
		Params.data.constant.magnitude.linearX = Magnitude;
		return Params;
	}

	void CreateEffects()
	{
		const GameInputDeviceInfo* Info = nullptr;
		if (!Device || FAILED(Device->GetDeviceInfo(&Info)) || !Info || Info->forceFeedbackMotorCount == 0)
		{
			return;
		}
		Device->SetForceFeedbackMotorGain(0, 1.f);
		const GameInputForceFeedbackParams SpringParams = Condition(GameInputForceFeedbackSpring, 0.f);
		const GameInputForceFeedbackParams DamperParams = Condition(GameInputForceFeedbackDamper, 0.f);
		const GameInputForceFeedbackParams ConstantParams = ConstantForce(0.f);
		Device->CreateForceFeedbackEffect(0, &SpringParams, &Spring);
		Device->CreateForceFeedbackEffect(0, &DamperParams, &Damper);
		Device->CreateForceFeedbackEffect(0, &ConstantParams, &Constant);
		IGameInputForceFeedbackEffect* Running[] = {Spring.Get(), Damper.Get(), Constant.Get()};
		for (IGameInputForceFeedbackEffect* Effect : Running)
		{
			if (Effect) Effect->SetState(GameInputFeedbackRunning);
		}
	}
};

FSimCruiseWheelDevice::FSimCruiseWheelDevice() : Impl(MakeUnique<FImpl>()) {}

FSimCruiseWheelDevice::~FSimCruiseWheelDevice()
{
	Stop();
	Impl->ReleaseEffects();
}

bool FSimCruiseWheelDevice::Poll(FSimCruiseWheelReading& Out)
{
	IGameInput* Input = FGameInputBaseModule::IsAvailable() ? FGameInputBaseModule::GetGameInput() : nullptr;
	TComPtr<IGameInputReading> Reading;
	if (!Input || FAILED(Input->GetCurrentReading(GameInputKindRacingWheel, nullptr, &Reading)) || !Reading)
	{
		if (Impl->Device)
		{
			UE_LOG(LogSimCruiseWheel, Display, TEXT("Racing wheel disconnected: %s"), *Name);
			Impl->ReleaseEffects();
			Impl->Device.Reset();
			Name.Reset();
		}
		return false;
	}
	GameInputRacingWheelState State = {};
	if (!Reading->GetRacingWheelState(&State))
	{
		return false;
	}
	TComPtr<IGameInputDevice> Device;
	Reading->GetDevice(&Device);
	if (Device != Impl->Device)
	{
		Impl->ReleaseEffects();
		Impl->Device = Device;
		const GameInputDeviceInfo* Info = nullptr;
		if (Device && SUCCEEDED(Device->GetDeviceInfo(&Info)) && Info)
		{
			const GameInputRacingWheelInfo* Wheel = Info->racingWheelInfo;
			Impl->HalfRotationDeg = Wheel && Wheel->maxWheelAngle > 1.f ? Wheel->maxWheelAngle : 450.0;
			Impl->bHasClutch = Wheel && Wheel->hasClutch;
			Impl->bHasShifter = Wheel && Wheel->hasPatternShifter;
			Name = FString::Printf(TEXT("%s (%04x:%04x), %.0f degrees lock to lock, %u force-feedback motor(s)%s%s"),
				Info->displayName ? UTF8_TO_TCHAR(Info->displayName) : TEXT("racing wheel"), Info->vendorId, Info->productId,
				Impl->HalfRotationDeg * 2.0, Info->forceFeedbackMotorCount, Impl->bHasClutch ? TEXT(", clutch") : TEXT(""),
				Impl->bHasShifter ? TEXT(", H shifter") : TEXT(""));
			UE_LOG(LogSimCruiseWheel, Display, TEXT("Racing wheel: %s"), *Name);
		}
		Impl->CreateEffects();
		Impl->LastButtons = static_cast<uint32>(State.buttons);
	}
	const uint32 Buttons = static_cast<uint32>(State.buttons);
	const uint32 Pressed = Buttons & ~Impl->LastButtons;
	Impl->LastButtons = Buttons;
	Out.Wheel = FMath::Clamp(static_cast<double>(State.wheel), -1.0, 1.0);
	Out.HalfRotationDeg = Impl->HalfRotationDeg;
	Out.Throttle = FMath::Clamp(static_cast<double>(State.throttle), 0.0, 1.0);
	Out.Brake = FMath::Clamp(static_cast<double>(State.brake), 0.0, 1.0);
	Out.Clutch = FMath::Clamp(static_cast<double>(State.clutch), 0.0, 1.0);
	Out.Handbrake = FMath::Clamp(static_cast<double>(State.handbrake), 0.0, 1.0);
	Out.bNextGear = (Pressed & GameInputRacingWheelNextGear) != 0;
	Out.bPreviousGear = (Pressed & GameInputRacingWheelPreviousGear) != 0;
	Out.bHasPatternShifter = Impl->bHasShifter;
	Out.PatternShifterGear = State.patternShifterGear;
	Out.bHasClutch = Impl->bHasClutch;
	return true;
}

void FSimCruiseWheelDevice::SetForces(double Spring, double Damper, double Constant)
{
	if (Impl->Spring)
	{
		const GameInputForceFeedbackParams P = FImpl::Condition(GameInputForceFeedbackSpring, static_cast<float>(FMath::Clamp(Spring, 0.0, 1.0)));
		Impl->Spring->SetParams(&P);
	}
	if (Impl->Damper)
	{
		const GameInputForceFeedbackParams P = FImpl::Condition(GameInputForceFeedbackDamper, static_cast<float>(FMath::Clamp(Damper, 0.0, 1.0)));
		Impl->Damper->SetParams(&P);
	}
	if (Impl->Constant)
	{
		const GameInputForceFeedbackParams P = FImpl::ConstantForce(static_cast<float>(FMath::Clamp(Constant, -1.0, 1.0)));
		Impl->Constant->SetParams(&P);
	}
}

void FSimCruiseWheelDevice::Stop()
{
	SetForces(0.0, 0.0, 0.0);
}

#else

struct FSimCruiseWheelDevice::FImpl {};
FSimCruiseWheelDevice::FSimCruiseWheelDevice() : Impl(MakeUnique<FImpl>()) {}
FSimCruiseWheelDevice::~FSimCruiseWheelDevice() = default;
bool FSimCruiseWheelDevice::Poll(FSimCruiseWheelReading&) { return false; }
void FSimCruiseWheelDevice::SetForces(double, double, double) {}
void FSimCruiseWheelDevice::Stop() {}

#endif
