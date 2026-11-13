// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicleSim.h"

#include "Vehicle/SimCruiseVehicleDynamics.h"
#include "Vehicle/SimCruiseUnits.h"

namespace
{
	using SimCruiseDynamics::MoveToward;
	using SimCruiseDynamics::SmoothStep;

	constexpr double Tau = 2.0 * UE_DOUBLE_PI;
	constexpr double LockBlendRad = 3.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyRackRateHighwayRadPerSecond = 12.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyReturnRateMinimumRadPerSecond = 24.0 * UE_DOUBLE_PI / 180.0;
	constexpr double KeyReturnFactor = 1.6;
	constexpr double SteerDeadBand = 0.01;

	// Traction control (Powertrain.gd): the slip read through a 20 ms low-pass
	// and a slew limit, proportional plus integral on the error, harder cut on
	// a large overshoot, cut changed no faster than an engine can.
	constexpr double TcFilterS = 0.02;
	constexpr double TcSlipSlewPerSecond = 100.0;
	constexpr double TcProportional = 0.15;
	constexpr double TcOvershoot = 0.05;
	constexpr double TcCutRateUp = 20.0;
	constexpr double TcCutRateDown = 6.0;

	// ABS modulator: integral gain on the slip error (1/s), a proportional dump
	// past the peak, and the slip it aims at relative to the peak.
	constexpr double AbsGainPerSecond = 20.0;
	constexpr double AbsProportional = 0.3;
	constexpr double AbsTargetOverPeak = 1.0;

	// Godot ran the driveline at 60 Hz with 6 substeps; the cars' launches were calibrated there.
	constexpr double ClutchCalibrationSubstepS = 1.0 / 360.0;

	double SmoothMin(double A, double B, double K)
	{
		const double H = FMath::Clamp(0.5 + 0.5 * (B - A) / K, 0.0, 1.0);
		return FMath::Lerp(B, A, H) - K * H * (1.0 - H);
	}

	double AxleValue(const TArray<double>& Values, int32 Axle, double Fallback = 0.0)
	{
		return Values.IsValidIndex(Axle) ? Values[Axle] : Fallback;
	}
}

FSimCruiseVehicleSim::FSimCruiseVehicleSim()
{
	Initialize(FSimCruiseCarDefinition());
}

void FSimCruiseVehicleSim::Initialize(const FSimCruiseCarDefinition& InCar)
{
	Car = InCar;
	TireRadiusM = Car.TireFreeRadiusM();
	WheelInertiaKgM2 = FMath::Max(Car.WheelRotationalInertiaKgM2, UE_DOUBLE_SMALL_NUMBER);
	RestLengthM = Car.MaximumSuspensionTravelM;
	ParkingRackRad = SimCruiseUnits::ToRadians(Car.ParkingRackAngleDeg);
	RoadReferenceRad = SimCruiseUnits::ToRadians(Car.RoadReferenceSteerAngleDeg);
	LimitSlipSurplusRad = SimCruiseUnits::ToRadians(Car.SteeringSlipLimitSurplusDeg);
	KeyRackRateParkingRadPerSecond = 2.75 * ParkingRackRad;
	AntiRollRateNPerM[0] = SimCruiseDynamics::GetAntiRollBarRateNPerM(Car, 0);
	AntiRollRateNPerM[1] = SimCruiseDynamics::GetAntiRollBarRateNPerM(Car, 1);
	UE_LOG(LogTemp, Verbose, TEXT("SimCruise vehicle %s: wheel rates %.0f/%.0f N/m, anti-roll %.0f/%.0f N/m"), *Car.Id,
		SimCruiseDynamics::GetWheelSpringRateNPerM(Car, 0), SimCruiseDynamics::GetWheelSpringRateNPerM(Car, 1),
		AntiRollRateNPerM[0], AntiRollRateNPerM[1]);

	// Godot's car origin sits at the ground at ride height; the body (the root
	// collision box) is centred collision_box_y above it.
	ReferenceOriginLocalM = FVector3d(0.0, 0.0, -Car.CollisionShapeCenterYM);
	const double HalfWheelbase = Car.WheelbaseM * 0.5;
	const double MountZ = Car.SuspensionMountHeightM - Car.CollisionShapeCenterYM;
	MountsLocalM[0] = FVector3d(HalfWheelbase, -Car.FrontTrackM * 0.5, MountZ);
	MountsLocalM[1] = FVector3d(HalfWheelbase, Car.FrontTrackM * 0.5, MountZ);
	MountsLocalM[2] = FVector3d(-HalfWheelbase, -Car.RearTrackM * 0.5, MountZ);
	MountsLocalM[3] = FVector3d(-HalfWheelbase, Car.RearTrackM * 0.5, MountZ);
	CenterOfMassLocalM = FVector3d((Car.FrontWeightFraction - 0.5) * Car.WheelbaseM, 0.0,
		Car.CenterOfGravityHeightM - Car.CollisionShapeCenterYM);

	State = FSimCruiseVehicleSimState();
	State.EngineRpm = Car.IdleSpeedRpm;
	State.EngineOmegaRadPerSecond = Car.RestartEngineSpeedRadPerSecond;
	for (FSimCruiseWheelState& Wheel : State.Wheels)
	{
		Wheel.SuspensionLengthM = RestLengthM;
	}
}

FVector3d FSimCruiseVehicleSim::GetPrincipalInertiaKgM2() const
{
	return SimCruiseUnits::GodotInertiaToUnrealAxes(Car.InertiaKgM2);
}

void FSimCruiseVehicleSim::ResetState()
{
	for (FSimCruiseWheelState& Wheel : State.Wheels)
	{
		Wheel.RollingSlipAngleRad = 0.0;
		Wheel.AngularSpeedRadPerSecond = 0.0;
	}
	State.SoundThrottle = 0.0;
	State.SoundCombustion = 0.0;
	State.SoundShaftLoad = 0.0;
	State.SelectedGear = State.bManual ? 0 : 1;
	State.ClutchPedal = 0.0;
	State.bStalled = false;
	State.bRoofDown = false;
	State.FlippedSeconds = 0.0;
	State.StuckSeconds = 0.0;
	State.StrandedSeconds = 0.0;
	State.Gear = 0;
	State.bInReverse = false;
	State.ShiftTimeS = 0.0;
	State.SteeringRad = 0.0;
	State.SteeringInput = 0.0;
	State.bServiceBrake = false;
	State.EngineRpm = Car.IdleSpeedRpm;
	State.EngineOmegaRadPerSecond = State.EngineRpm * Tau / 60.0;
}

bool FSimCruiseVehicleSim::IsElectric() const
{
	return Car.EngineKind == TEXT("electric");
}

void FSimCruiseVehicleSim::SetManual(bool bEnabled, double BodySpeedMps)
{
	if (IsElectric())
	{
		bEnabled = false;
	}
	State.bManual = bEnabled;
	State.ClutchPedal = 0.0;
	State.bStalled = false;
	State.SelectedGear = State.bInReverse ? -1 : State.Gear + 1;
	State.EngineOmegaRadPerSecond = FMath::Max(State.EngineOmegaRadPerSecond, Car.RestartEngineSpeedRadPerSecond);
	if (State.bManual && BodySpeedMps < 1.0)
	{
		SelectRatio(0);
	}
}

void FSimCruiseVehicleSim::SelectRatio(int32 Ratio)
{
	State.SelectedGear = Ratio;
	State.bInReverse = Ratio == -1;
	State.Gear = FMath::Clamp(Ratio - 1, 0, FMath::Max(Car.ForwardGearRatios.Num() - 1, 0));
}

FString FSimCruiseVehicleSim::ChangeGear(int32 GearStep, double ForwardSpeedMps)
{
	if (!State.bManual)
	{
		return FString();
	}
	const int32 Proposed = FMath::Clamp(State.SelectedGear + GearStep, -1, Car.ForwardGearRatios.Num());
	if (Proposed == State.SelectedGear)
	{
		return FString();
	}
	if (State.ClutchPedal < 0.75)
	{
		return TEXT("Hold the clutch to change gear");
	}
	if ((Proposed == -1 && ForwardSpeedMps > 1.0) || (Proposed > 0 && ForwardSpeedMps < -1.0))
	{
		return TEXT("Stop before changing direction");
	}
	if (Proposed > 0 && FMath::Abs(ForwardSpeedMps) / TireRadiusM * Car.ForwardGearRatios[Proposed - 1] *
		Car.FinalDriveRatio * 60.0 / Tau > Car.MaximumGearSpeedRpm)
	{
		return TEXT("Speed is too high for that gear");
	}
	SelectRatio(Proposed);
	return FString();
}

void FSimCruiseVehicleSim::StartEngine()
{
	if (State.bManual && State.bStalled && (State.SelectedGear == 0 || State.ClutchPedal > 0.75))
	{
		State.EngineOmegaRadPerSecond = Car.RestartEngineSpeedRadPerSecond;
		State.EngineRpm = Car.IdleSpeedRpm;
		State.bStalled = false;
	}
}

FString FSimCruiseVehicleSim::GetGearText() const
{
	if (IsElectric())
	{
		return State.bInReverse ? TEXT("R") : TEXT("D");
	}
	if (State.bManual)
	{
		if (State.SelectedGear == 0)
		{
			return TEXT("N");
		}
		return State.SelectedGear == -1 ? FString(TEXT("R")) : FString::FromInt(State.SelectedGear);
	}
	return State.bInReverse ? FString(TEXT("R")) : FString::FromInt(State.Gear + 1);
}

double FSimCruiseVehicleSim::AvailableSteeringRad(double SpeedMps, bool bAssisted) const
{
	const double Speed = FMath::Abs(SpeedMps);
	if (!bAssisted)
	{
		const double RoadAngle = RoadReferenceRad / (1.0 + Speed * 0.044);
		return FMath::Lerp(ParkingRackRad, RoadAngle, SmoothStep(2.0, 6.0, Speed));
	}
	// The Ackermann angle for the assist's lateral acceleration plus the slip
	// surplus this tyre model carries at the limit, never more than the lock.
	const double LimitAngle = FMath::Atan(Car.WheelbaseM * Car.SteeringAssistLateralG * SimCruiseDynamics::GravityMps2 /
		FMath::Max(Speed * Speed, 0.01)) + LimitSlipSurplusRad;
	return SmoothMin(ParkingRackRad, LimitAngle, LockBlendRad);
}

double FSimCruiseVehicleSim::KeyRackRateRadPerSecond(double SpeedMps) const
{
	return FMath::Lerp(KeyRackRateParkingRadPerSecond, KeyRackRateHighwayRadPerSecond,
		SmoothStep(5.0, 25.0, FMath::Abs(SpeedMps)));
}

double FSimCruiseVehicleSim::ThumbstickCommand(double Stick, double SpeedMps) const
{
	// A stick has a few millimetres of travel: give its centre to lane
	// corrections and keep full available steering at either end.
	Stick = FMath::Clamp(Stick, -1.0, 1.0);
	const double SpeedSquared = FMath::Max(SpeedMps * SpeedMps, 0.01);
	const double AngleForCornering = FMath::Atan(Car.WheelbaseM * 0.89 * SimCruiseDynamics::GravityMps2 / SpeedSquared);
	const double CenterGain = FMath::Clamp(AngleForCornering / AvailableSteeringRad(SpeedMps, bSteeringAssist), 0.16, 1.0);
	return Stick * (CenterGain + (1.0 - CenterGain) * Stick * Stick);
}

double FSimCruiseVehicleSim::AdvanceSteering(double Current, double Target, double DeltaTimeS, double ApplyRate, double ReturnRate)
{
	// Spend the actual fraction of this interval returning through centre; a
	// correction beyond centre releases the lock as promptly as letting go.
	const double UnwindTarget = Current * Target < 0.0 ? 0.0 : Target;
	if (FMath::Abs(UnwindTarget) < FMath::Abs(Current))
	{
		const double UnwindSeconds = FMath::Abs(Current - UnwindTarget) / ReturnRate;
		Current = MoveToward(Current, UnwindTarget, DeltaTimeS * ReturnRate);
		DeltaTimeS = FMath::Max(0.0, DeltaTimeS - UnwindSeconds);
	}
	return MoveToward(Current, Target, DeltaTimeS * ApplyRate);
}

double FSimCruiseVehicleSim::DrivenOmega() const
{
	const FSimCruiseWheelState* W = State.Wheels;
	if (Car.DrivetrainLayout == TEXT("fwd"))
	{
		return (W[0].AngularSpeedRadPerSecond + W[1].AngularSpeedRadPerSecond) * 0.5;
	}
	if (Car.DrivetrainLayout == TEXT("awd"))
	{
		const double Share = Car.AwdFrontTorqueShareFraction;
		return Share * (W[0].AngularSpeedRadPerSecond + W[1].AngularSpeedRadPerSecond) * 0.5 +
			(1.0 - Share) * (W[2].AngularSpeedRadPerSecond + W[3].AngularSpeedRadPerSecond) * 0.5;
	}
	return (W[2].AngularSpeedRadPerSecond + W[3].AngularSpeedRadPerSecond) * 0.5;
}

double FSimCruiseVehicleSim::MotorTorqueNm(double Throttle, double Ratio, double SignDirection, double SpeedMps)
{
	// Constant torque to the base speed, constant power above it, nothing past
	// the motor's top speed; light regeneration when coasting.
	const double MotorOmega = DrivenOmega() * SignDirection * Ratio;
	double Available = FMath::Min(Car.ElectricMotorPeakTorqueNm, Car.ElectricMotorPeakPowerW / FMath::Max(FMath::Abs(MotorOmega), 1.0)) * Car.EngineTorqueScale;
	if (FMath::Abs(MotorOmega) * 60.0 / Tau > Car.ElectricMotorMaximumSpeedRpm)
	{
		Available = 0.0;
	}
	double Torque = Available * Throttle;
	if (Throttle < 0.01 && FMath::Abs(SpeedMps) > 1.0)
	{
		Torque = -Car.ElectricRegenerativeTorqueNm * SmoothStep(1.0, 5.0, FMath::Abs(SpeedMps)) * FMath::Sign(MotorOmega);
	}
	State.EngineOmegaRadPerSecond = FMath::Abs(MotorOmega);
	return Torque;
}

void FSimCruiseVehicleSim::Step(double DeltaTimeS, int32 Substeps, const FSimCruiseVehicleControls& Controls,
	const FSimCruiseBodyState& Body, FSimCruiseGroundQuery Ground, FSimCruiseBodyForces& OutForces)
{
	OutForces = FSimCruiseBodyForces();
	if (DeltaTimeS <= 0.0)
	{
		return;
	}
	Substeps = FMath::Max(Substeps, 1);
	const double Dt = DeltaTimeS;
	const double R = TireRadiusM;
	const double I = WheelInertiaKgM2;
	const double MassKg = Car.MassKg;

	const FVector3d Forward = Body.Rotation.RotateVector(FVector3d::UnitX());
	const FVector3d Right = Body.Rotation.RotateVector(FVector3d::UnitY());
	const FVector3d Up = Body.Rotation.RotateVector(FVector3d::UnitZ());
	const FVector3d Velocity = Body.LinearVelocityMps;
	const FVector3d Omega = Body.AngularVelocityRadPerSecond;
	const double Speed = Velocity.Dot(Forward);
	State.ForwardSpeedMps = Speed;

	// Pedals. The automatic changes direction only near standstill, like a
	// real selector's reverse inhibitor: a pedal below the brake threshold
	// while rolling the other way coasts instead of engaging the other gear.
	const double Gas = FMath::Clamp(Controls.Throttle, 0.0, 1.0);
	const double Back = FMath::Clamp(Controls.Brake, 0.0, 1.0);
	const bool bFoot = Controls.bFullBrake;
	double Throttle = 0.0;
	State.bServiceBrake = bFoot || (Back > 0.01 && Speed > 0.45) || (Gas > 0.01 && Speed < -0.45);
	if (!State.bServiceBrake && !State.bManual)
	{
		if (Gas > 0.0 && Speed > -0.45)
		{
			State.bInReverse = false;
			Throttle = Gas;
		}
		else if (Back > 0.0 && Speed < 0.45)
		{
			State.bInReverse = true;
			Throttle = Back;
		}
	}
	double BrakeAmount = bFoot ? 1.0 : (State.bServiceBrake ? FMath::Max(Back, Gas) : 0.0);
	// Launch control: the brake (or the full service brake) and the full accelerator together
	// at a standstill arm it; letting go of the brake launches from its engine speed until the
	// accelerator lifts or the car passes 15 m/s.
	if (Car.LaunchControlRpm > 0.0 && !State.bManual && !IsElectric())
	{
		const bool bHolding = (bFoot || Back > 0.5) && Gas > 0.8 && FMath::Abs(Speed) < 0.45;
		if (bHolding)
		{
			State.bLaunchArmed = true;
			State.bLaunching = false;
		}
		else if (State.bLaunchArmed)
		{
			State.bLaunchArmed = false;
			State.bLaunching = Gas > 0.8 && Back < 0.1 && !bFoot;
		}
		if (State.bLaunching && (Gas < 0.5 || Speed > 15.0))
		{
			State.bLaunching = false;
		}
		if (State.bLaunchArmed)
		{
			State.bInReverse = false;
			State.bServiceBrake = true;
			BrakeAmount = 1.0;
			Throttle = Gas;
		}
	}
	else
	{
		State.bLaunchArmed = false;
		State.bLaunching = false;
	}
	if (State.bManual)
	{
		Throttle = Gas;
		State.bServiceBrake = bFoot || Back > 0.01;
		BrakeAmount = FMath::Max(Back, bFoot ? 1.0 : 0.0);
		State.bInReverse = State.SelectedGear == -1;
		const double Pedal = FMath::Clamp(Controls.Clutch, 0.0, 1.0);
		State.ClutchPedal = MoveToward(State.ClutchPedal, Pedal, Dt * (Pedal > State.ClutchPedal ? 14.0 : 2.8));
	}
	State.BrakeAmount = BrakeAmount;

	// Steering rack.
	const double Cap = AvailableSteeringRad(Speed, bSteeringAssist);
	double Turn = Controls.bThumbstickSteer ? ThumbstickCommand(Controls.Steer, Speed) : FMath::Clamp(Controls.Steer, -1.0, 1.0);
	if (FMath::Abs(Turn) < SteerDeadBand)
	{
		Turn = 0.0;
	}
	if (Controls.bDirectRack)
	{
		// The shell's shaper owns rates and caps; the rack only stops at the lock.
		State.SteeringRad = FMath::Clamp(Controls.RackAngleRad, -ParkingRackRad, ParkingRackRad);
		State.SteeringInput = Cap > 1.0e-6 ? FMath::Clamp(State.SteeringRad / Cap, -1.0, 1.0) : 0.0;
	}
	else if (Controls.bThumbstickSteer)
	{
		State.SteeringInput = AdvanceSteering(State.SteeringInput, Turn, Dt, 2.75, 4.4);
	}
	else if (bSteeringAssist)
	{
		// Key rates are set in rack angle, so the same key moves the wheel more
		// slowly as the cap closes with speed. Releasing or countersteering unwinds first.
		const double Apply = KeyRackRateRadPerSecond(Speed) / Cap;
		const double ReturnRate = FMath::Max(Apply * KeyReturnFactor, KeyReturnRateMinimumRadPerSecond / Cap);
		State.SteeringInput = AdvanceSteering(State.SteeringInput, Turn, Dt, Apply, ReturnRate);
	}
	else
	{
		State.SteeringInput = MoveToward(State.SteeringInput, Turn, Dt * (Turn != 0.0 ? 2.75 : 4.4));
	}
	if (!Controls.bDirectRack)
	{
		State.SteeringRad = State.SteeringInput * Cap;
	}
	// Steering compliance: the front tyres' aligning torque (last step's) winds the column,
	// rack mounts and tie rods, so the road wheels turn a little less than the rack asks.
	// It is a real share of a road car's understeer (often a quarter to a half of it).
	const double Steering = Car.SteeringStiffnessNmPerDeg > 0.0
		? State.SteeringRad + State.RackAligningTorqueNm / SimCruiseUnits::ToDegrees(1.0) / Car.SteeringStiffnessNmPerDeg
		: State.SteeringRad;

	State.bRoofDown = Up.Dot(FVector3d::UnitZ()) < 0.2;
	State.FlippedSeconds = State.bRoofDown ? State.FlippedSeconds + Dt : 0.0;

	// Ackermann: the inner wheel turns more, by ackermann_blend of the full geometry.
	const double HalfTrack = Car.FrontTrackM * 0.5;
	const double Blend = Car.AckermannBlendFraction;
	const double Rack = FMath::Abs(Steering);
	const double TurnRadius = Car.WheelbaseM / FMath::Max(0.001, FMath::Tan(Rack));
	const double InnerAngle = FMath::Lerp(Rack, FMath::Atan(Car.WheelbaseM / FMath::Max(0.2, TurnRadius - HalfTrack)), Blend);
	const double OuterAngle = FMath::Lerp(Rack, FMath::Atan(Car.WheelbaseM / (TurnRadius + HalfTrack)), Blend);
	const double ToeRad = SimCruiseUnits::ToRadians(Car.ToeAngleDegPerWheel);
	const double CasterRad = SimCruiseUnits::ToRadians(Car.CasterAngleDeg);

	struct FContactRow
	{
		bool bValid = false;
		FVector3d Point = FVector3d::ZeroVector;
		FVector3d Normal = FVector3d::UnitZ();
		double Load = 0.0;
		FVector3d WheelForward = FVector3d::UnitX();
		FVector3d WheelRight = FVector3d::UnitY();
		double ForwardSpeed = 0.0;
		double LateralSpeed = 0.0;
		double Mu = 0.0;
		double SideForce = 0.0;
		/** TMeasy: tan(relaxed slip angle) and surface times axle grip. */
		double SlipY = 0.0;
		double GripScale = 1.0;
		double MeanLateral = 0.0;
		double MeanLongitudinal = 0.0;
	};
	FContactRow Rows[4];
	double Lengths[4] = { RestLengthM, RestLengthM, RestLengthM, RestLengthM };
	State.WheelContactCount = 0;

	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		FSimCruiseWheelState& Wheel = State.Wheels[WheelIndex];
		const FVector3d Source = Body.PositionM + Body.Rotation.RotateVector(MountsLocalM[WheelIndex]);
		FSimCruiseGroundHit Hit;
		Wheel.bContact = false;
		// The tyre is a tread, not a point: rays at its centre and near both shoulders. The
		// highest one carries the wheel (a kerb edge under one shoulder lifts it, a pothole
		// narrower than the tread does not drop it), and the grip is the tread's mean.
		bool bHit = false;
		{
			const double Shoulder = 0.4 * Car.TireSectionWidthM;
			const FVector3d Offsets[3] = { FVector3d::ZeroVector, Right * Shoulder, Right * -Shoulder };
			double GripSum = 0.0;
			int32 GripCount = 0;
			double Best = -1.0e9;
			double CentreHeight = -1.0e9;
			for (int32 Ray = 0; Ray < (Car.ContactRays >= 3 ? 3 : 1); ++Ray)
			{
				FSimCruiseGroundHit Probe;
				const FVector3d From = Source + Offsets[Ray];
				if (Ground(From, From - Up * (RestLengthM + R), Probe) && Probe.NormalM.Dot(Up) > 0.33)
				{
					GripSum += Probe.SurfaceGrip;
					++GripCount;
					const double Height = (Probe.PointM - Source).Dot(Up) + Probe.RoadProfileM;
					if (Ray == 0)
					{
						CentreHeight = Height;
						Best = Height;
						Hit = Probe;
						bHit = true;
					}
					else if (Height > Best + 0.01)
					{
						// A step under a shoulder (a kerb, a pothole's edge) carries the tyre. The
						// contact stays under the wheel's centre: on even ground, and as the body
						// rolls, the shoulders must not widen the track.
						Best = Height;
						if (!bHit || CentreHeight < -1.0e8)
						{
							Hit = Probe;
							bHit = true;
						}
						else
						{
							Hit.PointM += Up * (Height - (Hit.PointM - Source).Dot(Up) - Hit.RoadProfileM);
							Hit.NormalM = Probe.NormalM;
						}
					}
				}
			}
			if (bHit)
			{
				Hit.SurfaceGrip = GripSum / GripCount;
			}
		}
		// A tyre does not stand on a wall: a surface more than 75 deg from level (a bank the car slid
		// into, the ground of a car on its side or roof) gives the wheel no contact; the body's own
		// collision meets it. Such hits once drove the suspension into huge loads, and the car off
		// in a spin at hundreds of m/s.
		if (bHit && Hit.NormalM.Dot(Up) < 0.25)
		{
			bHit = false;
		}
		if (bHit)
		{
			FContactRow& Row = Rows[WheelIndex];
			const int32 Axle = WheelIndex < 2 ? 0 : 1;
			const double Length = FMath::Clamp((Source - Hit.PointM).Dot(Up) - R - Hit.RoadProfileM,
				Car.MinimumSuspensionTravelM, RestLengthM);
			Lengths[WheelIndex] = Length;
			const FVector3d Arm = Hit.PointM - Body.CenterOfMassM;
			const FVector3d PointVelocity = Velocity + Omega.Cross(Arm);
			// At most ten times the corner's static share: a hard bump-stop hit is three to five, and
			// a step past ten is a landing the integration cannot carry in one substep.
			const double Load = FMath::Min(SimCruiseDynamics::ComputeSuspensionLoadN(Car, Axle, Length,
				PointVelocity.Dot(Hit.NormalM) - Hit.RoadProfileRateMps), 10.0 * 0.25 * Car.MassKg * SimCruiseDynamics::GravityMps2);

			// Road-wheel angle, positive right; the inner wheel is the right one
			// in a right turn. Toe-in turns each wheel toward the centreline.
			const double Side = (WheelIndex % 2 == 0) ? -1.0 : 1.0;
			double Angle = 0.0;
			if (WheelIndex < 2)
			{
				const bool bInner = (WheelIndex == 1 && Steering > 0.0) || (WheelIndex == 0 && Steering < 0.0);
				Angle = FMath::Sign(Steering) * (bInner ? InnerAngle : OuterAngle);
			}
			Angle -= Side * ToeRad;
			const double CamberDeg = AxleValue(Car.StaticCamberDegByAxle, Axle) -
				(Car.StaticSuspensionLengthM - Length) * AxleValue(Car.CamberGainDegPerMByAxle, Axle);
			double Inclination = Side * SimCruiseUnits::ToRadians(CamberDeg);
			if (WheelIndex < 2)
			{
				Inclination += FMath::Sin(Angle) * CasterRad;
			}
			Wheel.InclinationRad = Inclination;
			Wheel.SteerAngleRad = Angle;

			FVector3d WheelForward = Forward * FMath::Cos(Angle) + Right * FMath::Sin(Angle);
			WheelForward = (WheelForward - Hit.NormalM * WheelForward.Dot(Hit.NormalM)).GetSafeNormal();
			const FVector3d WheelRight = Hit.NormalM.Cross(WheelForward).GetSafeNormal();

			Row.bValid = true;
			Row.Point = Hit.PointM;
			Row.Normal = Hit.NormalM;
			Row.Load = Load;
			Row.WheelForward = WheelForward;
			Row.WheelRight = WheelRight;
			Row.ForwardSpeed = PointVelocity.Dot(WheelForward);
			Row.LateralSpeed = PointVelocity.Dot(WheelRight);
			Wheel.bContact = true;
			Wheel.ContactPointM = Hit.PointM;
			Wheel.ContactNormal = Hit.NormalM;
			Wheel.SurfaceGrip = Hit.SurfaceGrip;
			Wheel.ForwardSpeedMps = Row.ForwardSpeed;
			Wheel.LateralSpeedMps = Row.LateralSpeed;
			++State.WheelContactCount;
		}
		Wheel.SuspensionLengthM = Lengths[WheelIndex];
	}

	// Stranded: roof down for 1.5 s, or no wheel touching for 2 s while nearly still.
	State.StuckSeconds = (State.WheelContactCount == 0 && Velocity.Size() < 1.0) ? State.StuckSeconds + Dt : 0.0;
	State.StrandedSeconds = (State.FlippedSeconds > 1.5 || State.StuckSeconds > 2.0) ? State.StrandedSeconds + Dt : 0.0;

	// Anti-roll bars move load from the extended to the compressed side.
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		if (Rows[WheelIndex].bValid)
		{
			Rows[WheelIndex].Load = FMath::Max(0.0, Rows[WheelIndex].Load +
				(Lengths[WheelIndex ^ 1] - Lengths[WheelIndex]) * AntiRollRateNPerM[WheelIndex < 2 ? 0 : 1]);
		}
	}

	// Roll centres: the links carry the axle's side force at the roll centre's height straight to
	// the outer tyre (F h_rc / track, the geometric load transfer), at once and without rolling the
	// body; the springs carry only the rest. The body's forces are unchanged in sum. From the
	// last substep's side forces (positive to the right moves load to the left, even, wheel).
	for (int32 Axle = 0; Axle < 2; ++Axle)
	{
		const double Height = AxleValue(Car.RollCentreHeightMByAxle, Axle);
		const int32 Left = Axle * 2;
		if (Height <= 0.0 || !Rows[Left].bValid || !Rows[Left + 1].bValid)
		{
			continue;
		}
		const double Track = Axle == 0 ? Car.FrontTrackM : Car.RearTrackM;
		const double Side = State.Wheels[Left].LateralForceN + State.Wheels[Left + 1].LateralForceN;
		const double Transfer = FMath::Clamp(Side * Height / FMath::Max(Track, 0.5),
			-Rows[Left].Load, Rows[Left + 1].Load);
		Rows[Left].Load += Transfer;
		Rows[Left + 1].Load -= Transfer;
	}

	// Gear selection.
	double ForwardRatio = (State.bInReverse ? Car.ReverseGearRatio : Car.ForwardGearRatios[State.Gear]) * Car.FinalDriveRatio;
	const double SignDirection = State.bInReverse ? -1.0 : 1.0;
	State.ShiftTimeS = FMath::Max(0.0, State.ShiftTimeS - Dt);
	if (!State.bManual && State.ShiftTimeS <= 0.0 && !State.bInReverse)
	{
		// The shift map follows the pedal (Throttle is the driver's pedal here,
		// before traction control trims it).
		const int32 Next = SimCruiseDynamics::SelectAutomaticGear(Car, State.Gear, State.EngineRpm,
			FMath::Abs(Speed) * ForwardRatio / R, Throttle, State.bServiceBrake || BrakeAmount > 0.01,
			Speed * Omega.Z / SimCruiseDynamics::GravityMps2);
		if (Next != State.Gear)
		{
			State.Gear = Next;
			State.ShiftTimeS = Car.GearShiftDurationS;
			ForwardRatio = Car.ForwardGearRatios[State.Gear] * Car.FinalDriveRatio;
		}
	}

	// Lateral force per contact from this interval's relaxed slip angle.
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		FSimCruiseWheelState& Wheel = State.Wheels[WheelIndex];
		FContactRow& Row = Rows[WheelIndex];
		if (!Row.bValid)
		{
			Wheel.LoadN = 0.0;
			Wheel.RollingSlipAngleRad = 0.0;
			Wheel.SlipAngleRad = 0.0;
			Wheel.LongitudinalForceN = 0.0;
			Wheel.LateralForceN = 0.0;
			continue;
		}
		const double Fz = Row.Load;
		Wheel.LoadN = Fz;
		double SlipAngle = 0.0;
		if (Fz > 0.0)
		{
			SlipAngle = SimCruiseDynamics::ComputeRelaxedSlipAngleRad(Car, Row.ForwardSpeed, Row.LateralSpeed, Dt, Fz,
				Wheel.RollingSlipAngleRad);
		}
		else
		{
			Wheel.RollingSlipAngleRad = 0.0;
		}
		Wheel.SlipAngleRad = SlipAngle;
		const int32 Axle = WheelIndex < 2 ? 0 : 1;
		Row.Mu = SimCruiseDynamics::ComputeFrictionCoefficient(Car, Axle, Fz, Wheel.SurfaceGrip);
		Wheel.FrictionCoefficient = Row.Mu;
		const double RoadLean = FMath::Asin(FMath::Clamp(Right.Dot(Row.Normal), -1.0, 1.0));
		if (SimCruiseDynamics::UsesTmeasy(Car))
		{
			// The slip force comes from the combined TMeasy curve in each substep; here only
			// the camber thrust (the same term the legacy law adds).
			Row.SlipY = FMath::Tan(FMath::Clamp(SlipAngle, -1.4, 1.4));
			Row.GripScale = FMath::Max(Wheel.SurfaceGrip, 0.0) * AxleValue(Car.AxleGripMultiplier, Axle, 1.0);
			SimCruiseDynamics::FTmeasyCurve CurveX, CurveY;
			SimCruiseDynamics::GetTmeasyCurves(Car, Fz, Row.GripScale, CurveX, CurveY);
			Row.Mu = Fz > 1.0 ? CurveY.PeakForce / Fz : 0.0;
			Wheel.FrictionCoefficient = Row.Mu;
			Row.SideForce = SimCruiseDynamics::ComputeLateralForceN(Car, Fz, Row.Mu, 0.0,
				Wheel.InclinationRad - RoadLean, Row.ForwardSpeed);
		}
		else
		{
			Row.SideForce = SimCruiseDynamics::ComputeLateralForceN(Car, Fz, Row.Mu, SlipAngle,
				Wheel.InclinationRad - RoadLean, Row.ForwardSpeed);
		}
	}

	const bool bThrottleAid = Car.bTractionControlEnabled;
	if (!bThrottleAid || Throttle <= 0.0)
	{
		State.TcCut = 0.0;
		State.TcIntegral = 0.0;
		State.TcSlip = 0.0;
		State.TcSlipFiltered = 0.0;
	}
	const double PedalThrottle = Throttle;
	State.PedalThrottle = PedalThrottle;
	State.SoundThrottle = Throttle;
	State.SoundCombustion = 0.0;
	State.SoundShaftLoad = 0.0;
	const bool bElectric = IsElectric();
	const double Ds = Dt / Substeps;
	const double SubstepWeight = 1.0 / Substeps;
	const double ParkingBrakeNm = Controls.bHandbrake ? Car.ParkingBrakeTorqueNm : 0.0;
	double AxleSpeed = 0.0;

	for (int32 Substep = 0; Substep < Substeps; ++Substep)
	{
		// Traction control: torque reduction from how far the most-slipping
		// driven wheel is past its target slip.
		if (bThrottleAid && PedalThrottle > 0.0)
		{
			double Ratio = 0.0;
			for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
			{
				const FSimCruiseWheelState& Wheel = State.Wheels[WheelIndex];
				if (Car.IsDrivenWheel(WheelIndex) && Wheel.LoadN > 0.0)
				{
					Ratio = FMath::Max(Ratio, Wheel.TractionControlSlip * SignDirection /
						FMath::Max(FMath::Max(Car.TractionControlTargetSlipRatio, Wheel.PeakSlip), 0.01));
				}
			}
			State.TcSlipFiltered += (Ratio - State.TcSlipFiltered) * (1.0 - FMath::Exp(-Ds / TcFilterS));
			State.TcSlip = MoveToward(State.TcSlip, State.TcSlipFiltered, TcSlipSlewPerSecond * Ds);
			const double Excess = State.TcSlip - 1.0;
			if (State.ShiftTimeS <= 0.0)
			{
				// While the clutch slips at its capacity the wheels take the
				// flywheel, not the throttle: cutting harder would not help.
				const double IntegralStep = Ds * Car.TractionControlIntegralGainPerSecond * FMath::Clamp(Excess, -1.0, 0.5);
				if (!State.bClutchSaturated || IntegralStep < 0.0)
				{
					State.TcIntegral = FMath::Clamp(State.TcIntegral + IntegralStep, 0.0, 0.9);
				}
				const double Wanted = FMath::Clamp(State.TcIntegral + TcProportional * FMath::Clamp(Excess, -1.0, 2.0) +
					TcOvershoot * FMath::Max(Excess - 1.0, 0.0), 0.0, 0.9);
				State.TcCut = FMath::Clamp(Wanted, State.TcCut - TcCutRateDown * Ds,
					State.TcCut + (State.bClutchSaturated ? 0.0 : TcCutRateUp * Ds));
			}
			Throttle = PedalThrottle * (1.0 - State.TcCut);
		}

		double ClutchLoad = 0.0;
		// Torque the gearbox input receives: the clutch's, multiplied by a slipping torque converter.
		double OutputLoad = 0.0;
		if (bElectric)
		{
			ClutchLoad = MotorTorqueNm(Throttle, ForwardRatio, SignDirection, Speed);
			State.SoundCombustion += FMath::Abs(ClutchLoad) / (Substeps * FMath::Max(Car.ElectricMotorPeakTorqueNm, 1.0));
			State.SoundShaftLoad += ClutchLoad / (Substeps * FMath::Max(Car.ElectricMotorPeakTorqueNm, 1.0));
		}
		else
		{
			State.EngineRpm = State.EngineOmegaRadPerSecond * 60.0 / Tau;
			const double Rpm = State.EngineRpm;
			const double Friction = Car.EngineFrictionTorqueNm + Car.EngineFrictionTorqueNmPerRpm * Rpm;
			// The pedal opens the throttle plate; the cylinders fill by how much air that opening
			// passes against what the engine draws at this speed (SimCruiseDynamics::EngineLoad).
			const double Load = SimCruiseDynamics::EngineLoad(Car, Throttle, Rpm);
			double Torque = Rpm < Car.FuelCutSpeedRpm ? (SimCruiseDynamics::SampleEngineTorqueNm(Car, Rpm) + Friction) * Load : 0.0;
			double Drag = Friction * (Rpm > Car.IdleSpeedRpm ? 1.0 : 0.5);
			// A closed throttle also pumps against manifold vacuum; at full throttle that loss is
			// already in the published curve.
			if (Rpm > Car.IdleSpeedRpm)
			{
				Drag += (Car.EnginePumpingTorqueNm + Car.EnginePumpingTorqueNmPerRpm * Rpm) * (1.0 - Load);
			}
			double Governor = FMath::Clamp((Car.GovernorSpeedRpm - Rpm) * 0.8, 0.0, 100.0);
			AxleSpeed = DrivenOmega() * SignDirection;
			// The clutch (or converter, dual clutch) couples engine and driven
			// wheels in proportion to their speed difference, never harder than
			// the torque that closes the gap in one substep. In low gears that cap
			// binds below clutch_stiffness, so it is part of each car's
			// calibrated launch: it is evaluated at the substep the cars were
			// calibrated with (Godot's 1/360 s), not the actual one, which keeps
			// the clutch the same at every physics rate and is stable for any
			// substep up to that length.
			const double Reflected = ForwardRatio * ForwardRatio * Car.DrivelineEfficiencyFraction /
				(I * (Car.DrivetrainLayout == TEXT("awd") ? 4.0 : 2.0));
			const double Coupling = FMath::Min(Car.ClutchCouplingNmPerRadPerSecond,
				1.0 / (FMath::Max(Ds, ClutchCalibrationSubstepS) * (1.0 / Car.EngineRotationalInertiaKgM2 + Reflected)));
			ClutchLoad = FMath::Clamp((State.EngineOmegaRadPerSecond - AxleSpeed * ForwardRatio) * Coupling,
				-Car.ClutchBackTorqueNm, Car.ClutchCapacityNm);
			// The launch speed follows the pedal, not the torque traction control leaves.
			const double LaunchTargetRpm = State.bLaunching ? Car.LaunchControlRpm
				: Car.LaunchSpeedRpm + PedalThrottle * Car.LaunchSpeedRpmPerThrottleFraction;
			if (State.bManual)
			{
				const double Capacity = Car.ClutchCapacityNm * (1.0 - SmoothStep(0.16, 0.88, State.ClutchPedal));
				// A dry clutch with the pedal up locks: below its capacity it carries whatever torque
				// keeps engine and gearbox together. That stiffness is the most a substep can take
				// without overshooting (the 1/(dt I) cap the automatic's coupling uses); slipping
				// with the pedal part-way down it stays the calibrated viscous coupling.
				const double LockCoupling = 1.0 / (FMath::Max(Ds, ClutchCalibrationSubstepS) * (1.0 / Car.EngineRotationalInertiaKgM2 + Reflected));
				const double Engaged = 1.0 - SmoothStep(0.02, 0.16, State.ClutchPedal);
				const double ManualCoupling = FMath::Lerp(FMath::Min(Car.ManualClutchCouplingNmPerRadPerSecond, LockCoupling), LockCoupling, Engaged);
				// Locked, the clutch also carries this substep's engine torque, so engine and
				// gearbox end the substep at one speed instead of slipping by torque / stiffness.
				// A stalled engine makes no torque and has no governor: settle that first, so the
				// locked clutch does not carry torque the engine no longer makes.
				if (State.bStalled)
				{
					Torque = 0.0;
					Governor = 0.0;
					Drag = FMath::Min(26.0, State.EngineOmegaRadPerSecond * 0.85);
				}
				const double Gap = State.EngineOmegaRadPerSecond - AxleSpeed * ForwardRatio;
				const double EngineAhead = Engaged * (Torque + Governor - Drag) * Ds / Car.EngineRotationalInertiaKgM2;
				ClutchLoad = State.SelectedGear == 0 ? 0.0 : FMath::Clamp((Gap + EngineAhead) * ManualCoupling, -Capacity, Capacity);
			}
			else if (AxleSpeed * ForwardRatio * 60.0 / Tau < LaunchTargetRpm && ClutchLoad > 0.0)
			{
				ClutchLoad = FMath::Min(ClutchLoad, FMath::Max(0.0, Torque - Drag + (Rpm - LaunchTargetRpm) * 0.16));
			}
			// Launch control armed: the clutch is open and the engine is held at its launch speed.
			if (State.bLaunchArmed)
			{
				ClutchLoad = 0.0;
				if (Rpm > Car.LaunchControlRpm)
				{
					Torque = 0.0;
				}
			}
			// A gear change interrupts the drive, except on a dual clutch.
			if (State.ShiftTimeS > 0.0 && Car.TransmissionKind != TEXT("dct"))
			{
				ClutchLoad = 0.0;
				Torque *= 0.12;
			}
			if (!State.bManual && Throttle < 0.01 && Rpm < Car.IdleSpeedRpm + 200.0 && FMath::Abs(Speed) < 3.0)
			{
				ClutchLoad = 0.0;
			}
			OutputLoad = ClutchLoad;
			// A torque converter (automatics only, not in manual mode) multiplies the torque it
			// passes while the turbine runs slower than the engine: stall_ratio at a standstill,
			// falling linearly to 1 at the coupling point (typical 1.8-2.4 and 0.85-0.9).
			if (!State.bManual && Car.TransmissionKind == TEXT("automatic") && Car.TorqueConverterStallRatio > 1.0 && ClutchLoad > 0.0)
			{
				const double SpeedRatio = FMath::Clamp(AxleSpeed * ForwardRatio / FMath::Max(State.EngineOmegaRadPerSecond, 1.0), 0.0, 1.0);
				const double CouplingPoint = FMath::Max(Car.TorqueConverterCouplingSpeedRatio, 0.1);
				OutputLoad = ClutchLoad * (1.0 + (Car.TorqueConverterStallRatio - 1.0) * FMath::Max(0.0, 1.0 - SpeedRatio / CouplingPoint));
			}
			State.SoundCombustion += (Torque + Governor) / (Substeps * 180.0);
			State.SoundShaftLoad += ClutchLoad / (Substeps * 180.0);
			State.EngineOmegaRadPerSecond = FMath::Clamp(
				State.EngineOmegaRadPerSecond + (Torque + Governor - Drag - ClutchLoad) * Ds / Car.EngineRotationalInertiaKgM2,
				State.bManual ? 0.0 : Car.MinimumAutomaticEngineSpeedRadPerSecond,
				Car.MaximumEngineSpeedRadPerSecond);
			if (State.bManual && !State.bStalled && State.EngineOmegaRadPerSecond < Car.StallEngineSpeedRadPerSecond)
			{
				State.bStalled = true;
			}
		}
		State.bClutchSaturated = !bElectric && ClutchLoad >= Car.ClutchCapacityNm * 0.98;
		State.ClutchTorqueNm = ClutchLoad;

		if (bElectric)
		{
			OutputLoad = ClutchLoad;
		}
		const double AxleTorque = OutputLoad * ForwardRatio * Car.DrivelineEfficiencyFraction * SignDirection;
		double WheelOmegas[4];
		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			WheelOmegas[WheelIndex] = State.Wheels[WheelIndex].AngularSpeedRadPerSecond;
		}
		double DriveTorque[4];
		SimCruiseDynamics::SplitDriveTorqueToWheels(Car, AxleTorque, WheelOmegas, Ds,
			bElectric ? FMath::Abs(ClutchLoad) > 0.0 : ClutchLoad > 0.0, DriveTorque);

		for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
		{
			FSimCruiseWheelState& Wheel = State.Wheels[WheelIndex];
			FContactRow& Row = Rows[WheelIndex];
			const double RearPedal = SimCruiseDynamics::RearBrakePedal(Car, BrakeAmount);
			double BrakeNm = WheelIndex < 2 ? BrakeAmount * Car.FrontBrakeTorqueNm : RearPedal * Car.RearBrakeTorqueNm;
			const double RowSpeed = Row.bValid ? Row.ForwardSpeed : 0.0;
			// ABS: a per-wheel pressure modulator cycling each tyre about its peak slip.
			if (Car.bAbsEnabled && BrakeNm > 0.0 && FMath::Abs(RowSpeed) > 1.5)
			{
				const double Target = Wheel.PeakSlip * AbsTargetOverPeak;
				// Braking slip is positive as the wheel turns slower than the road
				// in the direction of travel, forwards or in reverse.
				const double BrakingSlip = -Wheel.SlipRatio * FMath::Sign(RowSpeed);
				const double SlipError = (Target - BrakingSlip) / Target;
				Wheel.AbsRelease = FMath::Clamp(Wheel.AbsRelease + Ds * AbsGainPerSecond * FMath::Clamp(SlipError, -3.0, 1.0), 0.05, 1.0);
				BrakeNm *= FMath::Clamp(Wheel.AbsRelease + AbsProportional * FMath::Clamp(SlipError, -1.0, 0.0), 0.0, 1.0);
			}
			else
			{
				Wheel.AbsRelease = 1.0;
			}
			if (WheelIndex > 1)
			{
				BrakeNm = FMath::Max(BrakeNm, ParkingBrakeNm);
			}
			const double Spin = MoveToward(Wheel.AngularSpeedRadPerSecond + DriveTorque[WheelIndex] * Ds / I, 0.0, BrakeNm * Ds / I);
			if (!Row.bValid)
			{
				Wheel.AngularSpeedRadPerSecond = Spin;
				continue;
			}

			FSimCruiseLongitudinalSubstepInput Sub;
			Sub.NormalLoadN = Row.Load;
			Sub.FrictionCoefficient = Row.Mu;
			Sub.ForwardSpeedMps = Row.ForwardSpeed;
			Sub.LateralSpeedMps = Row.LateralSpeed;
			Sub.LateralForceN = Row.SideForce;
			Sub.StartAngularSpeedRadPerSecond = Wheel.AngularSpeedRadPerSecond;
			Sub.SpinAfterTorqueRadPerSecond = Spin;
			Sub.BrakeTorqueNm = BrakeNm;
			Sub.TireRadiusM = R;
			Sub.WheelInertiaKgM2 = I;
			Sub.SubstepS = Ds;
			Sub.LateralSlip = Row.SlipY;
			Sub.GripScale = Row.GripScale;
			const FSimCruiseLongitudinalSubstepOutput Out = SimCruiseDynamics::SolveLongitudinalSubstep(Car, Sub);
			Wheel.SlipRatio = Out.SlipRatio;
			Wheel.TractionControlSlip = Out.TractionControlSlip;
			Wheel.PeakSlip = Out.PeakSlipRatio;
			Wheel.GripUse = Out.GripUseFraction;
			Wheel.AngularSpeedRadPerSecond = Out.EndAngularSpeedRadPerSecond;
			Row.MeanLongitudinal += Out.LongitudinalForceN * SubstepWeight;
			Row.MeanLateral += Out.LateralForceN * SubstepWeight;
		}
	}

	// Forces at each contact patch, then aero and rolling resistance at the
	// centre of mass.
	State.RackAligningTorqueNm = 0.0;
	for (int32 WheelIndex = 0; WheelIndex < 4; ++WheelIndex)
	{
		FSimCruiseWheelState& Wheel = State.Wheels[WheelIndex];
		Wheel.SpinAngleRad = FMath::Fmod(Wheel.SpinAngleRad + Wheel.AngularSpeedRadPerSecond * Dt, Tau);
		const FContactRow& Row = Rows[WheelIndex];
		if (!Row.bValid)
		{
			Wheel.AligningTorqueNm = 0.0;
			continue;
		}
		Wheel.LongitudinalForceN = Row.MeanLongitudinal;
		Wheel.LateralForceN = Row.MeanLateral;
		// Aligning torque: the side force acts a trail behind the steering axis (the tyre's
		// pneumatic trail plus the caster's R sin(caster)); a force to the right steers the
		// wheel to the left. Fades in from walking pace, where the trail has no meaning.
		{
			const double Trail = SimCruiseDynamics::TmeasyPneumaticTrailM(Car, Row.Load, FMath::Tan(FMath::Clamp(Wheel.SlipAngleRad, -1.4, 1.4)))
				+ (WheelIndex < 2 ? R * FMath::Sin(CasterRad) : 0.0);
			Wheel.AligningTorqueNm = -Trail * Row.MeanLateral * SmoothStep(0.5, 3.0, FMath::Abs(Row.ForwardSpeed));
			if (WheelIndex < 2)
			{
				State.RackAligningTorqueNm += Wheel.AligningTorqueNm;
			}
		}
		const FVector3d Force = Row.Normal * Row.Load + Row.WheelForward * Row.MeanLongitudinal + Row.WheelRight * Row.MeanLateral;
		OutForces.bWheelActive[WheelIndex] = true;
		OutForces.WheelPointM[WheelIndex] = Row.Point;
		OutForces.WheelForceN[WheelIndex] = Force;
		OutForces.ForceN += Force;
		OutForces.TorqueNm += (Row.Point - Body.CenterOfMassM).Cross(Force);
	}
	// Drag on the speed through the air: a headwind adds to it, a tailwind takes from it, a
	// crosswind pushes the car sideways (at the centre of mass; no yaw moment from it yet).
	const FVector3d AirVelocity = Velocity - WindMps;
	const double AirSpeed = AirVelocity.Size();
	if (AirSpeed > 0.02)
	{
		OutForces.ForceN += -AirVelocity / AirSpeed * 0.5 * AirDensityKgPerM3 * Car.DragAreaM2 * AirSpeed * AirSpeed;
	}
	const double SpeedMagnitude = Velocity.Size();
	if (SpeedMagnitude > 0.02)
	{
		const FVector3d Rolling(Velocity.X, Velocity.Y, 0.0);
		const double RollingSpeed = Rolling.Size();
		if (RollingSpeed > 0.01 && State.WheelContactCount > 0)
		{
			OutForces.ForceN += -Rolling / RollingSpeed *
				FMath::Min(RollingSpeed * MassKg / Dt, MassKg * SimCruiseDynamics::GravityMps2 * Car.RollingResistanceCoefficient);
		}
	}
	State.EngineRpm = State.EngineOmegaRadPerSecond * 60.0 / Tau;
}
