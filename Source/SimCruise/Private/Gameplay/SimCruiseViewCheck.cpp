// Copyright SimCruise contributors. All Rights Reserved.

#include "Gameplay/SimCruiseViewCheck.h"

#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/SimCruiseDriveTest.h"
#include "Gameplay/SimCruiseGameMode.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "ProceduralMeshComponent.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"
#include "World/SimCruiseBridges.h"
#include "World/SimCruiseGuardRails.h"
#include "World/SimCruiseRegionalTerrain.h"
#include "World/SimCruiseSignals.h"
#include "World/SimCruiseTileBuilder.h"
#include "World/SimCruiseTraffic.h"
#include "World/SimCruiseTrees.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldTile.h"

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseViewCheck, Log, All);

namespace
{
	constexpr int32 GridX = 48;
	constexpr int32 GridY = 27;
	constexpr double RangeCm = 200000.0;
	/** A ray counts as below the horizon when it points more than about 1 deg down. */
	constexpr double BelowHorizonZ = -0.02;
	/** Wait at most this long (s) for the car, the tiles and the shaders. */
	constexpr double ReadyTimeoutS = 600.0;
	constexpr double ShotTimeoutS = 15.0;

	/** The class of one actor, or Count when it is none of the known kinds. */
	ESimCruiseViewHit ClassifyActor(const AActor* Actor)
	{
		if (Actor->IsA<ASimCruiseCarPresentationActor>() || Actor->IsA<ASimCruiseTrafficCar>()) return ESimCruiseViewHit::Car;
		if (Actor->IsA<ASimCruiseRegionalTerrain>()) return ESimCruiseViewHit::Ground;
		if (Actor->IsA<ASimCruiseTrees>()) return ESimCruiseViewHit::Tree;
		if (Actor->IsA<ASimCruiseBridges>() || Actor->IsA<ASimCruiseGuardRails>() || Actor->IsA<ASimCruiseSignals>()) return ESimCruiseViewHit::Props;
		return ESimCruiseViewHit::Count;
	}

	ESimCruiseViewHit Classify(const FHitResult& Hit)
	{
		const AActor* Actor = Hit.GetActor();
		if (!Actor)
		{
			return ESimCruiseViewHit::Other;
		}
		if (const ASimCruiseWorldTile* Tile = Cast<ASimCruiseWorldTile>(Actor))
		{
			const UPrimitiveComponent* Component = Hit.GetComponent();
			if (Component && Component == Tile->Mesh.Get())
			{
				switch (Tile->LayerAtFace(Hit.FaceIndex))
				{
				case ESimCruiseTileLayer::Terrain: return ESimCruiseViewHit::Ground;
				case ESimCruiseTileLayer::Asphalt:
				case ESimCruiseTileLayer::Concrete: return ESimCruiseViewHit::Road;
				case ESimCruiseTileLayer::Building: return ESimCruiseViewHit::Building;
				case ESimCruiseTileLayer::Props: return ESimCruiseViewHit::Props;
				default: return ESimCruiseViewHit::Other;
				}
			}
			// The tile's palms are instanced components.
			return Component && Component->IsA<UInstancedStaticMeshComponent>() ? ESimCruiseViewHit::Tree : ESimCruiseViewHit::Props;
		}
		// A car's parts can be actors attached to the car.
		for (const AActor* Each = Actor; Each; Each = Each->GetAttachParentActor())
		{
			const ESimCruiseViewHit Kind = ClassifyActor(Each);
			if (Kind != ESimCruiseViewHit::Count) return Kind;
			if (Each->IsA<ASimCruiseWorldTile>()) break;
		}
		return ESimCruiseViewHit::Other;
	}
}

float FSimCruiseViewStats::WorldPercent() const
{
	return Get(ESimCruiseViewHit::Road) + Get(ESimCruiseViewHit::Building) + Get(ESimCruiseViewHit::Ground)
		+ Get(ESimCruiseViewHit::Tree) + Get(ESimCruiseViewHit::Car) + Get(ESimCruiseViewHit::Props);
}

namespace SimCruiseViewCheckRules
{
	const TCHAR* Name(ESimCruiseViewHit Hit)
	{
		switch (Hit)
		{
		case ESimCruiseViewHit::Road: return TEXT("road");
		case ESimCruiseViewHit::Building: return TEXT("building");
		case ESimCruiseViewHit::Ground: return TEXT("ground");
		case ESimCruiseViewHit::Tree: return TEXT("tree");
		case ESimCruiseViewHit::Car: return TEXT("car");
		case ESimCruiseViewHit::Props: return TEXT("props");
		case ESimCruiseViewHit::Other: return TEXT("other");
		case ESimCruiseViewHit::Empty: return TEXT("empty");
		default: return TEXT("?");
		}
	}

	FString Judge(const FSimCruiseViewStats& Stats)
	{
		TArray<FString> Reasons;
		if (Stats.RaysBelowHorizon == 0)
		{
			Reasons.Add(TEXT("no-rays-below-horizon"));
		}
		else if (Stats.WorldPercent() < MinWorldPercent)
		{
			Reasons.Add(FString::Printf(TEXT("world-%.0f%%-below-%.0f%%"), Stats.WorldPercent(), MinWorldPercent));
		}
		if (Stats.bPlaceHasRoads && (Stats.NearestRoadM < 0.f || Stats.NearestRoadM > MaxRoadDistanceM))
		{
			Reasons.Add(FString::Printf(TEXT("no-road-within-%.0fm"), MaxRoadDistanceM));
		}
		if (Stats.bPlaceHasBuildings && Stats.BuildingCells == 0)
		{
			Reasons.Add(TEXT("no-building-in-view"));
		}
		if (!Stats.bHasImage)
		{
			Reasons.Add(TEXT("no-screenshot"));
		}
		else if (Stats.Edges < MinEdges)
		{
			Reasons.Add(FString::Printf(TEXT("flat-image-edges-%.2f-below-%.2f"), Stats.Edges, MinEdges));
		}
		return FString::Join(Reasons, TEXT(","));
	}

	void MeasureImage(int32 Width, int32 Height, const TArray<FColor>& Pixels, FSimCruiseViewStats& Stats)
	{
		Stats.bHasImage = false;
		if (Width < 2 || Height < 2 || Pixels.Num() < Width * Height)
		{
			return;
		}
		const int32 Y0 = FMath::FloorToInt32(Height * 0.4f);
		const int32 Rows = Height - Y0;
		TArray<float> Luma;
		Luma.SetNumUninitialized(Width * Rows);
		constexpr int32 HueBinCount = 36;
		int32 Hues[HueBinCount] = {};
		double Sum = 0.0;
		for (int32 Y = 0; Y < Rows; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor& C = Pixels[(Y0 + Y) * Width + X];
				const float L = 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
				Luma[Y * Width + X] = L;
				Sum += L;
				const int32 Max = FMath::Max3(C.R, C.G, C.B);
				const int32 Min = FMath::Min3(C.R, C.G, C.B);
				// Coloured pixels only: grey, black and white have no hue.
				if (Max > 38 && (Max - Min) > 0.2f * Max)
				{
					const float D = static_cast<float>(Max - Min);
					float Hue = 0.f;
					if (Max == C.R) Hue = 60.f * FMath::Fmod((C.G - C.B) / D + 6.f, 6.f);
					else if (Max == C.G) Hue = 60.f * ((C.B - C.R) / D + 2.f);
					else Hue = 60.f * ((C.R - C.G) / D + 4.f);
					++Hues[FMath::Clamp(FMath::FloorToInt32(Hue / 10.f), 0, HueBinCount - 1)];
				}
			}
		}
		const double Count = static_cast<double>(Width) * Rows;
		const double Mean = Sum / Count;
		double Var = 0.0;
		double Gradient = 0.0;
		int64 GradientCount = 0;
		for (int32 Y = 0; Y < Rows; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const float L = Luma[Y * Width + X];
				Var += FMath::Square(L - Mean);
				if (X + 1 < Width && Y + 1 < Rows)
				{
					Gradient += FMath::Abs(Luma[Y * Width + X + 1] - L) + FMath::Abs(Luma[(Y + 1) * Width + X] - L);
					++GradientCount;
				}
			}
		}
		Stats.LumaStdDev = static_cast<float>(FMath::Sqrt(Var / Count));
		Stats.Edges = GradientCount > 0 ? static_cast<float>(Gradient / GradientCount) : 0.f;
		Stats.HueBins = 0;
		for (int32 Bin = 0; Bin < HueBinCount; ++Bin)
		{
			Stats.HueBins += Hues[Bin] > Count * 0.005 ? 1 : 0;
		}
		Stats.bHasImage = true;
	}

	FString Slug(const FString& Title)
	{
		FString Out;
		bool bDash = false;
		for (const TCHAR Ch : Title.ToLower())
		{
			if (FChar::IsAlnum(Ch) && Ch < 128)
			{
				if (bDash && !Out.IsEmpty()) Out.AppendChar(TEXT('-'));
				Out.AppendChar(Ch);
				bDash = false;
			}
			else
			{
				bDash = true;
			}
		}
		Out.LeftInline(48);
		return Out.IsEmpty() ? FString(TEXT("place")) : Out;
	}
}

ASimCruiseViewCheck::ASimCruiseViewCheck()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ASimCruiseViewCheck::IsRequested(const TCHAR* CommandLine)
{
	float Ignored = 0.f;
	return FParse::Param(CommandLine, TEXT("SimCruiseViewCheck")) || FParse::Value(CommandLine, TEXT("SimCruiseViewCheck="), Ignored);
}

void ASimCruiseViewCheck::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("SimCruiseViewCheck="), DelaySeconds);
	DelaySeconds = FMath::Max(0.f, DelaySeconds);
	bWantPad = FParse::Param(Cmd, TEXT("SimCruiseDriveTestPad"));
	bQuit = FParse::Param(Cmd, TEXT("SimCruiseViewCheckQuit"));
	Started = FPlatformTime::Seconds();
	StageStarted = Started;
	UE_LOG(LogSimCruiseViewCheck, Display, TEXT("View check: %.0f s after the car is placed and the tiles are built%s"),
		DelaySeconds, bWantPad ? TEXT(" (on the drive robot's test pad)") : TEXT(""));
}

void ASimCruiseViewCheck::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ShotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ShotHandle);
		ShotHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void ASimCruiseViewCheck::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = FPlatformTime::Seconds();
	APlayerController* Player = UGameplayStatics::GetPlayerController(this, 0);
	const APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr;
	const bool bShadersBusy = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
	const bool bTiles = !Stream || (Pawn && Stream->AreTilesReadyAround(Pawn->GetActorLocation(), 1));
	const bool bPlaced = Pawn && (Mode ? Mode->IsDriveReady() : true);
	const bool bReady = bPlaced && bTiles && !bShadersBusy && (!bWantPad || SimCruiseTestPad::Find(GetWorld()));

	switch (Stage)
	{
	case EStage::WaitReady:
		if (bReady || Now - Started > ReadyTimeoutS)
		{
			if (!bReady)
			{
				UE_LOG(LogSimCruiseViewCheck, Warning, TEXT("View check: not ready after %.0f s; looking anyway"), ReadyTimeoutS);
			}
			Stage = EStage::Delay;
			StageStarted = Now;
		}
		break;
	case EStage::Delay:
		// A tile build or a shader compile restarts the wait (unless it waited too long already).
		if (!bReady && Now - Started < ReadyTimeoutS)
		{
			StageStarted = Now;
			break;
		}
		if (Now - StageStarted >= DelaySeconds && Player)
		{
			TraceView(Player);
			PlaceTitle = Mode ? Mode->GetPlace().Title : FString();
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"), TEXT("ViewCheck")));
			IFileManager::Get().MakeDirectory(*Dir, true);
			ShotFile = FPaths::Combine(Dir, FString::Printf(TEXT("%s-%s.png"), *SimCruiseViewCheckRules::Slug(PlaceTitle),
				*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
			// With the delegate bound, the engine hands over the pixels and saves nothing itself.
			ShotHandle = UGameViewportClient::OnScreenshotCaptured().AddUObject(this, &ASimCruiseViewCheck::OnScreenshot);
			FScreenshotRequest::RequestScreenshot(ShotFile, true, false);
			Stage = EStage::WaitShot;
			StageStarted = Now;
		}
		break;
	case EStage::WaitShot:
		if (bShotDone || Now - StageStarted > ShotTimeoutS)
		{
			Report();
			Stage = EStage::Done;
			if (bQuit)
			{
				if (Player)
				{
					Player->ConsoleCommand(TEXT("quit"));
				}
				else
				{
					FPlatformMisc::RequestExit(false);
				}
			}
		}
		break;
	default:
		break;
	}
}

void ASimCruiseViewCheck::TraceView(APlayerController* Player)
{
	Stats = FSimCruiseViewStats();
	int32 SizeX = 0, SizeY = 0;
	Player->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0)
	{
		SizeX = 1280;
		SizeY = 720;
	}
	const APawn* Pawn = Player->GetPawn();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruiseViewCheck), true);
	Params.bReturnFaceIndex = true;
	if (Pawn)
	{
		Params.AddIgnoredActor(Pawn);
		TArray<AActor*> Attached;
		Pawn->GetAttachedActors(Attached, true, true);
		Params.AddIgnoredActors(Attached);
	}
	const FVector CarCm = Pawn ? Pawn->GetActorLocation() : Player->PlayerCameraManager->GetCameraLocation();
	int32 Counts[static_cast<int32>(ESimCruiseViewHit::Count)] = {};
	TSet<FIntVector> BuildingCells;
	float NearestRoadCm = TNumericLimits<float>::Max();
	for (int32 J = 0; J < GridY; ++J)
	{
		for (int32 I = 0; I < GridX; ++I)
		{
			const FVector2D Screen((I + 0.5) / GridX * SizeX, (J + 0.5) / GridY * SizeY);
			FVector Origin, Direction;
			if (!UGameplayStatics::DeprojectScreenToWorld(Player, Screen, Origin, Direction))
			{
				continue;
			}
			FHitResult Hit;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * RangeCm, ECC_Visibility, Params);
			const ESimCruiseViewHit Kind = bHit ? Classify(Hit) : ESimCruiseViewHit::Empty;
			if (Direction.Z < BelowHorizonZ)
			{
				++Stats.RaysBelowHorizon;
				++Counts[static_cast<int32>(Kind)];
			}
			if (Kind == ESimCruiseViewHit::Road)
			{
				NearestRoadCm = FMath::Min(NearestRoadCm, static_cast<float>(FVector::Dist(Hit.ImpactPoint, CarCm)));
			}
			else if (Kind == ESimCruiseViewHit::Building)
			{
				BuildingCells.Add(FIntVector(FMath::FloorToInt32(Hit.ImpactPoint.X / 1000.0), FMath::FloorToInt32(Hit.ImpactPoint.Y / 1000.0),
					FMath::FloorToInt32(Hit.ImpactPoint.Z / 1000.0)));
			}
		}
	}
	for (int32 K = 0; K < static_cast<int32>(ESimCruiseViewHit::Count); ++K)
	{
		Stats.Percent[K] = Stats.RaysBelowHorizon > 0 ? 100.f * Counts[K] / Stats.RaysBelowHorizon : 0.f;
	}
	Stats.NearestRoadM = NearestRoadCm < TNumericLimits<float>::Max() ? NearestRoadCm / 100.f : -1.f;
	Stats.BuildingCells = BuildingCells.Num();

	// What the map holds round the car: a place without roads or buildings is not judged on them.
	const ASimCruiseGameMode* Mode = GetWorld()->GetAuthGameMode<ASimCruiseGameMode>();
	if (const ASimCruiseWorldStreamer* Stream = Mode ? Mode->GetStreamer() : nullptr)
	{
		const FIntPoint Here = Stream->GetFrame().TileAtWorldCm(CarCm);
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				const ASimCruiseWorldTile* Tile = Stream->GetTile(Here + FIntPoint(DX, DY));
				const FSimCruiseTileBuild* Build = Tile ? Tile->GetBuild() : nullptr;
				if (!Build) continue;
				Stats.bPlaceHasRoads |= Build->Roads.Ribbons.Num() > 0;
				Stats.bPlaceHasBuildings |= Build->Buildings > 0;
			}
		}
	}
}

void ASimCruiseViewCheck::OnScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
	if (ShotHandle.IsValid())
	{
		UGameViewportClient::OnScreenshotCaptured().Remove(ShotHandle);
		ShotHandle.Reset();
	}
	SimCruiseViewCheckRules::MeasureImage(Width, Height, Pixels, Stats);
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	if (Png.IsEmpty() || !FFileHelper::SaveArrayToFile(Png, *ShotFile))
	{
		UE_LOG(LogSimCruiseViewCheck, Warning, TEXT("View check: could not save %s"), *ShotFile);
	}
	bShotDone = true;
}

void ASimCruiseViewCheck::Report()
{
	using namespace SimCruiseViewCheckRules;
	Reasons = Judge(Stats);
	const bool bPass = Reasons.IsEmpty();
	bPassed = bPass;
	FString Classes;
	FString JsonClasses;
	for (int32 K = 0; K < static_cast<int32>(ESimCruiseViewHit::Count); ++K)
	{
		const TCHAR* N = Name(static_cast<ESimCruiseViewHit>(K));
		Classes += FString::Printf(TEXT(" %s=%.0f%%"), N, Stats.Percent[K]);
		JsonClasses += FString::Printf(TEXT("%s\"%s\": %.1f"), K > 0 ? TEXT(", ") : TEXT(""), N, Stats.Percent[K]);
	}
	const FString Shot = Stats.bHasImage ? ShotFile : FString(TEXT("none"));
	UE_LOG(LogSimCruiseViewCheck, Display,
		TEXT("VIEWCHECK %s place=\"%s\"%s world=%.0f%% road_near=%.0fm building_cells=%d edges=%.2f lumsd=%.1f hues=%d shot=%s reason=%s"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *PlaceTitle, *Classes, Stats.WorldPercent(), Stats.NearestRoadM, Stats.BuildingCells,
		Stats.Edges, Stats.LumaStdDev, Stats.HueBins, *Shot, bPass ? TEXT("ok") : *Reasons);

	const FString JsonFile = FPaths::ChangeExtension(ShotFile, TEXT("json"));
	const FString Json = FString::Printf(
		TEXT("{\n  \"result\": \"%s\",\n  \"place\": \"%s\",\n  \"test_pad\": %s,\n  \"rays_below_horizon\": %d,\n  \"percent\": {%s},\n")
		TEXT("  \"world_percent\": %.1f,\n  \"nearest_road_m\": %.1f,\n  \"building_cells\": %d,\n  \"place_has_roads\": %s,\n  \"place_has_buildings\": %s,\n")
		TEXT("  \"edges\": %.3f,\n  \"luma_sd\": %.2f,\n  \"hue_bins\": %d,\n  \"screenshot\": \"%s\",\n  \"reason\": \"%s\",\n")
		TEXT("  \"thresholds\": {\"min_world_percent\": %.0f, \"max_road_distance_m\": %.0f, \"min_edges\": %.2f}\n}\n"),
		bPass ? TEXT("PASS") : TEXT("FAIL"), *PlaceTitle.ReplaceCharWithEscapedChar(), bWantPad ? TEXT("true") : TEXT("false"), Stats.RaysBelowHorizon, *JsonClasses,
		Stats.WorldPercent(), Stats.NearestRoadM, Stats.BuildingCells, Stats.bPlaceHasRoads ? TEXT("true") : TEXT("false"),
		Stats.bPlaceHasBuildings ? TEXT("true") : TEXT("false"), Stats.Edges, Stats.LumaStdDev, Stats.HueBins,
		*Shot.Replace(TEXT("\\"), TEXT("/")), bPass ? TEXT("ok") : *Reasons, MinWorldPercent, MaxRoadDistanceM, MinEdges);
	if (!ShotFile.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Json, *JsonFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
}
