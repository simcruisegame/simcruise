// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePlaceholderVehicle.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/SimCruiseHaptics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SimCruiseGameInstance.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicle/CarDefinitionLoader.h"

namespace
{
	constexpr double G = 9.80665;
	constexpr double PhysicsStepS = 1.0 / 240.0;
	constexpr double AirDensity = 1.204;

	double LateralLaw(const FSimCruiseCarDefinition& Car, double Alpha)
	{
		const double A = FMath::Abs(Alpha);
		const double F = FMath::Tanh(A * Car.SlipAngleStiffnessPerRad)
			* (1.0 - Car.SlipAngleFalloffFraction * FMath::SmoothStep(Car.SlipAngleFalloffStartRad, Car.SlipAngleFalloffEndRad, A));
		return FMath::Sign(Alpha) * F;
	}
}

ASimCruisePlaceholderVehicle::ASimCruisePlaceholderVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	AutoPossessPlayer = EAutoReceiveInput::Disabled;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	auto MakeMesh = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* Component = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Component->SetupAttachment(Root);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		return Component;
	};
	BodyMesh = MakeMesh(TEXT("Body"), CubeMesh.Object);
	CabinMesh = MakeMesh(TEXT("Cabin"), CubeMesh.Object);
	static const TCHAR* WheelNames[4] = {TEXT("WheelFL"), TEXT("WheelFR"), TEXT("WheelRL"), TEXT("WheelRR")};
	for (int32 I = 0; I < 4; ++I)
	{
		WheelMeshes.Add(MakeMesh(WheelNames[I], CylinderMesh.Object));
	}
}

void ASimCruisePlaceholderVehicle::SetCarDefinition(const FSimCruiseCarDefinition& InCar)
{
	Car = InCar;
	PeakSlipAngleRad = SimCruiseHaptics::PlateauSlipAngleRad(Car);
	EngineRpm = Car.IdleSpeedRpm;
	BuildVisuals();
}

void ASimCruisePlaceholderVehicle::BeginPlay()
{
	Super::BeginPlay();
	const USimCruiseGameInstance* GameInstance = GetGameInstance<USimCruiseGameInstance>();
	const FSimCruiseCarDefinition* Found = GameInstance ? GameInstance->FindCarDefinition(TEXT("nb1999")) : nullptr;
	if (Found)
	{
		SetCarDefinition(*Found);
	}
	else
	{
		FSimCruiseCarDefinition Loaded;
		FString Error;
		if (!FSimCruiseCarDefinitionLoader::LoadById(TEXT("nb1999"), Loaded, Error))
		{
			UE_LOG(LogTemp, Warning, TEXT("Placeholder vehicle uses built-in NB defaults: %s"), *Error);
		}
		SetCarDefinition(Loaded);
	}
	ResetVehicle(GetActorTransform());
}

void ASimCruisePlaceholderVehicle::BuildVisuals()
{
	if (!BodyMesh)
	{
		return;
	}
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	auto Paint = [this, Base](UStaticMeshComponent* Component, const FLinearColor& Color)
	{
		if (!Base || !Component)
		{
			return;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
		Mid->SetVectorParameterValue(TEXT("Color"), Color);
		Component->SetMaterial(0, Mid);
		Paints.Add(Mid);
	};
	Paints.Reset();

	// Lower body: the definition's collision box (Godot x width, y height, z length).
	const FVector3d Box = Car.CollisionBoxSizeM;
	BodyMesh->SetRelativeLocation(FVector(0.0, 0.0, Car.CollisionShapeCenterYM * 100.0));
	BodyMesh->SetRelativeScale3D(FVector(Box.Z, Box.X, Box.Y));
	Paint(BodyMesh, FLinearColor(0.32f, 0.015f, 0.02f));
	// Greenhouse over the seats, enclosing the driver's eye.
	const double BodyTop = (Car.CollisionShapeCenterYM + Box.Y * 0.5) * 100.0;
	const FVector Eye = SimCruiseGodotToLocalCm(Car.DriverEyeM);
	CabinMesh->SetRelativeLocation(FVector(Eye.X - 15.0, 0.0, BodyTop + 20.0));
	CabinMesh->SetRelativeScale3D(FVector(1.45, Box.X * 0.86, 0.40));
	Paint(CabinMesh, FLinearColor(0.02f, 0.025f, 0.03f));

	const double Radius = Car.TireFreeRadiusM();
	for (int32 I = 0; I < WheelMeshes.Num(); ++I)
	{
		UStaticMeshComponent* Wheel = WheelMeshes[I];
		const double Track = I < 2 ? Car.FrontTrackM : Car.RearTrackM;
		Wheel->SetRelativeLocation(FVector((I < 2 ? 0.5 : -0.5) * Car.WheelbaseM * 100.0, (I % 2 == 0 ? -0.5 : 0.5) * Track * 100.0, Radius * 100.0));
		Wheel->SetRelativeScale3D(FVector(Radius * 2.0, Radius * 2.0, Car.TireSectionWidthM));
		Paint(Wheel, FLinearColor(0.012f, 0.012f, 0.014f));
	}
	UpdateVisuals(0.0);
}

double ASimCruisePlaceholderVehicle::EngineTorqueNm(double Rpm) const
{
	const TArray<FSimCruiseTorqueCurvePoint>& Curve = Car.EngineTorqueCurve;
	if (Curve.IsEmpty())
	{
		return 0.0;
	}
	if (Rpm <= Curve[0].EngineSpeedRpm)
	{
		return Curve[0].EngineTorqueNm;
	}
	for (int32 I = 1; I < Curve.Num(); ++I)
	{
		if (Rpm <= Curve[I].EngineSpeedRpm)
		{
			const double T = (Rpm - Curve[I - 1].EngineSpeedRpm) / FMath::Max(Curve[I].EngineSpeedRpm - Curve[I - 1].EngineSpeedRpm, 1.0);
			return FMath::Lerp(Curve[I - 1].EngineTorqueNm, Curve[I].EngineTorqueNm, T);
		}
	}
	return Car.TorquePastCurveNm;
}

double ASimCruisePlaceholderVehicle::ComputeEngineRpm(double Drive) const
{
	const double Ratio = bReverse ? Car.ReverseGearRatio : (Car.ForwardGearRatios.IsValidIndex(Gear - 1) ? Car.ForwardGearRatios[Gear - 1] : 0.0);
	const bool bCoupled = Ratio > 0.0 && !(bManual && Command.Clutch > 0.5f) && Gear != 0;
	const double Free = Car.IdleSpeedRpm + Drive * 3500.0;
	if (!bCoupled)
	{
		return FMath::Min(Free, Car.FuelCutSpeedRpm);
	}
	const double Wheel = FMath::Abs(Vx) / FMath::Max(Car.TireFreeRadiusM(), 0.1) * Ratio * Car.FinalDriveRatio * 60.0 / UE_DOUBLE_TWO_PI;
	// Launch: a slipping clutch (manual) or converter (automatic) holds the
	// engine near the definition's launch speed until the road catches up.
	const double Launch = Car.LaunchSpeedRpm + Car.LaunchSpeedRpmPerThrottleFraction * Drive;
	return FMath::Clamp(FMath::Max(Wheel, Drive > 0.02 ? FMath::Min(Launch, Wheel + Launch) : Car.IdleSpeedRpm), Car.IdleSpeedRpm, Car.MaximumGearSpeedRpm);
}

void ASimCruisePlaceholderVehicle::StepPhysics(double H)
{
	const double M = Car.MassKg;
	const double L = Car.WheelbaseM;
	const double Fw = Car.FrontWeightFraction;
	const double A = L * (1.0 - Fw);
	const double B = L * Fw;
	const double Izz = FMath::Max(Car.InertiaKgM2.Y, 100.0);
	const double Rw = FMath::Max(Car.TireFreeRadiusM(), 0.1);
	const double Mu = Car.PeakLongitudinalFrictionCoefficient;
	const double MuLat = Mu * Car.LateralGripScale;
	const double Delta = FMath::Clamp(Command.RackAngleRad, -FMath::DegreesToRadians(Car.ParkingRackAngleDeg), FMath::DegreesToRadians(Car.ParkingRackAngleDeg));

	// Pedals, as Godot's Powertrain.drive reads them.
	double Throttle = 0.0;
	double BrakeAmount = 0.0;
	if (bManual)
	{
		Throttle = Command.Throttle;
		bServiceBrake = Command.bFullServiceBrake || Command.Brake > 0.01f;
		BrakeAmount = FMath::Max<double>(Command.Brake, Command.bFullServiceBrake ? 1.0 : 0.0);
		bReverse = Gear == -1;
	}
	else
	{
		bServiceBrake = Command.bFullServiceBrake || (Command.Brake > 0.01f && Vx > 0.45) || (Command.Throttle > 0.01f && Vx < -0.45);
		if (!bServiceBrake)
		{
			if (Command.Throttle > 0.f && Vx > -0.45) { bReverse = false; Throttle = Command.Throttle; }
			else if (Command.Brake > 0.f && Vx < 0.45) { bReverse = true; Throttle = Command.Brake; }
		}
		BrakeAmount = Command.bFullServiceBrake ? 1.0 : (bServiceBrake ? FMath::Max(Command.Brake, Command.Throttle) : 0.0);
		Gear = bReverse ? -1 : FMath::Max(Gear, 1);
	}
	DriveFraction = Throttle;

	// Automatic gear selection on engine speed.
	ShiftTimerS = FMath::Max(0.0, ShiftTimerS - H);
	if (!bManual && !bReverse && ShiftTimerS <= 0.0)
	{
		if (EngineRpm > Car.AutomaticUpshiftSpeedRpm && Gear < Car.ForwardGearRatios.Num()) { ++Gear; ShiftTimerS = Car.GearShiftDurationS; }
		else if (EngineRpm < Car.AutomaticDownshiftSpeedRpm && Gear > 1) { --Gear; ShiftTimerS = Car.GearShiftDurationS; }
	}
	EngineRpm = ComputeEngineRpm(Throttle);

	const double Ratio = bReverse ? Car.ReverseGearRatio : (Car.ForwardGearRatios.IsValidIndex(Gear - 1) ? Car.ForwardGearRatios[Gear - 1] : 0.0);
	const bool bCoupled = Ratio > 0.0 && !(bManual && Command.Clutch > 0.5f) && ShiftTimerS <= 0.0;
	double EngineTorque = 0.0;
	double EngineBrakeTorque = 0.0;
	if (bCoupled)
	{
		const double Friction = Car.EngineFrictionTorqueNm + Car.EngineFrictionTorqueNmPerRpm * EngineRpm;
		const bool bFuelCut = EngineRpm >= Car.FuelCutSpeedRpm;
		EngineTorque = bFuelCut ? 0.0 : Throttle * EngineTorqueNm(EngineRpm);
		// Engine braking opposes the wheels' rotation, whichever way they turn.
		EngineBrakeTorque = bFuelCut ? Friction : (1.0 - Throttle) * Friction;
		if (FMath::Abs(Vx) < 1.0)
		{
			EngineBrakeTorque = 0.0; // idle creep and stalling are not modelled
		}
	}
	const double Gearing = Ratio * Car.FinalDriveRatio / Rw;
	const double DriveForce = EngineTorque * Gearing * Car.DrivelineEfficiencyFraction * (bReverse ? -1.0 : 1.0)
		- FMath::Sign(Vx) * EngineBrakeTorque * Gearing;
	const bool bFrontDriven = Car.IsDrivenWheel(0);
	const bool bRearDriven = Car.IsDrivenWheel(2);
	const double FrontShare = bFrontDriven && bRearDriven ? Car.AwdFrontTorqueShareFraction : (bFrontDriven ? 1.0 : 0.0);

	// Static load plus longitudinal transfer from the last step.
	AxleLoadN[0] = FMath::Max(0.0, M * G * Fw - M * LongitudinalAccel * Car.CenterOfGravityHeightM / L);
	AxleLoadN[1] = FMath::Max(0.0, M * G * (1.0 - Fw) + M * LongitudinalAccel * Car.CenterOfGravityHeightM / L);

	const double BrakeFront = BrakeAmount * 2.0 * Car.FrontBrakeTorqueNm / Rw;
	const double BrakeRear = BrakeAmount * 2.0 * Car.RearBrakeTorqueNm / Rw + Command.ParkingBrake * 2.0 * Car.ParkingBrakeTorqueNm / Rw;

	double AxleFx[2];
	double AxleFy[2];
	for (int32 Axle = 0; Axle < 2; ++Axle)
	{
		const double Steer = Axle == 0 ? Delta : 0.0;
		const double LateralAtAxle = Vy + (Axle == 0 ? A : -B) * YawRate;
		const double WheelLong = FMath::Cos(Steer) * Vx + FMath::Sin(Steer) * LateralAtAxle;
		const double WheelLat = -FMath::Sin(Steer) * Vx + FMath::Cos(Steer) * LateralAtAxle;
		const double TargetAlpha = FMath::Atan2(WheelLat, FMath::Max(FMath::Abs(WheelLong), 0.5));
		const double Travel = FMath::Max(FMath::Abs(WheelLong), 0.3) * H / FMath::Max(Car.TireRelaxationLengthM, 0.05);
		AxleSlipAngle[Axle] += (TargetAlpha - AxleSlipAngle[Axle]) * (1.0 - FMath::Exp(-Travel));

		const double Fz = AxleLoadN[Axle];
		const double Capacity = Mu * Fz;
		const double Drive = DriveForce * (Axle == 0 ? FrontShare : 1.0 - FrontShare);
		const double BrakeForce = Axle == 0 ? BrakeFront : BrakeRear;
		const double Moving = FMath::Abs(WheelLong) > 0.2 ? FMath::Sign(WheelLong) : 0.0;
		double BrakeDemand = BrakeForce * Moving;
		bAxleAbs[Axle] = false;
		if (Car.bAbsEnabled && BrakeForce > 0.0 && FMath::Abs(Drive - BrakeDemand) > 0.95 * Capacity && Moving != 0.0)
		{
			BrakeDemand = FMath::Clamp(BrakeDemand, -0.95 * Capacity, 0.95 * Capacity);
			bAxleAbs[Axle] = true;
		}
		double Fx = Drive - BrakeDemand;
		const double Use = Capacity > 1.0 ? FMath::Abs(Fx) / Capacity : 0.0;
		double LateralShare = 1.0;
		if (Use > 1.0)
		{
			// Locked or spinning: the patch slides, at a little less than peak friction.
			const bool bLocked = BrakeForce > 0.0 && FMath::Abs(BrakeDemand) > FMath::Abs(Drive);
			Fx = FMath::Sign(Fx) * Capacity * 0.9;
			AxleSlipRatio[Axle] = bLocked ? -1.0 * FMath::Sign(WheelLong) : FMath::Sign(Fx) * FMath::Min(1.0, Car.PeakSlipRatio * (1.5 + 3.0 * (Use - 1.0)));
			LateralShare = bLocked ? 0.25 : 0.45;
		}
		else
		{
			AxleSlipRatio[Axle] = FMath::Sign(Fx) * Car.PeakSlipRatio * FMath::Pow(Use, 1.5);
			LateralShare = FMath::Sqrt(FMath::Max(0.0, 1.0 - Use * Use));
		}
		const double Fy = -MuLat * Fz * LateralLaw(Car, AxleSlipAngle[Axle]) * LateralShare;
		// Into body axes.
		AxleFx[Axle] = Fx * FMath::Cos(Steer) - Fy * FMath::Sin(Steer);
		AxleFy[Axle] = Fx * FMath::Sin(Steer) + Fy * FMath::Cos(Steer);
	}

	const double Speed = FMath::Sqrt(Vx * Vx + Vy * Vy);
	const double Drag = 0.5 * AirDensity * Car.DragAreaM2 * Speed * Vx;
	const double Rolling = Car.RollingResistanceCoefficient * M * G * FMath::Clamp(Vx / 0.3, -1.0, 1.0);
	const double Fx = AxleFx[0] + AxleFx[1] - Drag - Rolling - M * G * FMath::Sin(GroundPitchRad);
	const double Fy = AxleFy[0] + AxleFy[1];
	const double Mz = A * AxleFy[0] - B * AxleFy[1];

	LongitudinalAccel = Fx / M;
	LateralAccel = Fy / M;
	Vx += (LongitudinalAccel + Vy * YawRate) * H;
	Vy += (LateralAccel - Vx * YawRate) * H;
	YawRate += Mz / Izz * H;

	// Near standstill a planar slip model has no meaning: blend to rolling
	// kinematics and let held brakes stop the car.
	const double Kinematic = Vx * FMath::Tan(Delta) / L;
	const double Blend = FMath::SmoothStep(0.5, 2.5, Speed);
	YawRate = FMath::Lerp(Kinematic, YawRate, Blend);
	Vy *= FMath::Lerp(FMath::Exp(-H * 6.0), 1.0, Blend);
	if (Speed < 0.25 && (BrakeAmount > 0.05 || Command.ParkingBrake > 0.05f) && FMath::Abs(DriveForce) < (BrakeFront + BrakeRear))
	{
		Vx = 0.0;
		Vy = 0.0;
		YawRate = 0.0;
	}

	YawRad += YawRate * H;
	const double CosYaw = FMath::Cos(YawRad);
	const double SinYaw = FMath::Sin(YawRad);
	PlanarLocationCm.X += (Vx * CosYaw - Vy * SinYaw) * H * 100.0;
	PlanarLocationCm.Y += (Vx * SinYaw + Vy * CosYaw) * H * 100.0;
}

void ASimCruisePlaceholderVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	PhysicsAccumulatorS = FMath::Min(PhysicsAccumulatorS + DeltaSeconds, 0.1);
	while (PhysicsAccumulatorS >= PhysicsStepS)
	{
		StepPhysics(PhysicsStepS);
		PhysicsAccumulatorS -= PhysicsStepS;
	}
	FollowGround(false);
	UpdateVisuals(DeltaSeconds);
}

void ASimCruisePlaceholderVehicle::FollowGround(bool bSnap)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double L = Car.WheelbaseM * 100.0;
	const FRotator Heading(0.0, FMath::RadiansToDegrees(YawRad), 0.0);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruisePlaceholderGround), false, this);
	double Heights[4];
	int32 Hits = 0;
	const double Reference = bSnap ? PlanarLocationCm.Z + 5000.0 : GroundZ + 150.0;
	for (int32 I = 0; I < 4; ++I)
	{
		const double Track = (I < 2 ? Car.FrontTrackM : Car.RearTrackM) * 100.0;
		const FVector Offset = Heading.RotateVector(FVector((I < 2 ? 0.5 : -0.5) * L, (I % 2 == 0 ? -0.5 : 0.5) * Track, 0.0));
		const FVector XY(PlanarLocationCm.X + Offset.X, PlanarLocationCm.Y + Offset.Y, 0.0);
		FHitResult Hit;
		const FVector Start(XY.X, XY.Y, Reference);
		const FVector End(XY.X, XY.Y, Reference - (bSnap ? 20000.0 : 1500.0));
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			Heights[I] = Hit.ImpactPoint.Z;
			++Hits;
		}
		else
		{
			Heights[I] = GroundZ;
		}
	}
	bGrounded = Hits > 0;
	if (!bGrounded)
	{
		return;
	}
	const double Front = 0.5 * (Heights[0] + Heights[1]);
	const double Rear = 0.5 * (Heights[2] + Heights[3]);
	const double Left = 0.5 * (Heights[0] + Heights[2]);
	const double Right = 0.5 * (Heights[1] + Heights[3]);
	const double Mean = 0.25 * (Heights[0] + Heights[1] + Heights[2] + Heights[3]);
	GroundPitchRad = FMath::Atan2(Front - Rear, L);
	GroundRollRad = FMath::Atan2(Left - Right, 0.5 * (Car.FrontTrackM + Car.RearTrackM) * 100.0);
	// Each corner's departure from the fitted plane, as suspension travel.
	const double Plane[4] = {Mean + 0.5 * (Front - Rear) + 0.5 * (Left - Right), Mean + 0.5 * (Front - Rear) - 0.5 * (Left - Right),
		Mean - 0.5 * (Front - Rear) + 0.5 * (Left - Right), Mean - 0.5 * (Front - Rear) - 0.5 * (Left - Right)};
	for (int32 I = 0; I < 4; ++I)
	{
		WheelTravelM[I] = Car.StaticSuspensionLengthM + (Heights[I] - Plane[I]) / 100.0;
	}
	GroundZ = Mean;
	PlanarLocationCm.Z = Mean;
	SetActorLocationAndRotation(FVector(PlanarLocationCm.X, PlanarLocationCm.Y, Mean),
		FRotator(FMath::RadiansToDegrees(GroundPitchRad), FMath::RadiansToDegrees(YawRad), FMath::RadiansToDegrees(GroundRollRad)));
}

void ASimCruisePlaceholderVehicle::UpdateVisuals(double DeltaTimeS)
{
	const double Radius = FMath::Max(Car.TireFreeRadiusM(), 0.1);
	for (int32 I = 0; I < WheelMeshes.Num(); ++I)
	{
		const int32 Axle = I < 2 ? 0 : 1;
		const double RoadOmega = Vx / Radius;
		const double Omega = AxleSlipRatio[Axle] <= -0.99 ? 0.0 : RoadOmega * (1.0 + AxleSlipRatio[Axle]);
		WheelSpinRad[I] = FMath::Fmod(WheelSpinRad[I] + Omega * DeltaTimeS, UE_DOUBLE_TWO_PI);
		const double Steer = I < 2 ? Command.RackAngleRad : 0.0;
		const FQuat Rotation = FQuat(FVector::UpVector, Steer) * FQuat(FVector::RightVector, -WheelSpinRad[I]) * FQuat(FVector::ForwardVector, UE_DOUBLE_HALF_PI);
		WheelMeshes[I]->SetRelativeRotation(Rotation);
	}
}

FVector ASimCruisePlaceholderVehicle::GetVelocity() const
{
	const double CosYaw = FMath::Cos(YawRad);
	const double SinYaw = FMath::Sin(YawRad);
	return FVector(Vx * CosYaw - Vy * SinYaw, Vx * SinYaw + Vy * CosYaw, 0.0) * 100.0;
}

FSimCruiseVehicleSetup ASimCruisePlaceholderVehicle::GetVehicleSetup() const
{
	FSimCruiseVehicleSetup Setup;
	Setup.CarId = Car.Id;
	Setup.DisplayName = Car.Name;
	Setup.WheelbaseM = Car.WheelbaseM;
	Setup.CenterOfMassToFrontAxleM = Car.WheelbaseM * (1.0 - Car.FrontWeightFraction);
	Setup.ParkingRackAngleRad = FMath::DegreesToRadians(Car.ParkingRackAngleDeg);
	Setup.SteeringAssistLateralG = Car.SteeringAssistLateralG;
	Setup.SteeringSlipLimitSurplusRad = FMath::DegreesToRadians(Car.SteeringSlipLimitSurplusDeg);
	Setup.DriverEyeLocalCm = SimCruiseGodotToLocalCm(Car.DriverEyeM);
	Setup.IdleRpm = Car.IdleSpeedRpm;
	Setup.UpshiftRpm = Car.AutomaticUpshiftSpeedRpm;
	Setup.RedlineRpm = Car.FuelCutSpeedRpm;
	Setup.ForwardGearCount = Car.ForwardGearRatios.Num();
	Setup.SkidpadG = Car.TargetSkidpadG;
	const double SeventyMph = 70.0 * 0.44704;
	const double StopM = Car.TargetSeventyToZeroFeet * 0.3048;
	Setup.BrakingG = StopM > 1.0 ? SeventyMph * SeventyMph / (2.0 * StopM) / G : 0.9;
	return Setup;
}

void ASimCruisePlaceholderVehicle::GetVehicleTelemetry(FSimCruiseVehicleTelemetry& Out) const
{
	Out.BodyVelocityMps = FVector(Vx, Vy, 0.0);
	Out.BodyAngularVelocityRadPerS = FVector(0.0, 0.0, YawRate);
	Out.EngineSpeedRpm = EngineRpm;
	Out.Gear = bReverse ? -1 : Gear;
	Out.bManualTransmission = bManual;
	Out.bEngineStalled = false;
	Out.bElectric = Car.EngineKind == TEXT("electric");
	Out.bServiceBrakeOn = bServiceBrake;
	Out.RackAngleRad = Command.RackAngleRad;
	Out.EngineLoad = DriveFraction;
	Out.RoadRoughness = 0.0;
	const double Track = 0.5 * (Car.FrontTrackM + Car.RearTrackM);
	for (int32 I = 0; I < 4; ++I)
	{
		const int32 Axle = I < 2 ? 0 : 1;
		const double AxleShare = Axle == 0 ? Car.FrontWeightFraction : 1.0 - Car.FrontWeightFraction;
		const double Transfer = Car.MassKg * LateralAccel * Car.CenterOfGravityHeightM / FMath::Max(Track, 0.5) * AxleShare;
		FSimCruiseWheelTelemetry& Wheel = Out.Wheels[I];
		Wheel.bContact = bGrounded;
		Wheel.bDriven = Car.IsDrivenWheel(I);
		Wheel.bAbsActive = bAxleAbs[Axle];
		// Turning right (lateral acceleration positive) loads the left wheels.
		Wheel.NormalLoadN = FMath::Max(0.0, 0.5 * AxleLoadN[Axle] + (I % 2 == 0 ? 0.5 : -0.5) * Transfer);
		Wheel.SlipRatio = AxleSlipRatio[Axle];
		Wheel.PeakSlipRatio = Car.PeakSlipRatio;
		Wheel.SlipAngleRad = AxleSlipAngle[Axle];
		Wheel.PeakSlipAngleRad = PeakSlipAngleRad;
		Wheel.SuspensionTravelM = WheelTravelM[I];
		Wheel.SurfaceGrip = 1.0;
	}
}

void ASimCruisePlaceholderVehicle::ApplyDriverCommand(const FSimCruiseDriverCommand& InCommand)
{
	Command = InCommand;
}

FString ASimCruisePlaceholderVehicle::RequestGearChange(int32 Step)
{
	if (!bManual)
	{
		return TEXT("Automatic shifting · T (B) for manual");
	}
	if (Command.Clutch < 0.5f)
	{
		return TEXT("Clutch in to shift");
	}
	const int32 Next = FMath::Clamp(Gear + Step, -1, Car.ForwardGearRatios.Num());
	if (Next == -1 && Vx > 1.0)
	{
		return TEXT("Stop before selecting reverse");
	}
	Gear = Next;
	bReverse = Gear == -1;
	return FString();
}

void ASimCruisePlaceholderVehicle::SetManualTransmission(bool bInManual)
{
	bManual = bInManual;
	if (bManual)
	{
		Gear = bReverse ? -1 : 0;
	}
	else
	{
		Gear = FMath::Max(1, Gear);
	}
}

void ASimCruisePlaceholderVehicle::RequestEngineStart()
{
}

void ASimCruisePlaceholderVehicle::ResetVehicle(const FTransform& WorldTransform)
{
	Vx = Vy = YawRate = 0.0;
	LongitudinalAccel = LateralAccel = 0.0;
	AxleSlipAngle[0] = AxleSlipAngle[1] = 0.0;
	AxleSlipRatio[0] = AxleSlipRatio[1] = 0.0;
	YawRad = FMath::DegreesToRadians(WorldTransform.Rotator().Yaw);
	PlanarLocationCm = WorldTransform.GetLocation();
	GroundZ = PlanarLocationCm.Z;
	Gear = bManual ? 0 : 1;
	bReverse = false;
	EngineRpm = Car.IdleSpeedRpm;
	FollowGround(true);
}
