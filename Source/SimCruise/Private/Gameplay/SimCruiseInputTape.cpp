// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseInputTape.h"

#include "Engine/World.h"
#include "Gameplay/SimCruiseDriveTest.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "Gameplay/SimCruisePlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Vehicle/SimCruiseUnits.h"
#include "Vehicle/SimCruiseVehicle.h"
#include "Vehicle/SimCruiseVehicleMovementComponent.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseWorldStreamer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseInputTape, Log, All);

namespace
{
	const TCHAR* TapeMagic = TEXT("# simcruise input tape 1");
	const TCHAR* TapeColumns = TEXT("t,dt,pad_steer,key_steer,pad_throttle,pad_brake,key_throttle,key_brake,full_brake,handbrake,clutch,"
		"wheel,wheel_rack_rad,wheel_throttle,wheel_brake,wheel_has_clutch,wheel_clutch,shift,manual");
	constexpr int32 TapeColumnCount = 19;

	FString DriveTestDir()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DriveTest"));
	}

	/** Where the car's body stands on the ground: the ground point under it. */
	FVector GroundUnder(const ASimCruiseVehicle& Vehicle)
	{
		const double BoxCentreCm = SimCruiseUnits::ToUnrealLength(Vehicle.GetVehicleMovement()->GetCarDefinition().CollisionShapeCenterYM);
		return Vehicle.GetActorLocation() - Vehicle.GetActorQuat().RotateVector(FVector(0.0, 0.0, BoxCentreCm));
	}
}

// --- FSimCruiseInputTape -----------------------------------------------------

const FSimCruiseInputFrame* FSimCruiseInputTape::At(double TimeS, int32& Cursor) const
{
	if (Frames.IsEmpty() || TimeS > Frames.Last().TimeS + 1e-9)
	{
		return nullptr;
	}
	if (!Frames.IsValidIndex(Cursor) || (Cursor > 0 && Frames[Cursor - 1].TimeS >= TimeS))
	{
		Cursor = 0;
	}
	while (Cursor < Frames.Num() - 1 && Frames[Cursor].TimeS < TimeS - 1e-9)
	{
		++Cursor;
	}
	return &Frames[Cursor];
}

FString FSimCruiseInputTape::ToCsv() const
{
	TArray<FString> Lines;
	Lines.Add(TapeMagic);
	Lines.Add(FString::Printf(TEXT("# car=%s"), *CarId));
	Lines.Add(FString::Printf(TEXT("# ground_cm=%.2f,%.2f,%.2f"), StartGroundCm.X, StartGroundCm.Y, StartGroundCm.Z));
	Lines.Add(FString::Printf(TEXT("# yaw_deg=%.4f"), StartYawDeg));
	if (bHasLonLat)
	{
		Lines.Add(FString::Printf(TEXT("# lonlat=%.8f,%.8f"), StartLonDeg, StartLatDeg));
	}
	Lines.Add(FString::Printf(TEXT("# start_speed_mps=%.3f"), StartSpeedMps));
	if (bHasPlace)
	{
		// The place line holds numbers only; the title and street have lines of their own (they hold commas).
		Lines.Add(FString::Printf(TEXT("# place=%.8f,%.8f"), PlaceLonDeg, PlaceLatDeg));
		Lines.Add(FString::Printf(TEXT("# place_title=%s"), *PlaceTitle.Replace(TEXT("\n"), TEXT(" "))));
		Lines.Add(FString::Printf(TEXT("# place_street=%s"), *PlaceStreet.Replace(TEXT("\n"), TEXT(" "))));
		Lines.Add(FString::Printf(TEXT("# frame=%d,%d,%.6f"), FrameOriginTile.X, FrameOriginTile.Y, FrameTileSizeM));
	}
	if (!Note.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("# note=%s"), *Note.Replace(TEXT("\n"), TEXT(" "))));
	}
	Lines.Add(TapeColumns);
	for (const FSimCruiseInputFrame& F : Frames)
	{
		const FSimCruiseRawControls& R = F.Raw;
		Lines.Add(FString::Printf(TEXT("%.6f,%.6f,%.5f,%.0f,%.5f,%.5f,%d,%d,%d,%d,%d,%d,%.6f,%.5f,%.5f,%d,%.5f,%d,%d"),
			F.TimeS, F.DeltaS, R.PadSteer, R.KeySteer, R.PadThrottle, R.PadBrake, R.bKeyThrottle ? 1 : 0, R.bKeyBrake ? 1 : 0,
			R.bFullServiceBrake ? 1 : 0, R.bParkingBrake ? 1 : 0, R.bClutch ? 1 : 0, R.bWheel ? 1 : 0, R.WheelRackRad, R.WheelThrottle,
			R.WheelBrake, R.bWheelClutch ? 1 : 0, R.WheelClutch, F.Shift, F.bManual ? 1 : 0));
	}
	return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
}

bool FSimCruiseInputTape::FromCsv(const FString& Text, FSimCruiseInputTape& Out, FString& OutError)
{
	Out = FSimCruiseInputTape();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, true);
	if (Lines.IsEmpty() || !Lines[0].TrimStartAndEnd().Equals(TapeMagic))
	{
		OutError = TEXT("not a SimCruise input tape (first line)");
		return false;
	}
	bool bColumns = false;
	for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
	{
		const FString Line = Lines[LineIndex].TrimStartAndEnd();
		if (Line.StartsWith(TEXT("#")))
		{
			FString Key, Value;
			if (Line.RightChop(1).TrimStart().Split(TEXT("="), &Key, &Value))
			{
				TArray<FString> Parts;
				Value.ParseIntoArray(Parts, TEXT(","));
				if (Key == TEXT("car")) { Out.CarId = Value; }
				else if (Key == TEXT("ground_cm") && Parts.Num() == 3) { Out.StartGroundCm = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2])); }
				else if (Key == TEXT("yaw_deg")) { Out.StartYawDeg = FCString::Atod(*Value); }
				else if (Key == TEXT("lonlat") && Parts.Num() == 2) { Out.bHasLonLat = true; Out.StartLonDeg = FCString::Atod(*Parts[0]); Out.StartLatDeg = FCString::Atod(*Parts[1]); }
				else if (Key == TEXT("start_speed_mps")) { Out.StartSpeedMps = FCString::Atod(*Value); }
				else if (Key == TEXT("place") && Parts.Num() == 2) { Out.bHasPlace = true; Out.PlaceLonDeg = FCString::Atod(*Parts[0]); Out.PlaceLatDeg = FCString::Atod(*Parts[1]); }
				else if (Key == TEXT("place_title")) { Out.PlaceTitle = Value; }
				else if (Key == TEXT("place_street")) { Out.PlaceStreet = Value; }
				else if (Key == TEXT("frame") && Parts.Num() == 3) { Out.FrameOriginTile = FIntPoint(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1])); Out.FrameTileSizeM = FCString::Atod(*Parts[2]); }
				else if (Key == TEXT("note")) { Out.Note = Value; }
			}
			continue;
		}
		if (!bColumns)
		{
			if (Line != TapeColumns)
			{
				OutError = FString::Printf(TEXT("line %d: expected the column names %s"), LineIndex + 1, TapeColumns);
				return false;
			}
			bColumns = true;
			continue;
		}
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT(","), false);
		if (Cells.Num() != TapeColumnCount)
		{
			OutError = FString::Printf(TEXT("line %d: %d values, expected %d"), LineIndex + 1, Cells.Num(), TapeColumnCount);
			return false;
		}
		auto D = [&Cells](int32 I) { return FCString::Atod(*Cells[I]); };
		auto B = [&Cells](int32 I) { return FCString::Atoi(*Cells[I]) != 0; };
		FSimCruiseInputFrame F;
		F.TimeS = D(0);
		F.DeltaS = D(1);
		F.Raw.PadSteer = FMath::Clamp(D(2), -1.0, 1.0);
		F.Raw.KeySteer = FMath::Clamp(D(3), -1.0, 1.0);
		F.Raw.PadThrottle = FMath::Clamp(D(4), 0.0, 1.0);
		F.Raw.PadBrake = FMath::Clamp(D(5), 0.0, 1.0);
		F.Raw.bKeyThrottle = B(6);
		F.Raw.bKeyBrake = B(7);
		F.Raw.bFullServiceBrake = B(8);
		F.Raw.bParkingBrake = B(9);
		F.Raw.bClutch = B(10);
		F.Raw.bWheel = B(11);
		F.Raw.WheelRackRad = D(12);
		F.Raw.WheelThrottle = FMath::Clamp(D(13), 0.0, 1.0);
		F.Raw.WheelBrake = FMath::Clamp(D(14), 0.0, 1.0);
		F.Raw.bWheelClutch = B(15);
		F.Raw.WheelClutch = FMath::Clamp(D(16), 0.0, 1.0);
		F.Shift = FCString::Atoi(*Cells[17]);
		F.bManual = B(18);
		if (Out.Frames.Num() && F.TimeS < Out.Frames.Last().TimeS)
		{
			OutError = FString::Printf(TEXT("line %d: time goes backwards"), LineIndex + 1);
			return false;
		}
		Out.Frames.Add(F);
	}
	if (Out.CarId.IsEmpty())
	{
		OutError = TEXT("no '# car=' line");
		return false;
	}
	if (Out.Frames.IsEmpty())
	{
		OutError = TEXT("no frames");
		return false;
	}
	return true;
}

bool FSimCruiseInputTape::Save(const FString& Path) const
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveStringToFile(ToCsv(), *Path);
}

bool FSimCruiseInputTape::Load(const FString& Path, FSimCruiseInputTape& Out, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("%s: cannot be read"), *Path);
		return false;
	}
	if (!FromCsv(Text, Out, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
		return false;
	}
	return true;
}

// --- FSimCruiseReplayStats ---------------------------------------------------

void FSimCruiseReplayStats::Add(double TimeS, const FVector& PositionM, const FVector& VelocityLocalMps, double YawRateRadS, double RearAxleM, int32 Gear)
{
	if (!bHasStart)
	{
		bHasStart = true;
		Start = PositionM;
		Last = PositionM;
		LastTimeS = TimeS;
	}
	const FVector Rel = PositionM - Start;
	DistanceM += FVector2D(PositionM - Last).Size();
	Last = PositionM;
	const double Vx = VelocityLocalMps.X;
	const double Vy = VelocityLocalMps.Y;
	const double Speed = FVector2D(Vx, Vy).Size();
	SpeedTimeSum += Speed * FMath::Max(TimeS - LastTimeS, 0.0);
	LastTimeS = TimeS;
	DurationS = TimeS;
	// Body slip above 3 m/s (near a stop it is noise) and the rear axle's slip angle as the drive robot measures it (above 5 m/s
	// for the rear axle: at walking pace geometry alone makes it large).
	const double BetaDeg = Vx > 3.0 ? FMath::RadiansToDegrees(FMath::Atan2(Vy, Vx)) : 0.0;
	const double RearDeg = Vx > 5.0 ? FMath::RadiansToDegrees(FMath::Atan2(Vy - YawRateRadS * RearAxleM, Vx)) : 0.0;
	const double YawDps = FMath::RadiansToDegrees(YawRateRadS);
	MaxSpeedKph = FMath::Max(MaxSpeedKph, Speed * 3.6);
	MaxYawDps = FMath::Max(MaxYawDps, FMath::Abs(YawDps));
	MaxBetaDeg = FMath::Max(MaxBetaDeg, FMath::Abs(BetaDeg));
	MaxRearSlipDeg = FMath::Max(MaxRearSlipDeg, FMath::Abs(RearDeg));
	if (LapTimeS < 0.0 && DistanceM > 200.0 && FVector2D(Rel).Size() < 15.0)
	{
		LapTimeS = TimeS;
	}
	Rows.Add(FString::Printf(TEXT("%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%d"),
		TimeS, Speed * 3.6, YawDps, BetaDeg, RearDeg, Rel.X, Rel.Y, Rel.Z, DistanceM, Gear));
}

FString FSimCruiseReplayStats::CsvHeader()
{
	return TEXT("t,speed_kph,yaw_rate_dps,beta_deg,rear_slip_deg,x_m,y_m,z_m,distance_m,gear");
}

FString FSimCruiseReplayStats::ToCsv() const
{
	return CsvHeader() + TEXT("\n") + FString::Join(Rows, TEXT("\n")) + TEXT("\n");
}

bool FSimCruiseReplayStats::SaveCsv(const FString& Path) const
{
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	return FFileHelper::SaveStringToFile(ToCsv(), *Path);
}

FString FSimCruiseReplayStats::Summary() const
{
	return FString::Printf(TEXT("seconds=%.2f distance_m=%.1f lap_time_s=%.2f max_kph=%.1f mean_kph=%.1f max_yaw_dps=%.1f max_beta_deg=%.1f max_rear_slip_deg=%.1f"),
		DurationS, DistanceM, LapTimeS, MaxSpeedKph, DurationS > 0.0 ? SpeedTimeSum / DurationS * 3.6 : 0.0, MaxYawDps, MaxBetaDeg, MaxRearSlipDeg);
}

// --- FSimCruiseInputTapeDriver -----------------------------------------------

void FSimCruiseInputTapeDriver::Begin(ASimCruisePlayerController& Player)
{
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Value(Cmd, TEXT("SimCruiseReplayInput="), TapePath))
	{
		FString Error;
		if (!FSimCruiseInputTape::Load(TapePath, Tape, Error))
		{
			UE_LOG(LogSimCruiseInputTape, Error, TEXT("REPLAY FAILED %s"), *Error);
			Stage = EStage::Done;
			Player.ConsoleCommand(TEXT("quit"));
			return;
		}
		FString Car;
		if (FParse::Value(Cmd, TEXT("SimCruiseReplayCar="), Car) && !Car.IsEmpty())
		{
			Tape.CarId = Car;
		}
		bPad = FParse::Param(Cmd, TEXT("SimCruiseReplayPad"));
		OutPath = FPaths::Combine(DriveTestDir(), FString::Printf(TEXT("replay-%s.csv"), *FPaths::GetBaseFilename(TapePath)));
		FParse::Value(Cmd, TEXT("SimCruiseReplayOut="), OutPath);
		Stage = EStage::WaitCar;
		StageStarted = FPlatformTime::Seconds();
		UE_LOG(LogSimCruiseInputTape, Display, TEXT("Replay %s: %s, %d frames, %.1f s -> %s"), *TapePath, *Tape.CarId, Tape.Frames.Num(), Tape.GetDuration(), *OutPath);
		return;
	}
	FString Path;
	if (FParse::Value(Cmd, TEXT("SimCruiseRecordInput="), Path) && !Path.IsEmpty())
	{
		RecordPath = Path;
		bRecordAtStart = true;
	}
	else if (FParse::Param(Cmd, TEXT("SimCruiseRecordInput")))
	{
		bRecordAtStart = true;
	}
}

void FSimCruiseInputTapeDriver::StartRecording(ASimCruisePlayerController& Player)
{
	const ASimCruiseVehicle* Vehicle = Cast<ASimCruiseVehicle>(Player.GetPawn());
	if (!Vehicle || !Vehicle->GetVehicleMovement())
	{
		return;
	}
	Recording = FSimCruiseInputTape();
	Recording.CarId = Vehicle->GetRealCar().Id;
	Recording.StartGroundCm = GroundUnder(*Vehicle);
	Recording.StartYawDeg = Vehicle->GetActorRotation().Yaw;
	Recording.StartSpeedMps = Player.GetTelemetry().BodyVelocityMps.X;
	Recording.Note = FString::Printf(TEXT("recorded %s"), *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	if (const ASimCruiseGameMode* Mode = Player.GetWorld()->GetAuthGameMode<ASimCruiseGameMode>())
	{
		Recording.bHasLonLat = Mode->WorldToLonLat(Recording.StartGroundCm, Recording.StartLonDeg, Recording.StartLatDeg);
		// Where the drive is, so that a replay travels there first.
		const FSimCruisePlace& Place = Mode->GetPlace();
		if (Mode->IsWorldDrive() && Place.IsValid())
		{
			Recording.bHasPlace = true;
			Recording.PlaceTitle = Place.Title;
			Recording.PlaceStreet = Place.Street;
			Recording.PlaceLonDeg = Place.Lon;
			Recording.PlaceLatDeg = Place.Lat;
			if (const ASimCruiseWorldStreamer* Stream = Mode->GetStreamer())
			{
				Recording.FrameOriginTile = Stream->GetFrame().OriginTile;
				Recording.FrameTileSizeM = Stream->GetFrame().TileSizeM;
			}
		}
	}
	RecordClock = 0.0;
	PendingShift = 0;
	bRecording = true;
	Player.ShowMessage(FMath::Abs(Recording.StartSpeedMps) > 0.5 ? TEXT("Recording input (F9 stops) · a replay starts at rest") : TEXT("Recording input (F9 stops)"), 3.f);
}

void FSimCruiseInputTapeDriver::StopRecording(ASimCruisePlayerController& Player)
{
	if (!bRecording)
	{
		return;
	}
	bRecording = false;
	const FString Path = !RecordPath.IsEmpty() ? RecordPath
		: FPaths::Combine(DriveTestDir(), FString::Printf(TEXT("input-%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
	RecordPath.Reset();
	const bool bSaved = Recording.Save(Path);
	UE_LOG(LogSimCruiseInputTape, Display, TEXT("Input recording: %d frames, %.1f s -> %s%s"), Recording.Frames.Num(), Recording.GetDuration(), *Path, bSaved ? TEXT("") : TEXT(" (NOT SAVED)"));
	Player.ShowMessage(bSaved ? FString::Printf(TEXT("Input saved: %s"), *FPaths::GetCleanFilename(Path)) : TEXT("Input recording could not be saved"), 4.f);
}

void FSimCruiseInputTapeDriver::End()
{
	if (bRecording)
	{
		bRecording = false;
		const FString Path = !RecordPath.IsEmpty() ? RecordPath
			: FPaths::Combine(DriveTestDir(), FString::Printf(TEXT("input-%s.csv"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
		Recording.Save(Path);
		UE_LOG(LogSimCruiseInputTape, Display, TEXT("Input recording at exit: %d frames -> %s"), Recording.Frames.Num(), *Path);
	}
}

void FSimCruiseInputTapeDriver::Process(ASimCruisePlayerController& Player, FSimCruiseRawControls& Raw, float DeltaSeconds)
{
	if (Stage != EStage::Off)
	{
		TickReplay(Player, Raw, DeltaSeconds);
		return;
	}
	const ASimCruiseGameMode* Mode = Player.GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	const bool bPlaced = Player.GetPawn() && (!Mode || Mode->IsDriveReady());
	if (bRecordAtStart && bPlaced)
	{
		bRecordAtStart = false;
		StartRecording(Player);
	}
	else if (Player.WasInputKeyJustPressed(EKeys::F9) && bPlaced)
	{
		if (bRecording)
		{
			StopRecording(Player);
			return;
		}
		StartRecording(Player);
	}
	if (!bRecording)
	{
		PendingShift = 0;
		return;
	}
	RecordClock += DeltaSeconds;
	FSimCruiseInputFrame& Frame = Recording.Frames.AddDefaulted_GetRef();
	Frame.TimeS = RecordClock;
	Frame.DeltaS = DeltaSeconds;
	Frame.Raw = Raw;
	Frame.Shift = PendingShift;
	Frame.bManual = Player.GetTelemetry().bManualTransmission;
	PendingShift = 0;
}

void FSimCruiseInputTapeDriver::TickReplay(ASimCruisePlayerController& Player, FSimCruiseRawControls& Raw, float DeltaSeconds)
{
	// The tape drives; nothing from the pad reaches the car.
	Raw = FSimCruiseRawControls();
	PendingShift = 0;
	UWorld* World = Player.GetWorld();
	ASimCruiseGameMode* Mode = World->GetAuthGameMode<ASimCruiseGameMode>();
	ASimCruiseVehicle* Vehicle = Cast<ASimCruiseVehicle>(Player.GetPawn());
	const double Now = FPlatformTime::Seconds();
	auto Next = [this, Now](EStage NewStage) { Stage = NewStage; StageStarted = Now; StageFrames = 0; };
	++StageFrames;
	switch (Stage)
	{
	case EStage::WaitCar:
		// As the drive robot waits: the car placed, the tiles round it built, no shaders compiling.
		if (Vehicle && SimCruiseDriveRobot::IsReadyToDrive(World, Vehicle) && StageFrames > 30)
		{
			// The tape's place first: a drive recorded at another place travels there, waits for its
			// tiles, then the car goes to the recorded start.
			if (!bPad && !bTravelled && Mode)
			{
				bTravelled = true;
				if (!Tape.bHasPlace)
				{
					UE_LOG(LogSimCruiseInputTape, Warning, TEXT("Replay: the tape has no place (an older recording); it replays at the game's start, %s"), *Mode->GetPlace().Title);
				}
				else
				{
					const double ToLon = Tape.bHasLonLat ? Tape.StartLonDeg : Tape.PlaceLonDeg;
					const double ToLat = Tape.bHasLonLat ? Tape.StartLatDeg : Tape.PlaceLatDeg;
					double HereLon = 0.0, HereLat = 0.0;
					const bool bHere = Mode->WorldToLonLat(Vehicle->GetActorLocation(), HereLon, HereLat);
					// About 111 km a degree.
					const double Km = bHere ? FMath::Sqrt(FMath::Square((HereLat - ToLat) * 111.0) + FMath::Square((HereLon - ToLon) * 111.0 * FMath::Cos(FMath::DegreesToRadians(ToLat)))) : 1.0e9;
					if (Km > 2.0)
					{
						// To the recorded start itself (its nearest road); the place's own street may be elsewhere.
						const FSimCruisePlace Target = FSimCruisePlace::Make(ToLon, ToLat, Tape.PlaceTitle.IsEmpty() ? FString(TEXT("Recorded start")) : Tape.PlaceTitle,
							Tape.bHasLonLat ? FString() : Tape.PlaceStreet);
						UE_LOG(LogSimCruiseInputTape, Display, TEXT("Replay: travelling to the tape's place %s (%.6f, %.6f), %.0f km from here"), *Target.Title, ToLat, ToLon, bHere ? Km : -1.0);
						Mode->TravelTo(Target);
						Next(EStage::Travel);
						break;
					}
				}
			}
			Next(EStage::Place);
		}
		else if (Now - StageStarted > 400.0)
		{
			UE_LOG(LogSimCruiseInputTape, Error, TEXT("REPLAY FAILED the car was not ready after 400 s"));
			Stage = EStage::Done;
			Player.ConsoleCommand(TEXT("quit"));
		}
		break;
	case EStage::Travel:
		// The new place's tiles are built and the car stands on its road.
		if (Vehicle && StageFrames > 30 && (!Mode || Mode->IsDriveReady()) && SimCruiseDriveRobot::IsReadyToDrive(World, Vehicle))
		{
			UE_LOG(LogSimCruiseInputTape, Display, TEXT("Replay: arrived at %s after %.0f s"), Mode ? *Mode->GetPlace().Title : TEXT("-"), Now - StageStarted);
			Next(EStage::Place);
		}
		else if (Now - StageStarted > 400.0)
		{
			UE_LOG(LogSimCruiseInputTape, Error, TEXT("REPLAY FAILED the tape's place did not load in 400 s"));
			Stage = EStage::Done;
			Player.ConsoleCommand(TEXT("quit"));
		}
		break;
	case EStage::Place:
	{
		if (!Vehicle)
		{
			break;
		}
		if (Vehicle->GetRealCar().Id != Tape.CarId)
		{
			if (Mode && StageFrames == 1)
			{
				Mode->RespawnCar(&Player, Tape.CarId);
			}
			else if (StageFrames > 120)
			{
				UE_LOG(LogSimCruiseInputTape, Error, TEXT("REPLAY FAILED could not drive %s (the car is %s)"), *Tape.CarId, *Vehicle->GetRealCar().Id);
				Stage = EStage::Done;
				Player.ConsoleCommand(TEXT("quit"));
			}
			break;
		}
		if (bPad)
		{
			SimCruiseTestPad::Spawn(World, Vehicle);
		}
		else
		{
			FVector Ground = Tape.StartGroundCm;
			const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
			if (Tape.bHasLonLat && Stream && Stream->GetFrame().IsValid())
			{
				Ground = Stream->GetFrame().LonLatToWorldCm(Tape.StartLonDeg, Tape.StartLatDeg);
				double Z = 0.0;
				Ground.Z = Stream->SurfaceHeightAt(Ground, Z) ? Z : Tape.StartGroundCm.Z;
			}
			const double AwayM = FVector::Dist2D(Ground, Vehicle->GetActorLocation()) / 100.0;
			if (AwayM < 500.0 && (!Stream || Stream->AreTilesReadyAround(Ground, 1)))
			{
				Vehicle->ResetAt(Ground + FVector(0.0, 0.0, 5.0), static_cast<float>(Tape.StartYawDeg));
				double Lon = 0.0, Lat = 0.0;
				const bool bGeo = Mode && Mode->WorldToLonLat(Ground, Lon, Lat);
				UE_LOG(LogSimCruiseInputTape, Display, TEXT("REPLAY START place=\"%s\" lat=%.6f lon=%.6f yaw=%.1f (%.0f m from where the car stood)"),
					Mode ? *Mode->GetPlace().Title : TEXT("-"), bGeo ? Lat : 0.0, bGeo ? Lon : 0.0, Tape.StartYawDeg, AwayM);
			}
			else
			{
				UE_LOG(LogSimCruiseInputTape, Warning, TEXT("Replay: the recorded start is %.0f m away and not built; replaying from where the car stands"), AwayM);
			}
		}
		Next(EStage::Settle);
		break;
	}
	case EStage::Settle:
		// Hands off until the car has settled on its springs.
		if (StageFrames > 60 && Now - StageStarted > 2.0)
		{
			Next(EStage::Replay);
			Clock = 0.0;
			Cursor = 0;
			LastShiftFrame = -1;
			Stats = FSimCruiseReplayStats();
			UE_LOG(LogSimCruiseInputTape, Display, TEXT("Replay started: %s"), *Tape.CarId);
		}
		break;
	case EStage::Replay:
	{
		if (!Vehicle)
		{
			FinishReplay(Player);
			break;
		}
		Clock += DeltaSeconds;
		const FSimCruiseInputFrame* Frame = Tape.At(Clock, Cursor);
		if (!Frame)
		{
			FinishReplay(Player);
			break;
		}
		Raw = Frame->Raw;
		if (Frame->bManual != Player.GetTelemetry().bManualTransmission)
		{
			Vehicle->SetManualTransmission(Frame->bManual);
		}
		// Every gear change recorded up to this frame, in order (a slower replay skips none).
		for (int32 Index = LastShiftFrame + 1; Index <= Cursor; ++Index)
		{
			const int32 Shift = Tape.Frames[Index].Shift;
			for (int32 Step = 0; Step < FMath::Abs(Shift); ++Step)
			{
				Vehicle->RequestGearChange(Shift > 0 ? 1 : -1);
			}
		}
		LastShiftFrame = Cursor;
		const FSimCruiseDriveTelemetry& T = Player.GetTelemetry();
		const FSimCruiseVehicleSetup& Setup = Player.GetCachedVehicleSetup();
		Stats.Add(Clock, Vehicle->GetActorLocation() / 100.0, T.BodyVelocityMps, T.BodyAngularVelocityRadPerS.Z,
			Setup.WheelbaseM - Setup.CenterOfMassToFrontAxleM, T.Gear);
		break;
	}
	default:
		break;
	}
}

void FSimCruiseInputTapeDriver::FinishReplay(ASimCruisePlayerController& Player)
{
	Stage = EStage::Done;
	const bool bSaved = Stats.SaveCsv(OutPath);
	UE_LOG(LogSimCruiseInputTape, Display, TEXT("REPLAY car=%s tape=%s %s csv=%s%s"), *Tape.CarId, *FPaths::GetCleanFilename(TapePath), *Stats.Summary(),
		*OutPath, bSaved ? TEXT("") : TEXT(" (NOT SAVED)"));
	if (!FParse::Param(FCommandLine::Get(), TEXT("SimCruiseKeepOpen")))
	{
		Player.ConsoleCommand(TEXT("quit"));
	}
}
