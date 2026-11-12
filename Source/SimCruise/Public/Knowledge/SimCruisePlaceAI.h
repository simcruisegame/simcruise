// Copyright SimCruise contributors. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Knowledge/SimCruisePlaceGraph.h"

/**
 * The local AI fills the place knowledge graph where the open data says nothing.
 *
 * Method (reading, not guessing): Wikipedia's geosearch gives the articles about places
 * within 1.5 km of an area's centre (free API, text CC BY-SA 4.0); their lead extracts go
 * to a small language model on the CPU (Ollama on localhost, num_gpu 0, so the 28 W GPU
 * keeps rendering), which answers in a fixed JSON schema whose words are the graph's own
 * tokens: the styles that dominate (with shares), the main decade, the wall palette, the
 * roof form. A 4 B model guessing from memory called Ocean Drive Pueblo Revival; reading
 * the extracts it says Art Deco 0.75, MiMo 0.25, 1930s.
 *
 * One answer per area (a zoom-14 tile, about 2.2 km), cached in
 * data/knowledge/areas/ (bundled) or Saved/Knowledge/areas/. The answer becomes weak
 * tile facts (style at 0.6 x share, era and palette at 0.4), below a building's own
 * open data and below the spread from dated neighbours.
 */
struct SIMCRUISE_API FSimCruiseAreaPrior
{
	TArray<TPair<FString, float>> Styles; // graph style tokens and shares
	int32 Decade = 0;
	FString Palette;                      // palette name without "palette:", or empty
	FString Roofs;                        // flat, pitched, mixed or empty
	TArray<FString> Sources;              // Wikipedia article titles read
	FString Model;

	bool IsEmpty() const { return Styles.Num() == 0 && Decade == 0 && Palette.IsEmpty() && Roofs.IsEmpty(); }
};

namespace SimCruisePlaceAI
{
	/** The zoom-14 area of a zoom-16 tile. */
	inline FIntPoint AreaOf(const FIntPoint& Tile16) { return FIntPoint(Tile16.X >> 2, Tile16.Y >> 2); }
	SIMCRUISE_API void AreaCentre(const FIntPoint& Area, double& OutLat, double& OutLon);

	SIMCRUISE_API FString GeosearchUrl(double Lat, double Lon);
	SIMCRUISE_API bool ParseGeosearch(const FString& Json, TArray<int64>& OutPageIds, FString& OutError);
	SIMCRUISE_API FString ExtractsUrl(const TArray<int64>& PageIds);
	/** "Title: extract" lines, each cut to 500 characters, in the order of the ids. */
	SIMCRUISE_API bool ParseExtracts(const FString& Json, const TArray<int64>& PageIds, TArray<FString>& OutLines, TArray<FString>& OutTitles, FString& OutError);

	/** The Ollama /api/chat body: CPU only, low temperature, a JSON schema of the graph's own tokens. */
	SIMCRUISE_API FString ChatRequest(const FSimCruisePlaceRules& Rules, const FString& Model, const FString& PlaceLabel, double Lat, double Lon, const TArray<FString>& Lines);
	SIMCRUISE_API bool ParseChat(const FSimCruisePlaceRules& Rules, const FString& Json, FSimCruiseAreaPrior& Out, FString& OutError);
	/** Ollama /api/tags: the smallest chat model (no embedding models), or empty. */
	SIMCRUISE_API FString PickModel(const FString& TagsJson);

	SIMCRUISE_API void ToFacts(const FSimCruiseAreaPrior& Prior, FSimCruiseFactSet& Out);

	SIMCRUISE_API FString ToJson(const FIntPoint& Area, const FSimCruiseAreaPrior& Prior);
	SIMCRUISE_API bool FromJson(const FString& Json, FSimCruiseAreaPrior& Out);
	SIMCRUISE_API FString BundledPath(const FIntPoint& Area);
	SIMCRUISE_API FString CachePath(const FIntPoint& Area);
	/** The area's prior from the bundle or the cache (thread-safe, kept in memory). */
	SIMCRUISE_API bool ForArea(const FIntPoint& Area, FSimCruiseAreaPrior& Out);
	SIMCRUISE_API bool IsKnown(const FIntPoint& Area);
	SIMCRUISE_API void Store(const FIntPoint& Area, const FSimCruiseAreaPrior& Prior);

	SIMCRUISE_API FString OllamaUrl();
}
