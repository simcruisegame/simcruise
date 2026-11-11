// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarPresentationComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Presentation/SimCruiseCarAssets.h"
#include "Presentation/SimCruiseCarMaterialLibrary.h"

namespace SimCruisePresentationComponentPrivate
{
	/** The wanted name when it is free under Outer, else a numbered variant. */
	FName FreeName(UObject* Outer, const UClass* Class, FName Wanted)
	{
		return StaticFindObjectFast(nullptr, Outer, Wanted) ? MakeUniqueObjectName(Outer, Class, Wanted) : Wanted;
	}
}
using SimCruisePresentationComponentPrivate::FreeName;

USimCruiseCarPresentationComponent::USimCruiseCarPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Pose after physics so the wheels match this frame's simulation.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	for (FVector& Hub : StaticHubs)
	{
		Hub = FVector::ZeroVector;
	}
}

double USimCruiseCarPresentationComponent::SteeringWheelAngleFor(double FrontLeftSteerRad, double FrontRightSteerRad, double Ratio)
{
	return (FrontLeftSteerRad + FrontRightSteerRad) * 0.5 * Ratio;
}

FTransform USimCruiseCarPresentationComponent::KnuckleTransform(const FVector& StaticHubCm, const FSimCruiseWheelVisualState& Wheel, bool bLeftSide)
{
	// Unreal yaw is positive to the right; the state's steer is positive to the left.
	// Positive roll tips the top toward +Y, outboard for a right wheel.
	const FRotator Rotation(0.0, -FMath::RadiansToDegrees(Wheel.SteerAngleRad), FMath::RadiansToDegrees(Wheel.CamberRad) * (bLeftSide ? -1.0 : 1.0));
	return FTransform(Rotation, StaticHubCm + FVector(0.0, 0.0, Wheel.SuspensionTravelCm));
}

FQuat USimCruiseCarPresentationComponent::SteeringWheelTurn(double AngleRad)
{
	return FQuat(FVector::ForwardVector, AngleRad);
}

FRotator USimCruiseCarPresentationComponent::SpinRotation(float SpinAngleRad)
{
	// Rolling forward moves the top of the wheel forward: a negative pitch about the axle (Y).
	// Wrapped to one turn so an accumulated angle keeps its precision in degrees.
	const double Wrapped = FMath::Fmod(static_cast<double>(SpinAngleRad), UE_DOUBLE_TWO_PI);
	return FRotator(-FMath::RadiansToDegrees(Wrapped), 0.0, 0.0);
}

void USimCruiseCarPresentationComponent::ClearParts()
{
	for (USceneComponent* Part : Parts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
	Knuckles.Reset();
	Spinners.Reset();
	SteeringWheelMesh = nullptr;
	Materials.Reset();
	RearLamps.Reset();
	FrontLamps.Reset();
	LampLevel = -1;
}

UMaterialInstanceDynamic* USimCruiseCarPresentationComponent::MaterialFor(int32 MaterialIndex)
{
	if (TObjectPtr<UMaterialInstanceDynamic>* Found = Materials.Find(MaterialIndex))
	{
		return *Found;
	}
	const ESimCruiseCarMaterialRole Role = Built->RoleOf(MaterialIndex);
	UMaterialInstanceDynamic* MID = SimCruiseCarMaterials::MakeInstance(this, Role, Built->Model, MaterialIndex, Built->Profile);
	Materials.Add(MaterialIndex, MID);
	if (Role == ESimCruiseCarMaterialRole::LampRear)
	{
		RearLamps.Add(MID);
	}
	else if (Role == ESimCruiseCarMaterialRole::LampFront)
	{
		FrontLamps.Add(MID);
	}
	return MID;
}

UStaticMeshComponent* USimCruiseCarPresentationComponent::MakeMeshComponent(USceneComponent* Parent, FName Name, UStaticMesh* Mesh,
	const FTransform& Relative, const TArray<int32>& SectionMaterials)
{
	if (!Mesh)
	{
		return nullptr;
	}
	// Unique names: an actor may carry several cars, and a re-initialised car's
	// old parts keep their names until garbage collection.
	UObject* Outer = GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this);
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Outer, FreeName(Outer, UStaticMeshComponent::StaticClass(), Name));
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetupAttachment(Parent);
	Component->SetRelativeTransform(Relative);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	Component->SetStaticMesh(Mesh);
	bool bAllGlass = SectionMaterials.Num() > 0;
	for (int32 Slot = 0; Slot < SectionMaterials.Num(); ++Slot)
	{
		Component->SetMaterial(Slot, MaterialFor(SectionMaterials[Slot]));
		bAllGlass &= SimCruiseCarMaterials::MasterFor(Built->RoleOf(SectionMaterials[Slot]), nullptr) == ESimCruiseCarMaster::Glass;
	}
	// Glass and lenses let the sun into the cabin.
	Component->SetCastShadow(!bAllGlass);
	Component->RegisterComponent();
	Parts.Add(Component);
	return Component;
}

bool USimCruiseCarPresentationComponent::InitializeForCar(const FString& InCarId)
{
	ClearParts();
	CarId = InCarId;
	LastError.Reset();
	Built = SimCruiseCarAssets::Get(InCarId, LastError);
	if (!Built.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("SimCruise car presentation: %s"), *LastError);
		return false;
	}
	RigLog = Built->Log;
	SteeringRatio = Built->SteeringRatio;
	DriverEyeCm = Built->DriverEyeCm;
	for (int32 W = 0; W < 4; ++W)
	{
		StaticHubs[W] = Built->HubsCm[W];
	}

	auto AddPart = [this](USceneComponent* Parent, const FSimCruiseBuiltPart& Part, const FTransform& Relative) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Solid = MakeMeshComponent(Parent, Part.Name, Part.Mesh.Get(), Relative, Part.MeshMaterials);
		MakeMeshComponent(Parent, FName(Part.Name.ToString() + TEXT("_glass")), Part.Glass.Get(), Relative, Part.GlassMaterials);
		return Solid;
	};
	AddPart(this, Built->Body, FTransform::Identity);
	for (int32 W = 0; W < 4; ++W)
	{
		UObject* Outer = GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this);
		const FName KnuckleName(*FString::Printf(TEXT("%s_knuckle"), *Built->Wheels[W].Name.ToString()));
		USceneComponent* Knuckle = NewObject<USceneComponent>(Outer, FreeName(Outer, USceneComponent::StaticClass(), KnuckleName));
		Knuckle->SetMobility(EComponentMobility::Movable);
		Knuckle->SetupAttachment(this);
		Knuckle->SetRelativeTransform(FTransform(StaticHubs[W]));
		Knuckle->RegisterComponent();
		Parts.Add(Knuckle);
		Knuckles.Add(Knuckle);
		Spinners.Add(AddPart(Knuckle, Built->Wheels[W], FTransform::Identity));
		AddPart(Knuckle, Built->Uprights[W], FTransform::Identity);
	}
	if (Built->bHasSteeringWheel)
	{
		SteeringRest = Built->SteeringWheel.PartToVehicle;
		SteeringWheelMesh = AddPart(this, Built->SteeringWheel, SteeringRest);
	}
	ApplyVisualState(FSimCruiseCarVisualState());
	return true;
}

void USimCruiseCarPresentationComponent::SetWheelStateProvider(TScriptInterface<ISimCruiseWheelStateProvider> Provider)
{
	StateProvider = Provider;
}

FVector USimCruiseCarPresentationComponent::GetStaticHubLocation(int32 Wheel) const
{
	return Wheel >= 0 && Wheel < 4 ? StaticHubs[Wheel] : FVector::ZeroVector;
}

void USimCruiseCarPresentationComponent::ApplyVisualState(const FSimCruiseCarVisualState& State)
{
	if (!Built.IsValid())
	{
		return;
	}
	for (int32 W = 0; W < Knuckles.Num() && W < 4; ++W)
	{
		const FSimCruiseWheelVisualState& Wheel = State.Wheels[W];
		Knuckles[W]->SetRelativeTransform(KnuckleTransform(StaticHubs[W], Wheel, W == 0 || W == 2));
		if (Spinners.IsValidIndex(W) && Spinners[W])
		{
			Spinners[W]->SetRelativeRotation(SpinRotation(Wheel.SpinAngleRad));
		}
	}
	SteeringWheelAngle = SteeringWheelAngleFor(State.Wheels[0].SteerAngleRad, State.Wheels[1].SteerAngleRad, SteeringRatio);
	if (SteeringWheelMesh)
	{
		// Local X runs along the column away from the driver, Y to his right,
		// Z to the rim's top: a positive angle about X carries the top toward
		// -Y, anticlockwise as the driver sees it (a left turn).
		SteeringWheelMesh->SetRelativeRotation(SteeringRest.GetRotation() * SteeringWheelTurn(SteeringWheelAngle));
	}
	const int32 Level = State.bBrake ? 2 : (State.bLamps ? 1 : 0);
	if (Level != LampLevel)
	{
		LampLevel = Level;
		for (UMaterialInstanceDynamic* Lamp : RearLamps)
		{
			SimCruiseCarMaterials::SetLampLevel(Lamp, ESimCruiseCarMaterialRole::LampRear, static_cast<float>(Level));
		}
		for (UMaterialInstanceDynamic* Lamp : FrontLamps)
		{
			SimCruiseCarMaterials::SetLampLevel(Lamp, ESimCruiseCarMaterialRole::LampFront, State.bLamps ? 1.f : 0.f);
		}
	}
}

void USimCruiseCarPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (StateProvider.GetInterface())
	{
		FSimCruiseCarVisualState State;
		if (StateProvider->GetCarVisualState(State))
		{
			ApplyVisualState(State);
		}
	}
}

ASimCruiseCarPresentationActor::ASimCruiseCarPresentationActor()
{
	Presentation = CreateDefaultSubobject<USimCruiseCarPresentationComponent>(TEXT("Presentation"));
	RootComponent = Presentation;
	Presentation->SetMobility(EComponentMobility::Movable);
}
