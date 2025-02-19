#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

ConstantBuffer<SceneUniform> sceneUniform : register(b0, space0);
TextureCube skybox                        : register(t0, space0);
SamplerState skyboxSampler                : register(s0, space0);

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

    output.posH = float4(output.uv * 2.0 - 1.0, DEVICE_Z_FAR, 1.0);

    return output;
}

// ------------------------------------------------------------------------
// Pixel shader

// #todo: Ray direction is slightly different than path tracing.
float3 generateCameraRay(float2 uv)
{
	float2 screenPos = uv * 2.0 - 1.0;

	float4 worldPos = mul(float4(screenPos, 0.0, 1.0), sceneUniform.viewProjInvMatrix);
	worldPos.xyz /= worldPos.w;

	return normalize(worldPos.xyz);
}

float4 mainPS(Interpolants interpolants) : SV_TARGET
{
	float3 dir = generateCameraRay(interpolants.uv);
	float3 color = skybox.SampleLevel(skyboxSampler, dir, 0).rgb;

    return float4(color, 0);
}
