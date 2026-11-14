// Copyright SimCruise contributors. All Rights Reserved.

#include "Live/SimCruiseAerialEye.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/Compression.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "World/SimCruiseWorldResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseAerialEye, Log, All);

static TAutoConsoleVariable<int32> CVarSimCruiseEyeDump(TEXT("simcruise.Eye.Dump"), 0,
	TEXT("1: the aerial eye also writes photo | classes PNGs of each tile it reads to Saved/Scan/eyes/dump."));

class FSimCruiseAerialEyeModel
{
public:
	TStrongObjectPtr<UNNEModelData> Data;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> Cpu;
	TArray<float> Input;
	TArray<float> Output;
	int32 NumClasses = 0;
	/** The last decoded zoom-14 photograph: 16 tiles share one. Worker only. */
	FString PhotoPath;
	TArray<FColor> Photo;
	int32 PhotoSize = 0;
};

namespace SimCruiseAerialEyeLocal
{
	const uint32 Magic = 0x32455953; // "SYE2"
	/** A cell is "sure" of a class at this probability. */
	const uint8 SureLevel = 153; // 0.6 x 255

	/** Class colours of the dump pictures (lab/scan/scan_data.py PALETTE). */
	const FColor Palette[12] = {
		FColor(60, 60, 60), FColor(150, 120, 80), FColor(200, 60, 60), FColor(40, 90, 200), FColor(20, 110, 30), FColor(120, 200, 90),
		FColor(220, 210, 90), FColor(140, 150, 60), FColor(210, 180, 140), FColor(170, 170, 190), FColor(80, 160, 160), FColor(250, 250, 250)};

	/** Bilinear sample of an image at (U, V) in pixels (pixel centres at .5), clamped at the edges. */
	FLinearColor Sample(const TArray<FColor>& Img, int32 W, int32 H, double U, double V)
	{
		const double X = FMath::Clamp(U - 0.5, 0.0, W - 1.0), Y = FMath::Clamp(V - 0.5, 0.0, H - 1.0);
		const int32 X0 = FMath::FloorToInt32(X), Y0 = FMath::FloorToInt32(Y);
		const int32 X1 = FMath::Min(X0 + 1, W - 1), Y1 = FMath::Min(Y0 + 1, H - 1);
		const float Fx = static_cast<float>(X - X0), Fy = static_cast<float>(Y - Y0);
		auto C = [&](int32 Px, int32 Py) { const FColor& P = Img[Py * W + Px]; return FLinearColor(P.R, P.G, P.B, 0.f); };
		return FMath::Lerp(FMath::Lerp(C(X0, Y0), C(X1, Y0), Fx), FMath::Lerp(C(X0, Y1), C(X1, Y1), Fx), Fy);
	}

	/** Bilinear sample of one float plane (N x N) at (U, V) in its pixels. */
	float SamplePlane(const float* P, int32 N, double U, double V)
	{
		const double X = FMath::Clamp(U - 0.5, 0.0, N - 1.0), Y = FMath::Clamp(V - 0.5, 0.0, N - 1.0);
		const int32 X0 = FMath::FloorToInt32(X), Y0 = FMath::FloorToInt32(Y);
		const int32 X1 = FMath::Min(X0 + 1, N - 1), Y1 = FMath::Min(Y0 + 1, N - 1);
		const float Fx = static_cast<float>(X - X0), Fy = static_cast<float>(Y - Y0);
		return FMath::Lerp(FMath::Lerp(P[Y0 * N + X0], P[Y0 * N + X1], Fx), FMath::Lerp(P[Y1 * N + X0], P[Y1 * N + X1], Fx), Fy);
	}
}

FSimCruiseAerialEye& FSimCruiseAerialEye::Get()
{
	static FSimCruiseAerialEye* Instance = new FSimCruiseAerialEye();
	return *Instance;
}

const TArray<FName>& FSimCruiseAerialEye::ChannelNames()
{
	// Order of the model's classes (lab/scan/scan_data.py CLASSES); names from the scan contract.
	static const TArray<FName> Names = {
		TEXT("road_prob"),                    // paved road
		TEXT("surface_logits_dirt_track"),    // unpaved road or track
		TEXT("building_prob"),                // roof
		TEXT("water_prob"),
		TEXT("canopy_prob"),                  // tree canopy
		TEXT("surface_logits_grass_short"),
		TEXT("surface_logits_crop"),
		TEXT("surface_logits_shrub"),
		TEXT("surface_logits_bare_soil"),
		TEXT("surface_logits_concrete"),      // other built ground: parking, yards, plazas
		TEXT("surface_logits_wetland"),
		TEXT("surface_logits_snow_and_ice"),
	};
	return Names;
}

FString FSimCruiseAerialEye::Licence() const
{
	return bStudent
		? TEXT("SimCruise aerial student (MIT, SimCruise contributors). Contains information from OpenStreetMap, (c) OpenStreetMap contributors, ODbL.")
		: TEXT("Satlas Aerial_SwinB_SI backbone (Allen Institute for AI, ODC-BY 1.0) + SimCruise head (MIT). Contains information from OpenStreetMap, (c) OpenStreetMap contributors, ODbL.");
}

FString FSimCruiseAerialEye::TrainingDataLicence() const
{
	FString Ours = TEXT("USGS The National Map aerial imagery (public domain); OpenStreetMap via OpenFreeMap (ODbL); Meta and WRI canopy height map v2 (CC BY 4.0); ESA WorldCover 2021 v200 (CC BY 4.0)");
	return bStudent
		? Ours + TEXT("; soft labels of the satlas_landcover eye")
		: Ours + TEXT(" for the head; the Satlas backbone by its makers on SatlasPretrain (NAIP public domain, OpenStreetMap and Microsoft Buildings ODbL, ESA WorldCover CC BY 4.0, own annotations ODC-BY) from an ImageNet start");
}

bool FSimCruiseAerialEye::Initialize()
{
	check(IsInGameThread());
	if (bTried)
	{
		return bReady;
	}
	bTried = true;
	FString Choice;
	if (!FParse::Value(FCommandLine::Get(), TEXT("SimCruiseScanEye="), Choice) || Choice.IsEmpty() || Choice.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	if (Choice.Equals(TEXT("satlas_landcover"), ESearchCase::IgnoreCase))
	{
		bStudent = false;
		EyeName = TEXT("satlas_landcover");
		ModelPath = FPaths::ProjectSavedDir() / TEXT("AI/scan-eye-satlas.onnx");
	}
	else
	{
		bStudent = true;
		EyeName = TEXT("aerial_student");
		ModelPath = FPaths::ProjectDir() / TEXT("data/ai/scan-eye-student.onnx");
	}
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ModelPath, FILEREAD_Silent))
	{
		UE_LOG(LogSimCruiseAerialEye, Display, TEXT("Scan eye %s off: no %s"), *EyeName.ToString(), *ModelPath);
		return false;
	}
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
	if (!Runtime.IsValid())
	{
		UE_LOG(LogSimCruiseAerialEye, Display, TEXT("Scan eye off: no NNERuntimeORTCpu"));
		return false;
	}
	TSharedPtr<FSimCruiseAerialEyeModel> M = MakeShared<FSimCruiseAerialEyeModel>();
	M->Data.Reset(NewObject<UNNEModelData>(GetTransientPackage(), NAME_None, RF_Transient));
	M->Data->Init(TEXT("onnx"), Bytes);
	if (Runtime->CanCreateModelCPU(M->Data.Get()) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogSimCruiseAerialEye, Warning, TEXT("Scan eye: the CPU runtime cannot load %s"), *ModelPath);
		return false;
	}
	M->CpuModel = Runtime->CreateModelCPU(M->Data.Get());
	M->Cpu = M->CpuModel.IsValid() ? M->CpuModel->CreateModelInstanceCPU() : nullptr;
	const uint32 Shape[] = {1, 3, InputSize, InputSize};
	if (!M->Cpu.IsValid() || M->Cpu->SetInputTensorShapes({UE::NNE::FTensorShape::Make(Shape)}) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogSimCruiseAerialEye, Warning, TEXT("Scan eye: the model refused a 1x3x%dx%d input"), InputSize, InputSize);
		return false;
	}
	const TConstArrayView<UE::NNE::FTensorShape> Shapes = M->Cpu->GetOutputTensorShapes();
	if (Shapes.Num() != 1 || Shapes[0].Volume() % (uint64(InputSize) * InputSize) != 0)
	{
		UE_LOG(LogSimCruiseAerialEye, Warning, TEXT("Scan eye: unexpected outputs in %s"), *ModelPath);
		return false;
	}
	M->NumClasses = static_cast<int32>(Shapes[0].Volume() / (uint64(InputSize) * InputSize));
	if (M->NumClasses != ChannelNames().Num())
	{
		UE_LOG(LogSimCruiseAerialEye, Warning, TEXT("Scan eye: %d classes, expected %d"), M->NumClasses, ChannelNames().Num());
		return false;
	}
	M->Input.SetNumZeroed(3 * InputSize * InputSize);
	M->Output.SetNumZeroed(static_cast<int32>(Shapes[0].Volume()));
	// Workers decode and encode pictures; the module must be loaded on the game thread.
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	Model = M;
	bReady = true;
	FCoreDelegates::OnPreExit.AddLambda([]() { Get().bStop = true; });
	UE_LOG(LogSimCruiseAerialEye, Display, TEXT("Scan eye ready: %s v%d, %s (%d classes) on NNERuntimeORTCpu"), *EyeName.ToString(), EyeVersion, *ModelPath, M->NumClasses);
	return true;
}

FString FSimCruiseAerialEye::CachePath(const FIntPoint& Tile16) const
{
	return FPaths::ProjectSavedDir() / FString::Printf(TEXT("Scan/eyes/%s-v%d/16/%d/%d.eye"), *EyeName.ToString(), EyeVersion, Tile16.X, Tile16.Y);
}

void FSimCruiseAerialEye::InputsFromPhoto(const TArray<FColor>& Photo, int32 PhotoSize, const FIntPoint& Tile16, FSimCruiseScanInputs& Out)
{
	using namespace SimCruiseAerialEyeLocal;
	Out.TileZXY = FIntVector(16, Tile16.X, Tile16.Y);
	Out.Cells = Cells;
	Out.GuardCells = GuardCells;
	const double Tile = PhotoSize / 4.0;                  // photo pixels per tile
	const double Guard = Tile * GuardCells / Cells;       // photo pixels per guard border
	const double X0 = (((Tile16.X % 4) + 4) % 4) * Tile - Guard, Y0 = (((Tile16.Y % 4) + 4) % 4) * Tile - Guard;
	const double Step = (Tile + 2.0 * Guard) / InputSize;
	// Past the photo's edge the guard mirrors the photo: a clamped edge makes streaks the model reads as
	// roofs or roads, a mirror keeps the ground's own texture.
	auto Mirror = [PhotoSize](double U) { return U < 0.0 ? -U : (U >= PhotoSize ? 2.0 * PhotoSize - U : U); };
	Out.AerialSize = InputSize;
	Out.AerialRGB.SetNumUninitialized(InputSize * InputSize);
	for (int32 Y = 0; Y < InputSize; ++Y)
	{
		for (int32 X = 0; X < InputSize; ++X)
		{
			const FLinearColor C = Sample(Photo, PhotoSize, PhotoSize, Mirror(X0 + (X + 0.5) * Step), Mirror(Y0 + (Y + 0.5) * Step));
			Out.AerialRGB[Y * InputSize + X] = FColor(static_cast<uint8>(C.R + 0.5f), static_cast<uint8>(C.G + 0.5f), static_cast<uint8>(C.B + 0.5f), 255);
		}
	}
}

bool FSimCruiseAerialEye::CanRun(const FSimCruiseScanInputs& Inputs) const
{
	return bReady && Inputs.AerialSize >= 64 && Inputs.AerialRGB.Num() >= Inputs.AerialSize * Inputs.AerialSize && Inputs.Cells > 0;
}

bool FSimCruiseAerialEye::Run(const FSimCruiseScanInputs& Inputs, TArray<FSimCruiseScanChannel>& OutChannels, TArray<FSimCruiseScanPoint>& OutPoints)
{
	using namespace SimCruiseAerialEyeLocal;
	OutPoints.Reset();
	if (!CanRun(Inputs) || IsInGameThread())
	{
		return false;
	}
	FSimCruiseAerialEyeModel& M = *Model;
	// The photo (tile and guard) resampled to the model's input.
	const int32 Plane = InputSize * InputSize;
	const double In = static_cast<double>(Inputs.AerialSize) / InputSize;
	for (int32 Y = 0; Y < InputSize; ++Y)
	{
		for (int32 X = 0; X < InputSize; ++X)
		{
			const FLinearColor C = Inputs.AerialSize == InputSize
				? FLinearColor(Inputs.AerialRGB[Y * InputSize + X].R, Inputs.AerialRGB[Y * InputSize + X].G, Inputs.AerialRGB[Y * InputSize + X].B, 0.f)
				: Sample(Inputs.AerialRGB, Inputs.AerialSize, Inputs.AerialSize, (X + 0.5) * In, (Y + 0.5) * In);
			const int32 I = Y * InputSize + X;
			M.Input[I] = C.R / 255.f;
			M.Input[Plane + I] = C.G / 255.f;
			M.Input[2 * Plane + I] = C.B / 255.f;
		}
	}
	const UE::NNE::FTensorBindingCPU InBinding{M.Input.GetData(), static_cast<uint64>(M.Input.Num() * sizeof(float))};
	const UE::NNE::FTensorBindingCPU OutBinding{M.Output.GetData(), static_cast<uint64>(M.Output.Num() * sizeof(float))};
	if (M.Cpu->RunSync({InBinding}, {OutBinding}) != UE::NNE::EResultStatus::Ok)
	{
		return false;
	}
	// Probabilities to 0-255 channels on the cell grid (tile and guard), bilinear from the model's grid.
	const int32 Size = Inputs.Cells + 2 * Inputs.GuardCells;
	const double Scale = static_cast<double>(InputSize) / Size;
	OutChannels.Reset();
	for (int32 K = 0; K < M.NumClasses; ++K)
	{
		FSimCruiseScanChannel& Ch = OutChannels.AddDefaulted_GetRef();
		Ch.Name = ChannelNames()[K];
		Ch.Size = Size;
		Ch.Data.SetNumUninitialized(Size * Size);
		const float* P = M.Output.GetData() + int64(K) * Plane;
		double Decided = 0.0;
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float V = SamplePlane(P, InputSize, (X + 0.5) * Scale, (Y + 0.5) * Scale);
				Ch.Data[Y * Size + X] = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(V * 255.f), 0, 255));
				Decided += FMath::Max(V, 1.f - V);
			}
		}
		// Trust: how decided the eye is on this tile (mean of max(p, 1 - p)), 0.5..1 mapped to 0..1.
		Ch.Confidence = static_cast<float>(FMath::Clamp((Decided / (double(Size) * Size) - 0.5) * 2.0, 0.0, 1.0));
	}
	return true;
}

void FSimCruiseAerialEye::ComputeTints(const FSimCruiseScanInputs& Inputs, const TArray<FSimCruiseScanChannel>& Channels, TArray<FSimCruiseClassTint>& Out)
{
	using namespace SimCruiseAerialEyeLocal;
	Out.Reset();
	if (Channels.Num() == 0 || Inputs.AerialSize <= 0)
	{
		return;
	}
	const int32 Size = Channels[0].Size;
	const int32 G = FMath::Clamp(Inputs.GuardCells, 0, Size / 4);
	const double In = static_cast<double>(Inputs.AerialSize) / Size;
	const double Interior = double(Size - 2 * G) * (Size - 2 * G);
	for (const FSimCruiseScanChannel& Ch : Channels)
	{
		if (Ch.Size != Size)
		{
			continue;
		}
		// Linear-light mean over the sure cells of the tile's interior (sRGB averaged as light, not as numbers).
		double R = 0.0, Gr = 0.0, B = 0.0, P = 0.0;
		int64 Count = 0;
		for (int32 Y = G; Y < Size - G; ++Y)
		{
			for (int32 X = G; X < Size - G; ++X)
			{
				const uint8 V = Ch.Data[Y * Size + X];
				if (V < SureLevel)
				{
					continue;
				}
				const FColor C = Inputs.AerialRGB[FMath::Min(Inputs.AerialSize - 1, int32((Y + 0.5) * In)) * Inputs.AerialSize + FMath::Min(Inputs.AerialSize - 1, int32((X + 0.5) * In))];
				const FLinearColor L = FLinearColor::FromSRGBColor(C);
				R += L.R; Gr += L.G; B += L.B; P += V / 255.0;
				++Count;
			}
		}
		if (Count == 0)
		{
			continue;
		}
		FSimCruiseClassTint& T = Out.AddDefaulted_GetRef();
		T.Channel = Ch.Name;
		T.Mean = FLinearColor(float(R / Count), float(Gr / Count), float(B / Count)).ToFColorSRGB();
		T.Share = static_cast<float>(Count / Interior);
		T.Confidence = static_cast<float>(FMath::Min(1.0, T.Share / 0.02) * FMath::Clamp((P / Count - 0.5) * 2.0, 0.0, 1.0));
	}
}

void FSimCruiseAerialEye::Request(const FIntPoint& Tile16, const FString& AerialPath)
{
	if (!bReady || bStop || AerialPath.IsEmpty())
	{
		return;
	}
	{
		FScopeLock Guard(&Lock);
		if (Queued.Contains(Tile16) || Recent.Contains(Tile16))
		{
			return;
		}
		Queued.Add(Tile16);
		// Newest first: the streamer asks for the tiles on the car's way last.
		Queue.Insert(TPair<FIntPoint, FString>(Tile16, AerialPath), 0);
		// The car outran a long queue: drop the oldest asks (they are scanned when the car comes back).
		while (Queue.Num() > 48)
		{
			Queued.Remove(Queue.Last().Key);
			Queue.Pop();
		}
		if (bWorkerRunning)
		{
			return;
		}
		bWorkerRunning = true;
	}
	Async(EAsyncExecution::Thread, [this]() { RunWorker(); });
}

void FSimCruiseAerialEye::RunWorker()
{
	if (FRunnableThread* Thread = FRunnableThread::GetRunnableThread())
	{
		Thread->SetThreadPriority(TPri_BelowNormal);
	}
	for (;;)
	{
		TPair<FIntPoint, FString> Job;
		{
			FScopeLock Guard(&Lock);
			if (Queue.Num() == 0 || bStop)
			{
				bWorkerRunning = false;
				return;
			}
			Job = Queue[0];
			Queue.RemoveAt(0);
		}
		const FIntPoint Tile = Job.Key;
		TArray<FSimCruiseScanChannel> Channels;
		TArray<FSimCruiseClassTint> Tints;
		bool bHit = false;
		{
			TArray<uint8> Bytes;
			bHit = FFileHelper::LoadFileToArray(Bytes, *CachePath(Tile), FILEREAD_Silent) && Decode(Bytes, Channels, Tints);
		}
		double Seconds = 0.0;
		bool bOk = bHit;
		if (!bHit)
		{
			FSimCruiseAerialEyeModel& M = *Model;
			if (M.PhotoPath != Job.Value)
			{
				int32 W = 0, H = 0;
				M.Photo.Reset();
				M.PhotoPath.Reset();
				if (USimCruiseWorldResources::DecodeImageFile(Job.Value, M.Photo, W, H) && W == H && W >= 256)
				{
					M.PhotoPath = Job.Value;
					M.PhotoSize = W;
				}
			}
			if (!M.PhotoPath.IsEmpty())
			{
				FSimCruiseScanInputs Inputs;
				InputsFromPhoto(M.Photo, M.PhotoSize, Tile, Inputs);
				TArray<FSimCruiseScanPoint> Points;
				const double T0 = FPlatformTime::Seconds();
				bOk = Run(Inputs, Channels, Points);
				Seconds = FPlatformTime::Seconds() - T0;
				if (bOk)
				{
					ComputeTints(Inputs, Channels, Tints);
					TArray<uint8> Bytes;
					Encode(Channels, Tints, Bytes);
					FFileHelper::SaveArrayToFile(Bytes, *CachePath(Tile));
					UE_LOG(LogSimCruiseAerialEye, Log, TEXT("SCANEYE eye=%s tile=%d,%d ms=%.0f"), *EyeName.ToString(), Tile.X, Tile.Y, Seconds * 1000.0);
					if (CVarSimCruiseEyeDump.GetValueOnAnyThread() > 0)
					{
						DumpPicture(Inputs, Tile, Channels);
					}
				}
			}
		}
		FScopeLock Guard(&Lock);
		Queued.Remove(Tile);
		if (bOk)
		{
			Recent.Add(Tile, FScanned{MoveTemp(Channels), MoveTemp(Tints)});
			RecentOrder.Add(Tile);
			while (RecentOrder.Num() > 16)
			{
				Recent.Remove(RecentOrder[0]);
				RecentOrder.RemoveAt(0);
			}
			if (bHit) { ++CacheHits; }
			else { ++Runs; TotalSeconds += Seconds; MaxSeconds = FMath::Max(MaxSeconds, Seconds); RunTimes.Add(Seconds); }
		}
		else
		{
			++Failures;
		}
	}
}

void FSimCruiseAerialEye::DumpPicture(const FSimCruiseScanInputs& Inputs, const FIntPoint& Tile16, const TArray<FSimCruiseScanChannel>& Channels) const
{
	using namespace SimCruiseAerialEyeLocal;
	if (Channels.Num() != 12 || Inputs.AerialSize <= 0)
	{
		return;
	}
	const int32 S = Channels[0].Size;
	TArray<FColor> Pic;
	Pic.SetNumUninitialized(2 * S * S);
	const double In = static_cast<double>(Inputs.AerialSize) / S;
	for (int32 Y = 0; Y < S; ++Y)
	{
		for (int32 X = 0; X < S; ++X)
		{
			const FLinearColor C = Sample(Inputs.AerialRGB, Inputs.AerialSize, Inputs.AerialSize, (X + 0.5) * In, (Y + 0.5) * In);
			Pic[Y * 2 * S + X] = FColor(static_cast<uint8>(C.R), static_cast<uint8>(C.G), static_cast<uint8>(C.B), 255);
			int32 Best = 0;
			for (int32 K = 1; K < 12; ++K) if (Channels[K].Data[Y * S + X] > Channels[Best].Data[Y * S + X]) Best = K;
			Pic[Y * 2 * S + S + X] = Palette[Best];
		}
	}
	IImageWrapperModule* Wrappers = FModuleManager::GetModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Png = Wrappers ? Wrappers->CreateImageWrapper(EImageFormat::PNG) : nullptr;
	if (Png.IsValid() && Png->SetRaw(Pic.GetData(), Pic.Num() * sizeof(FColor), 2 * S, S, ERGBFormat::BGRA, 8))
	{
		const TArray64<uint8>& Bytes = Png->GetCompressed();
		FFileHelper::SaveArrayToFile(Bytes, *(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Scan/eyes/dump/16-%d-%d.png"), Tile16.X, Tile16.Y)));
	}
}

bool FSimCruiseAerialEye::Lookup(const FIntPoint& Tile16, TArray<FSimCruiseScanChannel>& Out, TArray<FSimCruiseClassTint>* OutTints)
{
	if (!bReady)
	{
		return false;
	}
	{
		FScopeLock Guard(&Lock);
		if (const FScanned* Found = Recent.Find(Tile16))
		{
			Out = Found->Channels;
			if (OutTints) *OutTints = Found->Tints;
			return true;
		}
	}
	TArray<uint8> Bytes;
	TArray<FSimCruiseClassTint> Tints;
	if (!FFileHelper::LoadFileToArray(Bytes, *CachePath(Tile16), FILEREAD_Silent) || !Decode(Bytes, Out, Tints))
	{
		return false;
	}
	if (OutTints) *OutTints = MoveTemp(Tints);
	return true;
}

void FSimCruiseAerialEye::Encode(const TArray<FSimCruiseScanChannel>& Channels, const TArray<FSimCruiseClassTint>& Tints, TArray<uint8>& OutBytes)
{
	TArray<uint8> Raw;
	FMemoryWriter W(Raw);
	uint32 Count = Channels.Num();
	W << Count;
	for (const FSimCruiseScanChannel& C : Channels)
	{
		FString Name = C.Name.ToString();
		int32 Size = C.Size;
		float Conf = C.Confidence;
		W << Name << Size << Conf;
		W.Serialize(const_cast<uint8*>(C.Data.GetData()), C.Data.Num());
	}
	uint32 TintCount = Tints.Num();
	W << TintCount;
	for (const FSimCruiseClassTint& T : Tints)
	{
		FString Name = T.Channel.ToString();
		FColor Mean = T.Mean;
		float Share = T.Share, Conf = T.Confidence;
		W << Name << Mean << Share << Conf;
	}
	const int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
	OutBytes.SetNumUninitialized(12 + Bound);
	int32 Compressed = Bound;
	FCompression::CompressMemory(NAME_Zlib, OutBytes.GetData() + 12, Compressed, Raw.GetData(), Raw.Num());
	const uint32 Header[3] = {SimCruiseAerialEyeLocal::Magic, static_cast<uint32>(FileVersion), static_cast<uint32>(Raw.Num())};
	FMemory::Memcpy(OutBytes.GetData(), Header, 12);
	OutBytes.SetNum(12 + Compressed);
}

bool FSimCruiseAerialEye::Decode(const TArray<uint8>& Bytes, TArray<FSimCruiseScanChannel>& OutChannels, TArray<FSimCruiseClassTint>& OutTints)
{
	OutTints.Reset();
	if (Bytes.Num() < 12)
	{
		return false;
	}
	uint32 Header[3];
	FMemory::Memcpy(Header, Bytes.GetData(), 12);
	if (Header[0] != SimCruiseAerialEyeLocal::Magic || Header[1] != uint32(FileVersion) || Header[2] > 64u * 1024 * 1024)
	{
		return false;
	}
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(Header[2]);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), Raw.Num(), Bytes.GetData() + 12, Bytes.Num() - 12))
	{
		return false;
	}
	FMemoryReader R(Raw);
	uint32 Count = 0;
	R << Count;
	OutChannels.Reset();
	for (uint32 I = 0; I < Count && !R.IsError(); ++I)
	{
		FString Name;
		int32 Size = 0;
		float Conf = 0.f;
		R << Name << Size << Conf;
		if (Size <= 0 || Size > 4096 || R.Tell() + int64(Size) * Size > R.TotalSize())
		{
			return false;
		}
		FSimCruiseScanChannel& C = OutChannels.AddDefaulted_GetRef();
		C.Name = FName(*Name);
		C.Size = Size;
		C.Confidence = Conf;
		C.Data.SetNumUninitialized(Size * Size);
		R.Serialize(C.Data.GetData(), C.Data.Num());
	}
	uint32 TintCount = 0;
	R << TintCount;
	for (uint32 I = 0; I < TintCount && I < 64 && !R.IsError(); ++I)
	{
		FString Name;
		FColor Mean;
		float Share = 0.f, Conf = 0.f;
		R << Name << Mean << Share << Conf;
		OutTints.Add(FSimCruiseClassTint{FName(*Name), Mean, Share, Conf});
	}
	return !R.IsError() && OutChannels.Num() == int32(Count) && OutTints.Num() == int32(TintCount);
}

FString FSimCruiseAerialEye::Describe() const
{
	FScopeLock Guard(&const_cast<FSimCruiseAerialEye*>(this)->Lock);
	TArray<double> Sorted = RunTimes;
	Sorted.Sort();
	const double P95 = Sorted.Num() ? Sorted[FMath::Min(Sorted.Num() - 1, FMath::FloorToInt32(0.95 * Sorted.Num()))] : 0.0;
	return FString::Printf(TEXT("SCANEYE STATS eye=%s ready=%d runs=%d mean_ms=%.0f p95_ms=%.0f max_ms=%.0f cache_hits=%d failures=%d queued=%d"),
		*EyeName.ToString(), bReady ? 1 : 0, Runs, Runs ? 1000.0 * TotalSeconds / Runs : 0.0, 1000.0 * P95, 1000.0 * MaxSeconds, CacheHits, Failures, Queue.Num());
}

static FAutoConsoleCommand GSimCruiseEyeStats(TEXT("SimCruise.Eye.Stats"), TEXT("Logs the scan eye's run statistics."),
	FConsoleCommandDelegate::CreateLambda([]() { UE_LOG(LogSimCruiseAerialEye, Display, TEXT("%s"), *FSimCruiseAerialEye::Get().Describe()); }));
