// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/SimCruiseDriverShaping.h"

class ASimCruisePlayerController;

/** One rendered frame of the driver's controls, before the shaper (what the pad, keys and wheel gave). */
struct FSimCruiseInputFrame
{
	/** Seconds since the recording started, at the end of this frame, and the frame's length. */
	double TimeS = 0.0;
	double DeltaS = 0.0;
	FSimCruiseRawControls Raw;
	/** Gear changes asked for during this frame: +1 up, -1 down, 0 none (two in one frame add). */
	int32 Shift = 0;
	/** The manual gearbox was on. */
	bool bManual = false;
};

/**
 * A recording of the driver's controls: car, start pose and one row per frame
 * (steer, throttle, brake, clutch, handbrake, gear changes, gearbox mode, with time).
 * Saved as CSV with '#' header lines, so a spreadsheet opens it and a person can edit it.
 */
struct SIMCRUISE_API FSimCruiseInputTape
{
	FString CarId;
	/** Where the car stood when the recording started: ground point under the body (world cm) and heading. */
	FVector StartGroundCm = FVector::ZeroVector;
	double StartYawDeg = 0.0;
	/** The same start as longitude and latitude, when the world had a geographic frame. */
	bool bHasLonLat = false;
	double StartLonDeg = 0.0;
	double StartLatDeg = 0.0;
	/** Speed at the start (m/s): a replay always starts at rest. */
	double StartSpeedMps = 0.0;
	/**
	 * The place the drive was at (the one ASimCruiseGameMode::GetPlace gave) and the world frame
	 * of the recording (origin tile, tile size): a replay travels there first. Older tapes have
	 * none and replay at the game's start.
	 */
	bool bHasPlace = false;
	FString PlaceTitle;
	FString PlaceStreet;
	double PlaceLonDeg = 0.0;
	double PlaceLatDeg = 0.0;
	FIntPoint FrameOriginTile = FIntPoint::ZeroValue;
	double FrameTileSizeM = 0.0;
	/**
	 * The moving traffic during the recording: "off", "on", or empty for an older tape. Traffic is
	 * not the same from run to run, so a tape recorded with it can replay on a different path.
	 */
	FString Traffic;
	FString Note;
	TArray<FSimCruiseInputFrame> Frames;

	double GetDuration() const { return Frames.Num() ? Frames.Last().TimeS : 0.0; }
	/** The frame in force at TimeS: the first whose span reaches TimeS (sample and hold). Cursor speeds up forward scans. */
	const FSimCruiseInputFrame* At(double TimeS, int32& Cursor) const;

	FString ToCsv() const;
	static bool FromCsv(const FString& Text, FSimCruiseInputTape& Out, FString& OutError);
	bool Save(const FString& Path) const;
	static bool Load(const FString& Path, FSimCruiseInputTape& Out, FString& OutError);
};

/**
 * What a replay measures, one row per sample and a summary: speed, yaw rate, body slip, the
 * rear axle's slip angle, distance and lap time (the first time the car comes back within 15 m
 * of where it started after more than 200 m). Fed from the player's telemetry in the game and
 * from the body state on the bench, so both write the same CSV.
 */
struct SIMCRUISE_API FSimCruiseReplayStats
{
	/** VelocityLocalMps: body frame (+X forward, +Y right); YawRateRadS about +Z; RearAxleM from the centre of mass. */
	void Add(double TimeS, const FVector& PositionM, const FVector& VelocityLocalMps, double YawRateRadS, double RearAxleM, int32 Gear);

	static FString CsvHeader();
	FString ToCsv() const;
	bool SaveCsv(const FString& Path) const;
	/** One line: REPLAY seconds=.. distance_m=.. lap_time_s=.. max_kph=.. mean_kph=.. max_yaw_dps=.. max_beta_deg=.. max_rear_slip_deg=.. */
	FString Summary() const;

	TArray<FString> Rows;
	double DurationS = 0.0;
	double DistanceM = 0.0;
	/** -1 when the car did not come back to its start. */
	double LapTimeS = -1.0;
	double MaxSpeedKph = 0.0;
	double MaxYawDps = 0.0;
	double MaxBetaDeg = 0.0;
	double MaxRearSlipDeg = 0.0;

private:
	bool bHasStart = false;
	FVector Start = FVector::ZeroVector;
	FVector Last = FVector::ZeroVector;
	double SpeedTimeSum = 0.0;
	double LastTimeS = 0.0;
};

/**
 * Records and replays the driver's controls in a game (any build).
 *
 * Record: -SimCruiseRecordInput[=FILE] starts once the car is placed; F9 starts and stops it
 * while driving. The tape goes to Saved/DriveTest/input-<time>.csv (or FILE) when the
 * recording stops or the game ends.
 *
 * Replay: -SimCruiseReplayInput=FILE waits for the car and the tiles (as the drive robot does),
 * travels to the tape's place when the car is more than 2 km from the recorded start (and
 * waits for its tiles again), puts the tape's car at the tape's start at rest (or on the drive robot's test pad with
 * -SimCruiseReplayPad), then feeds the recorded controls through the same shaper and vehicle
 * as a pad, and writes speed, yaw rate, slip and lap time to Saved/DriveTest/replay-<tape>.csv
 * (-SimCruiseReplayOut=FILE), logs a "REPLAY ..." summary line and quits
 * (-SimCruiseKeepOpen stays).
 *
 * Traffic is not the same from run to run, so a replay and a -SimCruiseRecordInput recording turn
 * the moving traffic off (-SimCruiseReplayTraffic / -SimCruiseRecordTraffic keep it). An F9
 * recording keeps the traffic the driver has, writes "# traffic=on" and warns that a replay can differ.
 * Each recording also saves its own path as <tape>-path.csv, in the replay CSV's columns.
 */
class SIMCRUISE_API FSimCruiseInputTapeDriver
{
public:
	/** Read the command line. */
	void Begin(ASimCruisePlayerController& Player);
	/** Each frame, before the shaper: records Raw, or replaces it with the tape. */
	void Process(ASimCruisePlayerController& Player, FSimCruiseRawControls& Raw, float DeltaSeconds);
	/** The driver asked for a gear (recorded with the frame). */
	void NoteShift(int32 Step) { PendingShift += Step; }
	/** While a tape drives, the pad's own gear buttons do nothing. */
	bool IsReplaying() const { return Stage == EStage::Replay || Stage == EStage::Settle || Stage == EStage::Place || Stage == EStage::Travel; }
	bool IsRecording() const { return bRecording; }
	void End();

private:
	enum class EStage : uint8 { Off, WaitCar, Travel, Place, Settle, Replay, Done };

	void StartRecording(ASimCruisePlayerController& Player);
	void StopRecording(ASimCruisePlayerController& Player);
	void TickReplay(ASimCruisePlayerController& Player, FSimCruiseRawControls& Raw, float DeltaSeconds);
	void FinishReplay(ASimCruisePlayerController& Player);

	// Recording.
	bool bRecording = false;
	bool bRecordAtStart = false;
	FString RecordPath;
	FSimCruiseInputTape Recording;
	double RecordClock = 0.0;
	int32 PendingShift = 0;
	/** The recorded drive's own path, in the replay's CSV form (<tape>-path.csv), to compare a replay with. */
	FSimCruiseReplayStats RecordPathStats;
	void SaveRecording(const FString& Path);

	// Replay.
	EStage Stage = EStage::Off;
	FSimCruiseInputTape Tape;
	FString TapePath;
	FString OutPath;
	bool bPad = false;
	/** The replay has asked for the tape's place once (or found it not needed). */
	bool bTravelled = false;
	double StageStarted = 0.0;
	int32 StageFrames = 0;
	double Clock = 0.0;
	int32 Cursor = 0;
	int32 LastShiftFrame = -1;
	FSimCruiseReplayStats Stats;
};
