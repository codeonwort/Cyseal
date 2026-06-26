#include "common.hlsl"

// ---------------------------------------------------------

#define MAX_HISTORY 64

struct PassUniform
{
	uint2  screenSize;
	float2 invScreenSize;
	uint   bInvalidateHistory;
	uint   bLimitHistory;
	uint   _pad0;
	uint   _pad1;
};

// ---------------------------------------------------------
// Shader parameters

ConstantBuffer<SceneUniform> sceneUniform;
ConstantBuffer<PassUniform>  passUniform;
Texture2D                    sceneDepthTexture;
Texture2D                    raytracingTexture0; // Direct lighting component of raytracing kernel
Texture2D                    raytracingTexture1; // Indirect lighting component of raytracing kernel
Texture2D                    velocityMapTexture;
Texture2D                    prevSceneDepthTexture;
Texture2D                    prevDirectColorTexture;
Texture2D                    prevDirectMomentTexture;
Texture2D                    prevGiColorTexture;
Texture2D                    prevGiMomentTexture;
RWTexture2D<float4>          currentDirectColorTexture;
RWTexture2D<float4>          currentDirectMomentTexture;
RWTexture2D<float4>          currentGiColorTexture;
RWTexture2D<float4>          currentGiMomentTexture;

SamplerState linearSampler : register(s0, space0);
SamplerState pointSampler  : register(s1, space0);

// ---------------------------------------------------------
// Shader

struct PrevFrameInfo
{
	bool   bValid;
	float3 positionWS;
	float  linearDepth;
	float3 directLighting;
	float  directHistoryCount;
	float2 directMoments;
	float3 giLighting;
	float  giHistoryCount;
	float2 giMoments;
};

float2 getScreenUV(uint2 texel)
{
	return (float2(texel) + float2(0.5, 0.5)) * passUniform.invScreenSize;
}

float getLuminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

PrevFrameInfo getReprojectedInfo(float2 currentScreenUV)
{
	float2 velocity = velocityMapTexture.SampleLevel(pointSampler, currentScreenUV, 0).xy;
	float2 screenUV = currentScreenUV - velocity;
	float4 positionCS = textureUVToClipSpace(screenUV);
	
	PrevFrameInfo info;
	if (uvOutOfBounds(screenUV))
	{
		info.bValid = false;
		return info;
	}

	float sceneDepth        = prevSceneDepthTexture.SampleLevel(pointSampler, screenUV, 0).x;
	float3 directLighting   = prevDirectColorTexture.SampleLevel(linearSampler, screenUV, 0).xyz;
	float4 directMoments    = prevDirectMomentTexture.SampleLevel(pointSampler, screenUV, 0);
	float3 giLighting       = prevGiColorTexture.SampleLevel(linearSampler, screenUV, 0).xyz;
	float4 giMoments        = prevGiMomentTexture.SampleLevel(pointSampler, screenUV, 0);

	info.bValid             = true;
	info.positionWS         = clipSpaceToWorldSpace(positionCS, sceneUniform.prevViewProjInvMatrix);
	info.linearDepth        = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix); // Assume projInv is invariant
	info.directLighting     = directLighting;
	info.directHistoryCount = directMoments.z;
	info.directMoments      = directMoments.xy;
	info.giLighting         = giLighting;
	info.giHistoryCount     = giMoments.z;
	info.giMoments          = giMoments.xy;
	
	return info;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	if (any(tid.xy >= passUniform.screenSize))
	{
		return;
	}
	
	uint2 texel = tid.xy;
	float2 screenUV = getScreenUV(texel);

	float sceneDepth = sceneDepthTexture.Load(int3(texel, 0)).r;
	float3 positionWS = getWorldPositionFromSceneDepth(screenUV, sceneDepth, sceneUniform.viewProjInvMatrix);
	float linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix);

	float3 directLighting = raytracingTexture0.Load(int3(texel, 0)).xyz;
	float  directHistoryCount = 0;
	float2 directMoments = 0;
	
	float3 giLighting = raytracingTexture1.Load(int3(texel, 0)).xyz;
	float  giHistoryCount = 0;
	float2 giMoments = 0;
	
	if (sceneDepth != DEVICE_Z_FAR)
	{
		directMoments.x = getLuminance(directLighting);
		directMoments.y = directMoments.x * directMoments.x;
		
		giMoments.x = getLuminance(giLighting);
		giMoments.y = giMoments.x * giMoments.x;
		
		// Temporal reprojection
		PrevFrameInfo prevFrame = getReprojectedInfo(screenUV);
		bool bTemporalReprojection = false;
		if (passUniform.bInvalidateHistory == 0)
		{
			float depthDiff = abs(prevFrame.linearDepth - linearDepth) / linearDepth;
			bTemporalReprojection = prevFrame.bValid && depthDiff <= 0.03;
		}
		
		float3 prevDirectLighting = 0;
		float2 prevDirectMoments = 0;
		float3 prevGiLighting = 0;
		float2 prevGiMoments = 0;
		if (bTemporalReprojection)
		{
			directHistoryCount = prevFrame.directHistoryCount;
			prevDirectLighting = prevFrame.directLighting;
			prevDirectMoments  = prevFrame.directMoments;
			
			giHistoryCount     = prevFrame.giHistoryCount;
			prevGiLighting     = prevFrame.giLighting;
			prevGiMoments      = prevFrame.giMoments;
		}
		
		directLighting = lerp(prevDirectLighting, directLighting, 1.0 / (1.0 + directHistoryCount));
		directMoments = lerp(prevDirectMoments, directMoments, 1.0 / (1.0 + directHistoryCount));
		directHistoryCount += 1;
		
		giLighting = lerp(prevGiLighting, giLighting, 1.0 / (1.0 + giHistoryCount));
		giMoments = lerp(prevGiMoments, giMoments, 1.0 / (1.0 + giHistoryCount));
		giHistoryCount += 1;
		
		if (passUniform.bLimitHistory != 0)
		{
			directHistoryCount = min(directHistoryCount, MAX_HISTORY);
			giHistoryCount = min(giHistoryCount, MAX_HISTORY);
		}
	}

	//variance = max(0.0, moments.y - moments.x * moments.x);
	
	currentDirectColorTexture[texel]  = float4(directLighting, 1);
	currentDirectMomentTexture[texel] = float4(directMoments, directHistoryCount, 1);
	currentGiColorTexture[texel]      = float4(giLighting, 1);
	currentGiMomentTexture[texel]     = float4(giMoments, giHistoryCount, 1);
}
