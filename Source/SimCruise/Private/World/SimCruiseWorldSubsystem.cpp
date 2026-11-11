// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseWorldSubsystem.h"

#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "World/SimCruiseWorldPreview.h"
#include "World/SimCruiseWorldStreamer.h"

bool USimCruiseWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
}

void USimCruiseWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Cmd = FCommandLine::Get();
	FString Start;
	FString Preview;
	const bool bWorld = FParse::Param(Cmd, TEXT("SimCruiseWorld"));
	const bool bStart = FParse::Value(Cmd, TEXT("SimCruiseStart="), Start);
	const bool bPreview = FParse::Value(Cmd, TEXT("SimCruisePreview="), Preview, false);
	if (!bWorld && !bStart && !bPreview)
	{
		return;
	}
	double Lat = BundledLat, Lon = BundledLon;
	FString Title = TEXT("Ocean Drive, Miami Beach");
	if (bStart)
	{
		TArray<FString> Parts;
		Start.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 2 && Parts[0].IsNumeric() && Parts[1].IsNumeric())
		{
			Lat = FCString::Atod(*Parts[0]);
			Lon = FCString::Atod(*Parts[1]);
			Title = FString::Printf(TEXT("%.5f, %.5f"), Lat, Lon);
		}
	}
	StartDrive(Lon, Lat, Title);
	if (bPreview)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASimCruiseWorldPreview* Rig = InWorld.SpawnActor<ASimCruiseWorldPreview>(ASimCruiseWorldPreview::StaticClass(), FTransform::Identity, Params);
		if (Rig)
		{
			Rig->Begin(Streamer, Preview);
		}
	}
}

ASimCruiseWorldStreamer* USimCruiseWorldSubsystem::StartDrive(double LonDeg, double LatDeg, const FString& Title)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	if (!Streamer)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Name = TEXT("SimCruiseWorldStreamer");
		Streamer = World->SpawnActor<ASimCruiseWorldStreamer>(ASimCruiseWorldStreamer::StaticClass(), FTransform::Identity, Params);
	}
	if (Streamer)
	{
		Streamer->StartAt(LonDeg, LatDeg, Title);
	}
	return Streamer;
}
