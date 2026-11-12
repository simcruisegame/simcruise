// Copyright SimCruise contributors. All Rights Reserved.

#include "Vehicle/SimCruiseTuning.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Vehicle/SimCruiseVehicleDynamics.h"

namespace
{
	double Axle(const TArray<double>& Values, int32 Index, double Fallback = 0.0)
	{
		return Values.IsValidIndex(Index) ? Values[Index] : Fallback;
	}

	constexpr double LbInToNm = 175.126835;
}

FSimCruiseTuning FSimCruiseTuning::FromCar(const FSimCruiseCarDefinition& Car, double StockSteeringRatio)
{
	FSimCruiseTuning T;
	// A car given as wheel rates (N/m) shows them as the equivalent coil at its lever ratio.
	for (int32 A = 0; A < 2; ++A)
	{
		double Coil = Axle(Car.CoilSpringRateLbfPerInByAxle, A);
		const double Wheel = Axle(Car.WheelSpringRateNPerMByAxle, A);
		if (Wheel > 0.0)
		{
			const double Lever = Axle(Car.SpringLeverRatioByAxle, A, 1.0);
			Coil = Wheel / LbInToNm / FMath::Max(Lever * Lever, 0.01);
		}
		(A == 0 ? T.FrontSpringLbIn : T.RearSpringLbIn) = Coil;
	}
	T.FinalDrive = Car.FinalDriveRatio;
	T.SteeringRatio = StockSteeringRatio;
	return T;
}

void FSimCruiseTuning::Clamp(const FSimCruiseTuning& Stock)
{
	FrontSpringLbIn = FMath::Clamp(FrontSpringLbIn, Stock.FrontSpringLbIn * 0.5, Stock.FrontSpringLbIn * 4.0);
	RearSpringLbIn = FMath::Clamp(RearSpringLbIn, Stock.RearSpringLbIn * 0.5, Stock.RearSpringLbIn * 4.0);
	DampingPct = FMath::Clamp(DampingPct, 50.0, 250.0);
	RideHeightMm = FMath::Clamp(RideHeightMm, -60.0, 30.0);
	FrontBarPct = FMath::Clamp(FrontBarPct, 0.0, 300.0);
	RearBarPct = FMath::Clamp(RearBarPct, 0.0, 300.0);
	TorquePct = FMath::Clamp(TorquePct, 50.0, 150.0);
	FinalDrive = FMath::Clamp(FinalDrive, Stock.FinalDrive * 0.75, Stock.FinalDrive * 1.25);
	SteeringRatio = Stock.SteeringRatio > 0.0 ? FMath::Clamp(SteeringRatio, Stock.SteeringRatio * 0.6, Stock.SteeringRatio * 1.4) : 0.0;
}

FSimCruiseCarDefinition FSimCruiseTuning::Apply(const FSimCruiseCarDefinition& Real, const FSimCruiseTuning& Stock) const
{
	FSimCruiseCarDefinition Car = Real;
	const double Springs[2] = { FrontSpringLbIn, RearSpringLbIn };
	const double StockSprings[2] = { Stock.FrontSpringLbIn, Stock.RearSpringLbIn };
	for (int32 A = 0; A < 2; ++A)
	{
		const double Scale = StockSprings[A] > 0.0 ? Springs[A] / StockSprings[A] : 1.0;
		if (Car.WheelSpringRateNPerMByAxle.IsValidIndex(A) && Car.WheelSpringRateNPerMByAxle[A] > 0.0)
		{
			Car.WheelSpringRateNPerMByAxle[A] *= Scale;
		}
		if (Car.CoilSpringRateLbfPerInByAxle.IsValidIndex(A))
		{
			Car.CoilSpringRateLbfPerInByAxle[A] = Springs[A];
		}
	}
	Car.DamperScale = DampingPct / 100.0;
	Car.AntiRollBarScaleByAxle = { FrontBarPct / 100.0, RearBarPct / 100.0 };
	Car.EngineTorqueScale = TorquePct / 100.0;
	Car.FinalDriveRatio = FinalDrive;
	// Ride height: the body, its collision and its centre of mass move together over the wheels.
	const double Offset = RideHeightMm / 1000.0;
	Car.SuspensionMountHeightM += Offset;
	Car.CollisionShapeCenterYM += Offset;
	Car.CenterOfGravityHeightM += Offset;
	return Car;
}

double FSimCruiseTuning::RollGradientDegPerG(const FSimCruiseCarDefinition& Car)
{
	// Per axle: the springs give k t^2 / 2 and the bar (its load moved across per metre of
	// difference in travel) K_bar t^2, in N m per radian of roll.
	double RollStiffness = 0.0;
	for (int32 A = 0; A < 2; ++A)
	{
		const double Track = A == 0 ? Car.FrontTrackM : Car.RearTrackM;
		const double Spring = SimCruiseDynamics::GetWheelSpringRateNPerM(Car, A);
		const double Bar = SimCruiseDynamics::GetAntiRollBarRateNPerM(Car, A);
		RollStiffness += Track * Track * (0.5 * Spring + Bar);
	}
	const double Weight = Car.MassKg * SimCruiseDynamics::GravityMps2;
	const double Arm = Car.CenterOfGravityHeightM;
	const double Net = RollStiffness - Weight * Arm;
	return Net > 1.0 ? FMath::RadiansToDegrees(Weight * Arm / Net) : 99.0;
}

FString FSimCruiseTuning::PathFor(const FString& CarId)
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SimCruise"), FString::Printf(TEXT("tuning-%s.json"), *CarId));
}

bool FSimCruiseTuning::Load(const FString& CarId)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PathFor(CarId)))
	{
		return false;
	}
	TSharedPtr<FJsonObject> Json;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	Json->TryGetNumberField(TEXT("front_spring_lb_in"), FrontSpringLbIn);
	Json->TryGetNumberField(TEXT("rear_spring_lb_in"), RearSpringLbIn);
	Json->TryGetNumberField(TEXT("damping_pct"), DampingPct);
	Json->TryGetNumberField(TEXT("ride_height_mm"), RideHeightMm);
	Json->TryGetNumberField(TEXT("front_bar_pct"), FrontBarPct);
	Json->TryGetNumberField(TEXT("rear_bar_pct"), RearBarPct);
	Json->TryGetNumberField(TEXT("torque_pct"), TorquePct);
	Json->TryGetNumberField(TEXT("final_drive"), FinalDrive);
	Json->TryGetNumberField(TEXT("steering_ratio"), SteeringRatio);
	return true;
}

bool FSimCruiseTuning::Save(const FString& CarId) const
{
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetNumberField(TEXT("front_spring_lb_in"), FrontSpringLbIn);
	Json->SetNumberField(TEXT("rear_spring_lb_in"), RearSpringLbIn);
	Json->SetNumberField(TEXT("damping_pct"), DampingPct);
	Json->SetNumberField(TEXT("ride_height_mm"), RideHeightMm);
	Json->SetNumberField(TEXT("front_bar_pct"), FrontBarPct);
	Json->SetNumberField(TEXT("rear_bar_pct"), RearBarPct);
	Json->SetNumberField(TEXT("torque_pct"), TorquePct);
	Json->SetNumberField(TEXT("final_drive"), FinalDrive);
	Json->SetNumberField(TEXT("steering_ratio"), SteeringRatio);
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	const FString Path = PathFor(CarId);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	// Written whole and moved into place, so a crash never leaves half a file.
	const FString Temp = Path + TEXT(".tmp");
	return FFileHelper::SaveStringToFile(Text, *Temp) && IFileManager::Get().Move(*Path, *Temp, true, true);
}
