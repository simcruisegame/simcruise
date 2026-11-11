// Copyright SimCruise contributors. All Rights Reserved.

#include "Tests/VehicleBenchWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PBDRigidsSolver.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Vehicle/CarDefinitionLoader.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"

FSimCruiseVehicleBenchWorld::FSimCruiseVehicleBenchWorld(double InFixedStepS, double GroundHalfSizeM, bool bSingleThreaded)
	: FixedStepS(InFixedStepS)
{
	if (!Wrapper.CreateTestWorld(EWorldType::Game) || !Wrapper.BeginPlayInTestWorld())
	{
		return;
	}
	World = Wrapper.GetTestWorld();

	FPhysScene* Scene = World->GetPhysicsScene();
	Chaos::FPhysicsSolver* Solver = Scene ? Scene->GetSolver() : nullptr;
	if (Solver == nullptr)
	{
		World = nullptr;
		return;
	}
	// Fixed asynchronous step, solved on the calling thread so a bench is
	// reproducible step for step.
	if (bSingleThreaded)
	{
		Solver->SetThreadingMode_External(Chaos::EThreadingModeTemp::SingleThread);
	}
	Solver->EnableAsyncMode(FixedStepS);

	// The pad: a static box whose top face is Z = 0.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Ground = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	UBoxComponent* Box = NewObject<UBoxComponent>(Ground, TEXT("BenchPad"));
	const double HalfSizeCm = SimCruiseUnits::ToUnrealLength(GroundHalfSizeM);
	Box->SetBoxExtent(FVector(HalfSizeCm, HalfSizeCm, 50.0), false);
	Box->SetMobility(EComponentMobility::Static);
	Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Ground->SetRootComponent(Box);
	Box->SetWorldLocation(FVector(0.0, 0.0, -50.0));
	Box->RegisterComponent();
	Wrapper.TickTestWorld(static_cast<float>(FixedStepS));
}

FSimCruiseVehicleBenchWorld::~FSimCruiseVehicleBenchWorld()
{
	if (Wrapper.GetTestWorld() != nullptr)
	{
		Wrapper.DestroyTestWorld(true);
	}
}

FVector3d FSimCruiseVehicleBenchWorld::BodyLocationForGround(const FSimCruiseCarDefinition& Car, const FVector3d& GroundLocationM)
{
	return GroundLocationM + FVector3d(0.0, 0.0, Car.CollisionShapeCenterYM);
}

ASimCruiseVehicle* FSimCruiseVehicleBenchWorld::SpawnVehicle(const FSimCruiseCarDefinition& Car, const FVector3d& GroundLocationM,
	TFunction<void(FSimCruiseVehicleDriverContext&)> Driver, bool bSteeringAssist, double MaxWheelSubstepS)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	const FTransform Transform(FQuat::Identity, SimCruiseUnits::ToUnrealLength(BodyLocationForGround(Car, GroundLocationM)));
	ASimCruiseVehicle* Vehicle = World->SpawnActorDeferred<ASimCruiseVehicle>(ASimCruiseVehicle::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Vehicle == nullptr)
	{
		return nullptr;
	}
	Vehicle->SetCarDefinition(Car);
	Vehicle->GetVehicleMovement()->SetSteeringAssist(bSteeringAssist);
	Vehicle->GetVehicleMovement()->MaxWheelSubstepSeconds = MaxWheelSubstepS;
	Vehicle->GetVehicleMovement()->SetPhysicsThreadDriver(MoveTemp(Driver));
	Vehicle->FinishSpawning(Transform);
	return Vehicle;
}

bool FSimCruiseVehicleBenchWorld::RunUntil(TFunctionRef<bool()> Done, double MaxSimSeconds)
{
	if (World == nullptr)
	{
		return false;
	}
	// Six fixed steps per game frame at 360 Hz: the frame rate does not
	// matter to the vehicle, whose bench driver runs on the physics step.
	const int32 StepsPerFrame = FMath::Max(1, FMath::RoundToInt32((1.0 / 60.0) / FixedStepS));
	const float FrameS = static_cast<float>(StepsPerFrame * FixedStepS);
	double Elapsed = 0.0;
	while (!Done())
	{
		if (Elapsed > MaxSimSeconds)
		{
			return false;
		}
		Wrapper.TickTestWorld(FrameS);
		Elapsed += FrameS;
	}
	return true;
}

bool SimCruiseLoadBenchCar(FAutomationTestBase& Test, const FString& CarId, FSimCruiseCarDefinition& OutCar)
{
	FString Error;
	if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, OutCar, Error))
	{
		Test.AddError(FString::Printf(TEXT("Could not load %s: %s"), *CarId, *Error));
		return false;
	}
	return true;
}

#endif
