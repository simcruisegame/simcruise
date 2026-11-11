// Copyright SimCruise contributors. All Rights Reserved.

#include "Atlas/SimCruiseAtlasClient.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && PLATFORM_WINDOWS

namespace
{
	void ExpectError(FAutomationTestBase& Test, const TCHAR* What, ESimCruiseAtlasError Actual, ESimCruiseAtlasError Expected)
	{
		Test.TestTrue(FString::Printf(TEXT("%s (got %s, want %s)"), What, LexToString(Actual), LexToString(Expected)), Actual == Expected);
	}

	/** A stand-in helper: cmd.exe /c <command> (no network, no Bun). */
	FSimCruiseAtlasConfig ShellConfig(int32 MaxConcurrent = 2)
	{
		FSimCruiseAtlasConfig Config;
		Config.Executable = FPlatformMisc::GetEnvironmentVariable(TEXT("ComSpec"));
		if (Config.Executable.IsEmpty()) Config.Executable = TEXT("C:\\Windows\\System32\\cmd.exe");
		Config.PrefixArgs = { TEXT("/c") };
		Config.MaxConcurrent = MaxConcurrent;
		return Config;
	}

	FSimCruiseAtlasRequest Shell(const FString& Command, const TArray<FString>& Args, double Timeout = 20.0)
	{
		FSimCruiseAtlasRequest R;
		R.Command = Command;
		R.Args = Args;
		R.TimeoutSeconds = Timeout;
		return R;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseAtlasProcessResultsTest, "SimCruise.Atlas.ProcessResults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseAtlasProcessResultsTest::RunTest(const FString& Parameters)
{
	const FSimCruiseAtlasConfig Config = ShellConfig();

	FSimCruiseAtlasResult Ok = FSimCruiseAtlasClient::RunBlocking(Config, Shell(TEXT("echo"), { TEXT("[1,2,3]") }), nullptr);
	ExpectError(*this, TEXT("valid JSON is accepted"), Ok.Error, ESimCruiseAtlasError::None);
	TestTrue(TEXT("the document is parsed"), Ok.Json.IsValid() && Ok.Json->Type == EJson::Array && Ok.Json->AsArray().Num() == 3);

	FSimCruiseAtlasResult Bad = FSimCruiseAtlasClient::RunBlocking(Config, Shell(TEXT("echo"), { TEXT("not-json") }), nullptr);
	ExpectError(*this, TEXT("malformed output is a typed error"), Bad.Error, ESimCruiseAtlasError::MalformedOutput);

	FSimCruiseAtlasResult Empty = FSimCruiseAtlasClient::RunBlocking(Config, Shell(TEXT("rem"), {}), nullptr);
	ExpectError(*this, TEXT("no output is a typed error"), Empty.Error, ESimCruiseAtlasError::EmptyOutput);

	FSimCruiseAtlasResult Exit = FSimCruiseAtlasClient::RunBlocking(Config, Shell(TEXT("exit"), { TEXT("3") }), nullptr);
	ExpectError(*this, TEXT("a failing helper reports its exit code"), Exit.Error, ESimCruiseAtlasError::ExitCode);
	TestEqual(TEXT("exit code"), Exit.ExitCode, 3);

	const double T0 = FPlatformTime::Seconds();
	FSimCruiseAtlasResult Slow = FSimCruiseAtlasClient::RunBlocking(Config, Shell(TEXT("ping"), { TEXT("-n"), TEXT("30"), TEXT("127.0.0.1") }, 1.0), nullptr);
	const double Elapsed = FPlatformTime::Seconds() - T0;
	ExpectError(*this, TEXT("a hung helper times out"), Slow.Error, ESimCruiseAtlasError::Timeout);
	TestTrue(FString::Printf(TEXT("timeout is prompt (%.2f s)"), Elapsed), Elapsed < 5.0);

	FSimCruiseAtlasConfig Missing = Config;
	Missing.Executable = TEXT("C:\\definitely\\not\\here\\atlas.exe");
	ExpectError(*this, TEXT("a missing helper fails to launch"), FSimCruiseAtlasClient::RunBlocking(Missing, Shell(TEXT("tile"), {}), nullptr).Error, ESimCruiseAtlasError::LaunchFailed);

	FSimCruiseAtlasConfig None;
	ExpectError(*this, TEXT("no helper configured"), FSimCruiseAtlasClient::RunBlocking(None, Shell(TEXT("tile"), {}), nullptr).Error, ESimCruiseAtlasError::NotConfigured);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseAtlasAsyncTest, "SimCruise.Atlas.AsyncQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruiseAtlasAsyncTest::RunTest(const FString& Parameters)
{
	TSharedRef<FSimCruiseAtlasClient, ESPMode::ThreadSafe> Client = MakeShared<FSimCruiseAtlasClient, ESPMode::ThreadSafe>(ShellConfig(2));

	// Issuing work never blocks the caller (the game thread).
	const double T0 = FPlatformTime::Seconds();
	TArray<TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe>> Jobs;
	for (int32 i = 0; i < 4; ++i)
	{
		Jobs.Add(Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("2"), TEXT("127.0.0.1") })));
	}
	const double IssueSeconds = FPlatformTime::Seconds() - T0;
	TestTrue(FString::Printf(TEXT("issuing 4 jobs returns at once (%.3f s)"), IssueSeconds), IssueSeconds < 0.25);

	int32 MaxRunning = 0;
	bool bAllDone = false;
	while (!bAllDone && FPlatformTime::Seconds() - T0 < 30.0)
	{
		MaxRunning = FMath::Max(MaxRunning, Client->NumRunning());
		bAllDone = true;
		for (const auto& Job : Jobs) bAllDone &= Job->IsDone();
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("all queued jobs finish"), bAllDone);
	TestTrue(FString::Printf(TEXT("never more than 2 helper processes (%d)"), MaxRunning), MaxRunning <= 2 && MaxRunning >= 1);
	for (const auto& Job : Jobs)
	{
		const FSimCruiseAtlasResult R = Job->Future.Get();
		ExpectError(*this, TEXT("ping output is not a JSON document"), R.Error, ESimCruiseAtlasError::MalformedOutput);
	}

	// Cancelling a running job kills the helper promptly.
	TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> Long = Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("60"), TEXT("127.0.0.1") }, 120.0));
	TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> Queued = Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("60"), TEXT("127.0.0.1") }, 120.0));
	TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> Waiting = Client->Run(Shell(TEXT("ping"), { TEXT("-n"), TEXT("60"), TEXT("127.0.0.1") }, 120.0));
	FPlatformProcess::Sleep(0.3f);
	const double C0 = FPlatformTime::Seconds();
	Client->CancelAll();
	const FSimCruiseAtlasResult A = Long->Future.Get();
	const FSimCruiseAtlasResult B = Queued->Future.Get();
	const FSimCruiseAtlasResult C = Waiting->Future.Get();
	const double CancelSeconds = FPlatformTime::Seconds() - C0;
	ExpectError(*this, TEXT("running job cancelled"), A.Error, ESimCruiseAtlasError::Cancelled);
	ExpectError(*this, TEXT("second running job cancelled"), B.Error, ESimCruiseAtlasError::Cancelled);
	ExpectError(*this, TEXT("queued job cancelled"), C.Error, ESimCruiseAtlasError::Cancelled);
	TestTrue(FString::Printf(TEXT("cancel is prompt (%.2f s)"), CancelSeconds), CancelSeconds < 5.0);
	return true;
}

#endif
