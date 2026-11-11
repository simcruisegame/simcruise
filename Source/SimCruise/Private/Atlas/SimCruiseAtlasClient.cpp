// Copyright SimCruise contributors. All Rights Reserved.

#include "Atlas/SimCruiseAtlasClient.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseAtlas, Log, All);

const TCHAR* LexToString(ESimCruiseAtlasError Error)
{
	switch (Error)
	{
	case ESimCruiseAtlasError::None: return TEXT("none");
	case ESimCruiseAtlasError::NotConfigured: return TEXT("not-configured");
	case ESimCruiseAtlasError::LaunchFailed: return TEXT("launch-failed");
	case ESimCruiseAtlasError::Timeout: return TEXT("timeout");
	case ESimCruiseAtlasError::Cancelled: return TEXT("cancelled");
	case ESimCruiseAtlasError::ExitCode: return TEXT("exit-code");
	case ESimCruiseAtlasError::EmptyOutput: return TEXT("empty-output");
	case ESimCruiseAtlasError::MalformedOutput: return TEXT("malformed-output");
	}
	return TEXT("unknown");
}

FString FSimCruiseAtlasResult::Describe() const
{
	FString Message = FString::Printf(TEXT("%s (exit %d, %.2f s)"), LexToString(Error), ExitCode, Seconds);
	const FString Err = Stderr.TrimStartAndEnd();
	if (!Err.IsEmpty())
	{
		Message += TEXT(": ") + Err.Left(300);
	}
	return Message;
}

namespace
{
	FString Quote(const FString& Arg)
	{
		if (!Arg.IsEmpty() && !Arg.Contains(TEXT(" ")) && !Arg.Contains(TEXT("\"")) && !Arg.Contains(TEXT("\t")))
		{
			return Arg;
		}
		FString Escaped = Arg.Replace(TEXT("\""), TEXT("\\\""));
		if (Escaped.EndsWith(TEXT("\\")))
		{
			Escaped += TEXT("\\"); // a trailing backslash must not escape the closing quote
		}
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	void DrainPipe(void* Pipe, TArray<uint8>& Into)
	{
		TArray<uint8> Chunk;
		while (FPlatformProcess::ReadPipeToArray(Pipe, Chunk) && Chunk.Num() > 0)
		{
			Into.Append(Chunk);
			Chunk.Reset();
		}
	}

	FString Utf8(const TArray<uint8>& Bytes)
	{
		if (Bytes.Num() == 0)
		{
			return FString();
		}
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		return FString(Converted.Length(), Converted.Get());
	}

	FString FirstExisting(std::initializer_list<FString> Paths)
	{
		for (const FString& Path : Paths)
		{
			if (!Path.IsEmpty() && IFileManager::Get().FileExists(*Path))
			{
				return Path;
			}
		}
		return FString();
	}
}

FSimCruiseAtlasConfig FSimCruiseAtlasConfig::ResolveDefault()
{
	FSimCruiseAtlasConfig Config;
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Exe = PLATFORM_WINDOWS ? TEXT("atlas.exe") : TEXT("atlas");
	const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_MAP_HELPER"));
	Config.Executable = FirstExisting({ Override, FPaths::Combine(Root, TEXT("tools"), Exe), FPaths::Combine(FPlatformProcess::BaseDir(), Exe) });
	if (Config.Executable.IsEmpty())
	{
		const FString Home = FPlatformMisc::GetEnvironmentVariable(PLATFORM_WINDOWS ? TEXT("USERPROFILE") : TEXT("HOME"));
		const FString Bun = FirstExisting({ FPaths::Combine(Home, TEXT(".bun"), TEXT("bin"), PLATFORM_WINDOWS ? TEXT("bun.exe") : TEXT("bun")) });
		const FString Script = FPaths::Combine(Root, TEXT("helper"), TEXT("atlas.mjs"));
		if (!Bun.IsEmpty() && IFileManager::Get().FileExists(*Script))
		{
			Config.Executable = Bun;
			Config.PrefixArgs = { Script };
		}
	}
	Config.WorkingDirectory = Root;
	const FString CacheOverride = FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_MAP_CACHE"));
	Config.CacheDir = !CacheOverride.IsEmpty() ? CacheOverride : FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("SimCruise"), TEXT("map-cache"));
	return Config;
}

FSimCruiseAtlasClient::FSimCruiseAtlasClient(const FSimCruiseAtlasConfig& InConfig)
	: Config(InConfig)
{
	Config.MaxConcurrent = FMath::Clamp(Config.MaxConcurrent, 1, 8);
}

FSimCruiseAtlasClient::~FSimCruiseAtlasClient()
{
	// Worker lambdas hold a strong reference, so nothing is running by now.
	bShuttingDown = true;
}

TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> FSimCruiseAtlasClient::Run(const FSimCruiseAtlasRequest& Request)
{
	TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> Job = MakeShared<FSimCruiseAtlasJob, ESPMode::ThreadSafe>();
	Job->Request = Request;
	Job->Future = Job->Promise.GetFuture();
	FScopeLock Guard(&Lock);
	Job->JobSerial = NextSerial++;
	Queue.Add(Job);
	StartNextLocked();
	return Job;
}

TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> FSimCruiseAtlasClient::RequestTile(const FIntPoint& Tile, double TimeoutSeconds)
{
	FSimCruiseAtlasRequest Request;
	Request.Command = TEXT("tile");
	Request.Args = { FString::FromInt(Tile.X), FString::FromInt(Tile.Y) };
	Request.TimeoutSeconds = TimeoutSeconds;
	return Run(Request);
}

void FSimCruiseAtlasClient::CancelAll()
{
	FScopeLock Guard(&Lock);
	for (const TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>& Job : Running)
	{
		Job->Cancel();
	}
	TArray<TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>> Dropped = MoveTemp(Queue);
	Queue.Reset();
	for (const TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>& Job : Dropped)
	{
		Job->Cancel();
		FSimCruiseAtlasResult Result;
		Result.Error = ESimCruiseAtlasError::Cancelled;
		Job->bDone = true;
		Job->Promise.SetValue(MoveTemp(Result));
	}
}

int32 FSimCruiseAtlasClient::NumRunning() const
{
	FScopeLock Guard(&Lock);
	return Running.Num();
}

int32 FSimCruiseAtlasClient::NumQueued() const
{
	FScopeLock Guard(&Lock);
	return Queue.Num();
}

void FSimCruiseAtlasClient::StartNextLocked()
{
	while (Running.Num() < Config.MaxConcurrent && Queue.Num() > 0)
	{
		TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> Job = Queue[0];
		Queue.RemoveAt(0);
		if (Job->IsCancelled())
		{
			FSimCruiseAtlasResult Result;
			Result.Error = ESimCruiseAtlasError::Cancelled;
			Job->bDone = true;
			Job->Promise.SetValue(MoveTemp(Result));
			continue;
		}
		Running.Add(Job);
		TSharedRef<FSimCruiseAtlasClient, ESPMode::ThreadSafe> Self = AsShared();
		const FSimCruiseAtlasConfig JobConfig = Config;
		Async(EAsyncExecution::Thread, [Self, Job, JobConfig]()
		{
			FSimCruiseAtlasResult Result = RunBlocking(JobConfig, Job->Request, &Job->bCancelled);
			Self->Finish(Job, MoveTemp(Result));
		});
	}
}

void FSimCruiseAtlasClient::Finish(const TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>& Job, FSimCruiseAtlasResult&& Result)
{
	{
		FScopeLock Guard(&Lock);
		Running.Remove(Job);
		if (!bShuttingDown)
		{
			StartNextLocked();
		}
	}
	if (Result.Error != ESimCruiseAtlasError::None && Result.Error != ESimCruiseAtlasError::Cancelled)
	{
		UE_LOG(LogSimCruiseAtlas, Warning, TEXT("atlas %s failed: %s"), *Job->Request.Command, *Result.Describe());
	}
	Job->bDone = true;
	Job->Promise.SetValue(MoveTemp(Result));
}

FSimCruiseAtlasResult FSimCruiseAtlasClient::RunBlocking(const FSimCruiseAtlasConfig& Config, const FSimCruiseAtlasRequest& Request, const std::atomic<bool>* Cancel)
{
	FSimCruiseAtlasResult Result;
	const double Started = FPlatformTime::Seconds();
	if (!Config.IsConfigured())
	{
		Result.Error = ESimCruiseAtlasError::NotConfigured;
		return Result;
	}
	if (!Config.CacheDir.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*Config.CacheDir, true);
	}

	TArray<FString> Parts;
	for (const FString& Prefix : Config.PrefixArgs) Parts.Add(Quote(Prefix));
	if (!Request.Command.IsEmpty()) Parts.Add(Quote(Request.Command));
	if (!Config.CacheDir.IsEmpty()) Parts.Add(Quote(Config.CacheDir));
	for (const FString& Arg : Request.Args) Parts.Add(Quote(Arg));
	const FString Params = FString::Join(Parts, TEXT(" "));

	void* OutRead = nullptr; void* OutWrite = nullptr;
	void* ErrRead = nullptr; void* ErrWrite = nullptr;
	if (!FPlatformProcess::CreatePipe(OutRead, OutWrite) || !FPlatformProcess::CreatePipe(ErrRead, ErrWrite))
	{
		FPlatformProcess::ClosePipe(OutRead, OutWrite);
		Result.Error = ESimCruiseAtlasError::LaunchFailed;
		Result.Stderr = TEXT("could not create pipes");
		return Result;
	}
	const TCHAR* WorkingDir = Config.WorkingDirectory.IsEmpty() ? nullptr : *Config.WorkingDirectory;
	FProcHandle Proc = FPlatformProcess::CreateProc(*Config.Executable, *Params, false, true, true, nullptr, 0, WorkingDir, OutWrite, nullptr, ErrWrite);
	if (!Proc.IsValid())
	{
		FPlatformProcess::ClosePipe(OutRead, OutWrite);
		FPlatformProcess::ClosePipe(ErrRead, ErrWrite);
		Result.Error = ESimCruiseAtlasError::LaunchFailed;
		Result.Stderr = FString::Printf(TEXT("could not start %s"), *Config.Executable);
		Result.Seconds = FPlatformTime::Seconds() - Started;
		return Result;
	}

	TArray<uint8> OutBytes, ErrBytes;
	bool bKilled = false;
	while (true)
	{
		DrainPipe(OutRead, OutBytes);
		DrainPipe(ErrRead, ErrBytes);
		if (!FPlatformProcess::IsProcRunning(Proc))
		{
			break;
		}
		if (Cancel && Cancel->load())
		{
			Result.Error = ESimCruiseAtlasError::Cancelled;
			bKilled = true;
			break;
		}
		if (FPlatformTime::Seconds() - Started > Request.TimeoutSeconds)
		{
			Result.Error = ESimCruiseAtlasError::Timeout;
			bKilled = true;
			break;
		}
		FPlatformProcess::Sleep(0.005f);
	}
	if (bKilled)
	{
		FPlatformProcess::TerminateProc(Proc, true);
		FPlatformProcess::WaitForProc(Proc);
	}
	DrainPipe(OutRead, OutBytes);
	DrainPipe(ErrRead, ErrBytes);
	int32 Code = 0;
	FPlatformProcess::GetProcReturnCode(Proc, &Code);
	FPlatformProcess::CloseProc(Proc);
	FPlatformProcess::ClosePipe(OutRead, OutWrite);
	FPlatformProcess::ClosePipe(ErrRead, ErrWrite);

	Result.ExitCode = Code;
	Result.Stdout = Utf8(OutBytes);
	Result.Stderr = Utf8(ErrBytes);
	Result.Seconds = FPlatformTime::Seconds() - Started;
	if (bKilled)
	{
		return Result;
	}
	if (Code != 0)
	{
		Result.Error = ESimCruiseAtlasError::ExitCode;
		return Result;
	}

	// One JSON document; the helper prints it as its last line.
	TArray<FString> Lines;
	Result.Stdout.ParseIntoArrayLines(Lines, true);
	FString Document;
	for (int32 i = Lines.Num() - 1; i >= 0; --i)
	{
		const FString Line = Lines[i].TrimStartAndEnd();
		if (!Line.IsEmpty())
		{
			Document = Line;
			break;
		}
	}
	if (Document.IsEmpty())
	{
		Result.Error = ESimCruiseAtlasError::EmptyOutput;
		return Result;
	}
	TSharedPtr<FJsonValue> Value;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Document);
	if (!FJsonSerializer::Deserialize(Reader, Value) || !Value.IsValid() || (Value->Type != EJson::Object && Value->Type != EJson::Array))
	{
		Result.Error = ESimCruiseAtlasError::MalformedOutput;
		return Result;
	}
	Result.Json = Value;
	return Result;
}
