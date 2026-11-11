// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseWorldStreamer.h"

#include "Async/Async.h"
#include "Atlas/SimCruiseAtlasClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTileData.h"
#include "World/SimCruiseWorldResources.h"
#include "World/SimCruiseWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseWorld, Log, All);

ASimCruiseWorldStreamer::ASimCruiseWorldStreamer()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASimCruiseWorldStreamer::BeginPlay()
{
	Super::BeginPlay();
	Resources = NewObject<USimCruiseWorldResources>(this);
	Resources->Initialise();
	FSimCruiseAtlasConfig Config = FSimCruiseAtlasConfig::ResolveDefault();
	Config.MaxConcurrent = FMath::Max(1, MaxConcurrentBuilds);
	Atlas = MakeShared<FSimCruiseAtlasClient, ESPMode::ThreadSafe>(Config);
	Inbox = MakeShared<FInbox, ESPMode::ThreadSafe>();
	UE_LOG(LogSimCruiseWorld, Display, TEXT("World streamer ready; helper: %s; cache: %s"),
		Config.IsConfigured() ? *Config.Executable : TEXT("(none: bundled tiles only)"), *Config.CacheDir);
}

void ASimCruiseWorldStreamer::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Inbox.IsValid())
	{
		Inbox->bClosed = true;
	}
	if (Atlas.IsValid())
	{
		Atlas->CancelAll();
	}
	Super::EndPlay(Reason);
}

void ASimCruiseWorldStreamer::StartAt(double LonDeg, double LatDeg, const FString& InTitle)
{
	for (auto& Pair : Tiles)
	{
		if (Pair.Value) Pair.Value->Destroy();
	}
	Tiles.Reset();
	if (Atlas.IsValid()) Atlas->CancelAll();
	Frame = FSimCruiseWorldFrame::ForPlace(LonDeg, LatDeg);
	Title = InTitle;
	Manager.RingRadius = RingRadius;
	Manager.MaxInFlight = FMath::Max(1, MaxConcurrentBuilds);
	Manager.Reset();
	FocusCm = Frame.LonLatToWorldCm(LonDeg, LatDeg);
	FocusVelocity = FVector::ZeroVector;
	bHasFocus = true;
	bStarted = true;
	UE_LOG(LogSimCruiseWorld, Display, TEXT("Drive at %s (%.5f, %.5f): origin tile %s, tile %.2f m, generation %u"),
		*Title, LatDeg, LonDeg, *SimCruiseTileName(Frame.OriginTile), Frame.TileSizeM, Manager.GetGeneration());
}

void ASimCruiseWorldStreamer::SetFocus(const FVector& WorldCm, const FVector& VelocityCmPerSecond)
{
	FocusCm = WorldCm;
	FocusVelocity = VelocityCmPerSecond;
	bHasFocus = true;
}

FVector ASimCruiseWorldStreamer::CurrentFocus(FVector& OutVelocity) const
{
	OutVelocity = FocusVelocity;
	if (bHasFocus)
	{
		return FocusCm;
	}
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		return Camera->GetCameraLocation();
	}
	return FVector::ZeroVector;
}

ASimCruiseWorldTile* ASimCruiseWorldStreamer::GetTile(const FIntPoint& Tile) const
{
	const TObjectPtr<ASimCruiseWorldTile>* Found = Tiles.Find(Tile);
	return Found ? Found->Get() : nullptr;
}

bool ASimCruiseWorldStreamer::IsBusy() const
{
	return Manager.NumInFlight() > 0;
}

bool ASimCruiseWorldStreamer::AreTilesReadyAround(const FVector& WorldCm, int32 Radius) const
{
	if (!Frame.IsValid()) return false;
	const FIntPoint Centre = Frame.TileAtWorldCm(WorldCm);
	for (int32 dy = -Radius; dy <= Radius; ++dy)
	for (int32 dx = -Radius; dx <= Radius; ++dx)
	{
		const ASimCruiseWorldTile* Tile = GetTile(Centre + FIntPoint(dx, dy));
		if (!Tile || !Tile->IsCollisionReady()) return false;
	}
	return true;
}

bool ASimCruiseWorldStreamer::SurfaceHeightAt(const FVector& WorldCm, double& OutZCm) const
{
	if (!Frame.IsValid()) return false;
	const FIntPoint TileId = Frame.TileAtWorldCm(WorldCm);
	const ASimCruiseWorldTile* Tile = GetTile(TileId);
	if (!Tile) return false;
	const FVector Local = (WorldCm - Tile->GetActorLocation()) / 100.0;
	OutZCm = Tile->GetActorLocation().Z + Tile->SurfaceHeightAt(FVector2d(Local.X, Local.Y)) * 100.0;
	return true;
}

bool ASimCruiseWorldStreamer::FindRoadPose(const FString& RoadName, const FVector& NearWorldCm, double SearchRadiusM, FVector& OutLocationCm, FRotator& OutRotation) const
{
	double Best = SearchRadiusM * 100.0;
	bool bFound = false;
	for (const auto& Pair : Tiles)
	{
		const ASimCruiseWorldTile* Tile = Pair.Value;
		if (!Tile || !Tile->GetBuild()) continue;
		const FVector Origin = Tile->GetActorLocation();
		for (const FSimCruiseRibbon& Ribbon : Tile->GetBuild()->Roads.Ribbons)
		{
			if (!Ribbon.Name.Equals(RoadName, ESearchCase::IgnoreCase)) continue;
			for (int32 i = 1; i + 1 < Ribbon.St.Num(); ++i)
			{
				const FSimCruiseRibbonStation& St = Ribbon.St[i];
				const FVector C = Origin + FVector(St.C.X * 100.0, St.C.Y * 100.0, 0.0);
				const double D = FVector::Dist2D(C, NearWorldCm);
				if (D >= Best) continue;
				// The rightmost travel lane, between the lines the markings draw.
				const FVector2d Dir = St.Travel < 0 ? -St.T : St.T;
				const FVector2d Right(-Dir.Y, Dir.X);
				const double Offset = SimCruiseLanes::RightLaneCentreM(Ribbon.Class, St.HalfW, St.Travel);
				const FVector2d P = St.C + Right * Offset;
				const double L = FVector2d::DotProduct(P - St.C, St.R);
				const double H = Ribbon.CarriagewayHeight(i, L);
				OutLocationCm = Origin + FVector(P.X * 100.0, P.Y * 100.0, H * 100.0);
				OutRotation = FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)), 0.0);
				Best = D;
				bFound = true;
			}
		}
	}
	return bFound;
}

void ASimCruiseWorldStreamer::Launch(const FSimCruiseTileJobTicket& Ticket)
{
	TSharedPtr<FInbox, ESPMode::ThreadSafe> Box = Inbox;
	TSharedPtr<FSimCruiseAtlasClient, ESPMode::ThreadSafe> Client = bUseHelper ? Atlas : nullptr;
	const double TileSizeM = Frame.TileSizeM;
	TSet<FString> KnownAerials;
	for (const auto& Pair : Resources->Aerials) KnownAerials.Add(Pair.Key);

	// Seam contracts of the resident neighbours: this tile meets the roads they already built.
	TArray<FSimCruiseSeamEnd> Contracts = SimCruiseTileBuilder::NeighbourContracts(Ticket.Tile, TileSizeM, [this](const FIntPoint& T) -> const FSimCruiseTileBuild*
	{
		const ASimCruiseWorldTile* Tile = GetTile(T);
		return Tile ? Tile->GetBuild() : nullptr;
	});

	auto BuildFrom = [Box, Ticket, TileSizeM, KnownAerials, Client, Contracts](FSimCruiseTileData&& Data)
	{
		TSharedPtr<FResult, ESPMode::ThreadSafe> Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		Result->Ticket = Ticket;
		if (Box->bClosed) return;
		Result->Build = SimCruiseTileBuilder::Build(Data, TileSizeM, Contracts);
		Result->AerialKey = Data.AerialKey;
		if (!Data.AerialKey.IsEmpty() && !KnownAerials.Contains(Data.AerialKey))
		{
			FString Path = SimCruiseTileData::BundledAerialPath(Data.AerialKey);
			if (Path.IsEmpty() && Client.IsValid())
			{
				const FString Cached = FPaths::Combine(Client->GetConfig().CacheDir, Data.AerialKey);
				// The tile command never fetches the photograph; the aerial command does (Drive.gd photo job).
				// Tiles of one zoom-14 photograph often build together: one worker fetches, the others wait for it.
				bool bClaimed = false;
				while (!Box->bClosed && !IFileManager::Get().FileExists(*Cached))
				{
					{
						FScopeLock Guard(&Box->AerialLock);
						if (!Box->AerialsFetching.Contains(Data.AerialKey))
						{
							Box->AerialsFetching.Add(Data.AerialKey);
							bClaimed = true;
						}
					}
					if (bClaimed) break;
					FPlatformProcess::Sleep(0.05f);
				}
				if (bClaimed)
				{
					if (!IFileManager::Get().FileExists(*Cached))
					{
						const FIntPoint A = SimCruiseGeo::AerialTileOf(Data.Id);
						FSimCruiseAtlasRequest Request;
						Request.Command = TEXT("aerial");
						Request.Args = { FString::FromInt(A.X), FString::FromInt(A.Y) };
						Request.TimeoutSeconds = 60.0;
						FSimCruiseAtlasClient::RunBlocking(Client->GetConfig(), Request, &Box->bClosed);
					}
					FScopeLock Guard(&Box->AerialLock);
					Box->AerialsFetching.Remove(Data.AerialKey);
				}
				if (IFileManager::Get().FileExists(*Cached)) Path = Cached;
			}
			USimCruiseWorldResources::DecodeImageFile(Path, Result->AerialPixels, Result->AerialWidth, Result->AerialHeight);
		}
		if (!Box->bClosed) Box->Queue.Enqueue(Result);
	};
	auto Fail = [Box, Ticket](const FString& Error)
	{
		TSharedPtr<FResult, ESPMode::ThreadSafe> Result = MakeShared<FResult, ESPMode::ThreadSafe>();
		Result->Ticket = Ticket;
		Result->Error = Error;
		if (!Box->bClosed) Box->Queue.Enqueue(Result);
	};

	if (SimCruiseTileData::HasBundledTile(Ticket.Tile))
	{
		const FString Path = SimCruiseTileData::BundledTilePath(Ticket.Tile);
		Async(EAsyncExecution::ThreadPool, [Path, BuildFrom, Fail]()
		{
			FSimCruiseTileData Data;
			FString Error;
			if (!SimCruiseTileData::LoadFile(Path, Data, Error)) { Fail(Error); return; }
			BuildFrom(MoveTemp(Data));
		});
		return;
	}
	if (!Client.IsValid() || !Client->GetConfig().IsConfigured())
	{
		Fail(TEXT("no bundled tile and no atlas helper"));
		return;
	}
	TSharedRef<FSimCruiseAtlasJob, ESPMode::ThreadSafe> Job = Client->RequestTile(Ticket.Tile);
	// The continuation runs where the promise is fulfilled: the helper's worker thread, under
	// the client's lock for a cancel, or right here on the game thread if the job already
	// finished. Parsing and the multi-second build therefore go to the thread pool.
	Job->Future.Next([BuildFrom, Fail](FSimCruiseAtlasResult Result)
	{
		if (!Result.IsOk() || !Result.Json.IsValid() || Result.Json->Type != EJson::Object)
		{
			Fail(Result.Describe());
			return;
		}
		Async(EAsyncExecution::ThreadPool, [BuildFrom, Fail, Json = MoveTemp(Result.Json)]()
		{
			FSimCruiseTileData Data;
			FString Error;
			if (!SimCruiseTileData::ParseObject(Json->AsObject(), Data, Error)) { Fail(Error); return; }
			BuildFrom(MoveTemp(Data));
		});
	});
}

void ASimCruiseWorldStreamer::RemoveTile(const FIntPoint& Tile)
{
	TObjectPtr<ASimCruiseWorldTile> Actor;
	if (Tiles.RemoveAndCopyValue(Tile, Actor) && Actor)
	{
		Actor->Destroy();
	}
}

void ASimCruiseWorldStreamer::Rebase(const FIntPoint& NewOrigin)
{
	const FVector Offset = Frame.RebaseTo(NewOrigin);
	for (auto& Pair : Tiles)
	{
		if (Pair.Value)
		{
			Pair.Value->SetActorLocation(Frame.TileOriginCm(Pair.Key), false, nullptr, ETeleportType::TeleportPhysics);
		}
	}
	FocusCm += Offset;
	UE_LOG(LogSimCruiseWorld, Display, TEXT("Floating origin moved to tile %s (offset %.0f, %.0f m)"), *SimCruiseTileName(NewOrigin), Offset.X / 100.0, Offset.Y / 100.0);
	OnRebased.Broadcast(Offset);
}

void ASimCruiseWorldStreamer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickStreaming(DeltaSeconds);
}

void ASimCruiseWorldStreamer::TickStreaming(float DeltaSeconds)
{
	if (!bStarted || !Frame.IsValid() || !Inbox.IsValid())
	{
		return;
	}
	FVector Velocity;
	FVector Focus = CurrentFocus(Velocity);

	// The origin is a tile corner, so a rebase distance under a tile diagonal would ask
	// for the tile the origin is already on: never broadcast a zero offset.
	if (FVector::Dist2D(Focus, FVector::ZeroVector) > RebaseDistanceM * 100.0 && Frame.TileAtWorldCm(Focus) != Frame.OriginTile)
	{
		Rebase(Frame.TileAtWorldCm(Focus));
		Focus = CurrentFocus(Velocity);
	}

	const FVector2d FocusTile = Frame.WorldCmToTileCoord(FVector2d(Focus.X, Focus.Y));
	const FVector2d Ahead = Frame.WorldCmToTileCoord(FVector2d(Focus.X + Velocity.X * 5.0, Focus.Y + Velocity.Y * 5.0));
	Manager.SetFocus(FocusTile, Ahead);

	for (const FIntPoint& Tile : Manager.TakeEvictions())
	{
		RemoveTile(Tile);
	}
	const double Now = FPlatformTime::Seconds();
	for (const FSimCruiseTileJobTicket& Ticket : Manager.TakeRequests(Now))
	{
		Launch(Ticket);
	}

	// Install at most one finished tile per frame (mesh sections and collision are game-thread work).
	TSharedPtr<FResult, ESPMode::ThreadSafe> Result;
	int32 Installed = 0;
	while (Installed < 1 && Inbox->Queue.Dequeue(Result))
	{
		if (!Result.IsValid()) continue;
		if (Result->AerialPixels.Num() > 0 && !Resources->FindAerial(Result->AerialKey))
		{
			Resources->AddAerial(Result->AerialKey, Result->AerialPixels, Result->AerialWidth, Result->AerialHeight);
		}
		const bool bOk = Result->Build.IsValid() && Result->Error.IsEmpty();
		if (!Manager.Complete(Result->Ticket, bOk, Now))
		{
			if (bOk) ++ResultsRejected;
			else UE_LOG(LogSimCruiseWorld, Warning, TEXT("Tile %s failed: %s"), *SimCruiseTileName(Result->Ticket.Tile), *Result->Error);
			continue;
		}
		const FIntPoint TileId = Result->Ticket.Tile;
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASimCruiseWorldTile* Tile = GetWorld()->SpawnActor<ASimCruiseWorldTile>(ASimCruiseWorldTile::StaticClass(), Frame.TileOriginCm(TileId), FRotator::ZeroRotator, Params);
		if (!Tile) continue;
#if WITH_EDITOR
		Tile->SetActorLabel(FString::Printf(TEXT("Tile_%s"), *SimCruiseTileName(TileId)));
#endif
		UTexture2D* Aerial = Resources->FindAerial(Result->AerialKey);
		const FSimCruiseTileBuild& B = *Result->Build;
		UE_LOG(LogSimCruiseWorld, Display, TEXT("Tile %s: %d ribbons, %d stations, %d markings, %d triangles, %d ground vertices conditioned, heights %.2f..%.2f m, built in %.0f ms%s"),
			*SimCruiseTileName(TileId), B.Roads.Ribbons.Num(), B.Roads.NumStations(), B.MarkingQuads, B.NumTriangles(), B.ConditionedVertices,
			B.MinHeightM, B.MaxHeightM, B.BuildMs, Aerial ? TEXT(", aerial") : TEXT(""));
		Tile->Install(Result->Build.ToSharedRef(), Resources, Aerial, true);
		Tiles.Add(TileId, Tile);
		++TilesInstalled;
		++Installed;
	}

	// Photographs no tile uses any more are released.
	if (Installed > 0)
	{
		TSet<FString> Keep;
		for (const auto& Pair : Tiles) if (Pair.Value && Pair.Value->GetBuild()) Keep.Add(Pair.Value->GetBuild()->AerialKey);
		Resources->TrimAerials(Keep);
	}
}
