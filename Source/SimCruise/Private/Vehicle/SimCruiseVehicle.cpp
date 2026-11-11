// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseVehicle.h"

#include "Components/BoxComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruiseEnvironmentSubsystem.h"
#include "Gameplay/SimCruiseHaptics.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseVehiclePawn, Log, All);

ASimCruiseVehicle::ASimCruiseVehicle()
{
	// Ticks only to pass the gearbox's refusals to the driver's screen.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

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

bool ASimCruiseVehicle::AttachCarModel()
{
	if (!CarModel)
	{
		CarModel = NewObject<USimCruiseCarPresentationComponent>(this, TEXT("CarModel"));
		CarModel->SetMobility(EComponentMobility::Movable);
		CarModel->SetupAttachment(Body);
		CarModel->RegisterComponent();
		AddInstanceComponent(CarModel);
	}
	// The model's frame is the Godot car origin: the ground at ride height,
	// CollisionShapeCenterYM below the body's centre.
	const double OriginCm = SimCruiseUnits::ToUnrealLength(VehicleMovement->GetCarDefinition().CollisionShapeCenterYM);
	CarModel->SetRelativeTransform(FTransform(FVector(0.0, 0.0, -OriginCm)));
	if (!CarModel->InitializeForCar(CarId))
	{
		UE_LOG(LogSimCruiseVehiclePawn, Warning, TEXT("%s drives without a visible model: %s"), *CarId, *CarModel->GetLastError());
		return false;
	}
	// Every part is visual only (no collision), so nothing welds into the Chaos body.
	CarModel->SetWheelStateProvider(TScriptInterface<ISimCruiseWheelStateProvider>(this));
	// Pose after this pawn's physics results are in.
	CarModel->AddTickPrerequisiteActor(this);

	// The low beams: real lights from the head-lamp lenses, so the road ahead is lit at night.
	for (USpotLightComponent* Light : HeadLampLights)
	{
		if (Light)
		{
			Light->DestroyComponent();
		}
	}
	HeadLampLights.Reset();
	bHeadLampsShown = false;
	TArray<FVector> Lamps = CarModel->GetHeadLampLocations();
	if (Lamps.IsEmpty())
	{
		// No head-lamp surfaces on the model: the usual place, about 55 cm ahead of
		// the front hubs, just inside their track and 30 cm above them.
		const FVector FL = CarModel->GetStaticHubLocation(0), FR = CarModel->GetStaticHubLocation(1);
		Lamps.Add(FL + FVector(55.0, 12.0, 30.0));
		Lamps.Add(FR + FVector(55.0, -12.0, 30.0));
		UE_LOG(LogSimCruiseVehiclePawn, Display, TEXT("%s: no head-lamp lenses on the model; beams placed from the hubs"), *CarId);
	}
	for (int32 I = 0; I < Lamps.Num(); ++I)
	{
		USpotLightComponent* Light = NewObject<USpotLightComponent>(this, *FString::Printf(TEXT("LowBeam%d"), I));
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(CarModel);
		// Just ahead of the lens: from inside the housing the body would shadow its own beam.
		Light->SetRelativeLocation(Lamps[I] + FVector(18.0, 0.0, 0.0));
		Light->SetRelativeRotation(FRotator(-LowBeamAimDownDeg, 0.0, 0.0));
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(static_cast<float>(LowBeamPeakCandela));
		Light->SetInnerConeAngle(static_cast<float>(LowBeamCoreHalfAngleDeg));
		Light->SetOuterConeAngle(static_cast<float>(LowBeamEdgeHalfAngleDeg));
		// Where the beam falls to about a lux: sqrt(15000 cd / 1 lx) = 120 m.
		Light->SetAttenuationRadius(static_cast<float>(SimCruiseUnits::ToUnrealLength(FMath::Sqrt(LowBeamPeakCandela))));
		Light->SetSourceRadius(6.f);
		Light->SetUseTemperature(true);
		Light->SetTemperature(3200.f);
		Light->SetCastShadows(true);
		Light->SetVisibility(false);
		Light->RegisterComponent();
		AddInstanceComponent(Light);
		HeadLampLights.Add(Light);
	}
	return true;
}

void ASimCruiseVehicle::UpdateHeadLamps()
{
	const bool bLit = AreLampsWanted();
	if (bLit != bHeadLampsShown)
	{
		bHeadLampsShown = bLit;
		for (USpotLightComponent* Light : HeadLampLights)
		{
			if (Light)
			{
				Light->SetVisibility(bLit);
			}
		}
	}
	// The player's eye adapts to the road his own lamps light.
	if (IsPlayerControlled())
	{
		if (USimCruiseEnvironmentSubsystem* Subsystem = USimCruiseEnvironmentSubsystem::Get(GetWorld()))
		{
			if (ASimCruiseEnvironment* Environment = Subsystem->GetEnvironment())
			{
				const double Lux = bLit && !HeadLampLights.IsEmpty()
					? HeadLampLights.Num() * LowBeamPeakCandela / FMath::Square(NightAdaptationDistanceM) : 0.0;
				Environment->SetArtificialIlluminanceLux(Lux);
			}
		}
	}
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

// --- Driving shell ---------------------------------------------------------

FSimCruiseVehicleSetup ASimCruiseVehicle::GetVehicleSetup() const
{
	const FSimCruiseCarDefinition& Car = VehicleMovement->GetCarDefinition();
	FSimCruiseVehicleSetup Setup;
	Setup.CarId = Car.Id;
	Setup.DisplayName = Car.Name;
	Setup.WheelbaseM = Car.WheelbaseM;
	Setup.CenterOfMassToFrontAxleM = Car.WheelbaseM * (1.0 - Car.FrontWeightFraction);
	Setup.ParkingRackAngleRad = FMath::DegreesToRadians(Car.ParkingRackAngleDeg);
	Setup.SteeringAssistLateralG = Car.SteeringAssistLateralG;
	Setup.SteeringSlipLimitSurplusRad = FMath::DegreesToRadians(Car.SteeringSlipLimitSurplusDeg);
	Setup.SteeringRatio = CarModel ? CarModel->GetSteeringRatio() : 0.0;
	// The car file's eye is relative to the Godot origin; this pawn's origin is the body centre above it.
	Setup.DriverEyeLocalCm = SimCruiseGodotToLocalCm(Car.DriverEyeM) - FVector(0.0, 0.0, SimCruiseUnits::ToUnrealLength(Car.CollisionShapeCenterYM));
	Setup.IdleRpm = Car.IdleSpeedRpm;
	Setup.UpshiftRpm = Car.AutomaticUpshiftSpeedRpm;
	Setup.RedlineRpm = Car.FuelCutSpeedRpm;
	Setup.ForwardGearCount = Car.ForwardGearRatios.Num();
	Setup.SkidpadG = Car.TargetSkidpadG;
	const double SeventyMph = 70.0 * 0.44704;
	const double StopM = Car.TargetSeventyToZeroFeet * 0.3048;
	Setup.BrakingG = StopM > 1.0 ? SeventyMph * SeventyMph / (2.0 * StopM) / 9.80665 : 0.9;
	return Setup;
}

void ASimCruiseVehicle::ToDriveTelemetry(const FSimCruiseCarDefinition& Car, const FSimCruiseVehicleTelemetry& In, FSimCruiseDriveTelemetry& Out)
{
	Out.BodyVelocityMps = In.BodyVelocityLocalMps;
	Out.BodyAngularVelocityRadPerS = In.BodyAngularVelocityLocalRadPerSecond;
	Out.EngineSpeedRpm = In.EngineRpm;
	Out.Gear = In.Gear;
	Out.bManualTransmission = In.bManual;
	Out.bEngineStalled = In.bStalled;
	Out.bElectric = Car.EngineKind == TEXT("electric");
	Out.bServiceBrakeOn = In.bServiceBrake;
	Out.RackAngleRad = FMath::DegreesToRadians(In.SteeringAngleDeg);
	Out.EngineLoad = FMath::Clamp(In.SoundCombustion, 0.0, 1.0);
	Out.RoadRoughness = 0.0;
	const double PlateauRad = SimCruiseHaptics::PlateauSlipAngleRad(Car);
	for (int32 I = 0; I < 4; ++I)
	{
		FSimCruiseDriveWheelTelemetry& W = Out.Wheels[I];
		W = FSimCruiseDriveWheelTelemetry();
		if (!In.Wheels.IsValidIndex(I))
		{
			continue;
		}
		const FSimCruiseWheelTelemetry& V = In.Wheels[I];
		W.bContact = V.bContact;
		W.bDriven = Car.IsDrivenWheel(I);
		W.bAbsActive = V.AbsRelease < 0.95;
		W.NormalLoadN = V.LoadN;
		W.SlipRatio = V.SlipRatio;
		W.PeakSlipRatio = V.PeakSlipRatio;
		W.SlipAngleRad = FMath::DegreesToRadians(V.SlipAngleDeg);
		W.PeakSlipAngleRad = PlateauRad;
		// Compression from full droop (the longest the strut can be).
		W.SuspensionTravelM = FMath::Max(0.0, Car.MaximumSuspensionTravelM - V.SuspensionLengthCm / 100.0);
		W.SurfaceGrip = V.SurfaceGrip;
	}
}

void ASimCruiseVehicle::GetVehicleTelemetry(FSimCruiseDriveTelemetry& OutTelemetry) const
{
	FSimCruiseVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	ToDriveTelemetry(VehicleMovement->GetCarDefinition(), Telemetry, OutTelemetry);
}

void ASimCruiseVehicle::ApplyDriverCommand(const FSimCruiseDriverCommand& Command)
{
	FSimCruiseVehicleControls Controls;
	Controls.Throttle = FMath::Clamp(Command.Throttle, 0.f, 1.f);
	Controls.Brake = FMath::Clamp(Command.Brake, 0.f, 1.f);
	Controls.Clutch = FMath::Clamp(Command.Clutch, 0.f, 1.f);
	Controls.bHandbrake = Command.ParkingBrake > 0.5f;
	Controls.bFullBrake = Command.bFullServiceBrake;
	// The shell's shaper owns steering rates, the speed cap and the countersteer allowance.
	Controls.bDirectRack = true;
	Controls.RackAngleRad = Command.RackAngleRad;
	VehicleMovement->SetDriverControls(Controls);
}

FString ASimCruiseVehicle::RequestGearChange(int32 Step)
{
	FSimCruiseVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	if (!Telemetry.bManual && !bManualRequested)
	{
		return TEXT("Automatic shifting · T (B) for manual");
	}
	// The gearbox answers on the physics thread; a refusal reaches the screen from Tick.
	LastShownGearMessage.Reset();
	if (Step > 0)
	{
		VehicleMovement->ShiftUp();
	}
	else if (Step < 0)
	{
		VehicleMovement->ShiftDown();
	}
	return FString();
}

void ASimCruiseVehicle::SetManualTransmission(bool bManual)
{
	bManualRequested = bManual;
	VehicleMovement->SetManualTransmission(bManual);
}

void ASimCruiseVehicle::RequestEngineStart()
{
	VehicleMovement->RequestEngineStart();
}

void ASimCruiseVehicle::ResetVehicle(const FTransform& WorldTransform)
{
	ResetAt(WorldTransform.GetLocation(), static_cast<float>(WorldTransform.Rotator().Yaw));
}

void ASimCruiseVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateHeadLamps();
	FSimCruiseVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	if (!Telemetry.LastGearMessage.IsEmpty() && Telemetry.LastGearMessage != LastShownGearMessage)
	{
		LastShownGearMessage = Telemetry.LastGearMessage;
		if (ASimCruisePlayerController* Player = Cast<ASimCruisePlayerController>(GetController()))
		{
			Player->ShowMessage(Telemetry.LastGearMessage, 2.f);
		}
	}
}

// --- Car model ---------------------------------------------------------------

void ASimCruiseVehicle::ToVisualState(const FSimCruiseCarDefinition& Car, const FSimCruiseVehicleTelemetry& In, FSimCruiseCarVisualState& Out)
{
	for (int32 I = 0; I < 4; ++I)
	{
		FSimCruiseWheelVisualState& W = Out.Wheels[I];
		if (!In.Wheels.IsValidIndex(I))
		{
			W = FSimCruiseWheelVisualState();
			continue;
		}
		const FSimCruiseWheelTelemetry& V = In.Wheels[I];
		const bool bLeft = I == 0 || I == 2;
		// The model's steer is positive to the left; the vehicle's to the right.
		W.SteerAngleRad = static_cast<float>(-FMath::DegreesToRadians(V.SteerAngleDeg));
		// Inclination leans the top toward the car's right; camber is positive with the top outboard.
		W.CamberRad = static_cast<float>(FMath::DegreesToRadians(V.InclinationDeg) * (bLeft ? -1.0 : 1.0));
		W.SpinAngleRad = static_cast<float>(FMath::Fmod(FMath::DegreesToRadians(V.SpinAngleDeg), UE_DOUBLE_TWO_PI));
		// Bump (wheel up toward the body) is positive: the strut shorter than at static ride.
		W.SuspensionTravelCm = static_cast<float>(Car.StaticSuspensionLengthM * 100.0 - V.SuspensionLengthCm);
	}
	Out.bBrake = In.bServiceBrake;
}

bool ASimCruiseVehicle::AreLampsWanted() const
{
	if (bLampsOn)
	{
		return true;
	}
	const USimCruiseEnvironmentSubsystem* Subsystem = USimCruiseEnvironmentSubsystem::Get(GetWorld());
	const ASimCruiseEnvironment* Environment = Subsystem ? Subsystem->GetEnvironment() : nullptr;
	return Environment && Environment->GetSunPosition().ApparentElevationDeg < -1.0;
}

bool ASimCruiseVehicle::GetCarVisualState(FSimCruiseCarVisualState& OutState) const
{
	FSimCruiseVehicleTelemetry Telemetry;
	VehicleMovement->CopyTelemetry(Telemetry);
	if (Telemetry.PhysicsSteps <= 0)
	{
		return false;
	}
	ToVisualState(VehicleMovement->GetCarDefinition(), Telemetry, OutState);
	OutState.bLamps = AreLampsWanted();
	return true;
}
