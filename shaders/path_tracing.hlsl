#include "common.hlsl"

//#ifndef SHADER_STAGE
//    #error Definition of SHADER_STAGE must be provided
//#endif
//#define SHADER_STAGE_RAYGEN     1
//#define SHADER_STAGE_CLOSESTHIT 2
//#define SHADER_STAGE_MISS       3

// ---------------------------------------------------------
#define OBJECT_ID_NONE            0xffff
#define PI                        3.14159265

#define TRACE_AMBIENT_OCCLUSION   0
#define TRACE_DIFFUSE_GI          1
#define TRACE_MODE                TRACE_DIFFUSE_GI
//#define TRACE_MODE                TRACE_AMBIENT_OCCLUSION

// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
// TMin should be kept small to prevent missing geometry at close contact areas.
// #todo: See 'Ray Tracing Gems' series.
#define RAYGEN_T_MIN              0.001
#define RAYGEN_T_MAX              10000.0
#define MAX_BOUNCE                8
#define SURFACE_NORMAL_OFFSET     0.001

// Temp boost sky light as light sources are not there yet
// and the indoor scene is too dark.
#define SKYBOX_BOOST              1.0

#define RANDOM_SEQUENCE_WIDTH     64
#define RANDOM_SEQUENCE_HEIGHT    64
#define RANDOM_SEQUENCE_LENGTH    (RANDOM_SEQUENCE_WIDTH * RANDOM_SEQUENCE_HEIGHT)

struct PathTracingUniform
{
	float4 thetaSeq[RANDOM_SEQUENCE_LENGTH / 4];
	float4 phiSeq[RANDOM_SEQUENCE_LENGTH / 4];
	uint renderTargetWidth;
	uint renderTargetHeight;
	uint bInvalidateHistory;
	uint _pad0;
};

struct VertexAttributes
{
	float3 normal;
	float2 texcoord;
};

struct RayGenConstantBuffer
{
	float4x4 dummyValue;
};
struct ClosestHitPushConstants
{
	uint objectID;
};

// Global root signature
RaytracingAccelerationStructure    rtScene            : register(t0, space0);
ByteAddressBuffer                  gIndexBuffer       : register(t1, space0);
ByteAddressBuffer                  gVertexBuffer      : register(t2, space0);
StructuredBuffer<GPUSceneItem>     gpuSceneBuffer     : register(t3, space0);
TextureCube                        skybox             : register(t4, space0);
RWTexture2D<float4>                renderTarget       : register(u0, space0);
ConstantBuffer<SceneUniform>       sceneUniform       : register(b0, space0);
ConstantBuffer<PathTracingUniform> pathTracingUniform : register(b1, space0);

// Local root signature (raygen)
ConstantBuffer<RayGenConstantBuffer> g_rayGenCB : register(b0, space1);

// Local root signature (closest hit)
ConstantBuffer<ClosestHitPushConstants> g_closestHitCB : register(b0, space2);

// Material binding
#define TEMP_MAX_SRVS 1024
ConstantBuffer<Material> materials[]        : register(b0, space3); // bindless in another space
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space3); // bindless in another space

SamplerState albedoSampler                  : register(s0, space0);
SamplerState skyboxSampler                  : register(s1, space0);

typedef BuiltInTriangleIntersectionAttributes IntersectionAttributes;
// Should match with path_tracing_pass.cpp
struct RayPayload
{
	float3 surfaceNormal;
	float  roughness;

	float3 albedo;
	float  hitTime;

	float3 emission;
	uint   objectID;
};

RayPayload createRayPayload()
{
	RayPayload payload;
	payload.surfaceNormal = float3(0, 0, 0);
	payload.roughness     = 1.0;
	payload.albedo        = float3(0, 0, 0);
	payload.hitTime       = -1.0;
	payload.emission      = float3(0, 0, 0);
	payload.objectID      = OBJECT_ID_NONE;
	return payload;
}

void generateCameraRay(uint2 texel, out float3 origin, out float3 direction)
{
	float2 xy = float2(texel) + 0.5;
	float2 screenPos = (xy / DispatchRaysDimensions().xy) * 2.0 - 1.0;
	screenPos.y = -screenPos.y;

	float4 worldPos = mul(float4(screenPos, 0.0, 1.0), sceneUniform.viewProjInvMatrix);
	worldPos.xyz /= worldPos.w;

	origin = sceneUniform.cameraPosition.xyz;
	direction = normalize(worldPos.xyz - origin);
}

void computeTangentFrame(float3 N, out float3 T, out float3 B)
{
	B = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1.0, 0.0, 0.0);
	T = normalize(cross(B, N));
	B = cross(N, T);
}

// ---------------------------------------------------------
// BSDFs (#todo-pathtracing: Move to bsdf.hlsl)

// cosTheta = dot(incident_or_exitant_light, half_vector)
float3 fresnelSchlick(float cosTheta, float3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// N: macrosurface normal
float geometryBeckmann(float3 N, float3 H, float3 V, float roughness)
{
	float thetaV = acos(dot(N, V));
	float tanThetaV = tan(thetaV);
	float a = 1.0 / (roughness * tanThetaV);
	float aa = a * a;

	if (dot(V, H) / dot(V, N) <= 0.0)
	{
		return 0.0;
	}

	if (a < 1.6)
	{
		float num = 3.535 * a + 2.181 * aa;
		float denom = 1.0 + 2.276 * a + 2.577 * aa;
		return num / denom;
	}
	return 1.0;
}

float geometrySmithBeckmann(float3 N, float3 H, float3 V, float3 L, float roughness)
{
	float ggx2 = geometryBeckmann(N, H, V, roughness);
	float ggx1 = geometryBeckmann(N, H, L, roughness);
	return 1.0f / (1.0f + ggx1 * ggx2);
}

float distributionBeckmann(float3 N, float3 H, float roughness)
{
	float cosH = dot(N, H);
	if (roughness == 0.0)
	{
		return 1.0;
	}
	if (H.z < 0.0) cosH = -cosH;
	float cosH2 = cosH * cosH;
	float rr = roughness * roughness;

	float exp_x = (1.0 - cosH2) / (rr * cosH);
	float num = (cosH > 0.0 ? 1.0 : 0.0) * exp(-exp_x);

	float denom = PI * rr * cosH2 * cosH2;
	return num / denom;
}

float ErfInv(float x)
{
	float w, p;
	x = clamp(x, -.99999f, .99999f);
	w = -log((1 - x) * (1 + x));
	if (w < 5) {
		w = w - 2.5f;
		p = 2.81022636e-08f;
		p = 3.43273939e-07f + p * w;
		p = -3.5233877e-06f + p * w;
		p = -4.39150654e-06f + p * w;
		p = 0.00021858087f + p * w;
		p = -0.00125372503f + p * w;
		p = -0.00417768164f + p * w;
		p = 0.246640727f + p * w;
		p = 1.50140941f + p * w;
	} else {
		w = sqrt(w) - 3;
		p = -0.000200214257f;
		p = 0.000100950558f + p * w;
		p = 0.00134934322f + p * w;
		p = -0.00367342844f + p * w;
		p = 0.00573950773f + p * w;
		p = -0.0076224613f + p * w;
		p = 0.00943887047f + p * w;
		p = 1.00167406f + p * w;
		p = 2.83297682f + p * w;
	}
	return p * x;
}
float Erf(float x)
{
	// constants
	float a1 = 0.254829592f;
	float a2 = -0.284496736f;
	float a3 = 1.421413741f;
	float a4 = -1.453152027f;
	float a5 = 1.061405429f;
	float p = 0.3275911f;

	// Save the sign of x
	int sign = 1;
	if (x < 0) sign = -1;
	x = abs(x);

	// A&S formula 7.1.26
	float t = 1 / (1 + p * x);
	float y =
		1 -
		(((((a5 * t + a4) * t) + a3) * t + a2) * t + a1) * t * exp(-x * x);

	return sign * y;
}
float CosTheta(float3 w) { return w.z; }
float Cos2Theta(float3 w) { return w.z * w.z; }
float AbsCosTheta(float3 w) { return abs(w.z); }
float Sin2Theta(float3 w) { return max(0.0f, 1.0f - Cos2Theta(w)); }
float SinTheta(float3 w) { return sqrt(Sin2Theta(w)); }
float CosPhi(float3 w) {
	float sinTheta = SinTheta(w);
	return (sinTheta == 0) ? 1 : clamp(w.x / sinTheta, -1, 1);
}
float SinPhi(float3 w) {
	float sinTheta = SinTheta(w);
	return (sinTheta == 0) ? 0 : clamp(w.y / sinTheta, -1, 1);
}
void BeckmannSample11(
	float cosThetaI, float U1, float U2,
	out float slope_x, out float slope_y)
{
	const float Pi = PI;

	/* Special case (normal incidence) */
	if (cosThetaI > .9999) {
		float r = sqrt(-log(1.0f - U1));
		float sinPhi = sin(2 * Pi * U2);
		float cosPhi = cos(2 * Pi * U2);
		slope_x = r * cosPhi;
		slope_y = r * sinPhi;
		return;
	}

	/* The original inversion routine from the paper contained
	   discontinuities, which causes issues for QMC integration
	   and techniques like Kelemen-style MLT. The following code
	   performs a numerical inversion with better behavior */
	float sinThetaI =
		sqrt(max((float)0, (float)1 - cosThetaI * cosThetaI));
	float tanThetaI = sinThetaI / cosThetaI;
	float cotThetaI = 1 / tanThetaI;

	/* Search interval -- everything is parameterized
	   in the Erf() domain */
	float a = -1, c = Erf(cotThetaI);
	float sample_x = max(U1, (float)1e-6f);

	/* Start with a good initial guess */
	// float b = (1-sample_x) * a + sample_x * c;

	/* We can do better (inverse of an approximation computed in
	 * Mathematica) */
	float thetaI = acos(cosThetaI);
	float fit = 1 + thetaI * (-0.876f + thetaI * (0.4265f - 0.0594f * thetaI));
	float b = c - (1 + c) * pow(1 - sample_x, fit);

	/* Normalization factor for the CDF */
	const float SQRT_PI_INV = 1.f / sqrt(Pi);
	float normalization =
		1 /
		(1 + c + SQRT_PI_INV * tanThetaI * exp(-cotThetaI * cotThetaI));

	int it = 0;
	while (++it < 10) {
		/* Bisection criterion -- the oddly-looking
		   Boolean expression are intentional to check
		   for NaNs at little additional cost */
		if (!(b >= a && b <= c)) b = 0.5f * (a + c);

		/* Evaluate the CDF and its derivative
		   (i.e. the density function) */
		float invErf = ErfInv(b);
		float value =
			normalization *
			(1 + b + SQRT_PI_INV * tanThetaI * exp(-invErf * invErf)) -
			sample_x;
		float derivative = normalization * (1 - invErf * tanThetaI);

		if (abs(value) < 1e-5f) break;

		/* Update bisection intervals */
		if (value > 0)
			c = b;
		else
			a = b;

		b -= value / derivative;
	}

	/* Now convert back into a slope value */
	slope_x = ErfInv(b);

	/* Simulate Y component */
	slope_y = ErfInv(2.0f * max(U2, (float)1e-6f) - 1.0f);
}

float3 BeckmannSample(float3 wi, float alpha_x, float alpha_y, float U1, float U2)
{
	// 1. stretch wi
	float3 wiStretched = normalize(float3(alpha_x * wi.x, alpha_y * wi.y, wi.z));

	// 2. simulate P22_{wi}(x_slope, y_slope, 1, 1)
	float slope_x, slope_y;
	BeckmannSample11(CosTheta(wiStretched), U1, U2, slope_x, slope_y);

	// 3. rotate
	float tmp = CosPhi(wiStretched) * slope_x - SinPhi(wiStretched) * slope_y;
	slope_y = SinPhi(wiStretched) * slope_x + CosPhi(wiStretched) * slope_y;
	slope_x = tmp;

	// 4. unstretch
	slope_x = alpha_x * slope_x;
	slope_y = alpha_y * slope_y;

	// 5. compute normal
	return normalize(float3(-slope_x, -slope_y, 1.0));
}

float3 sampleWh(float rand0, float rand1, float3 Wo, float alpha)
{
	// https://pbr-book.org/3ed-2018/Light_Transport_I_Surface_Reflection/Sampling_Reflection_Functions#sec:microfacet-sample
	// Sample from the distribution of visible microfacets from a given wo.
	float3 Wh;
	bool bFlip = Wo.z < 0.0;
	Wh = BeckmannSample(bFlip ? -Wo : Wo, alpha, alpha, rand0, rand1);
	if (bFlip) Wh = -Wh;
	return Wh;
}

void microfactBRDF(
	RayDesc rayDesc, RayPayload rayPayload,
	float rand0, float rand1,
	out float3 outReflectance, out float3 outScatteredDir)
{
	float3 baseColor = rayPayload.albedo;
	float roughness = rayPayload.roughness;
	float metallic = 0.0; // #todo-pathtracing: No metallic yet

	float3 worldT, worldB;
	computeTangentFrame(rayPayload.surfaceNormal, worldT, worldB);
	float3x3 localToWorld = float3x3(worldT, worldB, rayPayload.surfaceNormal);
	float3x3 worldToLocal = transpose(localToWorld);

	float3 N = float3(0, 0, 1); // #todo-pathtracing: No bump mapping yet
	float3 Wo = mul(-rayDesc.Direction, worldToLocal);
	float3 Wh = sampleWh(rand0, rand1, Wo, roughness);
	float3 Wi = reflect(-Wo, Wh);
	float NdotWi = abs(dot(N, Wi));

	float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

	float3 F = fresnelSchlick(abs(dot(Wh, Wo)), F0);
	float G = geometrySmithBeckmann(N, Wh, Wo, Wi, roughness);
	float NDF = distributionBeckmann(N, Wh, roughness);

	float3 kS = F;
	float3 kD = 1.0 - kS;
	float3 diffuse = baseColor - (1.0 - metallic);
	float3 specular = (F * G * NDF) / (4.0 * NdotWi * abs(dot(N, Wo)) + 0.001);

	outReflectance = (kD * diffuse + kS * specular) * NdotWi;
	outScatteredDir = mul(Wi, localToWorld);
	// #todo-pathtracing: outPdf
}

// ---------------------------------------------------------

float3 traceIncomingRadiance(uint2 targetTexel, float3 cameraRayOrigin, float3 cameraRayDir)
{
	uint firstSeqLinearIndex = targetTexel.x + pathTracingUniform.renderTargetWidth * targetTexel.y;

	RayPayload currentRayPayload = createRayPayload();

	RayDesc currentRay;
	currentRay.Origin = cameraRayOrigin;
	currentRay.Direction = cameraRayDir;
	currentRay.TMin = RAYGEN_T_MIN;
	currentRay.TMax = RAYGEN_T_MAX;

	float3 reflectanceHistory[MAX_BOUNCE + 1];
	float3 radianceHistory[MAX_BOUNCE + 1];
	uint numBounces = 0;

	while (numBounces < MAX_BOUNCE)
	{
		uint instanceInclusionMask = ~0; // Do not ignore anything
		uint rayContributionToHitGroupIndex = 0;
		uint multiplierForGeometryContributionToHitGroupIndex = 1;
		uint missShaderIndex = 0;
		TraceRay(
			rtScene,
			RAY_FLAG_NONE,
			instanceInclusionMask,
			rayContributionToHitGroupIndex,
			multiplierForGeometryContributionToHitGroupIndex,
			missShaderIndex,
			currentRay,
			currentRayPayload);

		// Hit the sky. Sample the skybox.
		if (currentRayPayload.objectID == OBJECT_ID_NONE)
		{
			radianceHistory[numBounces] = SKYBOX_BOOST * skybox.SampleLevel(skyboxSampler, currentRay.Direction, 0.0).rgb;
			reflectanceHistory[numBounces] = 1;
			break;
		}
		// Emissive shape. Exit the loop.
		else if (any(currentRayPayload.emission > 0))
		{
			radianceHistory[numBounces] = currentRayPayload.emission;
			reflectanceHistory[numBounces] = 1;
			break;
		}

		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRay.Direction + currentRay.Origin;

#if 1
		uint seqLinearIndex0 = (firstSeqLinearIndex + (2 * numBounces + 0)) % RANDOM_SEQUENCE_LENGTH;
		uint seqLinearIndex1 = (firstSeqLinearIndex + (2 * numBounces + 1)) % RANDOM_SEQUENCE_LENGTH;
		float rand0 = pathTracingUniform.phiSeq[seqLinearIndex0 / 4][seqLinearIndex0 % 4] / (PI * 2.0);
		float rand1 = pathTracingUniform.phiSeq[seqLinearIndex1 / 4][seqLinearIndex1 % 4] / (PI * 2.0);

		float3 scatteredReflectance, scatteredDir;
		microfactBRDF(currentRay, currentRayPayload, rand0, rand1,
			scatteredReflectance, scatteredDir);
#else

		uint seqLinearIndex = (firstSeqLinearIndex + numBounces) % RANDOM_SEQUENCE_LENGTH;
		float theta = pathTracingUniform.thetaSeq[seqLinearIndex / 4][seqLinearIndex % 4];
		float phi = pathTracingUniform.phiSeq[seqLinearIndex / 4][seqLinearIndex % 4];

		float3 scatteredReflectance = currentRayPayload.albedo;
		float3 scatteredDir = float3(cos(phi) * cos(theta), sin(phi) * cos(theta), sin(theta));
		scatteredDir = (surfaceTangent * scatteredDir.x) + (surfaceBitangent * scatteredDir.y) + (surfaceNormal * scatteredDir.z);
#endif

		radianceHistory[numBounces] = 0;
		reflectanceHistory[numBounces] = scatteredReflectance;

		currentRay.Origin = surfacePosition + SURFACE_NORMAL_OFFSET * surfaceNormal;
		currentRay.Direction = scatteredDir;
		//currentRay.TMin = RAYGEN_T_MIN;
		//currentRay.TMax = RAYGEN_T_MAX;

		numBounces += 1;
	}

	float3 Li = 0;
	if (numBounces < MAX_BOUNCE)
	{
		for (uint i = 0; i <= numBounces; ++i)
		{
			uint j = numBounces - i;
			Li = reflectanceHistory[j] * (Li + radianceHistory[j]);
		}
	}

	return Li;
}

float traceAmbientOcclusion(uint2 targetTexel, float3 cameraRayOrigin, float3 cameraRayDir)
{
	RayPayload cameraRayPayload = createRayPayload();
	RayDesc cameraRay;
	{
		cameraRay.Origin = cameraRayOrigin;
		cameraRay.Direction = cameraRayDir;
		cameraRay.TMin = RAYGEN_T_MIN;
		cameraRay.TMax = RAYGEN_T_MAX;

		uint instanceInclusionMask = ~0; // Do not ignore anything
		uint rayContributionToHitGroupIndex = 0;
		uint multiplierForGeometryContributionToHitGroupIndex = 1;
		uint missShaderIndex = 0;
		TraceRay(
			rtScene,
			RAY_FLAG_NONE,
			instanceInclusionMask,
			rayContributionToHitGroupIndex,
			multiplierForGeometryContributionToHitGroupIndex,
			missShaderIndex,
			cameraRay,
			cameraRayPayload);
	}

	// Current pixel is the sky.
	if (cameraRayPayload.objectID == OBJECT_ID_NONE)
	{
		return 1.0;
	}

	uint firstSeqLinearIndex = targetTexel.x + pathTracingUniform.renderTargetWidth * targetTexel.y;

	RayPayload currentRayPayload = cameraRayPayload;
	RayDesc currentRayDesc = cameraRay;

	uint numBounces = 0;
	while (numBounces < MAX_BOUNCE)
	{
		float3 surfaceNormal = currentRayPayload.surfaceNormal;
		float3 surfaceTangent, surfaceBitangent;
		computeTangentFrame(surfaceNormal, surfaceTangent, surfaceBitangent);

		float3 surfacePosition = currentRayPayload.hitTime * currentRayDesc.Direction + currentRayDesc.Origin;
		surfacePosition += SURFACE_NORMAL_OFFSET * surfaceNormal; // Slightly push toward N

		uint seqLinearIndex = (firstSeqLinearIndex + numBounces) % RANDOM_SEQUENCE_LENGTH;
		float theta = pathTracingUniform.thetaSeq[seqLinearIndex / 4][seqLinearIndex % 4];
		float phi = pathTracingUniform.phiSeq[seqLinearIndex / 4][seqLinearIndex % 4];
		float3 aoRayDir = float3(cos(phi) * cos(theta), sin(phi) * cos(theta), sin(theta));
		aoRayDir = (surfaceTangent * aoRayDir.x) + (surfaceBitangent * aoRayDir.y) + (surfaceNormal * aoRayDir.z);

		RayPayload aoRayPayload = createRayPayload();
		RayDesc aoRay;
		aoRay.Origin = surfacePosition;
		aoRay.Direction = aoRayDir;
		aoRay.TMin = RAYGEN_T_MIN;
		aoRay.TMax = RAYGEN_T_MAX;

		uint instanceInclusionMask = ~0; // Do not ignore anything
		uint rayContributionToHitGroupIndex = 0;
		uint multiplierForGeometryContributionToHitGroupIndex = 1;
		uint missShaderIndex = 0;
		TraceRay(
			rtScene,
			RAY_FLAG_NONE,
			instanceInclusionMask,
			rayContributionToHitGroupIndex,
			multiplierForGeometryContributionToHitGroupIndex,
			missShaderIndex,
			aoRay,
			aoRayPayload);

		if (aoRayPayload.objectID == OBJECT_ID_NONE)
		{
			break;
		}
		numBounces += 1;
		currentRayPayload = aoRayPayload;
		currentRayDesc = aoRay;
	}

	float ambientOcclusion = (numBounces == MAX_BOUNCE) ? 0.0 : pow(0.9, numBounces);
	return ambientOcclusion;
}

[shader("raygeneration")]
void MainRaygen()
{
	uint2 targetTexel = DispatchRaysIndex().xy;

	float3 cameraRayOrigin, cameraRayDir;
	generateCameraRay(targetTexel, cameraRayOrigin, cameraRayDir);

#if TRACE_MODE == TRACE_AMBIENT_OCCLUSION
	float ambientOcclusion = traceAmbientOcclusion(targetTexel, cameraRayOrigin, cameraRayDir);

	float prevAmbientOcclusion, historyCount;
	if (pathTracingUniform.bInvalidateHistory == 0)
	{
		prevAmbientOcclusion = renderTarget[targetTexel].x;
		historyCount = renderTarget[targetTexel].w;
	}
	else
	{
		prevAmbientOcclusion = 0.0;
		historyCount = 0;
	}

	ambientOcclusion = lerp(prevAmbientOcclusion, ambientOcclusion, 1.0 / (1.0 + historyCount));
	historyCount += 1;

	renderTarget[targetTexel] = float4(ambientOcclusion.xxx, historyCount);
#elif TRACE_MODE == TRACE_DIFFUSE_GI

	float3 Li = traceIncomingRadiance(targetTexel, cameraRayOrigin, cameraRayDir);
	float3 prevLi;
	float historyCount;
	if (pathTracingUniform.bInvalidateHistory == 0)
	{
		prevLi = renderTarget[targetTexel].xyz;
		historyCount = renderTarget[targetTexel].w;
	}
	else
	{
		prevLi = 0;
		historyCount = 0;
	}
	Li = lerp(prevLi, Li, 1.0 / (1.0 + historyCount));
	historyCount += 1;

	renderTarget[targetTexel] = float4(Li, historyCount);
#endif
}

[shader("closesthit")]
void MainClosestHit(inout RayPayload payload, in IntersectionAttributes attr)
{
	uint objectID = g_closestHitCB.objectID;

	GPUSceneItem sceneItem = gpuSceneBuffer[objectID];
	
	// Get the base index of the triangle's first 32 bit index.
	uint triangleIndexStride = 3 * 4; // 4 = sizeof(uint32)
	uint baseIndex = PrimitiveIndex() * triangleIndexStride;
	baseIndex += sceneItem.indexBufferOffset;
	uint3 indices = gIndexBuffer.Load<uint3>(baseIndex);

	// position = float3 = 12 bytes
	float3 p0 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.x);
	float3 p1 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.y);
	float3 p2 = gVertexBuffer.Load<float3>(sceneItem.positionBufferOffset + 12 * indices.z);
	// (normal, texcoord) = (float3, float2) = total 20 bytes
	VertexAttributes v0 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.x);
	VertexAttributes v1 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.y);
	VertexAttributes v2 = gVertexBuffer.Load<VertexAttributes>(sceneItem.nonPositionBufferOffset + 20 * indices.z);

	float3 barycentrics = float3(
		1 - attr.barycentrics.x - attr.barycentrics.y,
		attr.barycentrics.x,
		attr.barycentrics.y);
	
	float2 texcoord = barycentrics.x * v0.texcoord
		+ barycentrics.y * v1.texcoord
		+ barycentrics.z * v2.texcoord;

	Material material = materials[objectID];
	Texture2D albedoTex = albedoTextures[NonUniformResourceIndex(material.albedoTextureIndex)];

	float3 surfaceNormal = normalize(
		barycentrics.x * v0.normal
		+ barycentrics.y * v1.normal
		+ barycentrics.z * v2.normal);
	surfaceNormal = normalize(mul(float4(surfaceNormal, 0.0), sceneItem.modelMatrix).xyz);
	// Hmm if hit the back face I should flip surfaceNormal but how to know it?
	payload.surfaceNormal = surfaceNormal;

	payload.roughness = material.roughness;
	payload.emission = material.emission;

	payload.albedo = albedoTex.SampleLevel(albedoSampler, texcoord, 0.0).rgb;
	payload.albedo *= material.albedoMultiplier.rgb;

	payload.hitTime = RayTCurrent();
	payload.objectID = objectID;
}

[shader("miss")]
void MainMiss(inout RayPayload payload)
{
	payload.objectID = OBJECT_ID_NONE;
	payload.hitTime = -1.0;
}
