#include "common.hlsl"

// References
// - [Holger Dammertz] Edge Avoiding A-Trous Wavelet Transform for fast Global Illumination Filtering
// - [AMD FidelityFX-Denoiser] Temporal reprojection logic: https://github.com/GPUOpen-Effects/FidelityFX-Denoiser/blob/master/ffx-shadows-dnsr/ffx_denoiser_shadows_tileclassification.h

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint stepWidth;
};

struct BlurUniform
{
    float4 kernelAndOffset[25]; // (kernel, offsetX, offsetY, _unused)
    float cPhi;
    float nPhi;
    float pPhi;
    float _pad0;
    uint textureWidth;
    uint textureHeight;
    uint bSkipBlur;
    uint _pad2;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants>  pushConstants;

ConstantBuffer<SceneUniform>   sceneUniform;
ConstantBuffer<BlurUniform>    blurUniform;
Texture2D<GBUFFER0_DATATYPE>   inGBuffer0Texture;
Texture2D<GBUFFER1_DATATYPE>   inGBuffer1Texture;
Texture2D<float>               inDepthTexture;
RWTexture2D<float4>            inColorTexture;
RWTexture2D<float>             inVarianceTexture;
RWTexture2D<float4>            outColorTexture;
RWTexture2D<float>             outVarianceTexture;

// ------------------------------------------------------------------------
// Compute shader

float2 getScreenResolution()
{
    return float2(blurUniform.textureWidth, blurUniform.textureHeight);
}

float2 getScreenUV(uint2 texel)
{
    return (float2(texel) + float2(0.5, 0.5)) / getScreenResolution();
}

int2 clampTexel(int2 texel)
{
    return clamp(texel, int2(0, 0), int2(blurUniform.textureWidth - 1, blurUniform.textureHeight - 1));
}

float3 getWorldPosition(uint2 tid)
{
    float sceneDepth = inDepthTexture.Load(int3(tid.xy, 0)).r;
    float2 uv = getScreenUV(tid.xy);
    float4 positionCS = getPositionCS(uv, sceneDepth);
    return clipSpaceToWorldSpace(positionCS, sceneUniform.viewProjInvMatrix);
}

// Load input variance with 3x3 Gaussian filter.
float loadVariance3x3(int2 texel)
{
	const int2 offsets[9] =
	{
		{ -1, -1 }, { 0, -1 }, { +1, -1 },
		{ -1,  0 }, { 0,  0 }, { +1,  0 },
		{ -1, +1 }, { 0, +1 }, { +1, +1 },
	};
	const float weights[9] =
	{
		1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0,
		2.0 / 16.0, 4.0 / 16.0, 2.0 / 16.0,
		1.0 / 16.0, 2.0 / 16.0, 1.0 / 16.0,
	};
	
	float v = 0;
	for (int i = 0; i < 9; i++)
	{
		int2 xy = clampTexel(texel + offsets[i]);
		v += weights[i] * inVarianceTexture.Load(int3(xy, 0)).x;
	}
	return v;
}

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= blurUniform.textureWidth || tid.y >= blurUniform.textureHeight)
    {
        return;
    }

    GBUFFER0_DATATYPE gbuffer0Data = inGBuffer0Texture.Load(int3(tid.xy, 0));
    GBUFFER1_DATATYPE gbuffer1Data = inGBuffer1Texture.Load(int3(tid.xy, 0));
    GBufferData gbufferData = decodeGBuffers(gbuffer0Data, gbuffer1Data);

    float2 resolution = getScreenResolution();
    float stepWidth = float(pushConstants.stepWidth);

    float2 uv0     = (float2(tid.xy) + float2(0.5, 0.5)) / resolution;
    float3 color0  = inColorTexture[tid.xy].xyz;
	float  var0    = loadVariance3x3(int2(tid.xy));
    float3 albedo0 = gbufferData.albedo;
    float3 normal0 = gbufferData.normalWS;
    float3 pos0    = getWorldPosition(tid.xy);

    if (blurUniform.bSkipBlur != 0)
    {
        outColorTexture[tid.xy] = float4(color0, 1.0);
        return;
    }

    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int i = 0; i < 25; ++i)
    {
        float4 params = blurUniform.kernelAndOffset[i];
        float kernel = params.x;
        float2 offset = params.yz;

        float3 diff; float distSq;

        int2 neighborTexel = clampTexel(int2(float2(tid.xy) + offset * stepWidth));

        GBUFFER0_DATATYPE neighborGBuffer0Data = inGBuffer0Texture.Load(int3(neighborTexel, 0));
        GBUFFER1_DATATYPE neighborGBuffer1Data = inGBuffer1Texture.Load(int3(neighborTexel, 0));
        GBufferData neighborGBufferData = decodeGBuffers(neighborGBuffer0Data, neighborGBuffer1Data);

        float3 color1  = inColorTexture[neighborTexel].xyz;
        float3 albedo1 = neighborGBufferData.albedo;
        float3 normal1 = neighborGBufferData.normalWS;
        float3 pos1    = getWorldPosition(neighborTexel);

		// #wip: Change luminance weight according to SVGF paper
#define SVGF_WEIGHTS 1
#if SVGF_WEIGHTS
		float luminanceDiff = length(color0 - color1);
		float colorWeight = exp(-1 * luminanceDiff / (blurUniform.cPhi * sqrt(var0) + 0.01));
#else
        diff = color0 - color1;
        distSq = dot(diff, diff);
        float colorWeight = min(1.0, exp(-distSq / blurUniform.cPhi));
#endif

#if SVGF_WEIGHTS
		float albedoWeight = 1;
#else
        diff = albedo0 - albedo1;
        distSq = max(0.0, dot(diff, diff));
        float albedoWeight = min(1.0, exp(-distSq / blurUniform.cPhi));
#endif

#if (0 && SVGF_WEIGHTS)
		// #wip: Produces NaN?
		float normalWeight = pow(max(0.0, dot(normal0, normal1)), blurUniform.nPhi);
#else
        diff = normal0 - normal1;
        distSq = max(0.0, dot(diff, diff) / (stepWidth * stepWidth));
        float normalWeight = min(1.0, exp(-distSq / blurUniform.nPhi));
#endif

#if (0 && SVGF_WEIGHTS)
		// #wip: Change depth weight according to SVGF paper
#else
        diff = pos0 - pos1;
        distSq = dot(diff, diff);
        float posWeight = min(1.0, exp(-distSq / blurUniform.pPhi));
#endif

        float weight = colorWeight * albedoWeight * normalWeight * posWeight;
        sum += color1 * weight * kernel;
        weightSum += weight * kernel;
    }
    float3 finalColor = sum / weightSum;

    outColorTexture[tid.xy] = float4(finalColor, 1.0);
	
	// #wip: Filter next variance
	outVarianceTexture[tid.xy] = var0;
}
