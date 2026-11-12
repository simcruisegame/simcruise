// Copyright SimCruise contributors. All Rights Reserved.

#include "Live/SimCruiseLiveSky.h"

#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "UnrealClient.h"
#include "Components/StaticMeshComponent.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Presentation/SimCruiseCarMaterialLibrary.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseWorldStreamer.h"
#include "World/SimCruiseWorldSubsystem.h"

namespace SimCruiseLiveSkyPrivate
{
	enum ELight : int32 { PortRed, StarboardGreen, TailWhite, Beacon, StrobeLeft, StrobeRight, Landing, LightCount };

	/** Real sizes, m: length and wing span by ADS-B emitter category. */
	FVector2f SizeFor(const FString& Category)
	{
		if (Category == TEXT("A1")) { return {9.0f, 11.0f}; }    // light: Cessna 172
		if (Category == TEXT("A2")) { return {16.0f, 16.0f}; }   // small: business jets, turboprops
		if (Category == TEXT("A4")) { return {47.0f, 38.0f}; }   // high-vortex large: B757
		if (Category == TEXT("A5")) { return {64.0f, 61.0f}; }   // heavy: B777, A330, MD-11
		if (Category == TEXT("A7")) { return {13.0f, 11.0f}; }   // rotorcraft (rotor disc as span)
		return {38.0f, 35.0f};                                   // A3 large: A320, B737
	}

	/** Luminous intensity (cd) of each light (14 CFR 25.1391-25.1401 minima; landing lights of PAR-64 class). */
	constexpr float IntensityCd[LightCount] = {40.0f, 40.0f, 20.0f, 400.0f, 1500.0f, 1500.0f, 250000.0f};
	const FLinearColor Colour[LightCount] = {
		FLinearColor(1.0f, 0.05f, 0.03f), FLinearColor(0.1f, 1.0f, 0.4f), FLinearColor(1.0f, 1.0f, 1.0f), FLinearColor(1.0f, 0.06f, 0.03f),
		FLinearColor(1.0f, 1.0f, 1.0f), FLinearColor(1.0f, 1.0f, 1.0f), FLinearColor(1.0f, 0.95f, 0.85f)};
	constexpr float LightDiameterM = 0.4f;

	FRotator RotationFor(float TrackDeg, float GroundSpeedMs, float VerticalRateMs)
	{
		// Track is clockwise from north; the map frame has +X east and +Y south, so yaw = track - 90.
		const float Pitch = GroundSpeedMs > 5.0f ? FMath::RadiansToDegrees(FMath::Atan2(VerticalRateMs, GroundSpeedMs)) : 0.0f;
		return FRotator(Pitch, TrackDeg - 90.0f, 0.0f);
	}
}

using namespace SimCruiseLiveSkyPrivate;

ASimCruiseLiveSky::ASimCruiseLiveSky()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetActorEnableCollision(false);
}

bool ASimCruiseLiveSky::ResolveAssets()
{
	if (Master)
	{
		return true;
	}
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	Master = SimCruiseCarMaterials::GetMaster(ESimCruiseCarMaster::Opaque);
	if (!Cube || !Cylinder || !Sphere || !Master)
	{
		Master = nullptr;
		return false;
	}
	BodyMaterial = UMaterialInstanceDynamic::Create(Master, this);
	// Airliner white with a little grey, semi-gloss paint.
	BodyMaterial->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.62f, 0.63f, 0.65f, 1.0f));
	return true;
}

void ASimCruiseLiveSky::SetAircraft(const TArray<FSimCruiseLiveAircraft>& Aircraft, const FDateTime& ReceivedUtc)
{
	if (!ResolveAssets())
	{
		return;
	}
	TSet<FString> Seen;
	for (const FSimCruiseLiveAircraft& Report : Aircraft)
	{
		if (Report.bOnGround || Report.Hex.IsEmpty())
		{
			continue; // taxiing aircraft are hidden behind the airport fence
		}
		Seen.Add(Report.Hex);
		FPlane* Plane = Planes.Find(Report.Hex);
		if (!Plane)
		{
			Plane = &MakePlane(Report);
		}
		// The next tick starts the new report from where the aircraft is drawn now and fades
		// the difference out, so it glides onto its new track instead of jumping.
		Plane->Report = Report;
		Plane->ReportUtc = ReceivedUtc - FTimespan::FromSeconds(Report.PositionAgeS);
		Plane->bPlaced = false;
	}
	for (auto It = Planes.CreateIterator(); It; ++It)
	{
		if (!Seen.Contains(It.Key()))
		{
			DestroyPlane(It.Value());
			It.RemoveCurrent();
		}
	}
}

ASimCruiseLiveSky::FPlane& ASimCruiseLiveSky::MakePlane(const FSimCruiseLiveAircraft& Report)
{
	FPlane& Plane = Planes.Add(Report.Hex);
	Plane.Phase = static_cast<float>(GetTypeHash(Report.Hex) % 1000) / 1000.0f;
	const FVector2f Size = SizeFor(Report.Category);
	const float L = Size.X * 100.0f, S = Size.Y * 100.0f, D = L * 0.095f;

	USceneComponent* Root = NewObject<USceneComponent>(this);
	Root->SetupAttachment(RootComponent);
	Root->SetMobility(EComponentMobility::Movable);
	Root->RegisterComponent();
	Plane.Root = Root;
	Owned.Add(Root);

	auto Part = [&](UStaticMesh* Mesh, const FVector& At, const FRotator& Rotation, const FVector& SizeCm, UMaterialInterface* Material)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetMobility(EComponentMobility::Movable);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false); // a shadow kilometres below on the ground would be wrong in size and place
		C->SetupAttachment(Root);
		C->SetRelativeLocationAndRotation(At, Rotation);
		C->SetRelativeScale3D(SizeCm / 100.0f);
		C->SetMaterial(0, Material);
		C->bNeverDistanceCull = true;
		C->RegisterComponent();
		Owned.Add(C);
		return C;
	};

	if (Report.Category == TEXT("A7"))
	{
		Plane.Body.Add(Part(Sphere, FVector::ZeroVector, FRotator::ZeroRotator, FVector(L * 0.35f, D * 2.2f, D * 2.2f), BodyMaterial));
		Plane.Body.Add(Part(Cube, FVector(-L * 0.3f, 0, 0), FRotator::ZeroRotator, FVector(L * 0.5f, D * 0.4f, D * 0.5f), BodyMaterial));
		Plane.Body.Add(Part(Cylinder, FVector(0, 0, D * 1.3f), FRotator::ZeroRotator, FVector(S, S, 8.0f), BodyMaterial));
	}
	else
	{
		// Fuselage along +X (the cylinder's axis is Z), wings, tailplane and fin.
		Plane.Body.Add(Part(Cylinder, FVector::ZeroVector, FRotator(90.0f, 0, 0), FVector(D, D, L), BodyMaterial));
		Plane.Body.Add(Part(Sphere, FVector(L * 0.5f, 0, 0), FRotator::ZeroRotator, FVector(D * 1.6f, D, D), BodyMaterial));
		Plane.Body.Add(Part(Cube, FVector(L * 0.03f, 0, -D * 0.25f), FRotator(0, 0, 0), FVector(L * 0.13f, S, D * 0.12f), BodyMaterial));
		Plane.Body.Add(Part(Cube, FVector(-L * 0.45f, 0, D * 0.1f), FRotator::ZeroRotator, FVector(L * 0.08f, S * 0.34f, D * 0.1f), BodyMaterial));
		Plane.Body.Add(Part(Cube, FVector(-L * 0.42f, 0, D * 0.5f + L * 0.08f), FRotator(0, 0, 0), FVector(L * 0.13f, D * 0.12f, L * 0.16f), BodyMaterial));
	}

	const FVector LightAt[LightCount] = {
		FVector(L * 0.0f, -S * 0.5f, -D * 0.2f), FVector(L * 0.0f, S * 0.5f, -D * 0.2f), FVector(-L * 0.5f, 0, 0), FVector(0, 0, -D * 0.55f),
		FVector(-L * 0.03f, -S * 0.5f, -D * 0.2f), FVector(-L * 0.03f, S * 0.5f, -D * 0.2f), FVector(L * 0.12f, 0, -D * 0.5f)};
	for (int32 Index = 0; Index < LightCount; ++Index)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Master, this);
		Material->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.02f, 0.02f, 0.02f, 1.0f));
		Material->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0, 0, 0, 0));
		Owned.Add(Material);
		Plane.LightMaterials.Add(Material);
		Plane.Lights.Add(Part(Sphere, LightAt[Index], FRotator::ZeroRotator, FVector(LightDiameterM * 100.0f), Material));
	}
	return Plane;
}

void ASimCruiseLiveSky::DestroyPlane(FPlane& Plane)
{
	auto Drop = [this](UObject* Object)
	{
		Owned.Remove(Object);
		if (UActorComponent* Component = Cast<UActorComponent>(Object))
		{
			Component->DestroyComponent();
		}
	};
	for (UStaticMeshComponent* C : Plane.Body) { Drop(C); }
	for (UStaticMeshComponent* C : Plane.Lights) { Drop(C); }
	for (UMaterialInstanceDynamic* M : Plane.LightMaterials) { Owned.Remove(M); }
	Drop(Plane.Root);
}

void ASimCruiseLiveSky::ShiftOrigin(const FVector& OffsetCm)
{
	for (TPair<FString, FPlane>& Pair : Planes)
	{
		if (!Pair.Value.LastWorldCm.IsZero())
		{
			Pair.Value.LastWorldCm += OffsetCm;
		}
	}
}

void ASimCruiseLiveSky::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ClockSeconds += DeltaSeconds;
	FVector ViewCm = FVector::ZeroVector;
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		ViewCm = Camera->GetCameraLocation();
	}
	const FDateTime Now = FDateTime::UtcNow();
	for (TPair<FString, FPlane>& Pair : Planes)
	{
		UpdatePlane(Pair.Value, Now, ViewCm, DeltaSeconds);
	}
	TickShot(DeltaSeconds, ViewCm);
}

FString ASimCruiseLiveSky::StartShot(const FString& Name, float FovDeg)
{
	FVector ViewCm = FVector::ZeroVector;
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		ViewCm = Camera->GetCameraLocation();
	}
	const FPlane* Best = nullptr;
	double BestRange = TNumericLimits<double>::Max();
	for (const TPair<FString, FPlane>& Pair : Planes)
	{
		const FVector To = Pair.Value.LastWorldCm - ViewCm;
		const double Range = To.Size();
		const double Elevation = FMath::RadiansToDegrees(FMath::Asin(To.Z / FMath::Max(Range, 1.0)));
		// The highest in the sky: low ones hide behind the buildings.
		if (Pair.Value.bPlaced && Elevation > 3.0 && -Elevation < BestRange)
		{
			Best = &Pair.Value;
			BestRange = -Elevation;
		}
	}
	if (!Best)
	{
		return FString();
	}
	// The driver's own renderer, exposure and all: a camera on a 30 m mast over the car
	// becomes the view for two seconds, then the frame is saved and the view goes back.
	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	if (!Controller)
	{
		return FString();
	}
	if (!ShotCamera)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ShotCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
	}
	if (!ShotCamera)
	{
		return FString();
	}
	ShotCamera->GetCameraComponent()->SetFieldOfView(FovDeg);
	ShotCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	ShotReturnTarget = Controller->GetViewTarget();
	Controller->SetViewTarget(ShotCamera);
	ShotName = Name;
	ShotHex = Best->Report.Hex;
	ShotSeconds = 0.0f;
	const FVector To = Best->LastWorldCm - ViewCm;
	return FString::Printf(TEXT("%s %s (%s) %.1f km away, %.0f m up, elevation %.1f deg, bearing %.0f deg"), *Best->Report.Flight, *Best->Report.Type,
		*Best->Report.Hex, To.Size() / 100000.0, Best->Report.AltitudeM, FMath::RadiansToDegrees(FMath::Asin(To.Z / To.Size())),
		FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(To.X, -To.Y)) + 360.0, 360.0));
}

void ASimCruiseLiveSky::TickShot(float DeltaSeconds, const FVector& ViewCm)
{
	if (ShotName.IsEmpty() || !ShotCamera)
	{
		return;
	}
	APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
	const FPlane* Plane = Planes.Find(ShotHex);
	auto Finish = [&]()
	{
		if (Controller && ShotReturnTarget.IsValid())
		{
			Controller->SetViewTarget(ShotReturnTarget.Get());
		}
		ShotName.Reset();
	};
	if (!Plane || !Controller)
	{
		Finish();
		return;
	}
	// While the shot camera is the view, the player camera manager reports the mast: keep
	// the mast where it started.
	if (ShotSeconds == 0.0f)
	{
		ShotFrom = (ShotReturnTarget.IsValid() ? ShotReturnTarget->GetActorLocation() : ViewCm) + FVector(0.0, 0.0, 3000.0);
	}
	ShotCamera->SetActorLocationAndRotation(ShotFrom, (Plane->LastWorldCm - ShotFrom).Rotation());
	ShotSeconds += DeltaSeconds;
	if (ShotSeconds > 2.5f && !bShotRequested)
	{
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (ShotName + TEXT(".png")), false, false);
		bShotRequested = true;
	}
	if (ShotSeconds > 3.0f)
	{
		bShotRequested = false;
		Finish();
	}
}

void ASimCruiseLiveSky::UpdatePlane(FPlane& Plane, const FDateTime& NowUtc, const FVector& ViewCm, float DeltaSeconds)
{
	const USimCruiseWorldSubsystem* WorldSubsystem = GetWorld()->GetSubsystem<USimCruiseWorldSubsystem>();
	const ASimCruiseWorldStreamer* Streamer = WorldSubsystem ? WorldSubsystem->GetStreamer() : nullptr;
	if (!Streamer || !Streamer->GetFrame().IsValid() || !Plane.Root)
	{
		return;
	}
	const FSimCruiseWorldFrame& Frame = Streamer->GetFrame();
	// Dead reckoning is good for about half a minute; after that the aircraft holds its place.
	const double Age = FMath::Clamp((NowUtc - Plane.ReportUtc).GetTotalSeconds(), 0.0, 30.0);
	double Lat = 0.0, Lon = 0.0;
	float AltitudeM = 0.0f;
	FSimCruiseLiveData::Extrapolate(Plane.Report, Age, Lat, Lon, AltitudeM);
	FVector WorldCm = Frame.LonLatToWorldCm(Lon, Lat, AltitudeM - OriginElevationM);
	const double GroundDistanceM = FVector2D::Distance(FVector2D(WorldCm), FVector2D(ViewCm)) / 100.0;
	WorldCm.Z -= FSimCruiseLiveData::CurvatureDropM(GroundDistanceM) * 100.0;

	if (!Plane.bPlaced)
	{
		// A fresh report: carry the gap from where it was drawn and fade it out.
		if (!Plane.LastWorldCm.IsZero())
		{
			Plane.SmoothingOffsetCm = Plane.LastWorldCm - WorldCm;
			if (Plane.SmoothingOffsetCm.Size() > 200000.0) // over 2 km: a new track, not a correction
			{
				Plane.SmoothingOffsetCm = FVector::ZeroVector;
			}
		}
		Plane.bPlaced = true;
	}
	Plane.SmoothingOffsetCm *= FMath::Exp(-DeltaSeconds / 0.7f);
	const FVector Drawn = WorldCm + Plane.SmoothingOffsetCm;
	Plane.LastWorldCm = Drawn;
	const FRotator Rotation = RotationFor(Plane.Report.TrackDeg, Plane.Report.GroundSpeedMs, Plane.Report.VerticalRateMs);
	Plane.Root->SetWorldLocationAndRotation(Drawn, Rotation);

	// Lights: minimum apparent size about two pixels, intensity conserved.
	float PixelRad = 0.0008f;
	if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		FVector2D Viewport(1920.0, 1080.0);
		if (GetWorld()->GetGameViewport())
		{
			GetWorld()->GetGameViewport()->GetViewportSize(Viewport);
		}
		PixelRad = FMath::DegreesToRadians(Camera->GetFOVAngle()) / FMath::Max(Viewport.X, 320.0);
	}
	const double RangeM = FVector::Distance(Drawn, ViewCm) / 100.0;
	const float Dark = FMath::Clamp(static_cast<float>((-4.0 - SunElevationDeg) / 4.0), 0.0f, 1.0f);
	const float DisplayDiameterM = FMath::Lerp(LightDiameterM, FMath::Max(LightDiameterM, static_cast<float>(RangeM * PixelRad * 2.2)), Dark);
	const float AreaRatio = FMath::Square(LightDiameterM / DisplayDiameterM);
	const float ProjectedArea = PI * FMath::Square(LightDiameterM * 0.5f);

	const float T = FMath::Fmod(static_cast<float>(ClockSeconds) + Plane.Phase * 1.3f, 1.2f);
	const bool bStrobe = T < 0.05f || (T > 0.15f && T < 0.2f);
	const bool bBeacon = FMath::Fmod(static_cast<float>(ClockSeconds) + Plane.Phase, 1.0f) < 0.12f;
	// Landing lights: on below 3 km (10 000 ft rule), seen brightly only from ahead.
	const FVector Forward = Rotation.Vector();
	const FVector ToViewer = (ViewCm - Drawn).GetSafeNormal();
	const float Facing = FMath::Pow(FMath::Max(0.0f, static_cast<float>(FVector::DotProduct(Forward, ToViewer))), 12.0f);
	const bool bLanding = Plane.Report.AltitudeM < 3000.0f;

	const float Scale = FMath::Clamp(DisplayDiameterM / LightDiameterM, 1.0f, 1.0e6f);
	for (int32 Index = 0; Index < LightCount && Index < Plane.Lights.Num(); ++Index)
	{
		float On = 1.0f;
		if (Index == Beacon) { On = bBeacon ? 1.0f : 0.0f; }
		if (Index == StrobeLeft || Index == StrobeRight) { On = bStrobe ? 1.0f : 0.0f; }
		if (Index == Landing) { On = bLanding ? FMath::Max(Facing, 0.002f) : 0.0f; }
		const float Nits = On * IntensityCd[Index] / ProjectedArea * AreaRatio;
		Plane.Lights[Index]->SetVisibility(On > 0.0f);
		Plane.Lights[Index]->SetRelativeScale3D(FVector(LightDiameterM * Scale));
		Plane.LightMaterials[Index]->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(Colour[Index].R, Colour[Index].G, Colour[Index].B, Nits));
	}
}
