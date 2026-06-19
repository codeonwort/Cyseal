#include "common.hlsl"

// ---------------------------------------------------------

#define MAX_HISTORY 64

struct PassUniform
{
	uint2  prevScreenSize;
	float2 prevInvScreenSize;
};

// ---------------------------------------------------------
// Shader parameters

ConstantBuffer<SceneUniform> sceneUniform;
ConstantBuffer<PassUniform>  passUniform;
Texture2D                    sceneDepthTexture;
Texture2D                    raytracingTexture;
Texture2D                    prevSceneDepthTexture;
Texture2D                    prevColorTexture;
Texture2D                    prevMomentTexture;
Texture2D                    velocityMapTexture;
RWTexture2D<float4>          currentColorTexture;
RWTexture2D<float4>          currentMomentTexture;

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

// [0,0] ~ (1,1)
float2 getUnscaledScreenUV(uint2 texel)
{
	return (float2(texel) + float2(0.5, 0.5)) * sceneUniform.screenResolution.zw;
}

float getLuminance(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

PrevFrameInfo getReprojectedInfo(float2 unscaledCurrentScreenUV, float2 currentScreenUV, float currentLinearDepth, float3 currentPositionWS)
{
	float2 velocity = velocityMapTexture.SampleLevel(pointSampler, currentScreenUV, 0).rg;
	float2 unscaledScreenUV = unscaledCurrentScreenUV - velocity;
	float2 fUnscaledTexel = unscaledScreenUV * float2(passUniform.prevScreenSize);
	float2 screenUV = fUnscaledTexel * sceneUniform.unscaledScreenResolution.zw;

	float3 color = prevColorTexture.SampleLevel(linearSampler, screenUV, 0).xyz;
	
	PrevFrameInfo info;
	info.bValid = false;
	
	if (uvOutOfBounds(screenUV))
	{
		return info;
	}
	
	float2 tabDir = step(frac(fUnscaledTexel), float2(0.5, 0.5)) - 0.5;
	float2 tabDeltaUV = tabDir * passUniform.prevInvScreenSize;

	for (int dy = 0; dy <= 1; dy++)
	{
		for (int dx = 0; dx <= 1; dx++)
		{
			float2 tabUV = screenUV + float2(dx, dy) * tabDeltaUV;
			float2 unscaledTabUV = tabUV * float2(passUniform.prevScreenSize) * sceneUniform.unscaledScreenResolution.zw;
			
			float sceneDepth = prevSceneDepthTexture.SampleLevel(pointSampler, tabUV, 0).x;
			// Use unscaled UV because projInv was built without knowing resolution scaling.
			// Also assume projInv is same for prev and curr frames.
			float linearDepth = getLinearDepth(unscaledTabUV, sceneDepth, sceneUniform.projInvMatrix);
			float4 moments = prevMomentTexture.SampleLevel(pointSampler, tabUV, 0);
			
			float3 positionWS = getWorldPositionFromSceneDepth(unscaledTabUV, sceneDepth, sceneUniform.prevViewProjInvMatrix);
			
			float depthDiff = abs(linearDepth - currentLinearDepth) / currentLinearDepth;
			bool bReproject = (length(positionWS - currentPositionWS) <= 0.05) || (depthDiff <= 0.03);

			if (bReproject)
			{
				info.bValid = true;
				info.positionWS = positionWS;
				info.linearDepth = linearDepth;
				info.color = color;
				info.historyCount = moments.z;
				info.moments = moments.xy;
				
				break;
			}
		}
	}
	
	return info;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	if (any(tid.xy >= sceneUniform.screenResolution.xy))
	{
		return;
	}
	
	uint2 texel = tid.xy;
	float2 unscaledScreenUV = getUnscaledScreenUV(texel);
	float2 screenUV = unscaledScreenUV * sceneUniform.screenResolution.xy * sceneUniform.unscaledScreenResolution.zw;

	float sceneDepth = sceneDepthTexture.Load(int3(texel, 0)).x;
	float3 positionWS = getWorldPositionFromSceneDepth(unscaledScreenUV, sceneDepth, sceneUniform.viewProjInvMatrix);
	float linearDepth = getLinearDepth(unscaledScreenUV, sceneDepth, sceneUniform.projInvMatrix);

	float3 Wo = raytracingTexture.Load(int3(texel, 0)).xyz;
	float historyCount = 0;
	float2 moments = 0;
	if (sceneDepth != DEVICE_Z_FAR)
	{
		moments.x = getLuminance(Wo);
		moments.y = moments.x * moments.x;
		
		PrevFrameInfo prevFrame = getReprojectedInfo(unscaledScreenUV, screenUV, linearDepth, positionWS);
		
		float3 prevWo = 0;
		float2 prevMoments = 0;
		if (prevFrame.bValid)
		{
			historyCount = prevFrame.historyCount;
			prevWo = prevFrame.color;
			prevMoments = prevFrame.moments;
		}
		
		Wo = lerp(prevWo, Wo, 1.0 / (1.0 + historyCount));
		moments = lerp(prevMoments, moments, 1.0 / (1.0 + historyCount));
		
		historyCount = min(historyCount + 1, MAX_HISTORY);
	}
	
	//variance = max(0.0, moments.y - moments.x * moments.x);
	
	currentColorTexture[texel] = float4(Wo, 1);
	currentMomentTexture[texel] = float4(moments, historyCount, 1);
}
