#include "common.hlsl"

// ---------------------------------------------------------

#define MAX_HISTORY 64

struct PassUniform
{
	uint2  screenSize;
	float2 invScreenSize;
};

// ---------------------------------------------------------
// Shader parameters

ConstantBuffer<SceneUniform> sceneUniform;
ConstantBuffer<PassUniform>  passUniform;
Texture2D                    sceneDepthTexture;
Texture2D                    raytracingTexture;
Texture2D                    prevSceneDepthTexture;
Texture2D                    prevColorTexture;
Texture2D                    velocityMapTexture;
RWTexture2D<float4>          currentColorTexture;

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
};

float2 getScreenUV(uint2 texel)
{
	return (float2(texel) + float2(0.5, 0.5)) * passUniform.invScreenSize;
}

PrevFrameInfo getReprojectedInfo(float2 currentScreenUV)
{
	float2 velocity = velocityMapTexture.SampleLevel(pointSampler, currentScreenUV, 0).rg;
	float2 screenUV = currentScreenUV - velocity;
	float4 positionCS = textureUVToClipSpace(screenUV);
	
	PrevFrameInfo info;
	if (uvOutOfBounds(screenUV))
	{
		info.bValid = false;
		return info;
	}

	float sceneDepth = prevSceneDepthTexture.SampleLevel(pointSampler, screenUV, 0).r;
	float4 colorAndHistory = prevColorTexture.SampleLevel(linearSampler, screenUV, 0);

	info.bValid = true;
	info.positionWS = clipSpaceToWorldSpace(positionCS, sceneUniform.prevViewProjInvMatrix);
	info.linearDepth = getLinearDepth(screenUV, sceneDepth, sceneUniform.projInvMatrix); // Assume projInv is invariant
	info.color = colorAndHistory.rgb;
	info.historyCount = colorAndHistory.a; // #todo-diffuse: history is bilinear sampled...
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

	float3 Wo = 0;
	float historyCount = 0;
	if (sceneDepth != DEVICE_Z_FAR)
	{
		Wo = raytracingTexture.Load(int3(texel, 0)).xyz;
		
		// Temporal reprojection
		PrevFrameInfo prevFrame = getReprojectedInfo(screenUV);
		bool bTemporalReprojection = false;
		{
			float depthDiff = abs(prevFrame.linearDepth - linearDepth) / linearDepth;
			bTemporalReprojection = prevFrame.bValid && depthDiff <= 0.03;
		}
		
		float3 prevWo = 0;
		if (bTemporalReprojection)
		{
			historyCount = prevFrame.historyCount;
			prevWo = prevFrame.color;
		}
		
		Wo = lerp(prevWo, Wo, 1.0 / (1.0 + historyCount));
		historyCount = min(historyCount + 1, MAX_HISTORY);
	}
	
	// #todo-diffuse: Should store history in moment texture
	currentColorTexture[texel] = float4(Wo, historyCount);
}
