// Copyright SimCruise contributors. All Rights Reserved.

#include "Live/SimCruiseSceneParser.h"

#include "Async/Async.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseSceneParser, Log, All);

class FSimCruiseSceneModel
{
public:
	TStrongObjectPtr<UNNEModelData> Data;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> Cpu;
	TArray<float> Input;
	TArray<float> Output;
};

FSimCruiseSceneParser& FSimCruiseSceneParser::Get()
{
	// On the heap and never destroyed (see the class comment: ORT at process exit).
	static FSimCruiseSceneParser* Instance = new FSimCruiseSceneParser();
	return *Instance;
}

const TCHAR* FSimCruiseSceneParser::ClassName(ESimCruiseSceneClass Class)
{
	static const TCHAR* Names[] = {TEXT("road"), TEXT("sidewalk"), TEXT("building"), TEXT("wall"), TEXT("fence"), TEXT("pole"), TEXT("traffic light"),
		TEXT("traffic sign"), TEXT("vegetation"), TEXT("terrain"), TEXT("sky"), TEXT("person"), TEXT("rider"), TEXT("car"), TEXT("truck"), TEXT("bus"),
		TEXT("train"), TEXT("motorcycle"), TEXT("bicycle")};
	const int32 Index = static_cast<int32>(Class);
	return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : TEXT("?");
}

bool FSimCruiseSceneParser::Initialize()
{
	check(IsInGameThread());
	if (bTried)
	{
		return bReady;
	}
	bTried = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("SimCruiseNoNeural")))
	{
		return false;
	}
	const FString Path = FPaths::ProjectDir() / TEXT("data/ai/ddrnet23-slim-cityscapes.onnx");
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		UE_LOG(LogSimCruiseSceneParser, Display, TEXT("Scene parser off: no %s"), *Path);
		return false;
	}
	TWeakInterfacePtr<INNERuntimeCPU> Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
	if (!Runtime.IsValid())
	{
		UE_LOG(LogSimCruiseSceneParser, Display, TEXT("Scene parser off: no NNERuntimeORTCpu"));
		return false;
	}
	TSharedPtr<FSimCruiseSceneModel> M = MakeShared<FSimCruiseSceneModel>();
	M->Data.Reset(NewObject<UNNEModelData>(GetTransientPackage(), NAME_None, RF_Transient));
	M->Data->Init(TEXT("onnx"), Bytes);
	if (Runtime->CanCreateModelCPU(M->Data.Get()) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogSimCruiseSceneParser, Warning, TEXT("Scene parser: the CPU runtime cannot load the model"));
		return false;
	}
	M->CpuModel = Runtime->CreateModelCPU(M->Data.Get());
	M->Cpu = M->CpuModel.IsValid() ? M->CpuModel->CreateModelInstanceCPU() : nullptr;
	const uint32 Shape[] = {1, 3, static_cast<uint32>(InputHeight), static_cast<uint32>(InputWidth)};
	if (!M->Cpu.IsValid() || M->Cpu->SetInputTensorShapes({UE::NNE::FTensorShape::Make(Shape)}) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogSimCruiseSceneParser, Warning, TEXT("Scene parser: the model refused a 1x3x%dx%d input"), InputHeight, InputWidth);
		return false;
	}
	M->Input.SetNumZeroed(3 * InputWidth * InputHeight);
	M->Output.SetNumZeroed(static_cast<int32>(ESimCruiseSceneClass::Count) * (InputWidth / OutputScale) * (InputHeight / OutputScale));
	Model = M;
	// Workers decode and encode pictures; a module may only be loaded on the game thread.
	ImageWrappers = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	bReady = true;
	UE_LOG(LogSimCruiseSceneParser, Display, TEXT("Scene parser ready: DDRNet23-Slim (Cityscapes, 19 classes) on NNERuntimeORTCpu"));
	return true;
}

bool FSimCruiseSceneParser::Parse(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutLabels, int32& OutWidth, int32& OutHeight, double* OutSeconds)
{
	if (!bReady || !Model.IsValid() || Width < 8 || Height < 8 || Pixels.Num() < Width * Height)
	{
		return false;
	}
	// Never on the game thread: a parse is seconds of CPU, and the first one ever, run on the
	// game thread of a commandlet, hung inside ONNX Runtime for an hour. Game-thread callers use
	// ParseWithTimeout, which runs it on a worker.
	if (IsInGameThread())
	{
		UE_LOG(LogSimCruiseSceneParser, Warning, TEXT("Scene parser: Parse on the game thread refused (use ParseWithTimeout)"));
		return false;
	}
	// One run at a time; a caller that finds the parser busy does without rather than waits.
	if (!RunLock.TryLock())
	{
		return false;
	}
	ON_SCOPE_EXIT { RunLock.Unlock(); };
	const double Start = FPlatformTime::Seconds();
	// Letterbox: the photograph scaled to fit at the top left, the rest mid grey.
	const double Scale = FMath::Min(double(InputWidth) / Width, double(InputHeight) / Height);
	const int32 SW = FMath::Clamp(FMath::RoundToInt(Width * Scale), 1, InputWidth);
	const int32 SH = FMath::Clamp(FMath::RoundToInt(Height * Scale), 1, InputHeight);
	const int32 Plane = InputWidth * InputHeight;
	float* In = Model->Input.GetData();
	for (int32 y = 0; y < InputHeight; ++y)
	{
		const int32 Sy = FMath::Min(static_cast<int32>(y / Scale), Height - 1);
		for (int32 x = 0; x < InputWidth; ++x)
		{
			const int32 I = y * InputWidth + x;
			if (x >= SW || y >= SH)
			{
				In[I] = In[Plane + I] = In[2 * Plane + I] = 0.5f;
				continue;
			}
			const FColor& C = Pixels[Sy * Width + FMath::Min(static_cast<int32>(x / Scale), Width - 1)];
			In[I] = C.R / 255.0f;
			In[Plane + I] = C.G / 255.0f;
			In[2 * Plane + I] = C.B / 255.0f;
		}
	}
	const UE::NNE::FTensorBindingCPU InBinding{Model->Input.GetData(), static_cast<uint64>(Model->Input.Num() * sizeof(float))};
	const UE::NNE::FTensorBindingCPU OutBinding{Model->Output.GetData(), static_cast<uint64>(Model->Output.Num() * sizeof(float))};
	if (Model->Cpu->RunSync({InBinding}, {OutBinding}) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogSimCruiseSceneParser, Warning, TEXT("Scene parser: inference failed"));
		return false;
	}
	// The class with the highest score in each cell of the photograph's part of the grid.
	const int32 GW = InputWidth / OutputScale, GH = InputHeight / OutputScale, GPlane = GW * GH;
	OutWidth = FMath::Clamp(FMath::DivideAndRoundUp(SW, OutputScale), 1, GW);
	OutHeight = FMath::Clamp(FMath::DivideAndRoundUp(SH, OutputScale), 1, GH);
	OutLabels.SetNumUninitialized(OutWidth * OutHeight);
	const float* Out = Model->Output.GetData();
	for (int32 y = 0; y < OutHeight; ++y)
	{
		for (int32 x = 0; x < OutWidth; ++x)
		{
			const int32 G = y * GW + x;
			int32 Best = 0;
			float BestScore = Out[G];
			for (int32 C = 1; C < static_cast<int32>(ESimCruiseSceneClass::Count); ++C)
			{
				if (Out[C * GPlane + G] > BestScore)
				{
					BestScore = Out[C * GPlane + G];
					Best = C;
				}
			}
			OutLabels[y * OutWidth + x] = static_cast<uint8>(Best);
		}
	}
	if (OutSeconds)
	{
		*OutSeconds = FPlatformTime::Seconds() - Start;
	}
	return true;
}

bool FSimCruiseSceneParser::ParseWithTimeout(const TArray<FColor>& Pixels, int32 Width, int32 Height, TArray<uint8>& OutLabels, int32& OutWidth, int32& OutHeight,
	double TimeoutSeconds)
{
	if (!bReady || bTimedOut)
	{
		return false;
	}
	struct FJob
	{
		TArray<FColor> Pixels;
		TArray<uint8> Labels;
		int32 W = 0, H = 0, GW = 0, GH = 0;
		bool bOk = false;
	};
	TSharedRef<FJob> Job = MakeShared<FJob>();
	Job->Pixels = Pixels;
	Job->W = Width;
	Job->H = Height;
	TFuture<void> Done = Async(EAsyncExecution::Thread, [Job]()
	{
		Job->bOk = FSimCruiseSceneParser::Get().Parse(Job->Pixels, Job->W, Job->H, Job->Labels, Job->GW, Job->GH);
	});
	if (!Done.WaitFor(FTimespan::FromSeconds(TimeoutSeconds)))
	{
		// The worker keeps the job alive; the parser is not used again in this run.
		bTimedOut = true;
		UE_LOG(LogSimCruiseSceneParser, Warning, TEXT("Scene parser: no answer in %.0f s; off for the rest of this run"), TimeoutSeconds);
		return false;
	}
	if (!Job->bOk)
	{
		return false;
	}
	OutLabels = MoveTemp(Job->Labels);
	OutWidth = Job->GW;
	OutHeight = Job->GH;
	return true;
}

ESimCruiseSceneClass FSimCruiseSceneParser::ClassAt(const TArray<uint8>& Labels, int32 GridWidth, int32 GridHeight, int32 X, int32 Y, int32 Width, int32 Height)
{
	const int32 Gx = FMath::Clamp(X * GridWidth / FMath::Max(Width, 1), 0, GridWidth - 1);
	const int32 Gy = FMath::Clamp(Y * GridHeight / FMath::Max(Height, 1), 0, GridHeight - 1);
	const int32 I = Gy * GridWidth + Gx;
	return Labels.IsValidIndex(I) ? static_cast<ESimCruiseSceneClass>(Labels[I]) : ESimCruiseSceneClass::Count;
}

TArray<float> FSimCruiseSceneParser::Shares(const TArray<uint8>& Labels)
{
	TArray<float> Out;
	Out.SetNumZeroed(static_cast<int32>(ESimCruiseSceneClass::Count));
	for (const uint8 L : Labels)
	{
		if (L < Out.Num())
		{
			Out[L] += 1.0f;
		}
	}
	for (float& S : Out)
	{
		S /= FMath::Max(Labels.Num(), 1);
	}
	return Out;
}

namespace SimCruiseSceneParserPrivate
{
	/** Cityscapes' own label colours, for looking at a parse. */
	const FColor Palette[] = {
		FColor(128, 64, 128), FColor(244, 35, 232), FColor(70, 70, 70), FColor(102, 102, 156), FColor(190, 153, 153), FColor(153, 153, 153),
		FColor(250, 170, 30), FColor(220, 220, 0), FColor(107, 142, 35), FColor(152, 251, 152), FColor(70, 130, 180), FColor(220, 20, 60),
		FColor(255, 0, 0), FColor(0, 0, 142), FColor(0, 0, 70), FColor(0, 60, 100), FColor(0, 80, 100), FColor(0, 0, 230), FColor(119, 11, 32)};

	FAutoConsoleCommand GParse(
		TEXT("SimCruise.AI.Parse"),
		TEXT("SimCruise.AI.Parse <image file>: label a street photograph with the local scene parser (on a worker); writes <name>-parse.png (photo | labels) under Saved/SceneParse"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.IsEmpty() || !FSimCruiseSceneParser::Get().Initialize())
			{
				UE_LOG(LogSimCruiseSceneParser, Warning, TEXT("SimCruise.AI.Parse <image file> (parser ready: %d)"), FSimCruiseSceneParser::Get().IsReady() ? 1 : 0);
				return;
			}
			const FString File = Args[0];
			Async(EAsyncExecution::Thread, [File]() { FSimCruiseSceneParser::Get().ParseFileToPicture(File); });
		}));
}

bool FSimCruiseSceneParser::ParseFileToPicture(const FString& File)
{
	using namespace SimCruiseSceneParserPrivate;
	if (!bReady || !ImageWrappers)
	{
		return false;
	}
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *File))
	{
		return false;
	}
	const EImageFormat Format = ImageWrappers->DetectImageFormat(Bytes.GetData(), Bytes.Num());
	TSharedPtr<IImageWrapper> Reader = Format != EImageFormat::Invalid ? ImageWrappers->CreateImageWrapper(Format) : nullptr;
	TArray<uint8> Raw;
	if (!Reader || !Reader->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Reader->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return false;
	}
	const int32 W = Reader->GetWidth(), H = Reader->GetHeight();
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(W * H);
	FMemory::Memcpy(Pixels.GetData(), Raw.GetData(), FMath::Min<int64>(Raw.Num(), Pixels.Num() * 4));
	TArray<uint8> Labels;
	int32 GW = 0, GH = 0;
	double Seconds = 0.0;
	if (!Parse(Pixels, W, H, Labels, GW, GH, &Seconds))
	{
		return false;
	}
	TArray<float> ClassShares = Shares(Labels);
	FString Line;
	for (int32 C = 0; C < ClassShares.Num(); ++C)
	{
		if (ClassShares[C] >= 0.01f)
		{
			Line += FString::Printf(TEXT(" %s %.0f%%"), ClassName(static_cast<ESimCruiseSceneClass>(C)), ClassShares[C] * 100.0f);
		}
	}
	UE_LOG(LogSimCruiseSceneParser, Display, TEXT("Parse %s (%dx%d) in %.2f s:%s"), *FPaths::GetCleanFilename(File), W, H, Seconds, *Line);
	// Photo on the left, its labels blended over it on the right.
	TArray<FColor> Out;
	Out.SetNumUninitialized(2 * W * H);
	for (int32 y = 0; y < H; ++y)
	{
		for (int32 x = 0; x < W; ++x)
		{
			const FColor P = Pixels[y * W + x];
			const int32 L = static_cast<int32>(ClassAt(Labels, GW, GH, x, y, W, H));
			const FColor C = L < UE_ARRAY_COUNT(Palette) ? Palette[L] : FColor::Black;
			Out[y * 2 * W + x] = FColor(P.R, P.G, P.B, 255);
			Out[y * 2 * W + W + x] = FColor((P.R + 3 * C.R) / 4, (P.G + 3 * C.G) / 4, (P.B + 3 * C.B) / 4, 255);
		}
	}
	TSharedPtr<IImageWrapper> Png = ImageWrappers->CreateImageWrapper(EImageFormat::PNG);
	if (Png && Png->SetRaw(Out.GetData(), Out.Num() * sizeof(FColor), 2 * W, H, ERGBFormat::BGRA, 8))
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("SceneParse") / (FPaths::GetBaseFilename(File) + TEXT("-parse.png"));
		FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path);
		UE_LOG(LogSimCruiseSceneParser, Display, TEXT("Parse picture: %s"), *Path);
	}
	return true;
}
