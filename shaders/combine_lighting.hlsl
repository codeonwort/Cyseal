#include "common.hlsl"
#include "bsdf.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

ConstantBuffer<SceneUniform> sceneUniform;
Texture2D                    sceneDepth;
Texture2D<GBUFFER0_DATATYPE> gbuffer0;
Texture2D<GBUFFER1_DATATYPE> gbuffer1;
Texture2D                    indirectDiffuse;
Texture2D                    indirectSpecular;
SamplerState                 pointSampler;

// ------------------------------------------------------------------------
// Vertex shader

struct Interpolants
{
    float4 posH : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Interpolants mainVS(uint vertexID: SV_VertexID)
{
    Interpolants output;

    output.uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.uv.y = 1.0 - output.uv.y;
    output.posH = float4(output.uv * 2.0 + -1.0, 0.0, 1.0);

    return output;
}

// ------------------------------------------------------------------------
// Pixel shader

float4 mainPS(Interpolants interpolants) : SV_TARGET
{
    float2 screenUV = interpolants.uv;
    screenUV.y = 1.0 - screenUV.y;
	screenUV *= sceneUniform.screenResolution.xy * sceneUniform.unscaledScreenResolution.zw;

    float deviceZ = sceneDepth.SampleLevel(pointSampler, screenUV, 0).r;
    float4 positionCS = getPositionCS(screenUV, deviceZ);
    float3 positionWS = clipSpaceToWorldSpace(positionCS, sceneUniform.viewProjInvMatrix);
    float3 viewDirection = normalize(positionWS - sceneUniform.cameraPosition.xyz);

    GBUFFER0_DATATYPE gbuffer0Data = gbuffer0.Load(int3(interpolants.posH.xy, 0));
    GBUFFER1_DATATYPE gbuffer1Data = gbuffer1.Load(int3(interpolants.posH.xy, 0));
    GBufferData gbufferData = decodeGBuffers(gbuffer0Data, gbuffer1Data);

    float NdotV = max(0.0, dot(-viewDirection, gbufferData.normalWS));

    const float3 F0 = lerp(0.04, gbufferData.albedo, gbufferData.metalMask);
    const float3 F = fresnelSchlickRoughness(NdotV, F0, gbufferData.roughness);
    const float3 kS = F;
    const float3 kD = (1.0 - gbufferData.metalMask) * (1.0 - kS);

	float4 color = (0, 0, 0, 0);

    color.rgb += kD * gbufferData.albedo * indirectDiffuse.SampleLevel(pointSampler, screenUV, 0).rgb;
    
	// #todo-specular: Apply specular coefficient?
    color.rgb += indirectSpecular.SampleLevel(pointSampler, screenUV, 0).rgb;

    return color;
}
