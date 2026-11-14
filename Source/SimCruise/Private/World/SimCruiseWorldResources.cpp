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
#include "Materials/MaterialExpressionPixelDepth.h"
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
		// The grass field's shared helpers (docs/research/world-detail-methods.md): hashed value
		// noise and the meso layer, used by the ground (lawns, 1x) and the regional landscape
		// (hillsides, at a larger size). Prepended to a Custom node's code, so one text serves both.
		// Seamless across tiles (docs/team/reviews/2026-11-14-scan-engine.md, P0): the surface metres
		// m carry the tile's anchor fmod(tile x size, 1664 m), computed exactly on the CPU, so the
		// two sides of a border differ by a whole number of 1664 m. Every pattern period divides
		// 1664 m (QP), and every hashed lattice key wraps with its own period (n cells per 1664 m),
		// so each pattern continues across the border. The hash is integer pcg2d (Jarzynski and
		// Olano, JCGT 9(3), 2020): exact on every GPU, unlike frac(sin(...)) on large arguments.
		// A struct (Custom node code is a function body): prepended once to each material.
		const TCHAR* SeamCode = TEXT(R"HLSL(
struct FSC
{
	float P() { return 1664.0; }
	float QP(float t) { return 1664.0 / max(round(1664.0 / t), 1.0); }
	uint2 Pcg(uint2 v)
	{
		v = v * 1664525u + 1013904223u;
		v.x += v.y * 1664525u; v.y += v.x * 1664525u;
		v = v ^ (v >> 16u);
		v.x += v.y * 1664525u; v.y += v.x * 1664525u;
		v = v ^ (v >> 16u);
		return v;
	}
	// A lattice cell's random value in [0, 1): the key wrapped by n cells (0: no wrap), salted by s.
	float Hash(float2 cell, float s, float n)
	{
		if (n > 0.5) cell -= floor(cell / n) * n;
		uint2 k = uint2(int2(cell)) + uint2(asuint(s), asuint(s) * 747796405u);
		return float(Pcg(k).x >> 8u) * (1.0 / 16777216.0);
	}
};
)HLSL");
		const TCHAR* GrassFieldCode = TEXT(R"HLSL(
struct FGF
{
	float H(float2 p, float s) { FSC sc; return sc.Hash(p, s, 0.0); }
	float N(float2 p, float s, float n)
	{
		FSC sc;
		float2 i = floor(p), f = frac(p), u = f * f * (3.0 - 2.0 * f);
		float a = sc.Hash(i, s, n), b = sc.Hash(i + float2(1, 0), s, n), c = sc.Hash(i + float2(0, 1), s, n), d = sc.Hash(i + float2(1, 1), s, n);
		return a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y;
	}
	// Three octaves at x1, x2, x4 (whole multiples keep one period); n: cells per period, 0 none.
	float F(float2 p, float s, float n) { return (N(p, s, n) * 0.571 + N(p * 2.0 + 1.7, s + 1, n * 2.0) * 0.286 + N(p * 4.0 + 9.2, s + 2, n * 4.0) * 0.143); }
	// p * f where f is made a whole number of cells per period P (metres; 0: no period).
	float2 At(float2 p, float f, float P, out float n) { n = P > 0.0 ? max(round(f * P), 1.0) : 0.0; return p * (P > 0.0 ? n / P : f); }
	// Meso: clumps (0.3-1 units), patches of yellower or bluer grass (2-8), clover and dry
	// patches (GrassDry from the season; 0 = growing; out dr). A multiplier around 1 on the grass
	// albedo. wSmall and wLarge fade the small (clumps, clover) and large (patches, dry) parts,
	// so a far surface does not alias them.
	// P: the period of p (the anchor period in p's units; 0 where p is not tile-anchored).
	float3 Meso(float2 p, float dry, float wSmall, float wLarge, out float clump, out float dr, float P = 0.0)
	{
		float n1, n2, n3, n4;
		float2 p1 = At(p, 2.2, P, n1), p2 = At(p, 0.2, P, n2), p3 = At(p, 3.0, P, n3), p4 = At(p, 1.0 / 7.0, P, n4);
		clump = F(p1, 11.0, n1);
		float gpatch = F(p2, 23.0, n2);
		float clover = saturate((F(p3, 41.0, n3) - 0.70) * 12.0) * wSmall;
		dr = saturate((F(p4, 31.0, n4) - (0.62 - 0.25 * dry)) * 3.0) * max(dry, 0.15) * wLarge;
		float3 meso = (1.0 + (0.24 * clump - 0.12) * wSmall) * lerp(float3(1, 1, 1), float3(1.25, 1.08, 1.15), gpatch * 0.6 * wLarge);
		meso = lerp(meso, float3(0.7, 0.8, 0.6), clover);
		return meso; // the dry share dr is applied by the caller: lawn and hillside albedos differ
	}
};
)HLSL");
		// The fine-detail layer (docs/research/world-detail-methods.md, "The AI detail kit"): eight
		// slices made once, offline, by an open image model (lab/detail-kit), stored as albedo
		// ratios with mean 1 (data/art/detail-kit), so absolute colour still comes from the
		// aerial photograph or the class colour and the kit only adds what a photograph of the
		// material shows up close. Hex tiling: three rotated, offset lookups per map, so a slice
		// never repeats. Slices: 0 asphalt, 1 concrete, 2 stucco, 3 brick, 4 soil, 5 sand,
		// 6 leaf litter, 7 bark.
		const TCHAR* DetailKitCode = TEXT(R"HLSL(
struct FDK
{
	// The hex lattice uses the skew 15/13 (for 2/sqrt 3): with a rational skew a shift of 26 cells
	// in x maps the lattice onto itself, so it can repeat exactly with the anchor period.
	// A cell key (i, j) wraps by the lattice's period: (30 N, 15 N) and (0, 26 N), N = P / (26 scale).
	float H(float2 c, float s, float N)
	{
		if (N > 0.5)
		{
			float k = floor(c.x / (30.0 * N));
			c -= k * float2(30.0 * N, 15.0 * N);
			c.y -= floor(c.y / (26.0 * N)) * 26.0 * N;
		}
		FSC sc;
		return sc.Hash(c, s, 0.0);
	}
	// p: surface metres; scale: metres one slice covers; fpx: metres per pixel; depthCm: pixel
	// depth. Returns the albedo ratio (1 = no change) and, in nOut, the surface slope in p's axes.
	// The layer fades in from 0.5 m, out by 40 m, and out before a slice is under ~30 pixels
	// (its mip chain is clamped at 32 px a slice, so it never mixes slices or aliases).
	float3 Sample(Texture2D Alb, SamplerState AlbS, Texture2D Nor, SamplerState NorS, float slice, float2 p, float scale,
		float fpx, float depthCm, float on, out float2 nOut)
	{
		scale = 1664.0 / (26.0 * max(round(1664.0 / (26.0 * scale)), 1.0));
		float w = on * saturate((depthCm - 50.0) / 50.0) * saturate((4000.0 - depthCm) / 1000.0) * saturate(2.0 - fpx * 60.0 / scale);
		nOut = float2(0.0, 0.0);
		float2 q = p / scale;
		float2 gx = ddx(q), gy = ddy(q);
		[branch] if (w <= 0.0) return float3(1.0, 1.0, 1.0);
		float2 cell = float2(fmod(slice, 4.0), floor(slice / 4.0));
		const float2 CS = float2(0.25, 0.5), INNER = float2(480.0 / 512.0, 480.0 / 512.0), TEX = float2(2048.0, 1024.0);
		// clamp the footprint to mip 4 (16 atlas texels a pixel)
		float lg = max(length(gx * CS * INNER * TEX), length(gy * CS * INNER * TEX));
		float k = lg > 16.0 ? 16.0 / lg : 1.0;
		gx *= k; gy *= k;
		float2 sk = float2(q.x * (15.0 / 13.0), q.y + (15.0 / 26.0) * q.x);
		float2 hb = floor(sk), fr = frac(sk);
		float3 acc = 0.0; float2 nacc = 0.0; float ws = 0.0, w2 = 0.0;
		const float NP = round(1664.0 / (26.0 * scale)); // lattice periods per anchor period
		[unroll] for (int i = 0; i < 3; ++i)
		{
			float2 cc = hb + (i == 0 ? float2(0, 0) : (i == 1 ? float2(1, 0) : float2(0, 1)));
			if (fr.x + fr.y > 1.0) cc = hb + (i == 0 ? float2(1, 1) : (i == 1 ? float2(1, 0) : float2(0, 1)));
			float2 cq = float2(cc.x * (13.0 / 15.0), cc.y - 0.5 * cc.x);
			float wi = pow(saturate(1.0 - 0.95 * length(q - cq)), 3.0);
			// Brick (3) and bark (7) have a direction (courses, furrows): never rotated; brick
			// is not offset either (a bond is a regular pattern, plain tiling is what a wall is).
			float an = (slice == 3.0 || slice == 7.0) ? 0.0 : H(cc, 3.0, NP) * 6.2832, ca = cos(an), sa = sin(an);
			float2x2 R = float2x2(ca, -sa, sa, ca);
			// Rotated about the cell's own centre: a whole-period shift of q leaves q - cq unchanged.
			float2 r = mul(R, q - cq) + (slice == 3.0 ? float2(0.0, 0.0) : float2(H(cc, 5.0, NP), H(cc, 7.0, NP)));
			float2 uv = (cell + (16.0 / 512.0) + frac(r) * INNER) * CS;
			float2 dx = mul(R, gx) * INNER * CS, dy = mul(R, gy) * INNER * CS;
			acc += wi * (Alb.SampleGrad(AlbS, uv, dx, dy).rgb + 0.5);
			float2 nt = Nor.SampleGrad(NorS, uv, dx, dy).rg * 2.0 - 1.0;
			nacc += wi * mul(nt, R); // back from the rotated lookup to p's axes
			ws += wi; w2 += wi * wi;
		}
		float3 ratio = acc / max(ws, 1e-4);
		ratio = 1.0 + (ratio - 1.0) * ws / max(sqrt(w2), 1e-4); // keep the contrast blending would lose
		nOut = nacc / max(ws, 1e-4) * w;
		return lerp(float3(1.0, 1.0, 1.0), clamp(ratio, 0.5, 1.5), w);
	}
};
)HLSL");
		const TCHAR* GroundCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
FSC sc; // SeamCode
float2 uv = LP.xy / TileSizeCm;
float2 m = LP.xy * 0.01 + Anchor.xy;
float4 mask = saturate(Texture2DSample(Mask, MaskSampler, uv));
mask.rgb = pow(mask.rgb, 1.0 / 2.2); // stored as an sRGB texture; alpha is linear
float3 photo = Texture2DSample(Aerial, AerialSampler, (Cell.xy + uv) * 0.25).rgb;
float3 g1 = Texture2DSample(Grass, GrassSampler, m / sc.QP(2.3)).rgb / max(GrassMean.rgb, 0.001);
float3 g2 = Texture2DSample(Grass, GrassSampler, m / sc.QP(9.7) + 0.31).rgb / max(GrassMean.rgb, 0.001);
// Two scales at an irrational ratio and a rotation, so the 512 px photograph never reads as a grid.
float3 s1 = Texture2DSample(Sand, SandSampler, m / sc.QP(2.9)).rgb / max(SandMean.rgb, 0.001);
float3 s2 = Texture2DSample(Sand, SandSampler, float2(m.x * 0.8 - m.y * 0.6, m.x * 0.6 + m.y * 0.8) / (sc.QP(5.0 * 7.37) / 5.0) + 0.17).rgb / max(SandMean.rgb, 0.001);
s1 = lerp(float3(1.0, 1.0, 1.0), s1, 0.5);
float3 p1 = Texture2DSample(Grain, GrainSampler, m / sc.QP(3.1)).rgb / max(GrainMean.rgb, 0.001);
float3 p2 = Texture2DSample(Grain, GrainSampler, m / sc.QP(23.0) + 0.53).rgb / max(GrainMean.rgb, 0.001);
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
float fpx = length(fwidth(m)); // ground metres per pixel (outside the branch, so the derivative is defined)
)HLSL" /* MSVC caps one literal at 16 KB */ R"HLSL(// Grass field (docs/research/world-detail-methods.md): a lawn is not one photograph. Three
// sizes the eye reads: no-repeat micro texture (hex tiling: three rotated, offset lookups),
// meso variation (clumps 0.3-1 m, patches of yellower or bluer grass 2-8 m, clover, dry
// patches), and near the car real depth: eight shells of procedural blades marched in the
// pixel, so blades stand up, hide each other and shade their own bases. All ALU; the
// meso field keeps the mean, so the colour the photograph and the grade set does not move.
if (GrassField > 0.5 && veg > 0.01)
{
	FGF gf; // the grass field helpers (GrassFieldCode)
	float2 gm = m; // tile-anchored metres (the hash is exact at any size: FSC::Hash)
	// micro: hex tiling of the CC0 grass photograph at about 1.7 m (a rational hex lattice whose
	// period divides the anchor period; see FDK.H)
	const float HexM = 1664.0 / (26.0 * round(1664.0 / (26.0 * 1.7)));
	const float HexN = round(1664.0 / (26.0 * HexM));
	FDK hk;
	float2 q = gm / HexM;
	float2 sk = float2(q.x * (15.0 / 13.0), q.y + (15.0 / 26.0) * q.x);
	float2 hexBase = floor(sk), fr = frac(sk);
	float3 acc = 0.0; float wsum = 0.0, w2 = 0.0;
	[unroll] for (int k = 0; k < 3; ++k)
	{
		float2 cc = hexBase + (k == 0 ? float2(0, 0) : (k == 1 ? float2(1, 0) : float2(0, 1)));
		if (fr.x + fr.y > 1.0) cc = hexBase + (k == 0 ? float2(1, 1) : (k == 1 ? float2(1, 0) : float2(0, 1)));
		float2 cq = float2(cc.x * (13.0 / 15.0), cc.y - 0.5 * cc.x);
		float w = pow(saturate(1.0 - 0.95 * length(q - cq)), 3.0);
		float an = hk.H(cc, 3.0, HexN) * 6.2832;
		float2 ql = q - cq; // rotated about the cell's centre (seamless, as FDK)
		float2 r = float2(ql.x * cos(an) - ql.y * sin(an), ql.x * sin(an) + ql.y * cos(an)) + float2(hk.H(cc, 5.0, HexN), hk.H(cc, 7.0, HexN));
		acc += w * Texture2DSampleGrad(Grass, GrassSampler, r, ddx(q), ddy(q)).rgb / max(GrassMean.rgb, 0.001);
		wsum += w; w2 += w * w;
	}
	float3 micro = acc / max(wsum, 1e-4);
	micro = 1.0 + (micro - 1.0) * wsum / max(sqrt(w2), 1e-4); // keep the contrast blending would lose
	// meso: clumps, patches, clover, dry patches (GrassDry from the season; 0 = growing)
	float clump, dryShare;
	float3 meso = gf.Meso(gm, GrassDry, 1.0, 1.0, clump, dryShare, 1664.0);
	meso = lerp(meso, float3(2.6, 1.6, 2.4), dryShare);
	grass = micro * meso;
	// nearW: parallax blade shells within 22 m
	float nearW = saturate((2200.0 - Depth) / 400.0);
	if (nearW > 0.0)
	{
		// Only well inside the lawn (the 10 m mask is soft at a path's edge), and only while a
		// blade is wider than a pixel: thinner than that it would shimmer, so it fades to its mean.
		nearW *= saturate((veg - 0.3) * 3.0) * saturate(2.0 - fpx / 0.02);
	}
	if (nearW > 0.0)
	{
		float3 dir = -CV;                     // camera to surface (world = tile axes)
		float2 slope = dir.xy / min(dir.z, -0.05) * 0.01; // metres sideways per cm of height
		float Hb = 8.0 * (1.0 + 0.4 * GrassDry);  // tallest blade, cm
		// One leaning, tapering, flat blade per 2 cm cell on two offset grids; the eight height
		// slices start at a per-pixel random offset so the slices do not show as steps.
		float2 jk = floor(gm * 1013.0); float jit = sc.Hash(jk, 71.0, 1013.0 * 1664.0);
		float shade = 0.55; bool hit = false;
		[unroll] for (int s = 0; s < 8; ++s)
		{
			float z = Hb * (1.0 - (s + jit) / 8.0);
			float2 pz = (gm + slope * z) * 50.0; // 2 cm cells: St. Augustine blades are 6-10 mm wide
			[unroll] for (int g = 0; g < 2; ++g)
			{
				float2 pc = pz + (g == 0 ? float2(0, 0) : float2(0.5, 0.5));
				float2 ci = floor(pc), ck = ci - floor(ci / (50.0 * 1664.0)) * (50.0 * 1664.0); // 2 cm cells, wrapped by the anchor period
				float sd = 80.0 + g * 9.0;
				float bh = Hb * (0.55 + 0.45 * gf.H(ck, sd)) * (0.8 + 0.4 * clump);
				float t = z / bh;
				float an = gf.H(ck, sd + 1.0) * 3.1416;
				float2 ax = float2(cos(an), sin(an));
				float2 bc = ci + 0.3 + 0.4 * float2(gf.H(ck, sd + 2.0), gf.H(ck, sd + 3.0)) + ax * (gf.H(ck, sd + 4.0) - 0.5) * 0.6 * t * t;
				float2 d = pc - bc;
				float across = abs(dot(d, float2(-ax.y, ax.x))), along = abs(dot(d, ax));
				if (!hit && t < 1.0 && across < 0.22 * (1.0 - t) && along < 0.03 + 0.05 * (1.0 - t))
				{
					shade = (0.5 + 0.75 * t) * (0.85 + 0.3 * gf.H(ck, sd + 5.0));
					hit = true;
				}
			}
		}
		grass *= lerp(1.0, shade * 1.1, nearW);
	}
}
float3 detail = (p1 * lerp(1.0, p2, 0.4)) * (bare + paved) + grass * veg + s1 * lerp(1.0, s2, 0.6) * sand;
// The AI detail kit (DetailKitCode): the dominant ground class picks its slice (bare soil,
// pavement concrete, sand, leaf litter under a canopy); lawns keep the grass field. Its slope
// tilts VN, which the normal output below takes.
{
	FDK dk; float2 dn;
	float ksl = 4.0, kw = bare;
	if (paved > kw) { ksl = 1.0; kw = paved; }
	if (sand > kw) { ksl = 5.0; kw = sand; }
	if (forest > kw) { ksl = 6.0; kw = forest; }
	kw = saturate(kw * 1.5);
	float3 kr = dk.Sample(DetailAlb, DetailAlbSampler, DetailNrm, DetailNrmSampler, ksl, m, ksl == 1.0 ? 1.5 : 1.2, fpx, Depth, DetailLayer, dn);
	detail *= lerp(float3(1.0, 1.0, 1.0), kr, kw);
	VN = normalize(VN + float3(dn * kw, 0.0));
}
float dl = dot(detail, W);
float3 hue = lerp(float3(1.0, 1.0, 1.0), detail / max(dl, 0.001), 0.3 * veg + 0.15 * sand);
baseColor *= lerp(1.0, dl, 0.55 * veg + 0.25 * sand + 0.4 * (bare + paved)) * hue;
// Steep ground (road cuts, banks, gullies): bare soil from 25 degrees, rock from 33 to 45,
// sampled triplanar so a cut face is not stretched, rock with horizontal bedding bands.
// Colours from the rule palette (drier climates have paler, redder soil; the GEOL chunk's
// SSURGO soil and SGMC rock colours replace it when they arrive).
{
	float steep = saturate((0.906 - VN.z) / 0.067);   // cos 25 -> cos 33 degrees
	float rocky = saturate((0.839 - VN.z) / 0.132);   // cos 33 -> cos 45 degrees
	[branch] if (steep > 0.0)
	{
		float3 an = abs(normalize(VN)); an /= max(an.x + an.y + an.z, 1e-4);
		float z = LP.z * 0.01;
		float Tg = sc.QP(1.9), gm0 = max(dot(GrainMean.rgb, W), 0.001);
		float gx = dot(Texture2DSample(Grain, GrainSampler, float2(m.y, z) / Tg).rgb, W) / gm0;
		float gy = dot(Texture2DSample(Grain, GrainSampler, float2(m.x, z) / Tg).rgb, W) / gm0;
		float gz = dot(Texture2DSample(Grain, GrainSampler, m / Tg).rgb, W) / gm0;
		float grain = gx * an.x + gy * an.y + gz * an.z;
		float strata = 0.82 + 0.18 * sc.Hash(float2(floor(z / 0.35), 0.0), 9.0, 0.0);
		float3 soil = lerp(float3(0.16, 0.115, 0.075), float3(0.29, 0.21, 0.14), GrassDry);
		float3 rock = float3(0.21, 0.20, 0.18) * strata;
		float3 face = lerp(soil, rock, rocky) * lerp(1.0, grain, 0.6);
		baseColor = lerp(baseColor, face, steep * (1.0 - water));
	}
}
// Albedo limits (docs/team/research/realtime-context.md, item 4): lawns and meadows keep a
// grass albedo (luminance 0.18 at most) and a natural saturation (0.55 at most), so a field
// never turns neon; the photograph's tone and the class colour keep their hue.
{
	float Lb = max(dot(baseColor, W), 1e-4);
	float mx = max(baseColor.r, max(baseColor.g, baseColor.b)), mn = min(baseColor.r, min(baseColor.g, baseColor.b));
	float sat = (mx - mn) / max(mx, 1e-4);
	float sMax = lerp(1.0, 0.55, veg);
	// The exact pull toward grey that leaves saturation sMax (c' = L + k (c - L)).
	float den = (mx - mn) - sMax * (mx - Lb);
	baseColor = Lb + (baseColor - Lb) * (sat > sMax && den > 1e-5 ? saturate(sMax * Lb / den) : 1.0);
	baseColor *= min(1.0, lerp(0.8, 0.18, veg) / Lb);
}
baseColor = lerp(baseColor, float3(0.025, 0.15, 0.17), water);
// Snow lying now (MODIS snow cover, set per tile by ASimCruiseRegionalTerrain): settled snow
// over the ground, thinner where grass and shrubs poke through.
baseColor = lerp(baseColor, float3(0.80, 0.83, 0.86), SnowCover * (1.0 - water) * (1.0 - 0.35 * veg));
Roughness = lerp(lerp(lerp(0.9, 0.97, veg), 0.86, sand), 0.08, water);
Roughness = lerp(Roughness, 0.92, saturate((0.906 - VN.z) / 0.067) * (1.0 - water));
Nrm = normalize(VN);
return max(baseColor, 0.0);
)HLSL");

		const TCHAR* AsphaltCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
FSC sc; // SeamCode
float2 m = LP.xy * 0.01 + Anchor.xy;
float meanL = max(dot(AsphaltMean.rgb, W), 0.001);
float2 uv = m / sc.QP(2.6);
float a = dot(Texture2DSample(Asphalt, AsphaltSampler, uv).rgb, W) / meanL;
float ax = dot(Texture2DSample(Asphalt, AsphaltSampler, uv + float2(0.0012, 0.0)).rgb, W) / meanL;
float ay = dot(Texture2DSample(Asphalt, AsphaltSampler, uv + float2(0.0, 0.0012)).rgb, W) / meanL;
float macro = dot(Texture2DSample(Asphalt, AsphaltSampler, m / sc.QP(23.0) + 0.41).rgb, W) / meanL;
float macro2 = dot(Texture2DSample(Asphalt, AsphaltSampler, m / sc.QP(61.0) + 0.77).rgb, W) / meanL;
float v = lerp(1.0, macro, 0.30) * lerp(1.0, macro2, 0.25);
float fine = lerp(1.0, a, 0.45);
float3 col = Tint.rgb * fine * v;
float3 gravel = float3(0.26, 0.23, 0.19) * lerp(1.0, a, 1.0);
col = lerp(col, gravel, VC.r);
Roughness = saturate(0.80 + 0.10 * (1.0 - a) + 0.05 * VC.r);
float3 g = float3(-(ax - a), -(ay - a), 0.0) * Bump;
// The AI detail kit (DetailKitCode): aggregate and cracks up close; the gravel edge is soil.
{
	FDK dk; float2 dn;
	float3 kr = dk.Sample(DetailAlb, DetailAlbSampler, DetailNrm, DetailNrmSampler, VC.r > 0.5 ? 4.0 : 0.0, m, 1.0, length(fwidth(m)), Depth, DetailLayer, dn);
	col *= kr;
	g += float3(dn * 0.6, 0.0);
}
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
// Standing water reflects as water (F0 0.02, Specular 0.25), not as plastic (0.04, the default 0.5).
Spec = lerp(0.5, 0.25, puddle);
return max(col, 0.0);
)HLSL");

		const TCHAR* ConcreteCode = TEXT(R"HLSL(
const float3 W = float3(0.2126, 0.7152, 0.0722);
FSC sc; // SeamCode
float2 m = LP.xy * 0.01 + Anchor.xy;
float vertical = 1.0 - saturate(abs(VN.z) * 2.0 - 0.5);
float2 uv = lerp(m, float2(m.x + m.y, LP.z * 0.01), vertical) / sc.QP(1.9);
float meanL = max(dot(GrainMean.rgb, W), 0.001);
float a = dot(Texture2DSample(Grain, GrainSampler, uv).rgb, W) / meanL;
float ax = dot(Texture2DSample(Grain, GrainSampler, uv + float2(0.0015, 0.0)).rgb, W) / meanL;
float ay = dot(Texture2DSample(Grain, GrainSampler, uv + float2(0.0, 0.0015)).rgb, W) / meanL;
float macro = dot(Texture2DSample(Grain, GrainSampler, m / sc.QP(17.0) + 0.29).rgb, W) / meanL;
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
FSC sc; // SeamCode
float2 m = LP.xy * 0.01 + Anchor.xy;
float meanL = max(dot(GrainMean.rgb, W), 0.001);
float a = dot(Texture2DSample(Grain, GrainSampler, m / sc.QP(2.6)).rgb, W) / meanL;
float worn = dot(Texture2DSample(Grain, GrainSampler, m / sc.QP(9.0) + 0.61).rgb, W) / meanL;
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
// Weathering (lighting review 2026-11-12): broad staining at a quarter of the colour, and the
// splash zone at the foot of the wall a fifth darker up to 1.2 m.
// Stains only darken (never lift a wall above its weathered-paint albedo).
float3 stucco = VC.rgb * lerp(1.0, grain, 0.09) * lerp(1.0, min(stain, 1.0), 0.25);
stucco *= lerp(0.78, 1.0, smoothstep(0.0, 1.2, h));
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
	// The storey height rides in the vertex alpha (2.4 m + 4 m x alpha); opaque white is the
	// usual 3.1 m. Main Street's 19th-century blocks run 4-4.5 m storeys.
	const float storey = VA > 0.995 ? 3.1 : 2.4 + 4.0 * VA;
	float level = floor(max(h, 0.0) / storey);
	float y = h - level * storey;
	float bayW = 2.5 + 0.8 * frac(seed * 7.13);
	float x = frac(along / bayW + 0.5) * bayW;
	float winW = bayW * (0.46 + 0.16 * frac(seed * 3.71));
	float x0 = 0.5 * (bayW - winW);
	float y0 = 0.95, y1 = storey - 0.68;
	bool shop = kind > 10.5 && kind < 12.5 && level < 0.5;
	if (shop)
	{
		bayW = 3.6; x = frac(along / bayW + 0.5) * bayW; winW = 3.1; x0 = 0.25; y0 = 0.35; y1 = storey - 0.45;
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
	// Rain runs dirt off each sill: a streak under the window, darkest at the sill, fading over
	// a metre (one window in three barely streaked).
	float under = step(x0 + 0.05, x) * step(x, x0 + winW - 0.05) * (1.0 - inWin);
	float below = y0 - y;
	float streak = under * step(0.0, below) * exp(-below / 0.7) * (0.06 + 0.12 * frac(r * 5.3));
	col *= 1.0 - streak;
	col = lerp(col, float3(0.75, 0.75, 0.72), frame);
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
	col = lerp(float3(0.006, 0.007, 0.008), float3(0.75, 0.75, 0.72), bars);
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
		// The AI detail kit (DetailKitCode): two atlases and the on switch (-SimCruiseNoDetailLayer).
		// Road and ground only (kept small: walls, kerbs and bark are left as they were).
		if (Layer == ESimCruiseTileLayer::Terrain || Layer == ESimCruiseTileLayer::Asphalt)
		{
			AddInput(Custom, TEXT("DetailAlb"), TextureParam(Material, TEXT("DetailAlb"), DefaultTexture));
			AddInput(Custom, TEXT("DetailNrm"), TextureParam(Material, TEXT("DetailNrm"), DefaultTexture));
			AddInput(Custom, TEXT("DetailLayer"), ScalarParam(Material, TEXT("DetailLayer"), 0.f));
			if (Layer != ESimCruiseTileLayer::Terrain)
			{
				AddInput(Custom, TEXT("Depth"), AddExpression<UMaterialExpressionPixelDepth>(Material));
			}
		}

		switch (Layer)
		{
		case ESimCruiseTileLayer::Terrain:
		{
			Custom->Code = FString(SeamCode) + GrassFieldCode + DetailKitCode + GroundCode;
			UMaterialExpressionConstant* GroundSpecular = AddExpression<UMaterialExpressionConstant>(Material);
			GroundSpecular->R = 0.35f; // vegetation and soil reflect less than the 0.5 default
			Material->GetEditorOnlyData()->Specular.Connect(0, GroundSpecular);
		}
			AddInput(Custom, TEXT("TileSizeCm"), ScalarParam(Material, TEXT("TileSizeCm"), 55000.f));
			AddInput(Custom, TEXT("Cell"), VectorParam(Material, TEXT("Cell"), FLinearColor(0, 0, 0, 0)));
			AddInput(Custom, TEXT("AerialGain"), ScalarParam(Material, TEXT("AerialGain"), 1.f));
			AddInput(Custom, TEXT("SnowCover"), ScalarParam(Material, TEXT("SnowCover"), 0.f));
			AddInput(Custom, TEXT("Mask"), TextureParam(Material, TEXT("Mask"), DefaultTexture));
			AddInput(Custom, TEXT("Aerial"), TextureParam(Material, TEXT("Aerial"), DefaultTexture));
			AddInput(Custom, TEXT("Grass"), TextureParam(Material, TEXT("Grass"), DefaultTexture));
			AddInput(Custom, TEXT("Sand"), TextureParam(Material, TEXT("Sand"), DefaultTexture));
			AddInput(Custom, TEXT("Grain"), TextureParam(Material, TEXT("Grain"), DefaultTexture));
			AddInput(Custom, TEXT("GrassMean"), VectorParam(Material, TEXT("GrassMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("SandMean"), VectorParam(Material, TEXT("SandMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("GrainMean"), VectorParam(Material, TEXT("GrainMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("GrassField"), ScalarParam(Material, TEXT("GrassField"), 0.f));
			AddInput(Custom, TEXT("GrassDry"), ScalarParam(Material, TEXT("GrassDry"), 0.f));
			AddInput(Custom, TEXT("CV"), AddExpression<UMaterialExpressionCameraVectorWS>(Material));
			AddInput(Custom, TEXT("Depth"), AddExpression<UMaterialExpressionPixelDepth>(Material));
			break;
		case ESimCruiseTileLayer::Asphalt:
			Custom->Code = FString(SeamCode) + DetailKitCode + AsphaltCode;
			Custom->AdditionalOutputs.Add({ FName(TEXT("Spec")), CMOT_Float1 });
			Custom->RebuildOutputs();
			Material->GetEditorOnlyData()->Specular.Connect(3, Custom);
			AddInput(Custom, TEXT("Asphalt"), TextureParam(Material, TEXT("Asphalt"), DefaultTexture));
			AddInput(Custom, TEXT("AsphaltMean"), VectorParam(Material, TEXT("AsphaltMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("Tint"), VectorParam(Material, TEXT("Tint"), FLinearColor(0.085f, 0.083f, 0.08f)));
			AddInput(Custom, TEXT("Bump"), ScalarParam(Material, TEXT("Bump"), 0.25f));
			AddInput(Custom, TEXT("Wet"), ScalarParam(Material, TEXT("Wet"), 0.f));
			break;
		case ESimCruiseTileLayer::Concrete:
			Custom->Code = FString(SeamCode) + ConcreteCode;
			AddInput(Custom, TEXT("Grain"), TextureParam(Material, TEXT("Grain"), DefaultTexture));
			AddInput(Custom, TEXT("GrainMean"), VectorParam(Material, TEXT("GrainMean"), FLinearColor(0.2f, 0.2f, 0.2f)));
			AddInput(Custom, TEXT("Tint"), VectorParam(Material, TEXT("Tint"), FLinearColor(0.34f, 0.33f, 0.31f)));
			AddInput(Custom, TEXT("Bump"), ScalarParam(Material, TEXT("Bump"), 0.15f));
			break;
		case ESimCruiseTileLayer::Paint:
			Custom->Code = FString(SeamCode) + PaintCode;
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

#if WITH_EDITOR
const TCHAR* USimCruiseWorldResources::GrassFieldHlsl()
{
	// The landscape's grass field needs the seam helpers it calls (SeamCode) in front.
	static const FString Code = FString(SimCruiseWorldMaterials::SeamCode) + SimCruiseWorldMaterials::GrassFieldCode;
	return *Code;
}
#endif

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
	// The AI detail kit (lab/detail-kit, docs/research/world-detail-methods.md): albedo ratios and
	// normals, 4 x 2 slices; when missing, a flat stand-in (ratio 1, flat normal) keeps it inert.
	auto LoadKit = [this](const TCHAR* Relative, const TCHAR* Name) -> UTexture2D*
	{
		TArray<FColor> Pixels;
		int32 W = 0, H = 0;
		const FString Path = FPaths::Combine(SimCruiseTileData::RepositoryRoot(), Relative);
		if (!DecodeImageFile(Path, Pixels, W, H))
		{
			UE_LOG(LogSimCruiseWorldResources, Warning, TEXT("Missing detail kit %s"), *Path);
			return nullptr;
		}
		return CreateTexture(this, Name, Pixels, W, H, true, true);
	};
	DetailAlbTexture = LoadKit(TEXT("data/art/detail-kit/detail-albedo.png"), TEXT("T_SimCruiseDetailAlb"));
	DetailNrmTexture = LoadKit(TEXT("data/art/detail-kit/detail-normal.png"), TEXT("T_SimCruiseDetailNrm"));
	if (!DetailAlbTexture || !DetailNrmTexture)
	{
		TArray<FColor> Flat;
		Flat.Init(FColor(188, 188, 188, 128), 4 * 4); // sRGB 188 = linear 0.5: ratio 1, normal flat
		DetailAlbTexture = CreateTexture(this, TEXT("T_SimCruiseDetailAlbFlat"), Flat, 4, 4, true, true);
		DetailNrmTexture = DetailAlbTexture;
	}
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

UTexture2D* USimCruiseWorldResources::AddAerialMips(const FString& Key, TArray<TArray<FColor>>&& Mips, int32 Width, int32 Height)
{
	if (UTexture2D* Existing = FindAerial(Key))
	{
		return Existing;
	}
	UTexture2D* Texture = CreateTextureFromMips(this, NAME_None, MoveTemp(Mips), Width, Height, true, false);
	if (Texture)
	{
		Aerials.Add(Key, Texture);
	}
	return Texture;
}

UTexture2D* USimCruiseWorldResources::ReplaceAerialMips(const FString& Key, TArray<TArray<FColor>>&& Mips, int32 Width, int32 Height, UTexture2D*& OutNew)
{
	OutNew = nullptr;
	UTexture2D* Old = FindAerial(Key);
	if (!Old)
	{
		return nullptr;
	}
	OutNew = CreateTextureFromMips(this, NAME_None, MoveTemp(Mips), Width, Height, true, false);
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
	TArray<TArray<FColor>> Mips;
	BuildMipChain(Pixels, Width, Height, bSRGB, Mips);
	return CreateTextureFromMips(Outer, Name, MoveTemp(Mips), Width, Height, bSRGB, bWrap);
}

void USimCruiseWorldResources::BuildMipChain(const TArray<FColor>& Pixels, int32 Width, int32 Height, bool bSRGB, TArray<TArray<FColor>>& OutMips)
{
	OutMips.Reset();
	if (Width <= 0 || Height <= 0 || Pixels.Num() != Width * Height)
	{
		return;
	}
	// Box-filtered mip chain (in sRGB-decoded space for colour images).
	OutMips.Add(Pixels);
	int32 W = Width, H = Height;
	while (W > 1 || H > 1)
	{
		const TArray<FColor>& Level = OutMips.Last();
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
		OutMips.Add(MoveTemp(Next));
		W = NW;
		H = NH;
	}
}

UTexture2D* USimCruiseWorldResources::CreateTextureFromMips(UObject* Outer, FName Name, TArray<TArray<FColor>>&& Mips, int32 Width, int32 Height, bool bSRGB, bool bWrap)
{
	if (Width <= 0 || Height <= 0 || Mips.IsEmpty() || Mips[0].Num() != Width * Height)
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
	int32 W = Width, H = Height;
	for (const TArray<FColor>& Level : Mips)
	{
		FTexture2DMipMap* Mip = new FTexture2DMipMap(W, H, 1);
		Data->Mips.Add(Mip);
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		void* Dest = Mip->BulkData.Realloc(static_cast<int64>(W) * H * 4);
		FMemory::Memcpy(Dest, Level.GetData(), static_cast<int64>(W) * H * 4);
		Mip->BulkData.Unlock();
		W = FMath::Max(1, W / 2);
		H = FMath::Max(1, H / 2);
	}
	Texture->SRGB = bSRGB;
	Texture->AddressX = bWrap ? TA_Wrap : TA_Clamp;
	Texture->AddressY = bWrap ? TA_Wrap : TA_Clamp;
	Texture->LODGroup = TEXTUREGROUP_World;
	Texture->NeverStream = true;
	Texture->UpdateResource();
	return Texture;
}
