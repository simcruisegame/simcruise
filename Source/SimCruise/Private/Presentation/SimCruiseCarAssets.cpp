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

		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), FName(*Name), RF_Transient);
		TArray<FVertexInstanceID> Instances;
		for (int32 S = 0; S < Sections.Num(); ++S)
		{
			const FSimCruiseRigSection& Section = *Sections[S];
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			const FName Slot(*FString::Printf(TEXT("Slot%02d"), S));
			SlotNames[Group] = Slot;
			Mesh->GetStaticMaterials().Add(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), Slot));
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
	FSimCruiseCarDefinition Definition;
	if (!FSimCruiseCarDefinitionLoader::LoadById(CarId, Definition, OutError))
	{
		return nullptr;
	}
	TSharedPtr<FSimCruiseBuiltCar> Car = MakeShared<FSimCruiseBuiltCar>();
	Car->CarId = CarId;
	if (!SimCruisePresentationProfiles::Load(CarId, Car->Profile, OutError))
	{
		return nullptr;
	}
	const FString ModelPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), Car->Profile.ModelPath));
	if (!SimCruiseGltf::Load(ModelPath, Car->Model, OutError))
	{
		return nullptr;
	}
	const FSimCruiseRigInputs Inputs = FSimCruiseRigInputs::FromCar(Definition);
	FSimCruiseRiggedCar Rigged;
	FString RigError;
	const bool bRigged = SimCruiseCarModelRig::Build(Car->Model, Car->Profile, Inputs, Rigged, RigError);
	Car->Log = Rigged.Log;
	if (!bRigged)
	{
		OutError = FString::Printf(TEXT("%s: could not fit %s: %s"), *CarId, *ModelPath, *RigError);
		return nullptr;
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
