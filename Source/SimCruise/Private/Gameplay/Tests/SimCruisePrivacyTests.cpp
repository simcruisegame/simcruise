// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "Gameplay/SimCruisePreferences.h"
#include "HAL/FileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Vehicle/CarDefinitionLoader.h"

/*
 * Privacy (project owner, 2026-11-14): no person's name, e-mail address or account name may appear
 * in the game or in the repository. The words are kept here only as CRC-32 values (FCrc::MemCrc32 of
 * the lower-case UTF-8 word), so this test does not write them either. It reads every text file of
 * the checkout (not Saved, Intermediate, Binaries, DerivedDataCache, node_modules, build, tools, or a
 * folder that is another git repository), splits it into words of letters and digits, and fails on
 * each word whose CRC is in the list, with the file and the line. Add a word: put its CRC in PrivateWordCrcs.
 * Map data and third-party texts (data/tiles, data/map, data/knowledge/places, data/observed, licenses,
 * helper/vendor) are not read: a street or an author there may have the same name.
 */
namespace
{
	const uint32 PrivateWordCrcs[] = {
		0xFEF0BE8Fu, // the project owner's first name
		0xAFE23AF2u, // the project owner's e-mail domain
		0x23586CDAu, // the project owner's account name
		0x3A1A6340u, // the project owner's other account name
	};

	uint32 WordCrc(const FString& Word)
	{
		const FTCHARToUTF8 Utf8(*Word.ToLower());
		return FCrc::MemCrc32(Utf8.Get(), Utf8.Length());
	}

	bool IsPrivate(uint32 Crc)
	{
		for (const uint32 Private : PrivateWordCrcs)
		{
			if (Private == Crc) return true;
		}
		return false;
	}

	bool IsTextFile(const FString& Path)
	{
		static const TCHAR* Extensions[] = {
			TEXT("md"), TEXT("txt"), TEXT("json"), TEXT("jsonl"), TEXT("ini"), TEXT("cpp"), TEXT("h"), TEXT("cs"), TEXT("ps1"),
			TEXT("sh"), TEXT("py"), TEXT("mjs"), TEXT("js"), TEXT("html"), TEXT("css"), TEXT("xml"), TEXT("yml"), TEXT("yaml"),
			TEXT("iss"), TEXT("csv"), TEXT("uproject"), TEXT("gitignore"), TEXT("gitattributes"), TEXT("editorconfig"),
		};
		const FString Extension = FPaths::GetExtension(Path).ToLower();
		for (const TCHAR* Text : Extensions)
		{
			if (Extension == Text) return true;
		}
		return false;
	}

	void FindFiles(const FString& Root, const FString& Dir, bool bTop, TArray<FString>& Out)
	{
		static const TCHAR* SkipPaths[] = {
			TEXT("data/tiles"), TEXT("data/map"), TEXT("data/knowledge/places"), TEXT("data/observed"), TEXT("licenses"), TEXT("helper/vendor"),
		};
		const FString Relative = Dir.RightChop(Root.Len()).Replace(TEXT("\\"), TEXT("/")).TrimChar(TEXT('/'));
		for (const TCHAR* S : SkipPaths)
		{
			if (Relative.Equals(S, ESearchCase::IgnoreCase)) return;
		}
		static const TCHAR* Skip[] = {
			TEXT("Saved"), TEXT("Intermediate"), TEXT("Binaries"), TEXT("DerivedDataCache"), TEXT("node_modules"), TEXT("build"),
			TEXT("tools"), TEXT(".git"), TEXT(".vs"), TEXT(".cache"), TEXT("__pycache__"),
		};
		if (!bTop && FPaths::DirectoryExists(FPaths::Combine(Dir, TEXT(".git")))) return; // another repository
		if (!bTop && FPaths::FileExists(FPaths::Combine(Dir, TEXT(".git")))) return;      // another worktree
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *FPaths::Combine(Dir, TEXT("*")), /*Files=*/ true, /*Directories=*/ false);
		for (const FString& Name : Names)
		{
			if (IsTextFile(Name)) Out.Add(FPaths::Combine(Dir, Name));
		}
		Names.Reset();
		IFileManager::Get().FindFiles(Names, *FPaths::Combine(Dir, TEXT("*")), /*Files=*/ false, /*Directories=*/ true);
		for (const FString& Name : Names)
		{
			bool bSkip = false;
			for (const TCHAR* S : Skip) bSkip |= Name.Equals(S, ESearchCase::IgnoreCase);
			if (!bSkip) FindFiles(Root, FPaths::Combine(Dir, Name), false, Out);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePrivacyNoPrivateWordsTest, "SimCruise.Privacy.NoPrivateWords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruisePrivacyNoPrivateWordsTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	TArray<FString> Files;
	FindFiles(Root, Root, true, Files);
	TestTrue(TEXT("found the checkout's text files"), Files.Num() > 100);
	int32 Hits = 0;
	for (const FString& File : Files)
	{
		if (IFileManager::Get().FileSize(*File) > 20 * 1024 * 1024) continue;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File)) continue;
		int32 Line = 1;
		FString Word;
		for (int32 i = 0; i <= Text.Len(); ++i)
		{
			const TCHAR C = i < Text.Len() ? Text[i] : TEXT(' ');
			if (FChar::IsAlnum(C))
			{
				Word.AppendChar(C);
				continue;
			}
			if (Word.Len() >= 4 && Word.Len() <= 24 && IsPrivate(WordCrc(Word)))
			{
				AddError(FString::Printf(TEXT("%s:%d has a private word (a person's name, e-mail or account); write \"the project owner\" or \"Miata\""),
					*File.RightChop(Root.Len()), Line));
				++Hits;
			}
			Word.Reset();
			if (C == TEXT('\n')) ++Line;
		}
	}
	AddInfo(FString::Printf(TEXT("PRIVACY %d files read, %d private words"), Files.Num(), Hits));
	return true;
}

/*
 * A renamed car keeps the player's choices: the saved "car" and "seat_<id>" move to the new id, once,
 * and nothing else changes. The built-in table points at car files that exist, and CarIdCrc is the
 * standard CRC-32 (the table's values were made with zlib.crc32 of the UTF-8 id).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruisePreferencesRenamedCarTest, "SimCruise.Gameplay.Preferences.RenamedCarIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FSimCruisePreferencesRenamedCarTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("CarIdCrc is zlib's CRC-32"), SimCruisePreferences::CarIdCrc(TEXT("nb2001_lowered")), 0x020BBE52u);
	TestEqual(TEXT("CarIdCrc of a test id"), SimCruisePreferences::CarIdCrc(TEXT("old_test_car")), 0x6558FE77u);

	const TArray<FString> Cars = FSimCruiseCarDefinitionLoader::FindAvailableIds();
	for (const TPair<uint32, FString>& Renamed : SimCruisePreferences::RenamedCarIds())
	{
		TestTrue(FString::Printf(TEXT("the renamed car %s exists"), *Renamed.Value), Cars.Contains(Renamed.Value));
		for (const FString& Car : Cars)
		{
			TestNotEqual(FString::Printf(TEXT("no car file still has the retired id of %s"), *Renamed.Value), SimCruisePreferences::CarIdCrc(Car), Renamed.Key);
		}
	}

	const TMap<uint32, FString> Table = { { SimCruisePreferences::CarIdCrc(TEXT("old_test_car")), TEXT("new_test_car") } };
	FJsonObject Doc;
	Doc.SetStringField(TEXT("car"), TEXT("old_test_car"));
	Doc.SetStringField(TEXT("seat_old_test_car"), TEXT("10,20,30,0"));
	Doc.SetStringField(TEXT("seat_mustang65"), TEXT("1,2,3,4"));
	Doc.SetStringField(TEXT("gearbox"), TEXT("manual"));
	TestTrue(TEXT("the first load migrates"), SimCruisePreferences::MigrateCarIds(Doc, Table));
	FString Value;
	TestTrue(TEXT("car moved"), Doc.TryGetStringField(TEXT("car"), Value) && Value == TEXT("new_test_car"));
	TestTrue(TEXT("seat moved"), Doc.TryGetStringField(TEXT("seat_new_test_car"), Value) && Value == TEXT("10,20,30,0"));
	TestFalse(TEXT("old seat key gone"), Doc.HasField(TEXT("seat_old_test_car")));
	TestTrue(TEXT("other seats kept"), Doc.TryGetStringField(TEXT("seat_mustang65"), Value) && Value == TEXT("1,2,3,4"));
	TestTrue(TEXT("other choices kept"), Doc.TryGetStringField(TEXT("gearbox"), Value) && Value == TEXT("manual"));
	TestFalse(TEXT("the second load changes nothing"), SimCruisePreferences::MigrateCarIds(Doc, Table));

	FJsonObject Both;
	Both.SetStringField(TEXT("seat_old_test_car"), TEXT("1,1,1,1"));
	Both.SetStringField(TEXT("seat_new_test_car"), TEXT("2,2,2,2"));
	SimCruisePreferences::MigrateCarIds(Both, Table);
	TestTrue(TEXT("a seat already saved for the new id wins"), Both.TryGetStringField(TEXT("seat_new_test_car"), Value) && Value == TEXT("2,2,2,2"));
	return true;
}

#endif
