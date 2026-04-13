// Simply blit the final scene color to the final render target (a swapchain image or a texture).

#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

ConstantBuffer<SceneUniform> sceneUniform;
Texture2D                    sourceTexture;
SamplerState                 sourceTextureSampler;

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

	float4 color = sourceTexture.SampleLevel(sourceTextureSampler, screenUV, 0);
	return color;
}
