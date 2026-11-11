// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseCarMirror.h"

#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#endif

namespace
{
	// V: from the surface toward the eye; P: the pixel (world, cm). C0..C2: the mirrors'
	// centres (world, cm; far away for a mirror the car lacks); N0..N2: the normal each
	// mirror is set to (aimed from the driver's seat); K0..K2: 1 / radius of a convex glass
	// (1/cm), 0 flat.
	// The pixel belongs to the nearest mirror; the glass is that plane (or sphere), not the
	// model's own surface, so the view is the one a driver who set the mirrors sees.
	// Fwd/Right/Up: the capture camera's axes; TanHalf: tan of half its horizontal field of
	// view; Aspect: width / height. The silvering reflects about 85 %, without colour.
	const TCHAR* MirrorCode = TEXT(R"HLSL(
float3 n = normalize(N);
if (dot(n, V) < 0.0) n = -n;
float best = 1e7;
float3 cs[3] = { C0.xyz, C1.xyz, C2.xyz };
float3 ns[3] = { N0.xyz, N1.xyz, N2.xyz };
float ks[3] = { K0, K1, K2 };
for (int i = 0; i < 3; ++i)
{
	float d = length(P - cs[i]);
	if (d < best)
	{
		best = d;
		n = normalize(ns[i] + (P - cs[i]) * ks[i]);
	}
}
float3 r = reflect(-normalize(V), n);
float z = dot(r, Fwd.xyz);
float3 col = HasCapture > 0.5 ? float3(0, 0, 0) : Fallback.rgb;
if (HasCapture > 0.5 && z > 0.05)
{
	float2 uv = float2(dot(r, Right.xyz), dot(r, Up.xyz)) / (z * TanHalf);
	uv = float2(0.5 + 0.5 * uv.x, 0.5 - 0.5 * uv.y * Aspect);
	if (all(uv > 0.0) && all(uv < 1.0))
	{
		col = Texture2DSampleLevel(Tex, TexSampler, uv, 0).rgb;
	}
}
return col * 0.85;
)HLSL");
}

namespace SimCruiseCarMirror
{
	UMaterialInterface* Get()
	{
		static TStrongObjectPtr<UMaterialInterface> Cached;
		if (Cached.IsValid())
		{
			return Cached.Get();
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Vehicle/Materials/M_SimCruiseCarMirror.M_SimCruiseCarMirror"), nullptr, LOAD_NoWarn | LOAD_Quiet);
#if WITH_EDITOR
		if (!Material && FApp::CanEverRender())
		{
			UMaterial* Built = NewObject<UMaterial>(GetTransientPackage(), TEXT("M_SimCruiseCarMirror"), RF_Transient);
			// Mirror glass is a section of Nanite body and cabin parts.
			Built->SetUsageByFlag(MATUSAGE_Nanite, true);
			UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Built);
			Built->GetExpressionCollection().AddExpression(Custom);
			Custom->OutputType = CMOT_Float3;
			Custom->Description = TEXT("SimCruise car mirror");
			Custom->Code = MirrorCode;
			Custom->Inputs.Reset();
			auto Add = [&](auto* Expression) { Built->GetExpressionCollection().AddExpression(Expression); return Expression; };
			auto Input = [&](const TCHAR* Name, UMaterialExpression* Expression)
			{
				FCustomInput In;
				In.InputName = Name;
				In.Input.Connect(0, Expression);
				Custom->Inputs.Add(In);
			};
			auto Scalar = [&](const TCHAR* Name, float Value)
			{
				UMaterialExpressionScalarParameter* P = Add(NewObject<UMaterialExpressionScalarParameter>(Built));
				P->ParameterName = Name;
				P->DefaultValue = Value;
				return P;
			};
			auto Vector = [&](const TCHAR* Name, const FLinearColor& Value)
			{
				UMaterialExpressionVectorParameter* P = Add(NewObject<UMaterialExpressionVectorParameter>(Built));
				P->ParameterName = Name;
				P->DefaultValue = Value;
				return P;
			};
			UMaterialExpressionTextureObjectParameter* Tex = Add(NewObject<UMaterialExpressionTextureObjectParameter>(Built));
			Tex->ParameterName = TEXT("Capture");
			Tex->Texture = LoadObject<UTexture>(nullptr, TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));
			Input(TEXT("Tex"), Tex);
			Input(TEXT("V"), Add(NewObject<UMaterialExpressionCameraVectorWS>(Built)));
			Input(TEXT("N"), Add(NewObject<UMaterialExpressionVertexNormalWS>(Built)));
			Input(TEXT("P"), Add(NewObject<UMaterialExpressionWorldPosition>(Built)));
			for (int32 I = 0; I < 3; ++I)
			{
				Input(*FString::Printf(TEXT("C%d"), I), Vector(*FString::Printf(TEXT("C%d"), I), FLinearColor(1e9f, 1e9f, 1e9f, 0)));
				Input(*FString::Printf(TEXT("N%d"), I), Vector(*FString::Printf(TEXT("N%d"), I), FLinearColor(-1, 0, 0, 0)));
				Input(*FString::Printf(TEXT("K%d"), I), Scalar(*FString::Printf(TEXT("K%d"), I), 0.f));
			}
			Input(TEXT("Fwd"), Vector(TEXT("Fwd"), FLinearColor(-1, 0, 0, 0)));
			Input(TEXT("Right"), Vector(TEXT("Right"), FLinearColor(0, -1, 0, 0)));
			Input(TEXT("Up"), Vector(TEXT("Up"), FLinearColor(0, 0, 1, 0)));
			Input(TEXT("TanHalf"), Scalar(TEXT("TanHalf"), 1.73f));
			Input(TEXT("Aspect"), Scalar(TEXT("Aspect"), 2.f));
			Input(TEXT("HasCapture"), Scalar(TEXT("HasCapture"), 0.f));
			Input(TEXT("Fallback"), Vector(TEXT("Fallback"), FLinearColor(300.f, 330.f, 360.f, 0.f)));
			Custom->RebuildOutputs();
			// The reflection is light leaving the glass, independent of the light on it.
			Built->GetEditorOnlyData()->BaseColor.Connect(0, Scalar(TEXT("Black"), 0.f));
			Built->GetEditorOnlyData()->Roughness.Connect(0, Scalar(TEXT("Roughness"), 1.f));
			Built->GetEditorOnlyData()->Specular.Connect(0, Scalar(TEXT("Specular"), 0.f));
			Built->GetEditorOnlyData()->EmissiveColor.Connect(0, Custom);
			Built->PreEditChange(nullptr);
			Built->PostEditChange();
			Material = Built;
		}
#endif
		if (Material)
		{
			Cached.Reset(Material);
		}
		return Material;
	}
}
