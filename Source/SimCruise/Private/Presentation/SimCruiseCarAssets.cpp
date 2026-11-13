// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarAssets.h"

#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseCarMaterialLibrary.h"
#include "Presentation/SimCruiseCarModelRig.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/Package.h"
#include "Vehicle/CarDefinition.h"
#include "Vehicle/CarDefinitionLoader.h"

#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseCarAssets, Log, All);

namespace SimCruiseCarAssetsPrivate
{
	TMap<FString, TSharedPtr<FSimCruiseBuiltCar>> Cache;

	UStaticMesh* BuildMesh(const FString& Name, const TArray<const FSimCruiseRigSection*>& Sections, bool bNanite, bool bFullPrecisionUVs = false);
	void BuildPart(FSimCruiseBuiltCar& Car, const FSimCruiseRigPart& Part, FSimCruiseBuiltPart& Out);
}

UStaticMesh* SimCruiseCarAssets::BuildStaticMesh(const FString& Name, const TArray<const FSimCruiseRigSection*>& Sections, bool bNanite, bool bFullPrecisionUVs)
{
	return SimCruiseCarAssetsPrivate::BuildMesh(Name, Sections, bNanite, bFullPrecisionUVs);
}

namespace SimCruiseCarAssetsPrivate
{
	UStaticMesh* BuildMesh(const FString& Name, const TArray<const FSimCruiseRigSection*>& Sections, bool bNanite, bool bFullPrecisionUVs)
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();

		int32 VertexCount = 0, TriangleCount = 0;
		bool bAnyTangents = false;
		for (const FSimCruiseRigSection* Section : Sections)
		{
			VertexCount += Section->Positions.Num();
			TriangleCount += Section->NumTriangles();
			bAnyTangents |= Section->Tangents.Num() == Section->Positions.Num() && Section->Positions.Num() > 0;
		}
		Description.ReserveNewVertices(VertexCount);
		Description.ReserveNewVertexInstances(VertexCount);
		Description.ReserveNewTriangles(TriangleCount);
		Description.ReserveNewPolygonGroups(Sections.Num());

		// Unique: a rebuilt car (after ClearCache) must not replace meshes still drawn by older parts.
		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(),
			StaticFindObjectFast(nullptr, GetTransientPackage(), FName(*Name)) ? MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), FName(*Name)) : FName(*Name), RF_Transient);
		TArray<FVertexInstanceID> Instances;
		for (int32 S = 0; S < Sections.Num(); ++S)
		{
			const FSimCruiseRigSection& Section = *Sections[S];
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			const FName Slot(*FString::Printf(TEXT("Slot%02d"), S));
			SlotNames[Group] = Slot;
			Mesh->GetStaticMaterials().Add_GetRef(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), Slot)).UVChannelData = FMeshUVChannelInfo(1.f);
			Instances.SetNumUninitialized(Section.Positions.Num());
			const bool bTangents = Section.Tangents.Num() == Section.Positions.Num();
			for (int32 V = 0; V < Section.Positions.Num(); ++V)
			{
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = Section.Positions[V];
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				Normals[Instance] = Section.Normals.IsValidIndex(V) ? Section.Normals[V] : FVector3f::UpVector;
				if (bTangents)
				{
					const FVector4f& T = Section.Tangents[V];
					Tangents[Instance] = FVector3f(T.X, T.Y, T.Z);
					Signs[Instance] = T.W >= 0.f ? 1.f : -1.f;
				}
				UVs.Set(Instance, 0, Section.UV0.IsValidIndex(V) ? Section.UV0[V] : FVector2f::ZeroVector);
				Instances[V] = Instance;
			}
			for (int32 I = 0; I + 2 < Section.Indices.Num(); I += 3)
			{
				const uint32 A = Section.Indices[I], B = Section.Indices[I + 1], C = Section.Indices[I + 2];
				if (A == B || B == C || A == C) continue;
				Description.CreateTriangle(Group, {Instances[A], Instances[B], Instances[C]});
			}
		}

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bMarkPackageDirty = false;
		Params.bUseHashAsGuid = true;
		Params.bBuildSimpleCollision = false;
		Params.bCommitMeshDescription = true;
		Params.bAllowCpuAccess = false;
#if WITH_EDITOR
		// Full build: author normals kept, MikkTSpace tangents, Nanite for big parts.
		Params.bFastBuild = false;
		Mesh->SetNumSourceModels(1);
		FMeshBuildSettings& Build = Mesh->GetSourceModel(0).BuildSettings;
		Build.bRecomputeNormals = false;
		Build.bRecomputeTangents = !bAnyTangents;
		Build.bUseMikkTSpace = true;
		Build.bGenerateLightmapUVs = false;
		Build.bRemoveDegenerates = true;
		Build.bUseFullPrecisionUVs = bFullPrecisionUVs;
		Build.bUseHighPrecisionTangentBasis = false;
		FMeshNaniteSettings Nanite = Mesh->GetNaniteSettings();
		Nanite.bEnabled = bNanite;
		// Keep the car's fine curvature in the fallback mesh too (ray tracing, reflections).
		Nanite.FallbackPercentTriangles = 1.f;
		Mesh->SetNaniteSettings(Nanite);
#else
		Params.bFastBuild = true;
		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Description);
		FStaticMeshOperations::ComputeTangentsAndNormals(Description, EComputeNTBsFlags::Tangents | EComputeNTBsFlags::WeightedNTBs);
#endif
		Mesh->bSupportRayTracing = true;
		TArray<const FMeshDescription*> Descriptions = {&Description};
		if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params))
		{
			UE_LOG(LogSimCruiseCarAssets, Error, TEXT("Mesh build failed for %s"), *Name);
			return nullptr;
		}
#if WITH_EDITOR
		FStaticMeshCompilingManager::Get().FinishCompilation({Mesh});
#endif
		return Mesh;
	}

	void BuildPart(FSimCruiseBuiltCar& Car, const FSimCruiseRigPart& Part, FSimCruiseBuiltPart& Out)
	{
		Out.Name = Part.Name;
		Out.PartToVehicle = Part.PartToVehicle;
		Out.Triangles = Part.NumTriangles();
		TArray<const FSimCruiseRigSection*> Solid, Clear;
		for (const FSimCruiseRigSection& Section : Part.Sections)
		{
			if (Section.NumTriangles() == 0) continue;
			const FSimCruiseGltfMaterial* Author = Car.Model.Materials.IsValidIndex(Section.MaterialIndex) ? &Car.Model.Materials[Section.MaterialIndex] : nullptr;
			Car.Roles.Add(Section.MaterialIndex, Section.Role);
			if (SimCruiseCarMaterials::MasterFor(Section.Role, Author) == ESimCruiseCarMaster::Glass)
			{
				Clear.Add(&Section);
				Out.GlassMaterials.Add(Section.MaterialIndex);
			}
			else
			{
				Solid.Add(&Section);
				Out.MeshMaterials.Add(Section.MaterialIndex);
			}
		}
		int32 SolidTriangles = 0;
		for (const FSimCruiseRigSection* Section : Solid) SolidTriangles += Section->NumTriangles();
		const FString Base = FString::Printf(TEXT("SM_%s_%s"), *Car.CarId, *Part.Name.ToString());
		if (Solid.Num() > 0)
		{
			Out.bNanite = SolidTriangles >= SimCruiseCarAssets::NaniteMinTriangles;
			Out.Mesh.Reset(BuildMesh(Base, Solid, Out.bNanite));
		}
		if (Clear.Num() > 0)
		{
			Out.Glass.Reset(BuildMesh(Base + TEXT("_Glass"), Clear, false));
		}
	}
}

TSharedPtr<FSimCruiseBuiltCar> SimCruiseCarAssets::Get(const FString& CarId, FString& OutError)
{
	using namespace SimCruiseCarAssetsPrivate;
	if (TSharedPtr<FSimCruiseBuiltCar>* Found = Cache.Find(CarId))
	{
		return *Found;
	}
	const double Start = FPlatformTime::Seconds();
	TSharedPtr<FSimCruiseBuiltCar> Car = MakeShared<FSimCruiseBuiltCar>();
	Car->CarId = CarId;
	if (!SimCruisePresentationProfiles::Load(CarId, Car->Profile, OutError))
	{
		return nullptr;
	}
	// A driven car is fitted to its driving data; a car that only parks, to its profile's dimensions.
	FSimCruiseRigInputs Inputs;
	if (const TOptional<FSimCruiseCarPresentationProfile::FParkedDimensions>& Parked = Car->Profile.Parked; Parked.IsSet())
	{
		Inputs.WheelbaseM = Parked->WheelbaseM;
		Inputs.FrontTrackM = Parked->FrontTrackM;
		Inputs.RearTrackM = Parked->RearTrackM;
		Inputs.TireRadiusM = Parked->TyreRadiusM;
		Inputs.TireWidthM = Parked->TyreWidthM > 0.0 ? Parked->TyreWidthM : Inputs.TireWidthM;
		Car->SteeringRatio = Car->Profile.SteeringRatio;
	}
	else
	{
		FSimCruiseCarDefinition Definition;
		if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Definition, OutError))
		{
			return nullptr;
		}
		Inputs = FSimCruiseRigInputs::FromCar(Definition);
	}
	const FString ModelPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Car->Profile.ModelPath));
	if (!SimCruiseGltf::Load(ModelPath, Car->Model, OutError))
	{
		return nullptr;
	}
	FSimCruiseRiggedCar Rigged;
	FString RigError;
	const bool bRigged = SimCruiseCarModelRig::Build(Car->Model, Car->Profile, Inputs, Rigged, RigError);
	Car->Log = Rigged.Log;
	if (bRigged && Car->Profile.WheelDesign.IsSet())
	{
		// The profile's wheel design replaces the model's rims and tyres; discs and calipers stay.
		// Its own untextured materials: a bright silver-painted face, a polished lip, a dark cap.
		auto AddMaterial = [&Car](const TCHAR* Name, const FLinearColor& Base, float Metallic, float Roughness)
		{
			FSimCruiseGltfMaterial M;
			M.Name = Name;
			M.BaseColorFactor = Base;
			M.MetallicFactor = Metallic;
			M.RoughnessFactor = Roughness;
			return Car->Model.Materials.Add(M);
		};
		SimCruiseWheelBuilder::FMaterials Materials;
		Materials.Face = AddMaterial(TEXT("simcruise_wheel_face"), FLinearColor(0.52f, 0.53f, 0.55f), 1.f, 0.38f);
		Materials.Lip = AddMaterial(TEXT("simcruise_wheel_lip"), FLinearColor(0.91f, 0.91f, 0.92f), 1.f, 0.1f);
		Materials.Cap = AddMaterial(TEXT("simcruise_wheel_cap"), FLinearColor(0.02f, 0.02f, 0.022f), 0.f, 0.45f);
		// Plain rubber: the model's tyre material carries its own texture and sidewall detail, laid out for its UVs.
		Materials.Tyre = AddMaterial(TEXT("simcruise_wheel_tyre"), FLinearColor(0.025f, 0.025f, 0.026f), 0.f, 0.85f);
		for (int32 W = 0; W < 4; ++W)
		{
			SimCruiseWheelBuilder::FMaterials Own = Materials;
			TArray<FSimCruiseRigSection> Kept;
			for (FSimCruiseRigSection& Section : Rigged.Wheels[W].Sections)
			{
				if (Section.Role == ESimCruiseCarMaterialRole::BrakeDisc || Section.Role == ESimCruiseCarMaterialRole::BrakeCaliper) Kept.Add(MoveTemp(Section));
			}
			SimCruiseWheelBuilder::BuildMeshWheel(Car->Profile.WheelDesign, Inputs.TireRadiusM * 100.0, Inputs.TireWidthM * 100.0,
				(W == 0 || W == 2) ? -1.0 : 1.0, Own, Kept);
			Rigged.Wheels[W].Sections = MoveTemp(Kept);
		}
		Rigged.Log.Add(FString::Printf(TEXT("wheel design '%s': %.0fx%.1f in, %.1f in lip, %d spokes (%s)"), *Car->Profile.WheelDesign.Style,
			Car->Profile.WheelDesign.RimDiameterIn, Car->Profile.WheelDesign.RimWidthIn, Car->Profile.WheelDesign.LipIn, Car->Profile.WheelDesign.Spokes,
			*Car->Profile.WheelDesign.Source));
		Car->Log = Rigged.Log;
	}
	if (!bRigged)
	{
		OutError = FString::Printf(TEXT("%s: could not fit %s: %s"), *CarId, *ModelPath, *RigError);
		return nullptr;
	}
	// Role areas split some author materials: their copies follow the model's own.
	for (const FSimCruiseRiggedCar::FExtraMaterial& Extra : Rigged.ExtraMaterials)
	{
		FSimCruiseGltfMaterial Copy = Car->Model.Materials.IsValidIndex(Extra.Source) ? Car->Model.Materials[Extra.Source] : FSimCruiseGltfMaterial();
		Copy.Name += FString(TEXT("#")) + SimCruiseCarMaterialRoles::ToString(Extra.Role);
		Car->Model.Materials.Add(Copy);
	}
	// Geometry now lives in the rig parts; keep only materials and images.
	Car->Model.Primitives.Empty();

	BuildPart(*Car, Rigged.Body, Car->Body);
	for (int32 W = 0; W < 4; ++W)
	{
		BuildPart(*Car, Rigged.Wheels[W], Car->Wheels[W]);
		BuildPart(*Car, Rigged.Uprights[W], Car->Uprights[W]);
		Car->HubsCm[W] = Rigged.HubsCm[W];
	}
	Car->bHasSteeringWheel = Rigged.bHasSteeringWheel;
	if (Rigged.bHasSteeringWheel)
	{
		BuildPart(*Car, Rigged.SteeringWheel, Car->SteeringWheel);
	}
	Car->DriverEyeCm = Inputs.DriverEyeM * 100.0;
	Car->TyreRadiusCm = Inputs.TireRadiusM * 100.0;
	Car->HeadLampsCm = SimCruiseCarModelRig::HeadLampCentresCm(Rigged);
	Car->MirrorsCm = SimCruiseCarModelRig::MirrorCentresCm(Rigged);
	Car->Dials = Rigged.Dials;
	// Each dial's face: a disc in its own frame (X to the dial's 12 o'clock, Z toward the driver),
	// UVs across it for the live face drawn into a render target. Both windings: seen from either side.
	for (int32 D = 0; D < Car->Dials.Num(); ++D)
	{
		const FSimCruiseRiggedCar::FDial& Dial = Car->Dials[D];
		FSimCruiseRigPart Part;
		Part.Name = FName(*FString::Printf(TEXT("dial_%d"), D));
		const FVector X = Dial.Up, Z = Dial.Normal, Y = Z ^ X;
		Part.PartToVehicle = FTransform(X, Y, Z, Dial.CentreCm);
		FSimCruiseRigSection& Section = Part.Sections.AddDefaulted_GetRef();
		Section.MaterialIndex = SimCruiseCarAssets::DialMaterialIndex(D);
		Section.Role = ESimCruiseCarMaterialRole::Gauge;
		constexpr int32 Segments = 48;
		const float R = static_cast<float>(Dial.RadiusCm);
		Section.Positions.Add(FVector3f::ZeroVector);
		Section.Normals.Add(FVector3f(0.f, 0.f, 1.f));
		Section.UV0.Add(FVector2f(0.5f, 0.5f));
		for (int32 K = 0; K < Segments; ++K)
		{
			const float A = UE_TWO_PI * K / Segments;
			const float C = FMath::Cos(A), S = FMath::Sin(A);
			Section.Positions.Add(FVector3f(R * C, R * S, 0.f));
			Section.Normals.Add(FVector3f(0.f, 0.f, 1.f));
			// u to the right (+Y), v down (-X): the canvas reads upright on the dial.
			Section.UV0.Add(FVector2f(0.5f + 0.5f * S, 0.5f - 0.5f * C));
		}
		for (int32 K = 0; K < Segments; ++K)
		{
			const uint32 A = 1 + K, B = 1 + (K + 1) % Segments;
			Section.Indices.Append({0u, A, B, 0u, B, A});
		}
		BuildPart(*Car, Part, Car->DialParts.AddDefaulted_GetRef());
		Car->Log.Add(FString::Printf(TEXT("dial %d (%s) at %s cm, radius %.1f cm"), D,
			Car->Profile.Dials.IsValidIndex(Dial.Spec) ? *Car->Profile.Dials[Dial.Spec].Quantity : TEXT("?"), *Dial.CentreCm.ToString(), Dial.RadiusCm));
	}
	for (const FVector4& Mirror : Car->MirrorsCm)
	{
		if (Mirror.W > 0.0) Car->Log.Add(FString::Printf(TEXT("mirror glass at %s cm"), *FVector(Mirror).ToString()));
	}
	for (const FVector& Lamp : Car->HeadLampsCm)
	{
		Car->Log.Add(FString::Printf(TEXT("head-lamp beam origin %s cm"), *Lamp.ToString()));
	}
	Car->SteeringRatio = Car->Profile.SteeringRatio;
	Car->BuildSeconds = FPlatformTime::Seconds() - Start;
	Car->Log.Add(FString::Printf(TEXT("built in %.1f s (body %d triangles%s)"), Car->BuildSeconds, Car->Body.Triangles,
		Car->Body.bNanite ? TEXT(", Nanite") : TEXT("")));
	for (const FString& Line : Car->Log)
	{
		UE_LOG(LogSimCruiseCarAssets, Display, TEXT("%s: %s"), *CarId, *Line);
	}
	Cache.Add(CarId, Car);
	return Car;
}

void SimCruiseCarAssets::ClearCache()
{
	SimCruiseCarAssetsPrivate::Cache.Reset();
}
