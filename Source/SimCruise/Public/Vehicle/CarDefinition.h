// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** One published or estimated engine curve sample. */
struct FSimCruiseTorqueCurvePoint
{
	double EngineSpeedRpm = 0.0;
	double EngineTorqueNm = 0.0;
};

/** Damper dyno data in its source units: shaft speed in/s, force lbf. */
struct FSimCruiseDamperDynoPoint
{
	double ShaftSpeedInPerSecond = 0.0;
	double CompressionForceLbf = 0.0;
	double ReboundForceLbf = 0.0;
};

/**
 * Complete native representation of game/cars/*.json.
 *
 * Field names carry their physical units. The original JSON text and parsed
 * document remain attached so source notes, future fields, numeric spelling,
 * and calibration metadata are never lost when Unreal consumes the spec.
 * Values from Godot's CarSpec defaults are the fallback for omitted keys.
 */
struct FSimCruiseCarDefinition
{
	// Identity and presentation.
	FString Id = TEXT("nb1999");
	FString Name = TEXT("1999 Roadster Sport");
	FString Make;
	FString Body = TEXT("roadster");
	FString ModelScene;
	bool bModelRigFit = false;
	bool bModelRigFlip = false;
	int32 SeatCount = 2;
	FVector3d DriverEyeM = FVector3d(-0.347, 1.009, 0.503);

	// Sprung mass and rigid-body geometry.
	double MassKg = 1043.0;
	double FrontWeightFraction = 0.515;
	double CenterOfGravityHeightM = 0.435;
	FVector3d InertiaKgM2 = FVector3d(1130.0, 1260.0, 370.0);
	FVector3d CollisionBoxSizeM = FVector3d(1.56, 0.49, 3.65);
	double CollisionShapeCenterYM = 0.46;
	double AngularDampingPerSecond = 0.02;

	// Wheel and tire geometry.
	double WheelbaseM = 2.266;
	double FrontTrackM = 1.414;
	double RearTrackM = 1.44;
	double SuspensionMountHeightM = 0.51;
	double VisualHubDropM = 0.205;
	double TireSectionWidthM = 0.195;
	double TireAspectRatio = 0.50;
	double RimDiameterIn = 15.0;

	// Tire force and transient parameters.
	double PeakLongitudinalFrictionCoefficient = 0.963;
	double LateralGripScale = 1.105;
	double TireLoadSensitivityFraction = 0.095;
	double ReferenceCornerLoadN = 2557.0;
	TArray<double> AxleGripMultiplier = {1.0, 1.0};
	double MinimumFrictionCoefficient = 0.7;
	double MaximumFrictionCoefficient = 1.16;
	double SlipAngleStiffnessPerRad = 10.8;
	double SlipAngleFalloffFraction = 0.13;
	double SlipAngleFalloffStartRad = 0.18;
	double SlipAngleFalloffEndRad = 0.65;
	double CamberThrustCoefficientPerRad = 0.48;
	double LongitudinalSlipStiffness = 12.0;
	double TireRelaxationLengthM = 0.35;
	double PeakSlipRatio = 0.10;
	double SlipRatioFalloffFraction = 0.0;
	double RelaxationLoadExponent = 0.5;
	bool bAbsEnabled = false;
	double WheelRotationalInertiaKgM2 = 1.05;
	double RollingResistanceCoefficient = 0.012;

	// Steering geometry and driver assists.
	double ParkingRackAngleDeg = 36.6;
	double RoadReferenceSteerAngleDeg = 35.5;
	double AckermannBlendFraction = 0.49;
	double SteeringAssistLateralG = 0.95;
	double SteeringSlipLimitSurplusDeg = 5.0;

	// Brake torque at each wheel.
	double FrontBrakeTorqueNm = 1150.0;
	double RearBrakeTorqueNm = 650.0;
	double ParkingBrakeTorqueNm = 900.0;

	// Aerodynamic body drag, Cd multiplied by frontal area.
	double DragAreaM2 = 0.65;

	// Combustion engine and electric motor data.
	FString EngineKind = TEXT("combustion");
	TArray<FSimCruiseTorqueCurvePoint> EngineTorqueCurve = {
		{800.0, 94.0}, {1750.0, 127.0}, {3000.0, 146.0}, {4150.0, 151.0},
		{5000.0, 161.0}, {5900.0, 156.0}, {6500.0, 153.0}, {7000.0, 137.0}
	};
	double TorquePastCurveNm = 120.0;
	double IdleSpeedRpm = 850.0;
	double RestartEngineSpeedRadPerSecond = 89.0;
	double GovernorSpeedRpm = 875.0;
	double FuelCutSpeedRpm = 7040.0;
	double MaximumGearSpeedRpm = 7200.0;
	double EngineRotationalInertiaKgM2 = 0.18;
	double EngineFrictionTorqueNm = 10.0;
	double EngineFrictionTorqueNmPerRpm = 0.002;
	double MinimumAutomaticEngineSpeedRadPerSecond = 78.0;
	double MaximumEngineSpeedRadPerSecond = 750.0;
	double StallEngineSpeedRadPerSecond = 35.0;
	double ElectricMotorPeakTorqueNm = 0.0;
	double ElectricMotorPeakPowerW = 0.0;
	double ElectricMotorMaximumSpeedRpm = 0.0;
	double ElectricRegenerativeTorqueNm = 0.0;

	// Clutch, gearbox, differential and driven-axle selection.
	TArray<double> ForwardGearRatios = {3.136, 1.888, 1.330, 1.0, 0.814};
	double ReverseGearRatio = 3.758;
	double FinalDriveRatio = 4.30;
	double DrivelineEfficiencyFraction = 0.895;
	double GearShiftDurationS = 0.21;
	double AutomaticUpshiftSpeedRpm = 6700.0;
	double AutomaticUpshiftMinimumEngineSpeedRadPerSecond = 590.0;
	double AutomaticDownshiftSpeedRpm = 2100.0;
	double ClutchCouplingNmPerRadPerSecond = 12.0;
	double ClutchCapacityNm = 210.0;
	double ClutchBackTorqueNm = 200.0;
	double LaunchSpeedRpm = 1000.0;
	double LaunchSpeedRpmPerThrottleFraction = 2250.0;
	double ManualClutchCouplingNmPerRadPerSecond = 10.0;
	FString TransmissionKind = TEXT("manual");
	FString DrivetrainLayout = TEXT("rwd");
	double AwdFrontTorqueShareFraction = 0.5;
	double FrontDifferentialBiasRatio = 1.0;
	double RearDifferentialBiasRatio = 2.0;
	double CenterDifferentialBiasRatio = 1.0;
	bool bTractionControlEnabled = false;
	double TractionControlTargetSlipRatio = 0.0;
	double TractionControlIntegralGainPerSecond = 3.0;

	// Springs and anti-roll bars, indexed [front, rear].
	TArray<double> CoilSpringRateLbfPerInByAxle = {168.0, 130.0};
	TArray<double> SpringLeverRatioByAxle = {0.68, 0.77};
	TArray<double> WheelSpringRateNPerMByAxle = {0.0, 0.0};
	TArray<double> AntiRollBarDiameterMByAxle = {0.022, 0.011};
	TArray<double> AntiRollBarTorsionLengthMByAxle = {0.822, 0.813};
	TArray<double> AntiRollBarArmLengthMByAxle = {0.216, 0.122};
	TArray<double> AntiRollBarLeverRatioByAxle = {0.548, 0.586};
	TArray<double> AntiRollBarRateNmPerMByAxle = {0.0, 0.0};
	double StaticSuspensionLengthM = 0.217;
	double MinimumSuspensionTravelM = 0.074;
	double MaximumSuspensionTravelM = 0.315;
	double BumpStopGapM = 0.054;
	double BumpStopStiffnessNPerM2 = 800000.0;
	double MaximumTireContactLoadN = 14200.0;
	TArray<double> StaticCamberDegByAxle = {-0.53, -1.10};
	TArray<double> CamberGainDegPerMByAxle = {23.0, 25.0};
	double CasterAngleDeg = 6.0;
	double ToeAngleDegPerWheel = 0.15;
	double UnsprungMassKgPerCorner = 23.0;
	TArray<FString> DamperModeByAxle = {TEXT("ratio"), TEXT("dyno")};
	TArray<double> DamperReboundCriticalFractionByAxle = {0.56, 0.56};
	TArray<double> DamperCompressionCriticalFractionByAxle = {0.33, 0.33};
	TArray<double> DamperKneeSpeedMPerSecondByAxle = {0.12, 0.12};
	TArray<double> DamperKneeSlopeByAxle = {0.48, 0.48};
	TArray<TArray<FSimCruiseDamperDynoPoint>> DamperDynoByAxle = {
		{},
		{{0.0, 0.0, 0.0}, {1.0, 42.0, 73.0}, {1.1, 45.0, 80.0},
		 {2.0, 56.0, 127.0}, {3.0, 66.0, 146.0}, {4.0, 72.0, 160.0},
		 {6.0, 87.0, 183.0}, {8.0, 100.0, 206.0}, {9.98, 112.0, 224.0}}
	};

	// Published/calibration targets retain their original units.
	double TargetZeroToSixtySeconds = 7.97;
	double TargetSeventyToZeroFeet = 173.0;
	double TargetSkidpadG = 0.89;

	// Source evidence is retained as a nested object and as the original text.
	FString CalibrationOpen;
	FString SourcesEstimates;
	TSharedPtr<FJsonObject> SourceAnnotations;
	TSharedPtr<FJsonObject> ModelRigOptions;
	TSharedPtr<FJsonObject> OriginalDocument;
	FString OriginalJson;

	double TireFreeRadiusM() const;
	bool IsDrivenWheel(int32 WheelIndex) const;
};
