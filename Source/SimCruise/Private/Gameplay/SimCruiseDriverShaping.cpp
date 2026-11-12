// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseDriverShaping.h"

#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarTriggerGamma(
	TEXT("simcruise.TriggerGamma"),
	static_cast<float>(SimCruiseShaping::DefaultTriggerGamma),
	TEXT("Right-trigger throttle curve: pedal = trigger^gamma (1 = linear, 1.8 default, up to 3)."),
	ECVF_Default);

namespace SimCruiseShaping
{
	static double MoveToward(double From, double To, double Delta)
	{
		if (FMath::Abs(To - From) <= Delta)
		{
			return To;
		}
		return From + FMath::Sign(To - From) * Delta;
	}

	double SmoothMin(double A, double B, double K)
	{
		const double H = FMath::Clamp(0.5 + 0.5 * (B - A) / K, 0.0, 1.0);
		return FMath::Lerp(B, A, H) - K * H * (1.0 - H);
	}

	double AvailableSteeringRad(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps)
	{
		const double Speed = FMath::Abs(ForwardSpeedMps);
		const double LimitAngle = FMath::Atan(Setup.WheelbaseM * Setup.SteeringAssistLateralG * StandardGravity / FMath::Max(Speed * Speed, 0.01))
			+ Setup.SteeringSlipLimitSurplusRad;
		return SmoothMin(Setup.ParkingRackAngleRad, LimitAngle, LockBlendRad);
	}

	double FrontAxleSlipAngleRad(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps, double LateralSpeedMps, double YawRateRadPerS)
	{
		if (ForwardSpeedMps <= BetaMinForwardSpeedMps)
		{
			return 0.0;
		}
		const double FrontLateral = LateralSpeedMps + YawRateRadPerS * Setup.CenterOfMassToFrontAxleM;
		return FMath::Atan2(FrontLateral, FMath::Max(ForwardSpeedMps, 3.0));
	}

	double RangeTowardRad(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps, double BetaFrontRad, double Direction)
	{
		const double Cap = AvailableSteeringRad(Setup, ForwardSpeedMps);
		if (Direction * BetaFrontRad <= CountersteerMinBetaRad)
		{
			return Cap;
		}
		const double Allowance = FMath::Min(FMath::Abs(BetaFrontRad) + Setup.SteeringSlipLimitSurplusRad, Setup.ParkingRackAngleRad);
		return FMath::Max(Cap, Allowance);
	}

	double ThumbstickCommand(const FSimCruiseVehicleSetup& Setup, double Stick, double ForwardSpeedMps)
	{
		Stick = FMath::Clamp(Stick, -1.0, 1.0);
		const double SpeedSquared = FMath::Max(ForwardSpeedMps * ForwardSpeedMps, 0.01);
		const double AngleForCornering = FMath::Atan(Setup.WheelbaseM * PadCentreCorneringG * StandardGravity / SpeedSquared);
		const double CentreGain = FMath::Clamp(AngleForCornering / AvailableSteeringRad(Setup, ForwardSpeedMps), 0.16, 1.0);
		return Stick * (CentreGain + (1.0 - CentreGain) * Stick * Stick);
	}

	double KeyRackRateRadPerS(const FSimCruiseVehicleSetup& Setup, double ForwardSpeedMps)
	{
		const double Parking = 2.75 * Setup.ParkingRackAngleRad;
		return FMath::Lerp(Parking, KeyRackRateHighwayRadPerS, FMath::SmoothStep(5.0, 25.0, FMath::Abs(ForwardSpeedMps)));
	}

	double AdvanceSteering(double Current, double Target, double DeltaTimeS, double ApplyRate, double ReturnRate)
	{
		const double UnwindTarget = Current * Target < 0.0 ? 0.0 : Target;
		if (FMath::Abs(UnwindTarget) < FMath::Abs(Current))
		{
			const double UnwindSeconds = FMath::Abs(Current - UnwindTarget) / FMath::Max(ReturnRate, UE_DOUBLE_SMALL_NUMBER);
			Current = MoveToward(Current, UnwindTarget, DeltaTimeS * ReturnRate);
			DeltaTimeS = FMath::Max(0.0, DeltaTimeS - UnwindSeconds);
		}
		return MoveToward(Current, Target, DeltaTimeS * ApplyRate);
	}

	double TriggerThrottle(double Trigger, double Gamma)
	{
		return FMath::Pow(FMath::Clamp(Trigger, 0.0, 1.0), FMath::Clamp(Gamma, 1.0, 3.0));
	}

	double KeyPedal(double Current, bool bPressed, double RisePerS, double DeltaTimeS)
	{
		return bPressed ? MoveToward(Current, 1.0, RisePerS * DeltaTimeS) : MoveToward(Current, 0.0, KeyPedalReleasePerS * DeltaTimeS);
	}
}

void FSimCruiseDriverShaper::Reset()
{
	RackRad = 0.0;
	KeyThrottle = 0.0;
	KeyBrake = 0.0;
	Clutch = 0.0;
	RangeLeft = 0.0;
	RangeRight = 0.0;
	bUsingThumbstick = false;
}

FSimCruiseDriverCommand FSimCruiseDriverShaper::Update(const FSimCruiseVehicleSetup& Setup, const FSimCruiseDriveTelemetry& Telemetry,
	const FSimCruiseRawControls& Raw, double DeltaTimeS)
{
	using namespace SimCruiseShaping;
	DeltaTimeS = FMath::Clamp(DeltaTimeS, 0.0, 0.1);

	const double ForwardSpeed = Telemetry.BodyVelocityMps.X;
	const double Cap = AvailableSteeringRad(Setup, ForwardSpeed);
	const double Beta = FrontAxleSlipAngleRad(Setup, ForwardSpeed, Telemetry.BodyVelocityMps.Y, Telemetry.BodyAngularVelocityRadPerS.Z);
	RangeRight = RangeTowardRad(Setup, ForwardSpeed, Beta, 1.0);
	RangeLeft = RangeTowardRad(Setup, ForwardSpeed, Beta, -1.0);

	double Pad = FMath::Clamp(Raw.PadSteer, -1.0, 1.0);
	double Key = FMath::Clamp(Raw.KeySteer, -1.0, 1.0);
	if (FMath::Abs(Pad) < SteerDeadBand) { Pad = 0.0; }
	if (FMath::Abs(Key) < SteerDeadBand) { Key = 0.0; }
	bUsingThumbstick = FMath::Abs(Pad) > FMath::Abs(Key);

	double Target = 0.0;
	double ApplyRate = 0.0;
	double ReturnRate = 0.0;
	if (bUsingThumbstick)
	{
		// A quicker steering ratio turns the wheels further for the same thumb, up to the range.
		const double Quick = FMath::Clamp(Setup.SteeringQuickness, 0.5, 2.0);
		const double Command = FMath::Clamp(ThumbstickCommand(Setup, Pad, ForwardSpeed) * Quick, -1.0, 1.0);
		Target = Command * (Command > 0.0 ? RangeRight : RangeLeft);
		ApplyRate = PadApplyRatePerS * Cap * Quick;
		ReturnRate = PadReturnRatePerS * Cap;
	}
	else
	{
		Target = Key * (Key > 0.0 ? RangeRight : RangeLeft);
		ApplyRate = KeyRackRateRadPerS(Setup, ForwardSpeed) * FMath::Clamp(Setup.SteeringQuickness, 0.5, 2.0);
		ReturnRate = FMath::Max(ApplyRate * KeyReturnFactor, KeyReturnRateMinRadPerS);
	}

	// A hand catching a slide moves fast: toward the front axle's velocity the
	// rack may travel at up to 40 deg/s. Every other move keeps its own rate.
	if (FMath::Abs(Beta) > CountersteerMinBetaRad && Target * Beta > 0.0)
	{
		ApplyRate = FMath::Max(ApplyRate, CountersteerRateRadPerS);
		ReturnRate = FMath::Max(ReturnRate, CountersteerRateRadPerS);
	}

	RackRad = AdvanceSteering(RackRad, Target, DeltaTimeS, ApplyRate, ReturnRate);
	if (Raw.bWheel)
	{
		// A real steering wheel drives the rack directly, as the column does.
		RackRad = Raw.WheelRackRad;
		bUsingThumbstick = false;
	}
	RackRad = FMath::Clamp(RackRad, -Setup.ParkingRackAngleRad, Setup.ParkingRackAngleRad);

	KeyThrottle = KeyPedal(KeyThrottle, Raw.bKeyThrottle, KeyThrottleRisePerS, DeltaTimeS);
	KeyBrake = KeyPedal(KeyBrake, Raw.bKeyBrake, KeyBrakeRisePerS, DeltaTimeS);
	const double ClutchTarget = Raw.bClutch ? 1.0 : 0.0;
	Clutch = Clutch + FMath::Clamp(ClutchTarget - Clutch, -ClutchReleasePerS * DeltaTimeS, ClutchPressPerS * DeltaTimeS);
	if (Raw.bWheel && Raw.bWheelClutch)
	{
		Clutch = FMath::Max(Raw.bClutch ? Clutch : 0.0, FMath::Clamp(Raw.WheelClutch, 0.0, 1.0));
	}

	FSimCruiseDriverCommand Command;
	Command.Throttle = static_cast<float>(FMath::Clamp(FMath::Max3(TriggerThrottle(Raw.PadThrottle, CVarTriggerGamma.GetValueOnGameThread()), KeyThrottle,
		Raw.bWheel ? Raw.WheelThrottle : 0.0), 0.0, 1.0));
	Command.Brake = static_cast<float>(FMath::Clamp(FMath::Max3(Raw.PadBrake, KeyBrake, Raw.bWheel ? Raw.WheelBrake : 0.0), 0.0, 1.0));
	Command.Clutch = static_cast<float>(Clutch);
	Command.ParkingBrake = Raw.bParkingBrake ? 1.f : 0.f;
	Command.bFullServiceBrake = Raw.bFullServiceBrake;
	Command.RackAngleRad = RackRad;
	return Command;
}
