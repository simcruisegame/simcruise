// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarMaterialLibrary.h"

#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Presentation/SimCruiseGltfModel.h"
#include "Presentation/SimCruisePresentationProfile.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectHash.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSimCruiseCarMaterials, Log, All);

namespace SimCruiseCarMaterialsPrivate
{
	const TCHAR* MasterNames[3] = {TEXT("M_SimCruiseCarOpaque"), TEXT("M_SimCruiseCarMasked"), TEXT("M_SimCruiseCarGlass")};

	TStrongObjectPtr<UMaterialInterface> Masters[3];
	TMap<FString, TStrongObjectPtr<UTexture2D>> TextureCache;

	// Detail kinds understood by the surface HLSL (Det.x).
	enum EKind : int32
	{
		KindPlain = 0,
		KindPaint = 1,
		KindGrainedPlastic = 2,
		KindRubber = 3,
		KindTyre = 4,
		KindDisc = 5,
		KindCaliper = 6,
		KindChrome = 7,
		KindBrushed = 8,
		KindInteriorPlastic = 10,
		KindCloth = 11,
		KindLeather = 12,
		KindCarpet = 13,
		KindUnderbody = 16,
		KindAsphalt = 17,
	};

	/**
	 * Surface HLSL: base colour, metallic, roughness, clear coat and the
	 * local-space slope perturbations of the clear coat (GTop) and the base
	 * layer (GBot). P is the part-local position in centimetres; every
	 * feature fades out once it is smaller than about a pixel (fp: cm/pixel)
	 * and its lost variance is moved into roughness.
	 */
	const TCHAR* SurfaceCode = TEXT(R"HLSL(
struct FSimCruiseNoise
{
	float H(float3 p) { p = frac(p * 0.1031); p += dot(p, p.zyx + 31.32); return frac((p.x + p.y) * p.z); }
	float3 H3(float3 p) { p = frac(p * float3(0.1031, 0.1030, 0.0973)); p += dot(p, p.yxz + 33.33); return frac((p.xxy + p.yxx) * p.zyx); }
	float V(float3 p)
	{
		float3 i = floor(p); float3 f = frac(p); float3 u = f * f * (3.0 - 2.0 * f);
		float a = H(i), b = H(i + float3(1, 0, 0)), c = H(i + float3(0, 1, 0)), d = H(i + float3(1, 1, 0));
		float e = H(i + float3(0, 0, 1)), g = H(i + float3(1, 0, 1)), h = H(i + float3(0, 1, 1)), k = H(i + float3(1, 1, 1));
		return lerp(lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y), lerp(lerp(e, g, u.x), lerp(h, k, u.x), u.y), u.z);
	}
	float3 G(float3 p)
	{
		const float e = 0.2; float c = V(p);
		return float3(V(p + float3(e, 0, 0)) - c, V(p + float3(0, e, 0)) - c, V(p + float3(0, 0, e)) - c) / e;
	}
	float3 Flake(float3 p, out float Id)
	{
		float3 i = floor(p); float3 f = frac(p); float best = 8.0; float3 cell = i;
		for (int z = -1; z <= 1; z++)
		for (int y = -1; y <= 1; y++)
		for (int x = -1; x <= 1; x++)
		{
			float3 g = float3(x, y, z); float3 r = g + H3(i + g) - f; float d = dot(r, r);
			if (d < best) { best = d; cell = i + g; }
		}
		Id = H(cell + 17.0);
		return H3(cell + 5.0) * 2.0 - 1.0;
	}
	float Weave(float3 p)
	{
		float3 w = sin(p * 6.2831853);
		return w.x * w.y + w.y * w.z + w.z * w.x;
	}
};
FSimCruiseNoise Nz;
float Kind = Det.x;
float Fp = max(length(fwidth(P)), 1e-4);
float3 BaseC = Base.rgb * lerp(float3(1, 1, 1), TBase.rgb, Flags.x);
float Mt = Surf.x * lerp(1.0, TORM.b, Flags.z);
float Rg = Surf.y * lerp(1.0, TORM.g, Flags.z);
float Cc = Surf.z;
float Ccr = Surf.w;
float Occ = lerp(1.0, TORM.r, Flags.w);
float3 Top = 0;
float3 Bot = 0;
float Strength = Det.w;
#define SC_FADE(s) saturate(((s) / Fp - 1.0) * 0.5)
if (Kind > 0.5 && Kind < 1.5)
{
	// Base coat flakes (mica/aluminium) under a clear coat with orange peel.
	const float Cell = 0.055;
	float Id;
	float3 Tilt = Nz.Flake(P / Cell, Id);
	float Fl = Det.y * SC_FADE(Cell);
	Bot = Tilt * 0.32 * Fl;
	Rg = sqrt(Rg * Rg + Det.y * 0.05 * (1.0 - SC_FADE(Cell)));
	BaseC *= lerp(1.0, 0.82 + 0.36 * Id, Fl * 0.6);
	const float Peel = 0.32;
	Top = Nz.G(P / Peel) * 0.010 * Det.z * SC_FADE(Peel);
	Top += Nz.G(P / 7.0) * 0.0025 * Det.z;
}
else if (Kind > 1.5 && Kind < 2.5)
{
	// Exterior textured plastic: pebble grain about 0.8 mm.
	const float S = 0.08;
	float F = SC_FADE(S);
	Bot = Nz.G(P / S) * 0.22 * F * Strength + Nz.G(P / 0.6) * 0.03;
	Rg = saturate(Rg + (Nz.V(P / S) - 0.5) * 0.18 * F + 0.06 * (1.0 - F));
	Top = Bot;
}
else if (Kind > 2.5 && Kind < 3.5)
{
	const float S = 0.05;
	float F = SC_FADE(S);
	Bot = Nz.G(P / S) * 0.12 * F; Top = Bot;
	Rg = saturate(Rg + (Nz.V(P / 0.4) - 0.5) * 0.1);
}
else if (Kind > 3.5 && Kind < 4.5)
{
	// Tyre (part frame: hub origin, axle along Y). Tread faces outward, the
	// sidewall faces along the axle; rubber grain plus soft moulding.
	float Side = saturate((abs(NL.y) - 0.35) * 3.0);
	const float S = 0.04;
	float F = SC_FADE(S);
	Bot = Nz.G(P / S) * 0.10 * F + Nz.G(P / 0.5) * 0.02;
	Top = Bot;
	Rg = lerp(0.88, 0.78, Side) + (Nz.V(P / 0.7) - 0.5) * 0.08;
	BaseC = lerp(float3(0.030, 0.030, 0.031), float3(0.036, 0.036, 0.037), Side) * lerp(float3(1, 1, 1), TBase.rgb * 3.0, Flags.x);
	// Road dust settles on the tread shoulders.
	BaseC = lerp(BaseC, float3(0.075, 0.068, 0.060), (1.0 - Side) * 0.25 * Ext.x);
}
else if (Kind > 4.5 && Kind < 5.5)
{
	// Cast-iron disc: concentric lathe marks on the friction faces, rust on edges.
	float R = length(P.xz);
	float3 Radial = float3(P.x, 0, P.z) / max(R, 1e-3);
	const float Pitch = 0.045;
	float F = SC_FADE(Pitch);
	float Face = saturate((abs(NL.y) - 0.6) * 4.0);
	Bot = Radial * cos(R / Pitch * 6.2831853) * 0.06 * F * Face + Nz.G(P / 0.3) * 0.02;
	Top = Bot;
	Rg = lerp(0.62, 0.32 + (Nz.V(float3(R / 0.08, 0, 0)) - 0.5) * 0.12, Face) + 0.08 * (1.0 - F) * Face;
	Mt = lerp(0.55, 1.0, Face);
	BaseC = lerp(float3(0.20, 0.11, 0.065), float3(0.40, 0.39, 0.38), Face);
}
else if (Kind > 5.5 && Kind < 6.5)
{
	const float S = 0.15;
	float F = SC_FADE(S);
	Bot = Nz.G(P / S) * 0.2 * F; Top = Bot;
	Rg = saturate(Rg + (Nz.V(P / S) - 0.5) * 0.15);
}
else if (Kind > 6.5 && Kind < 7.5)
{
	Top = Nz.G(P / 3.0) * 0.003; Bot = Top;
}
else if (Kind > 7.5 && Kind < 8.5)
{
	// Brushed: grain stretched along the part's X.
	float3 Q = P * float3(0.5, 25.0, 25.0);
	float F = SC_FADE(0.04);
	Bot = Nz.G(Q) * float3(0.5, 25.0, 25.0) * 0.0015 * F; Top = Bot;
	Rg = saturate(Rg + (Nz.V(Q) - 0.5) * 0.12 * F + 0.05 * (1.0 - F));
}
else if (Kind > 9.5 && Kind < 10.5)
{
	// Moulded interior plastic: fine pebble grain, satin sheen.
	const float S = 0.06;
	float F = SC_FADE(S);
	Bot = (Nz.G(P / S) * 0.28 * F + Nz.G(P / 0.35) * 0.05) * Strength; Top = Bot;
	Rg = saturate(Rg + (Nz.V(P / S) - 0.5) * 0.16 * F + 0.05 * (1.0 - F));
	BaseC *= 0.92 + 0.16 * Nz.V(P / 4.0);
}
else if (Kind > 10.5 && Kind < 11.5)
{
	// Woven seat cloth.
	const float S = 0.12;
	float F = SC_FADE(S);
	float W = Nz.Weave(P / S);
	const float E = 0.08;
	float3 Gw = float3(Nz.Weave((P + float3(E, 0, 0)) / S) - W, Nz.Weave((P + float3(0, E, 0)) / S) - W, Nz.Weave((P + float3(0, 0, E)) / S) - W) / E;
	Bot = Gw * 0.025 * F; Top = Bot;
	BaseC *= lerp(1.0, 0.85 + 0.15 * W, F) * (0.9 + 0.2 * Nz.V(P / 0.7));
	Rg = 0.95;
}
else if (Kind > 11.5 && Kind < 12.5)
{
	// Leather grain.
	const float S = 0.11;
	float F = SC_FADE(S);
	Bot = (Nz.G(P / S) * 0.25 * F + Nz.G(P / 0.03) * 0.08 * SC_FADE(0.03)) * Strength; Top = Bot;
	Rg = saturate(Rg + (Nz.V(P / S) - 0.5) * 0.2 * F);
}
else if (Kind > 12.5 && Kind < 13.5)
{
	const float S = 0.05;
	float F = SC_FADE(S);
	Bot = Nz.G(P / S) * 0.4 * F; Top = Bot;
	BaseC *= 0.7 + 0.6 * lerp(0.5, Nz.V(P / S), F);
	Rg = 1.0;
}
else if (Kind > 15.5 && Kind < 16.5)
{
	Bot = Nz.G(P / 0.5) * 0.08; Top = Bot;
	Rg = saturate(Rg + (Nz.V(P / 6.0) - 0.5) * 0.2);
	BaseC *= 0.75 + 0.5 * Nz.V(P / 9.0);
}
else if (Kind > 16.5 && Kind < 17.5)
{
	// Asphalt concrete: stone chips of 3-8 mm in a dark binder, worn and
	// stained at the metre scale. No texture, so nothing tiles.
	const float Chip = 0.45;
	float Id;
	float3 Tilt = Nz.Flake(P / Chip, Id);
	float F = SC_FADE(Chip);
	float Stone = lerp(0.123, 0.05 + Id * Id * 0.22, F);
	float Wear = Nz.V(P / 180.0) * 0.6 + Nz.V(P / 37.0) * 0.4;
	BaseC = float3(1.0, 0.985, 0.96) * lerp(0.055, Stone, 0.65) * (0.8 + 0.4 * Wear) * Base.rgb;
	Bot = Tilt * 0.25 * F + Nz.G(P / 0.15) * 0.1 * SC_FADE(0.15);
	Top = Bot;
	Rg = 0.88 + (Nz.V(P / 3.0) - 0.5) * 0.1;
}
#undef SC_FADE
Metallic = saturate(Mt);
Roughness = clamp(Rg, 0.02, 1.0);
ClearCoatAmount = saturate(Cc);
ClearCoatRough = clamp(Ccr, 0.01, 1.0);
AO = Occ;
GTop = Top;
GBot = Bot;
Alpha = Base.a * lerp(1.0, TBase.a, Flags.x);
return BaseC;
)HLSL");

	/** Combines the vertex normal, the author's normal map and the detail slopes (world space). */
	const TCHAR* NormalCode = TEXT(R"HLSL(
float Sgn = Parameters.TwoSidedSign;
float3 N = normalize(lerp(Nw, Nm, UseN));
float3 T = GT - N * dot(GT, N);
float3 B = GB - N * dot(GB, N);
BottomNormal = normalize(N - B) * Sgn;
return normalize(N - T) * Sgn;
)HLSL");

	const TCHAR* GlassCode = TEXT(R"HLSL(
float3 Tex = lerp(float3(1, 1, 1), TBase.rgb, GS.w);
Rough = clamp(GS.x, 0.0, 1.0);
Coverage = saturate(GS.y);
Spec = GS.z;
Emis = Em.rgb * Em.a * Tex;
return Base.rgb * Tex;
)HLSL");

#if WITH_EDITOR
	template <typename T>
	T* Add(UMaterial* Material, int32 X, int32 Y)
	{
		T* Expression = NewObject<T>(Material, NAME_None, RF_Transactional);
		Material->GetExpressionCollection().AddExpression(Expression);
		Expression->Material = Material;
		Expression->MaterialExpressionEditorX = X;
		Expression->MaterialExpressionEditorY = Y;
		Expression->UpdateMaterialExpressionGuid(true, false);
		return Expression;
	}

	UMaterialExpressionVectorParameter* Vector(UMaterial* Material, const TCHAR* Name, const FLinearColor& Default, int32 Y)
	{
		UMaterialExpressionVectorParameter* P = Add<UMaterialExpressionVectorParameter>(Material, -900, Y);
		P->ParameterName = Name;
		P->DefaultValue = Default;
		P->UpdateParameterGuid(true, false);
		return P;
	}

	/** A 4x4 white texture in linear space: the default for linear (mask) samplers. */
	UTexture* LinearWhite()
	{
		static TStrongObjectPtr<UTexture> Texture;
		if (!Texture.IsValid())
		{
			FImage Image(4, 4, ERawImageFormat::BGRA8, EGammaSpace::Linear);
			FMemory::Memset(Image.RawData.GetData(), 0xFF, Image.RawData.Num());
			UTexture* Created = FImageUtils::CreateTexture(ETextureClass::TwoD, Image, GetTransientPackage(), TEXT("T_SimCruiseLinearWhite"), RF_Transient, false);
			Created->SRGB = false;
			Created->CompressionSettings = TC_Masks;
			Created->PostEditChange();
			Texture.Reset(Created);
		}
		return Texture.Get();
	}

	UMaterialExpressionTextureSampleParameter2D* TextureParam(UMaterial* Material, const TCHAR* Name, const TCHAR* DefaultPath, EMaterialSamplerType Sampler, int32 Y)
	{
		UMaterialExpressionTextureSampleParameter2D* P = Add<UMaterialExpressionTextureSampleParameter2D>(Material, -900, Y);
		P->ParameterName = Name;
		P->Texture = Sampler == SAMPLERTYPE_Masks ? LinearWhite() : LoadObject<UTexture>(nullptr, DefaultPath);
		P->SamplerType = Sampler;
		P->UpdateParameterGuid(true, false);
		return P;
	}

	void AddInput(UMaterialExpressionCustom* Custom, const TCHAR* Name, UMaterialExpression* From, int32 OutputIndex = 0)
	{
		FCustomInput& Input = Custom->Inputs.AddDefaulted_GetRef();
		Input.InputName = Name;
		Input.Input.Connect(OutputIndex, From);
	}

	void AddOutput(UMaterialExpressionCustom* Custom, const TCHAR* Name, ECustomMaterialOutputType Type)
	{
		FCustomOutput& Output = Custom->AdditionalOutputs.AddDefaulted_GetRef();
		Output.OutputName = Name;
		Output.OutputType = Type;
	}

	UMaterial* BuildSurfaceMaster(bool bMasked)
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), MasterNames[bMasked ? 1 : 0], RF_Transient | RF_Public);
		Material->SetShadingModel(MSM_ClearCoat);
		Material->BlendMode = bMasked ? BLEND_Masked : BLEND_Opaque;
		Material->TwoSided = true;
		Material->bTangentSpaceNormal = false;
		Material->SetUsageByFlag(MATUSAGE_Nanite, true);
		Material->SetUsageByFlag(MATUSAGE_StaticLighting, false);
		if (bMasked)
		{
			Material->OpacityMaskClipValue = 0.33f;
		}
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();

		UMaterialExpressionVectorParameter* Base = Vector(Material, TEXT("BaseColor"), FLinearColor(0.5f, 0.5f, 0.5f, 1.f), 0);
		UMaterialExpressionVectorParameter* Surf = Vector(Material, TEXT("Surface"), FLinearColor(0.f, 0.5f, 0.f, 0.05f), 150);
		UMaterialExpressionVectorParameter* Det = Vector(Material, TEXT("Detail"), FLinearColor(0.f, 0.f, 0.f, 1.f), 300);
		UMaterialExpressionVectorParameter* Flags = Vector(Material, TEXT("TextureFlags"), FLinearColor(0.f, 0.f, 0.f, 0.f), 450);
		UMaterialExpressionVectorParameter* Ext = Vector(Material, TEXT("Extra"), FLinearColor(0.f, 0.f, 0.f, 0.f), 600);
		UMaterialExpressionVectorParameter* Emissive = Vector(Material, TEXT("Emissive"), FLinearColor(0.f, 0.f, 0.f, 0.f), 750);
		UMaterialExpressionTextureCoordinate* UV = Add<UMaterialExpressionTextureCoordinate>(Material, -1200, 900);
		UMaterialExpressionTextureSampleParameter2D* BaseTex = TextureParam(Material, TEXT("BaseTexture"), TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), SAMPLERTYPE_Color, 900);
		UMaterialExpressionTextureSampleParameter2D* NormalTex = TextureParam(Material, TEXT("NormalTexture"), TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"), SAMPLERTYPE_Normal, 1100);
		UMaterialExpressionTextureSampleParameter2D* OrmTex = TextureParam(Material, TEXT("MetallicRoughnessTexture"), TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), SAMPLERTYPE_Masks, 1300);
		UMaterialExpressionTextureSampleParameter2D* EmissiveTex = TextureParam(Material, TEXT("EmissiveTexture"), TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), SAMPLERTYPE_Color, 1500);
		for (UMaterialExpressionTextureSampleParameter2D* Tex : {BaseTex, NormalTex, OrmTex, EmissiveTex})
		{
			Tex->Coordinates.Connect(0, UV);
		}
		UMaterialExpressionLocalPosition* LocalPos = Add<UMaterialExpressionLocalPosition>(Material, -900, -200);
		UMaterialExpressionVertexNormalWS* NormalWS = Add<UMaterialExpressionVertexNormalWS>(Material, -900, -350);
		UMaterialExpressionTransform* NormalLocal = Add<UMaterialExpressionTransform>(Material, -600, -350);
		NormalLocal->TransformSourceType = TRANSFORMSOURCE_World;
		NormalLocal->TransformType = TRANSFORM_Local;
		NormalLocal->Input.Connect(0, NormalWS);

		UMaterialExpressionCustom* Surface = Add<UMaterialExpressionCustom>(Material, -300, 0);
		Surface->Description = TEXT("SimCruise car surface");
		Surface->Code = SurfaceCode;
		Surface->OutputType = CMOT_Float3;
		Surface->Inputs.Reset();
		AddInput(Surface, TEXT("P"), LocalPos);
		AddInput(Surface, TEXT("NL"), NormalLocal);
		AddInput(Surface, TEXT("Base"), Base, 5);
		AddInput(Surface, TEXT("Surf"), Surf, 5);
		AddInput(Surface, TEXT("Det"), Det, 5);
		AddInput(Surface, TEXT("Flags"), Flags, 5);
		AddInput(Surface, TEXT("Ext"), Ext, 5);
		AddInput(Surface, TEXT("TBase"), BaseTex, 5);
		AddInput(Surface, TEXT("TORM"), OrmTex, 5);
		AddOutput(Surface, TEXT("Metallic"), CMOT_Float1);
		AddOutput(Surface, TEXT("Roughness"), CMOT_Float1);
		AddOutput(Surface, TEXT("ClearCoatAmount"), CMOT_Float1);
		AddOutput(Surface, TEXT("ClearCoatRough"), CMOT_Float1);
		AddOutput(Surface, TEXT("AO"), CMOT_Float1);
		AddOutput(Surface, TEXT("GTop"), CMOT_Float3);
		AddOutput(Surface, TEXT("GBot"), CMOT_Float3);
		AddOutput(Surface, TEXT("Alpha"), CMOT_Float1);
		Surface->RebuildOutputs();

		UMaterialExpressionTransform* TopWS = Add<UMaterialExpressionTransform>(Material, 0, 300);
		TopWS->TransformSourceType = TRANSFORMSOURCE_Local;
		TopWS->TransformType = TRANSFORM_World;
		TopWS->Input.Connect(6, Surface);
		UMaterialExpressionTransform* BotWS = Add<UMaterialExpressionTransform>(Material, 0, 450);
		BotWS->TransformSourceType = TRANSFORMSOURCE_Local;
		BotWS->TransformType = TRANSFORM_World;
		BotWS->Input.Connect(7, Surface);
		UMaterialExpressionTransform* MapWS = Add<UMaterialExpressionTransform>(Material, -600, 1100);
		MapWS->TransformSourceType = TRANSFORMSOURCE_Tangent;
		MapWS->TransformType = TRANSFORM_World;
		MapWS->Input.Connect(0, NormalTex);

		UMaterialExpressionCustom* Normals = Add<UMaterialExpressionCustom>(Material, 300, 300);
		Normals->Description = TEXT("SimCruise car normals");
		Normals->Code = NormalCode;
		Normals->OutputType = CMOT_Float3;
		Normals->Inputs.Reset();
		AddInput(Normals, TEXT("Nw"), NormalWS);
		AddInput(Normals, TEXT("Nm"), MapWS);
		AddInput(Normals, TEXT("UseN"), Flags, 2); // G channel
		AddInput(Normals, TEXT("GT"), TopWS);
		AddInput(Normals, TEXT("GB"), BotWS);
		AddOutput(Normals, TEXT("BottomNormal"), CMOT_Float3);
		Normals->RebuildOutputs();

		UMaterialExpressionCustom* EmissiveMix = Add<UMaterialExpressionCustom>(Material, 0, 750);
		EmissiveMix->Description = TEXT("SimCruise emissive");
		EmissiveMix->Code = TEXT("return Em.rgb * Em.a * lerp(float3(1, 1, 1), TE.rgb, saturate(Flags.w * 0 + UseE));");
		EmissiveMix->OutputType = CMOT_Float3;
		EmissiveMix->Inputs.Reset();
		AddInput(EmissiveMix, TEXT("Em"), Emissive, 5);
		AddInput(EmissiveMix, TEXT("TE"), EmissiveTex, 5);
		AddInput(EmissiveMix, TEXT("Flags"), Flags, 5);
		AddInput(EmissiveMix, TEXT("UseE"), Ext, 3); // Extra.b: use emissive texture
		EmissiveMix->RebuildOutputs();

		Data->BaseColor.Connect(0, Surface);
		Data->Metallic.Connect(1, Surface);
		Data->Roughness.Connect(2, Surface);
		Data->ClearCoat.Connect(3, Surface);
		Data->ClearCoatRoughness.Connect(4, Surface);
		Data->AmbientOcclusion.Connect(5, Surface);
		Data->Normal.Connect(0, Normals);
		Data->EmissiveColor.Connect(0, EmissiveMix);
		if (bMasked)
		{
			Data->OpacityMask.Connect(8, Surface);
		}
		UMaterialExpressionClearCoatNormalCustomOutput* Bottom = Add<UMaterialExpressionClearCoatNormalCustomOutput>(Material, 600, 450);
		Bottom->Input.Connect(1, Normals);

		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		return Material;
	}

	UMaterial* BuildGlassMaster()
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), MasterNames[2], RF_Transient | RF_Public);
		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_ThinTranslucent);
		Material->TranslucencyLightingMode = TLM_SurfacePerPixelLighting;
		Material->TwoSided = true;
		Material->SetUsageByFlag(MATUSAGE_StaticLighting, false);
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		UMaterialExpressionVectorParameter* Base = Vector(Material, TEXT("BaseColor"), FLinearColor(0.f, 0.f, 0.f, 1.f), 0);
		UMaterialExpressionVectorParameter* Tint = Vector(Material, TEXT("Transmittance"), FLinearColor(0.8f, 0.85f, 0.82f, 1.f), 150);
		UMaterialExpressionVectorParameter* GS = Vector(Material, TEXT("GlassSurface"), FLinearColor(0.02f, 0.f, 0.5f, 0.f), 300);
		UMaterialExpressionVectorParameter* Em = Vector(Material, TEXT("Emissive"), FLinearColor(0.f, 0.f, 0.f, 0.f), 450);
		UMaterialExpressionTextureCoordinate* UV = Add<UMaterialExpressionTextureCoordinate>(Material, -1200, 600);
		UMaterialExpressionTextureSampleParameter2D* BaseTex = TextureParam(Material, TEXT("BaseTexture"), TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"), SAMPLERTYPE_Color, 600);
		BaseTex->Coordinates.Connect(0, UV);
		UMaterialExpressionCustom* Glass = Add<UMaterialExpressionCustom>(Material, -300, 0);
		Glass->Description = TEXT("SimCruise glass");
		Glass->Code = GlassCode;
		Glass->OutputType = CMOT_Float3;
		Glass->Inputs.Reset();
		AddInput(Glass, TEXT("Base"), Base, 5);
		AddInput(Glass, TEXT("GS"), GS, 5);
		AddInput(Glass, TEXT("Em"), Em, 5);
		AddInput(Glass, TEXT("TBase"), BaseTex, 5);
		AddOutput(Glass, TEXT("Rough"), CMOT_Float1);
		AddOutput(Glass, TEXT("Coverage"), CMOT_Float1);
		AddOutput(Glass, TEXT("Spec"), CMOT_Float1);
		AddOutput(Glass, TEXT("Emis"), CMOT_Float3);
		Glass->RebuildOutputs();
		Data->BaseColor.Connect(0, Glass);
		Data->Roughness.Connect(1, Glass);
		Data->Opacity.Connect(2, Glass);
		Data->Specular.Connect(3, Glass);
		Data->EmissiveColor.Connect(4, Glass);
		UMaterialExpressionConstant* Zero = Add<UMaterialExpressionConstant>(Material, -300, 300);
		Zero->R = 0.f;
		Data->Metallic.Connect(0, Zero);
		UMaterialExpressionConstant* One = Add<UMaterialExpressionConstant>(Material, -300, 400);
		One->R = 1.f;
		UMaterialExpressionThinTranslucentMaterialOutput* Thin = Add<UMaterialExpressionThinTranslucentMaterialOutput>(Material, 300, 300);
		Thin->TransmittanceColor.Connect(0, Tint);
		Thin->SurfaceCoverage.Connect(0, One);
		Material->PreEditChange(nullptr);
		Material->PostEditChange();
		return Material;
	}
#endif

	FLinearColor Clamp01(const FLinearColor& C, float Lo, float Hi)
	{
		return FLinearColor(FMath::Clamp(C.R, Lo, Hi), FMath::Clamp(C.G, Lo, Hi), FMath::Clamp(C.B, Lo, Hi), C.A);
	}

	float Luminance(const FLinearColor& C)
	{
		return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B;
	}

	/** Scale an author colour so its luminance sits in a physically plausible band, keeping the hue. */
	FLinearColor ToAlbedo(const FLinearColor& C, float MinLum, float MaxLum)
	{
		const float L = Luminance(C);
		if (L < 1e-4f)
		{
			return FLinearColor(MinLum, MinLum, MinLum, C.A);
		}
		const float Target = FMath::Clamp(L, MinLum, MaxLum);
		FLinearColor Out = C * (Target / L);
		Out.A = C.A;
		return Clamp01(Out, 0.f, 0.95f);
	}
}

UMaterialInterface* SimCruiseCarMaterials::GetMaster(ESimCruiseCarMaster Master)
{
	using namespace SimCruiseCarMaterialsPrivate;
	const int32 Index = static_cast<int32>(Master);
	if (Masters[Index].IsValid())
	{
		return Masters[Index].Get();
	}
	// A saved copy (cooked games, or after SimCruise.SaveCarMaterials) wins.
	const FString Path = FString::Printf(TEXT("/Game/Vehicles/Materials/%s.%s"), MasterNames[Index], MasterNames[Index]);
	UMaterialInterface* Loaded = nullptr;
#if WITH_EDITOR
	// In the editor the generated graph is the source of truth: always build it,
	// so a code change is never hidden behind a stale saved asset.
	Loaded = Master == ESimCruiseCarMaster::Glass ? BuildGlassMaster() : BuildSurfaceMaster(Master == ESimCruiseCarMaster::Masked);
#else
	Loaded = LoadObject<UMaterialInterface>(nullptr, *Path);
	if (!Loaded)
	{
		UE_LOG(LogSimCruiseCarMaterials, Error, TEXT("Missing %s: run SimCruise.SaveCarMaterials in the editor and commit Content/Vehicles/Materials."), *Path);
		Loaded = UMaterial::GetDefaultMaterial(MD_Surface);
	}
#endif
	Masters[Index].Reset(Loaded);
	return Loaded;
}

ESimCruiseCarMaster SimCruiseCarMaterials::MasterFor(ESimCruiseCarMaterialRole Role, const FSimCruiseGltfMaterial* Author)
{
	using R = ESimCruiseCarMaterialRole;
	switch (Role)
	{
	case R::Glass: case R::GlassDark: case R::LampRear: case R::LampFront: case R::LampAmber:
		return ESimCruiseCarMaster::Glass;
	default:
		break;
	}
	if (Author && Author->AlphaMode == TEXT("MASK"))
	{
		return ESimCruiseCarMaster::Masked;
	}
	if (Author && Author->AlphaMode == TEXT("BLEND") && Role == R::Authored)
	{
		return ESimCruiseCarMaster::Glass;
	}
	return ESimCruiseCarMaster::Opaque;
}

UTexture2D* SimCruiseCarMaterials::LoadTexture(const FString& File, bool bSRGB, bool bNormalMap)
{
	using namespace SimCruiseCarMaterialsPrivate;
	const FString Key = File + (bSRGB ? TEXT("|srgb") : TEXT("|lin")) + (bNormalMap ? TEXT("|n") : TEXT(""));
	if (TStrongObjectPtr<UTexture2D>* Found = TextureCache.Find(Key))
	{
		return Found->Get();
	}
	UTexture2D* Texture = nullptr;
#if WITH_EDITOR
	FImage Image;
	if (FImageUtils::LoadImage(*File, Image))
	{
		Texture = Cast<UTexture2D>(FImageUtils::CreateTexture(ETextureClass::TwoD, Image, GetTransientPackage(),
			FPaths::GetBaseFilename(File), RF_Transient, false));
		if (Texture)
		{
			Texture->SRGB = bSRGB;
			Texture->NeverStream = true;
			if (bNormalMap)
			{
				Texture->CompressionSettings = TC_Normalmap;
				// glTF normal maps are +Y up (OpenGL); Unreal expects DirectX green.
				Texture->bFlipGreenChannel = true;
				Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
			}
			else if (!bSRGB)
			{
				Texture->CompressionSettings = TC_Masks;
			}
			Texture->PostEditChange();
		}
	}
#else
	// Cooked games: an uncompressed transient texture. Normal maps get the same
	// green flip (glTF +Y to DirectX) the editor path asks the texture build for.
	FImage Image;
	if (FImageUtils::LoadImage(*File, Image))
	{
		Image.ChangeFormat(ERawImageFormat::BGRA8, bSRGB ? EGammaSpace::sRGB : EGammaSpace::Linear);
		if (bNormalMap)
		{
			for (FColor& Texel : Image.AsBGRA8())
			{
				Texel.G = 255 - Texel.G;
			}
		}
		Texture = FImageUtils::CreateTexture2DFromImage(Image);
		if (Texture)
		{
			Texture->SRGB = bSRGB;
			Texture->UpdateResource();
		}
	}
#endif
	if (!Texture)
	{
		UE_LOG(LogSimCruiseCarMaterials, Warning, TEXT("Could not load texture %s"), *File);
		return nullptr;
	}
	TextureCache.Add(Key, TStrongObjectPtr<UTexture2D>(Texture));
	return Texture;
}

UMaterialInstanceDynamic* SimCruiseCarMaterials::MakeInstance(UObject* Outer, ESimCruiseCarMaterialRole Role,
	const FSimCruiseGltfModel& Model, int32 MaterialIndex, const FSimCruiseCarPresentationProfile& Profile)
{
	using namespace SimCruiseCarMaterialsPrivate;
	using R = ESimCruiseCarMaterialRole;
	const FSimCruiseGltfMaterial* Author = Model.Materials.IsValidIndex(MaterialIndex) ? &Model.Materials[MaterialIndex] : nullptr;
	const ESimCruiseCarMaster Master = MasterFor(Role, Author);
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(GetMaster(Master), Outer);
	if (!MID)
	{
		return nullptr;
	}
	const FLinearColor AuthorBase = Author ? Author->BaseColorFactor : FLinearColor(0.5f, 0.5f, 0.5f, 1.f);
	auto Image = [&](int32 ImageIndex) -> FString
	{
		return Model.ImageFiles.IsValidIndex(ImageIndex) ? Model.ImageFiles[ImageIndex] : FString();
	};
	const FString BaseFile = Author ? Image(Author->BaseColorImage) : FString();
	const FString NormalFile = Author ? Image(Author->NormalImage) : FString();
	const FString OrmFile = Author ? Image(Author->MetallicRoughnessImage) : FString();
	const FString OcclusionFile = Author ? Image(Author->OcclusionImage) : FString();
	const FString EmissiveFile = Author ? Image(Author->EmissiveImage) : FString();
	const FLinearColor* Tint = Profile.RoleTints.Find(Role);

	if (Master == ESimCruiseCarMaster::Glass)
	{
		// Transmittance at normal incidence; reflection is Fresnel (F0 0.04).
		FLinearColor Transmit(0.82f, 0.88f, 0.85f);   // laminated, faint green
		FLinearColor Surface(0.02f, 0.f, 0.5f, 0.f);   // roughness, top coverage, specular, use texture
		FLinearColor BaseColor(0.f, 0.f, 0.f, 1.f);
		switch (Role)
		{
		case R::GlassDark: Transmit = FLinearColor(0.12f, 0.12f, 0.12f); break;
		case R::LampRear: Transmit = FLinearColor(0.55f, 0.015f, 0.01f); Surface.R = 0.03f; BaseColor = FLinearColor(0.18f, 0.005f, 0.004f); break;
		case R::LampAmber: Transmit = FLinearColor(0.9f, 0.38f, 0.02f); Surface.R = 0.03f; BaseColor = FLinearColor(0.3f, 0.1f, 0.0f); break;
		case R::LampFront: Transmit = FLinearColor(0.93f, 0.93f, 0.93f); Surface.R = 0.01f; break;
		case R::Authored:
			if (Author)
			{
				// Author glass: its alpha is a coverage guess; keep its hue as transmittance.
				const FLinearColor C = AuthorBase;
				Transmit = Clamp01(FLinearColor(FMath::Lerp(1.f, C.R, C.A), FMath::Lerp(1.f, C.G, C.A), FMath::Lerp(1.f, C.B, C.A)), 0.05f, 0.95f);
			}
			break;
		default: break;
		}
		if (Tint)
		{
			Transmit = *Tint;
		}
		if (!BaseFile.IsEmpty() && Role != R::Glass && Role != R::GlassDark)
		{
			if (UTexture2D* Tex = LoadTexture(BaseFile, true, false))
			{
				MID->SetTextureParameterValue(TEXT("BaseTexture"), Tex);
				Surface.A = 1.f;
			}
		}
		MID->SetVectorParameterValue(TEXT("Transmittance"), Transmit);
		MID->SetVectorParameterValue(TEXT("GlassSurface"), Surface);
		MID->SetVectorParameterValue(TEXT("BaseColor"), BaseColor);
		MID->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(0, 0, 0, 0));
		return MID;
	}

	// Opaque / masked surfaces: (metallic, roughness, clear coat, clear-coat roughness), (kind, flake, orange peel, strength).
	FLinearColor Base = AuthorBase;
	FLinearColor Surface(Author ? Author->MetallicFactor : 0.f, Author ? Author->RoughnessFactor : 0.6f, 0.f, 0.05f);
	FLinearColor Detail(KindPlain, 0.f, 0.f, 1.f);
	FLinearColor Extra(0.f, 0.f, 0.f, 0.f);
	FLinearColor Emissive(0.f, 0.f, 0.f, 0.f);
	bool bUseAuthorTextures = true;
	switch (Role)
	{
	case R::Paint:
	{
		const FSimCruisePaintFinish& P = Profile.Paint;
		const bool bAtlas = !BaseFile.IsEmpty();
		Base = bAtlas ? FLinearColor::White : P.BaseColor;
		Surface = FLinearColor(P.Metallic, P.BaseRoughness, 1.f, P.ClearCoatRoughness);
		Detail = FLinearColor(KindPaint, P.Flake, P.OrangePeel, 1.f);
		break;
	}
	case R::WheelAlloy:
		Base = FLinearColor(0.56f, 0.57f, 0.58f);
		Surface = FLinearColor(0.85f, 0.32f, 1.f, 0.06f);
		Detail = FLinearColor(KindPaint, 0.5f, 0.15f, 1.f);
		bUseAuthorTextures = false;
		break;
	case R::Chrome:
		Base = FLinearColor(0.56f, 0.57f, 0.58f);
		Surface = FLinearColor(1.f, 0.05f, 0.f, 0.05f);
		Detail.R = KindChrome;
		bUseAuthorTextures = false;
		break;
	case R::BareMetal:
		Base = FLinearColor(0.80f, 0.81f, 0.82f);
		Surface = FLinearColor(1.f, 0.36f, 0.f, 0.05f);
		Detail.R = KindBrushed;
		bUseAuthorTextures = false;
		break;
	case R::Tyre:
		Base = FLinearColor(0.032f, 0.032f, 0.033f);
		Surface = FLinearColor(0.f, 0.84f, 0.f, 0.05f);
		Detail.R = KindTyre;
		Extra.R = 1.f; // dust
		break;
	case R::BrakeDisc:
		Base = FLinearColor(0.40f, 0.39f, 0.38f);
		Surface = FLinearColor(1.f, 0.36f, 0.f, 0.05f);
		Detail.R = KindDisc;
		bUseAuthorTextures = false;
		break;
	case R::BrakeCaliper:
		Base = ToAlbedo(AuthorBase, 0.04f, 0.3f);
		Surface = FLinearColor(0.35f, 0.55f, 0.f, 0.05f);
		Detail.R = KindCaliper;
		break;
	case R::PlasticGrained:
		Base = ToAlbedo(AuthorBase, 0.028f, 0.08f);
		Surface = FLinearColor(0.f, 0.6f, 0.f, 0.05f);
		Detail.R = KindGrainedPlastic;
		break;
	case R::TrimGloss:
		Base = FLinearColor(0.012f, 0.012f, 0.013f);
		Surface = FLinearColor(0.f, 0.3f, 1.f, 0.05f);
		Detail = FLinearColor(KindPaint, 0.f, 0.3f, 1.f);
		break;
	case R::Rubber:
		Base = FLinearColor(0.025f, 0.025f, 0.026f);
		Surface = FLinearColor(0.f, 0.82f, 0.f, 0.05f);
		Detail.R = KindRubber;
		break;
	case R::MirrorGlass:
		Base = FLinearColor(0.90f, 0.91f, 0.92f);
		Surface = FLinearColor(1.f, 0.0f, 0.f, 0.05f);
		bUseAuthorTextures = false;
		break;
	case R::InteriorPlastic:
		// Grained dash plastic is near-matte (roughness about 0.85): at 0.66 the sky's reflection
		// turned the near-black dash light grey in the sun (a screenshot of the Miata, 2026-11-14).
		Base = ToAlbedo(AuthorBase, 0.025f, 0.12f);
		Surface = FLinearColor(0.f, 0.85f, 0.f, 0.05f);
		Detail.R = KindInteriorPlastic;
		break;
	case R::Cloth:
		Base = ToAlbedo(AuthorBase, 0.03f, 0.3f);
		Surface = FLinearColor(0.f, 0.95f, 0.f, 0.05f);
		Detail.R = KindCloth;
		break;
	case R::Leather:
		Base = BaseFile.IsEmpty() ? ToAlbedo(AuthorBase, 0.03f, 0.5f) : FLinearColor::White;
		Surface = FLinearColor(0.f, 0.72f, 0.f, 0.05f); // worn wheel and seat leather: satin, not glossy
		Detail.R = KindLeather;
		break;
	case R::Carpet:
		Base = BaseFile.IsEmpty() ? ToAlbedo(AuthorBase, 0.02f, 0.2f) : FLinearColor::White;
		Surface = FLinearColor(0.f, 1.f, 0.f, 0.05f);
		Detail.R = KindCarpet;
		break;
	case R::Gauge:
		Base = BaseFile.IsEmpty() ? ToAlbedo(AuthorBase, 0.02f, 0.85f) : FLinearColor::White;
		Surface = FLinearColor(0.f, 0.35f, 0.f, 0.05f);
		Emissive = FLinearColor(1.f, 1.f, 1.f, 25.f); // faint daytime backlight, nits
		break;
	case R::Plate:
		Base = ToAlbedo(AuthorBase, 0.02f, 0.8f);
		Surface = FLinearColor(0.f, 0.45f, 0.f, 0.05f);
		break;
	case R::Underbody:
		Base = ToAlbedo(AuthorBase, 0.03f, 0.15f);
		Surface = FLinearColor(0.3f, 0.7f, 0.f, 0.05f);
		Detail.R = KindUnderbody;
		break;
	case R::Authored:
	default:
		// Author factors, kept physically plausible (no albedo below 2 % or above 90 %).
		Base = Surface.R > 0.5f ? AuthorBase : ToAlbedo(AuthorBase, 0.02f, 0.9f);
		if (Author && Author->ClearcoatFactor > 0.f)
		{
			Surface.B = Author->ClearcoatFactor;
			Surface.A = FMath::Max(0.02f, Author->ClearcoatRoughnessFactor);
		}
		break;
	}
	if (Tint)
	{
		Base = FLinearColor(Tint->R, Tint->G, Tint->B, Base.A);
	}
	FLinearColor Flags(0.f, 0.f, 0.f, 0.f);
	if (bUseAuthorTextures && !BaseFile.IsEmpty())
	{
		if (UTexture2D* Tex = LoadTexture(BaseFile, true, false))
		{
			MID->SetTextureParameterValue(TEXT("BaseTexture"), Tex);
			Flags.R = 1.f;
			if (Role == R::Paint)
			{
				Base = FLinearColor::White;
			}
		}
	}
	if (bUseAuthorTextures && !NormalFile.IsEmpty())
	{
		if (UTexture2D* Tex = LoadTexture(NormalFile, false, true))
		{
			MID->SetTextureParameterValue(TEXT("NormalTexture"), Tex);
			Flags.G = 1.f;
		}
	}
	if (bUseAuthorTextures && !OrmFile.IsEmpty() && Role == R::Authored)
	{
		if (UTexture2D* Tex = LoadTexture(OrmFile, false, false))
		{
			MID->SetTextureParameterValue(TEXT("MetallicRoughnessTexture"), Tex);
			Flags.B = 1.f;
			Flags.A = OcclusionFile == OrmFile ? 1.f : 0.f;
		}
	}
	if (!EmissiveFile.IsEmpty() && Author && Author->EmissiveFactor.GetMax() > 0.f)
	{
		if (UTexture2D* Tex = LoadTexture(EmissiveFile, true, false))
		{
			MID->SetTextureParameterValue(TEXT("EmissiveTexture"), Tex);
			Extra.B = 1.f;
			if (Emissive.A <= 0.f)
			{
				Emissive = FLinearColor(1.f, 1.f, 1.f, 25.f);
			}
		}
	}
	MID->SetVectorParameterValue(TEXT("BaseColor"), Base);
	MID->SetVectorParameterValue(TEXT("Surface"), Surface);
	MID->SetVectorParameterValue(TEXT("Detail"), Detail);
	MID->SetVectorParameterValue(TEXT("TextureFlags"), Flags);
	MID->SetVectorParameterValue(TEXT("Extra"), Extra);
	MID->SetVectorParameterValue(TEXT("Emissive"), Emissive);
	return MID;
}

void SimCruiseCarMaterials::SetLampLevel(UMaterialInstanceDynamic* Lamp, ESimCruiseCarMaterialRole Role, float Level)
{
	if (!Lamp)
	{
		return;
	}
	using R = ESimCruiseCarMaterialRole;
	// Luminance in cd/m2 (Unreal emissive 1 = 1 nit): tail lamps ~ 300,
	// brake lamps ~ 6000 (filament behind a red lens, SAE J586 intensity over
	// the lamp area), headlamps much brighter.
	// Signal red (SAE J578: x >= 0.65, y <= 0.33) lies outside the sRGB gamut;
	// the nearest displayable colour is almost pure linear red. More green
	// and blue tone-map a bright lens to salmon instead of red.
	FLinearColor Colour(1.f, 0.012f, 0.004f);
	float Nits = 0.f;
	if (Role == R::LampRear)
	{
		Nits = Level <= 0.f ? 0.f : (Level < 1.5f ? 300.f : 6000.f);
	}
	else if (Role == R::LampAmber)
	{
		Colour = FLinearColor(1.f, 0.45f, 0.02f);
		Nits = Level > 0.f ? 8000.f : 0.f;
	}
	else if (Role == R::LampFront)
	{
		Colour = FLinearColor(1.f, 0.95f, 0.88f);
		Nits = Level > 0.f ? 20000.f * Level : 0.f;
	}
	Lamp->SetVectorParameterValue(TEXT("Emissive"), FLinearColor(Colour.R, Colour.G, Colour.B, Nits));
	FLinearColor Surface;
	if (Lamp->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("GlassSurface")), Surface))
	{
		// A lit lamp's glow covers the lens; off, the lens is clear coloured plastic.
		Surface.G = Nits > 0.f ? FMath::Clamp(Nits / 6000.f, 0.15f, 0.85f) : 0.f;
		Lamp->SetVectorParameterValue(TEXT("GlassSurface"), Surface);
	}
}

#if WITH_EDITOR
void SimCruiseCarMaterials::PrepareSavedCopy(UMaterial* Saved)
{
	// A copy of a transient material keeps RF_Transient on its expressions and editor-only
	// data, which then never reach the file (the cook asserts on the missing EditorOnly).
	Saved->ClearFlags(RF_Transient);
	Saved->SetFlags(RF_Public | RF_Standalone);
	ForEachObjectWithOuter(Saved, [](UObject* Inner) { Inner->ClearFlags(RF_Transient); }, /*bIncludeNestedObjects=*/ true);
}

bool SimCruiseCarMaterials::SaveMasters(FString& OutReport)
{
	using namespace SimCruiseCarMaterialsPrivate;
	bool bOk = true;
	// The mask samplers' default (a transient texture in the editor) is saved alongside, so
	// the cooked masters reference an asset rather than /Engine/Transient.
	UTexture* SavedWhite = nullptr;
	{
		const FString PackageName = TEXT("/Game/Vehicles/Materials/T_SimCruiseLinearWhite");
		UPackage* Package = CreatePackage(*PackageName);
		FImage Image(4, 4, ERawImageFormat::BGRA8, EGammaSpace::Linear);
		FMemory::Memset(Image.RawData.GetData(), 0xFF, Image.RawData.Num());
		SavedWhite = FImageUtils::CreateTexture(ETextureClass::TwoD, Image, Package, TEXT("T_SimCruiseLinearWhite"), RF_Public | RF_Standalone, false);
		SavedWhite->SRGB = false;
		SavedWhite->CompressionSettings = TC_Masks;
		SavedWhite->PostEditChange();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		const bool bSaved = UPackage::SavePackage(Package, SavedWhite, *File, Args);
		OutReport += FString::Printf(TEXT("%s %s\n"), bSaved ? TEXT("saved") : TEXT("FAILED"), *File);
		bOk &= bSaved;
	}
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const FString PackageName = FString::Printf(TEXT("/Game/Vehicles/Materials/%s"), MasterNames[Index]);
		UPackage* Package = CreatePackage(*PackageName);
		UMaterial* Built = Index == 2 ? BuildGlassMaster() : BuildSurfaceMaster(Index == 1);
		UMaterial* Saved = DuplicateObject<UMaterial>(Built, Package, MasterNames[Index]);
		PrepareSavedCopy(Saved);
		for (UMaterialExpression* Expression : Saved->GetExpressions())
		{
			UMaterialExpressionTextureBase* Sampler = Cast<UMaterialExpressionTextureBase>(Expression);
			if (Sampler && Sampler->Texture == LinearWhite())
			{
				Sampler->Texture = SavedWhite;
			}
		}
		Saved->PostEditChange();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		const bool bSaved = UPackage::SavePackage(Package, Saved, *File, Args);
		OutReport += FString::Printf(TEXT("%s %s\n"), bSaved ? TEXT("saved") : TEXT("FAILED"), *File);
		bOk &= bSaved;
	}
	return bOk;
}
#endif
