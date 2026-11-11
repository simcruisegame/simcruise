// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Chaos/SimCallbackObject.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "PhysicsProxy/SingleParticlePhysicsProxy.h"
#include "Tests/VehicleBenchWorld.h"
#include "Tests/VehicleBenches.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"

#include <atomic>

namespace
{
	constexpr EAutomationTestFlags VehicleTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext |
		EAutomationTestFlags::EngineFilter;

	/** Applies a fixed SI force and torque through the same unit boundary the vehicle uses, and records the body each step. */
	class FForceProbeCallback : public Chaos::TSimCallbackObject<>
	{
	public:
		Chaos::FSingleParticlePhysicsProxy* Proxy = nullptr;
		FVector3d ForceN = FVector3d::ZeroVector;
		FVector3d TorqueNm = FVector3d::ZeroVector;
		int32 StepsToApply = 0;
		int32 AppliedSteps = 0;
		TArray<FVector3d> VelocityAtStepMps;
		TArray<FVector3d> AngularVelocityAtStep;
		TArray<double> DeltaTimes;

		virtual FName GetFNameForStatId() const override
		{
			const static FLazyName StaticName("FSimCruiseForceProbeCallback");
			return StaticName;
		}

		virtual void OnPreSimulate_Internal() override
		{
			Chaos::FRigidBodyHandle_Internal* Handle = Proxy ? Proxy->GetPhysicsThreadAPI() : nullptr;
			if (Handle == nullptr || AppliedSteps > StepsToApply)
			{
				return;
			}
			VelocityAtStepMps.Add(SimCruiseUnits::ToSIVelocity(FVector3d(Handle->V())));
			AngularVelocityAtStep.Add(FVector3d(Handle->W()));
			DeltaTimes.Add(GetDeltaTime_Internal());
			if (AppliedSteps < StepsToApply)
			{
				Handle->AddForce(SimCruiseUnits::ToUnrealForce(ForceN));
				Handle->AddTorque(SimCruiseUnits::ToUnrealTorque(TorqueNm));
			}
			++AppliedSteps;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleChaosForceIntegrationTest, "SimCruise.Vehicle.Chaos.ForceIntegration", VehicleTestFlags)
bool FSimCruiseVehicleChaosForceIntegrationTest::RunTest(const FString& Parameters)
{
	const double FixedStep = SimCruiseShippingFixedStepS;
	FSimCruiseVehicleBenchWorld Bench(FixedStep, 10.0);
	if (!TestTrue(TEXT("bench world"), Bench.IsValid()))
	{
		return false;
	}
	UWorld* World = Bench.GetWorld();

	// A free body far above the pad, gravity off, with known SI mass properties.
	const double MassKg = 1200.0;
	const FVector3d InertiaKgM2(450.0, 1500.0, 1700.0);
	AActor* Probe = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(0.0, 0.0, 10000.0)));
	UBoxComponent* Box = NewObject<UBoxComponent>(Probe, TEXT("ProbeBody"));
	Box->SetBoxExtent(FVector(200.0, 80.0, 30.0), false);
	Box->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Probe->SetRootComponent(Box);
	Box->SetWorldLocation(FVector(0.0, 0.0, 10000.0));
	Box->BodyInstance.SetInertiaConditioningEnabled(false);
	Box->SetSimulatePhysics(true);
	Box->SetEnableGravity(false);
	Box->SetLinearDamping(0.0f);
	Box->SetAngularDamping(0.0f);
	Box->RegisterComponent();
	Box->GetBodyInstance()->SetMassOverride(static_cast<float>(MassKg), true);
	Chaos::FSingleParticlePhysicsProxy* Proxy = Box->GetBodyInstance()->GetPhysicsActorHandle();
	if (!TestNotNull(TEXT("probe particle"), Proxy))
	{
		return false;
	}
	Chaos::FRigidBodyHandle_External& External = Proxy->GetGameThreadAPI();
	const FVector3d InertiaUnreal = InertiaKgM2 * 1.0e4;
	External.SetM(MassKg);
	External.SetInvM(1.0 / MassKg);
	External.SetI(Chaos::TVec3<Chaos::FRealSingle>(InertiaUnreal.X, InertiaUnreal.Y, InertiaUnreal.Z));
	External.SetInvI(Chaos::TVec3<Chaos::FRealSingle>(1.0 / InertiaUnreal.X, 1.0 / InertiaUnreal.Y, 1.0 / InertiaUnreal.Z));
	External.SetCenterOfMass(FVector3d::ZeroVector);
	External.SetSleepType(Chaos::ESleepType::NeverSleep);

	Chaos::FPhysicsSolver* Solver = World->GetPhysicsScene()->GetSolver();
	FForceProbeCallback* Callback = Solver->CreateAndRegisterSimCallbackObject_External<FForceProbeCallback>();
	Callback->Proxy = Proxy;
	Callback->ForceN = FVector3d(600.0, -240.0, 120.0);
	// Torque about the yaw axis only, so no gyroscopic coupling enters the check.
	Callback->TorqueNm = FVector3d(0.0, 0.0, 340.0);
	Callback->StepsToApply = 360;

	Bench.RunUntil([Callback]() { return Callback->AppliedSteps > Callback->StepsToApply; }, 5.0);
	TestEqual(TEXT("one callback per fixed step"), Callback->AppliedSteps, Callback->StepsToApply + 1);
	bool bFixed = true;
	for (const double Dt : Callback->DeltaTimes)
	{
		bFixed &= FMath::IsNearlyEqual(Dt, FixedStep, 1.0e-9);
	}
	TestTrue(TEXT("every step has the fixed interval"), bFixed);

	// After k steps of constant force: v = F k dt / m and w = tau k dt / I, exactly (symplectic Euler).
	double WorstLinear = 0.0;
	double WorstAngular = 0.0;
	for (int32 K = 0; K < Callback->VelocityAtStepMps.Num(); ++K)
	{
		const FVector3d Expected = Callback->ForceN * (K * FixedStep / MassKg);
		WorstLinear = FMath::Max(WorstLinear, (Callback->VelocityAtStepMps[K] - Expected).Size());
		const FVector3d ExpectedW(0.0, 0.0, Callback->TorqueNm.Z * K * FixedStep / InertiaKgM2.Z);
		WorstAngular = FMath::Max(WorstAngular, (Callback->AngularVelocityAtStep[K] - ExpectedW).Size());
	}
	for (const int32 K : { 0, 1, 2, 10, 100, 360 })
	{
		if (Callback->VelocityAtStepMps.IsValidIndex(K))
		{
			UE_LOG(LogTemp, Display, TEXT("FORCEPROBE k=%d v=%s expected=%s w=%s"), K, *Callback->VelocityAtStepMps[K].ToString(),
				*(Callback->ForceN * (K * FixedStep / MassKg)).ToString(), *Callback->AngularVelocityAtStep[K].ToString());
		}
	}
	const FString ProbeLine = FString::Printf(TEXT("FORCEPROBE %d steps at %.1f Hz: worst linear error %.3g m/s (of %.4f per s), worst angular %.3g rad/s (of %.4f per s)"),
		Callback->AppliedSteps, 1.0 / FixedStep, WorstLinear, (Callback->ForceN / MassKg).Size(), WorstAngular,
		Callback->TorqueNm.Z / InertiaKgM2.Z);
	UE_LOG(LogTemp, Display, TEXT("%s"), *ProbeLine);
	AddInfo(ProbeLine);
	TestTrue(TEXT("force integrates as F/m per step (N to kg cm/s^2 boundary), to float precision"), WorstLinear < 1.0e-5);
	TestTrue(TEXT("torque integrates as tau/I per step (N m and kg m^2 boundary)"), WorstAngular < 1.0e-5);

	Solver->UnregisterAndFreeSimCallbackObject_External(Callback);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleChaosBodyTest, "SimCruise.Vehicle.Chaos.BodyProperties", VehicleTestFlags)
bool FSimCruiseVehicleChaosBodyTest::RunTest(const FString& Parameters)
{
	FSimCruiseCarDefinition Car;
	if (!SimCruiseLoadBenchCar(*this, TEXT("nb1999"), Car))
	{
		return false;
	}
	FSimCruiseVehicleBenchWorld Bench(SimCruiseShippingFixedStepS, 50.0);
	ASimCruiseVehicle* Vehicle = Bench.SpawnVehicle(Car, FVector3d(0.0, 0.0, 0.05), nullptr);
	if (!TestNotNull(TEXT("vehicle"), Vehicle))
	{
		return false;
	}
	auto CheckBody = [this, Vehicle](const FSimCruiseCarDefinition& Expected)
	{
		Chaos::FSingleParticlePhysicsProxy* Proxy = Vehicle->GetBody()->GetBodyInstance()->GetPhysicsActorHandle();
		if (!TestNotNull(TEXT("vehicle particle"), Proxy))
		{
			return;
		}
		const Chaos::FRigidBodyHandle_External& Body = Proxy->GetGameThreadAPI();
		TestEqual(FString::Printf(TEXT("%s mass (kg)"), *Expected.Id), static_cast<double>(Body.M()), Expected.MassKg, 1.0e-6);
		const FVector3d ExpectedInertia = SimCruiseUnits::GodotInertiaToUnrealAxes(Expected.InertiaKgM2) * 1.0e4;
		const FVector3d Inertia(Body.I().X, Body.I().Y, Body.I().Z);
		TestEqual(FString::Printf(TEXT("%s inertia (kg cm^2, roll pitch yaw)"), *Expected.Id), Inertia, ExpectedInertia, 1.0f);
		const FVector3d ExpectedCentre = SimCruiseUnits::ToUnrealLength(FVector3d((Expected.FrontWeightFraction - 0.5) * Expected.WheelbaseM, 0.0,
			Expected.CenterOfGravityHeightM - Expected.CollisionShapeCenterYM));
		TestEqual(FString::Printf(TEXT("%s centre of mass (cm, body frame)"), *Expected.Id), FVector3d(Body.CenterOfMass()), ExpectedCentre, 1.0e-3f);
		TestFalse(TEXT("inertia conditioning off"), Body.InertiaConditioningEnabled());
		const FVector BoxExtent = Vehicle->GetBody()->GetUnscaledBoxExtent();
		TestEqual(FString::Printf(TEXT("%s collision box from the car file (cm half extents)"), *Expected.Id), FVector3d(BoxExtent),
			SimCruiseUnits::ToUnrealLength(FVector3d(Expected.CollisionBoxSizeM.Z, Expected.CollisionBoxSizeM.X, Expected.CollisionBoxSizeM.Y) * 0.5), 1.0e-3f);
	};
	Bench.RunUntil([]() { return false; }, 0.1);
	CheckBody(Car);

	// Swapping cars at runtime resizes the body and installs the new mass properties.
	FSimCruiseCarDefinition Mustang;
	if (SimCruiseLoadBenchCar(*this, TEXT("mustang65"), Mustang))
	{
		Vehicle->SetCarDefinition(Mustang);
		Vehicle->ResetAt(FVector(0.0, 0.0, 5.0), 0.0f);
		Bench.RunUntil([]() { return false; }, 0.5);
		CheckBody(Mustang);
		TestEqual(TEXT("the swapped car stands on four wheels"), Vehicle->GetVehicleTelemetry().WheelContactCount, 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseVehicleChaosConfigTest, "SimCruise.Vehicle.Chaos.FixedStepConfig", VehicleTestFlags)
bool FSimCruiseVehicleChaosConfigTest::RunTest(const FString& Parameters)
{
	const UPhysicsSettings* Settings = UPhysicsSettings::Get();
	TestTrue(TEXT("physics ticks asynchronously (bTickPhysicsAsync)"), Settings->bTickPhysicsAsync);
	TestEqual(TEXT("fixed physics step is the vehicle's chosen rate"), static_cast<double>(Settings->AsyncFixedTimeStepSize),
		SimCruiseShippingFixedStepS, 1.0e-7);
	return true;
}

#endif
