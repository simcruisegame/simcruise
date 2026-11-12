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
#include "World/SimCruiseTileDetail.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#if WITH_EDITOR
#include "StaticMeshCompiler.h"
#endif

#if WITH_EDITOR
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Engine/Font.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionWorldPosition.h"
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
		case ESimCruiseTileLayer::Props: return TEXT("Props");
		case ESimCruiseTileLayer::Foliage: return TEXT("Foliage");
		case ESimCruiseTileLayer::Signs: return TEXT("Signs");
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
// A mapped park or meadow is turf only where the photograph is green: a tribal park in the
// desert, dry rangeland or a winter field keeps the colour the photograph shows.
float green = saturate((photo.g - max(photo.r, photo.b)) * 25.0 + 0.6);
float veg = mask.r * (Cell.z > 0.5 ? green : 1.0);
float sand = mask.g * (1.0 - veg);
float water = mask.a;
float paved = mask.b * (1.0 - water);
float bare = saturate(1.0 - veg - sand - paved);
float3 classColor = float3(0.15, 0.14, 0.11) * bare + float3(0.052, 0.088, 0.026) * veg + float3(0.50, 0.45, 0.35) * sand + float3(0.27, 0.26, 0.24) * paved;
// The photograph is taken from above: white roofs, sand and glare. Ground seen from the
// street is darker (grass ~0.1, concrete ~0.3, sand ~0.35 albedo), so highlights compress.
float3 shot = photo * AerialGain;
// Blue and violet casts in a city's photograph are tower shadows and haze, not ground:
// bare ground, pavement and sidewalks are warm or neutral, so they keep only their tone.
float cast = saturate((shot.b - shot.r) * 25.0);
shot = lerp(shot, dot(shot, W) * float3(1.03, 1.0, 0.95), cast * 0.9);
float sl = max(dot(shot, W), 0.0001);
// Under a closed canopy the photograph is dark crowns; the ground there is leaf litter and
// needles, brown and shaded, not lawn.
float forest = veg * saturate((0.075 - sl) / 0.035);
classColor = lerp(classColor, float3(0.062, 0.052, 0.036), forest);
float target = sl < 0.16 ? sl : 0.16 + (sl - 0.16) * 0.4;
shot *= target / sl;
// The zoom-14 photograph is about 10 m a pixel: on lawns and sand its blur would be the
// ground itself. There the land-cover colour (St. Augustine turf, Miami Beach's quartz and
// shell sand) leads and the photograph only shades it.
float classL = max(dot(classColor, W), 0.0001);
float3 led = classColor * lerp(1.0, clamp(sl / classL, 0.6, 1.6), 0.35);
float lead = saturate(0.8 * veg + 0.7 * sand);
float3 baseColor = Cell.z > 0.5 ? lerp(shot, led, lead) : classColor;
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
float fine = lerp(1.0, a, 0.45);
float3 col = Tint.rgb * fine * v;
float3 gravel = float3(0.26, 0.23, 0.19) * lerp(1.0, a, 1.0);
col = lerp(col, gravel, VC.r);
Roughness = saturate(0.80 + 0.10 * (1.0 - a) + 0.05 * VC.r);
float3 g = float3(-(ax - a), -(ay - a), 0.0) * Bump;
Nrm = normalize(VN + g);
// Rain (Wet 0..1, the environment's road water, from the live weather): water fills the
// pores and darkens the stone (dark asphalt to about 0.6 of its dry albedo, Lekner and Dorf
// 1988), the surface turns glossy, and on a soaked road water stands in the low spots.
float damp = saturate(Wet * 2.0);
float low = macro * lerp(1.0, macro2, 0.5);
float puddle = saturate((Wet - 0.4) * 2.5) * smoothstep(0.93, 0.82, low);
col *= lerp(1.0, 0.6, damp) * lerp(1.0, 0.85, puddle);
Roughness = lerp(lerp(Roughness, 0.35, damp), 0.04, puddle);
Nrm = normalize(lerp(VN + g, VN, puddle));
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
// The live sea (Live/SimCruiseLiveWorldSubsystem): the swell runs along WaveDir and its
// slopes grow with the measured wave height (WaveScale 1 = the 0.3 m of a calm Miami day).
float2 wd = normalize(WaveDir.xy + float2(1e-4, 0.0));
float2x2 turn = float2x2(wd.x, -wd.y, wd.y, wd.x);
const float2 D[6] = { float2(0.91, 0.41), float2(0.6, -0.8), float2(-0.3, 0.95), float2(0.99, -0.12), float2(-0.7, -0.71), float2(0.2, 0.98) };
const float L[6] = { 9.0, 5.3, 3.1, 1.7, 1.1, 0.63 };
const float A[6] = { 0.05, 0.03, 0.017, 0.009, 0.005, 0.003 };
for (int i = 0; i < 6; i++)
{
	float k = 6.2831853 / L[i];
	float w = sqrt(9.81 * k);
	float2 d = mul(turn, D[i]);
	float ph = dot(d, p) * k - w * Time;
	grad += d * (A[i] * WaveScale * k * cos(ph));
}
Nrm = normalize(float3(-grad.x, -grad.y, 1.0));
Roughness = 0.04;
return lerp(float3(0.010, 0.040, 0.045), float3(0.012, 0.055, 0.055), VC.r);
)HLSL");

		// Buildings, street furniture and palm trunks: one shader, branching on the surface
		// kind in UV1.x (SimCruiseSurfaceKind); UV0 is (metres along the face, metres above
		// the building's ground). Windows, shopfronts, stucco and neon are drawn here, not modelled.
		const TCHAR* BuildingCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
float kind = round(UV1.x);
// The seed is constant on a building, but interpolation leaves it a few 1e-7 off across a
// triangle, and the hashes below multiply it by about 1e7: quantised, every pixel of a
// window draws the same random number (unquantised, lit windows speckled at night).
float seed = floor(UV1.y * 4096.0 + 0.5) / 4096.0;
float along = UV0.x;
float h = UV0.y;
float meanL = max(dot(GrainMean.rgb, W), 0.001);
float2 m = LP.xy * 0.01 + Anchor.xy;
float vertical = 1.0 - saturate(abs(VN.z) * 2.0 - 0.5);
float2 tuv = lerp(m, float2(along + seed * 37.0, h), vertical);
float grain = dot(Texture2DSample(Grain, GrainSampler, tuv / 1.3).rgb, W) / meanL;
float stain = dot(Texture2DSample(Grain, GrainSampler, float2(along * 0.11, h * 0.035) + seed * 5.3).rgb, W) / meanL;
float3 stucco = VC.rgb * lerp(1.0, grain, 0.09) * lerp(1.0, stain, 0.12);
stucco *= lerp(0.84, 1.0, saturate(h / 0.8));
float3 col = stucco;
float rough = 0.88;
float metal = 0.0;
float3 emis = float3(0.0, 0.0, 0.0);
// The window a room is drawn behind, set by the facade branches (rPane 0: none).
float rPane = 0.0, rx = 0.0, ry = 0.0, rBay = 1.0, rX0 = 0.0, rW = 1.0, rY0 = 0.0, rY1 = 1.0;
float rR = 0.0, rLit = 0.0, rTint = 0.0, rFy = 1e-4;
bool rShop = false;
if (kind < 0.5 || (kind > 10.5 && kind < 13.5))
{
	const float storey = 3.1;
	float level = floor(max(h, 0.0) / storey);
	float y = h - level * storey;
	float bayW = 2.5 + 0.8 * frac(seed * 7.13);
	float x = frac(along / bayW + 0.5) * bayW;
	float winW = bayW * (0.46 + 0.16 * frac(seed * 3.71));
	float x0 = 0.5 * (bayW - winW);
	float y0 = 0.95, y1 = 2.42;
	bool shop = kind > 10.5 && kind < 12.5 && level < 0.5;
	if (shop)
	{
		bayW = 3.6; x = frac(along / bayW + 0.5) * bayW; winW = 3.1; x0 = 0.25; y0 = 0.35; y1 = 2.65;
	}
	// Edges filtered over the pixel's footprint: a window a few pixels across (down the
	// street) must not alias into a speckle under the temporal jitter.
	float fx = max(fwidth(along), 1e-4), fy = max(fwidth(h), 1e-4), fm = max(fx, fy);
	float inWin = saturate((x - x0) / fx + 0.5) * saturate((x0 + winW - x) / fx + 0.5) * saturate((y - y0) / fy + 0.5) * saturate((y1 - y) / fy + 0.5) * step(0.0, h);
	float edge = min(min(x - x0, x0 + winW - x), min(y - y0, y1 - y));
	// Frames and mullions thinner than a pixel fade to their share of the window.
	float thin = saturate(1.5 - fm / 0.06);
	float frame = inWin * lerp(0.15, saturate((0.055 - edge) / fm + 0.5), thin);
	float mullion = inWin * lerp(0.05, saturate((0.025 - abs(x - (x0 + 0.5 * winW))) / fx + 0.5), thin) * (shop ? 0.0 : 1.0);
	frame = saturate(frame + mullion);
	float reveal = inWin * (1.0 - smoothstep(0.0, 0.14, y1 - y));
	float2 cell = float2(floor(along / bayW + 0.5), level);
	float r = frac(sin(dot(cell + seed * 17.0, float2(12.9898, 78.233))) * 43758.5453);
	// The pane itself has next to no diffuse colour: what it shows is the street's reflection
	// (its specular) and the room behind it (emissive, below). One window in four is tinted.
	float tinted = step(0.72, r);
	float3 glass = lerp(float3(0.006, 0.007, 0.008), VC.rgb * 0.12, tinted * 0.55);
	float pane = inWin * (1.0 - frame);
	col = lerp(stucco, glass, pane);
	col = lerp(col, float3(0.86, 0.86, 0.83), frame);
	col *= 1.0 - 0.45 * reveal * (1.0 - frame);
	rough = lerp(0.9, 0.04, pane);
	float lit = shop ? step(r, 0.72) : step(r, 0.36);
	rough = lerp(rough, 0.95, pane * lit * Lights); // a lit room shows its light, not a noisy mirror

	rPane = pane; rx = x; ry = y; rBay = bayW; rX0 = x0; rW = winW; rY0 = y0; rY1 = y1;
	rR = r; rLit = lit; rTint = tinted; rFy = fy; rShop = shop;
	// Art Deco street fronts at night: coloured uplights at the foot of the facade, one
	// every 3.6 m, washing the stucco (about 310 lux at the base, falling off upward), on
	// three hotels in five; pink, violet, cyan, teal or warm white per hotel.
	if (kind > 11.5)
	{
		float pick = frac(seed * 13.37);
		float3 tints[5] = { float3(1.0, 0.22, 0.62), float3(0.55, 0.28, 1.0), float3(0.18, 0.75, 1.0), float3(0.15, 1.0, 0.72), float3(1.0, 0.72, 0.45) };
		float3 wc = tints[(int)floor(frac(seed * 5.71) * 4.999)];
		float fx = (frac(along / 3.6) - 0.5) * 3.6;
		float spread = 0.28 + 0.2 * max(h, 0.0);
		float cone = exp(-(fx * fx) / (spread * spread));
		float fall = exp(-max(h, 0.0) / 5.0) * smoothstep(0.05, 0.5, h);
		emis += step(pick, 0.6) * Lights * stucco * wc * 100.0 * cone * fall * (1.0 - pane);
	}
}
else if (kind < 1.5) { rough = 0.86; }
else if (kind < 2.5) { col = VC.rgb * lerp(1.0, grain, 0.35); rough = 0.93; }
else if (kind < 3.5) { col = VC.rgb * 0.55 + 0.35; rough = 0.25; emis = VC.rgb * (300.0 + 2500.0 * Lights); }
else if (kind < 4.5)
{
	float bars = step(frac(along / 1.8), 0.035) + step(abs(h - 2.75), 0.04);
	bars = saturate(bars);
	col = lerp(float3(0.006, 0.007, 0.008), float3(0.8, 0.8, 0.77), bars);
	rough = lerp(0.04, 0.5, bars);
	// A lobby or cafe behind the porch glass, lit all day like a shop (a few closed at night).
	float pr = frac(sin(dot(float2(floor(along / 1.8), 0.0) + seed * 17.0, float2(12.9898, 78.233))) * 43758.5453);
	rPane = 1.0 - bars; rx = frac(along / 1.8) * 1.8; ry = h; rBay = 1.8; rX0 = 0.0; rW = 1.8; rY0 = 0.0; rY1 = 2.75;
	rR = pr; rLit = step(pr, 0.72); rTint = 0.0; rFy = max(fwidth(h), 1e-4); rShop = true;
	rough = lerp(rough, 0.95, rPane * rLit * Lights);
}
else if (kind < 5.5) { col = VC.rgb * lerp(1.0, grain, 0.25); rough = 0.5; }
else if (kind < 6.5) { col = VC.rgb * lerp(1.0, grain, 0.12); rough = 0.42; metal = 0.6; }
else if (kind < 7.5)
{
	float ring = smoothstep(0.7, 0.95, frac(h / 0.085 + 0.25 * sin(along * 6.2832)));
	col = VC.rgb * lerp(1.0, grain, 0.3) * (1.0 - 0.32 * ring) * lerp(0.8, 1.0, saturate(h / 1.5));
	rough = 0.95;
}
else if (kind < 8.5) { col = float3(0.8, 0.78, 0.72); rough = 0.2; emis = Lights * float3(1.0, 0.8, 0.58) * 9000.0; }
else if (kind < 9.5) { col = float3(0.03, 0.045, 0.05); rough = 0.05; }
)HLSL") TEXT(R"HLSL(
// Behind any window (set above), a room traced in the shader (interior mapping): the cell
// is the bay across, the storey up and 4-6 m deep (a shop 7-9 m).
if (rPane > 0.0)
{
	// The view ray from the pane meets the back wall, a side wall, the floor or the ceiling,
	// so the room moves with parallax as the car passes.
	float3 V = -normalize(CV);
	float3 N = normalize(VN);
	float3 T = normalize(cross(float3(0.0, 0.0, 1.0), N) + float3(1e-5, 1e-5, 0.0));
	T *= dot(ddx(LP), T) * ddx(along) + dot(ddy(LP), T) * ddy(along) < 0.0 ? -1.0 : 1.0; // along runs +T
	float3 rd = float3(dot(V, T), V.z, max(-dot(V, N), 0.03));
	float ceilH = rShop ? 3.0 : 2.8;
	float deep = (rShop ? 7.0 : 4.0) + 2.0 * frac(rR * 7.7);
	float tx = rd.x > 0.0 ? (rBay - rx) / rd.x : (rd.x < 0.0 ? -rx / rd.x : 1e4);
	float ty = rd.y > 0.0 ? (ceilH - ry) / rd.y : (rd.y < 0.0 ? -ry / rd.y : 1e4);
	float tz = deep / rd.z;
	float t = min(min(tx, ty), tz);
	float3 hit = float3(rx, ry, 0.0) + t * rd;
	float r2 = frac(rR * 91.7), r3 = frac(rR * 53.3), r4 = frac(rR * 17.9);
	float3 wallC = lerp(float3(0.62, 0.6, 0.55), float3(0.68, 0.6, 0.48), r2);
	wallC = lerp(wallC, VC.rgb, step(0.8, r3) * 0.5);
	float3 surf = wallC;
	float shade = 1.0;
	if (t == ty)
	{
		surf = rd.y > 0.0 ? float3(0.78, 0.77, 0.74) : (r3 < 0.5 ? float3(0.28, 0.18, 0.11) : float3(0.4, 0.38, 0.35)); // ceiling; wood or tile floor
	}
	else if (t == tx)
	{
		shade = 0.8;
	}
	else
	{
		// The back wall: a sofa, desk or shelving along its foot, a picture on some; a shop's
		// shelves run its height.
		float fx0 = rBay * (0.12 + 0.3 * r2), fx1 = fx0 + rBay * (0.35 + 0.25 * r3);
		float furn = step(fx0, hit.x) * step(hit.x, fx1) * step(hit.y, rShop ? 2.1 : 0.85);
		surf = lerp(surf, lerp(float3(0.12, 0.1, 0.09), float3(0.36, 0.31, 0.26), r3), furn);
		if (rShop) surf *= 1.0 - 0.4 * step(0.6, frac(hit.y / 0.42)) * step(hit.y, 2.1);
		float pic = step(abs(hit.x - rBay * 0.5), 0.35) * step(abs(hit.y - 1.6), 0.25) * step(0.55, r2) * (rShop ? 0.0 : 1.0);
		surf = lerp(surf, lerp(float3(0.15, 0.2, 0.3), float3(0.5, 0.35, 0.2), r3), pic);
	}
	// Illuminance on what the ray meets (lux): daylight through the window falls off with depth,
	// from a daylight factor of 10-20 % at the glass (about 5000 lux under a clear sky) to 1-2 %
	// at the back (about 1000 at 5 m); the room's lamps, on in shops all day (a few closed at
	// night) and in one home in three at night.
	float dayLux = (5000.0 * exp(-hit.z / 3.0) + 400.0) * (t == ty && rd.y > 0.0 ? 0.55 : 1.0);
	// Lamps differ room to room (a lit home 25-260 lux, log-uniform; one in eight only the cool
	// flicker-free glow of a television, 6 lux), and one lamp under the ceiling in the middle of
	// the room lights it unevenly (inverse square, normalised to its level 1.5 m away), so a
	// lit window at night shows a room rather than a flat sheet of light.
	// Shops and cafes run from a dim warm restaurant (about 175 lux) to bright retail (900).
	float level = (rShop ? 700.0 : 130.0) * exp2(lerp(rShop ? -2.0 : -2.4, rShop ? 0.4 : 1.0, frac(rR * 41.3)));
	float3 lampTint = lerp(float3(1.0, 0.72, 0.45), float3(1.0, 0.86, 0.7), frac(rR * 3.3));
	if (!rShop && frac(rR * 23.9) < 0.125) { level = 6.0; lampTint = float3(0.55, 0.7, 1.0); }
	float lamp = rShop ? level * lerp(1.0, rLit, Lights) : level * rLit * Lights;
	float3 toLamp = float3(0.5 * rBay, ceilH - 0.35, 0.5 * deep) - hit;
	float near = rShop ? 1.0 : 2.25 / (dot(toLamp, toLamp) + 0.4); // a shop has rows of fixtures
	float3 room = surf * shade * ((1.0 - Lights) * dayLux * float3(0.95, 1.0, 1.05) + lamp * near * lampTint) / 3.14159;
	// Blinds or curtains just inside the glass (homes and offices, not shops).
	float u = (rx - rX0) / rW, v = (ry - rY0) / (rY1 - rY0);
	float cover = 0.0;
	float3 coverC = float3(0.82, 0.8, 0.74);
	if (!rShop)
	{
		if (r4 < 0.35)
		{
			// Venetian blinds let down part way; their slats average out with distance.
			float slat = lerp(0.85, step(frac(ry / 0.05), 0.8), saturate(1.5 - rFy / 0.02));
			cover = step(1.0 - (0.3 + 0.7 * frac(r4 * 7.3)), v) * slat;
		}
		else if (r4 < 0.65)
		{
			float c = 0.15 + 0.3 * frac(r4 * 5.1);
			cover = saturate(step(u, c) + step(1.0 - c, u));
			coverC = lerp(float3(0.78, 0.74, 0.66), lerp(float3(0.45, 0.2, 0.18), float3(0.2, 0.3, 0.4), r2), step(0.6, frac(r4 * 3.7)));
			coverC *= 0.85 + 0.15 * sin(u * 70.0);
		}
	}
	float3 coverLum = coverC * ((1.0 - Lights) * 6000.0 + lamp * 0.6 * lampTint) / 3.14159;
	room = lerp(room, coverLum, cover);
	// Clear glass passes about 85 %, the tinted panes a quarter.
	emis += rPane * room * lerp(0.85, 0.25, rTint);
}
Roughness = rough;
Metal = metal;
Emis = emis;
Nrm = normalize(VN);
return max(col, 0.0);
)HLSL");

		// Lettering (text components): the glyph's distance field cuts the letter out. Neon
		// tubes (vertex alpha 1) glow day and night like the facades' neon (kind 3); painted
		// letters (alpha 0.5) are lit at night by the lamps that light the facade.
		const TCHAR* SignsCode = TEXT(R"HLSL(
float d = Glyph;
float w = max(fwidth(d), 1e-4);
Mask = saturate((d - 0.5) / w + 0.5);
float3 c = VC.rgb;
Nrm = normalize(VN);
if (VA > 0.75)
{
	Roughness = 0.25;
	// By day a lit tube still reads in its own colour against sunlit stucco (photographs of
	// Ocean Drive at noon): brighter than the facades' thin neon, and less washed out.
	Emis = c * (1200.0 + 1600.0 * Lights) * lerp(0.6, 1.0, smoothstep(0.5, 0.65, d));
	return c * 0.75 + 0.12;
}
Roughness = 0.45;
Emis = c * Lights * 60.0;
return c;
)HLSL");

		// Palm fronds: pinnate leaflets cut out of a V-shaped card, ragged and torn.
		const TCHAR* FoliageCode = TEXT(R"HLSL(
float s = UV0.x;
float v = abs(UV0.y);
float seed = floor(UV1.y * 4096.0 + 0.5) / 4096.0; // quantised (see the building shader)
float t = s * 90.0 - v * 7.0;
float idx = floor(t);
float r = frac(sin((idx + seed * 131.0) * 12.9898) * 43758.5453);
float leaf = step(frac(t), 0.62 + 0.2 * r);
float reach = 0.72 + 0.28 * r;
float mask = leaf * step(v, reach) * step(0.03, r);
mask = max(mask, step(v, 0.04) * step(s, 0.97));
Mask = mask;
float3 col = VC.rgb * (0.8 + 0.4 * r) * lerp(1.05, 0.85, v);
col = lerp(col, float3(0.42, 0.36, 0.19), smoothstep(0.82, 1.0, v / reach) * 0.55);
Sss = col * float3(0.8, 1.0, 0.35);
Roughness = 0.55;
Nrm = normalize(VN);
return col;
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

		UMaterialExpression* TexCoord(UMaterial* Material, int32 Index)
		{
			UMaterialExpressionTextureCoordinate* T = AddExpression<UMaterialExpressionTextureCoordinate>(Material);
			T->CoordinateIndex = Index;
			return T;
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
			AddInput(Custom, TEXT("Tint"), VectorParam(Material, TEXT("Tint"), FLinearColor(0.085f, 0.083f, 0.08f)));
			AddInput(Custom, TEXT("Bump"), ScalarParam(Material, TEXT("Bump"), 0.25f));
			AddInput(Custom, TEXT("Wet"), ScalarParam(Material, TEXT("Wet"), 0.f));
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
		case ESimCruiseTileLayer::Props:
		{
			Custom->Code = BuildingCode;
			Custom->AdditionalOutputs.Add({ FName(TEXT("Metal")), CMOT_Float1 });
			Custom->AdditionalOutputs.Add({ FName(TEXT("Emis")), CMOT_Float3 });
			AddInput(Custom, TEXT("Grain"), TextureParam(Material, TEXT("Grain"), DefaultTexture));
			AddInput(Custom, TEXT("GrainMean"), VectorParam(Material, TEXT("GrainMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("UV0"), TexCoord(Material, 0));
			AddInput(Custom, TEXT("UV1"), TexCoord(Material, 1));
			AddInput(Custom, TEXT("Lights"), ScalarParam(Material, TEXT("Lights"), 0.f));
			AddInput(Custom, TEXT("CV"), AddExpression<UMaterialExpressionCameraVectorWS>(Material)); // the rooms behind the windows
			Custom->RebuildOutputs();
			Material->GetEditorOnlyData()->Metallic.Connect(3, Custom);
			Material->GetEditorOnlyData()->EmissiveColor.Connect(4, Custom);
			break;
		}
		case ESimCruiseTileLayer::Foliage:
		{
			Custom->Code = FoliageCode;
			Custom->AdditionalOutputs.Add({ FName(TEXT("Mask")), CMOT_Float1 });
			Custom->AdditionalOutputs.Add({ FName(TEXT("Sss")), CMOT_Float3 });
			AddInput(Custom, TEXT("UV0"), TexCoord(Material, 0));
			AddInput(Custom, TEXT("UV1"), TexCoord(Material, 1));
			Material->BlendMode = BLEND_Masked;
			Material->TwoSided = true;
			Material->SetShadingModel(MSM_TwoSidedFoliage);
			Custom->RebuildOutputs();
			Material->GetEditorOnlyData()->OpacityMask.Connect(3, Custom);
			Material->GetEditorOnlyData()->SubsurfaceColor.Connect(4, Custom);
			{
				// The live wind (Live/SimCruiseLiveWorldSubsystem): drag grows with the square of the
				// wind speed, so a frond's tip bends about 1.1 cm per (m/s)^2 (30 cm in a 5 m/s
				// breeze, 1.1 m at 10 m/s), along the wind, dipping as it bends; gusts come and go
				// over seconds and the leaflets flutter faster as the wind rises. Each palm has
				// its own phase from where it stands.
				UMaterialExpressionCustom* Sway = AddExpression<UMaterialExpressionCustom>(Material);
				Sway->OutputType = CMOT_Float3;
				Sway->Inputs.Reset();
				Sway->Description = TEXT("SimCruise wind sway");
				Sway->Code = TEXT(R"HLSL(
float s = saturate(UV0.x);
float v = max(WindMs, 0.0);
float2 dir = WindDir.xy;
float phase = dot(WP.xy, float2(0.0013, 0.0017));
float gust = 0.65 + 0.35 * sin(Time * 1.7 + phase) * sin(Time * 0.37 + phase * 0.5);
float bend = min(1.1 * v * v, 160.0) * s * s * gust;
float flutter = sin(Time * (6.0 + 0.4 * v) + s * 9.0 + phase * 7.0) * min(0.8 * v, 12.0) * s;
return float3(dir * bend, -0.25 * bend + flutter);
)HLSL");
				AddInput(Sway, TEXT("UV0"), TexCoord(Material, 0));
				AddInput(Sway, TEXT("WP"), AddExpression<UMaterialExpressionWorldPosition>(Material));
				AddInput(Sway, TEXT("Time"), AddExpression<UMaterialExpressionTime>(Material));
				AddInput(Sway, TEXT("WindMs"), ScalarParam(Material, TEXT("WindMs"), 0.f));
				AddInput(Sway, TEXT("WindDir"), VectorParam(Material, TEXT("WindDir"), FLinearColor(1.f, 0.f, 0.f, 0.f)));
				Sway->RebuildOutputs();
				Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0, Sway);
			}
			break;
		}
		case ESimCruiseTileLayer::Signs:
		{
			Custom->Code = SignsCode;
			Custom->AdditionalOutputs.Add({ FName(TEXT("Mask")), CMOT_Float1 });
			Custom->AdditionalOutputs.Add({ FName(TEXT("Emis")), CMOT_Float3 });
			UMaterialExpressionFontSampleParameter* Glyph = AddExpression<UMaterialExpressionFontSampleParameter>(Material);
			Glyph->ParameterName = TEXT("Font"); // the name text components set their font pages by
			Glyph->Font = LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/RobotoDistanceField.RobotoDistanceField"));
			Glyph->FontTexturePage = 0;
			AddInput(Custom, TEXT("Glyph"), Glyph, 4); // the alpha: the distance field
			AddInput(Custom, TEXT("Lights"), ScalarParam(Material, TEXT("Lights"), 0.f));
			Material->BlendMode = BLEND_Masked;
			Custom->RebuildOutputs();
			Material->GetEditorOnlyData()->OpacityMask.Connect(3, Custom);
			Material->GetEditorOnlyData()->EmissiveColor.Connect(4, Custom);
			break;
		}
		case ESimCruiseTileLayer::Water:
		{
			Custom->Code = WaterCode;
			AddInput(Custom, TEXT("Time"), AddExpression<UMaterialExpressionTime>(Material));
			AddInput(Custom, TEXT("WaveScale"), ScalarParam(Material, TEXT("WaveScale"), 1.f));
			AddInput(Custom, TEXT("WaveDir"), VectorParam(Material, TEXT("WaveDir"), FLinearColor(1.f, 0.f, 0.f, 0.f)));
			{
				// The live tide lifts the open sea (the surface built at mean sea level, about
				// -10 cm in the map's NAVD88 heights), not lakes and pools.
				UMaterialExpressionCustom* Tide = AddExpression<UMaterialExpressionCustom>(Material);
				Tide->OutputType = CMOT_Float3;
				Tide->Inputs.Reset();
				Tide->Description = TEXT("SimCruise tide");
				Tide->Code = TEXT("return float3(0.0, 0.0, abs(LP.z + 10.0) < 8.0 ? SeaLevelCm : 0.0);");
				AddInput(Tide, TEXT("LP"), AddExpression<UMaterialExpressionLocalPosition>(Material));
				AddInput(Tide, TEXT("SeaLevelCm"), ScalarParam(Material, TEXT("SeaLevelCm"), 0.f));
				Tide->RebuildOutputs();
				Material->GetEditorOnlyData()->WorldPositionOffset.Connect(0, Tide);
			}
			UMaterialExpressionConstant* Specular = AddExpression<UMaterialExpressionConstant>(Material);
			Specular->R = 0.25f; // F0 ~ 0.02, water
			Material->GetEditorOnlyData()->Specular.Connect(0, Specular);
			break;
		}
		default:
			break;
		}
		Custom->RebuildOutputs();
		// The palms are instanced static meshes drawn with the props (bark) and foliage materials.
		if (Layer == ESimCruiseTileLayer::Props || Layer == ESimCruiseTileLayer::Foliage)
		{
			Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);
		}
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
		// Editor builds make it from the code (a saved copy would hide a change); packaged
		// builds load the copy the SimCruiseBuildWorldMaterials commandlet saved.
		UMaterialInterface* Material = nullptr;
#if !WITH_EDITOR
		Material = LoadObject<UMaterialInterface>(nullptr, *SimCruiseWorldMaterials::AssetPath(Layer), nullptr, LOAD_NoWarn | LOAD_Quiet);
#endif
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
	AsphaltTexture = LoadDetail(TEXT("data/art/ground/asphalt.jpg"), TEXT("T_SimCruiseAsphalt"), AsphaltMean);
	GrassTexture = LoadDetail(TEXT("data/art/ground/grass-detail.jpg"), TEXT("T_SimCruiseGrass"), GrassMean);
	SandTexture = LoadDetail(TEXT("data/art/ground/mineral-ground.png"), TEXT("T_SimCruiseSand"), SandMean);
	TArray<FColor> Grey;
	Grey.Init(FColor(128, 128, 128, 255), 4 * 4);
	BlankTexture = CreateTexture(this, TEXT("T_SimCruiseBlank"), Grey, 4, 4, true, true);
	if (!AsphaltTexture) { AsphaltTexture = BlankTexture; AsphaltMean = MeanLinear(Grey); }
	if (!GrassTexture) { GrassTexture = BlankTexture; GrassMean = MeanLinear(Grey); }
	if (!SandTexture) { SandTexture = BlankTexture; SandMean = MeanLinear(Grey); }

	if (FApp::CanEverRender())
	{
		BuildPalmMeshes();
	}
}

namespace SimCruiseWorldMaterials
{
	UStaticMesh* BuildRuntimeMesh(const FName Name, const TArray<const FSimCruiseMeshData*>& Parts, bool bWaitForCompile, bool bFullBuild)
	{
		FMeshDescription Description;
		FStaticMeshAttributes Attributes(Description);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(2);
		TPolygonGroupAttributesRef<FName> SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		UStaticMesh* Mesh = NewObject<UStaticMesh>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UStaticMesh::StaticClass(), Name), RF_Transient);
		TArray<FVertexInstanceID> Instances;
		for (int32 S = 0; S < Parts.Num(); ++S)
		{
			const FSimCruiseMeshData& Part = *Parts[S];
			const FPolygonGroupID Group = Description.CreatePolygonGroup();
			const FName Slot(*FString::Printf(TEXT("Slot%d"), S));
			SlotNames[Group] = Slot;
			Mesh->GetStaticMaterials().Add_GetRef(FStaticMaterial(UMaterial::GetDefaultMaterial(MD_Surface), Slot)).UVChannelData = FMeshUVChannelInfo(1.f);
			Instances.SetNum(Part.Positions.Num());
			for (int32 V = 0; V < Part.Positions.Num(); ++V)
			{
				const FVertexID Vertex = Description.CreateVertex();
				Positions[Vertex] = FVector3f(Part.Positions[V]);
				const FVertexInstanceID Instance = Description.CreateVertexInstance(Vertex);
				Normals[Instance] = FVector3f(Part.Normals[V]);
				const FLinearColor& C = Part.Colors[V];
				Colors[Instance] = FVector4f(C.R, C.G, C.B, C.A);
				UVs.Set(Instance, 0, FVector2f(Part.UV0.IsValidIndex(V) ? Part.UV0[V] : FVector2D::ZeroVector));
				UVs.Set(Instance, 1, FVector2f(Part.UV1.IsValidIndex(V) ? Part.UV1[V] : FVector2D::ZeroVector));
				Instances[V] = Instance;
			}
			for (int32 I = 0; I + 2 < Part.Indices.Num(); I += 3)
			{
				const int32 A = Part.Indices[I], B = Part.Indices[I + 1], C = Part.Indices[I + 2];
				if (A == B || B == C || A == C) continue;
				Description.CreateTriangle(Group, { Instances[A], Instances[B], Instances[C] });
			}
		}
		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bMarkPackageDirty = false;
		Params.bUseHashAsGuid = true;
		Params.bBuildSimpleCollision = false;
		Params.bCommitMeshDescription = true;
		Params.bAllowCpuAccess = false;
#if WITH_EDITOR
		if (bFullBuild)
		{
		// The full build keeps the authored normals and makes a mesh distance field (Lumen).
		Params.bFastBuild = false;
		Mesh->SetNumSourceModels(1);
		FMeshBuildSettings& Build = Mesh->GetSourceModel(0).BuildSettings;
		Build.bRecomputeNormals = false;
		Build.bRecomputeTangents = true;
		Build.bUseMikkTSpace = true;
		Build.bGenerateLightmapUVs = false;
		Build.bRemoveDegenerates = true;
		Build.bUseFullPrecisionUVs = true;
		}
		else
		{
			Params.bFastBuild = true;
			FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Description);
			FStaticMeshOperations::ComputeTangentsAndNormals(Description, EComputeNTBsFlags::Tangents | EComputeNTBsFlags::WeightedNTBs);
		}
#else
		Params.bFastBuild = true;
		FStaticMeshOperations::ComputeTriangleTangentsAndNormals(Description);
			FStaticMeshOperations::ComputeTangentsAndNormals(Description, EComputeNTBsFlags::Tangents | EComputeNTBsFlags::WeightedNTBs);
#endif
		TArray<const FMeshDescription*> Descriptions = { &Description };
		if (!Mesh->BuildFromMeshDescriptions(Descriptions, Params))
		{
			return nullptr;
		}
#if WITH_EDITOR
		if (bWaitForCompile)
		{
			FStaticMeshCompilingManager::Get().FinishCompilation({ Mesh });
		}
#endif
		return Mesh;
	}
}

void USimCruiseWorldResources::BuildPalmMeshes()
{
	PalmBarkMaterial = UMaterialInstanceDynamic::Create(GetLayerMaterial(ESimCruiseTileLayer::Props), this);
	PalmBarkMaterial->SetTextureParameterValue(TEXT("Grain"), AsphaltTexture);
	PalmBarkMaterial->SetVectorParameterValue(TEXT("GrainMean"), AsphaltMean);
	PalmMeshes.Reset();
	for (int32 Variant = 0; Variant < SimCruiseTileDetail::PalmVariants; ++Variant)
	{
		FSimCruiseMeshData Trunk, Fronds;
		SimCruiseTileDetail::BuildPalmVariant(Variant, Trunk, Fronds);
		UStaticMesh* Mesh = SimCruiseWorldMaterials::BuildRuntimeMesh(*FString::Printf(TEXT("SM_SimCruisePalm%d"), Variant), { &Trunk, &Fronds }, true, true);
		if (!Mesh)
		{
			UE_LOG(LogSimCruiseWorldResources, Error, TEXT("Palm variant %d did not build"), Variant);
			continue;
		}
		// UStaticMesh::SetMaterial is editor-only; a packaged build sets the slots directly
		// (the mesh is not drawn yet, so its render proxy picks them up).
		TArray<FStaticMaterial>& Slots = Mesh->GetStaticMaterials();
		if (Slots.Num() < 2)
		{
			Slots.SetNum(2);
		}
		Slots[0].MaterialInterface = PalmBarkMaterial;
		Slots[1].MaterialInterface = GetLayerMaterial(ESimCruiseTileLayer::Foliage);
		// Collision: the bole and the lower trunk (the fronds are well above a car).
		Mesh->CreateBodySetup();
		if (UBodySetup* Body = Mesh->GetBodySetup())
		{
			Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			FKSphylElem Capsule(22.f, 260.f);
			Capsule.Center = FVector(0.0, 0.0, 150.0);
			Body->AggGeom.SphylElems.Add(Capsule);
			Body->CreatePhysicsMeshes();
		}
		PalmMeshes.Add(Mesh);
	}
	UE_LOG(LogSimCruiseWorldResources, Display, TEXT("Built %d palm meshes"), PalmMeshes.Num());
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

UTexture2D* USimCruiseWorldResources::ReplaceAerial(const FString& Key, const TArray<FColor>& Pixels, int32 Width, int32 Height, UTexture2D*& OutNew)
{
	OutNew = nullptr;
	UTexture2D* Old = FindAerial(Key);
	if (!Old)
	{
		return nullptr;
	}
	OutNew = CreateTexture(this, NAME_None, Pixels, Width, Height, true, false);
	if (OutNew)
	{
		Aerials.Add(Key, OutNew);
	}
	return Old;
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
