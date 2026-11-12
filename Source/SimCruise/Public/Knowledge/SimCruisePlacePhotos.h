// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FSimCruisePlaceRecord;

/**
 * The wall colour of a building measured from its own photograph.
 *
 * Wikidata names a photograph for many buildings (P18, a Wikimedia Commons file). Commons'
 * imageinfo gives its date, licence, author, categories and a 480 px thumbnail. Archive
 * pictures (before 1990) and postcards are left out: their colours are not today's paint.
 * In the thumbnail's facade band (the middle of the frame) sky, leaves, shadow and glass
 * are dropped and the largest colour cluster is the wall, kept when it covers at least a
 * fifth of what is left. The colour goes on the building's record with its source, so the
 * graph uses it as mapped data (FSimCruiseBuildingPlan::bColourFromData).
 */
struct SIMCRUISE_API FSimCruisePhotoInfo
{
	FString File;      // "Tides Hotel Miami Beach.jpg"
	FString ThumbUrl;
	FString PageUrl;
	int32 Year = 0;    // DateTimeOriginal
	FString Licence;   // "CC BY-SA 3.0"
	FString Artist;
	bool bPostcard = false;
};

namespace SimCruisePlacePhotos
{
	/** "http://commons.wikimedia.org/wiki/Special:FilePath/Tides%20Hotel.jpg" -> "Tides Hotel.jpg". */
	SIMCRUISE_API FString FileFromCommonsUrl(const FString& Url);
	SIMCRUISE_API FString ImageInfoUrl(const FString& File);
	SIMCRUISE_API bool ParseImageInfo(const FString& Json, FSimCruisePhotoInfo& Out, FString& OutError);
	/** A colour photograph of today's building: 1990 or later, not a postcard, a JPEG thumbnail. */
	SIMCRUISE_API bool IsUsable(const FSimCruisePhotoInfo& Info);

	/** The wall colour (sRGB) in the facade band of an image; Share is the part of the kept pixels it covers. */
	SIMCRUISE_API bool WallColour(const TArray<FColor>& Pixels, int32 Width, int32 Height, FColor& OutColour, float& OutShare);
	SIMCRUISE_API bool DecodeImage(const TArray<uint8>& Bytes, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);

	/** A record's photographs are kept as "a.jpg|b.jpg". */
	SIMCRUISE_API TArray<FString> Images(const FString& List);
	SIMCRUISE_API void AddImage(FString& List, const FString& File);

	/** Puts a measured colour on a record ("#RRGGBB" and its source line). */
	SIMCRUISE_API void Apply(FSimCruisePlaceRecord& Record, const FColor& Colour, float Share, const FSimCruisePhotoInfo& Info);
}
