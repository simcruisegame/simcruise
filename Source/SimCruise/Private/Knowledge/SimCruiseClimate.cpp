// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruiseClimate.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "World/SimCruiseTileData.h"

namespace
{
	/** Beck et al. (2023) legend, class numbers 1-30. */
	const TCHAR* const Codes[] = { TEXT(""),
		TEXT("Af"), TEXT("Am"), TEXT("Aw"), TEXT("BWh"), TEXT("BWk"), TEXT("BSh"), TEXT("BSk"), TEXT("Csa"), TEXT("Csb"), TEXT("Csc"),
		TEXT("Cwa"), TEXT("Cwb"), TEXT("Cwc"), TEXT("Cfa"), TEXT("Cfb"), TEXT("Cfc"), TEXT("Dsa"), TEXT("Dsb"), TEXT("Dsc"), TEXT("Dsd"),
		TEXT("Dwa"), TEXT("Dwb"), TEXT("Dwc"), TEXT("Dwd"), TEXT("Dfa"), TEXT("Dfb"), TEXT("Dfc"), TEXT("Dfd"), TEXT("ET"), TEXT("EF") };
	constexpr int32 NumClasses = 30;

	/** The grid as run lengths: for row y, runs [RowFirst[y], RowFirst[y + 1]) of (start column, class). */
	struct FGrid
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<int32> RowFirst;
		TArray<uint16> Start;
		TArray<uint8> Class;

		uint8 At(int32 X, int32 Y) const
		{
			if (Width <= 0) return 0;
			X = ((X % Width) + Width) % Width; // wraps across 180 degrees
			Y = FMath::Clamp(Y, 0, Height - 1);
			int32 Lo = RowFirst[Y], Hi = RowFirst[Y + 1] - 1;
			while (Lo < Hi)
			{
				const int32 Mid = (Lo + Hi + 1) / 2;
				if (Start[Mid] <= X) Lo = Mid; else Hi = Mid - 1;
			}
			return Class[Lo];
		}
	};

	FCriticalSection GridLock;
	bool bTried = false;
	FGrid TheGrid;

	bool Decode(const TArray<uint8>& Png, FGrid& Out)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Png.GetData(), Png.Num()) || !Wrapper->GetRaw(ERGBFormat::Gray, 8, Raw)) return false;
		const int32 W = static_cast<int32>(Wrapper->GetWidth()), H = static_cast<int32>(Wrapper->GetHeight());
		if (W <= 0 || H <= 0 || W > 65535 || Raw.Num() != static_cast<int64>(W) * H) return false;
		Out = FGrid();
		Out.Width = W;
		Out.Height = H;
		Out.RowFirst.Reserve(H + 1);
		for (int32 y = 0; y < H; ++y)
		{
			Out.RowFirst.Add(Out.Start.Num());
			const uint8* Row = Raw.GetData() + static_cast<int64>(y) * W;
			for (int32 x = 0; x < W; ++x)
			{
				const uint8 C = Row[x] <= NumClasses ? Row[x] : 0;
				if (x == 0 || C != Out.Class.Last())
				{
					Out.Start.Add(static_cast<uint16>(x));
					Out.Class.Add(C);
				}
			}
		}
		Out.RowFirst.Add(Out.Start.Num());
		return true;
	}

	const FGrid& Grid()
	{
		FScopeLock Lock(&GridLock);
		if (!bTried)
		{
			bTried = true;
			TArray<uint8> Png;
			if (FFileHelper::LoadFileToArray(Png, *SimCruiseClimate::GridPath(), FILEREAD_Silent)) Decode(Png, TheGrid);
		}
		return TheGrid;
	}

	/** A class to stand for a place-graph climate fact when the grid says nothing. */
	uint8 ClassOfFact(FName Fact)
	{
		const FString F = Fact.ToString();
		if (F == TEXT("climate:tropical")) return 2;           // Am
		if (F == TEXT("climate:arid")) return 4;               // BWh
		if (F == TEXT("climate:mediterranean")) return 8;      // Csa
		if (F == TEXT("climate:humid_subtropical")) return 14; // Cfa
		if (F == TEXT("climate:oceanic")) return 15;           // Cfb
		if (F == TEXT("climate:humid_continental")) return 25; // Dfa
		if (F == TEXT("climate:subarctic")) return 27;         // Dfc
		if (F == TEXT("climate:polar")) return 29;             // ET
		return 0;
	}

	/** The month as it would be north of the equator (the seasons flip in the south). */
	int32 NorthMonth(double LatDeg, int32 Month)
	{
		Month = FMath::Clamp(Month, 1, 12);
		return LatDeg >= 0.0 ? Month : ((Month + 5) % 12) + 1;
	}
}

namespace SimCruiseClimate
{
	FString GridPath()
	{
		return FPaths::Combine(SimCruiseTileData::RepositoryRoot(), TEXT("data/climate/koppen-1991-2020.png"));
	}

	const TCHAR* Source()
	{
		return HasGrid() ? TEXT("Beck et al. 2023 Köppen-Geiger 1991-2020 (CC BY 4.0)") : TEXT("latitude band");
	}

	bool HasGrid() { return Grid().Width > 0; }

	bool LoadGridForTest(const TArray<uint8>& Png)
	{
		FGrid G;
		if (!Decode(Png, G)) return false;
		FScopeLock Lock(&GridLock);
		bTried = true;
		TheGrid = MoveTemp(G);
		return true;
	}

	void ResetGrid()
	{
		FScopeLock Lock(&GridLock);
		bTried = false;
		TheGrid = FGrid();
	}

	uint8 KoppenAt(double LatDeg, double LonDeg)
	{
		const FGrid& G = Grid();
		if (G.Width <= 0 || !FMath::IsFinite(LatDeg) || !FMath::IsFinite(LonDeg)) return 0;
		const int32 X = FMath::FloorToInt((LonDeg + 180.0) / 360.0 * G.Width);
		const int32 Y = FMath::FloorToInt((90.0 - LatDeg) / 180.0 * G.Height);
		if (const uint8 C = G.At(X, Y)) return C;
		// A coastal point in a sea cell (a barrier island, a pier): the nearest land cell.
		uint8 Best = 0;
		int32 BestD = TNumericLimits<int32>::Max();
		for (int32 R = 1; R <= 3 && Best == 0; ++R)
		{
			for (int32 dy = -R; dy <= R; ++dy)
			for (int32 dx = -R; dx <= R; ++dx)
			{
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != R) continue;
				const uint8 C = G.At(X + dx, Y + dy);
				const int32 D = dx * dx + dy * dy;
				if (C != 0 && D < BestD) { BestD = D; Best = C; }
			}
		}
		return Best;
	}

	const TCHAR* KoppenCode(uint8 Class) { return Class >= 1 && Class <= NumClasses ? Codes[Class] : TEXT(""); }

	uint8 KoppenFromCode(const FString& Code)
	{
		for (int32 i = 1; i <= NumClasses; ++i) if (Code.Equals(Codes[i], ESearchCase::CaseSensitive)) return static_cast<uint8>(i);
		return 0;
	}

	FName ClimateFact(uint8 C)
	{
		if (C == 0 || C > NumClasses) return NAME_None;
		if (C <= 3) return TEXT("climate:tropical");
		if (C <= 7) return TEXT("climate:arid");
		if (C <= 10) return TEXT("climate:mediterranean");
		if (C == 11 || C == 14) return TEXT("climate:humid_subtropical");
		if (C <= 16) return TEXT("climate:oceanic");
		if (C <= 28)
		{
			// The third letter: a/b warm or hot summers, c/d short cold ones.
			const TCHAR Third = Codes[C][2];
			return Third == TEXT('a') || Third == TEXT('b') ? FName(TEXT("climate:humid_continental")) : FName(TEXT("climate:subarctic"));
		}
		return TEXT("climate:polar");
	}

	FName LatitudeBandFact(double LatDeg)
	{
		const double A = FMath::Abs(LatDeg);
		return A < 23.5 ? FName(TEXT("climate:tropical")) : A < 35.0 ? FName(TEXT("climate:humid_subtropical"))
			: A < 55.0 ? FName(TEXT("climate:humid_continental")) : FName(TEXT("climate:subarctic"));
	}

	FName Season(uint8 C, FName Fallback, double LatDeg, int32 Month)
	{
		if (C == 0) C = ClassOfFact(Fallback);
		const int32 M = NorthMonth(LatDeg, Month);
		if (C >= 1 && C <= 3)
		{
			if (C == 1) return TEXT("season:wet");
			return M >= 5 && M <= 10 ? FName(TEXT("season:wet")) : FName(TEXT("season:dry"));
		}
		return M == 12 || M <= 2 ? FName(TEXT("season:winter")) : M <= 5 ? FName(TEXT("season:spring")) : M <= 8 ? FName(TEXT("season:summer")) : FName(TEXT("season:autumn"));
	}

	float GrassDry(uint8 C, FName Fallback, double LatDeg, int32 Month)
	{
		if (C == 0) C = ClassOfFact(Fallback);
		if (C == 0) return 0.0f;
		const int32 M = NorthMonth(LatDeg, Month) - 1;
		// Estimates, January to December (northern months), per class family.
		static const float Am[12] = { 0.18f, 0.22f, 0.28f, 0.25f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.08f, 0.12f };
		static const float Aw[12] = { 0.45f, 0.55f, 0.60f, 0.50f, 0.08f, 0.08f, 0.08f, 0.08f, 0.08f, 0.08f, 0.15f, 0.30f };
		static const float Cs[12] = { 0.15f, 0.10f, 0.10f, 0.15f, 0.35f, 0.60f, 0.80f, 0.85f, 0.80f, 0.60f, 0.30f, 0.20f };
		// Warm-season lawns (Bermuda, zoysia, St. Augustine) go dormant tan after the first frosts.
		static const float Cfa[12] = { 0.60f, 0.55f, 0.35f, 0.15f, 0.08f, 0.08f, 0.12f, 0.15f, 0.12f, 0.20f, 0.40f, 0.55f };
		static const float Cfb[12] = { 0.20f, 0.20f, 0.12f, 0.08f, 0.05f, 0.08f, 0.15f, 0.20f, 0.12f, 0.08f, 0.12f, 0.18f };
		static const float D[12] = { 0.60f, 0.60f, 0.50f, 0.30f, 0.10f, 0.08f, 0.12f, 0.18f, 0.20f, 0.35f, 0.55f, 0.60f };
		static const float E[12] = { 0.50f, 0.50f, 0.50f, 0.50f, 0.40f, 0.25f, 0.20f, 0.25f, 0.40f, 0.50f, 0.50f, 0.50f };
		if (C == 1) return 0.05f;
		if (C == 2) return Am[M];
		if (C == 3) return Aw[M];
		if (C == 4 || C == 5) return 0.85f;
		if (C == 6 || C == 7) return M >= 5 && M <= 8 ? 0.75f : 0.55f;
		if (C <= 10) return Cs[M];
		if (C == 11 || C == 14) return Cfa[M];
		if (C <= 16) return Cfb[M];
		if (C <= 28) return D[M];
		return E[M];
	}
}
