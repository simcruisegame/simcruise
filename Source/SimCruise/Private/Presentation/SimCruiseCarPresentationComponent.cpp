// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "Presentation/SimCruiseCarClock.h"
#include "Presentation/SimCruiseCarMirror.h"
#include "HAL/IConsoleManager.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/PointLightComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Kismet/KismetRenderingLibrary.h"
#include <limits>

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

double USimCruiseCarPresentationComponent::GetDrawnSpinRad(int32 Wheel) const
{
	return Spinners.IsValidIndex(Wheel) && Spinners[Wheel] ? -FMath::DegreesToRadians(Spinners[Wheel]->GetRelativeRotation().Pitch) : 0.0;
}

double USimCruiseCarPresentationComponent::GetDrawnSteerRad(int32 Wheel) const
{
	return Knuckles.IsValidIndex(Wheel) && Knuckles[Wheel] ? -FMath::DegreesToRadians(Knuckles[Wheel]->GetRelativeRotation().Yaw) : 0.0;
}

void USimCruiseCarPresentationComponent::AddAddons()
{
	static UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (const FSimCruiseCarPresentationProfile::FAddon& Addon : Built->Profile.Addons)
	{
		UStaticMesh* Mesh = Addon.bBox ? Cube : Cylinder;
		if (!Mesh) continue;
		FTransform Relative;
		if (Addon.bBox)
		{
			// The engine cube is 1 m on a side about its centre.
			Relative = FTransform(FRotator(-Addon.PitchDeg, 0.0, 0.0), Addon.CentreM * 100.0, Addon.SizeM);
		}
		else
		{
			// The engine cylinder is 1 m across and 1 m long along Z, about its centre.
			const FVector Axis = Addon.ToM - Addon.FromM;
			const double Length = Axis.Size();
			if (Length < 1e-3) continue;
			const FQuat Turn = FQuat::FindBetweenNormals(FVector::UpVector, Axis / Length);
			Relative = FTransform(Turn, (Addon.FromM + Addon.ToM) * 50.0, FVector(Addon.RadiusM * 2.0, Addon.RadiusM * 2.0, Length));
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(SimCruiseCarMaterials::GetMaster(ESimCruiseCarMaster::Opaque), this);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Addon.Colour);
		MID->SetVectorParameterValue(TEXT("Surface"), FLinearColor(static_cast<float>(Addon.Metallic), static_cast<float>(Addon.Roughness), 0.f, 0.05f));
		MID->SetVectorParameterValue(TEXT("Detail"), FLinearColor(0.f, 0.f, 0.f, 1.f));
		MID->SetVectorParameterValue(TEXT("TextureFlags"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		MID->SetVectorParameterValue(TEXT("Extra"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		MID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0.f, 0.f, 0.f, 0.f));
		UObject* Outer = GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this);
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(Outer, FreeName(Outer, UStaticMeshComponent::StaticClass(), FName(TEXT("addon"))));
		Part->SetMobility(EComponentMobility::Movable);
		Part->SetupAttachment(this);
		Part->SetRelativeTransform(Relative);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetStaticMesh(Mesh);
		for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
		{
			Part->SetMaterial(Slot, MID);
		}
		Part->RegisterComponent();
		Parts.Add(Part);
	}
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
	ScreenMaterial = nullptr;
	ScreenRpm = 0.0;
	DialTargets.Reset();
	DialShown.Reset();
	Needles.Reset();
	NeedleRest.Reset();
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
			ClockMID->SetScalarParameterValue(TEXT("RadioTenths"), static_cast<float>(FMath::RoundToInt(Clock->RadioMHz * 10.0)));
			MID = ClockMID;
			ClockMaterial = ClockMID;
			ClockMinutes = -1;
		}
	}
	// The digital cluster: its own picture, with the live speed and tachometer drawn over the painted ones.
	const TOptional<FSimCruiseScreenSpec>& Screen = Built->Profile.Screen;
	if (MID && Screen.IsSet() && Built->Model.Materials.IsValidIndex(MaterialIndex) && Built->Model.Materials[MaterialIndex].Name == Screen->Material)
	{
		if (UMaterialInterface* ScreenBase = SimCruiseCarScreen::Get())
		{
			const FSimCruiseGltfMaterial& Author = Built->Model.Materials[MaterialIndex];
			UTexture* Texture = Built->Model.ImageFiles.IsValidIndex(Author.BaseColorImage)
				? SimCruiseCarMaterials::LoadTexture(Built->Model.ImageFiles[Author.BaseColorImage], true, false) : nullptr;
			UMaterialInstanceDynamic* ScreenMID = UMaterialInstanceDynamic::Create(ScreenBase, this);
			if (Texture) ScreenMID->SetTextureParameterValue(TEXT("BaseTexture"), Texture);
			const FSimCruiseScreenSpec& S = *Screen;
			ScreenMID->SetVectorParameterValue(TEXT("TexSize"), FLinearColor(S.TextureSize.X, S.TextureSize.Y, 0.f, 0.f));
			ScreenMID->SetScalarParameterValue(TEXT("FlipV"), S.bFlipV ? 1.f : 0.f);
			// A plain background point: inside the speed box's own corner.
			const double ClearX = (S.SpeedBox.X + 2.0) / S.TextureSize.X, ClearY = (S.SpeedBox.Y + 2.0) / S.TextureSize.Y;
			ScreenMID->SetVectorParameterValue(TEXT("Clear"), FLinearColor(ClearX, S.bFlipV ? 1.0 - ClearY : ClearY, 0.f, 0.f));
			ScreenMID->SetVectorParameterValue(TEXT("Centre"), FLinearColor(S.TachCentre.X, S.TachCentre.Y, 0.f, 0.f));
			ScreenMID->SetVectorParameterValue(TEXT("Band"), FLinearColor(S.TachBand.X, S.TachBand.Y, 0.f, 0.f));
			ScreenMID->SetVectorParameterValue(TEXT("Arc"), FLinearColor(S.TachStartDeg, S.TachDegPerThousand, S.TachPaintedEndDeg, 0.f));
			ScreenMID->SetVectorParameterValue(TEXT("SpeedMin"), FLinearColor(S.SpeedBox.X, S.SpeedBox.Y, 0.f, 0.f));
			ScreenMID->SetVectorParameterValue(TEXT("SpeedMax"), FLinearColor(S.SpeedBox.Z, S.SpeedBox.W, 0.f, 0.f));
			ScreenMID->SetScalarParameterValue(TEXT("Backlight"), static_cast<float>(S.Backlight));
			MID = ScreenMID;
			ScreenMaterial = ScreenMID;
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
	AddAddons();
	if (Built->bHasSteeringWheel)
	{
		SteeringRest = Built->SteeringWheel.PartToVehicle;
		SteeringWheelMesh = AddPart(this, Built->SteeringWheel, SteeringRest);
		if (SteeringWheelMesh)
		{
			// The turning wheel's own shadow drew black jagged tears on its spokes (where the spokes meet
			// the horn pad) in every turned frame, also with its shadow pages redrawn each frame; with the
			// wheel casting no shadow they are gone. Under the cowl the dash's own shadow covers it anyway.
			// -SimCruiseWheelShadow keeps it on to look at the fault.
			SteeringWheelMesh->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Always;
			SteeringWheelMesh->SetCastShadow(FParse::Param(FCommandLine::Get(), TEXT("SimCruiseWheelShadow")));
		}
	}
	// The instruments: a live face over each of the model's dials (their needles are part of the
	// dash mesh and cannot turn), drawn into a render target the face's material shows and lights.
	for (int32 D = 0; D < Built->DialParts.Num(); ++D)
	{
		const FSimCruiseBuiltPart& Part = Built->DialParts[D];
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(this);
		Target->RenderTargetFormat = RTF_RGBA8_SRGB;
		Target->ClearColor = FLinearColor::Black;
		const int32 SpecIndex = Built->Dials.IsValidIndex(D) ? Built->Dials[D].Spec : -1;
		// A straight scale's face is a strip: its target keeps the strip's shape (512 across).
		const FSimCruiseDialSpec* LinearSpec = Built->Profile.Dials.IsValidIndex(SpecIndex) && Built->Profile.Dials[SpecIndex].IsLinear() ? &Built->Profile.Dials[SpecIndex] : nullptr;
		const int32 TargetHigh = LinearSpec ? FMath::Clamp(4 * FMath::RoundToInt(128.0 * LinearSpec->Radius / (LinearSpec->LengthM * 0.5 + 0.6 * LinearSpec->Radius)), 32, 512) : 256;
		Target->InitAutoFormat(LinearSpec ? 512 : 256, TargetHigh);
		Target->UpdateResourceImmediate(true);
		DialTargets.Add(Target);
		DialShown.Add(0.0);
		const bool bFace = !Built->Profile.Dials.IsValidIndex(SpecIndex) || Built->Profile.Dials[SpecIndex].bFace;
		if (bFace)
		{
			AddPart(this, Part, Part.PartToVehicle);
		}
		if (UMaterialInstanceDynamic* Face = bFace ? MaterialFor(SimCruiseCarAssets::DialMaterialIndex(D)) : nullptr)
		{
			Face->SetTextureParameterValue(TEXT("BaseTexture"), Target);
			Face->SetTextureParameterValue(TEXT("EmissiveTexture"), Target);
			Face->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.8f, 0.8f, 0.8f, 1.f));
			Face->SetVectorParameterValue(TEXT("TextureFlags"), FLinearColor(1.f, 0.f, 0.f, 0.f));
			FLinearColor Extra(0.f, 0.f, 0.f, 0.f);
			Face->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Extra")), Extra);
			Extra.B = 1.f;
			Face->SetVectorParameterValue(TEXT("Extra"), Extra);
			// The marks white paint lit from behind (about 250 nits, so they read in the shade under the cowl), the face black.
			Face->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(1.f, 1.f, 1.f, 250.f));
		}
		if (bFace)
		{
			DrawDialFace(D);
		}
		// The needle over it, an orange-red pointer lit from behind as the marks are.
		UStaticMeshComponent* Needle = nullptr;
		if (Built->NeedleParts.IsValidIndex(D))
		{
			const FSimCruiseBuiltPart& NeedlePart = Built->NeedleParts[D];
			Needle = AddPart(this, NeedlePart, NeedlePart.PartToVehicle);
			if (Needle) Needle->SetCastShadow(false);
			if (UMaterialInstanceDynamic* Paint = MaterialFor(SimCruiseCarAssets::NeedleMaterialIndex(D)))
			{
				Paint->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.9f, 0.22f, 0.04f, 1.f));
				Paint->SetVectorParameterValue(TEXT("TextureFlags"), FLinearColor(0.f, 0.f, 0.f, 0.f));
				Paint->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(1.f, 0.3f, 0.06f, 40.f));
			}
		}
		Needles.Add(Needle);
		NeedleRest.Add(Part.PartToVehicle);
	}
	ApplyVisualState(FSimCruiseCarVisualState());
	return true;
}

double USimCruiseCarPresentationComponent::DialTarget(const FString& Quantity, const FSimCruiseCarVisualState& State)
{
	if (Quantity == TEXT("speed_mph")) return FMath::Abs(State.SpeedMps) * 2.2369362921;
	if (Quantity == TEXT("speed_kmh")) return FMath::Abs(State.SpeedMps) * 3.6;
	if (Quantity == TEXT("rpm")) return State.bEngineRunning ? State.EngineRpm : 0.0;
	// No cooling or fuel model yet: a warm engine at mid-scale and a tank three-quarters full.
	if (Quantity == TEXT("coolant")) return State.bEngineRunning ? 0.5 : 0.0;
	if (Quantity == TEXT("fuel")) return 0.72;
	// Oil pressure rises with engine speed to the relief valve's pressure.
	if (Quantity == TEXT("oil_pressure")) return State.bEngineRunning ? FMath::Clamp(0.3 + 0.55 * State.EngineRpm / 4000.0, 0.0, 0.85) : 0.0;
	return 0.0;
}

double USimCruiseCarPresentationComponent::GetDialValue(const FString& Quantity) const
{
	for (int32 D = 0; Built.IsValid() && D < Built->Dials.Num() && D < DialShown.Num(); ++D)
	{
		const int32 Spec = Built->Dials[D].Spec;
		if (Built->Profile.Dials.IsValidIndex(Spec) && Built->Profile.Dials[Spec].Quantity == Quantity)
		{
			return DialShown[D];
		}
	}
	return std::numeric_limits<double>::quiet_NaN();
}

double USimCruiseCarPresentationComponent::NeedleAngleRad(const FSimCruiseDialSpec& Dial, double Value)
{
	const double Range = Dial.MaxValue - Dial.MinValue;
	// A needle rests a hair below its stop pin and can swing a little past the last mark.
	const double T = FMath::Abs(Range) > 1e-9 ? FMath::Clamp((Value - Dial.MinValue) / Range, -0.02, 1.02) : 0.0;
	return FMath::DegreesToRadians(Dial.StartDeg + Dial.SweepDeg * T);
}

void USimCruiseCarPresentationComponent::UpdateDials(float DeltaTime)
{
	if (ScreenMaterial)
	{
		// A digital tachometer's arc follows the engine closely (about 0.05 s); the speed as the car's.
		const double Target = DialTarget(TEXT("rpm"), LastState);
		ScreenRpm += (Target - ScreenRpm) * (1.0 - FMath::Exp(-FMath::Max(DeltaTime, 0.f) / 0.05));
		ScreenMaterial->SetScalarParameterValue(TEXT("Rpm"), static_cast<float>(ScreenRpm));
		ScreenMaterial->SetScalarParameterValue(TEXT("Mph"), static_cast<float>(DialTarget(TEXT("speed_mph"), LastState)));
	}
	if (!Built.IsValid() || Needles.IsEmpty())
	{
		return;
	}
	// Needles follow with a needle's own lag (about 0.12 s) and turn as meshes: no redraw.
	const double Follow = 1.0 - FMath::Exp(-FMath::Max(DeltaTime, 0.f) / 0.12);
	for (int32 D = 0; D < Needles.Num() && D < Built->Dials.Num(); ++D)
	{
		const int32 Spec = Built->Dials[D].Spec;
		if (!Built->Profile.Dials.IsValidIndex(Spec)) continue;
		const FSimCruiseDialSpec& Dial = Built->Profile.Dials[Spec];
		// A tachometer's needle is light and fast (it follows a gear change within about a tenth of a
		// second); the others keep the slower needle lag.
		const double DialFollow = Dial.Quantity == TEXT("rpm") ? 1.0 - FMath::Exp(-FMath::Max(DeltaTime, 0.f) / 0.04) : Follow;
		DialShown[D] += (DialTarget(Dial.Quantity, LastState) - DialShown[D]) * DialFollow;
		if (UStaticMeshComponent* Needle = Needles[D])
		{
			if (Dial.IsLinear())
			{
				// A straight scale: the bar slides along the part's +Y (to the right), centred on the scale.
				const double Range = Dial.MaxValue - Dial.MinValue;
				const double T = FMath::Abs(Range) > 1e-9 ? FMath::Clamp((DialShown[D] - Dial.MinValue) / Range, -0.01, 1.01) : 0.0;
				const double AlongCm = (T - 0.5) * Dial.LengthM * 100.0;
				Needle->SetRelativeTransform(FTransform(FVector(0.0, AlongCm, 0.0)) * NeedleRest[D]);
			}
			else
			{
				// The part's +X is 12 o'clock and +Y 3 o'clock, so clockwise is a turn about +Z.
				const FQuat Turn(FVector::UpVector, NeedleAngleRad(Dial, DialShown[D]));
				Needle->SetRelativeTransform(FTransform(Turn) * NeedleRest[D]);
			}
		}
	}
}

void USimCruiseCarPresentationComponent::DrawDialFace(int32 Index)
{
	if (!Built.IsValid() || !DialTargets.IsValidIndex(Index) || !DialTargets[Index] || !GEngine) return;
	const int32 Spec = Built->Dials[Index].Spec;
	if (!Built->Profile.Dials.IsValidIndex(Spec)) return;
	const FSimCruiseDialSpec& Dial = Built->Profile.Dials[Spec];
	UTextureRenderTarget2D* Target = DialTargets[Index];
	UKismetRenderingLibrary::ClearRenderTarget2D(this, Target, FLinearColor::Black);
	UCanvas* Canvas = nullptr;
	FVector2D Size;
	FDrawToRenderTargetContext Context;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Target, Canvas, Size, Context);
	if (!Canvas)
	{
		return;
	}
	const double Range = Dial.MaxValue - Dial.MinValue;
	const FLinearColor Ink(0.92f, 0.92f, 0.9f, 1.f);
	if (Dial.IsLinear())
	{
		// A straight (ribbon) scale: ticks along a line across the middle, numerals above, a caption below.
		const double Margin = 0.6 * Dial.Radius / (Dial.LengthM + 1.2 * Dial.Radius) * Size.X;
		auto Xof = [&](double V) { return Margin + (Size.X - 2.0 * Margin) * (Range != 0.0 ? (V - Dial.MinValue) / Range : 0.0); };
		const double Mid = Size.Y * 0.55;
		Canvas->K2_DrawLine(FVector2D(Xof(Dial.MinValue), Mid + Size.Y * 0.2), FVector2D(Xof(Dial.MaxValue), Mid + Size.Y * 0.2), 2.f, Ink);
		UFont* LineFont = GEngine->GetLargeFont();
		const int32 Steps = Dial.MajorStep > 0.0 ? FMath::RoundToInt(Range / Dial.MajorStep) : 0;
		for (int32 M = 0; M <= Steps && M <= 60; ++M)
		{
			const double V = Dial.MinValue + M * Dial.MajorStep;
			Canvas->K2_DrawLine(FVector2D(Xof(V), Mid), FVector2D(Xof(V), Mid + Size.Y * 0.2), 5.f, Ink);
			for (int32 K = 1; K <= Dial.MinorPerMajor && M < Steps; ++K)
			{
				const double Vm = V + Dial.MajorStep * K / (Dial.MinorPerMajor + 1);
				Canvas->K2_DrawLine(FVector2D(Xof(Vm), Mid + Size.Y * 0.1), FVector2D(Xof(Vm), Mid + Size.Y * 0.2), 3.f, Ink);
			}
			if (LineFont)
			{
				Canvas->K2_DrawText(LineFont, FString::Printf(TEXT("%d"), FMath::RoundToInt(V / FMath::Max(Dial.LabelDivisor, 1e-6))), FVector2D(Xof(V), Size.Y * 0.28),
					FVector2D(1.4, 1.4), Ink, 0.f, FLinearColor::Transparent, FVector2D::ZeroVector, true, true, false, FLinearColor::Black);
			}
		}
		if (LineFont && !Dial.Caption.IsEmpty())
		{
			Canvas->K2_DrawText(LineFont, Dial.Caption, FVector2D(Size.X * 0.5, Size.Y * 0.9), FVector2D(0.9, 0.9), Ink, 0.f, FLinearColor::Transparent,
				FVector2D::ZeroVector, true, true, false, FLinearColor::Black);
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
		return;
	}
	const FVector2D C = Size * 0.5;
	const double R = Size.X * 0.5;
	auto Angle = [&Dial](double V) { return NeedleAngleRad(Dial, V); };
	// Clockwise from 12 o'clock with the canvas's y down.
	auto At = [&](double A, double Fraction) { return C + FVector2D(FMath::Sin(A), -FMath::Cos(A)) * R * Fraction; };
	const FLinearColor Mark(0.92f, 0.92f, 0.9f, 1.f), Red(0.85f, 0.08f, 0.05f, 1.f);
	UFont* Font = GEngine->GetMediumFont();
	// Radius in metres: the small gauges are under 4 cm.
	const bool bSmall = Dial.Radius < 0.04;
	const int32 Majors = Dial.MajorStep > 0.0 ? FMath::RoundToInt(Range / Dial.MajorStep) : 0;
	for (int32 M = 0; M <= Majors && M <= 40; ++M)
	{
		const double V = Dial.MinValue + M * Dial.MajorStep;
		const bool bRed = Dial.RedFrom > 0.0 && V >= Dial.RedFrom - 1e-6;
		const double A = Angle(V);
		Canvas->K2_DrawLine(At(A, 0.80), At(A, 0.96), bSmall ? 5.f : 4.f, bRed ? Red : Mark);
		if (!bSmall && M < Majors)
		{
			for (int32 K = 1; K <= Dial.MinorPerMajor; ++K)
			{
				const double Vm = V + Dial.MajorStep * K / (Dial.MinorPerMajor + 1);
				const bool bRedMinor = Dial.RedFrom > 0.0 && Vm >= Dial.RedFrom - 1e-6;
				Canvas->K2_DrawLine(At(Angle(Vm), 0.88), At(Angle(Vm), 0.96), 2.f, bRedMinor ? Red : Mark);
			}
		}
		if (!bSmall && Font)
		{
			const FString Text = FString::Printf(TEXT("%d"), FMath::RoundToInt(V / FMath::Max(Dial.LabelDivisor, 1e-6)));
			Canvas->K2_DrawText(Font, Text, At(A, 0.62), FVector2D(1.35, 1.35), bRed ? Red : Mark, 0.f, FLinearColor::Transparent,
				FVector2D::ZeroVector, true, true, false, FLinearColor::Black);
		}
	}
	// The red zone as a band just inside the ticks.
	if (Dial.RedFrom > 0.0 && Dial.RedFrom < Dial.MaxValue)
	{
		for (double V = Dial.RedFrom; V < Dial.MaxValue; V += Range / 120.0)
		{
			Canvas->K2_DrawLine(At(Angle(V), 0.97), At(Angle(FMath::Min(V + Range / 120.0, Dial.MaxValue)), 0.97), 5.f, Red);
		}
	}
	if (Font)
	{
		const FVector2D LabelScale = bSmall ? FVector2D(2.6, 2.6) : FVector2D(1.1, 1.1);
		if (!Dial.StartLabel.IsEmpty())
			Canvas->K2_DrawText(Font, Dial.StartLabel, At(Angle(Dial.MinValue), 0.58), LabelScale, Mark, 0.f, FLinearColor::Transparent, FVector2D::ZeroVector, true, true, false, FLinearColor::Black);
		if (!Dial.EndLabel.IsEmpty())
			Canvas->K2_DrawText(Font, Dial.EndLabel, At(Angle(Dial.MaxValue), 0.58), LabelScale, Dial.RedFrom > 0.0 ? Red : Mark, 0.f, FLinearColor::Transparent, FVector2D::ZeroVector, true, true, false, FLinearColor::Black);
		if (!Dial.Caption.IsEmpty())
			Canvas->K2_DrawText(Font, Dial.Caption, C + FVector2D(0.0, R * (bSmall ? 0.42 : 0.38)), bSmall ? FVector2D(1.8, 1.8) : FVector2D(0.95, 0.95), Mark, 0.f,
				FLinearColor::Transparent, FVector2D::ZeroVector, true, true, false, FLinearColor::Black);
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
}

void USimCruiseCarPresentationComponent::SetWheelStateProvider(TScriptInterface<ISimCruiseWheelStateProvider> Provider)
{
	StateProvider = Provider;
}

TArray<FVector> USimCruiseCarPresentationComponent::GetHeadLampLocations() const
{
	return Built.IsValid() ? Built->HeadLampsCm : TArray<FVector>();
}

const FSimCruiseCarPresentationProfile* USimCruiseCarPresentationComponent::GetProfile() const
{
	return Built.IsValid() ? &Built->Profile : nullptr;
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
	const double WheelTarget = State.bHasSteeringWheel ? State.SteeringWheelRad
		: SteeringWheelAngleFor(State.Wheels[0].SteerAngleRad, State.Wheels[1].SteerAngleRad, GetWheelSteeringRatio());
	// The rim on screen never turns faster than hands can (963 deg/s, NHTSA's fastest sustained
	// test-driver input): a reset, a replay settling or a frame hitch would otherwise spin it in
	// one frame and smear a doubled rim across the dash.
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// (No clock between two states, as in a test or the first frame: straight to the angle.)
	if (SteeringWheelTime < 0.0 || Now <= SteeringWheelTime)
	{
		SteeringWheelAngle = WheelTarget;
	}
	else
	{
		const double Step = FMath::DegreesToRadians(963.0) * FMath::Clamp(Now - SteeringWheelTime, 0.0, 0.1);
		SteeringWheelAngle += FMath::Clamp(WheelTarget - SteeringWheelAngle, -Step, Step);
	}
	SteeringWheelTime = Now;
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
				UpdateCabinFill();
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
			LastState = State;
		}
	}
	UpdateDials(DeltaTime);
}

void USimCruiseCarPresentationComponent::UpdateCabinFill()
{
	if (!CabinFill) return;
	const USimCruiseEnvironmentSubsystem* Subsystem = USimCruiseEnvironmentSubsystem::Get(GetWorld());
	const ASimCruiseEnvironment* Environment = Subsystem ? Subsystem->GetEnvironment() : nullptr;
	if (!Environment) return;
	// Incident light at the reference exposure: E = 2.5 * 2^EV100 lux (C = 250). About a fifth
	// of it is sky; a quarter of that reaches the dash through the openings. At 50 cm a point of
	// I candela gives I / 0.25 lux, so I = 0.25 * 0.25 * 0.2 * E.
	const double E = 2.5 * FMath::Pow(2.0, static_cast<double>(Environment->GetReferenceEV100()));
	CabinFill->SetIntensity(static_cast<float>(0.0125 * E));
}

void USimCruiseCarPresentationComponent::SetMirrorsLive(bool bLive, USceneComponent* Parent, const FVector& RelativeEyeCm)
{
	if (!bLive)
	{
		if (MirrorCapture) { MirrorCapture->DestroyComponent(); MirrorCapture = nullptr; }
		if (CabinFill) { CabinFill->DestroyComponent(); CabinFill = nullptr; }
		for (UMaterialInstanceDynamic* Mirror : MirrorMaterials) { Mirror->SetScalarParameterValue(TEXT("HasCapture"), 0.f); }
		return;
	}
	if (!CabinFill && Parent)
	{
		// Sky light through the windscreen and the open top. Lumen's short-range occlusion treats
		// the cabin as nearly closed: in shade the dash fell to 17 % of its sunlit brightness on Tail
		// of the Dragon (a real open cabin in shade keeps about a third), so leaving a tree's shadow
		// flipped it from black to bright in one frame. A soft, shadowless fill between the seats and
		// the dash, no specular, not seen by Lumen, its strength set from the daylight level.
		CabinFill = NewObject<UPointLightComponent>(GetOwner() ? static_cast<UObject*>(GetOwner()) : static_cast<UObject*>(this), TEXT("CabinFill"));
		CabinFill->SetupAttachment(Parent);
		CabinFill->SetRelativeLocation(FVector(RelativeEyeCm.X + 45.0, 0.0, RelativeEyeCm.Z - 30.0));
		CabinFill->SetMobility(EComponentMobility::Movable);
		CabinFill->SetCastShadows(false);
		CabinFill->SetIntensityUnits(ELightUnits::Candelas);
		CabinFill->SetAttenuationRadius(130.f);
		CabinFill->SetSourceRadius(35.f);
		CabinFill->SetSoftSourceRadius(35.f);
		CabinFill->SetSpecularScale(0.f);
		CabinFill->SetIndirectLightingIntensity(0.f);
		CabinFill->SetVolumetricScatteringIntensity(0.f);
		CabinFill->bAffectTranslucentLighting = false;
		CabinFill->SetLightColor(FLinearColor(0.85f, 0.92f, 1.0f));
		CabinFill->RegisterComponent();
		UpdateCabinFill();
	}
	if (MirrorCapture || !Parent)
	{
		return;
	}
	// Wide enough for the three glasses (the door mirrors look outward, the passenger's is
	// convex); 1152 x 480 puts about 11.5 texels in each degree (768 x 320 read as a blocky,
	// blurred picture in the rear-view glass at 1080p: release review 2026-11-14).
	constexpr float FovDeg = 100.f;
	constexpr int32 SizeX = 1152, SizeY = 480;
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
	// atmosphere alone); motion blur, bloom and volumetric fog. Anti-aliasing stays on: without it
	// the glass showed stair-stepped edges. (Set as
	// show-flag settings: registering the capture rebuilds its flags from them.)
	TArray<FEngineShowFlagsSetting> Flags;
	for (const TCHAR* Name : {TEXT("MotionBlur"), TEXT("Bloom"), TEXT("VolumetricFog"), TEXT("DistanceFieldAO"),
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
