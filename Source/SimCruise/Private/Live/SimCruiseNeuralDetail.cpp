// Copyright SimCruise contributors. All Rights Reserved.

#include "Live/SimCruiseNeuralDetail.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "NNERuntimeGPU.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "World/SimCruiseWorldResources.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseNeural, Log, All);

class FSimCruiseNeuralModel
{
public:
	TStrongObjectPtr<UNNEModelData> Data;
	TSharedPtr<UE::NNE::IModelGPU> GpuModel;
	TSharedPtr<UE::NNE::IModelInstanceGPU> Gpu;
	TSharedPtr<UE::NNE::IModelCPU> CpuModel;
	TSharedPtr<UE::NNE::IModelInstanceCPU> Cpu;

	UE::NNE::IModelInstanceRunSync* Instance() const
	{
		return Gpu.IsValid() ? static_cast<UE::NNE::IModelInstanceRunSync*>(Gpu.Get()) : static_cast<UE::NNE::IModelInstanceRunSync*>(Cpu.Get());
	}
};

namespace SimCruiseNeuralPrivate
{
	FAutoConsoleCommand GBench(
		TEXT("SimCruise.Neural.Bench"),
		TEXT("SimCruise.Neural.Bench [image file]: run the AI detail engine on one aerial photograph (no cache) and log the time"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			FSimCruiseNeuralDetail& Neural = FSimCruiseNeuralDetail::Get();
			if (!Neural.Initialize())
			{
				UE_LOG(LogSimCruiseNeural, Warning, TEXT("AI detail engine not available: %s"), *Neural.Describe());
				return;
			}
			const FString Path = Args.Num() > 0 ? Args[0] : FPaths::ProjectDir() / TEXT("data/tiles/aerial/a-4545-6976.jpg");
			TArray<FColor> In, Out;
			int32 W = 0, H = 0, OW = 0, OH = 0;
			double Seconds = 0.0;
			if (!USimCruiseWorldResources::DecodeImageFile(Path, In, W, H) || !Neural.Upscale(In, W, H, Out, OW, OH, Seconds))
			{
				UE_LOG(LogSimCruiseNeural, Warning, TEXT("Bench failed on %s"), *Path);
				return;
			}
			UE_LOG(LogSimCruiseNeural, Display, TEXT("Bench: %s %dx%d -> %dx%d in %.2f s (%d windows, %.1f ms each). %s"), *FPaths::GetCleanFilename(Path), W, H, OW, OH,
				Seconds, FSimCruiseNeuralDetail::WindowCount(W, H), Seconds * 1000.0 / FMath::Max(FSimCruiseNeuralDetail::WindowCount(W, H), 1), *Neural.Describe());
			IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> Jpeg = Wrappers.CreateImageWrapper(EImageFormat::JPEG);
			if (Jpeg && Jpeg->SetRaw(Out.GetData(), Out.Num() * sizeof(FColor), OW, OH, ERGBFormat::BGRA, 8))
			{
				const FString Saved = FPaths::ProjectSavedDir() / TEXT("NeuralDetail") / (FPaths::GetBaseFilename(Path) + TEXT("-bench.jpg"));
				FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(95), *Saved);
				UE_LOG(LogSimCruiseNeural, Display, TEXT("Bench result: %s"), *Saved);
			}
		}));
}

FSimCruiseNeuralDetail& FSimCruiseNeuralDetail::Get()
{
	static FSimCruiseNeuralDetail Instance;
	return Instance;
}

int32 FSimCruiseNeuralDetail::WindowCount(int32 Width, int32 Height)
{
	const int32 Core = Window - 2 * Overlap;
	return FMath::DivideAndRoundUp(Width, Core) * FMath::DivideAndRoundUp(Height, Core);
}

bool FSimCruiseNeuralDetail::Initialize()
{
	check(IsInGameThread());
	if (bTried)
	{
		return bReady;
	}
	bTried = true;
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("SimCruiseNoNeural")))
	{
		RuntimeName = TEXT("off (-SimCruiseNoNeural)");
		return false;
	}
	int32 Scale = OutputScale;
	if (FParse::Value(Cmd, TEXT("SimCruiseNeuralScale="), Scale) && (Scale == 1 || Scale == 2 || Scale == 4))
	{
		OutputScale = Scale;
	}

	const FString ModelPath = FPaths::ProjectDir() / TEXT("data/ai/real-esrgan-general-x4v3.onnx");
	TArray64<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *ModelPath))
	{
		RuntimeName = TEXT("no model file");
		UE_LOG(LogSimCruiseNeural, Display, TEXT("AI detail engine off: no %s"), *ModelPath);
		return false;
	}

	Model = MakeShared<FSimCruiseNeuralModel>();
	Model->Data.Reset(NewObject<UNNEModelData>(GetTransientPackage(), NAME_None, RF_Transient));
	Model->Data->Init(TEXT("onnx"), Bytes);

	// Where it runs: the CPU and system RAM by default, so the power-capped GPU keeps every
	// watt for the frame (the work is in the background either way); -SimCruiseNeuralDevice=gpu
	// uses DirectML instead. Either falls back to the other.
	FString Device = TEXT("cpu");
	FParse::Value(Cmd, TEXT("SimCruiseNeuralDevice="), Device);
	auto TryGpu = [this]()
	{
		if (TWeakInterfacePtr<INNERuntimeGPU> Gpu = UE::NNE::GetRuntime<INNERuntimeGPU>(TEXT("NNERuntimeORTDml")); Gpu.IsValid()
			&& Gpu->CanCreateModelGPU(Model->Data.Get()) == INNERuntimeGPU::ECanCreateModelGPUStatus::Ok)
		{
			Model->GpuModel = Gpu->CreateModelGPU(Model->Data.Get());
			Model->Gpu = Model->GpuModel.IsValid() ? Model->GpuModel->CreateModelInstanceGPU() : nullptr;
			RuntimeName = TEXT("NNERuntimeORTDml (GPU)");
		}
	};
	auto TryCpu = [this]()
	{
		if (TWeakInterfacePtr<INNERuntimeCPU> Cpu = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu")); Cpu.IsValid()
			&& Cpu->CanCreateModelCPU(Model->Data.Get()) == INNERuntimeCPU::ECanCreateModelCPUStatus::Ok)
		{
			Model->CpuModel = Cpu->CreateModelCPU(Model->Data.Get());
			Model->Cpu = Model->CpuModel.IsValid() ? Model->CpuModel->CreateModelInstanceCPU() : nullptr;
			RuntimeName = TEXT("NNERuntimeORTCpu (CPU and RAM)");
		}
	};
	if (Device.Equals(TEXT("gpu"), ESearchCase::IgnoreCase))
	{
		TryGpu();
		if (!Model->Gpu.IsValid()) { TryCpu(); }
	}
	else
	{
		TryCpu();
		if (!Model->Cpu.IsValid()) { TryGpu(); }
	}
	UE::NNE::IModelInstanceRunSync* Instance = Model->Instance();
	if (!Instance)
	{
		UE_LOG(LogSimCruiseNeural, Display, TEXT("AI detail engine off: no NNE GPU runtime (runtimes: %s)"),
			*FString::Join(UE::NNE::GetAllRuntimeNames(), TEXT(", ")));
		RuntimeName = TEXT("no GPU runtime");
		Model.Reset();
		return false;
	}
	const uint32 Shape[] = {1, 3, static_cast<uint32>(Window), static_cast<uint32>(Window)};
	if (Instance->SetInputTensorShapes({UE::NNE::FTensorShape::Make(Shape)}) != UE::NNE::EResultStatus::Ok)
	{
		UE_LOG(LogSimCruiseNeural, Warning, TEXT("AI detail engine: the model refused a 1x3x%dx%d input"), Window, Window);
		Model.Reset();
		return false;
	}
	bReady = true;
	UE_LOG(LogSimCruiseNeural, Display, TEXT("AI detail engine ready: Real-ESRGAN General x4v3 on %s, aerial photographs kept at %dx."), *RuntimeName, OutputScale);
	return true;
}

bool FSimCruiseNeuralDetail::Upscale(const TArray<FColor>& In, int32 Width, int32 Height, TArray<FColor>& Out, int32& OutWidth, int32& OutHeight, double& OutSeconds)
{
	if (!bReady || !Model.IsValid() || Width <= 0 || Height <= 0 || In.Num() != Width * Height)
	{
		return false;
	}
	FScopeLock Guard(&RunLock);
	UE::NNE::IModelInstanceRunSync* Instance = Model->Instance();
	const int32 Core = Window - 2 * Overlap;
	const int32 OW = Width * ModelScale, OH = Height * ModelScale, OutWindow = Window * ModelScale;
	TArray<FColor> Full;
	Full.SetNumUninitialized(OW * OH);
	TArray<float> Input, Output;
	Input.SetNumUninitialized(3 * Window * Window);
	Output.SetNumUninitialized(3 * OutWindow * OutWindow);
	const int32 Plane = Window * Window, OutPlane = OutWindow * OutWindow;

	const double Start = FPlatformTime::Seconds();
	for (int32 CY = 0; CY < Height; CY += Core)
	{
		for (int32 CX = 0; CX < Width; CX += Core)
		{
			const int32 SX = CX - Overlap, SY = CY - Overlap;
			for (int32 Y = 0; Y < Window; ++Y)
			{
				const int32 IY = FMath::Clamp(SY + Y, 0, Height - 1);
				for (int32 X = 0; X < Window; ++X)
				{
					const FColor& C = In[IY * Width + FMath::Clamp(SX + X, 0, Width - 1)];
					const int32 I = Y * Window + X;
					Input[I] = C.R / 255.0f;
					Input[Plane + I] = C.G / 255.0f;
					Input[2 * Plane + I] = C.B / 255.0f;
				}
			}
			const UE::NNE::FTensorBindingCPU InBinding{Input.GetData(), static_cast<uint64>(Input.Num() * sizeof(float))};
			const UE::NNE::FTensorBindingCPU OutBinding{Output.GetData(), static_cast<uint64>(Output.Num() * sizeof(float))};
			const double RunStart = FPlatformTime::Seconds();
			if (Instance->RunSync({InBinding}, {OutBinding}) != UE::NNE::EResultStatus::Ok)
			{
				UE_LOG(LogSimCruiseNeural, Warning, TEXT("AI detail engine: inference failed"));
				return false;
			}
			if (Duty < 0.999f && !IsInGameThread() && Model->Gpu.IsValid())
			{
				// Leave the GPU to the frame between windows.
				FPlatformProcess::Sleep(static_cast<float>((FPlatformTime::Seconds() - RunStart) * (1.0 / FMath::Max(Duty, 0.05f) - 1.0)));
			}
			// Keep the core of the window: the overlap hides the network's edge effects.
			const int32 CoreW = FMath::Min(Core, Width - CX), CoreH = FMath::Min(Core, Height - CY);
			for (int32 Y = 0; Y < CoreH * ModelScale; ++Y)
			{
				const int32 WY = Overlap * ModelScale + Y;
				FColor* Row = &Full[(CY * ModelScale + Y) * OW + CX * ModelScale];
				for (int32 X = 0; X < CoreW * ModelScale; ++X)
				{
					const int32 I = WY * OutWindow + Overlap * ModelScale + X;
					Row[X] = FColor(
						static_cast<uint8>(FMath::Clamp(Output[I] * 255.0f + 0.5f, 0.0f, 255.0f)),
						static_cast<uint8>(FMath::Clamp(Output[OutPlane + I] * 255.0f + 0.5f, 0.0f, 255.0f)),
						static_cast<uint8>(FMath::Clamp(Output[2 * OutPlane + I] * 255.0f + 0.5f, 0.0f, 255.0f)), 255);
				}
			}
		}
	}
	OutSeconds = FPlatformTime::Seconds() - Start;
	TotalSeconds += OutSeconds;
	Images++;

	const int32 Factor = ModelScale / FMath::Clamp(OutputScale, 1, ModelScale);
	if (Factor > 1)
	{
		Downsample(Full, OW, OH, Factor, Out);
		OutWidth = OW / Factor;
		OutHeight = OH / Factor;
	}
	else
	{
		Out = MoveTemp(Full);
		OutWidth = OW;
		OutHeight = OH;
	}
	return true;
}

void FSimCruiseNeuralDetail::Downsample(const TArray<FColor>& In, int32 Width, int32 Height, int32 Factor, TArray<FColor>& Out)
{
	const int32 W = Width / Factor, H = Height / Factor, N = Factor * Factor;
	Out.SetNumUninitialized(W * H);
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			uint32 R = 0, G = 0, B = 0, A = 0;
			for (int32 DY = 0; DY < Factor; ++DY)
			{
				const FColor* Row = &In[(Y * Factor + DY) * Width + X * Factor];
				for (int32 DX = 0; DX < Factor; ++DX)
				{
					R += Row[DX].R; G += Row[DX].G; B += Row[DX].B; A += Row[DX].A;
				}
			}
			Out[Y * W + X] = FColor((R + N / 2) / N, (G + N / 2) / N, (B + N / 2) / N, (A + N / 2) / N);
		}
	}
}

FString FSimCruiseNeuralDetail::CachePath(const FString& SourcePath) const
{
	// The source file's size and time stamp are in the name: a new photograph is enhanced again.
	const FFileStatData Stat = IFileManager::Get().GetStatData(*SourcePath);
	return FPaths::ProjectSavedDir() / TEXT("NeuralDetail") / FString::Printf(TEXT("%s-x%d-%llx.jpg"), *FPaths::GetBaseFilename(SourcePath), OutputScale,
		static_cast<unsigned long long>(Stat.FileSize ^ Stat.ModificationTime.GetTicks()));
}

bool FSimCruiseNeuralDetail::EnhanceAerial(const FString& SourcePath, TArray<FColor>& Pixels, int32& Width, int32& Height)
{
	if (!bReady || Pixels.IsEmpty() || OutputScale <= 1)
	{
		return false;
	}
	const FString Cached = CachePath(SourcePath);
	TArray<FColor> Enhanced;
	int32 W = 0, H = 0;
	if (IFileManager::Get().FileExists(*Cached) && USimCruiseWorldResources::DecodeImageFile(Cached, Enhanced, W, H) && W == Width * OutputScale && H == Height * OutputScale)
	{
		Pixels = MoveTemp(Enhanced);
		Width = W;
		Height = H;
		return true;
	}
	FScopeLock Guard(&QueueLock);
	if (!Queued.Contains(SourcePath))
	{
		Queued.Add(SourcePath);
		Queue.Add(SourcePath);
		if (!bWorkerRunning)
		{
			bWorkerRunning = true;
			Async(EAsyncExecution::Thread, [this]() { RunWorker(); });
		}
	}
	return false;
}

void FSimCruiseNeuralDetail::RunWorker()
{
	for (;;)
	{
		FString Path;
		{
			FScopeLock Guard(&QueueLock);
			if (Queue.IsEmpty())
			{
				bWorkerRunning = false;
				return;
			}
			// Newest first: the photograph the car has just reached matters most.
			Path = Queue.Pop();
		}
		Process(Path);
		FScopeLock Guard(&QueueLock);
		Queued.Remove(Path);
	}
}

void FSimCruiseNeuralDetail::Process(const FString& SourcePath)
{
	TArray<FColor> In, Out;
	int32 W = 0, H = 0, OW = 0, OH = 0;
	double Seconds = 0.0;
	const double Start = FPlatformTime::Seconds();
	if (!USimCruiseWorldResources::DecodeImageFile(SourcePath, In, W, H) || !Upscale(In, W, H, Out, OW, OH, Seconds))
	{
		return;
	}
	UE_LOG(LogSimCruiseNeural, Display, TEXT("AI detail: %s %dx%d -> %dx%d, %.2f s on the GPU in %.1f s at %.0f %% duty"), *FPaths::GetCleanFilename(SourcePath),
		W, H, OW, OH, Seconds, FPlatformTime::Seconds() - Start, Duty * 100.0f);
	IImageWrapperModule& Wrappers = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Jpeg = Wrappers.CreateImageWrapper(EImageFormat::JPEG);
	if (Jpeg && Jpeg->SetRaw(Out.GetData(), Out.Num() * sizeof(FColor), OW, OH, ERGBFormat::BGRA, 8))
	{
		FFileHelper::SaveArrayToFile(Jpeg->GetCompressed(92), *CachePath(SourcePath));
	}
	const FString Key = FPaths::GetCleanFilename(SourcePath);
	AsyncTask(ENamedThreads::GameThread, [this, Key, Pixels = MoveTemp(Out), OW, OH]()
	{
		if (OnEnhanced)
		{
			OnEnhanced(Key, Pixels, OW, OH);
		}
	});
}

FString FSimCruiseNeuralDetail::Describe() const
{
	return FString::Printf(TEXT("AI detail engine: %s, %d photographs, %.1f s of inference in total"), RuntimeName.IsEmpty() ? TEXT("not started") : *RuntimeName,
		Images, TotalSeconds);
}
