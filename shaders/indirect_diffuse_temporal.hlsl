#include "common.hlsl"

// ---------------------------------------------------------

#define MAX_HISTORY 64

struct PassUniform
{
    uint2  screenSize;
    float2 invScreenSize;
};

// ---------------------------------------------------------
// Global root signature

ConstantBuffer<SceneUniform> sceneUniform          : register(b0, space0);
ConstantBuffer<PassUniform>  passUniform           : register(b1, space0);
Texture2D                    sceneDepthTexture     : register(t0, space0);
Texture2D                    raytracingTexture     : register(t1, space0);
Texture2D                    prevSceneDepthTexture : register(t2, space0);
Texture2D                    prevColorTexture      : register(t3, space0);
Texture2D                    velocityMapTexture    : register(t4, space0);
RWTexture2D<float4>          currentColorTexture   : register(u0, space0);

// Samplers
SamplerState linearSampler : register(s2, space0);
SamplerState pointSampler  : register(s3, space0);

// ---------------------------------------------------------

float2 getScreenUV(uint2 texel)
{
    return (float2(texel) + float2(0.5, 0.5)) * passUniform.invScreenSize;
}

// ---------------------------------------------------------
// Shader stages

struct PrevFrameInfo
{
	bool bValid;
	float3 positionWS;
	float linearDepth;
	float3 color;
	float historyCount;
};

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
	if (sceneDepth == DEVICE_Z_FAR)
	{
		currentColorTexture[texel] = float4(Wo, 1.0);
	}
    else
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
