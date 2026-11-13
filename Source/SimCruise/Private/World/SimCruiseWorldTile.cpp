// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseWorldTile.h"

#include "Misc/Parse.h"

#include "Misc/CommandLine.h"

#include "CollisionQueryParams.h"
#include "Engine/HitResult.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "StaticMeshResources.h"
#include "Misc/App.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Presentation/SimCruiseCarAssets.h"
#include "Presentation/SimCruiseCarPresentationComponent.h"
#include "World/SimCruiseGeo.h"
#include "World/SimCruiseWorldResources.h"

namespace
{
	TAutoConsoleVariable<int32> CVarSigns(TEXT("simcruise.World.Signs"), 1,
		TEXT("1: the named places' lettering on their buildings (tiles installed after the change); 0: none, for measuring their cost."));
	TAutoConsoleVariable<int32> CVarParkedCars(TEXT("simcruise.World.ParkedCars"), 1,
		TEXT("1: parked cars in the parking lanes' stalls (drawn runs only); 0: empty stalls."));

	/**
	 * The cars that park: the bundled models with a fitted presentation. Mostly what a
	 * Miami street holds (SUVs, a pickup, a minivan, an electric sedan), and the classics
	 * and sports cars Ocean Drive is known for.
	 */
	const TCHAR* ParkedCarFor(uint8 Pick)
	{
		struct FShare { int32 UpTo; const TCHAR* Id; };
		static const FShare Shares[] = {
			{ 25, TEXT("nb1999") }, { 50, TEXT("mustang65") }, { 75, TEXT("meridian_z") }, { 110, TEXT("model3") },
			{ 145, TEXT("grandcherokee13") }, { 175, TEXT("f150raptor18") }, { 205, TEXT("odyssey18") }, { 256, TEXT("crv25") },
		};
		for (const FShare& Share : Shares)
		{
			if (Pick < Share.UpTo) return Share.Id;
		}
		return TEXT("meridian_z");
	}

	/** Cars whose model could not be fitted: tried once per run, then a stand-in parks there. */
	TSet<FString> UnfitCars;

	/**
	 * Body colours in about the shares US cars have them (white 25 %, black 20 %, grey 18 %,
	 * silver 11 %, blue 9 %, red 9 %, then green, orange and yellow), sRGB.
	 */
	FLinearColor ParkedColourFor(uint8 Byte)
	{
		struct FShade { int32 UpTo; FColor Colour; };
		static const FShade Shades[] = {
			{ 64, FColor(232, 232, 226) },  // white
			{ 115, FColor(14, 14, 16) },    // black
			{ 161, FColor(88, 91, 95) },    // grey
			{ 189, FColor(167, 169, 171) }, // silver
			{ 212, FColor(22, 52, 112) },   // blue
			{ 235, FColor(150, 18, 22) },   // red
			{ 245, FColor(31, 74, 54) },    // green
			{ 251, FColor(205, 92, 20) },   // orange
			{ 256, FColor(226, 182, 30) },  // yellow
		};
		for (const FShade& Shade : Shades)
		{
			if (Byte < Shade.UpTo) return FLinearColor(Shade.Colour);
		}
		return FLinearColor(Shades[0].Colour);
	}
}

const TCHAR* SimCruiseParkedCars::CarFor(uint8 Pick)
{
	FString Id = ParkedCarFor(Pick);
	return UnfitCars.Contains(Id) ? TEXT("meridian_z") : ParkedCarFor(Pick);
}

FLinearColor SimCruiseParkedCars::ColourFor(uint8 Byte)
{
	return ParkedColourFor(Byte);
}

void ASimCruiseWorldTile::SurfaceGripAtFace(int32 FaceIndex, float& OutDryGrip, float& OutWetLoss) const
{
	// Peak friction relative to dry asphalt (0.8-0.9), from Wong, Theory of Ground Vehicles,
	// table 1.3: wet asphalt or concrete 0.5-0.7 (about 0.7 of dry), gravel 0.6 (0.7 of dry,
	// little changed when wet), earth road 0.68 dry and 0.55 wet (0.8 and 0.65 of dry asphalt;
	// the tile's ground: verges, lawns, soil).
	OutDryGrip = 1.f;
	OutWetLoss = 0.3f;
	TSharedPtr<const FSurfaceMap, ESPMode::ThreadSafe> Map;
	{
		FScopeLock Lock(&SurfaceLock);
		Map = SurfaceMap;
	}
	if (!Map.IsValid() || FaceIndex < 0)
	{
		return;
	}
	int32 Section = INDEX_NONE;
	for (int32 S = Map->FirstFace.Num() - 1; S >= 0; --S)
	{
		if (FaceIndex >= Map->FirstFace[S]) { Section = S; break; }
	}
	if (Section == INDEX_NONE)
	{
		return;
	}
	switch (static_cast<ESimCruiseTileLayer>(Map->Layer[Section]))
	{
	case ESimCruiseTileLayer::Asphalt:
	{
		const int32 Local = FaceIndex - Map->AsphaltFirstFace;
		if (Map->GravelAsphalt.IsValidIndex(Local) && Map->GravelAsphalt[Local])
		{
			OutDryGrip = 0.7f;
			OutWetLoss = 0.1f;
		}
		break;
	}
	case ESimCruiseTileLayer::Terrain:
		OutDryGrip = 0.8f;
		OutWetLoss = 0.19f;
		break;
	default:
		break;
	}
}

ASimCruiseWorldTile::ASimCruiseWorldTile()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable); // the floating origin moves tiles
	Mesh->bUseComplexAsSimpleCollision = true;
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Mesh->SetCastShadow(true);
	RootComponent = Mesh;
}

void ASimCruiseWorldTile::Install(const TSharedRef<FSimCruiseTileBuild>& InBuild, USimCruiseWorldResources* Resources, UTexture2D* Aerial, bool bAsyncCollision)
{

	Build = InBuild;
	TileId = InBuild->Tile;
	bCollisionReady = false;
	PendingSteps.Reset();
	CollisionSections.Reset();
	// What runs now is what the tile must have to be drawn (the static mesh, its materials);
	// the rest (collision, ground mask, palms, signs) queues as steps for ContinueInstall, so
	// a tile streaming in costs a few milliseconds a frame instead of up to 140 ms in one.
	Mesh->bUseAsyncCooking = bAsyncCollision;
	Mesh->ClearAllMeshSections();

	// What is drawn: one static mesh of every layer (static draw path, so virtual shadow
	// pages stay cached; in editor builds a mesh distance field, so Lumen sees the street in
	// reflections and shaded sides). The procedural mesh keeps only the collision and is
	// hidden once the static mesh is built. Headless runs draw nothing and skip it.
	if (RenderMesh)
	{
		RenderMesh->DestroyComponent();
		RenderMesh = nullptr;
	}
	RenderSlots.Reset();
	if (Resources && InBuild->RenderData)
	{
		// The worker filled the buffers: here they only become a mesh and go to the GPU.
		UStaticMesh* Static = NewObject<UStaticMesh>(this, *FString::Printf(TEXT("SM_SimCruiseTile_%d_%d"), TileId.X, TileId.Y), RF_Transient);
		for (int32 Slot = 0; Slot < InBuild->RenderSlots.Num(); ++Slot)
		{
			Static->GetStaticMaterials().Add_GetRef(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), *FString::Printf(TEXT("Layer%d"), InBuild->RenderSlots[Slot]))).UVChannelData = FMeshUVChannelInfo(1.f);
		}
		Static->SetRenderData(MoveTemp(InBuild->RenderData));
		Static->InitResources();
		Static->CalculateExtendedBounds();
		RenderSlots = InBuild->RenderSlots;
		RenderMesh = NewObject<UStaticMeshComponent>(this, TEXT("RenderMesh"));
		RenderMesh->SetMobility(EComponentMobility::Movable);
		RenderMesh->SetupAttachment(Mesh);
		RenderMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RenderMesh->SetCastShadow(true);
		RenderMesh->SetStaticMesh(Static);
		RenderMesh->RegisterComponent();
		AddInstanceComponent(RenderMesh);
		// Drawn by the static mesh: the procedural mesh keeps only the collision. Hidden is not
		// enough: a hidden primitive still gets a scene proxy, rebuilt with every section's GPU
		// buffers each time a section is added. Above every detail mode it is never added to the
		// scene (its physics state does not depend on that).
		Mesh->SetVisibility(false, false);
		Mesh->DetailMode = EDetailMode::DM_MAX;
		Mesh->MarkRenderStateDirty();
	}

	// The surface under each collision face, for the tyres' grip: the procedural mesh numbers
	// its collision faces section by section, in section order.
	TSharedPtr<FSurfaceMap, ESPMode::ThreadSafe> Surfaces = MakeShared<FSurfaceMap, ESPMode::ThreadSafe>();
	int32 FaceCount = 0;
	for (int32 i = 0; i < static_cast<int32>(ESimCruiseTileLayer::Count); ++i)
	{
		FSimCruiseMeshData& Layer = InBuild->Layers[i];
		{
			const ESimCruiseTileLayer SurfaceKind = static_cast<ESimCruiseTileLayer>(i);
			const bool bSolid = SurfaceKind == ESimCruiseTileLayer::Terrain || SurfaceKind == ESimCruiseTileLayer::Asphalt || SurfaceKind == ESimCruiseTileLayer::Concrete
				|| SurfaceKind == ESimCruiseTileLayer::Building || SurfaceKind == ESimCruiseTileLayer::Props;
			if (bSolid && !Layer.IsEmpty())
			{
				Surfaces->FirstFace.Add(FaceCount);
				Surfaces->Layer.Add(static_cast<uint8>(i));
				const int32 Faces = Layer.Indices.Num() / 3;
				if (SurfaceKind == ESimCruiseTileLayer::Asphalt)
				{
					// Vertex colour R marks gravel (an unpaved road, a rural shoulder).
					Surfaces->AsphaltFirstFace = FaceCount;
					Surfaces->GravelAsphalt.Init(false, Faces);
					if (Layer.Colors.Num() == Layer.Positions.Num())
					{
						for (int32 F = 0; F < Faces; ++F)
						{
							const float R = (Layer.Colors[Layer.Indices[3 * F]].R + Layer.Colors[Layer.Indices[3 * F + 1]].R + Layer.Colors[Layer.Indices[3 * F + 2]].R) / 3.f;
							Surfaces->GravelAsphalt[F] = R > 0.5f;
						}
					}
				}
				FaceCount += Faces;
			}
		}
		if (Layer.IsEmpty())
		{
			continue;
		}
		const ESimCruiseTileLayer Kind = static_cast<ESimCruiseTileLayer>(i);
		const bool bCollision = Kind == ESimCruiseTileLayer::Terrain || Kind == ESimCruiseTileLayer::Asphalt || Kind == ESimCruiseTileLayer::Concrete
			|| Kind == ESimCruiseTileLayer::Building || Kind == ESimCruiseTileLayer::Props;
		if (RenderMesh && !bCollision)
		{
			// Drawn by the static mesh only.
			Layer.Positions.Empty(); Layer.Normals.Empty(); Layer.Colors.Empty(); Layer.Indices.Empty(); Layer.UV0.Empty(); Layer.UV1.Empty();
			continue;
		}
		CollisionSections.Add(bCollision ? i : INDEX_NONE);
		PendingSteps.Add([this, InBuild, i]()
		{
			FSimCruiseMeshData& L = InBuild->Layers[i];
			const TArray<FVector2D> NoUVs;
			if (RenderMesh)
			{
				// Collision only (the static mesh draws): positions and triangles, no normals,
				// tangents, colours or UVs, and no collision yet (cooked once, at the end).
				const TArray<FVector> NoNormals;
				const TArray<FLinearColor> NoColours;
				const TArray<FProcMeshTangent> NoTangents;
				Mesh->CreateMeshSection_LinearColor(i, L.Positions, L.Indices, NoNormals, NoUVs, NoUVs, NoUVs, NoUVs, NoColours, NoTangents, false, false);
			}
			else
			{
				// Headless runs draw nothing but keep the full section (tests read it).
				TArray<FProcMeshTangent> LayerTangents;
				LayerTangents.SetNumUninitialized(L.Normals.Num());
				for (int32 v = 0; v < L.Normals.Num(); ++v)
				{
					const FVector& N = L.Normals[v];
					const FVector T = FMath::Abs(N.Z) < 0.9 ? FVector::CrossProduct(FVector::UpVector, N).GetSafeNormal() : FVector::CrossProduct(N, FVector::RightVector).GetSafeNormal() * -1.0;
					LayerTangents[v] = FProcMeshTangent(T.IsNearlyZero() ? FVector::ForwardVector : T, false);
				}
				const bool bLayerUVs = L.UV0.Num() == L.Positions.Num() && L.UV1.Num() == L.Positions.Num();
				Mesh->CreateMeshSection_LinearColor(i, L.Positions, L.Indices, L.Normals, bLayerUVs ? L.UV0 : NoUVs, bLayerUVs ? L.UV1 : NoUVs, NoUVs, NoUVs,
					L.Colors, LayerTangents, false, false);
			}
			if (Materials.IsValidIndex(i)) Mesh->SetMaterial(i, Materials[i]);
			// The game thread copy is gone once the section owns it.
			L.Positions.Empty(); L.Normals.Empty(); L.Colors.Empty(); L.Indices.Empty(); L.UV0.Empty(); L.UV1.Empty();
		});
	}
	// All collision sections in: one cook and one physics body for the tile (each section
	// with collision used to cook the whole tile again and swap the body).
	PendingSteps.Add([this]()
	{
		for (const int32 Section : CollisionSections)
		{
			if (Section == INDEX_NONE) continue;
			if (FProcMeshSection* S = Mesh->GetProcMeshSection(Section)) S->bEnableCollision = true;
		}
		Mesh->ClearCollisionConvexMeshes(); // public; it rebuilds the collision once
	});

	{
		FScopeLock Lock(&SurfaceLock);
		SurfaceMap = Surfaces;
	}

	// Parked cars wait for PlaceParkedCars (drawn runs only: they are scenery and obstacles).
	for (AActor* Old : ParkedCars)
	{
		if (Old) Old->Destroy();
	}
	ParkedCars.Reset();
	ParkedWaiting.Reset();
	if (Resources && CVarParkedCars.GetValueOnGameThread() != 0)
	{
		ParkedWaiting = MoveTemp(InBuild->ParkedCars);
	}
	InBuild->ParkedCars.Empty();

	// Palms: one instanced component per variant on this tile.
	for (UInstancedStaticMeshComponent* Old : PalmComponents)
	{
		if (Old) Old->DestroyComponent();
	}
	PalmComponents.Reset();
	if (Resources && !InBuild->PalmInstances.IsEmpty())
	PendingSteps.Add([this, InBuild, Resources]()
	{
		for (int32 Variant = 0; Variant < Resources->PalmMeshes.Num(); ++Variant)
		{
			TArray<FTransform> Transforms;
			for (const FSimCruisePalmInstance& Palm : InBuild->PalmInstances)
			{
				if (Palm.Variant != Variant) continue;
				Transforms.Add(FTransform(FRotator(0.0, Palm.YawDeg, 0.0), Palm.LocationCm, FVector(Palm.Scale)));
			}
			if (Transforms.IsEmpty() || !Resources->PalmMeshes[Variant]) continue;
			UInstancedStaticMeshComponent* Palms = NewObject<UInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Palms%d"), Variant));
			Palms->SetMobility(EComponentMobility::Movable);
			Palms->SetupAttachment(Mesh);
			Palms->SetStaticMesh(Resources->PalmMeshes[Variant]);
			Palms->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
			Palms->SetCastShadow(true);
			Palms->RegisterComponent();
			Palms->AddInstances(Transforms, false, false);
			AddInstanceComponent(Palms);
			PalmComponents.Add(Palms);
		}
		InBuild->PalmInstances.Empty();
	});

	if (Resources)
	{
		// The ground mask: its mips were built on the worker, so here it is only uploaded (no
		// blank mask for a frame or two any more).
		MaskTexture = InBuild->MaskMips.Num() > 0
			? USimCruiseWorldResources::CreateTextureFromMips(this, NAME_None, MoveTemp(InBuild->MaskMips), SimCruiseGroundMask::Size, SimCruiseGroundMask::Size, true, false)
			: nullptr;
		const FVector2d Anchor = SimCruiseTileBuilder::TextureAnchorM(TileId, InBuild->TileSizeM);
		const FIntPoint AerialTile = SimCruiseGeo::AerialTileOf(TileId);
		const FLinearColor Cell(TileId.X - AerialTile.X * 4, TileId.Y - AerialTile.Y * 4, Aerial ? 1.f : 0.f, 0.f);
		Materials.Reset();
		for (int32 i = 0; i < static_cast<int32>(ESimCruiseTileLayer::Count); ++i)
		{
			UMaterialInterface* Base = Resources->GetLayerMaterial(static_cast<ESimCruiseTileLayer>(i));
			UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, this);
			MID->SetVectorParameterValue(TEXT("Anchor"), FLinearColor(Anchor.X, Anchor.Y, 0.f, 0.f));
			MID->SetScalarParameterValue(TEXT("TileSizeCm"), InBuild->TileSizeM * 100.0);
			MID->SetVectorParameterValue(TEXT("Cell"), Cell);
			MID->SetTextureParameterValue(TEXT("Mask"), MaskTexture ? MaskTexture.Get() : Resources->BlankTexture.Get());
			MID->SetTextureParameterValue(TEXT("Aerial"), Aerial ? Aerial : Resources->BlankTexture.Get());
			MID->SetTextureParameterValue(TEXT("Grass"), Resources->GrassTexture);
			MID->SetTextureParameterValue(TEXT("Sand"), Resources->SandTexture);
			MID->SetTextureParameterValue(TEXT("Grain"), Resources->AsphaltTexture);
			MID->SetTextureParameterValue(TEXT("Asphalt"), Resources->AsphaltTexture);
			MID->SetVectorParameterValue(TEXT("GrassMean"), Resources->GrassMean);
			MID->SetVectorParameterValue(TEXT("SandMean"), Resources->SandMean);
			MID->SetVectorParameterValue(TEXT("GrainMean"), Resources->AsphaltMean);
			MID->SetVectorParameterValue(TEXT("AsphaltMean"), Resources->AsphaltMean);
			MID->SetScalarParameterValue(TEXT("Lights"), LightsOn);
			// The grass field (world-detail-methods.md): on unless -SimCruiseNoGrassField; the
			// dry share follows -SimCruiseGrassDry=0..1 until the place graph sets it per patch.
			static const bool bGrassField = !FParse::Param(FCommandLine::Get(), TEXT("SimCruiseNoGrassField"));
			static const float GrassDry = [] { float V = 0.f; FParse::Value(FCommandLine::Get(), TEXT("SimCruiseGrassDry="), V); return FMath::Clamp(V, 0.f, 1.f); }();
			MID->SetScalarParameterValue(TEXT("GrassField"), bGrassField ? 1.f : 0.f);
			MID->SetScalarParameterValue(TEXT("GrassDry"), GrassDry);
			Materials.Add(MID);
			const int32 Slot = RenderSlots.IndexOfByKey(i);
			if (RenderMesh && Slot != INDEX_NONE)
			{
				RenderMesh->SetMaterial(Slot, MID);
			}
		}
	}
	if (Resources)
	{
		// First of the steps: the ground mask (the collision sections queued above run after it).
		if (!MaskTexture) PendingSteps.Insert([this, InBuild]()
		{
			MaskTexture = USimCruiseWorldResources::CreateTexture(this, NAME_None, InBuild->Mask, SimCruiseGroundMask::Size, SimCruiseGroundMask::Size, true, false);
			InBuild->Mask.Empty();
			for (UMaterialInstanceDynamic* MID : Materials)
			{
				if (MID) MID->SetTextureParameterValue(TEXT("Mask"), MaskTexture);
			}
		}, 0);
	}

	// The named places' lettering: one text component each, in the Signs material.
	for (UTextRenderComponent* Old : SignComponents)
	{
		if (Old) Old->DestroyComponent();
	}
	SignComponents.Reset();
	const int32 SignsSlot = static_cast<int32>(ESimCruiseTileLayer::Signs);
	if (Resources && Materials.IsValidIndex(SignsSlot) && CVarSigns.GetValueOnGameThread() != 0)
	PendingSteps.Add([this, InBuild, SignsSlot]()
	{
		for (int32 s = 0; s < InBuild->Signs.Num(); ++s)
		{
			const FSimCruiseSign& Sign = InBuild->Signs[s];
			UTextRenderComponent* Text = NewObject<UTextRenderComponent>(this, *FString::Printf(TEXT("Sign%d"), s));
			Text->SetMobility(EComponentMobility::Movable);
			Text->SetupAttachment(Mesh);
			Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Text->SetCastShadow(false);
			Text->SetTextMaterial(Materials[SignsSlot]);
			Text->SetHorizontalAlignment(EHTA_Center);
			Text->SetVerticalAlignment(EVRTA_TextCenter);
			// The glyphs' cap height is about 0.7 of the world size (Roboto).
			Text->SetWorldSize(Sign.LetterCm / 0.7f);
			FString Shown = Sign.Text;
			if (Sign.bStacked)
			{
				// One letter a line, the way the blade signs on the towers read.
				Shown.Reset();
				for (int32 c = 0; c < Sign.Text.Len(); ++c)
				{
					if (FChar::IsWhitespace(Sign.Text[c])) continue;
					if (!Shown.IsEmpty()) Shown += TEXT("<br>"); // the text component's line break
					Shown.AppendChar(Sign.Text[c]);
				}
				Text->SetVertSpacingAdjust(-0.12f * Sign.LetterCm / 0.7f);
			}
			Text->SetText(FText::FromString(Shown));
			const FLinearColor C = Sign.Colour;
			Text->SetTextRenderColor(FLinearColor(C.R, C.G, C.B, Sign.bNeon ? 1.f : 0.5f).ToFColor(false));
			Text->SetRelativeLocationAndRotation(Sign.LocationCm, FRotator(0.0, Sign.YawDeg, 0.0));
			// Narrow (or shorten) the lettering to the room it has.
			const FVector Size = Text->GetTextLocalSize();
			const double Span = Sign.bStacked ? Size.Z : Size.Y;
			if (Span > Sign.MaxSpanCm && Span > 1.0)
			{
				const float Squeeze = static_cast<float>(Sign.MaxSpanCm / Span);
				if (Sign.bStacked) Text->SetWorldSize(Text->WorldSize * Squeeze);
				else Text->SetXScale(Squeeze);
			}
			Text->SetCachedMaxDrawDistance(35000.f);
			Text->RegisterComponent();
			AddInstanceComponent(Text);
			SignComponents.Add(Text);
		}
		UE_LOG(LogTemp, Display, TEXT("SimCruise tile %d_%d: %d signs"), TileId.X, TileId.Y, SignComponents.Num());
		InBuild->Signs.Empty();
	});
	// Headless runs (no Resources) and anything else: everything at once, as before.
	if (!Resources)
	{
		while (PendingSteps.Num() > 0) ContinueInstall(1e9);
	}
}

void ASimCruiseWorldTile::SetLightsOn(float Amount)
{
	if (FMath::Abs(Amount - LightsOn) < 0.01f)
	{
		return;
	}
	LightsOn = Amount;
	for (UMaterialInstanceDynamic* MID : Materials)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Lights"), Amount);
		}
	}
}

double ASimCruiseWorldTile::SurfaceHeightAt(const FVector2d& TileMetres) const
{
	if (!Build.IsValid())
	{
		return 0.0;
	}
	FSimCruiseRoadHit Hit;
	if (Build->Roads.FindCarriageway(TileMetres, Hit))
	{
		return Hit.Height;
	}
	return Build->TerrainHeightAt(TileMetres);
}

double ASimCruiseWorldTile::ContinueInstall(double BudgetMs)
{
	const double Start = FPlatformTime::Seconds();
	double Used = 0.0;
	while (PendingSteps.Num() > 0)
	{
		TFunction<void()> Step = MoveTemp(PendingSteps[0]);
		PendingSteps.RemoveAt(0, EAllowShrinking::No);
		const double StepStart = FPlatformTime::Seconds();
		Step();
		const double StepMs = (FPlatformTime::Seconds() - StepStart) * 1000.0;
		UE_CLOG(StepMs > 20.0, LogTemp, Display, TEXT("SimCruise tile %d_%d: an install step took %.1f ms (%d left)"), TileId.X, TileId.Y, StepMs, PendingSteps.Num());
		Used = (FPlatformTime::Seconds() - Start) * 1000.0;
		if (Used >= BudgetMs) break;
	}
	return Used;
}

bool ASimCruiseWorldTile::IsCollisionReady() const
{
	if (PendingSteps.Num() > 0)
	{
		return false; // collision sections still to come
	}
	if (bCollisionReady || !Build.IsValid() || !Mesh)
	{
		return bCollisionReady;
	}
	const double Half = Build->TileSizeM * 50.0;
	const FVector Centre = GetActorLocation() + FVector(Half, Half, 0.0);
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(SimCruiseTileCollisionReady), true);
	// +-10 km covers every US elevation; a hit means the cooked triangles are in the scene.
	bCollisionReady = Mesh->LineTraceComponent(Hit, Centre + FVector(0.0, 0.0, 1.0e6), Centre - FVector(0.0, 0.0, 1.0e6), Params);
	return bCollisionReady;
}

int32 ASimCruiseWorldTile::PlaceParkedCars(int32 Budget)
{
	int32 Placed = 0;
	UWorld* World = GetWorld();
	while (Placed < Budget && !ParkedWaiting.IsEmpty() && World)
	{
		const FSimCruiseParkedCar Stall = ParkedWaiting.Pop(EAllowShrinking::No);
		FString CarId = ParkedCarFor(Stall.Pick);
		if (UnfitCars.Contains(CarId)) CarId = TEXT("meridian_z");
		FString Error;
		TSharedPtr<FSimCruiseBuiltCar> Built = SimCruiseCarAssets::Get(CarId, Error);
		if (!Built.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("SimCruise parked car %s: %s"), *CarId, *Error);
			UnfitCars.Add(CarId);
			CarId = TEXT("meridian_z");
			Built = SimCruiseCarAssets::Get(CarId, Error);
		}
		if (!Built.IsValid() || !Built->Body.Mesh.IsValid())
		{
			continue;
		}
		// The car's frame has its origin between the axles at the body's height; the tyres
		// touch the ground a tyre radius under the hubs.
		const double GroundInFrameCm = Built->HubsCm[0].Z - Built->TyreRadiusCm;
		const FVector Location = GetActorTransform().TransformPosition(Stall.LocationCm - FVector(0.0, 0.0, GroundInFrameCm));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		ASimCruiseCarPresentationActor* Car = World->SpawnActor<ASimCruiseCarPresentationActor>(Location, FRotator(0.0, Stall.YawDeg, 0.0), Params);
		if (!Car)
		{
			continue;
		}
		if (!Car->Presentation->InitializeForCar(CarId))
		{
			Car->Destroy();
			continue;
		}
		Car->Presentation->SetPaintColor(ParkedColourFor(Stall.Colour));
		Car->Presentation->SetParked();
		// Solid: a box round the body.
		const FBox Body = Built->Body.Mesh->GetBoundingBox().TransformBy(Built->Body.PartToVehicle);
		UBoxComponent* Box = NewObject<UBoxComponent>(Car, TEXT("Collision"));
		Box->SetupAttachment(Car->Presentation);
		Box->SetRelativeLocation(Body.GetCenter());
		Box->SetBoxExtent(Body.GetExtent());
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Box->SetMobility(EComponentMobility::Movable);
		Box->RegisterComponent();
		Car->AddInstanceComponent(Box);
		Car->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
		ParkedCars.Add(Car);
		++Placed;
		if (ParkedWaiting.IsEmpty())
		{
			UE_LOG(LogTemp, Display, TEXT("SimCruise tile %d_%d: %d parked cars"), TileId.X, TileId.Y, ParkedCars.Num());
		}
	}
	return Placed;
}

void ASimCruiseWorldTile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (AActor* Car : ParkedCars)
	{
		if (Car) Car->Destroy();
	}
	ParkedCars.Reset();
	ParkedWaiting.Reset();
	Super::EndPlay(EndPlayReason);
}

ESimCruiseTileLayer ASimCruiseWorldTile::LayerAtFace(int32 FaceIndex) const
{
	TSharedPtr<const FSurfaceMap, ESPMode::ThreadSafe> Map;
	{
		FScopeLock Lock(&SurfaceLock);
		Map = SurfaceMap;
	}
	if (!Map.IsValid() || FaceIndex < 0)
	{
		return ESimCruiseTileLayer::Count;
	}
	for (int32 S = Map->FirstFace.Num() - 1; S >= 0; --S)
	{
		if (FaceIndex >= Map->FirstFace[S])
		{
			return static_cast<ESimCruiseTileLayer>(Map->Layer[S]);
		}
	}
	return ESimCruiseTileLayer::Count;
}
