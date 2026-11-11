// Copyright SimCruise contributors. All Rights Reserved.

#include "World/SimCruiseWorldResources.h"

#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "World/SimCruiseTileData.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseWorldResources, Log, All);

namespace SimCruiseWorldMaterials
{
	FString LayerName(ESimCruiseTileLayer Layer)
	{
		switch (Layer)
		{
		case ESimCruiseTileLayer::Terrain: return TEXT("Ground");
		case ESimCruiseTileLayer::Asphalt: return TEXT("Asphalt");
		case ESimCruiseTileLayer::Concrete: return TEXT("Concrete");
		case ESimCruiseTileLayer::Paint: return TEXT("Paint");
		case ESimCruiseTileLayer::Water: return TEXT("Water");
		case ESimCruiseTileLayer::Building: return TEXT("Building");
		default: return TEXT("Unknown");
		}
	}

	FString AssetPath(ESimCruiseTileLayer Layer)
	{
		const FString Name = TEXT("M_SimCruise") + LayerName(Layer);
		return FString::Printf(TEXT("/Game/World/Materials/%s.%s"), *Name, *Name);
	}

#if WITH_EDITOR
	namespace
	{
		const TCHAR* GroundCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
float2 uv = LP.xy / TileSizeCm;
float2 m = LP.xy * 0.01 + Anchor.xy;
float4 mask = saturate(Texture2DSample(Mask, MaskSampler, uv));
mask.rgb = pow(mask.rgb, 1.0 / 2.2); // stored as an sRGB texture; alpha is linear
float3 photo = Texture2DSample(Aerial, AerialSampler, (Cell.xy + uv) * 0.25).rgb;
float3 g1 = Texture2DSample(Grass, GrassSampler, m / 2.3).rgb / max(GrassMean.rgb, 0.001);
float3 g2 = Texture2DSample(Grass, GrassSampler, m / 9.7 + 0.31).rgb / max(GrassMean.rgb, 0.001);
// Two scales at an irrational ratio and a rotation, so the 512 px photograph never reads as a grid.
float3 s1 = Texture2DSample(Sand, SandSampler, m / 2.9).rgb / max(SandMean.rgb, 0.001);
float3 s2 = Texture2DSample(Sand, SandSampler, float2(m.x * 0.8 - m.y * 0.6, m.x * 0.6 + m.y * 0.8) / 7.37 + 0.17).rgb / max(SandMean.rgb, 0.001);
s1 = lerp(float3(1.0, 1.0, 1.0), s1, 0.5);
float3 p1 = Texture2DSample(Grain, GrainSampler, m / 3.1).rgb / max(GrainMean.rgb, 0.001);
float3 p2 = Texture2DSample(Grain, GrainSampler, m / 23.0 + 0.53).rgb / max(GrainMean.rgb, 0.001);
float veg = mask.r;
float sand = mask.g * (1.0 - veg);
float water = mask.a;
float paved = mask.b * (1.0 - water);
float bare = saturate(1.0 - veg - sand - paved);
float3 classColor = float3(0.15, 0.14, 0.11) * bare + float3(0.06, 0.10, 0.03) * veg + float3(0.42, 0.37, 0.28) * sand + float3(0.27, 0.26, 0.24) * paved;
// The photograph is taken from above: white roofs, sand and glare. Ground seen from the
// street is darker (grass ~0.1, concrete ~0.3, sand ~0.35 albedo), so highlights compress.
float3 shot = photo * AerialGain;
float sl = max(dot(shot, W), 0.0001);
float target = sl < 0.16 ? sl : 0.16 + (sl - 0.16) * 0.4;
shot *= target / sl;
float3 baseColor = Cell.z > 0.5 ? shot : classColor;
float3 grass = g1 * lerp(1.0, g2, 0.5);
float3 detail = (p1 * lerp(1.0, p2, 0.4)) * (bare + paved) + grass * veg + s1 * lerp(1.0, s2, 0.6) * sand;
float dl = dot(detail, W);
float3 hue = lerp(float3(1.0, 1.0, 1.0), detail / max(dl, 0.001), 0.3 * veg + 0.15 * sand);
baseColor *= lerp(1.0, dl, 0.55 * veg + 0.25 * sand + 0.4 * (bare + paved)) * hue;
baseColor = lerp(baseColor, float3(0.025, 0.15, 0.17), water);
Roughness = lerp(lerp(lerp(0.9, 0.97, veg), 0.86, sand), 0.08, water);
Nrm = normalize(VN);
return max(baseColor, 0.0);
)HLSL");

		const TCHAR* AsphaltCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
float2 m = LP.xy * 0.01 + Anchor.xy;
float meanL = max(dot(AsphaltMean.rgb, W), 0.001);
float2 uv = m / 2.6;
float a = dot(Texture2DSample(Asphalt, AsphaltSampler, uv).rgb, W) / meanL;
float ax = dot(Texture2DSample(Asphalt, AsphaltSampler, uv + float2(0.0012, 0.0)).rgb, W) / meanL;
float ay = dot(Texture2DSample(Asphalt, AsphaltSampler, uv + float2(0.0, 0.0012)).rgb, W) / meanL;
float macro = dot(Texture2DSample(Asphalt, AsphaltSampler, m / 23.0 + 0.41).rgb, W) / meanL;
float macro2 = dot(Texture2DSample(Asphalt, AsphaltSampler, m / 61.0 + 0.77).rgb, W) / meanL;
float v = lerp(1.0, macro, 0.30) * lerp(1.0, macro2, 0.25);
float fine = lerp(1.0, a, 0.85);
float3 col = Tint.rgb * fine * v;
float3 gravel = float3(0.26, 0.23, 0.19) * lerp(1.0, a, 1.0);
col = lerp(col, gravel, VC.r);
Roughness = saturate(0.80 + 0.10 * (1.0 - a) + 0.05 * VC.r);
float3 g = float3(-(ax - a), -(ay - a), 0.0) * Bump;
Nrm = normalize(VN + g);
return max(col, 0.0);
)HLSL");

		const TCHAR* ConcreteCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
float2 m = LP.xy * 0.01 + Anchor.xy;
float vertical = 1.0 - saturate(abs(VN.z) * 2.0 - 0.5);
float2 uv = lerp(m, float2(m.x + m.y, LP.z * 0.01), vertical) / 1.9;
float meanL = max(dot(GrainMean.rgb, W), 0.001);
float a = dot(Texture2DSample(Grain, GrainSampler, uv).rgb, W) / meanL;
float ax = dot(Texture2DSample(Grain, GrainSampler, uv + float2(0.0015, 0.0)).rgb, W) / meanL;
float ay = dot(Texture2DSample(Grain, GrainSampler, uv + float2(0.0, 0.0015)).rgb, W) / meanL;
float macro = dot(Texture2DSample(Grain, GrainSampler, m / 17.0 + 0.29).rgb, W) / meanL;
float3 col = Tint.rgb * lerp(1.0, a, 0.30) * lerp(1.0, macro, 0.25);
col *= lerp(1.0, 0.72, VC.g);   // gutter pan grime
col *= lerp(1.0, 1.06, VC.b);   // curb
Roughness = saturate(0.84 + 0.08 * (1.0 - a));
float3 g = float3(-(ax - a), -(ay - a), 0.0) * Bump * (1.0 - vertical);
Nrm = normalize(VN + g);
return max(col, 0.0);
)HLSL");

		const TCHAR* PaintCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
float2 m = LP.xy * 0.01 + Anchor.xy;
float meanL = max(dot(GrainMean.rgb, W), 0.001);
float a = dot(Texture2DSample(Grain, GrainSampler, m / 2.6).rgb, W) / meanL;
float worn = dot(Texture2DSample(Grain, GrainSampler, m / 9.0 + 0.61).rgb, W) / meanL;
float keep = saturate(0.55 + 0.45 * a) * saturate(0.8 + 0.2 * worn);
Roughness = 0.6 + 0.2 * (1.0 - keep);
Nrm = normalize(VN);
return VC.rgb * keep;
)HLSL");

		const TCHAR* WaterCode = TEXT(R"HLSL(
float2 p = LP.xy * 0.01 + Anchor.xy;
float2 grad = float2(0.0, 0.0);
const float2 D[6] = { float2(0.91, 0.41), float2(0.6, -0.8), float2(-0.3, 0.95), float2(0.99, -0.12), float2(-0.7, -0.71), float2(0.2, 0.98) };
const float L[6] = { 9.0, 5.3, 3.1, 1.7, 1.1, 0.63 };
const float A[6] = { 0.05, 0.03, 0.017, 0.009, 0.005, 0.003 };
for (int i = 0; i < 6; i++)
{
	float k = 6.2831853 / L[i];
	float w = sqrt(9.81 * k);
	float ph = dot(D[i], p) * k - w * Time;
	grad += D[i] * (A[i] * k * cos(ph));
}
Nrm = normalize(float3(-grad.x, -grad.y, 1.0));
Roughness = 0.04;
return lerp(float3(0.010, 0.040, 0.045), float3(0.012, 0.055, 0.055), VC.r);
)HLSL");

		const TCHAR* BuildingCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
float wall = VA;
float2 m = LP.xy * 0.01 + Anchor.xy;
float z = LP.z * 0.01;
float along = dot(LP.xy * 0.01, float2(-VN.y, VN.x));
float meanL = max(dot(GrainMean.rgb, W), 0.001);
float2 uv = lerp(m, float2(along, z), wall) / 1.7;
float grain = dot(Texture2DSample(Grain, GrainSampler, uv).rgb, W) / meanL;
float stain = dot(Texture2DSample(Grain, GrainSampler, float2(along, z) / 23.0 + 0.37).rgb, W) / meanL;
// Windows on a storey grid (3.3 m storeys, 3 m bays), none in the ground course.
float2 cell = float2(frac(along / 3.0), frac((z + 0.4) / 3.3));
float bay = step(0.2, cell.x) * step(cell.x, 0.8) * step(0.32, cell.y) * step(cell.y, 0.86);
float window = bay * wall;
float3 stucco = VC.rgb * lerp(1.0, grain, 0.12) * lerp(1.0, stain, 0.18);
float3 roof = float3(0.40, 0.39, 0.37) * lerp(1.0, grain, 0.35);
float3 col = lerp(roof, stucco, wall);
float3 glass = float3(0.025, 0.03, 0.035) + 0.02 * frac(sin(dot(floor(float2(along / 3.0, (z + 0.4) / 3.3)), float2(12.9898, 78.233))) * 43758.5453);
col = lerp(col, glass, window);
Roughness = lerp(lerp(0.9, 0.82, wall), 0.06, window);
Nrm = normalize(VN);
return max(col, 0.0);
)HLSL");

		template <typename T>
		T* AddExpression(UMaterial* Material)
		{
			T* Expression = NewObject<T>(Material);
			Material->GetExpressionCollection().AddExpression(Expression);
			return Expression;
		}

		void AddInput(UMaterialExpressionCustom* Custom, const TCHAR* Name, UMaterialExpression* Expression, int32 OutputIndex = 0)
		{
			FCustomInput Input;
			Input.InputName = Name;
			Input.Input.Connect(OutputIndex, Expression);
			Custom->Inputs.Add(Input);
		}

		UMaterialExpression* TextureParam(UMaterial* Material, const TCHAR* Name, UTexture* Default)
		{
			UMaterialExpressionTextureObjectParameter* P = AddExpression<UMaterialExpressionTextureObjectParameter>(Material);
			P->ParameterName = Name;
			P->Texture = Default;
			P->SamplerType = SAMPLERTYPE_Color;
			return P;
		}

		UMaterialExpression* VectorParam(UMaterial* Material, const TCHAR* Name, const FLinearColor& Default)
		{
			UMaterialExpressionVectorParameter* P = AddExpression<UMaterialExpressionVectorParameter>(Material);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		}

		UMaterialExpression* ScalarParam(UMaterial* Material, const TCHAR* Name, float Default)
		{
			UMaterialExpressionScalarParameter* P = AddExpression<UMaterialExpressionScalarParameter>(Material);
			P->ParameterName = Name;
			P->DefaultValue = Default;
			return P;
		}
	}

	UMaterial* BuildMaterial(UObject* Outer, FName Name, ESimCruiseTileLayer Layer, EObjectFlags Flags)
	{
		UTexture* DefaultTexture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
		UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
		Material->bTangentSpaceNormal = false;

		UMaterialExpressionCustom* Custom = AddExpression<UMaterialExpressionCustom>(Material);
		Custom->OutputType = CMOT_Float3;
		Custom->Inputs.Reset();
		Custom->Description = TEXT("SimCruise ") + LayerName(Layer);
		Custom->AdditionalOutputs.Add({ FName(TEXT("Roughness")), CMOT_Float1 });
		Custom->AdditionalOutputs.Add({ FName(TEXT("Nrm")), CMOT_Float3 });

		AddInput(Custom, TEXT("LP"), AddExpression<UMaterialExpressionLocalPosition>(Material));
		AddInput(Custom, TEXT("VN"), AddExpression<UMaterialExpressionVertexNormalWS>(Material));
		UMaterialExpressionVertexColor* VertexColor = AddExpression<UMaterialExpressionVertexColor>(Material);
		AddInput(Custom, TEXT("VC"), VertexColor);          // RGB
		AddInput(Custom, TEXT("VA"), VertexColor, 4);       // alpha (output 4)
		AddInput(Custom, TEXT("Anchor"), VectorParam(Material, TEXT("Anchor"), FLinearColor(0, 0, 0, 0)));

		switch (Layer)
		{
		case ESimCruiseTileLayer::Terrain:
		{
			Custom->Code = GroundCode;
			UMaterialExpressionConstant* GroundSpecular = AddExpression<UMaterialExpressionConstant>(Material);
			GroundSpecular->R = 0.35f; // vegetation and soil reflect less than the 0.5 default
			Material->GetEditorOnlyData()->Specular.Connect(0, GroundSpecular);
		}
			AddInput(Custom, TEXT("TileSizeCm"), ScalarParam(Material, TEXT("TileSizeCm"), 55000.f));
			AddInput(Custom, TEXT("Cell"), VectorParam(Material, TEXT("Cell"), FLinearColor(0, 0, 0, 0)));
			AddInput(Custom, TEXT("AerialGain"), ScalarParam(Material, TEXT("AerialGain"), 1.f));
			AddInput(Custom, TEXT("Mask"), TextureParam(Material, TEXT("Mask"), DefaultTexture));
			AddInput(Custom, TEXT("Aerial"), TextureParam(Material, TEXT("Aerial"), DefaultTexture));
			AddInput(Custom, TEXT("Grass"), TextureParam(Material, TEXT("Grass"), DefaultTexture));
			AddInput(Custom, TEXT("Sand"), TextureParam(Material, TEXT("Sand"), DefaultTexture));
			AddInput(Custom, TEXT("Grain"), TextureParam(Material, TEXT("Grain"), DefaultTexture));
			AddInput(Custom, TEXT("GrassMean"), VectorParam(Material, TEXT("GrassMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("SandMean"), VectorParam(Material, TEXT("SandMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("GrainMean"), VectorParam(Material, TEXT("GrainMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			break;
		case ESimCruiseTileLayer::Asphalt:
			Custom->Code = AsphaltCode;
			AddInput(Custom, TEXT("Asphalt"), TextureParam(Material, TEXT("Asphalt"), DefaultTexture));
			AddInput(Custom, TEXT("AsphaltMean"), VectorParam(Material, TEXT("AsphaltMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("Tint"), VectorParam(Material, TEXT("Tint"), FLinearColor(0.11f, 0.105f, 0.10f)));
			AddInput(Custom, TEXT("Bump"), ScalarParam(Material, TEXT("Bump"), 0.25f));
			break;
		case ESimCruiseTileLayer::Concrete:
			Custom->Code = ConcreteCode;
			AddInput(Custom, TEXT("Grain"), TextureParam(Material, TEXT("Grain"), DefaultTexture));
			AddInput(Custom, TEXT("GrainMean"), VectorParam(Material, TEXT("GrainMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("Tint"), VectorParam(Material, TEXT("Tint"), FLinearColor(0.34f, 0.33f, 0.31f)));
			AddInput(Custom, TEXT("Bump"), ScalarParam(Material, TEXT("Bump"), 0.15f));
			break;
		case ESimCruiseTileLayer::Paint:
			Custom->Code = PaintCode;
			AddInput(Custom, TEXT("Grain"), TextureParam(Material, TEXT("Grain"), DefaultTexture));
			AddInput(Custom, TEXT("GrainMean"), VectorParam(Material, TEXT("GrainMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			break;
		case ESimCruiseTileLayer::Building:
			Custom->Code = BuildingCode;
			AddInput(Custom, TEXT("Grain"), TextureParam(Material, TEXT("Grain"), DefaultTexture));
			AddInput(Custom, TEXT("GrainMean"), VectorParam(Material, TEXT("GrainMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			break;
		case ESimCruiseTileLayer::Water:
		{
			Custom->Code = WaterCode;
			AddInput(Custom, TEXT("Time"), AddExpression<UMaterialExpressionTime>(Material));
			UMaterialExpressionConstant* Specular = AddExpression<UMaterialExpressionConstant>(Material);
			Specular->R = 0.25f; // F0 ~ 0.02, water
			Material->GetEditorOnlyData()->Specular.Connect(0, Specular);
			break;
		}
		default:
			break;
		}
		Custom->RebuildOutputs();
		Material->GetEditorOnlyData()->BaseColor.Connect(0, Custom);
		Material->GetEditorOnlyData()->Roughness.Connect(1, Custom);
		Material->GetEditorOnlyData()->Normal.Connect(2, Custom);
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		return Material;
	}
#endif
}

void USimCruiseWorldResources::Initialise()
{
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	LayerMaterials.SetNum(static_cast<int32>(ESimCruiseTileLayer::Count));
	for (int32 i = 0; i < LayerMaterials.Num(); ++i)
	{
		const ESimCruiseTileLayer Layer = static_cast<ESimCruiseTileLayer>(i);
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *SimCruiseWorldMaterials::AssetPath(Layer), nullptr, LOAD_NoWarn | LOAD_Quiet);
#if WITH_EDITOR
		// Headless runs (automation with -nullrhi) draw nothing: no shader compiles for them.
		if (!Material && FApp::CanEverRender())
		{
			Material = SimCruiseWorldMaterials::BuildMaterial(this, FName(*(TEXT("M_SimCruise") + SimCruiseWorldMaterials::LayerName(Layer))), Layer, RF_Transient);
		}
#endif
		if (!Material)
		{
			UE_CLOG(FApp::CanEverRender(), LogSimCruiseWorldResources, Error, TEXT("No material for %s (run the SimCruiseBuildWorldMaterials commandlet)"), *SimCruiseWorldMaterials::LayerName(Layer));
			Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
		}
		LayerMaterials[i] = Material;
	}

	auto LoadDetail = [this](const FString& Relative, const TCHAR* Name, FLinearColor& OutMean) -> UTexture2D*
	{
		TArray<FColor> Pixels;
		int32 W = 0, H = 0;
		const FString Path = FPaths::Combine(SimCruiseTileData::RepositoryRoot(), Relative);
		if (!DecodeImageFile(Path, Pixels, W, H))
		{
			UE_LOG(LogSimCruiseWorldResources, Warning, TEXT("Missing detail texture %s"), *Path);
			return nullptr;
		}
		OutMean = MeanLinear(Pixels);
		return CreateTexture(this, Name, Pixels, W, H, true, true);
	};
	AsphaltTexture = LoadDetail(TEXT("game/art/asphalt.jpg"), TEXT("T_SimCruiseAsphalt"), AsphaltMean);
	GrassTexture = LoadDetail(TEXT("game/data/grass-detail.jpg"), TEXT("T_SimCruiseGrass"), GrassMean);
	SandTexture = LoadDetail(TEXT("game/data/mineral-ground.png"), TEXT("T_SimCruiseSand"), SandMean);
	TArray<FColor> Grey;
	Grey.Init(FColor(128, 128, 128, 255), 4 * 4);
	BlankTexture = CreateTexture(this, TEXT("T_SimCruiseBlank"), Grey, 4, 4, true, true);
	if (!AsphaltTexture) { AsphaltTexture = BlankTexture; AsphaltMean = MeanLinear(Grey); }
	if (!GrassTexture) { GrassTexture = BlankTexture; GrassMean = MeanLinear(Grey); }
	if (!SandTexture) { SandTexture = BlankTexture; SandMean = MeanLinear(Grey); }
}

UMaterialInterface* USimCruiseWorldResources::GetLayerMaterial(ESimCruiseTileLayer Layer) const
{
	const int32 Index = static_cast<int32>(Layer);
	return LayerMaterials.IsValidIndex(Index) ? LayerMaterials[Index].Get() : nullptr;
}

UTexture2D* USimCruiseWorldResources::FindAerial(const FString& Key) const
{
	const TObjectPtr<UTexture2D>* Found = Aerials.Find(Key);
	return Found ? Found->Get() : nullptr;
}

UTexture2D* USimCruiseWorldResources::AddAerial(const FString& Key, const TArray<FColor>& Pixels, int32 Width, int32 Height)
{
	if (UTexture2D* Existing = FindAerial(Key))
	{
		return Existing;
	}
	UTexture2D* Texture = CreateTexture(this, NAME_None, Pixels, Width, Height, true, false);
	if (Texture)
	{
		Aerials.Add(Key, Texture);
	}
	return Texture;
}

void USimCruiseWorldResources::TrimAerials(const TSet<FString>& Keep)
{
	for (auto It = Aerials.CreateIterator(); It; ++It)
	{
		if (!Keep.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

bool USimCruiseWorldResources::DecodeImageFile(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	TArray<uint8> Bytes;
	if (Path.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *Path, FILEREAD_Silent) || Bytes.Num() < 8)
	{
		return false;
	}
	IImageWrapperModule* Module = FModuleManager::GetModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
	if (!Module)
	{
		if (!IsInGameThread())
		{
			return false;
		}
		Module = &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	}
	const EImageFormat Format = Module->DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (Format == EImageFormat::Invalid)
	{
		return false;
	}
	TSharedPtr<IImageWrapper> Wrapper = Module->CreateImageWrapper(Format);
	TArray<uint8> Raw;
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return false;
	}
	OutWidth = Wrapper->GetWidth();
	OutHeight = Wrapper->GetHeight();
	if (Raw.Num() != OutWidth * OutHeight * 4)
	{
		return false;
	}
	OutPixels.SetNumUninitialized(OutWidth * OutHeight);
	FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), Raw.Num());
	return true;
}

FLinearColor USimCruiseWorldResources::MeanLinear(const TArray<FColor>& Pixels)
{
	if (Pixels.Num() == 0)
	{
		return FLinearColor(0.2f, 0.2f, 0.2f);
	}
	double R = 0, G = 0, B = 0;
	for (const FColor& C : Pixels)
	{
		const FLinearColor L(C); // sRGB decode
		R += L.R; G += L.G; B += L.B;
	}
	return FLinearColor(R / Pixels.Num(), G / Pixels.Num(), B / Pixels.Num(), 1.0);
}

UTexture2D* USimCruiseWorldResources::CreateTexture(UObject* Outer, FName Name, const TArray<FColor>& Pixels, int32 Width, int32 Height, bool bSRGB, bool bWrap)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
	{
		return nullptr;
	}
	UTexture2D* Texture = NewObject<UTexture2D>(Outer ? Outer : GetTransientPackage(), Name, RF_Transient);
	FTexturePlatformData* Data = new FTexturePlatformData();
	Data->SizeX = Width;
	Data->SizeY = Height;
	Data->SetNumSlices(1);
	Data->PixelFormat = PF_B8G8R8A8;
	Texture->SetPlatformData(Data);
	Texture->bNotOfflineProcessed = true;

	// Box-filtered mip chain (in sRGB-decoded space for colour images).
	TArray<FColor> Level = Pixels;
	int32 W = Width, H = Height;
	while (true)
	{
		FTexture2DMipMap* Mip = new FTexture2DMipMap(W, H, 1);
		Data->Mips.Add(Mip);
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Dest = Mip->BulkData.Realloc(static_cast<int64>(W) * H * 4);
		FMemory::Memcpy(Dest, Level.GetData(), static_cast<int64>(W) * H * 4);
		Mip->BulkData.Unlock();
		if (W == 1 && H == 1)
		{
			break;
		}
		const int32 NW = FMath::Max(1, W / 2), NH = FMath::Max(1, H / 2);
		TArray<FColor> Next;
		Next.SetNumUninitialized(NW * NH);
		for (int32 y = 0; y < NH; ++y)
		{
			for (int32 x = 0; x < NW; ++x)
			{
				FLinearColor Sum(0, 0, 0, 0);
				for (int32 dy = 0; dy < 2; ++dy)
				{
					for (int32 dx = 0; dx < 2; ++dx)
					{
						const FColor& C = Level[FMath::Min(2 * y + dy, H - 1) * W + FMath::Min(2 * x + dx, W - 1)];
						Sum += bSRGB ? FLinearColor(C) : FLinearColor(C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f);
					}
				}
				Sum *= 0.25f;
				Next[y * NW + x] = bSRGB ? Sum.ToFColor(true) : FColor(FMath::RoundToInt(Sum.R * 255.f), FMath::RoundToInt(Sum.G * 255.f), FMath::RoundToInt(Sum.B * 255.f), FMath::RoundToInt(Sum.A * 255.f));
				if (bSRGB)
				{
					Next[y * NW + x].A = static_cast<uint8>(FMath::RoundToInt(Sum.A * 255.f));
				}
			}
		}
		Level = MoveTemp(Next);
		W = NW;
		H = NH;
	}
	Texture->SRGB = bSRGB;
	Texture->AddressX = bWrap ? TA_Wrap : TA_Clamp;
	Texture->AddressY = bWrap ? TA_Wrap : TA_Clamp;
	Texture->LODGroup = TEXTUREGROUP_World;
	Texture->NeverStream = true;
	Texture->UpdateResource();
	return Texture;
}
