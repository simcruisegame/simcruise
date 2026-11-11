// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicle.h"

#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

ASimCruiseVehicle::ASimCruiseVehicle()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(UCollisionProfile::Vehicle_ProfileName);
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(true);
	Body->BodyInstance.SetInertiaConditioningEnabled(false);
	Body->SetLinearDamping(0.0f);
	Body->SetHiddenInGame(true);
	Body->SetCanEverAffectNavigation(false);
	RootComponent = Body;

	VehicleMovement = CreateDefaultSubobject<USimCruiseVehicleMovementComponent>(TEXT("VehicleMovement"));
	ApplyBodyShape();
}

void ASimCruiseVehicle::SetCarDefinition(const FSimCruiseCarDefinition& Car)
{
	// Shape first: a shape change can make Chaos recompute mass properties,
	// which the movement component then sets from the car.
	ApplyBodyShape(Car);
	VehicleMovement->SetCarDefinition(Car);
	CarId = Car.Id;
	bHasDefinition = true;
}

void ASimCruiseVehicle::ApplyBodyShape()
{
	ApplyBodyShape(VehicleMovement->GetCarDefinition());
}

void ASimCruiseVehicle::ApplyBodyShape(const FSimCruiseCarDefinition& Car)
{
	// Godot's collision_box is (width, height, length).
	const FVector3d SizeM = FVector3d(Car.CollisionBoxSizeM.Z, Car.CollisionBoxSizeM.X, Car.CollisionBoxSizeM.Y);
	Body->SetBoxExtent(SimCruiseUnits::ToUnrealLength(SizeM * 0.5), false);
}

void ASimCruiseVehicle::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyBodyShape();
}

void ASimCruiseVehicle::BeginPlay()
{
	if (!bHasDefinition && !CarId.IsEmpty())
	{
		// Before Super::BeginPlay, so the component configures the body after the shape is set.
		bHasDefinition = VehicleMovement->LoadCarById(CarId);
		ApplyBodyShape();
	}
	Super::BeginPlay();
}

FSimCruiseVehicleTelemetry ASimCruiseVehicle::GetVehicleTelemetry() const
{
	return VehicleMovement->GetTelemetry();
}

void ASimCruiseVehicle::SetThrottle(float Value) { VehicleMovement->SetThrottle(Value); }
void ASimCruiseVehicle::SetBrake(float Value) { VehicleMovement->SetBrake(Value); }
void ASimCruiseVehicle::SetSteer(float Value, bool bFromThumbstick) { VehicleMovement->SetSteer(Value, bFromThumbstick); }
void ASimCruiseVehicle::SetHandbrake(bool bEngaged) { VehicleMovement->SetHandbrake(bEngaged); }
void ASimCruiseVehicle::SetClutch(float Value) { VehicleMovement->SetClutch(Value); }
void ASimCruiseVehicle::ShiftUp() { VehicleMovement->ShiftUp(); }
void ASimCruiseVehicle::ShiftDown() { VehicleMovement->ShiftDown(); }
void ASimCruiseVehicle::ToggleManual() { VehicleMovement->ToggleManual(); }

FTransform ASimCruiseVehicle::BodyTransformForGroundPose(const FVector& GroundLocation, float YawDeg) const
{
	const FQuat Rotation(FRotator(0.0, YawDeg, 0.0));
	const double BoxCentreCm = SimCruiseUnits::ToUnrealLength(VehicleMovement->GetCarDefinition().CollisionShapeCenterYM);
	return FTransform(Rotation, GroundLocation + Rotation.RotateVector(FVector(0.0, 0.0, BoxCentreCm)));
}

void ASimCruiseVehicle::ResetAt(const FVector& GroundLocation, float YawDeg)
{
	VehicleMovement->ResetVehicle(BodyTransformForGroundPose(GroundLocation, YawDeg));
}
