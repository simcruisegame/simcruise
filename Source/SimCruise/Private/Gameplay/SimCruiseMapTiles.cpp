// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseMapTiles.h"

#include "Async/Async.h"
#include "Atlas/SimCruiseAtlasClient.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "Layout/Geometry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"
#include "World/SimCruiseTileData.h"
#include <atomic>

namespace
{
	constexpr int32 MapPx = 512;
	constexpr double MapUnits = 4096.0;

	// The guide-sign palette: dark land so the magenta course and amber stops stand out.
	const FColor Land(0x1B, 0x24, 0x22);
	const FColor Water(0x1D, 0x3A, 0x48);
	const FColor Green(0x21, 0x33, 0x2A);

	struct FRaster
	{
		TArray<FColor> Px;
		FRaster() { Px.Init(Land, MapPx * MapPx); }

		void Blend(int32 X, int32 Y, const FColor& C, float A)
		{
			if (X < 0 || Y < 0 || X >= MapPx || Y >= MapPx || A <= 0.f) return;
			FColor& D = Px[Y * MapPx + X];
			A = FMath::Min(A, 1.f);
			D.R = static_cast<uint8>(FMath::Lerp(static_cast<float>(D.R), static_cast<float>(C.R), A));
			D.G = static_cast<uint8>(FMath::Lerp(static_cast<float>(D.G), static_cast<float>(C.G), A));
			D.B = static_cast<uint8>(FMath::Lerp(static_cast<float>(D.B), static_cast<float>(C.B), A));
		}

		void Fill(const TArray<TArray<FVector2D>>& Rings, const FColor& C)
		{
			TArray<double> Cross;
			for (int32 Y = 0; Y < MapPx; ++Y)
			{
				const double V = (Y + 0.5) * MapUnits / MapPx;
				Cross.Reset();
				for (const TArray<FVector2D>& Ring : Rings)
				{
					for (int32 i = 0, n = Ring.Num(); i < n; ++i)
					{
						const FVector2D& A = Ring[i];
						const FVector2D& B = Ring[(i + 1) % n];
						if ((A.Y <= V) != (B.Y <= V)) Cross.Add(A.X + (V - A.Y) / (B.Y - A.Y) * (B.X - A.X));
					}
				}
				Cross.Sort();
				for (int32 k = 0; k + 1 < Cross.Num(); k += 2)
				{
					const int32 X0 = FMath::Max(0, FMath::RoundToInt(Cross[k] * MapPx / MapUnits));
					const int32 X1 = FMath::Min(MapPx, FMath::RoundToInt(Cross[k + 1] * MapPx / MapUnits));
					for (int32 X = X0; X < X1; ++X) Blend(X, Y, C, 1.f);
				}
			}
		}

		void Line(const TArray<FVector2D>& Pts, double WidthUnits, const FColor& C)
		{
			const double K = MapPx / MapUnits;
			const double Half = FMath::Max(WidthUnits * K * 0.5, 0.45);
			for (int32 i = 0; i + 1 < Pts.Num(); ++i)
			{
				const FVector2D A = Pts[i] * K, B = Pts[i + 1] * K;
				const int32 X0 = FMath::FloorToInt(FMath::Min(A.X, B.X) - Half - 1), X1 = FMath::CeilToInt(FMath::Max(A.X, B.X) + Half + 1);
				const int32 Y0 = FMath::FloorToInt(FMath::Min(A.Y, B.Y) - Half - 1), Y1 = FMath::CeilToInt(FMath::Max(A.Y, B.Y) + Half + 1);
				const FVector2D AB = B - A;
				const double L2 = FMath::Max(AB.SizeSquared(), 1e-9);
				for (int32 Y = FMath::Max(Y0, 0); Y <= FMath::Min(Y1, MapPx - 1); ++Y)
				{
					for (int32 X = FMath::Max(X0, 0); X <= FMath::Min(X1, MapPx - 1); ++X)
					{
						const FVector2D P(X + 0.5, Y + 0.5);
						const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0, 1.0);
						const double D = FVector2D::Distance(P, A + AB * T);
						const float Cover = static_cast<float>(FMath::Clamp(Half + 0.5 - D, 0.0, 1.0));
						if (Cover > 0.f && Px[Y * MapPx + X] != C) Blend(X, Y, C, Cover);
					}
				}
			}
		}
	};

	bool ReadPoints(const TSharedPtr<FJsonValue>& Value, TArray<FVector2D>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
		if (!Value.IsValid() || !Value->TryGetArray(Points)) return false;
		for (const TSharedPtr<FJsonValue>& P : *Points)
		{
			const TArray<TSharedPtr<FJsonValue>>* XY = nullptr;
			if (P.IsValid() && P->TryGetArray(XY) && XY->Num() >= 2) Out.Add(FVector2D((*XY)[0]->AsNumber(), (*XY)[1]->AsNumber()));
		}
		return Out.Num() > 0;
	}

	FString Key(const FIntVector& Id) { return FString::Printf(TEXT("%d_%d_%d"), Id.Z, Id.X, Id.Y); }

	const FSimCruiseAtlasConfig& HelperConfig()
	{
		static const FSimCruiseAtlasConfig Config = FSimCruiseAtlasConfig::ResolveDefault();
		return Config;
	}
}

struct FSimCruiseMapTiles::FFetch
{
	FString Key;
	FIntVector Id;
	std::atomic<bool> bDone{ false };
	std::atomic<bool> bCancel{ false };
	bool bOk = false;
	TArray<FColor> Pixels;
	TArray<FLabel> Names;
};

FSimCruiseMapTiles::~FSimCruiseMapTiles()
{
	if (Fetch.IsValid()) Fetch->bCancel = true;
}

bool FSimCruiseMapTiles::Raster(const FString& Raw, TArray<FColor>& OutPixels, TArray<FLabel>& OutNames)
{
	TSharedPtr<FJsonObject> Doc;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Doc) || !Doc.IsValid() || !Doc->HasField(TEXT("lines"))) return false;
	FRaster R;
	const TArray<TSharedPtr<FJsonValue>>* Fills = nullptr;
	if (Doc->TryGetArrayField(TEXT("fills"), Fills))
	{
		for (const TSharedPtr<FJsonValue>& F : *Fills)
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
			if (!F.IsValid() || !F->TryGetArray(Parts) || Parts->Num() < 2) continue;
			const int32 Kind = static_cast<int32>((*Parts)[0]->AsNumber());
			const TArray<TSharedPtr<FJsonValue>>* RingValues = nullptr;
			if (!(*Parts)[1]->TryGetArray(RingValues)) continue;
			TArray<TArray<FVector2D>> Rings;
			for (const TSharedPtr<FJsonValue>& RingValue : *RingValues)
			{
				TArray<FVector2D> Ring;
				if (ReadPoints(RingValue, Ring) && Ring.Num() >= 3) Rings.Add(MoveTemp(Ring));
			}
			R.Fill(Rings, Kind == 0 ? Water : Green);
		}
	}
	// Boundaries faint, then roads from minor to motorway (cartography.mjs classes 0..5).
	static const FColor Colors[] = { FColor(0x3A, 0x45, 0x41), FColor(0x4C, 0x58, 0x54), FColor(0x5F, 0x6C, 0x67), FColor(0x7C, 0x88, 0x83), FColor(0xA5, 0xAE, 0xA9), FColor(0xD0, 0xD5, 0xCF) };
	static const double Widths[] = { 5, 8, 10, 13, 17, 22 };
	const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
	if (Doc->TryGetArrayField(TEXT("lines"), Lines))
	{
		for (int32 Pass = 0; Pass < 6; ++Pass)
		{
			for (const TSharedPtr<FJsonValue>& L : *Lines)
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (!L.IsValid() || !L->TryGetArray(Parts) || Parts->Num() < 2) continue;
				const int32 Cls = FMath::Clamp(static_cast<int32>((*Parts)[0]->AsNumber()), 0, 5);
				if (Cls != Pass) continue;
				TArray<FVector2D> Pts;
				if (ReadPoints((*Parts)[1], Pts)) R.Line(Pts, Widths[Cls], Colors[Cls]);
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
	if (Doc->TryGetArrayField(TEXT("names"), Names))
	{
		for (const TSharedPtr<FJsonValue>& N : *Names)
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
			if (!N.IsValid() || !N->TryGetArray(Parts) || Parts->Num() < 5) continue;
			FLabel Label;
			Label.Text = (*Parts)[0]->AsString();
			// Tile units for now; Labels() turns them into world metres.
			Label.AtM = FVector2D((*Parts)[1]->AsNumber(), (*Parts)[2]->AsNumber());
			Label.Priority = static_cast<int32>((*Parts)[3]->AsNumber());
			Label.bBold = (*Parts)[4]->AsNumber() != 0.0;
			if (!Label.Text.IsEmpty()) OutNames.Add(MoveTemp(Label));
		}
	}
	OutPixels = MoveTemp(R.Px);
	return true;
}

void FSimCruiseMapTiles::Wanted(const FSimCruiseWorldFrame& Frame, const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx, TArray<FIntVector>& Out)
{
	Out.Reset();
	if (!Frame.IsValid() || PxPerM <= 0.0) return;
	// The map zoom whose tiles draw near 460 px wide (as the road atlas).
	const int32 Z = FMath::Clamp(FMath::RoundToInt(16.0 - FMath::Log2(460.0 / (Frame.TileSizeM * PxPerM))), 4, 14);
	const double Count = static_cast<double>(1 << (16 - Z));
	const FVector2D Ext = SizePx * 0.5 / PxPerM;
	const FVector2d Lo = Frame.WorldCmToTileCoord(FVector2d((CenterM.X - Ext.X) * 100.0, (CenterM.Y - Ext.Y) * 100.0));
	const FVector2d Hi = Frame.WorldCmToTileCoord(FVector2d((CenterM.X + Ext.X) * 100.0, (CenterM.Y + Ext.Y) * 100.0));
	const FVector2d C = Frame.WorldCmToTileCoord(FVector2d(CenterM.X * 100.0, CenterM.Y * 100.0));
	const int32 N = 1 << Z;
	for (int32 X = FMath::FloorToInt(Lo.X / Count); X <= FMath::FloorToInt(Hi.X / Count); ++X)
	{
		for (int32 Y = FMath::FloorToInt(Lo.Y / Count); Y <= FMath::FloorToInt(Hi.Y / Count); ++Y)
		{
			if (X >= 0 && Y >= 0 && X < N && Y < N) Out.Add(FIntVector(X, Y, Z));
		}
	}
	Out.Sort([&C, Count](const FIntVector& A, const FIntVector& B)
	{
		return FVector2d::DistSquared(FVector2d((A.X + 0.5) * Count, (A.Y + 0.5) * Count), C) < FVector2d::DistSquared(FVector2d((B.X + 0.5) * Count, (B.Y + 0.5) * Count), C);
	});
}

void FSimCruiseMapTiles::Update(const FSimCruiseWorldFrame& Frame, const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx)
{
	if (Fetch.IsValid() && Fetch->bDone)
	{
		const TSharedPtr<FFetch, ESPMode::ThreadSafe> Done = Fetch;
		Fetch.Reset();
		if (!Done->bOk || Done->Pixels.Num() != MapPx * MapPx)
		{
			RetryAt.Add(Done->Key, FPlatformTime::Seconds() + 60.0);
		}
		else
		{
			if (Images.Num() >= 64)
			{
				FString Oldest;
				double OldestTime = TNumericLimits<double>::Max();
				for (const TPair<FString, FImage>& Pair : Images) if (Pair.Value.LastUsed < OldestTime) { OldestTime = Pair.Value.LastUsed; Oldest = Pair.Key; }
				Images.Remove(Oldest);
			}
			if (UTexture2D* Texture = UTexture2D::CreateTransient(MapPx, MapPx, PF_B8G8R8A8, *FString::Printf(TEXT("SimCruisePlan_%s"), *Done->Key)))
			{
				Texture->Filter = TF_Bilinear;
				Texture->SRGB = true;
				FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
				void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
				FMemory::Memcpy(Data, Done->Pixels.GetData(), Done->Pixels.Num() * sizeof(FColor));
				Mip.BulkData.Unlock();
				Texture->UpdateResource();
				FImage& Image = Images.Add(Done->Key);
				Image.Texture.Reset(Texture);
				Image.Brush = MakeShared<FSlateBrush>();
				Image.Brush->SetResourceObject(Texture);
				Image.Brush->ImageSize = FVector2D(MapPx, MapPx);
				Image.Brush->DrawAs = ESlateBrushDrawType::Image;
				Image.Names = MoveTemp(Done->Names);
				Image.LastUsed = FPlatformTime::Seconds();
			}
		}
	}
	if (Fetch.IsValid()) return;
	TArray<FIntVector> List;
	Wanted(Frame, CenterM, PxPerM, SizePx, List);
	const double Now = FPlatformTime::Seconds();
	for (const FIntVector& Id : List)
	{
		const FString K = Key(Id);
		if (Images.Contains(K)) continue;
		if (const double* Retry = RetryAt.Find(K); Retry && *Retry > Now) continue;
		TSharedPtr<FFetch, ESPMode::ThreadSafe> Job = MakeShared<FFetch, ESPMode::ThreadSafe>();
		Job->Key = K;
		Job->Id = Id;
		Fetch = Job;
		const FString Offline = FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data"), TEXT("map"), FString::Printf(TEXT("map-%s.json"), *K));
		const FSimCruiseAtlasConfig Config = HelperConfig();
		Async(EAsyncExecution::ThreadPool, [Job, Offline, Config]()
		{
			FString Raw;
			if (!FFileHelper::LoadFileToString(Raw, *Offline) && Config.IsConfigured() && !Job->bCancel)
			{
				FSimCruiseAtlasRequest Request;
				Request.Command = TEXT("map");
				Request.Args = { FString::FromInt(Job->Id.Z), FString::FromInt(Job->Id.X), FString::FromInt(Job->Id.Y) };
				Request.TimeoutSeconds = 40.0;
				const FSimCruiseAtlasResult Result = FSimCruiseAtlasClient::RunBlocking(Config, Request, &Job->bCancel);
				if (Result.Error == ESimCruiseAtlasError::None) Raw = Result.Stdout;
			}
			Job->bOk = !Raw.IsEmpty() && Raster(Raw, Job->Pixels, Job->Names);
			Job->bDone = true;
		});
		return;
	}
}

void FSimCruiseMapTiles::Paint(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FSimCruiseWorldFrame& Frame,
	const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx) const
{
	TArray<FIntVector> List;
	Wanted(Frame, CenterM, PxPerM, SizePx, List);
	const double Now = FPlatformTime::Seconds();
	for (const FIntVector& Id : List)
	{
		const FImage* Image = Images.Find(Key(Id));
		if (!Image) continue;
		const_cast<FImage*>(Image)->LastUsed = Now;
		const double Count = static_cast<double>(1 << (16 - Id.Z));
		const FVector2d NwCm = Frame.TileCoordToWorldCm(FVector2d(Id.X * Count, Id.Y * Count));
		const FVector2D Nw = (FVector2D(NwCm.X, NwCm.Y) / 100.0 - CenterM) * PxPerM + SizePx * 0.5;
		const double Width = Frame.TileSizeM * Count * PxPerM;
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(FVector2f(Width + 0.5, Width + 0.5), FSlateLayoutTransform(FVector2f(Nw))), Image->Brush.Get());
	}
}

void FSimCruiseMapTiles::Labels(const FSimCruiseWorldFrame& Frame, const FVector2D& CenterM, double PxPerM, const FVector2D& SizePx, TArray<FLabel>& Out) const
{
	TArray<FIntVector> List;
	Wanted(Frame, CenterM, PxPerM, SizePx, List);
	for (const FIntVector& Id : List)
	{
		const FImage* Image = Images.Find(Key(Id));
		if (!Image) continue;
		const double Count = static_cast<double>(1 << (16 - Id.Z));
		const FVector2d NwCm = Frame.TileCoordToWorldCm(FVector2d(Id.X * Count, Id.Y * Count));
		const double SideM = Frame.TileSizeM * Count;
		for (const FLabel& Name : Image->Names)
		{
			FLabel L = Name;
			L.AtM = FVector2D(NwCm.X, NwCm.Y) / 100.0 + Name.AtM * SideM / MapUnits;
			Out.Add(MoveTemp(L));
		}
	}
}
