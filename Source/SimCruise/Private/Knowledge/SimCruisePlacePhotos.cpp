// Copyright SimCruise contributors. All Rights Reserved.

#include "Knowledge/SimCruisePlacePhotos.h"

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Knowledge/SimCruisePlaceGraph.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	int32 FirstYear(const FString& Text)
	{
		for (int32 i = 0; i + 4 <= Text.Len(); ++i)
		{
			if (!FChar::IsDigit(Text[i]) || (i > 0 && FChar::IsDigit(Text[i - 1]))) continue;
			bool bFour = true;
			for (int32 k = 0; k < 4; ++k) bFour &= FChar::IsDigit(Text[i + k]);
			if (!bFour || (i + 4 < Text.Len() && FChar::IsDigit(Text[i + 4]))) continue;
			const int32 Y = FCString::Atoi(*Text.Mid(i, 4));
			if (Y >= 1800 && Y <= 2100) return Y;
		}
		return 0;
	}

	FString Meta(const TSharedPtr<FJsonObject>& Ext, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* F = nullptr;
		FString V;
		if (Ext.IsValid() && Ext->TryGetObjectField(Field, F)) (*F)->TryGetStringField(TEXT("value"), V);
		return V;
	}

	/** Commons metadata is HTML at times ("<a href=...>Name</a>"): the text only. */
	FString StripTags(const FString& In)
	{
		FString Out;
		bool bTag = false;
		for (const TCHAR C : In)
		{
			if (C == TEXT('<')) bTag = true;
			else if (C == TEXT('>')) bTag = false;
			else if (!bTag) Out.AppendChar(C);
		}
		return Out.TrimStartAndEnd();
	}
}

namespace SimCruisePlacePhotos
{
	FString FileFromCommonsUrl(const FString& Url)
	{
		int32 At = Url.Find(TEXT("Special:FilePath/"));
		if (At == INDEX_NONE) return FString();
		return FGenericPlatformHttp::UrlDecode(Url.Mid(At + 17));
	}

	FString ImageInfoUrl(const FString& File)
	{
		return TEXT("https://commons.wikimedia.org/w/api.php?action=query&prop=imageinfo&iiprop=extmetadata%7Curl&iiurlwidth=480&format=json&titles=")
			+ FGenericPlatformHttp::UrlEncode(TEXT("File:") + File);
	}

	bool ParseImageInfo(const FString& Json, FSimCruisePhotoInfo& Out, FString& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedPtr<FJsonObject>* Query = nullptr;
		const TSharedPtr<FJsonObject>* Pages = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid()
			|| !Root->TryGetObjectField(TEXT("query"), Query) || !(*Query)->TryGetObjectField(TEXT("pages"), Pages))
		{
			OutError = TEXT("Commons imageinfo: no query.pages");
			return false;
		}
		for (const auto& KV : (*Pages)->Values)
		{
			const TSharedPtr<FJsonObject>* Page = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Infos = nullptr;
			if (!KV.Value->TryGetObject(Page) || !(*Page)->TryGetArrayField(TEXT("imageinfo"), Infos) || Infos->Num() == 0) continue;
			const TSharedPtr<FJsonObject> Info = (*Infos)[0]->AsObject();
			FString Title;
			(*Page)->TryGetStringField(TEXT("title"), Title);
			Title.RemoveFromStart(TEXT("File:"));
			Out.File = Title;
			Info->TryGetStringField(TEXT("thumburl"), Out.ThumbUrl);
			Info->TryGetStringField(TEXT("descriptionurl"), Out.PageUrl);
			const TSharedPtr<FJsonObject>* Ext = nullptr;
			Info->TryGetObjectField(TEXT("extmetadata"), Ext);
			const TSharedPtr<FJsonObject> E = Ext ? *Ext : nullptr;
			Out.Year = FirstYear(StripTags(Meta(E, TEXT("DateTimeOriginal"))));
			if (Out.Year == 0) Out.Year = FirstYear(Meta(E, TEXT("DateTime")));
			Out.Licence = Meta(E, TEXT("LicenseShortName"));
			Out.Artist = StripTags(Meta(E, TEXT("Artist")));
			const FString Cats = Meta(E, TEXT("Categories")) + TEXT(" ") + StripTags(Meta(E, TEXT("ImageDescription")));
			Out.bPostcard = Cats.Contains(TEXT("postcard")) || Cats.Contains(TEXT("Tichnor")) || Cats.Contains(TEXT("HABS"));
			return true;
		}
		OutError = TEXT("Commons imageinfo: no image");
		return false;
	}

	bool IsUsable(const FSimCruisePhotoInfo& Info)
	{
		const FString Lower = Info.ThumbUrl.ToLower();
		return Info.Year >= 1990 && !Info.bPostcard && !Info.ThumbUrl.IsEmpty() && (Lower.Contains(TEXT(".jpg")) || Lower.Contains(TEXT(".jpeg")));
	}

	bool WallColour(const TArray<FColor>& Pixels, int32 W, int32 H, FColor& OutColour, float& OutShare)
	{
		if (W < 16 || H < 16 || Pixels.Num() < W * H) return false;
		// About 160 samples across; the facade band is the middle of the frame (not the sky
		// at the top or the street at the bottom).
		const int32 Step = FMath::Max(1, W / 160);
		const int32 X0 = FMath::RoundToInt(W * 0.12f), X1 = FMath::RoundToInt(W * 0.88f);
		const int32 Y0 = FMath::RoundToInt(H * 0.22f), Y1 = FMath::RoundToInt(H * 0.78f);
		TArray<FColor> Kept;
		double SatSum = 0.0;
		int32 All = 0;
		for (int32 y = Y0; y < Y1; y += Step)
		{
			for (int32 x = X0; x < X1; x += Step)
			{
				const FColor C = Pixels[y * W + x];
				const int32 Mx = FMath::Max3(C.R, C.G, C.B), Mn = FMath::Min3(C.R, C.G, C.B);
				SatSum += Mx > 0 ? double(Mx - Mn) / Mx : 0.0;
				++All;
				const bool bSky = C.B > C.R + 12 && C.B > C.G && Mx > 150;
				const bool bLeaf = C.G > C.R + 8 && C.G > C.B + 8 && Mx < 170;
				const bool bDark = Mx < 95;
				if (!bSky && !bLeaf && !bDark) Kept.Add(C);
			}
		}
		// A black-and-white picture tells nothing about the paint.
		if (All == 0 || Kept.Num() < 200 || SatSum / All < 0.04) return false;
		// Coarse colour bins (24 levels a step), the biggest bin with its neighbours.
		auto Bin = [](const FColor& C) { return FIntVector(C.R / 24, C.G / 24, C.B / 24); };
		TMap<FIntVector, int32> Counts;
		for (const FColor& C : Kept) ++Counts.FindOrAdd(Bin(C));
		FIntVector Best;
		int32 BestN = -1;
		for (const auto& KV : Counts) if (KV.Value > BestN) { BestN = KV.Value; Best = KV.Key; }
		TArray<uint8> R, G, B;
		for (const FColor& C : Kept)
		{
			const FIntVector Q = Bin(C);
			if (FMath::Abs(Q.X - Best.X) <= 1 && FMath::Abs(Q.Y - Best.Y) <= 1 && FMath::Abs(Q.Z - Best.Z) <= 1)
			{
				R.Add(C.R); G.Add(C.G); B.Add(C.B);
			}
		}
		OutShare = float(R.Num()) / Kept.Num();
		auto Median = [](TArray<uint8>& V) { V.Sort(); return V[V.Num() / 2]; };
		OutColour = FColor(Median(R), Median(G), Median(B), 255);
		return OutShare >= 0.2f;
	}

	bool DecodeImage(const TArray<uint8>& Bytes, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
		if (Format == EImageFormat::Invalid) return false;
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format);
		TArray<uint8> Raw;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)) return false;
		OutWidth = static_cast<int32>(Wrapper->GetWidth());
		OutHeight = static_cast<int32>(Wrapper->GetHeight());
		OutPixels.SetNumUninitialized(OutWidth * OutHeight);
		FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), FMath::Min<int64>(Raw.Num(), OutPixels.Num() * 4));
		return true;
	}

	void Apply(FSimCruisePlaceRecord& Record, const FColor& Colour, float Share, const FSimCruisePhotoInfo& Info)
	{
		Record.Colour = FString::Printf(TEXT("#%02X%02X%02X"), Colour.R, Colour.G, Colour.B);
		Record.ColourSource = FString::Printf(TEXT("measured in File:%s (%d, %s, %s; wall %.0f%% of the facade band)"),
			*Info.File, Info.Year, *Info.Artist.Left(60), *Info.Licence, Share * 100.0f);
	}
}
