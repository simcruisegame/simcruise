// Copyright SimCruise contributors. All Rights Reserved.

#include "Tests/VehicleBenches.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/PlatformTime.h"
#include "Tests/VehicleBenchWorld.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

#include <atomic>

namespace
{
	constexpr double Gravity = SimCruiseUnits::StandardGravityMps2;
	constexpr double Tau = 2.0 * UE_DOUBLE_PI;

	/** Godot's car origin (ground at ride height) for a body state. */
	FVector3d CarOrigin(const FSimCruiseVehicleSim& Sim, const FSimCruiseBodyState& Body)
	{
		return Body.PositionM + Body.Rotation.RotateVector(Sim.GetReferenceOriginLocalM());
	}

	void RestartAt(FSimCruiseVehicleDriverContext& C, const FVector3d& BodyPositionM)
	{
		C.Body.PositionM = BodyPositionM;
		C.Body.Rotation = FQuat4d::Identity;
		C.Body.LinearVelocityMps = FVector3d::ZeroVector;
		C.Body.AngularVelocityRadPerSecond = FVector3d::ZeroVector;
		C.bBodyStateChanged = true;
		C.Sim.ResetState();
	}

	TArray<double> Fingerprint(const FSimCruiseVehicleSim& Sim, const FSimCruiseBodyState& Body)
	{
		TArray<double> Values;
		Values.Append({ Body.PositionM.X, Body.PositionM.Y, Body.PositionM.Z,
			Body.Rotation.X, Body.Rotation.Y, Body.Rotation.Z, Body.Rotation.W,
			Body.LinearVelocityMps.X, Body.LinearVelocityMps.Y, Body.LinearVelocityMps.Z,
			Body.AngularVelocityRadPerSecond.X, Body.AngularVelocityRadPerSecond.Y, Body.AngularVelocityRadPerSecond.Z,
			Sim.State.EngineOmegaRadPerSecond });
		for (const FSimCruiseWheelState& Wheel : Sim.State.Wheels)
		{
			Values.Add(Wheel.AngularSpeedRadPerSecond);
			Values.Add(Wheel.RollingSlipAngleRad);
		}
		return Values;
	}

	/** checks/CarTrack.gd as a physics-step driver. */
	struct FCarTrackDriver
	{
		static constexpr double Radius = 45.72;
		double Dt = 1.0 / 360.0;
		double TargetSkidpadG = 0.89;
		/** Launch control: hold the brake with the full accelerator this long, then let go (0: none). */
		double LaunchHoldS = 0.0;
		double LaunchHeldS = 0.0;
		/** Where the launch began, and when the car had rolled its first foot. */
		double LaunchStartX = TNumericLimits<double>::Max();
		double RolloutT = -1.0;
		bool bAccelerationOnly = false;
		bool bTrace = !FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_BENCH_TRACE")).IsEmpty();
		FVector3d StartBodyPositionM = FVector3d::ZeroVector;

		int32 State = 0;
		int64 Tick = 0;
		double T = 0.0;
		double Last = 0.0;
		double StartX = 0.0;
		bool bReached = false;
		double SpeedI = 0.0;
		double RampTurn = 0.0;
		double RampSpeed = 0.0;
		TArray<double> Window;
		int32 WindowSize = 360;
		std::atomic<bool> bDone{ false };
		FSimCruiseCarTrackResult Result;

		void Finish(FSimCruiseVehicleDriverContext& C)
		{
			Result.Fingerprint = Fingerprint(C.Sim, C.Body);
			Result.SimSeconds = C.SimTimeS;
			Result.bCompleted = !Result.bTimedOut;
			bDone = true;
		}

		void Step(FSimCruiseVehicleDriverContext& C)
		{
			C.Controls = FSimCruiseVehicleControls();
			if (bDone)
			{
				return;
			}
			++Tick;
			T += Dt;
			if (Tick > FMath::RoundToInt64(600.0 / Dt))
			{
				Result.bTimedOut = true;
				Finish(C);
				return;
			}
			const FVector3d Forward = C.Body.Rotation.RotateVector(FVector3d::UnitX());
			const FVector3d Right = C.Body.Rotation.RotateVector(FVector3d::UnitY());
			const double Speed = C.Body.LinearVelocityMps.Dot(Forward);
			const double OriginX = CarOrigin(C.Sim, C.Body).X;
			// More than 5 g of deceleration is no tyre: the car struck something.
			// (Not while settling, nor on the step after the restart teleport.)
			if (State != 0 && State != 3 && (Last - Speed) > 5.0 * Gravity * Dt)
			{
				Result.bCollided = true;
			}
			switch (State)
			{
			case 0:
				// Settle 1.5 s on the springs.
				if (Tick == FMath::RoundToInt64(1.5 / Dt))
				{
					T = 0.0;
					State = 1;
				}
				break;
			case 1:
				C.Controls.Throttle = 1.0;
				// A launch-control run, as the magazines time it: brake and full throttle held,
				// then the brake released; the clock starts on the release.
				if (LaunchHeldS < LaunchHoldS)
				{
					C.Controls.Brake = 1.0;
					LaunchHeldS += Dt;
					T = 0.0;
					break;
				}
				if (bTrace && !bReached && Tick % FMath::Max<int64>(1, FMath::RoundToInt64(0.25 / Dt)) == 0)
				{
					const FSimCruiseVehicleSimState& St = C.Sim.State;
					UE_LOG(LogTemp, Display, TEXT("LAUNCHTRACE t %.2f v %.1f mph gear %d rpm %.0f slip %.2f tc %.3f clutch %.0f front %.0f rear %.0f N"),
						T, SimCruiseUnits::MpsToMph(Speed), St.Gear + 1, St.EngineRpm,
						(St.Wheels[2].AngularSpeedRadPerSecond * C.Sim.GetTireRadiusM() - Speed) / FMath::Max(Speed, 1.0), St.TcCut, St.ClutchTorqueNm,
						St.Wheels[0].LoadN + St.Wheels[1].LoadN, St.Wheels[2].LoadN + St.Wheels[3].LoadN);
				}
				if (LaunchStartX == TNumericLimits<double>::Max())
				{
					LaunchStartX = OriginX;
				}
				if (RolloutT < 0.0 && FMath::Abs(OriginX - LaunchStartX) >= 0.3048)
				{
					RolloutT = T;
				}
				if (Speed >= 26.8224 && !bReached)
				{
					Result.ZeroToSixtyS = T - Dt + (26.8224 - Last) / FMath::Max(0.0001, Speed - Last) * Dt;
					Result.ZeroToSixtyRolloutS = Result.ZeroToSixtyS - FMath::Max(RolloutT, 0.0);
					bReached = true;
					if (bAccelerationOnly)
					{
						Finish(C);
						return;
					}
				}
				if (Speed >= 31.2928)
				{
					StartX = OriginX - (Speed - 31.2928) * Dt / 2.0;
					State = 2;
					T = 0.0;
				}
				if (T > 40.0)
				{
					Result.ZeroToSixtyS = TNumericLimits<double>::Max();
					StartX = OriginX;
					State = 2;
					T = 0.0;
				}
				break;
			case 2:
				C.Controls.Brake = 1.0;
				if (bTrace && Tick % FMath::Max<int64>(1, FMath::RoundToInt64(0.25 / Dt)) == 0)
				{
					FString Wheels;
					for (const FSimCruiseWheelState& W : C.Sim.State.Wheels)
					{
						Wheels += FString::Printf(TEXT(" | fz %.0f roll %.2f k %.3f"), W.LoadN,
							W.AngularSpeedRadPerSecond * C.Sim.GetTireRadiusM() / FMath::Max(Speed, 0.1), W.SlipRatio);
					}
					UE_LOG(LogTemp, Display, TEXT("BRAKETRACE t %.2f v %.2f%s"), T, Speed, *Wheels);
				}
				if (Speed < 0.1)
				{
					Result.SeventyToZeroFt = SimCruiseUnits::MetresToFeet(FMath::Abs(StartX - OriginX));
					State = 3;
					RestartAt(C, StartBodyPositionM);
					C.Controls = FSimCruiseVehicleControls();
					T = 0.0;
				}
				break;
			case 3:
				if (T >= 1.0)
				{
					State = 5;
					T = 0.0;
					RampTurn = 0.0;
					Result.SkidpadG = 0.0;
					Window.Reset();
					SpeedI = 0.0;
					RampSpeed = FMath::Sqrt(TargetSkidpadG * Gravity * Radius);
				}
				break;
			case 5:
			{
				// Ramp steer at the skidpad speed: steering grows 0.6 %/s after 6 s
				// at speed; the peak 1 s mean of lateral acceleration (v·yaw rate)
				// before the body slip angle passes 10° is the steady limit.
				SpeedI = FMath::Clamp(SpeedI + (RampSpeed - Speed) * Dt * 0.12, 0.0, 0.75);
				if (T > 6.0)
				{
					RampTurn += Dt * 0.006;
				}
				C.Controls.Throttle = FMath::Clamp((RampSpeed - Speed) * 0.65 + SpeedI, 0.0, 1.0);
				// Godot's ramp turns left; keep the same direction.
				C.Controls.Steer = -RampTurn;
				const double LateralG = FMath::Abs(Speed * C.Body.AngularVelocityRadPerSecond.Z) / Gravity;
				Window.Add(LateralG);
				if (Window.Num() > WindowSize)
				{
					Window.RemoveAt(0, EAllowShrinking::No);
				}
				double Mean = 0.0;
				for (const double Sample : Window)
				{
					Mean += Sample;
				}
				Mean /= Window.Num();
				const double Beta = FMath::Abs(FMath::Atan2(C.Body.LinearVelocityMps.Dot(Right), FMath::Max(FMath::Abs(Speed), 1.0)));
				if (T > 6.0 && FMath::Abs(Speed - RampSpeed) < 0.6)
				{
					Result.SkidpadG = FMath::Max(Result.SkidpadG, Mean);
				}
				const bool bEnd = Beta > SimCruiseUnits::ToRadians(10.0) || T > 120.0 || RampTurn >= 1.0;
				if (bTrace && (Tick % FMath::Max<int64>(1, FMath::RoundToInt64(0.5 / Dt)) == 0 || bEnd))
				{
					FString Wheels;
					for (const FSimCruiseWheelState& W : C.Sim.State.Wheels)
					{
						Wheels += FString::Printf(TEXT(" | fz %.0f a %.2f k %.3f len %.4f c %.3f"), W.LoadN, SimCruiseUnits::ToDegrees(W.RollingSlipAngleRad),
							W.SlipRatio, W.SuspensionLengthM, SimCruiseUnits::ToDegrees(W.InclinationRad));
					}
					UE_LOG(LogTemp, Display, TEXT("SKIDTRACE t %.2f v %.2f turn %.4f rack %.2f latg %.3f mean %.3f beta %.2f%s"), T, Speed, RampTurn,
						SimCruiseUnits::ToDegrees(C.Sim.State.SteeringRad), LateralG, Mean, SimCruiseUnits::ToDegrees(Beta), *Wheels);
				}
				if (bEnd)
				{
					Finish(C);
					return;
				}
				break;
			}
			default:
				break;
			}
			Last = Speed;
		}
	};

	/** checks/LiftOff.gd as a physics-step driver, all cases for one car. */
	struct FLiftOffDriver
	{
		static constexpr double SpinDeg = 8.0;
		static constexpr double WindowS = 3.0;
		static constexpr double TrailBrakeG = 0.15;
		double Dt = 1.0 / 360.0;
		double Radius = 40.0;
		double MassKg = 1000.0;
		double BrakeSumNm = 1800.0;
		FVector3d StartBodyPositionM = FVector3d::ZeroVector;
		TArray<FSimCruiseLiftOffCase> Cases;
		int32 CaseIndex = 0;
		bool bBegun = false;
		int32 State = 0;
		double T = 0.0;
		double SpeedI = 0.0;
		double RadialI = 0.0;
		double TargetSpeed = 0.0;
		double HoldTurn = 0.0;
		double HoldGas = 0.0;
		double Pedal = 0.0;
		double SteadyYaw = 0.0;
		struct FHistory { double RadiusError, SpeedError, Turn, Gas, Yaw, Beta, Speed; };
		TArray<FHistory> History;
		int32 HistorySize = 720;
		struct FSample { double T, Yaw, Beta, Speed; };
		TArray<FSample> Samples;
		bool bWrongGear = false;
		/** Log the wheels every 0.1 s after the lift (SIMCRUISE_TURNTRACE=1). */
		bool bTrace = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_TURNTRACE")) == TEXT("1");
		std::atomic<bool> bDone{ false };

		int32 RoadGear(const FSimCruiseVehicleSim& Sim, double Speed) const
		{
			const FSimCruiseCarDefinition& Car = Sim.GetCar();
			for (int32 Gear = 0; Gear < Car.ForwardGearRatios.Num(); ++Gear)
			{
				if (Speed / Sim.GetTireRadiusM() * Car.ForwardGearRatios[Gear] * Car.FinalDriveRatio * 60.0 / Tau < 4000.0)
				{
					return Gear;
				}
			}
			return Car.ForwardGearRatios.Num() - 1;
		}

		void Begin(FSimCruiseVehicleDriverContext& C)
		{
			if (CaseIndex >= Cases.Num())
			{
				bDone = true;
				return;
			}
			RestartAt(C, StartBodyPositionM);
			TargetSpeed = FMath::Sqrt(Cases[CaseIndex].TargetG * Gravity * Radius);
			State = 0;
			T = 0.0;
			SpeedI = 0.0;
			RadialI = 0.0;
			History.Reset();
			Samples.Reset();
			bWrongGear = false;
			Pedal = Cases[CaseIndex].BrakePedal >= 0.0 ? Cases[CaseIndex].BrakePedal
				: TrailBrakeG * MassKg * Gravity * C.Sim.GetTireRadiusM() / (2.0 * BrakeSumNm);
		}

		void Report()
		{
			FSimCruiseLiftOffCase& Case = Cases[CaseIndex];
			const double R0 = FMath::Abs(SteadyYaw);
			double R1 = 0.0, B1 = 0.0, PeakR = 0.0, PeakB = -TNumericLimits<double>::Max(), V0 = 0.0, V1 = 0.0;
			for (const FSample& S : Samples)
			{
				const double Aligned = FMath::Abs(S.Yaw) * FMath::Sign(S.Yaw * SteadyYaw);
				PeakR = FMath::Max(PeakR, Aligned);
				PeakB = FMath::Max(PeakB, S.Beta);
				if (S.T <= 1.0) { R1 = Aligned; B1 = S.Beta; }
				if (S.T <= 0.5) { V0 = S.Speed; }
				if (S.T <= 1.5) { V1 = S.Speed; }
			}
			const double FinalB = Samples.Num() ? Samples.Last().Beta : 0.0;
			double Settle = 0.0;
			for (const FSample& S : Samples)
			{
				if (FMath::Abs(S.Beta - FinalB) > 1.0)
				{
					Settle = S.T;
				}
			}
			Case.YawRatio1s = R1 / FMath::Max(R0, 1.0e-4);
			Case.YawOvershoot = PeakR / FMath::Max(R0, 1.0e-4) - 1.0;
			Case.Beta1sDeg = B1;
			Case.PeakBetaDeg = PeakB;
			Case.SettleS = Settle;
			Case.DecelG = (V0 - V1) / Gravity;
			Case.bSpun = PeakB > SpinDeg;
			Case.bReverseAtSpeed = bWrongGear;
		}

		void Step(FSimCruiseVehicleDriverContext& C)
		{
			if (bDone)
			{
				C.Controls = FSimCruiseVehicleControls();
				return;
			}
			if (!bBegun)
			{
				bBegun = true;
				Begin(C);
			}
			T += Dt;
			const FVector3d Forward = C.Body.Rotation.RotateVector(FVector3d::UnitX());
			const FVector3d Right = C.Body.Rotation.RotateVector(FVector3d::UnitY());
			const FVector3d Velocity = C.Body.LinearVelocityMps;
			const double Speed = Velocity.Dot(Forward);
			const double YawRate = C.Body.AngularVelocityRadPerSecond.Z;
			// Tail-out sideslip positive. The circle turns left, where Unreal's yaw rate is negative.
			const double TurnSign = FMath::Abs(YawRate) > 0.01 ? -FMath::Sign(YawRate) : 1.0;
			const double Beta = SimCruiseUnits::ToDegrees(FMath::Atan2(Velocity.Dot(Right), FMath::Max(FMath::Abs(Speed), 0.5))) * TurnSign;
			double Gas = 0.0, Brake = 0.0, Turn = HoldTurn;
			FSimCruiseLiftOffCase& Case = Cases[CaseIndex];
			switch (State)
			{
			case 0:
				if (T >= 0.5)
				{
					C.Body.LinearVelocityMps = Forward * TargetSpeed;
					C.bBodyStateChanged = true;
					for (FSimCruiseWheelState& Wheel : C.Sim.State.Wheels)
					{
						Wheel.AngularSpeedRadPerSecond = TargetSpeed / C.Sim.GetTireRadiusM();
					}
					const FSimCruiseCarDefinition& Car = C.Sim.GetCar();
					C.Sim.State.Gear = RoadGear(C.Sim, TargetSpeed);
					C.Sim.State.EngineOmegaRadPerSecond = TargetSpeed / C.Sim.GetTireRadiusM() *
						Car.ForwardGearRatios[C.Sim.State.Gear] * Car.FinalDriveRatio;
					C.Sim.State.EngineRpm = C.Sim.State.EngineOmegaRadPerSecond * 60.0 / Tau;
					State = 1;
					T = 0.0;
				}
				break;
			case 1:
			{
				// Circle driver (as Track.gd): aim 10 m ahead on the circle, trim
				// the radius error with an integrator; speed by PI throttle. The
				// centre is Radius to the car's starting left.
				const FVector3d Origin = CarOrigin(C.Sim, C.Body);
				const FVector2d Centre(StartBodyPositionM.X, StartBodyPositionM.Y - Radius);
				const FVector2d Relative = FVector2d(Origin.X, Origin.Y) - Centre;
				const double Current = Relative.Size();
				const double Angle = FMath::Atan2(Relative.Y, Relative.X) - 10.0 / Radius;
				const FVector2d Ahead = Centre + FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius - FVector2d(Origin.X, Origin.Y);
				const double Desired = FMath::Atan2(Ahead.Y, Ahead.X);
				const double Yaw = FMath::Atan2(Forward.Y, Forward.X);
				RadialI = FMath::Clamp(RadialI + (Current - Radius) * Dt * 0.08, -0.3, 0.6);
				Turn = FMath::Clamp(FMath::UnwindRadians(Desired - Yaw) * 1.9 - RadialI, -1.0, 1.0);
				SpeedI = FMath::Clamp(SpeedI + (TargetSpeed - Speed) * Dt * 0.3, 0.0, 0.9);
				Gas = FMath::Clamp((TargetSpeed - Speed) * 0.65 + SpeedI, 0.0, 1.0);
				History.Add({ FMath::Abs(Current - Radius), FMath::Abs(Speed - TargetSpeed), Turn, Gas, YawRate, Beta, Speed });
				if (History.Num() > HistorySize)
				{
					History.RemoveAt(0, EAllowShrinking::No);
				}
				bool bSettled = History.Num() == HistorySize && T > 6.0;
				for (const FHistory& H : History)
				{
					bSettled = bSettled && H.RadiusError < 0.6 && H.SpeedError < 0.15;
				}
				if (bSettled || T > 25.0)
				{
					if (!bSettled)
					{
						Case.bSteady = false;
						++CaseIndex;
						Begin(C);
						C.Controls = FSimCruiseVehicleControls();
						return;
					}
					const double N = History.Num();
					HoldTurn = 0.0;
					HoldGas = 0.0;
					double R0 = 0.0, B0 = 0.0, V0 = 0.0;
					for (const FHistory& H : History)
					{
						HoldTurn += H.Turn / N;
						HoldGas += H.Gas / N;
						R0 += H.Yaw / N;
						B0 += H.Beta / N;
						V0 += H.Speed / N;
					}
					SteadyYaw = R0;
					Case.bSteady = true;
					Case.SteadyLateralG = FMath::Abs(V0 * R0) / Gravity;
					Case.SteadyBetaDeg = B0;
					Case.SteadyGear = C.Sim.State.Gear + 1;
					Case.SteadyRackDeg = FMath::Abs(SimCruiseUnits::ToDegrees(C.Sim.State.SteeringRad));
					Turn = HoldTurn;
					Gas = HoldGas;
					State = 2;
					T = 0.0;
				}
				break;
			}
			case 2:
				Gas = HoldGas * FMath::Clamp(1.0 - T / 0.1, 0.0, 1.0);
				// The right foot moves across (0.1 s) and squeezes the pedal over
				// 0.3 s, through the lightest positions on the way.
				if (Case.bBrake)
				{
					Brake = Pedal * FMath::Clamp((T - 0.2) / 0.3, 0.0, 1.0);
				}
				if (bTrace && FMath::RoundToInt64(T / Dt) % FMath::Max<int64>(1, FMath::RoundToInt64(0.1 / Dt)) == 0)
				{
					FString Wheels;
					for (const FSimCruiseWheelState& W : C.Sim.State.Wheels)
					{
						Wheels += FString::Printf(TEXT(" | fz %5.0f k %+.3f a %+5.1f fx %+5.0f fy %+5.0f"), W.LoadN, W.SlipRatio,
							SimCruiseUnits::ToDegrees(W.SlipAngleRad), W.LongitudinalForceN, W.LateralForceN);
					}
					UE_LOG(LogTemp, Display, TEXT("TURNTRACE p %.2f t %.2f v %.1f beta %+.1f yaw %+.3f brake %.2f%s"),
						Case.BrakePedal, T, Speed, Beta, YawRate, Brake, *Wheels);
				}
				// Below 3 m/s the sideslip angle means little (and a stopped car has none).
				if (Speed > 3.0)
				{
					Samples.Add({ T, YawRate, Beta, Speed });
				}
				// A wheel turning at under a tenth of the road speed is locked.
				if (Case.bBrake && Case.FirstLockAxle < 0 && Speed > 3.0)
				{
					for (int32 Index = 0; Index < 4; ++Index)
					{
						if (C.Sim.State.Wheels[Index].AngularSpeedRadPerSecond * C.Sim.GetTireRadiusM() < 0.1 * Speed)
						{
							Case.FirstLockAxle = Index < 2 ? 0 : 1;
							Case.FirstLockS = T;
							break;
						}
					}
				}
				bWrongGear = bWrongGear || (C.Sim.State.bInReverse && Speed > 1.0);
				if (T >= WindowS || FMath::Abs(Beta) > 45.0 || (Speed < 0.5 && T > 0.5))
				{
					Report();
					++CaseIndex;
					Begin(C);
					C.Controls = FSimCruiseVehicleControls();
					return;
				}
				break;
			default:
				break;
			}
			C.Controls = FSimCruiseVehicleControls();
			C.Controls.Throttle = Gas;
			C.Controls.Brake = Brake;
			C.Controls.Steer = Turn;
		}
	};
}

FSimCruiseCarTrackResult SimCruiseRunCarTrack(const FSimCruiseCarDefinition& Car, double FixedStepS,
	double MaxWheelSubstepS, bool bAccelerationOnly)
{
	TSharedRef<FCarTrackDriver> Driver = MakeShared<FCarTrackDriver>();
	Driver->Dt = FixedStepS;
	Driver->TargetSkidpadG = Car.TargetSkidpadG;
	Driver->LaunchHoldS = Car.LaunchControlRpm > 0.0 ? 2.0 : 0.0;
	Driver->bAccelerationOnly = bAccelerationOnly;
	Driver->WindowSize = FMath::Max(1, FMath::RoundToInt32(1.0 / FixedStepS));
	const FVector3d Ground(0.0, 0.0, 0.18);
	Driver->StartBodyPositionM = FSimCruiseVehicleBenchWorld::BodyLocationForGround(Car, Ground);

	FSimCruiseVehicleBenchWorld World(FixedStepS);
	if (!World.IsValid())
	{
		return Driver->Result;
	}
	ASimCruiseVehicle* Vehicle = World.SpawnVehicle(Car, Ground,
		[Driver](FSimCruiseVehicleDriverContext& C) { Driver->Step(C); }, false, MaxWheelSubstepS);
	if (Vehicle == nullptr)
	{
		return Driver->Result;
	}

	const double WallStart = FPlatformTime::Seconds();
	World.RunUntil([&Driver]() { return Driver->bDone.load(); }, 700.0);
	FSimCruiseCarTrackResult Result = Driver->Result;
	Result.WallSeconds = FPlatformTime::Seconds() - WallStart;
	const FSimCruiseVehicleTelemetry Telemetry = Vehicle->GetVehicleTelemetry();
	Result.PhysicsSteps = Telemetry.PhysicsSteps;
	Result.MeanStepMicroseconds = Telemetry.MeanStepMicroseconds;
	return Result;
}

FSimCruiseLiftOffResult SimCruiseRunLiftOff(const FSimCruiseCarDefinition& Car, double FixedStepS,
	const TArray<double>& LevelsG, double RadiusM)
{
	TSharedRef<FLiftOffDriver> Driver = MakeShared<FLiftOffDriver>();
	Driver->Dt = FixedStepS;
	Driver->Radius = RadiusM;
	Driver->MassKg = Car.MassKg;
	Driver->BrakeSumNm = Car.FrontBrakeTorqueNm + Car.RearBrakeTorqueNm;
	Driver->HistorySize = FMath::Max(1, FMath::RoundToInt32(2.0 / FixedStepS));
	const FVector3d Ground(0.0, 0.0, 0.15);
	Driver->StartBodyPositionM = FSimCruiseVehicleBenchWorld::BodyLocationForGround(Car, Ground);
	for (const double Level : LevelsG)
	{
		for (const bool bBrake : { false, true })
		{
			FSimCruiseLiftOffCase Case;
			Case.TargetG = Level;
			Case.bBrake = bBrake;
			Driver->Cases.Add(Case);
		}
	}

	FSimCruiseLiftOffResult Result;
	FSimCruiseVehicleBenchWorld World(FixedStepS);
	if (!World.IsValid())
	{
		return Result;
	}
	if (World.SpawnVehicle(Car, Ground, [Driver](FSimCruiseVehicleDriverContext& C) { Driver->Step(C); }) == nullptr)
	{
		return Result;
	}
	Result.bCompleted = World.RunUntil([&Driver]() { return Driver->bDone.load(); }, 60.0 * Driver->Cases.Num());
	Result.Cases = Driver->Cases;
	return Result;
}

FSimCruiseLiftOffResult SimCruiseRunBrakeInTurn(const FSimCruiseCarDefinition& Car, double FixedStepS,
	const TArray<double>& Pedals, double LateralG, double RadiusM)
{
	// ISO 7975: steady on the circle at the lateral level, throttle released and the brake
	// applied at once (here over 0.3 s), the steering held; the same LiftOff driver.
	TSharedRef<FLiftOffDriver> Driver = MakeShared<FLiftOffDriver>();
	Driver->Dt = FixedStepS;
	Driver->Radius = RadiusM;
	Driver->MassKg = Car.MassKg;
	Driver->BrakeSumNm = Car.FrontBrakeTorqueNm + Car.RearBrakeTorqueNm;
	Driver->HistorySize = FMath::Max(1, FMath::RoundToInt32(2.0 / FixedStepS));
	const FVector3d Ground(0.0, 0.0, 0.15);
	Driver->StartBodyPositionM = FSimCruiseVehicleBenchWorld::BodyLocationForGround(Car, Ground);
	for (const double Pedal : Pedals)
	{
		FSimCruiseLiftOffCase Case;
		Case.TargetG = LateralG;
		Case.bBrake = true;
		Case.BrakePedal = Pedal;
		Driver->Cases.Add(Case);
	}

	FSimCruiseLiftOffResult Result;
	FSimCruiseVehicleBenchWorld World(FixedStepS);
	if (!World.IsValid())
	{
		return Result;
	}
	if (World.SpawnVehicle(Car, Ground, [Driver](FSimCruiseVehicleDriverContext& C) { Driver->Step(C); }) == nullptr)
	{
		return Result;
	}
	Result.bCompleted = World.RunUntil([&Driver]() { return Driver->bDone.load(); }, 60.0 * Driver->Cases.Num());
	Result.Cases = Driver->Cases;
	return Result;
}

FSimCruiseStaticLoadResult SimCruiseRunStaticLoad(const FSimCruiseCarDefinition& Car, double FixedStepS, double SettleS)
{
	struct FStaticDriver
	{
		double SettleS = 4.0;
		std::atomic<bool> bDone{ false };
		FSimCruiseStaticLoadResult Result;
		void Step(FSimCruiseVehicleDriverContext& C)
		{
			C.Controls = FSimCruiseVehicleControls();
			if (bDone || C.SimTimeS < SettleS)
			{
				return;
			}
			for (int32 Index = 0; Index < 4; ++Index)
			{
				Result.LoadN[Index] = C.Sim.State.Wheels[Index].LoadN;
				Result.SuspensionLengthM[Index] = C.Sim.State.Wheels[Index].SuspensionLengthM;
				Result.TotalLoadN += Result.LoadN[Index];
			}
			Result.FrontFraction = (Result.LoadN[0] + Result.LoadN[1]) / FMath::Max(Result.TotalLoadN, 1.0);
			const FRotator Rotator = FRotator(FQuat(C.Body.Rotation));
			Result.PitchDeg = Rotator.Pitch;
			Result.RollDeg = Rotator.Roll;
			Result.ResidualSpeedMps = C.Body.LinearVelocityMps.Size();
			Result.bCompleted = true;
			bDone = true;
		}
	};
	TSharedRef<FStaticDriver> Driver = MakeShared<FStaticDriver>();
	Driver->SettleS = SettleS;
	FSimCruiseVehicleBenchWorld World(FixedStepS);
	if (!World.IsValid())
	{
		return Driver->Result;
	}
	if (World.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.05), [Driver](FSimCruiseVehicleDriverContext& C) { Driver->Step(C); }) == nullptr)
	{
		return Driver->Result;
	}
	World.RunUntil([&Driver]() { return Driver->bDone.load(); }, SettleS + 30.0);
	FSimCruiseStaticLoadResult Result = Driver->Result;
	Result.GravityMps2 = -SimCruiseUnits::ToSILength(static_cast<double>(World.GetWorld()->GetGravityZ()));
	return Result;
}

#endif
