// Copyright SimCruise contributors. All Rights Reserved.

#include "Environment/SimCruiseLightingLab.h"

#include "AssetCompilingManager.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/SimCruiseEnvironment.h"
#include "Environment/SimCruisePerfSampler.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

#if WITH_EDITORONLY_DATA
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseLab, Log, All);

namespace
{
	constexpr double Cm = 100.0;

	UStaticMesh* LoadMesh(const TCHAR* Path)
	{
		return LoadObject<UStaticMesh>(nullptr, Path);
	}

	FRotator HeadingToRotator(float HeadingDeg, float PitchDeg)
	{
		// North is -Y; Unreal yaw 0 looks along +X (east) and grows toward +Y (south).
		return FRotator(PitchDeg, HeadingDeg - 90.0f, 0.0f);
	}

	TArray<FString> SplitList(const FString& Text)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(","), true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
		}
		return Parts;
	}

	/** Linear albedo from an sRGB-encoded 8-bit colour (paint chips and photos are sRGB). */
	FLinearColor Srgb(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B));
	}
}

ASimCruiseLightingLab::ASimCruiseLightingLab()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Static);

	// Camera poses. Eye heights are a standing photographer (1.65 m) unless noted.
	Views.Add({TEXT("street"), FVector(1.5, 20.0, 1.65), 12.0f, 0.0f, 70.8f});
	Views.Add({TEXT("props"), FVector(9.0, 7.5, 1.55), 0.0f, -8.0f, 60.0f});
	Views.Add({TEXT("facade"), FVector(3.5, -10.0, 1.65), 300.0f, 8.0f, 70.8f});
	Views.Add({TEXT("sky"), FVector(8.0, 0.0, 1.65), 100.0f, 18.0f, 80.0f});
	Views.Add({TEXT("road"), FVector(1.0, 4.0, 3.0), 0.0f, -50.0f, 70.0f});
}

void ASimCruiseLightingLab::BeginPlay()
{
	Super::BeginPlay();
	Build();
	StartScriptFromCommandLine();
}

ASimCruiseEnvironment* ASimCruiseLightingLab::Environment() const
{
	for (TActorIterator<ASimCruiseEnvironment> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void ASimCruiseLightingLab::EnsureMasterMaterial()
{
	if (MasterMaterial)
	{
		return;
	}
#if WITH_EDITORONLY_DATA
	// Editor builds (UnrealEditor.exe -game) can author a tiny parametric material at runtime.
	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), TEXT("SimCruiseLabSurface"), RF_Transient);
	UMaterialEditorOnlyData* Inputs = Material->GetEditorOnlyData();

	auto Vector = [Material](const TCHAR* Name, const FLinearColor& Default)
	{
		UMaterialExpressionVectorParameter* P = NewObject<UMaterialExpressionVectorParameter>(Material);
		P->ParameterName = Name;
		P->DefaultValue = Default;
		Material->GetExpressionCollection().AddExpression(P);
		return P;
	};
	auto Scalar = [Material](const TCHAR* Name, float Default)
	{
		UMaterialExpressionScalarParameter* P = NewObject<UMaterialExpressionScalarParameter>(Material);
		P->ParameterName = Name;
		P->DefaultValue = Default;
		Material->GetExpressionCollection().AddExpression(P);
		return P;
	};
	Inputs->BaseColor.Connect(0, Vector(TEXT("BaseColor"), FLinearColor(0.18f, 0.18f, 0.18f)));
	Inputs->Roughness.Connect(0, Scalar(TEXT("Roughness"), 0.8f));
	Inputs->Metallic.Connect(0, Scalar(TEXT("Metallic"), 0.0f));
	Inputs->Specular.Connect(0, Scalar(TEXT("Specular"), 0.5f));
	Material->PreEditChange(nullptr);
	Material->PostEditChange();
	MasterMaterial = Material;
#else
	MasterMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
#endif
}

UMaterialInterface* ASimCruiseLightingLab::Surface(const FString& Name, const FLinearColor& Albedo, float Roughness, float Metallic, float Specular)
{
	EnsureMasterMaterial();
	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(MasterMaterial, this, MakeUniqueObjectName(this, UMaterialInstanceDynamic::StaticClass(), FName(*(TEXT("M_") + Name))));
	Instance->SetVectorParameterValue(TEXT("BaseColor"), Albedo);
	Instance->SetVectorParameterValue(TEXT("Color"), Albedo); // BasicShapeMaterial fallback
	Instance->SetScalarParameterValue(TEXT("Roughness"), Roughness);
	Instance->SetScalarParameterValue(TEXT("Metallic"), Metallic);
	Instance->SetScalarParameterValue(TEXT("Specular"), Specular);
	Materials.Add(Instance);
	return Instance;
}

UStaticMeshComponent* ASimCruiseLightingLab::AddShape(const FString& Name, UStaticMesh* Mesh, const FVector& CenterM, const FVector& ScaleUnits, UMaterialInterface* Material, const FRotator& Rotation)
{
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(*Name)));
	Component->SetMobility(EComponentMobility::Static);
	Component->SetStaticMesh(Mesh);
	Component->SetupAttachment(RootComponent);
	Component->SetRelativeLocation(CenterM * Cm);
	Component->SetRelativeRotation(Rotation);
	Component->SetRelativeScale3D(ScaleUnits);
	Component->SetMaterial(0, Material);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Component->RegisterComponent();
	AddInstanceComponent(Component);
	return Component;
}

UStaticMeshComponent* ASimCruiseLightingLab::AddBox(const FString& Name, const FVector& CenterM, const FVector& SizeM, UMaterialInterface* Material, float YawDeg)
{
	// Not cached in a function-local static: a raw UObject pointer there is invisible to the
	// garbage collector and dangles once a later world no longer references the cube.
	// LoadObject finds the already-loaded mesh, so this stays cheap.
	UStaticMesh* Cube = LoadMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	// The engine cube is 100 units on a side, centred on its origin.
	return AddShape(Name, Cube, CenterM, SizeM, Material, FRotator(0.0f, YawDeg, 0.0f));
}

void ASimCruiseLightingLab::Build()
{
	if (bBuilt)
	{
		return;
	}
	bBuilt = true;
	UStaticMesh* Sphere = LoadMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Cylinder = LoadMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	// Albedos from measured tables (physicallybased.info; ASTER/USGS spectral library means):
	// fresh-ish asphalt 0.09, weathered concrete 0.30, dry beach sand 0.40, a lawn about 0.045
	// (its luminance is half the asphalt's in the December Ocean Drive photograph).
	UMaterialInterface* Asphalt = Surface(TEXT("Asphalt"), FLinearColor(0.085f, 0.085f, 0.083f), 0.85f);
	UMaterialInterface* Concrete = Surface(TEXT("Concrete"), FLinearColor(0.31f, 0.30f, 0.28f), 0.9f);
	UMaterialInterface* Kerb = Surface(TEXT("Kerb"), FLinearColor(0.42f, 0.41f, 0.38f), 0.85f);
	UMaterialInterface* Grass = Surface(TEXT("Grass"), FLinearColor(0.032f, 0.052f, 0.016f), 0.95f);
	UMaterialInterface* Sand = Surface(TEXT("Sand"), FLinearColor(0.50f, 0.44f, 0.34f), 0.95f);
	UMaterialInterface* Sea = Surface(TEXT("Sea"), FLinearColor(0.012f, 0.035f, 0.040f), 0.12f, 0.0f, 0.25f);
	UMaterialInterface* Glass = Surface(TEXT("Glass"), FLinearColor(0.03f, 0.035f, 0.04f), 0.05f, 0.0f, 0.5f);
	UMaterialInterface* Lane = Surface(TEXT("LanePaint"), FLinearColor(0.62f, 0.62f, 0.60f), 0.6f);
	UMaterialInterface* Yellow = Surface(TEXT("CentrePaint"), FLinearColor(0.55f, 0.40f, 0.06f), 0.6f);

	// Ocean Drive stucco: white, flamingo pink, mint and butter yellow (sRGB paint chips).
	const TArray<UMaterialInterface*> Stucco = {
		Surface(TEXT("StuccoWhite"), Srgb(236, 234, 228), 0.9f),
		Surface(TEXT("StuccoPink"), Srgb(240, 196, 196), 0.9f),
		Surface(TEXT("StuccoMint"), Srgb(190, 226, 210), 0.9f),
		Surface(TEXT("StuccoYellow"), Srgb(242, 226, 170), 0.9f),
		Surface(TEXT("StuccoWhite2"), Srgb(246, 244, 238), 0.9f),
	};

	// Ground: a base of sand, then the street from west to east (metres, X east, Y south).
	AddBox(TEXT("Base"), FVector(0.0, 0.0, -0.55), FVector(6000.0, 6000.0, 1.0), Sand);
	AddBox(TEXT("Lot"), FVector(-160.0, 0.0, -0.04), FVector(260.0, 1200.0, 0.1), Concrete);
	AddBox(TEXT("Road"), FVector(0.0, 0.0, -0.04), FVector(8.0, 1200.0, 0.1), Asphalt);
	AddBox(TEXT("WestWalk"), FVector(-6.1, 0.0, 0.025), FVector(4.2, 1200.0, 0.25), Concrete);
	AddBox(TEXT("WestKerb"), FVector(-4.05, 0.0, 0.025), FVector(0.15, 1200.0, 0.25), Kerb);
	AddBox(TEXT("EastKerb"), FVector(4.05, 0.0, 0.025), FVector(0.15, 1200.0, 0.25), Kerb);
	AddBox(TEXT("EastWalk"), FVector(5.1, 0.0, 0.025), FVector(2.0, 1200.0, 0.25), Concrete);
	AddBox(TEXT("Park"), FVector(33.0, 0.0, 0.0), FVector(54.0, 1200.0, 0.2), Grass);
	AddBox(TEXT("Beachwalk"), FVector(62.0, 0.0, 0.03), FVector(4.0, 1200.0, 0.2), Concrete);
	AddBox(TEXT("Beach"), FVector(125.0, 0.0, -0.05), FVector(122.0, 1200.0, 0.2), Sand);
	AddBox(TEXT("Sea"), FVector(1700.0, 0.0, -0.3), FVector(3030.0, 6000.0, 0.2), Sea);

	// Lane markings: dashed white parking-lane lines and a double yellow centre line.
	for (double Y = -590.0; Y < 590.0; Y += 9.0)
	{
		AddBox(FString::Printf(TEXT("Dash%.0f"), Y), FVector(-2.0, Y, 0.02), FVector(0.12, 3.0, 0.02), Lane);
		AddBox(FString::Printf(TEXT("DashE%.0f"), Y), FVector(2.0, Y, 0.02), FVector(0.12, 3.0, 0.02), Lane);
	}
	// The double yellow line in 12 m pieces: single 1200 m slivers did not rasterise reliably.
	for (double Y = -594.0; Y < 594.0; Y += 12.0)
	{
		AddBox(FString::Printf(TEXT("CentreA%.0f"), Y), FVector(-0.08, Y + 6.0, 0.02), FVector(0.1, 12.0, 0.02), Yellow);
		AddBox(FString::Printf(TEXT("CentreB%.0f"), Y), FVector(0.08, Y + 6.0, 0.02), FVector(0.1, 12.0, 0.02), Yellow);
	}

	// The hotel row: stucco blocks with Art Deco eyebrow ledges and recessed dark windows.
	const double Widths[] = {24.0, 18.0, 30.0, 21.0, 27.0, 16.0};
	const double Heights[] = {12.0, 15.5, 11.0, 18.0, 13.5, 10.0};
	int32 Block = 0;
	for (double Y = -560.0; Y < 560.0; ++Block)
	{
		const double Width = Widths[Block % UE_ARRAY_COUNT(Widths)];
		const double Height = Heights[(Block * 7 + 3) % UE_ARRAY_COUNT(Heights)];
		UMaterialInterface* Wall = Stucco[(Block * 3 + 1) % Stucco.Num()];
		const double CenterY = Y + Width / 2.0;
		const double FrontX = -8.2;
		const double Depth = 22.0;
		AddBox(FString::Printf(TEXT("Hotel%d"), Block), FVector(FrontX - Depth / 2.0, CenterY, Height / 2.0), FVector(Depth, Width, Height), Wall);
		const int32 Floors = FMath::Max(1, FMath::FloorToInt(Height / 3.3));
		for (int32 Floor = 1; Floor <= Floors; ++Floor)
		{
			const double Z = Floor * 3.3 - 0.35;
			if (Floor < Floors)
			{
				AddBox(FString::Printf(TEXT("Eyebrow%d_%d"), Block, Floor), FVector(FrontX + 0.35, CenterY, Z + 0.3), FVector(0.7, Width - 2.0, 0.12), Wall);
			}
			const int32 Windows = FMath::Max(2, FMath::FloorToInt(Width / 4.5));
			for (int32 W = 0; W < Windows; ++W)
			{
				const double WindowY = CenterY - Width / 2.0 + (W + 0.5) * Width / Windows;
				AddBox(FString::Printf(TEXT("Window%d_%d_%d"), Block, Floor, W), FVector(FrontX + 0.01, WindowY, Z - 1.35), FVector(0.04, 1.6, 1.7), Glass);
			}
		}
		Y += Width + (Block % 3 == 1 ? 6.0 : 2.5);
	}

	// Reference objects on the park edge, in front of the "props" camera (which looks north).
	UMaterialInterface* Grey18 = Surface(TEXT("GreyCard18"), FLinearColor(0.18f, 0.18f, 0.18f), 0.95f, 0.0f, 0.35f);
	UMaterialInterface* White = Surface(TEXT("White85"), FLinearColor(0.85f, 0.85f, 0.85f), 0.9f);
	UMaterialInterface* Black = Surface(TEXT("Black04"), FLinearColor(0.04f, 0.04f, 0.04f), 0.9f);
	UMaterialInterface* Chrome = Surface(TEXT("Chrome"), FLinearColor(0.95f, 0.93f, 0.88f), 0.03f, 1.0f);
	UMaterialInterface* Paint = Surface(TEXT("RedPaint"), Srgb(150, 18, 22), 0.25f, 0.0f, 0.5f);
	UMaterialInterface* Tyre = Surface(TEXT("Tyre"), FLinearColor(0.025f, 0.025f, 0.025f), 0.7f);
	UMaterialInterface* Steel = Surface(TEXT("Stand"), FLinearColor(0.3f, 0.3f, 0.3f), 0.5f, 1.0f);

	// Grey card on a stand, faced south toward the camera and tilted 30 degrees up.
	AddBox(TEXT("CardStand"), FVector(8.0, 0.0, 0.6), FVector(0.04, 0.04, 1.1), Steel);
	UStaticMeshComponent* Card = AddBox(TEXT("GreyCard"), FVector(8.0, 0.02, 1.25), FVector(0.4, 0.01, 0.5), Grey18);
	Card->SetRelativeRotation(FRotator(0.0f, 0.0f, -30.0f));
	AddBox(TEXT("GreyGround"), FVector(10.5, 2.0, 0.105), FVector(1.0, 1.0, 0.01), Grey18);

	const struct { const TCHAR* Name; double X; UMaterialInterface* M; } Balls[] = {
		{TEXT("SphereWhite"), 9.2, White}, {TEXT("SphereGrey"), 9.9, Grey18}, {TEXT("SphereBlack"), 10.6, Black}, {TEXT("SphereChrome"), 11.3, Chrome}};
	for (const auto& Ball : Balls)
	{
		AddShape(Ball.Name, Sphere, FVector(Ball.X, -0.5, 0.35), FVector(0.5), Ball.M);
	}

	// A car-sized box (1999 MX-5: 3.95 x 1.68 x 1.23 m) parked in the east parking lane.
	AddBox(TEXT("CarBody"), FVector(3.0, -3.0, 0.72), FVector(1.68, 3.95, 0.98), Paint);
	for (int32 Wheel = 0; Wheel < 4; ++Wheel)
	{
		const double X = 3.0 + (Wheel % 2 == 0 ? -0.72 : 0.72);
		const double Y = -3.0 + (Wheel < 2 ? -1.13 : 1.13);
		AddShape(FString::Printf(TEXT("Wheel%d"), Wheel), Cylinder, FVector(X, Y, 0.3), FVector(0.6, 0.6, 0.2), Tyre, FRotator(90.0f, 0.0f, 0.0f));
	}

	Probes.Add({TEXT("grey_card"), FVector(8.0, 0.02, 1.25) * Cm});
	Probes.Add({TEXT("grey_ground"), FVector(10.5, 2.0, 0.11) * Cm});
	Probes.Add({TEXT("sphere_white_top"), FVector(9.2, -0.5, 0.58) * Cm});
	Probes.Add({TEXT("sphere_grey_top"), FVector(9.9, -0.5, 0.58) * Cm});
	Probes.Add({TEXT("road_ahead"), FVector(-1.0, -20.0, 0.01) * Cm});
	Probes.Add({TEXT("road_near"), FVector(0.8, 8.0, 0.01) * Cm});
	Probes.Add({TEXT("facade_west"), FVector(-8.2, -40.0, 5.0) * Cm});
	Probes.Add({TEXT("car_side"), FVector(3.84, -3.0, 0.8) * Cm});
	Probes.Add({TEXT("grass"), FVector(20.0, -10.0, 0.1) * Cm});
}

bool ASimCruiseLightingLab::SetView(const FString& ViewName)
{
	const FSimCruiseLabView* View = Views.FindByPredicate([&ViewName](const FSimCruiseLabView& V) { return V.Name == ViewName; });
	if (!View)
	{
		UE_LOG(LogSimCruiseLab, Warning, TEXT("No lab view '%s'"), *ViewName);
		return false;
	}
	UWorld* World = GetWorld();
	if (!ViewCamera)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ViewCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
		ViewCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	ViewCamera->SetActorLocationAndRotation(View->EyeM * Cm, HeadingToRotator(View->HeadingDeg, View->PitchDeg));
	ViewCamera->GetCameraComponent()->SetFieldOfView(View->HorizontalFovDeg);
	CurrentView = ViewName;
	if (APlayerController* Controller = World->GetFirstPlayerController())
	{
		Controller->SetViewTarget(ViewCamera);
		if (APawn* Pawn = Controller->GetPawn())
		{
			Pawn->SetActorHiddenInGame(true);
		}
	}
	return true;
}

void ASimCruiseLightingLab::StartScriptFromCommandLine()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FString ShotsText;
	FString ViewsText = TEXT("street,props");
	FString CapturePresets = TEXT("ultra");
	FString PerfPresets;
	float PerfSeconds = 10.0f;
	float Settle = 6.0f;
	FParse::Value(Cmd, TEXT("SimCruiseLabShots="), ShotsText, false);
	FParse::Value(Cmd, TEXT("SimCruiseLabViews="), ViewsText, false);
	FParse::Value(Cmd, TEXT("SimCruiseLabCapturePresets="), CapturePresets, false);
	FParse::Value(Cmd, TEXT("SimCruiseLabPerf="), PerfPresets, false);
	FParse::Value(Cmd, TEXT("SimCruiseLabPerfSeconds="), PerfSeconds);
	FParse::Value(Cmd, TEXT("SimCruiseLabSettle="), Settle);
	if (!FParse::Value(Cmd, TEXT("SimCruiseLabOut="), OutputDir, false))
	{
		OutputDir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Lab");
	}

	auto Add = [this](FStep::EKind Kind, const FString& Text = FString(), float Seconds = 0.0f)
	{
		FStep Step;
		Step.Kind = Kind;
		Step.Text = Text;
		Step.Seconds = Seconds;
		Steps.Add(Step);
	};

	Steps.Reset();
	if (!ShotsText.IsEmpty())
	{
		for (const FString& PresetName : SplitList(CapturePresets))
		{
			Add(FStep::EKind::Preset, PresetName);
			for (const FString& Shot : SplitList(ShotsText))
			{
				Add(FStep::EKind::Time, Shot);
				bool bFirstView = true;
				for (const FString& ViewName : SplitList(ViewsText))
				{
					Add(FStep::EKind::View, ViewName);
					// The first view after a time change waits for the sky capture, Lumen and exposure.
					Add(FStep::EKind::Settle, FString(), bFirstView ? Settle : FMath::Max(Settle * 0.5f, 2.5f));
					Add(FStep::EKind::Capture, PresetName);
					bFirstView = false;
				}
			}
		}
	}
	if (!PerfPresets.IsEmpty())
	{
		FString PerfView = TEXT("street");
		FParse::Value(Cmd, TEXT("SimCruiseLabPerfView="), PerfView, false);
		for (const FString& PresetName : SplitList(PerfPresets))
		{
			Add(FStep::EKind::Preset, PresetName);
			Add(FStep::EKind::View, PerfView);
			Add(FStep::EKind::Settle, FString(), 5.0f);
			Add(FStep::EKind::Perf, PresetName, PerfSeconds);
		}
	}
	if (FParse::Param(Cmd, TEXT("SimCruiseLabQuit")) && !Steps.IsEmpty())
	{
		Add(FStep::EKind::Quit);
	}
	if (!Steps.IsEmpty())
	{
		IFileManager::Get().MakeDirectory(*OutputDir, true);
		StepIndex = 0;
		StepClock = 0.0f;
		bStepStarted = false;
		UE_LOG(LogSimCruiseLab, Display, TEXT("Lab script: %d steps, output %s"), Steps.Num(), *OutputDir);
	}
	else
	{
		SetView(TEXT("street"));
	}
}

bool ASimCruiseLightingLab::IsWorldReady() const
{
	const bool bShaders = !GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0;
	const bool bAssets = FAssetCompilingManager::Get().GetNumRemainingAssets() == 0;
	return bShaders && bAssets;
}

void ASimCruiseLightingLab::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (ViewCamera)
	{
		// The game mode may possess a pawn after BeginPlay; keep looking through the lab camera.
		if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
		{
			if (Controller->GetViewTarget() != ViewCamera)
			{
				Controller->SetViewTarget(ViewCamera);
			}
			if (APawn* Pawn = Controller->GetPawn())
			{
				Pawn->SetActorHiddenInGame(true);
			}
		}
	}
	RunSteps(DeltaSeconds);
}

void ASimCruiseLightingLab::RunSteps(float DeltaSeconds)
{
	if (!Steps.IsValidIndex(StepIndex))
	{
		return;
	}
	FStep& Step = Steps[StepIndex];
	auto Next = [this]()
	{
		++StepIndex;
		StepClock = 0.0f;
		bStepStarted = false;
	};

	switch (Step.Kind)
	{
	case FStep::EKind::Preset:
	{
		ESimCruiseGraphicsPreset Preset = ESimCruiseGraphicsPreset::High;
		if (Step.Text.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			Preset = FSimCruiseGraphics::AutoPickForThisMachine(false);
		}
		else if (!FSimCruiseGraphics::Parse(Step.Text, Preset))
		{
			UE_LOG(LogSimCruiseLab, Warning, TEXT("Unknown preset '%s', using high"), *Step.Text);
		}
		FSimCruiseGraphics::Apply(Preset);
		CurrentPreset = Preset;
		Next();
		break;
	}
	case FStep::EKind::Time:
	{
		// "name@2025-04-24T16:20:00Z" or "name@2025-04-24T16:20:00Z#console command|another command"
		FString Name;
		FString Iso;
		FString Spec = Step.Text;
		FString Commands;
		if (Spec.Split(TEXT("#"), &Spec, &Commands))
		{
			TArray<FString> Lines;
			Commands.ParseIntoArray(Lines, TEXT("|"));
			for (const FString& Line : Lines)
			{
				GEngine->Exec(GetWorld(), *Line.Replace(TEXT("~"), TEXT(" ")));
			}
		}
		if (!Spec.Split(TEXT("@"), &Name, &Iso))
		{
			Name = Spec;
			Iso = Spec;
		}
		FDateTime Utc;
		if (FDateTime::ParseIso8601(*Iso, Utc))
		{
			if (ASimCruiseEnvironment* Env = Environment())
			{
				Env->TimeScale = 0.0f;
				Env->SetUtcTime(Utc);
			}
			CurrentShot = Name;
		}
		else
		{
			UE_LOG(LogSimCruiseLab, Warning, TEXT("Cannot parse shot time '%s'"), *Step.Text);
		}
		Next();
		break;
	}
	case FStep::EKind::View:
		SetView(Step.Text);
		Next();
		break;
	case FStep::EKind::Settle:
		// The clock only runs once shaders and meshes have finished compiling.
		if (IsWorldReady())
		{
			StepClock += DeltaSeconds;
		}
		if (StepClock >= Step.Seconds)
		{
			Next();
		}
		break;
	case FStep::EKind::Capture:
	{
		if (!bStepStarted)
		{
			PendingCapture = OutputDir / FString::Printf(TEXT("%s_%s_%s.png"), *CurrentShot, *CurrentView, *FSimCruiseGraphics::Name(CurrentPreset));
			FScreenshotRequest::RequestScreenshot(PendingCapture, false, false);
			WriteSidecar(PendingCapture);
			bStepStarted = true;
			StepClock = 0.0f;
			break;
		}
		StepClock += DeltaSeconds;
		if (!FScreenshotRequest::IsScreenshotRequested() && StepClock > 0.3f)
		{
			UE_LOG(LogSimCruiseLab, Display, TEXT("Captured %s"), *PendingCapture);
			Next();
		}
		break;
	}
	case FStep::EKind::Perf:
		if (!bStepStarted)
		{
			bStepStarted = true;
			bPerfRunning = true;
			// The sampler outlives worlds (core ticker): reach the lab through a weak pointer so a
			// window that ends after the lab is destroyed does not write to freed memory.
			FSimCruisePerfSampler::Start(FString::Printf(TEXT("%s_%s"), *Step.Text, *CurrentView), Step.Seconds,
				[WeakLab = TWeakObjectPtr<ASimCruiseLightingLab>(this)](const FSimCruisePerfStats&)
				{
					if (ASimCruiseLightingLab* Lab = WeakLab.Get())
					{
						Lab->bPerfRunning = false;
					}
				});
			break;
		}
		if (!bPerfRunning)
		{
			Next();
		}
		break;
	case FStep::EKind::Quit:
		UE_LOG(LogSimCruiseLab, Display, TEXT("Lab script done."));
		FPlatformMisc::RequestExit(false, TEXT("SimCruiseLightingLab"));
		Next();
		break;
	}
}

void ASimCruiseLightingLab::WriteSidecar(const FString& ImagePath) const
{
	FString Json = TEXT("{\n");
	if (const ASimCruiseEnvironment* Env = Environment())
	{
		const FSimCruiseSkyPosition& Sun = Env->GetSunPosition();
		Json += FString::Printf(TEXT("  \"utc\": \"%s\",\n  \"sun_azimuth\": %.3f,\n  \"sun_elevation\": %.3f,\n  \"reference_ev100\": %.3f,\n  \"describe\": \"%s\",\n"),
			*Env->GetUtcTime().ToIso8601(), Sun.AzimuthDeg, Sun.ApparentElevationDeg, Env->GetReferenceEV100(), *Env->Describe().ReplaceCharWithEscapedChar());
	}
	Json += FString::Printf(TEXT("  \"view\": \"%s\",\n  \"preset\": \"%s\",\n  \"probes\": {\n"), *CurrentView, *FSimCruiseGraphics::Name(CurrentPreset));
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	for (int32 Index = 0; Index < Probes.Num(); ++Index)
	{
		FVector2D Screen(-1.0, -1.0);
		if (Controller)
		{
			Controller->ProjectWorldLocationToScreen(Probes[Index].WorldCm, Screen, false);
		}
		Json += FString::Printf(TEXT("    \"%s\": [%.1f, %.1f]%s\n"), *Probes[Index].Name, Screen.X, Screen.Y, Index + 1 < Probes.Num() ? TEXT(",") : TEXT(""));
	}
	Json += TEXT("  }\n}\n");
	FFileHelper::SaveStringToFile(Json, *FPaths::ChangeExtension(ImagePath, TEXT("json")));
	if (const ASimCruiseEnvironment* Env = Environment())
	{
		UE_LOG(LogSimCruiseLab, Display, TEXT("Shot %s/%s: %s"), *CurrentShot, *CurrentView, *Env->Describe());
	}
}
