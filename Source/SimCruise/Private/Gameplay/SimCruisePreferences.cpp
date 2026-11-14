// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruisePreferences.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
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

	void Write(const TSharedPtr<FJsonObject>& Doc);

	TSharedPtr<FJsonObject> Read()
	{
		FString Text;
		TSharedPtr<FJsonObject> Doc;
		if (FFileHelper::LoadFileToString(Text, *SimCruisePreferences::Path()))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			FJsonSerializer::Deserialize(Reader, Doc);
		}
		if (!Doc.IsValid()) return MakeShared<FJsonObject>();
		if (SimCruisePreferences::MigrateRenamedCarIds(*Doc)) Write(Doc);
		return Doc;
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

	uint32 CarIdCrc(const FString& CarId)
	{
		const FTCHARToUTF8 Utf8(*CarId);
		return FCrc::MemCrc32(Utf8.Get(), Utf8.Length());
	}

	const TMap<uint32, FString>& RenamedCarIds()
	{
		// The CRC-32 of each retired car id and the id that replaced it.
		static const TMap<uint32, FString> Renamed = {
			{ 0xBEEACA7Cu, TEXT("nb2001_lowered") }, // 2026-11-14: the lowered 2001 Miata got a neutral id
		};
		return Renamed;
	}

	bool MigrateRenamedCarIds(FJsonObject& Doc)
	{
		return MigrateCarIds(Doc, RenamedCarIds());
	}

	bool MigrateCarIds(FJsonObject& Doc, const TMap<uint32, FString>& OldIdCrcToNewId)
	{
		auto NewIdFor = [&OldIdCrcToNewId](const FString& OldId) -> const FString*
		{
			const FString* NewId = OldIdCrcToNewId.Find(CarIdCrc(OldId));
			return NewId && *NewId != OldId ? NewId : nullptr;
		};
		bool bChanged = false;
		FString Car;
		if (Doc.TryGetStringField(TEXT("car"), Car))
		{
			if (const FString* NewId = NewIdFor(Car))
			{
				Doc.SetStringField(TEXT("car"), *NewId);
				bChanged = true;
			}
		}
		TArray<FString> Keys;
		Doc.Values.GetKeys(Keys);
		for (const FString& Key : Keys)
		{
			if (!Key.StartsWith(TEXT("seat_"))) continue;
			const FString* NewId = NewIdFor(Key.Mid(5));
			if (!NewId) continue;
			const FString NewKey = TEXT("seat_") + *NewId;
			if (!Doc.HasField(NewKey)) Doc.SetField(NewKey, Doc.Values[Key]);
			Doc.RemoveField(Key);
			bChanged = true;
		}
		return bChanged;
	}
}
