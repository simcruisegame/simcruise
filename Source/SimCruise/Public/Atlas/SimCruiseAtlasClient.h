// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "Templates/SharedPointer.h"
#include <atomic>

/**
 * Client for the atlas helper (helper/atlas.mjs, shipped as tools/atlas.exe).
 *
 * The helper stays the only network-facing process (docs/unreal-architecture.md):
 * each request launches one helper job `<command> <cache dir> <args...>` and
 * reads one JSON document from its stdout. Jobs run on their own worker
 * threads and never block the game thread; at most MaxConcurrent run at once
 * (the Godot client ran one tile job at a time plus a few side jobs) and the
 * rest wait in a queue. Every job has a timeout and can be cancelled; a
 * timeout or cancel kills the helper's process tree. Exit codes, empty and
 * malformed output are typed errors, never a crash.
 */

enum class ESimCruiseAtlasError : uint8
{
	None,
	NotConfigured,   // no helper executable found
	LaunchFailed,
	Timeout,
	Cancelled,
	ExitCode,        // the helper reported a failure (stderr carries its message)
	EmptyOutput,
	MalformedOutput, // stdout was not one JSON document
};

SIMCRUISE_API const TCHAR* LexToString(ESimCruiseAtlasError Error);

struct FSimCruiseAtlasRequest
{
	FString Command;            // "tile", "search", "aerial", ...
	TArray<FString> Args;       // after the cache directory
	double TimeoutSeconds = 60.0;
};

struct FSimCruiseAtlasResult
{
	ESimCruiseAtlasError Error = ESimCruiseAtlasError::None;
	int32 ExitCode = 0;
	FString Stdout;
	FString Stderr;
	TSharedPtr<FJsonValue> Json;
	double Seconds = 0.0;

	bool IsOk() const { return Error == ESimCruiseAtlasError::None && Json.IsValid(); }
	FString Describe() const;
};

/** How to launch the helper. */
struct FSimCruiseAtlasConfig
{
	/** Executable to run (tools/atlas.exe, or bun). Empty: not configured. */
	FString Executable;
	/** Arguments placed before the command (for bun: the path of helper/atlas.mjs). */
	TArray<FString> PrefixArgs;
	/** The helper's bounded source cache (it evicts at 96 MB itself). */
	FString CacheDir;
	FString WorkingDirectory;
	int32 MaxConcurrent = 2;

	bool IsConfigured() const { return !Executable.IsEmpty(); }

	/**
	 * Resolution order: SIMCRUISE_MAP_HELPER (as in the Godot client), then
	 * <project>/tools/atlas(.exe), then atlas(.exe) beside the game binary, then
	 * bun (~/.bun/bin) running helper/atlas.mjs from the repository.
	 * The cache is <user settings dir>/SimCruise/map-cache unless SIMCRUISE_MAP_CACHE is set.
	 */
	static FSimCruiseAtlasConfig ResolveDefault();
};

/** Handle to an issued request; Cancel() is safe from any thread. */
class SIMCRUISE_API FSimCruiseAtlasJob : public TSharedFromThis<FSimCruiseAtlasJob, ESPMode::ThreadSafe>
{
public:
	void Cancel() { bCancelled = true; }
	bool IsCancelled() const { return bCancelled; }
	bool IsDone() const { return bDone; }
	uint64 Serial() const { return JobSerial; }
	TFuture<FSimCruiseAtlasResult> Future;

private:
	friend class FSimCruiseAtlasClient;
	FSimCruiseAtlasRequest Request;
	TPromise<FSimCruiseAtlasResult> Promise;
	std::atomic<bool> bCancelled{ false };
	std::atomic<bool> bDone{ false };
	uint64 JobSerial = 0;
};

class SIMCRUISE_API FSimCruiseAtlasClient : public TSharedFromThis<FSimCruiseAtlasClient, ESPMode::ThreadSafe>
{
public:
	explicit FSimCruiseAtlasClient(const FSimCruiseAtlasConfig& InConfig);
	~FSimCruiseAtlasClient();

	const FSimCruiseAtlasConfig& GetConfig() const { return Config; }

	/** Queue a request. The future completes on a worker thread. */
	TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> Run(const FSimCruiseAtlasRequest& Request);

	/** Convenience: the helper's `tile <x> <y>` document for a zoom-16 tile. */
	TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> RequestTile(const FIntPoint& Tile, double TimeoutSeconds = 90.0);

	/** Cancel every queued and running job (shutdown, a new search). */
	void CancelAll();

	int32 NumRunning() const;
	int32 NumQueued() const;

	/** Run one helper process synchronously on the calling thread (worker threads and tests only). */
	static FSimCruiseAtlasResult RunBlocking(const FSimCruiseAtlasConfig& Config, const FSimCruiseAtlasRequest& Request, const std::atomic<bool>* Cancel);

private:
	void StartNextLocked();
	void Finish(const TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>& Job, FSimCruiseAtlasResult&& Result);

	FSimCruiseAtlasConfig Config;
	mutable FCriticalSection Lock;
	TArray<TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>> Queue;
	TArray<TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>> Running;
	uint64 NextSerial = 1;
	std::atomic<bool> bShuttingDown{ false };
};
