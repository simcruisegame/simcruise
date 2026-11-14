// Copyright SimCruise contributors. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Live/SimCruiseAerialEye.h"

/** The scan eye's cache file reads back exactly, and its channel names follow the scan contract. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseAerialEyeCacheTest, "SimCruise.AI.AerialEye.Cache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCruiseAerialEyeCacheTest::RunTest(const FString& Parameters)
{
	const TArray<FName>& Names = FSimCruiseAerialEye::ChannelNames();
	TestEqual(TEXT("12 classes"), Names.Num(), 12);
	TestTrue(TEXT("contract names"), Names.Contains(FName(TEXT("road_prob"))) && Names.Contains(FName(TEXT("building_prob")))
		&& Names.Contains(FName(TEXT("water_prob"))) && Names.Contains(FName(TEXT("canopy_prob"))));
	const FSimCruiseAerialEye& Eye = FSimCruiseAerialEye::Get();
	TestTrue(TEXT("weights licence and OSM credit"), Eye.Licence().Contains(TEXT("MIT")) && Eye.Licence().Contains(TEXT("OpenStreetMap")));
	TestTrue(TEXT("training data recorded"), Eye.TrainingDataLicence().Contains(TEXT("WorldCover")));

	const int32 Size = FSimCruiseAerialEye::Cells + 2 * FSimCruiseAerialEye::GuardCells;
	TArray<FSimCruiseScanChannel> In;
	for (int32 K = 0; K < Names.Num(); ++K)
	{
		FSimCruiseScanChannel& C = In.AddDefaulted_GetRef();
		C.Name = Names[K];
		C.Size = Size;
		C.Confidence = 0.05f * K;
		C.Data.SetNumUninitialized(Size * Size);
		for (int32 I = 0; I < C.Data.Num(); ++I) C.Data[I] = static_cast<uint8>((I * 7 + K * 31) & 255);
	}
	TArray<uint8> Bytes;
	TArray<FSimCruiseClassTint> TintsIn = {FSimCruiseClassTint{TEXT("road_prob"), FColor(90, 92, 95, 255), 0.12f, 0.8f}};
	FSimCruiseAerialEye::Encode(In, TintsIn, Bytes);
	TArray<FSimCruiseClassTint> TintsOut;
	TArray<FSimCruiseScanChannel> Out;
	TestTrue(TEXT("decodes"), FSimCruiseAerialEye::Decode(Bytes, Out, TintsOut));
	TestEqual(TEXT("channel count"), Out.Num(), In.Num());
	bool bSame = Out.Num() == In.Num();
	for (int32 K = 0; bSame && K < In.Num(); ++K)
	{
		bSame = Out[K].Name == In[K].Name && Out[K].Size == In[K].Size && Out[K].Confidence == In[K].Confidence && Out[K].Data == In[K].Data;
	}
	TestTrue(TEXT("byte-identical channels"), bSame);
	TestTrue(TEXT("tints read back"), TintsOut.Num() == 1 && TintsOut[0].Channel == TintsIn[0].Channel && TintsOut[0].Mean == TintsIn[0].Mean && TintsOut[0].Confidence == TintsIn[0].Confidence);

	TArray<uint8> Broken = Bytes;
	Broken[0] ^= 0xFF;
	TestFalse(TEXT("a wrong magic is refused"), FSimCruiseAerialEye::Decode(Broken, Out, TintsOut));
	Broken = Bytes;
	Broken.SetNum(Bytes.Num() / 2);
	AddExpectedMessage(TEXT("Failed to uncompress memory"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("a cut file is refused"), FSimCruiseAerialEye::Decode(Broken, Out, TintsOut));
	return true;
}

/** The eye's input covers the tile and its guard border, cut from the right quarter of the zoom-14 photo. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseAerialEyeInputsTest, "SimCruise.AI.AerialEye.Inputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCruiseAerialEyeInputsTest::RunTest(const FString& Parameters)
{
	// A 1024 px photo whose 16 quarters each have their own colour (red = column, green = row).
	const int32 N = 1024;
	TArray<FColor> Photo;
	Photo.SetNumUninitialized(N * N);
	for (int32 Y = 0; Y < N; ++Y) for (int32 X = 0; X < N; ++X) Photo[Y * N + X] = FColor(40 + 60 * (X / 256), 40 + 60 * (Y / 256), 0, 255);
	FSimCruiseScanInputs In;
	FSimCruiseAerialEye::InputsFromPhoto(Photo, N, FIntPoint(4 * 100 + 2, 4 * 200 + 1), In);
	TestEqual(TEXT("input size"), In.AerialSize, FSimCruiseAerialEye::InputSize);
	TestEqual(TEXT("tile"), In.TileZXY, FIntVector(16, 402, 801));
	const FColor Centre = In.AerialRGB[(In.AerialSize / 2) * In.AerialSize + In.AerialSize / 2];
	TestEqual(TEXT("centre from column 2"), int32(Centre.R), 40 + 60 * 2);
	TestEqual(TEXT("centre from row 1"), int32(Centre.G), 40 + 60 * 1);
	// The guard border reaches into the neighbouring quarter on the left (column 1).
	const FColor Left = In.AerialRGB[(In.AerialSize / 2) * In.AerialSize + 1];
	TestEqual(TEXT("guard reaches the west neighbour"), int32(Left.R), 40 + 60 * 1);
	// At the photo's own edge the guard mirrors the photo (tile in column 0: the guard shows column 0 again).
	FSimCruiseAerialEye::InputsFromPhoto(Photo, N, FIntPoint(4 * 100, 4 * 200), In);
	TestEqual(TEXT("mirrored at the photo edge"), int32(In.AerialRGB[(In.AerialSize / 2) * In.AerialSize].R), 40);

	// Tints: a class sure everywhere takes the photo's colour; a class sure nowhere gives no tint.
	const int32 Size = FSimCruiseAerialEye::Cells + 2 * FSimCruiseAerialEye::GuardCells;
	TArray<FSimCruiseScanChannel> Channels;
	Channels.Add(FSimCruiseScanChannel{TEXT("road_prob"), Size, TArray<uint8>(), 1.f});
	Channels[0].Data.Init(255, Size * Size);
	Channels.Add(FSimCruiseScanChannel{TEXT("water_prob"), Size, TArray<uint8>(), 1.f});
	Channels[1].Data.Init(10, Size * Size);
	TArray<FSimCruiseClassTint> Tints;
	FSimCruiseAerialEye::ComputeTints(In, Channels, Tints);
	TestEqual(TEXT("one tint"), Tints.Num(), 1);
	if (Tints.Num() == 1)
	{
		TestEqual(TEXT("road tint is the photo's colour"), int32(Tints[0].Mean.R), 40);
		TestTrue(TEXT("full share and confidence"), Tints[0].Share > 0.99f && Tints[0].Confidence > 0.99f);
	}
	return true;
}

/**
 * The eye's back-off reads the frame time on its worker thread without touching FApp's game-thread
 * time (that raised an ensure, "no inherited time context", in game runs).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimCruiseAerialEyeBackOffTest, "SimCruise.AI.AerialEye.BackOffOffGameThread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSimCruiseAerialEyeBackOffTest::RunTest(const FString& Parameters)
{
	FSimCruiseAerialEye::NoteFrameSeconds(1.f / 30.f);
	TFuture<bool> Slow = Async(EAsyncExecution::Thread, []() { return FSimCruiseAerialEye::IsGameSlow(); });
	TestTrue(TEXT("a 30 fps frame is slow, read on a worker"), Slow.Get());
	FSimCruiseAerialEye::NoteFrameSeconds(1.f / 90.f);
	TFuture<bool> Fast = Async(EAsyncExecution::Thread, []() { return FSimCruiseAerialEye::IsGameSlow(); });
	TestFalse(TEXT("a 90 fps frame is not slow"), Fast.Get());
	FSimCruiseAerialEye::NoteFrameSeconds(0.f);
	return true;
}

#endif
