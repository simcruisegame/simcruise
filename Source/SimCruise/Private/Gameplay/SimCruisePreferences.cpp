// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePreferences.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString QaDir()
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("SIMCRUISE_QA_PREFS_DIR"));
	}

	TSharedPtr<FJsonObject> Read()
	{
		FString Text;
		TSharedPtr<FJsonObject> Doc;
		if (FFileHelper::LoadFileToString(Text, *SimCruisePreferences::Path()))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			FJsonSerializer::Deserialize(Reader, Doc);
		}
		return Doc.IsValid() ? Doc : MakeShared<FJsonObject>();
	}

	void Write(const TSharedPtr<FJsonObject>& Doc)
	{
		FString Text;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
		FJsonSerializer::Serialize(Doc.ToSharedRef(), Writer);
		const FString Final = SimCruisePreferences::Path();
		const FString Temp = Final + FString::Printf(TEXT(".%u.tmp"), FPlatformProcess::GetCurrentProcessId());
		if (FFileHelper::SaveStringToFile(Text, *Temp))
		{
			IFileManager::Get().Move(*Final, *Temp, /*bReplace=*/ true);
		}
	}
}

namespace SimCruisePreferences
{
	bool IsEnabled()
	{
		if (!QaDir().IsEmpty()) return true;
		if (GIsAutomationTesting) return false;
		const TCHAR* Cmd = FCommandLine::Get();
		static const TCHAR* QaFlags[] = {
			TEXT("SimCruiseDriveCapture"), TEXT("SimCruiseDriveTest"), TEXT("SimCruiseShellSelfTest"), TEXT("SimCruiseShellCapture"),
			TEXT("SimCruiseLab"), TEXT("SimCruiseLatencyProbe"), TEXT("unattended"),
		};
		for (const TCHAR* Flag : QaFlags)
		{
			if (FParse::Param(Cmd, Flag)) return false;
		}
		FString Ignored;
		return !FParse::Value(Cmd, TEXT("SimCruiseDrivePerf="), Ignored) && !FParse::Value(Cmd, TEXT("SimCruisePreview="), Ignored) &&
			!FParse::Value(Cmd, TEXT("SimCruiseSoak="), Ignored);
	}

	FString Path()
	{
		const FString Dir = QaDir();
		return FPaths::Combine(Dir.IsEmpty() ? FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SimCruise")) : Dir, TEXT("preferences.json"));
	}

	FString GetString(const FString& Key, const FString& Default)
	{
		FString Value;
		return IsEnabled() && Read()->TryGetStringField(Key, Value) ? Value : Default;
	}

	int32 GetInt(const FString& Key, int32 Default, int32 Min, int32 Max)
	{
		double Value = 0.0;
		return IsEnabled() && Read()->TryGetNumberField(Key, Value) ? FMath::Clamp(FMath::RoundToInt(Value), Min, Max) : Default;
	}

	bool GetBool(const FString& Key, bool Default)
	{
		bool Value = false;
		return IsEnabled() && Read()->TryGetBoolField(Key, Value) ? Value : Default;
	}

	void SetString(const FString& Key, const FString& Value)
	{
		if (!IsEnabled()) return;
		TSharedPtr<FJsonObject> Doc = Read();
		Doc->SetStringField(Key, Value);
		Write(Doc);
	}

	void SetInt(const FString& Key, int32 Value)
	{
		if (!IsEnabled()) return;
		TSharedPtr<FJsonObject> Doc = Read();
		Doc->SetNumberField(Key, Value);
		Write(Doc);
	}

	void SetBool(const FString& Key, bool Value)
	{
		if (!IsEnabled()) return;
		TSharedPtr<FJsonObject> Doc = Read();
		Doc->SetBoolField(Key, Value);
		Write(Doc);
	}
}
