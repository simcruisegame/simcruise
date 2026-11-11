// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "Presentation/SimCruiseCarClock.h"
#include "Presentation/SimCruiseCarMirror.h"
#include "HAL/IConsoleManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

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
	// The dashboard clock: the display's own texture, with the real time drawn over the painted one.
	const TOptional<FSimCruiseClockSpec>& Clock = Built->Profile.Clock;
	if (MID && Clock.IsSet() && Built->Model.Materials.IsValidIndex(MaterialIndex) && Built->Model.Materials[MaterialIndex].Name == Clock->Material)
	{
		if (UMaterialInterface* ClockBase = SimCruiseCarClock::Get())
		{
			const FSimCruiseGltfMaterial& Author = Built->Model.Materials[MaterialIndex];
			UTexture* Texture = Built->Model.ImageFiles.IsValidIndex(Author.BaseColorImage)
				? SimCruiseCarMaterials::LoadTexture(Built->Model.ImageFiles[Author.BaseColorImage], true, false) : nullptr;
			UMaterialInstanceDynamic* ClockMID = UMaterialInstanceDynamic::Create(ClockBase, this);
			if (Texture) ClockMID->SetTextureParameterValue(TEXT("BaseTexture"), Texture);
			ClockMID->SetVectorParameterValue(TEXT("BoxMin"), FLinearColor(Clock->Box.X, Clock->Box.Y, 0.f, 0.f));
			ClockMID->SetVectorParameterValue(TEXT("BoxMax"), FLinearColor(Clock->Box.Z, Clock->Box.W, 0.f, 0.f));
			ClockMID->SetVectorParameterValue(TEXT("Clear"), FLinearColor(Clock->Clear.X, Clock->Clear.Y, 0.f, 0.f));
			ClockMID->SetScalarParameterValue(TEXT("FlipV"), Clock->bFlipV ? 1.f : 0.f);
			MID = ClockMID;
			ClockMaterial = ClockMID;
			ClockMinutes = -1;
		}
	}
	// Mirror glass reflects the rearward capture.
	if (Role == ESimCruiseCarMaterialRole::MirrorGlass)
	{
		if (UMaterialInterface* MirrorBase = SimCruiseCarMirror::Get())
		{
			MID = UMaterialInstanceDynamic::Create(MirrorBase, this);
			if (MirrorTarget)
			{
				MID->SetTextureParameterValue(TEXT("Capture"), MirrorTarget);
				MID->SetScalarParameterValue(TEXT("HasCapture"), 1.f);
			}
			MirrorMaterials.Add(MID);
		}
	}
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

TArray<FVector> USimCruiseCarPresentationComponent::GetHeadLampLocations() const
{
	return Built.IsValid() ? Built->HeadLampsCm : TArray<FVector>();
}

bool USimCruiseCarPresentationComponent::SetPaintColor(const FLinearColor& Linear)
{
	if (!Built.IsValid())
	{
		return false;
	}
	bool bAny = false;
	for (const TPair<int32, TObjectPtr<UMaterialInstanceDynamic>>& Pair : Materials)
	{
		if (!Pair.Value || Built->RoleOf(Pair.Key) != ESimCruiseCarMaterialRole::Paint) continue;
		const FSimCruiseGltfMaterial* Author = Built->Model.Materials.IsValidIndex(Pair.Key) ? &Built->Model.Materials[Pair.Key] : nullptr;
		if (Author && Built->Model.ImageFiles.IsValidIndex(Author->BaseColorImage)) continue; // an atlas carries its own colour
		Pair.Value->SetVectorParameterValue(TEXT("BaseColor"), Linear);
		bAny = true;
	}
	return bAny;
}

void USimCruiseCarPresentationComponent::SetParked(float GlassNearCm, float GlassFarCm, float DrawDistanceCm)
{
	SetComponentTickEnabled(false);
	if (!Built.IsValid())
	{
		return;
	}
	using R = ESimCruiseCarMaterialRole;
	auto FarMaterial = [this](int32 MaterialIndex) -> UMaterialInstanceDynamic*
	{
		const R Role = Built->RoleOf(MaterialIndex);
		FLinearColor Base(0.012f, 0.014f, 0.016f), Surface(0.f, 0.06f, 1.f, 0.02f);
		if (Role == R::LampRear) Base = FLinearColor(0.22f, 0.006f, 0.005f);
		else if (Role == R::LampAmber) Base = FLinearColor(0.45f, 0.16f, 0.01f);
		else if (Role == R::LampFront) Base = FLinearColor(0.35f, 0.36f, 0.37f);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SimCruiseCarMaterials::GetMaster(ESimCruiseCarMaster::Opaque), this);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Base);
		MID->SetVectorParameterValue(TEXT("Surface"), Surface);
		MID->SetVectorParameterValue(TEXT("Detail"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		MID->SetVectorParameterValue(TEXT("TextureFlags"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		MID->SetVectorParameterValue(TEXT("Extra"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		MID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		return MID;
	};
	const FSimCruiseBuiltPart* AllParts[] = { &Built->Body, &Built->Wheels[0], &Built->Wheels[1], &Built->Wheels[2], &Built->Wheels[3],
		&Built->Uprights[0], &Built->Uprights[1], &Built->Uprights[2], &Built->Uprights[3], &Built->SteeringWheel };
	TArray<UStaticMeshComponent*> Existing;
	for (USceneComponent* Component : Parts)
	{
		if (UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component)) Existing.Add(Mesh);
	}
	for (UStaticMeshComponent* Part : Existing)
	{
		Part->SetCullDistance(DrawDistanceCm);
		Part->bAffectDynamicIndirectLighting = false;
		Part->bAffectDistanceFieldLighting = false;
		const FSimCruiseBuiltPart* Glass = nullptr;
		for (const FSimCruiseBuiltPart* P : AllParts)
		{
			if (P->Glass.IsValid() && P->Glass.Get() == Part->GetStaticMesh()) Glass = P;
		}
		if (Glass)
		{
			Part->SetCullDistance(GlassNearCm);
			UObject* Outer = GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this);
			UStaticMeshComponent* Far = NewObject<UStaticMeshComponent>(Outer, FreeName(Outer, UStaticMeshComponent::StaticClass(), FName(Part->GetName() + TEXT("_far"))));
			Far->SetMobility(EComponentMobility::Movable);
			Far->SetupAttachment(Part->GetAttachParent());
			Far->SetRelativeTransform(Part->GetRelativeTransform());
			Far->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Far->SetCanEverAffectNavigation(false);
			Far->SetStaticMesh(Part->GetStaticMesh());
			for (int32 Slot = 0; Slot < Glass->GlassMaterials.Num(); ++Slot)
			{
				Far->SetMaterial(Slot, FarMaterial(Glass->GlassMaterials[Slot]));
			}
			Far->SetCastShadow(false);
			Far->MinDrawDistance = GlassNearCm;
			Far->SetCullDistance(GlassFarCm);
			Far->bAffectDynamicIndirectLighting = false;
			Far->bAffectDistanceFieldLighting = false;
			Far->RegisterComponent();
			Parts.Add(Far);
		}
		Part->MarkRenderStateDirty();
	}
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
	ClockCheck -= DeltaTime;
	if (ClockMaterial && ClockCheck <= 0.f)
	{
		ClockCheck = 1.f;
		if (const USimCruiseEnvironmentSubsystem* Subsystem = USimCruiseEnvironmentSubsystem::Get(GetWorld()))
		{
			if (const ASimCruiseEnvironment* Environment = Subsystem->GetEnvironment())
			{
				const int32 Minutes = SimCruiseCarClock::LocalMinutes(Environment->GetUtcTime(), Environment->LatitudeDeg, Environment->LongitudeDeg);
				if (Minutes != ClockMinutes)
				{
					ClockMinutes = Minutes;
					ClockMaterial->SetScalarParameterValue(TEXT("Minutes"), static_cast<float>(Minutes));
				}
			}
		}
	}
	if (MirrorCapture && !MirrorMaterials.IsEmpty())
	{
		// Every third frame (about 25 Hz at the High preset): what a mirror shows is small and
		// changes slowly next to the road ahead.
		static IConsoleVariable* Interval = IConsoleManager::Get().FindConsoleVariable(TEXT("simcruise.Mirrors.Interval"));
		const uint32 Every = static_cast<uint32>(FMath::Max(Interval ? Interval->GetInt() : 3, 1));
		if (++MirrorFrame % Every == 0u)
		{
			MirrorCapture->CaptureSceneDeferred();
		}
		const FTransform Eye = MirrorCapture->GetComponentTransform();
		const FVector Fwd = Eye.GetUnitAxis(EAxis::X), Right = Eye.GetUnitAxis(EAxis::Y), Up = Eye.GetUnitAxis(EAxis::Z);
		// Each glass as the driver set it: the line from the eye to the mirror's centre
		// reflects straight back (rear-view) or a little outward (door mirrors, the far one
		// more), slightly down. The passenger-side glass is convex (US: about 1.3 m radius).
		static const FVector Aims[3] = {
			FVector(-1.0, -0.21, -0.05).GetSafeNormal(), FVector(-1.0, 0.0, -0.01).GetSafeNormal(), FVector(-1.0, 0.32, -0.06).GetSafeNormal()};
		static const float Curvature[3] = {0.f, 0.f, 1.f / 130.f};
		const FTransform& Car = GetComponentTransform();
		// The eye is where the driver's camera (and the capture) sits.
		const FVector EyeCm = Car.InverseTransformPosition(Eye.GetLocation());
		for (UMaterialInstanceDynamic* Mirror : MirrorMaterials)
		{
			Mirror->SetVectorParameterValue(TEXT("Fwd"), FLinearColor(Fwd.X, Fwd.Y, Fwd.Z, 0.f));
			Mirror->SetVectorParameterValue(TEXT("Right"), FLinearColor(Right.X, Right.Y, Right.Z, 0.f));
			Mirror->SetVectorParameterValue(TEXT("Up"), FLinearColor(Up.X, Up.Y, Up.Z, 0.f));
			for (int32 I = 0; I < 3 && Built.IsValid() && Built->MirrorsCm.IsValidIndex(I); ++I)
			{
				const FVector4& M = Built->MirrorsCm[I];
				if (M.W <= 0.0) continue;
				const FVector Centre(M);
				const FVector Normal = ((EyeCm - Centre).GetSafeNormal() + Aims[I]).GetSafeNormal();
				const FVector C = Car.TransformPosition(Centre), N = Car.TransformVectorNoScale(Normal);
				Mirror->SetVectorParameterValue(*FString::Printf(TEXT("C%d"), I), FLinearColor(C.X, C.Y, C.Z, 0.f));
				Mirror->SetVectorParameterValue(*FString::Printf(TEXT("N%d"), I), FLinearColor(N.X, N.Y, N.Z, 0.f));
				Mirror->SetScalarParameterValue(*FString::Printf(TEXT("K%d"), I), Curvature[I]);
			}
		}
	}
	if (StateProvider.GetInterface())
	{
		FSimCruiseCarVisualState State;
		if (StateProvider->GetCarVisualState(State))
		{
			ApplyVisualState(State);
		}
	}
}

void USimCruiseCarPresentationComponent::SetMirrorsLive(bool bLive, USceneComponent* Parent, const FVector& RelativeEyeCm)
{
	if (!bLive)
	{
		if (MirrorCapture) { MirrorCapture->DestroyComponent(); MirrorCapture = nullptr; }
		for (UMaterialInstanceDynamic* Mirror : MirrorMaterials) { Mirror->SetScalarParameterValue(TEXT("HasCapture"), 0.f); }
		return;
	}
	if (MirrorCapture || !Parent)
	{
		return;
	}
	// Wide enough for the three glasses (the door mirrors look outward, the passenger's is
	// convex); 768 x 320 puts about 7.7 texels in each degree.
	constexpr float FovDeg = 100.f;
	constexpr int32 SizeX = 768, SizeY = 320;
	if (!MirrorTarget)
	{
		MirrorTarget = NewObject<UTextureRenderTarget2D>(this);
		MirrorTarget->RenderTargetFormat = RTF_RGBA16f;
		MirrorTarget->ClearColor = FLinearColor::Black;
		MirrorTarget->InitAutoFormat(SizeX, SizeY);
		MirrorTarget->UpdateResourceImmediate(true);
	}
	MirrorCapture = NewObject<USceneCaptureComponent2D>(GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this), TEXT("MirrorCapture"));
	MirrorCapture->SetupAttachment(Parent);
	MirrorCapture->SetRelativeLocationAndRotation(RelativeEyeCm, FRotator(0.0, 180.0, 0.0));
	MirrorCapture->FOVAngle = FovDeg;
	// The camera sits in the driver's head: the seat and headrest would fill the view, so
	// the car itself is left out (top down, a roadster's mirror sees the road over the deck).
	if (AActor* Car = GetOwner())
	{
		MirrorCapture->HiddenActors.Add(Car);
	}
	MirrorCapture->TextureTarget = MirrorTarget;
	MirrorCapture->CaptureSource = SCS_SceneColorHDR;
	MirrorCapture->bCaptureEveryFrame = false;
	MirrorCapture->bCaptureOnMovement = false;
	MirrorCapture->bAlwaysPersistRenderingState = true;
	// Cheap: no Lumen in the mirror view (direct light, sky light and fog are what it shows).
	FPostProcessSettings& PP = MirrorCapture->PostProcessSettings;
	PP.bOverride_DynamicGlobalIlluminationMethod = true;
	PP.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::None;
	PP.bOverride_ReflectionMethod = true;
	PP.ReflectionMethod = EReflectionMethod::None;
	MirrorCapture->PostProcessBlendWeight = 1.f;
	// Measured at High: 4.7 ms a capture with everything on. Off: the global distance field
	// (0.75 ms: with Lumen off only distance-field AO and far shadows use it; virtual shadow
	// maps cover the mirror's view), distance-field AO and shadows,
	// the volumetric clouds with their shadow map (0.9 ms; the mirror's sky is the
	// atmosphere alone); motion blur, bloom, volumetric fog and anti-aliasing. (Set as
	// show-flag settings: registering the capture rebuilds its flags from them.)
	TArray<FEngineShowFlagsSetting> Flags;
	for (const TCHAR* Name : {TEXT("MotionBlur"), TEXT("Bloom"), TEXT("VolumetricFog"), TEXT("AntiAliasing"), TEXT("DistanceFieldAO"),
		TEXT("RayTracedDistanceFieldShadows"), TEXT("Cloud"), TEXT("AmbientOcclusion"), TEXT("ContactShadows")})
	{
		FEngineShowFlagsSetting Flag;
		Flag.ShowFlagName = Name;
		Flag.Enabled = false;
		Flags.Add(Flag);
	}
	MirrorCapture->SetShowFlagSettings(Flags);
	MirrorCapture->RegisterComponent();
	const float TanHalf = FMath::Tan(FMath::DegreesToRadians(FovDeg * 0.5f));
	for (UMaterialInstanceDynamic* Mirror : MirrorMaterials)
	{
		Mirror->SetTextureParameterValue(TEXT("Capture"), MirrorTarget);
		Mirror->SetScalarParameterValue(TEXT("HasCapture"), 1.f);
		Mirror->SetScalarParameterValue(TEXT("TanHalf"), TanHalf);
		Mirror->SetScalarParameterValue(TEXT("Aspect"), static_cast<float>(SizeX) / SizeY);
	}
}

ASimCruiseCarPresentationActor::ASimCruiseCarPresentationActor()
{
	Presentation = CreateDefaultSubobject<USimCruiseCarPresentationComponent>(TEXT("Presentation"));
	RootComponent = Presentation;
	Presentation->SetMobility(EComponentMobility::Movable);
}
