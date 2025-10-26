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
Texture2D                    raytracingTexture;
Texture2D                    velocityMapTexture;
Texture2D                    prevSceneDepthTexture;
Texture2D                    prevColorTexture;
Texture2D                    prevMomentTexture;
Texture2D                    prevSampleCountTexture;
RWTexture2D<float4>          currentColorTexture;
RWTexture2D<float2>          currentMomentTexture;
RWTexture2D<float>           currentSampleCountTexture;

SamplerState linearSampler : register(s0, space0);
SamplerState pointSampler  : register(s1, space0);

// ---------------------------------------------------------
// Shader

struct PrevFrameInfo
{
	bool   bValid;
	float3 positionWS;
	float  linearDepth;
	float3 color;
	float  historyCount;
	float2 moments;
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

	float sceneDepth = prevSceneDepthTexture.SampleLevel(pointSampler, screenUV, 0).x;
	float3 color = prevColorTexture.SampleLevel(linearSampler, screenUV, 0).xyz;
	float2 moments = prevMomentTexture.SampleLevel(pointSampler, screenUV, 0).xy;
	float sampleCount = prevSampleCountTexture.SampleLevel(pointSampler, screenUV, 0).x;

	info.bValid = true;
	info.positionWS = clipSpaceToWorldSpace(positionCS, sceneUniform.prevViewProjInvMatrix);
	info.linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix); // Assume projInv is invariant
	info.color = color;
	info.historyCount = sampleCount;
	info.moments = moments;
	
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

	float3 Wo = raytracingTexture.Load(int3(texel, 0)).xyz;
	float historyCount = 0;
	float2 moments = 0;
	if (sceneDepth != DEVICE_Z_FAR)
	{
		moments.x = getLuminance(Wo);
		moments.y = moments.x * moments.x;
		
		// Temporal reprojection
		PrevFrameInfo prevFrame = getReprojectedInfo(screenUV);
		bool bTemporalReprojection = false;
		if (passUniform.bInvalidateHistory == 0)
		{
			float depthDiff = abs(prevFrame.linearDepth - linearDepth) / linearDepth;
			bTemporalReprojection = prevFrame.bValid && depthDiff <= 0.03;
		}
		
		float3 prevWo = 0;
		float2 prevMoments = 0;
		if (bTemporalReprojection)
		{
			historyCount = prevFrame.historyCount;
			prevWo = prevFrame.color;
			prevMoments = prevFrame.moments;
		}
		
		Wo = lerp(prevWo, Wo, 1.0 / (1.0 + historyCount));
		moments = lerp(prevMoments, moments, 1.0 / (1.0 + historyCount));
		
		historyCount += 1;
		if (passUniform.bLimitHistory != 0)
		{
			historyCount = min(historyCount, MAX_HISTORY);
		}
	}

	//variance = max(0.0, moments.y - moments.x * moments.x);
	
	currentColorTexture[texel] = float4(Wo, 1);
	currentMomentTexture[texel] = moments;
	currentSampleCountTexture[texel] = historyCount;
}
