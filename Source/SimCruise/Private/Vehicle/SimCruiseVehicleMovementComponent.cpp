// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicleMovementComponent.h"

#include "Chaos/SimCallbackObject.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/SimCruiseUnits.h"
#include "World/SimCruiseWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseVehicle, Log, All);

/** Everything the physics thread owns for one vehicle; shared with the game thread under Mutex. */
struct FSimCruiseVehiclePhysicsState
{
	mutable FCriticalSection Mutex;
	FSimCruiseVehicleSim Sim;
	FSimCruiseVehicleControls Controls;
	int32 PendingShift = 0;
	int32 PendingManual = -1;
	bool bPendingToggleManual = false;
	bool bPendingEngineStart = false;
	bool bPendingReset = false;
	FTransform PendingResetTransform = FTransform::Identity;
	FString LastGearMessage;
	FSimCruiseVehicleTelemetry Telemetry;
	TFunction<void(FSimCruiseVehicleDriverContext&)> Driver;
	Chaos::FSingleParticlePhysicsProxy* Proxy = nullptr;
	TWeakObjectPtr<UWorld> World;
	ECollisionChannel Channel = ECC_Visibility;
	FCollisionQueryParams QueryParams;
	double MaxWheelSubstepSeconds = 1.0 / 360.0;
	double SimTimeS = 0.0;
	int64 Steps = 0;
	double StepSecondsTotal = 0.0;
	bool bActive = false;
	/** The car's mass properties in Chaos units (kg, cm, kg cm^2), principal axes = body axes. */
	double MassKg = 1.0;
	FVector3d CenterOfMassCm = FVector3d::ZeroVector;
	FVector3d InertiaKgCm2 = FVector3d::OneVector;
	/** Times the physics thread put back mass properties the game thread had recomputed. */
	int32 MassPropertyRestores = 0;
	/** The last step's body velocity, for the contact guard; the number of steps it held. */
	bool bHaveLastVelocity = false;
	FVector3d LastVelocityMps = FVector3d::ZeroVector;
	FVector3d LastAngularRadPerSecond = FVector3d::ZeroVector;
	int32 VelocityClamps = 0;
	/** Water on the road, 0 dry .. 1 soaked (the environment's live rain), set from the game thread. */
	float RoadWetness = 0.f;
	/** The air: density (kg/m3) and wind at the car's height (m/s, world axes). */
	double AirDensity = 1.225;
	FVector3d WindMps = FVector3d::ZeroVector;

	FSimCruiseVehiclePhysicsState()
	{
		Telemetry.Wheels.SetNum(4);
	}
};

namespace
{
	FSimCruiseBodyState ReadBody(const Chaos::FRigidBodyHandle_Internal& Handle)
	{
		FSimCruiseBodyState Body;
		const FVector3d X = Handle.X();
		const FQuat4d R = Handle.R();
		Body.PositionM = SimCruiseUnits::ToSILength(X);
		Body.Rotation = R;
		Body.CenterOfMassM = SimCruiseUnits::ToSILength(X + R.RotateVector(FVector3d(Handle.CenterOfMass())));
		Body.LinearVelocityMps = SimCruiseUnits::ToSIVelocity(FVector3d(Handle.V()));
		Body.AngularVelocityRadPerSecond = FVector3d(Handle.W());
		return Body;
	}

	void WriteBody(Chaos::FRigidBodyHandle_Internal& Handle, const FSimCruiseBodyState& Body)
	{
		Handle.SetX(SimCruiseUnits::ToUnrealLength(Body.PositionM));
		Handle.SetR(Body.Rotation);
		Handle.SetV(SimCruiseUnits::ToUnrealVelocity(Body.LinearVelocityMps));
		Handle.SetW(Body.AngularVelocityRadPerSecond);
	}

	void FillTelemetry(FSimCruiseVehiclePhysicsState& S, const FSimCruiseBodyState& Body, double DeltaTimeS)
	{
		const FSimCruiseVehicleSimState& St = S.Sim.State;
		FSimCruiseVehicleTelemetry& T = S.Telemetry;
		T.ForwardSpeedMps = St.ForwardSpeedMps;
		T.SpeedMps = Body.LinearVelocityMps.Size();
		T.SpeedKph = T.SpeedMps * 3.6;
		T.SpeedMph = SimCruiseUnits::MpsToMph(T.SpeedMps);
		T.BodyVelocityLocalMps = Body.Rotation.UnrotateVector(Body.LinearVelocityMps);
		T.BodyAngularVelocityLocalRadPerSecond = Body.Rotation.UnrotateVector(Body.AngularVelocityRadPerSecond);
		T.EngineRpm = St.EngineRpm;
		T.Gear = St.bManual ? St.SelectedGear : (St.bInReverse ? -1 : St.Gear + 1);
		T.GearText = S.Sim.GetGearText();
		T.bManual = St.bManual;
		T.bInReverse = St.bInReverse;
		T.bStalled = St.bStalled;
		T.bShifting = St.ShiftTimeS > 0.0;
		T.bServiceBrake = St.bServiceBrake;
		T.bRoofDown = St.bRoofDown;
		T.StrandedSeconds = St.StrandedSeconds;
		T.SteeringInput = St.SteeringInput;
		T.SteeringAngleDeg = SimCruiseUnits::ToDegrees(St.SteeringRad);
		T.ClutchPedal = St.ClutchPedal;
		T.Throttle = St.PedalThrottle * (1.0 - St.TcCut);
		T.BrakeAmount = St.BrakeAmount;
		T.TractionControlCut = St.TcCut;
		T.WheelContactCount = St.WheelContactCount;
		T.SoundThrottle = St.SoundThrottle;
		T.SoundCombustion = St.SoundCombustion;
		T.SoundShaftLoad = St.SoundShaftLoad;
		T.ClutchTorqueNm = St.ClutchTorqueNm;
		T.RackAligningTorqueNm = St.RackAligningTorqueNm;
		T.SimTimeS = S.SimTimeS;
		T.PhysicsSteps = S.Steps;
		T.FixedStepSeconds = DeltaTimeS;
		T.MeanStepMicroseconds = S.Steps > 0 ? S.StepSecondsTotal / S.Steps * 1.0e6 : 0.0;
		T.LastGearMessage = S.LastGearMessage;
	T.MassPropertyRestores = S.MassPropertyRestores;
		T.bAbsActive = false;
		T.Wheels.SetNum(4);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const FSimCruiseWheelState& W = St.Wheels[Index];
			FSimCruiseWheelTelemetry& Out = T.Wheels[Index];
			Out.bContact = W.bContact;
			Out.LoadN = W.LoadN;
			Out.SlipRatio = W.SlipRatio;
			Out.PeakSlipRatio = W.PeakSlip;
			Out.SlipAngleDeg = SimCruiseUnits::ToDegrees(W.SlipAngleRad);
			Out.GripUse = W.GripUse;
			const double Limit = FMath::Max(W.LoadN * W.FrictionCoefficient, 1.0);
			const double LateralScale = FMath::Max(S.Sim.GetCar().LateralGripScale, UE_DOUBLE_SMALL_NUMBER);
			Out.CombinedGripUse = W.bContact
				? FMath::Sqrt(FMath::Square(W.LongitudinalForceN) + FMath::Square(W.LateralForceN / LateralScale)) / Limit
				: 0.0;
			Out.AngularSpeedRadPerSecond = W.AngularSpeedRadPerSecond;
			Out.SpinAngleDeg = SimCruiseUnits::ToDegrees(W.SpinAngleRad);
			Out.SuspensionLengthCm = SimCruiseUnits::ToUnrealLength(W.SuspensionLengthM);
			Out.SteerAngleDeg = SimCruiseUnits::ToDegrees(W.SteerAngleRad);
			Out.InclinationDeg = SimCruiseUnits::ToDegrees(W.InclinationRad);
			Out.LongitudinalForceN = W.LongitudinalForceN;
			Out.LateralForceN = W.LateralForceN;
			Out.AligningTorqueNm = W.AligningTorqueNm;
			Out.SurfaceGrip = W.SurfaceGrip;
			Out.AbsRelease = W.AbsRelease;
			Out.ContactPointCm = SimCruiseUnits::ToUnrealLength(W.ContactPointM);
			Out.ContactNormal = W.ContactNormal;
			T.bAbsActive |= W.AbsRelease < 0.999;
		}
	}
}

/** Physics-thread step for one vehicle, at the solver's fixed asynchronous interval. */
static void StepVehicleOnPhysicsThread(FSimCruiseVehiclePhysicsState& S, double DeltaTimeS, double SimTimeS)
{
	FScopeLock Lock(&S.Mutex);
	if (!S.bActive || S.Proxy == nullptr || DeltaTimeS <= 0.0)
	{
		return;
	}
	Chaos::FRigidBodyHandle_Internal* Handle = S.Proxy->GetPhysicsThreadAPI();
	if (Handle == nullptr)
	{
		return;
	}
	if (Handle->ObjectState() == Chaos::EObjectStateType::Sleeping)
	{
		Handle->SetObjectState(Chaos::EObjectStateType::Dynamic);
	}
	if (Handle->ObjectState() != Chaos::EObjectStateType::Dynamic)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();

	// The car file owns the body's mass properties. Anything that makes the
	// game thread recompute them (welding an attached collider, rescaling the
	// body, a mass override or physical material change) would otherwise
	// silently swap in a uniform box's mass, centre and inertia.
	{
		const Chaos::TVec3<Chaos::FRealSingle> CurrentI = Handle->I();
		const FVector3d Inertia(CurrentI.X, CurrentI.Y, CurrentI.Z);
		if (!FMath::IsNearlyEqual(static_cast<double>(Handle->M()), S.MassKg, S.MassKg * 1.0e-6) ||
			!FVector3d(Handle->CenterOfMass()).Equals(S.CenterOfMassCm, 1.0e-3) ||
			!Inertia.Equals(S.InertiaKgCm2, S.InertiaKgCm2.GetMax() * 1.0e-5) ||
			!FQuat4d(Handle->RotationOfMass()).Equals(FQuat4d::Identity, 1.0e-6))
		{
			if (S.MassPropertyRestores++ == 0)
			{
				UE_LOG(LogSimCruiseVehicle, Warning,
					TEXT("%s: the vehicle body's mass properties were changed outside the vehicle (mass %.1f kg, expected %.1f); restoring the car's."),
					*S.Sim.GetCar().Id, static_cast<double>(Handle->M()), S.MassKg);
			}
			Handle->SetM(S.MassKg);
			Handle->SetInvM(1.0 / S.MassKg);
			Handle->SetI(Chaos::TVec3<Chaos::FRealSingle>(S.InertiaKgCm2.X, S.InertiaKgCm2.Y, S.InertiaKgCm2.Z));
			Handle->SetInvI(Chaos::TVec3<Chaos::FRealSingle>(1.0 / S.InertiaKgCm2.X, 1.0 / S.InertiaKgCm2.Y, 1.0 / S.InertiaKgCm2.Z));
			Handle->SetCenterOfMass(S.CenterOfMassCm);
			Handle->SetRotationOfMass(FQuat4d::Identity);
		}
	}

	if (S.bPendingReset)
	{
		Handle->SetX(S.PendingResetTransform.GetLocation());
		Handle->SetR(S.PendingResetTransform.GetRotation());
		Handle->SetV(FVector3d::ZeroVector);
		Handle->SetW(FVector3d::ZeroVector);
		S.Sim.ResetState();
		S.bPendingReset = false;
		S.bHaveLastVelocity = false;
	}

	FSimCruiseBodyState Body = ReadBody(*Handle);
	// A contact the solver resolves badly (the body box pressed into another car's) can hand the
	// car a speed no collision gives: 21 mph to 249 mph in a frame. Speed may fall as fast as it
	// likes (a crash), but in one step it may rise by no more than 0.5 m/s and 6 g, and the spin
	// by 1 rad/s plus 30 rad/s^2, to at most 12 rad/s; otherwise the last step's is kept.
	if (S.bHaveLastVelocity)
	{
		const double Dt = FMath::Max(DeltaTimeS, 1e-4);
		const double MaxSpeed = S.LastVelocityMps.Size() + 0.5 + 6.0 * 9.80665 * Dt;
		const double Speed = Body.LinearVelocityMps.Size();
		const double MaxSpin = FMath::Min(S.LastAngularRadPerSecond.Size() + 1.0 + 30.0 * Dt, 12.0);
		const double Spin = Body.AngularVelocityRadPerSecond.Size();
		bool bClamped = false;
		if (Speed > MaxSpeed)
		{
			Body.LinearVelocityMps *= MaxSpeed / Speed;
			bClamped = true;
		}
		if (Spin > MaxSpin)
		{
			Body.AngularVelocityRadPerSecond *= MaxSpin / Spin;
			bClamped = true;
		}
		if (bClamped)
		{
			WriteBody(*Handle, Body);
			if (S.VelocityClamps++ == 0)
			{
				UE_LOG(LogSimCruiseVehicle, Warning, TEXT("%s: a contact gave the car %.1f m/s and %.1f rad/s in one step; held to %.1f m/s, %.1f rad/s"),
					*S.Sim.GetCar().Id, Speed, Spin, Body.LinearVelocityMps.Size(), Body.AngularVelocityRadPerSecond.Size());
			}
		}
	}
	S.bHaveLastVelocity = true;
	S.LastVelocityMps = Body.LinearVelocityMps;
	S.LastAngularRadPerSecond = Body.AngularVelocityRadPerSecond;
	const double ForwardSpeed = Body.LinearVelocityMps.Dot(Body.Rotation.RotateVector(FVector3d::UnitX()));
	if (S.PendingManual >= 0)
	{
		S.Sim.SetManual(S.PendingManual == 1, Body.LinearVelocityMps.Size());
		S.PendingManual = -1;
	}
	if (S.bPendingToggleManual)
	{
		S.Sim.SetManual(!S.Sim.State.bManual, Body.LinearVelocityMps.Size());
		S.bPendingToggleManual = false;
	}
	while (S.PendingShift != 0)
	{
		const int32 Direction = S.PendingShift > 0 ? 1 : -1;
		S.LastGearMessage = S.Sim.ChangeGear(Direction, ForwardSpeed);
		S.PendingShift -= Direction;
	}
	if (S.bPendingEngineStart)
	{
		S.Sim.StartEngine();
		S.bPendingEngineStart = false;
	}

	FSimCruiseVehicleControls Controls = S.Controls;
	if (S.Driver)
	{
		FSimCruiseVehicleDriverContext Context{ S.Sim, Body, Controls, DeltaTimeS, SimTimeS, S.Steps };
		S.Driver(Context);
		if (Context.bBodyStateChanged)
		{
			WriteBody(*Handle, Body);
			// A bench or test that sets the speed is not a contact.
			S.LastVelocityMps = Body.LinearVelocityMps;
			S.LastAngularRadPerSecond = Body.AngularVelocityRadPerSecond;
			Body.CenterOfMassM = Body.PositionM + Body.Rotation.RotateVector(SimCruiseUnits::ToSILength(FVector3d(Handle->CenterOfMass())));
		}
	}

	UWorld* World = S.World.Get();
	const ECollisionChannel Channel = S.Channel;
	const FCollisionQueryParams& QueryParams = S.QueryParams;
	const float Wetness = FMath::Clamp(S.RoadWetness, 0.f, 1.f);
	S.Sim.AirDensityKgPerM3 = S.AirDensity;
	S.Sim.WindMps = S.WindMps;
	auto Ground = [World, Channel, &QueryParams, Wetness](const FVector3d& StartM, const FVector3d& EndM, FSimCruiseGroundHit& OutHit)
	{
		if (World == nullptr)
		{
			return false;
		}
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, SimCruiseUnits::ToUnrealLength(StartM), SimCruiseUnits::ToUnrealLength(EndM),
			Channel, QueryParams) || Hit.bStartPenetrating)
		{
			return false;
		}
		OutHit.PointM = SimCruiseUnits::ToSILength(FVector3d(Hit.ImpactPoint));
		OutHit.NormalM = FVector3d(Hit.ImpactNormal).GetSafeNormal();
		// The surface under the tyre: asphalt, gravel or the ground beside the road, and the rain on it.
		float DryGrip = 1.f, WetLoss = 0.3f;
		if (const ASimCruiseWorldTile* Tile = Cast<ASimCruiseWorldTile>(Hit.GetActor()))
		{
			Tile->SurfaceGripAtFace(Hit.FaceIndex, DryGrip, WetLoss);
		}
		OutHit.SurfaceGrip = DryGrip * (1.f - WetLoss * Wetness);
		return true;
	};

	const int32 Substeps = FMath::Max(1, FMath::CeilToInt32(DeltaTimeS / FMath::Max(S.MaxWheelSubstepSeconds, 1.0e-5) - 1.0e-6));
	FSimCruiseBodyForces Forces;
	S.Sim.Step(DeltaTimeS, Substeps, Controls, Body, Ground, Forces);
	Handle->AddForce(SimCruiseUnits::ToUnrealForce(Forces.ForceN));
	Handle->AddTorque(SimCruiseUnits::ToUnrealTorque(Forces.TorqueNm));

	++S.Steps;
	S.SimTimeS = SimTimeS + DeltaTimeS;
	S.StepSecondsTotal += FPlatformTime::Seconds() - StartSeconds;
	FillTelemetry(S, Body, DeltaTimeS);
}

/** Registered with the world's Chaos solver; runs before every fixed physics step. */
class FSimCruiseVehicleSimCallback : public Chaos::TSimCallbackObject<>
{
public:
	explicit FSimCruiseVehicleSimCallback(TSharedPtr<FSimCruiseVehiclePhysicsState, ESPMode::ThreadSafe> InState)
		: State(MoveTemp(InState))
	{
	}

	virtual FName GetFNameForStatId() const override
	{
		const static FLazyName StaticName("FSimCruiseVehicleSimCallback");
		return StaticName;
	}

	virtual void OnPreSimulate_Internal() override
	{
		if (State.IsValid())
		{
			StepVehicleOnPhysicsThread(*State, GetDeltaTime_Internal(), GetSimTime_Internal());
		}
	}

private:
	TSharedPtr<FSimCruiseVehiclePhysicsState, ESPMode::ThreadSafe> State;
};

USimCruiseVehicleMovementComponent::USimCruiseVehicleMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PhysicsState = MakeShared<FSimCruiseVehiclePhysicsState, ESPMode::ThreadSafe>();
}

void USimCruiseVehicleMovementComponent::SetCarDefinition(const FSimCruiseCarDefinition& Car)
{
	{
		FScopeLock Lock(&PhysicsState->Mutex);
		PhysicsState->Sim.Initialize(Car);
		PhysicsState->Sim.bSteeringAssist = bSteeringAssist;
		if (bStartManual)
		{
			PhysicsState->Sim.SetManual(true, 0.0);
		}
		PhysicsState->Sim.ResetState();
	}
	if (HasBegunPlay())
	{
		ConfigureBody();
	}
}

const FSimCruiseCarDefinition& USimCruiseVehicleMovementComponent::GetCarDefinition() const
{
	return PhysicsState->Sim.GetCar();
}

bool USimCruiseVehicleMovementComponent::LoadCarById(const FString& CarId)
{
	FSimCruiseCarDefinition Car;
	FString Error;
	if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Car, Error))
	{
		UE_LOG(LogSimCruiseVehicle, Error, TEXT("Could not load car %s: %s"), *CarId, *Error);
		return false;
	}
	SetCarDefinition(Car);
	return true;
}

UPrimitiveComponent* USimCruiseVehicleMovementComponent::GetBodyComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Cast<UPrimitiveComponent>(Owner->GetRootComponent()) : nullptr;
}

void USimCruiseVehicleMovementComponent::ConfigureBody()
{
	UPrimitiveComponent* Body = GetBodyComponent();
	if (Body == nullptr)
	{
		UE_LOG(LogSimCruiseVehicle, Error, TEXT("%s: the vehicle's root component must be a simulating primitive."), *GetPathName());
		return;
	}
	FBodyInstance* BodyInstance = Body->GetBodyInstance();
	if (BodyInstance == nullptr || !BodyInstance->IsValidBodyInstance())
	{
		UE_LOG(LogSimCruiseVehicle, Error, TEXT("%s: the vehicle body has no physics state."), *GetPathName());
		return;
	}

	FSimCruiseVehiclePhysicsState& S = *PhysicsState;
	FScopeLock Lock(&S.Mutex);
	const FSimCruiseCarDefinition& Car = S.Sim.GetCar();

	// Chaos's defaults are tuned for stability, not fidelity: no inertia
	// conditioning (it inflates inertia), Godot's damping, never sleep.
	BodyInstance->SetInertiaConditioningEnabled(false);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(static_cast<float>(Car.AngularDampingPerSecond));
	Body->SetEnableGravity(true);
	BodyInstance->SetMassOverride(static_cast<float>(Car.MassKg), true);

	Chaos::FSingleParticlePhysicsProxy* Proxy = BodyInstance->GetPhysicsActorHandle();
	if (Proxy == nullptr)
	{
		UE_LOG(LogSimCruiseVehicle, Error, TEXT("%s: the vehicle body has no Chaos particle."), *GetPathName());
		return;
	}
	Chaos::FRigidBodyHandle_External& External = Proxy->GetGameThreadAPI();
	const FVector3d InertiaKgCm2 = S.Sim.GetPrincipalInertiaKgM2() * (SimCruiseUnits::CentimetresPerMetre * SimCruiseUnits::CentimetresPerMetre);
	S.MassKg = Car.MassKg;
	S.CenterOfMassCm = SimCruiseUnits::ToUnrealLength(S.Sim.GetCenterOfMassLocalM());
	S.InertiaKgCm2 = InertiaKgCm2;
	External.SetM(Car.MassKg);
	External.SetInvM(1.0 / Car.MassKg);
	External.SetI(Chaos::TVec3<Chaos::FRealSingle>(InertiaKgCm2.X, InertiaKgCm2.Y, InertiaKgCm2.Z));
	External.SetInvI(Chaos::TVec3<Chaos::FRealSingle>(1.0 / InertiaKgCm2.X, 1.0 / InertiaKgCm2.Y, 1.0 / InertiaKgCm2.Z));
	External.SetCenterOfMass(S.CenterOfMassCm);
	External.SetRotationOfMass(FQuat::Identity);
	External.SetInertiaConditioningEnabled(false);
	External.SetSleepType(Chaos::ESleepType::NeverSleep);

	S.Proxy = Proxy;
	S.World = GetWorld();
	S.Channel = GroundTraceChannel;
	S.QueryParams = FCollisionQueryParams(SCENE_QUERY_STAT(SimCruiseSuspension), false, GetOwner());
	S.QueryParams.bReturnFaceIndex = true;
	S.MaxWheelSubstepSeconds = MaxWheelSubstepSeconds;
	S.Sim.bSteeringAssist = bSteeringAssist;
	S.bActive = true;
}

void USimCruiseVehicleMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	ConfigureBody();
	if (UPrimitiveComponent* Body = GetBodyComponent())
	{
		Body->OnComponentPhysicsStateChanged.AddUniqueDynamic(this, &USimCruiseVehicleMovementComponent::HandleBodyPhysicsStateChanged);
		BoundBody = Body;
	}

	UWorld* World = GetWorld();
	FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (Solver == nullptr)
	{
		UE_LOG(LogSimCruiseVehicle, Error, TEXT("%s: no Chaos solver; the vehicle will not drive."), *GetPathName());
		return;
	}
	SimCallback = Solver->CreateAndRegisterSimCallbackObject_External<FSimCruiseVehicleSimCallback>(PhysicsState);
	if (!Solver->IsUsingAsyncResults())
	{
		UE_LOG(LogSimCruiseVehicle, Warning,
			TEXT("%s: physics is not ticking asynchronously at a fixed step; enable bTickPhysicsAsync for a deterministic vehicle."),
			*GetPathName());
	}
}

void USimCruiseVehicleMovementComponent::HandleBodyPhysicsStateChanged(UPrimitiveComponent* ChangedComponent,
	EComponentPhysicsStateChange StateChange)
{
	if (StateChange == EComponentPhysicsStateChange::Destroyed)
	{
		// The old particle is queued for deletion on the physics thread: stop
		// stepping it before the next push reaches that thread.
		FScopeLock Lock(&PhysicsState->Mutex);
		PhysicsState->bActive = false;
		PhysicsState->Proxy = nullptr;
	}
	else if (HasBegunPlay() && ChangedComponent == GetBodyComponent())
	{
		ConfigureBody();
	}
}

void USimCruiseVehicleMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UPrimitiveComponent* Body = BoundBody.Get())
	{
		Body->OnComponentPhysicsStateChanged.RemoveDynamic(this, &USimCruiseVehicleMovementComponent::HandleBodyPhysicsStateChanged);
	}
	BoundBody.Reset();
	{
		FScopeLock Lock(&PhysicsState->Mutex);
		PhysicsState->bActive = false;
		PhysicsState->Proxy = nullptr;
		PhysicsState->Driver = nullptr;
	}
	if (SimCallback != nullptr)
	{
		UWorld* World = GetWorld();
		FPhysScene* Scene = World ? World->GetPhysicsScene() : nullptr;
		if (Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr)
		{
			Solver->UnregisterAndFreeSimCallbackObject_External(SimCallback);
		}
		SimCallback = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void USimCruiseVehicleMovementComponent::SetThrottle(float Value)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Controls.Throttle = FMath::Clamp(Value, 0.0f, 1.0f);
}

void USimCruiseVehicleMovementComponent::SetBrake(float Value)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Controls.Brake = FMath::Clamp(Value, 0.0f, 1.0f);
}

void USimCruiseVehicleMovementComponent::SetSteer(float Value, bool bFromThumbstick)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Controls.Steer = FMath::Clamp(Value, -1.0f, 1.0f);
	PhysicsState->Controls.bThumbstickSteer = bFromThumbstick;
}

void USimCruiseVehicleMovementComponent::SetDriverControls(const FSimCruiseVehicleControls& Controls)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Controls = Controls;
}

void USimCruiseVehicleMovementComponent::SetHandbrake(bool bEngaged)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Controls.bHandbrake = bEngaged;
}

void USimCruiseVehicleMovementComponent::SetFullBrake(bool bEngaged)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Controls.bFullBrake = bEngaged;
}

void USimCruiseVehicleMovementComponent::SetClutch(float Value)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Controls.Clutch = FMath::Clamp(Value, 0.0f, 1.0f);
}

void USimCruiseVehicleMovementComponent::ShiftUp()
{
	FScopeLock Lock(&PhysicsState->Mutex);
	++PhysicsState->PendingShift;
}

void USimCruiseVehicleMovementComponent::ShiftDown()
{
	FScopeLock Lock(&PhysicsState->Mutex);
	--PhysicsState->PendingShift;
}

void USimCruiseVehicleMovementComponent::ToggleManual()
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->bPendingToggleManual = !PhysicsState->bPendingToggleManual;
}

void USimCruiseVehicleMovementComponent::SetManualTransmission(bool bManual)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->PendingManual = bManual ? 1 : 0;
	PhysicsState->bPendingToggleManual = false;
}

void USimCruiseVehicleMovementComponent::RequestEngineStart()
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->bPendingEngineStart = true;
}

void USimCruiseVehicleMovementComponent::SetAir(double DensityKgPerM3, const FVector& WindMps)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->AirDensity = FMath::Clamp(DensityKgPerM3, 0.5, 1.6);
	PhysicsState->WindMps = FVector3d(WindMps);
}

void USimCruiseVehicleMovementComponent::SetRoadWetness(float Wetness)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->RoadWetness = FMath::Clamp(Wetness, 0.f, 1.f);
}

void USimCruiseVehicleMovementComponent::SetSteeringAssist(bool bAssist)
{
	bSteeringAssist = bAssist;
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Sim.bSteeringAssist = bAssist;
}

void USimCruiseVehicleMovementComponent::ResetVehicle(const FTransform& BodyTransform)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->PendingResetTransform = BodyTransform;
	PhysicsState->bPendingReset = true;
}

FSimCruiseVehicleTelemetry USimCruiseVehicleMovementComponent::GetTelemetry() const
{
	FSimCruiseVehicleTelemetry Copy;
	CopyTelemetry(Copy);
	return Copy;
}

void USimCruiseVehicleMovementComponent::CopyTelemetry(FSimCruiseVehicleTelemetry& OutTelemetry) const
{
	FScopeLock Lock(&PhysicsState->Mutex);
	OutTelemetry = PhysicsState->Telemetry;
}

float USimCruiseVehicleMovementComponent::GetForwardSpeedMps() const
{
	FScopeLock Lock(&PhysicsState->Mutex);
	return static_cast<float>(PhysicsState->Telemetry.ForwardSpeedMps);
}

float USimCruiseVehicleMovementComponent::GetEngineRpm() const
{
	FScopeLock Lock(&PhysicsState->Mutex);
	return static_cast<float>(PhysicsState->Telemetry.EngineRpm);
}

FString USimCruiseVehicleMovementComponent::GetGearText() const
{
	FScopeLock Lock(&PhysicsState->Mutex);
	return PhysicsState->Telemetry.GearText;
}

float USimCruiseVehicleMovementComponent::GetSteeringAngleDeg() const
{
	FScopeLock Lock(&PhysicsState->Mutex);
	return static_cast<float>(PhysicsState->Telemetry.SteeringAngleDeg);
}

void USimCruiseVehicleMovementComponent::SetPhysicsThreadDriver(TFunction<void(FSimCruiseVehicleDriverContext&)> Driver)
{
	FScopeLock Lock(&PhysicsState->Mutex);
	PhysicsState->Driver = MoveTemp(Driver);
}

void USimCruiseVehicleMovementComponent::ReadPhysicsState(TFunctionRef<void(const FSimCruiseVehicleSim&)> Reader) const
{
	FScopeLock Lock(&PhysicsState->Mutex);
	Reader(PhysicsState->Sim);
}
