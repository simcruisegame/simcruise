// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseCameraModels.h"

#include "Gameplay/SimCruiseDriverShaping.h"

namespace
{
	constexpr double G = SimCruiseShaping::StandardGravity;

	double Approach(double DeltaTimeS, double TimeConstantS)
	{
		return 1.0 - FMath::Exp(-DeltaTimeS / TimeConstantS);
	}
}

namespace SimCruiseLens
{
	double VerticalFovDeg(ESimCruiseLens Lens, double ScreenHeightCm, double ViewDistanceCm)
	{
		switch (Lens)
		{
		case ESimCruiseLens::Screen:
			if (ScreenHeightCm <= 1.0)
			{
				return NaturalVerticalDeg;
			}
			return FMath::Clamp(FMath::RadiansToDegrees(2.0 * FMath::Atan(ScreenHeightCm * 0.5 / FMath::Max(ViewDistanceCm, 20.0))), 10.0, 90.0);
		case ESimCruiseLens::Wide:
			return WideVerticalDeg;
		default:
			return NaturalVerticalDeg;
		}
	}

	double ScreenHeightCm(double DiagonalInches, double AspectRatio)
	{
		return DiagonalInches * 2.54 / FMath::Sqrt(1.0 + FMath::Square(FMath::Max(AspectRatio, 0.1)));
	}

	double TrueHorizontalFovDeg(double DiagonalInches, double AspectRatio, double ViewDistanceCm)
	{
		const double Width = ScreenHeightCm(DiagonalInches, AspectRatio) * FMath::Max(AspectRatio, 0.1);
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(Width * 0.5 / FMath::Max(ViewDistanceCm, 20.0)));
	}

	double HorizontalFromVerticalDeg(double VerticalDeg, double AspectRatio)
	{
		const double HalfV = FMath::DegreesToRadians(VerticalDeg) * 0.5;
		return FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(HalfV) * FMath::Max(AspectRatio, 0.1)));
	}

	const TCHAR* Label(ESimCruiseLens Lens)
	{
		switch (Lens)
		{
		case ESimCruiseLens::Screen: return TEXT("true FOV (as your eye sees the screen)");
		case ESimCruiseLens::Wide: return TEXT("wide (67°)");
		default: return TEXT("natural (45°)");
		}
	}
}

FRotator FSimCruiseEyePose::ToRotator() const
{
	return FRotationMatrix::MakeFromXZ(Forward, Up).Rotator();
}

FSimCruiseHeadModel::FSimCruiseHeadModel()
	: Rng(4181)
{
}

namespace SimCruiseCameraModels
{
	/**
	 * The acceleration between two velocity samples. With a physics time on both
	 * (the vehicle's asynchronous 360 Hz step), the difference is over that time:
	 * a 60 fps frame holds 5, 6 or 7 steps, so dividing by the frame time alone
	 * gives a +/-17 % jitter. A frame with no new physics result keeps the last
	 * acceleration. Without a physics time, the frame time is used.
	 */
	FVector Acceleration(const FVector& Velocity, FVector& PreviousVelocity, double SimTimeS, double& PreviousSimTimeS, double FrameS,
		FVector& LastAcceleration)
	{
		if (SimTimeS > 0.0 && PreviousSimTimeS > 0.0)
		{
			const double StepS = SimTimeS - PreviousSimTimeS;
			if (StepS <= 1e-6)
			{
				return LastAcceleration;
			}
			LastAcceleration = (Velocity - PreviousVelocity) / StepS;
		}
		else
		{
			LastAcceleration = (Velocity - PreviousVelocity) / FrameS;
		}
		PreviousVelocity = Velocity;
		PreviousSimTimeS = SimTimeS;
		return LastAcceleration;
	}
}

void FSimCruiseHeadModel::Reset()
{
	HeadOffset = FVector::ZeroVector;
	HeadVelocity = FVector::ZeroVector;
	Felt = FVector::ZeroVector;
	Vibration = FVector::ZeroVector;
	TravelSlow = 0.0;
	Roughness = 0.0;
	Gaze = 0.0;
	BrakePitch = 0.0;
	bStarted = false;
}

FSimCruiseEyePose FSimCruiseHeadModel::Update(const FTransform& Body, const FVector& WorldVelocityMps, const FSimCruiseVehicleSetup& Setup,
	const FSimCruiseDriveTelemetry& Telemetry, double DeltaTimeS)
{
	// Differentiate over the frame's real duration (a clamped step would
	// misstate the acceleration above 240 or below 20 fps); integrate the
	// spring and filters over a clamped step so a hitch cannot destabilise them.
	const double FrameS = FMath::Max(DeltaTimeS, 1e-4);
	const double Dt = FMath::Clamp(DeltaTimeS, 1.0 / 240.0, 1.0 / 20.0);
	if (!bStarted || FVector::Dist(WorldVelocityMps, PreviousVelocity) > 25.0)
	{
		PreviousVelocity = WorldVelocityMps;
		PreviousSimTimeS = Telemetry.SimTimeS;
		LastAcceleration = FVector::ZeroVector;
		bStarted = true;
		for (int32 I = 0; I < 4; ++I)
		{
			Travel[I] = Telemetry.Wheels[I].SuspensionTravelM;
		}
	}
	const FVector Raw = SimCruiseCameraModels::Acceleration(WorldVelocityMps, PreviousVelocity, Telemetry.SimTimeS, PreviousSimTimeS,
		FrameS, LastAcceleration);

	// Specific force the seat applies, in body axes: acceleration minus gravity.
	// Contact and tile-seam spikes are clamped before the filter.
	FVector Specific = Body.GetRotation().UnrotateVector(Raw + FVector(0.0, 0.0, G));
	Specific = Specific.BoundToCube(20.0);
	Felt += (Specific - Felt) * Approach(Dt, AccelFilterS);

	// Critically damped spring toward the steady offset, opposite to the felt force.
	const FVector Target(
		FMath::Clamp(-Felt.X / G * LongitudinalMPerG, -LongitudinalMaxM, LongitudinalMaxM),
		FMath::Clamp(-Felt.Y / G * LateralMPerG, -LateralMaxM, LateralMaxM),
		0.0);
	const FVector Spring = HeadOmega * HeadOmega * (Target - HeadOffset) - 2.0 * HeadOmega * HeadVelocity;
	HeadVelocity += Spring * Dt;
	HeadOffset += HeadVelocity * Dt;
	HeadOffset.X = FMath::Clamp(HeadOffset.X, -LongitudinalMaxM, LongitudinalMaxM);
	HeadOffset.Y = FMath::Clamp(HeadOffset.Y, -LateralMaxM, LateralMaxM);

	// Braking (the seat belt pulling back, felt X negative) dips the gaze.
	BrakePitch += (FMath::Clamp(-Felt.X / G, 0.0, 1.0) * BrakePitchDeg - BrakePitch) * Approach(Dt, 0.12);

	// Gaze lead, positive right: lateral acceleration and the rack both point into the corner.
	const double ForwardSpeed = Telemetry.BodyVelocityMps.X;
	const double HalfRange = 0.5 * SimCruiseShaping::AvailableSteeringRad(Setup, ForwardSpeed);
	double Lead = FMath::Clamp(Felt.Y / (0.5 * G), -1.0, 1.0) + 0.5 * FMath::Clamp(Telemetry.RackAngleRad / FMath::Max(HalfRange, 0.01), -1.0, 1.0);
	Lead = FMath::Clamp(Lead, -1.0, 1.0) * FMath::SmoothStep(3.0, 14.0, FMath::Abs(ForwardSpeed));
	Gaze += (Lead * GazeLeadDeg - Gaze) * Approach(Dt, 0.25);

	// Road roughness from the suspension travel rate, high-passed so steady
	// roll or dive does not shake the view.
	double Rate = 0.0;
	for (int32 I = 0; I < 4; ++I)
	{
		const double Y = Telemetry.Wheels[I].SuspensionTravelM;
		Rate += FMath::Min(1.5, FMath::Abs(Y - Travel[I]) / FrameS);
		Travel[I] = Y;
	}
	Rate *= 0.25;
	TravelSlow += (Rate - TravelSlow) * Approach(Dt, 0.5);
	Roughness += (FMath::Clamp((Rate - TravelSlow) / 0.35, 0.0, 1.0) - Roughness) * Approach(Dt, 0.1);
	const double Amplitude = VibrationM * (0.25 + 0.75 * FMath::Clamp(FMath::Abs(ForwardSpeed) / 30.0, 0.0, 1.0)) * (0.35 + 0.65 * Roughness);
	const FVector Shake = FVector(0.4 * Rng.FRandRange(-1.0, 1.0), Rng.FRandRange(-1.0, 1.0), Rng.FRandRange(-1.0, 1.0)) * Amplitude;
	Vibration += (Shake - Vibration) * (1.0 - FMath::Exp(-Dt * 30.0));

	const double Scale = FMath::Clamp(HeadMotion, 0.0, 1.0);
	const FVector EyeLocal = Setup.DriverEyeLocalCm + (HeadOffset + Vibration) * Scale * 100.0;
	const FVector LookLocal = FRotator(-BrakePitch * Scale, Gaze * Scale, 0.0).RotateVector(Setup.DriverSightLocalDirection.GetSafeNormal());

	const FVector BodyUp = Body.GetRotation().GetUpVector();
	const FQuat Level = FQuat::FindBetweenNormals(BodyUp, FVector::UpVector);
	FVector Up = FQuat::Slerp(FQuat::Identity, Level, 1.0 - HorizonFollow).RotateVector(BodyUp);
	if (Up.SizeSquared() < 0.25)
	{
		Up = BodyUp;
	}

	FSimCruiseEyePose Pose;
	Pose.Location = Body.TransformPosition(EyeLocal);
	Pose.Forward = Body.TransformVectorNoScale(LookLocal).GetSafeNormal();
	Pose.Up = Up.GetSafeNormal();
	return Pose;
}

double FSimCruiseChaseFrame::TargetSwingRad(const FVector& BodyVelocityMps, bool& bOutHold)
{
	bOutHold = false;
	const double Vx = BodyVelocityMps.X;
	const double Vy = BodyVelocityMps.Y;
	if (Vx < 0.0)
	{
		// Reversing: no swing. Travelling more sideways than backwards is a spin: hold.
		bOutHold = FMath::Abs(Vy) > FMath::Abs(Vx) && FMath::Sqrt(Vx * Vx + Vy * Vy) > 2.0;
		return 0.0;
	}
	const double Beta = FMath::Clamp(FMath::Atan2(Vy, FMath::Abs(Vx)), -MaxBetaRad, MaxBetaRad);
	return ChaseSlipShare * Beta * FMath::SmoothStep(2.0, 8.0, Vx);
}

void FSimCruiseChaseFrame::Reset()
{
	Swing = 0.0;
	bHasLocation = false;
}

void FSimCruiseChaseFrame::Update(ESimCruiseView View, const FTransform& Body, const FVector& BodyVelocityMps, double DeltaTimeS, bool bSnap,
	FVector& OutLocation, FVector& OutLookAt)
{
	const double Dt = FMath::Clamp(DeltaTimeS, 0.0, 0.1);
	bool bHold = false;
	const double TargetSwing = TargetSwingRad(BodyVelocityMps, bHold);
	if (bSnap)
	{
		Swing = bHold ? Swing : TargetSwing;
	}
	else if (!bHold)
	{
		Swing += (TargetSwing - Swing) * (1.0 - FMath::Exp(-Dt / LagS));
	}

	FVector Forward = Body.GetRotation().GetForwardVector();
	Forward.Z = 0.0;
	const double HeadingRad = Forward.SizeSquared() > 1e-6 ? FMath::Atan2(Forward.Y, Forward.X) : 0.0;
	const FRotator Frame(0.0, FMath::RadiansToDegrees(HeadingRad + Swing), 0.0);

	const double Speed = FMath::Abs(BodyVelocityMps.X);
	FVector Offset;
	FVector Look;
	if (View == ESimCruiseView::FarChase)
	{
		// Twice as far and higher: the car small in its road, the road ahead seen.
		Offset = FVector(-1050.0, 90.0 * FMath::Max(0.18, 1.0 - Speed / 14.0), 330.0);
		Look = FVector(520.0, 0.0, 60.0);
	}
	else if (View == ESimCruiseView::Scenic)
	{
		Offset = FVector(-720.0, 490.0, 345.0);
		Look = FVector(65.0, 0.0, 56.0);
	}
	else
	{
		Offset = FVector(-580.0, 192.0 * FMath::Max(0.18, 1.0 - Speed / 14.0), 194.0);
		Look = FVector(390.0, 0.0, 70.0);
	}
	const FVector Pivot = Body.GetLocation();
	const FVector Goal = Pivot + Frame.RotateVector(Offset);
	OutLookAt = Pivot + Frame.RotateVector(Look);

	// The lag acts on the camera's offset from the car, not on its world
	// position: it softens turns and swings without letting the camera fall
	// further behind as speed rises (a world-space lag trails v / rate metres).
	if (bSnap || !bHasLocation)
	{
		SmoothedOffset = Goal - Pivot;
		bHasLocation = true;
	}
	else
	{
		SmoothedOffset = FMath::Lerp(SmoothedOffset, Goal - Pivot, 1.0 - FMath::Exp(-PositionRate * Dt));
	}
	LastPivot = Pivot;
	OutLocation = Pivot + SmoothedOffset;
}

FVector2D FSimCruiseGMeter::Update(const FTransform& Body, const FVector& WorldVelocityMps, double DeltaTimeS, double SimTimeS)
{
	if (DeltaTimeS <= 0.0)
	{
		return Value;   // paused: hold the reading
	}
	const double FrameS = FMath::Max(DeltaTimeS, 1e-4);
	const double Dt = FMath::Clamp(DeltaTimeS, 1.0 / 240.0, 1.0 / 20.0);
	if (!bStarted || FVector::Dist(WorldVelocityMps, PreviousVelocity) > 25.0)
	{
		PreviousVelocity = WorldVelocityMps;
		PreviousSimTimeS = SimTimeS;
		LastAcceleration = FVector::ZeroVector;
		bStarted = true;
	}
	const FVector Raw = SimCruiseCameraModels::Acceleration(WorldVelocityMps, PreviousVelocity, SimTimeS, PreviousSimTimeS, FrameS, LastAcceleration);
	const FVector Specific = Body.GetRotation().UnrotateVector(Raw + FVector(0.0, 0.0, G)).BoundToCube(30.0);
	const FVector2D Sample(Specific.X / G, Specific.Y / G);
	Value += (Sample - Value) * Approach(Dt, 0.15);
	return Value;
}
