// Copyright SimCruise contributors. All Rights Reserved.

#include "Presentation/SimCruiseLowBeamPattern.h"

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITOR
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#endif

namespace
{
	// One formula for the shader and the C++ check (SimCruise.Presentation.LowBeamPattern).
	// Unreal draws a spot light's light function into the light-function atlas in light
	// space: the only coordinate it gives is the projective UV, (tan of the angle across
	// the beam) / (2 tan of the outer cone) + 0.5, u up and v across. UpSign and RightSign
	// fix the axes; AimDown is the lamp's aim below the car's horizon; Curb is +1 when the
	// curb side is the lamp's right.
	const TCHAR* PatternCode = TEXT(R"HLSL(
float2 t = (UV - 0.5) * 2.0 * TanOuter;
float elev = degrees(atan(t.x * UpSign)) - AimDown;
float az = degrees(atan(t.y * RightSign)) * Curb;
float cut = az < 0.0 ? -0.57 : min(-0.57 + az * 0.2679, 1.5);
float below = saturate((cut - elev) / 0.25 + 0.5);
float hot = exp(-pow((elev + 1.4) / 2.4, 2.0)) * exp(-pow((az - 2.5) / 13.0, 2.0));
float fore = smoothstep(-22.0, -3.0, elev);
float pattern = below * (0.22 + 0.78 * hot) * lerp(0.3, 1.0, fore);
pattern += 0.015 * (1.0 - below) * exp(-pow(elev / 6.0, 2.0));
return float3(pattern, pattern, pattern);
)HLSL");
}

namespace SimCruiseLowBeamPattern
{
	FString AssetPath()
	{
		return TEXT("/Game/Vehicle/Materials/LF_SimCruiseLowBeam.LF_SimCruiseLowBeam");
	}

	double Relative(double ElevationDeg, double CurbwardAzimuthDeg)
	{
		const double Elev = ElevationDeg, Az = CurbwardAzimuthDeg;
		const double Cut = Az < 0.0 ? -0.57 : FMath::Min(-0.57 + Az * 0.2679, 1.5);
		const double Below = FMath::Clamp((Cut - Elev) / 0.25 + 0.5, 0.0, 1.0);
		const double Hot = FMath::Exp(-FMath::Square((Elev + 1.4) / 2.4)) * FMath::Exp(-FMath::Square((Az - 2.5) / 13.0));
		const double T = FMath::Clamp((Elev + 22.0) / 19.0, 0.0, 1.0);
		const double Fore = T * T * (3.0 - 2.0 * T);
		return Below * (0.22 + 0.78 * Hot) * FMath::Lerp(0.3, 1.0, Fore) + 0.015 * (1.0 - Below) * FMath::Exp(-FMath::Square(Elev / 6.0));
	}

	UMaterialInterface* Get()
	{
		static TStrongObjectPtr<UMaterialInterface> Cached;
		if (Cached.IsValid())
		{
			return Cached.Get();
		}
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *AssetPath(), nullptr, LOAD_NoWarn | LOAD_Quiet);
#if WITH_EDITOR
		if (!Material && FApp::CanEverRender())
		{
			UMaterial* Built = NewObject<UMaterial>(GetTransientPackage(), TEXT("LF_SimCruiseLowBeam"), RF_Transient);
			Built->MaterialDomain = MD_LightFunction;
			UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Built);
			Built->GetExpressionCollection().AddExpression(Custom);
			Custom->OutputType = CMOT_Float3;
			Custom->Description = TEXT("SimCruise low beam");
			Custom->Code = PatternCode;
			Custom->Inputs.Reset();
			auto Input = [&](const TCHAR* Name, UMaterialExpression* Expression)
			{
				FCustomInput In;
				In.InputName = Name;
				In.Input.Connect(0, Expression);
				Custom->Inputs.Add(In);
			};
			auto Scalar = [&](const TCHAR* Name, float Value)
			{
				UMaterialExpressionScalarParameter* P = NewObject<UMaterialExpressionScalarParameter>(Built);
				Built->GetExpressionCollection().AddExpression(P);
				P->ParameterName = Name;
				P->DefaultValue = Value;
				return P;
			};
			UMaterialExpressionTextureCoordinate* UV = NewObject<UMaterialExpressionTextureCoordinate>(Built);
			Built->GetExpressionCollection().AddExpression(UV);
			Input(TEXT("UV"), UV);
			Input(TEXT("TanOuter"), Scalar(TEXT("TanOuter"), 0.6249f));
			Input(TEXT("AimDown"), Scalar(TEXT("AimDown"), 1.5f));
			Input(TEXT("Curb"), Scalar(TEXT("Curb"), 1.f));
			Input(TEXT("UpSign"), Scalar(TEXT("UpSign"), 1.f));
			Input(TEXT("RightSign"), Scalar(TEXT("RightSign"), 1.f));
			Custom->RebuildOutputs();
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
