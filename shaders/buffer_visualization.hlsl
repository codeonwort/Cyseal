#include "common.hlsl"

// Should match with EBufferVisualizationMode
#define MODE_NONE               0
#define MODE_MATERIAL_ID        1
#define MODE_ALBEDO             2
#define MODE_ROUGHNESS          3
#define MODE_METAL_MASK         4
#define MODE_NORMAL             5
#define MODE_DIRECT_LIGHTING    6
#define MODE_RAY_TRACED_SHADOWS 7
#define MODE_INDIRECT_DIFFUSE   8
#define MODE_INDIRECT_SPECULAR  9
#define MODE_VELOCITY_MAP       10
#define MODE_VISIBILITY_BUFFER  11
#define MODE_BARYCENTRIC_COORD  12

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint modeEnum;
	uint width;
	uint height;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants : register(b0, space0);

Texture2D<GBUFFER0_DATATYPE> gbuffer0       : register(t0, space0);
Texture2D<GBUFFER1_DATATYPE> gbuffer1       : register(t1, space0);
Texture2D sceneColor                        : register(t2, space0);
Texture2D shadowMask                        : register(t3, space0);
Texture2D indirectDiffuse                   : register(t4, space0);
Texture2D indirectSpecular                  : register(t5, space0);
Texture2D velocityMap                       : register(t6, space0);
Texture2D<uint> visibilityBuffer            : register(t7, space0);
Texture2D barycentricCoord                  : register(t8, space0);

SamplerState textureSampler                 : register(s0, space0);

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

	uint modeEnum = pushConstants.modeEnum;
	float4 color = float4(0.0, 0.0, 0.0, 1.0);

	GBUFFER0_DATATYPE encodedGBuffer0 = gbuffer0.Load(int3(interpolants.posH.xy, 0));
	GBUFFER1_DATATYPE encodedGBuffer1 = gbuffer1.Load(int3(interpolants.posH.xy, 0));
	GBufferData gbufferData = decodeGBuffers(encodedGBuffer0, encodedGBuffer1);

	if (modeEnum == MODE_MATERIAL_ID)
	{
		uint id = gbufferData.materialID;
		float x = float((id * (id + 17) * (id + 15)) & 0xFF) / 255.0;
		float y = float(((id + 4) * (id + 13) * (id + 11)) & 0xFF) / 255.0;
		float z = float(((id * 5) * (id + 21) * (id + 19)) & 0xFF) / 255.0;
		color.rgb = float3(x, y, z);
	}
	else if (modeEnum == MODE_ALBEDO)
	{
		color.rgb = gbufferData.albedo;
	}
	else if (modeEnum == MODE_ROUGHNESS)
	{
		color.rgb = gbufferData.roughness.xxx;
	}
	else if (modeEnum == MODE_METAL_MASK)
	{
		color.rgb = gbufferData.metalMask.xxx;
	}
	else if (modeEnum == MODE_NORMAL)
	{
		color.rgb = 0.5 * (1.0 + gbufferData.normalWS);
	}
	else if (modeEnum == MODE_DIRECT_LIGHTING)
	{
		color.rgb = sceneColor.SampleLevel(textureSampler, screenUV, 0.0).rgb;
	}
	else if (modeEnum == MODE_RAY_TRACED_SHADOWS)
	{
		color.rgb = shadowMask.SampleLevel(textureSampler, screenUV, 0.0).rrr;
	}
	else if (modeEnum == MODE_INDIRECT_DIFFUSE)
	{
		color.rgb = indirectDiffuse.SampleLevel(textureSampler, screenUV, 0.0).rgb;
	}
	else if (modeEnum == MODE_INDIRECT_SPECULAR)
	{
		color.rgb = indirectSpecular.SampleLevel(textureSampler, screenUV, 0.0).rgb;
	}
	else if (modeEnum == MODE_VELOCITY_MAP)
	{
		float2 vel = velocityMap.SampleLevel(textureSampler, screenUV, 0.0).rg;
		color.rg = 50.0 * abs(vel);
	}
	else if (modeEnum == MODE_VISIBILITY_BUFFER)
	{
		int2 coord = int2(screenUV * float2(pushConstants.width, pushConstants.height));
		uint visPacked = visibilityBuffer.Load(int3(coord, 0)).r;
		VisibilityBufferData vdata = decodeVisibilityBuffer(visPacked);
		// My no brainer hash function
		uint R = (((vdata.primitiveID << 2) + 71) ^ 306) & 0xff;
		uint G = (((vdata.primitiveID << 3) * 31) + 141) & 0xff;
		uint B = ((0xff - R) ^ (0xff - G)) & 0xff;
		color.rgb = float3(R, G, B) / 255.0;
	}
	else if (modeEnum == MODE_BARYCENTRIC_COORD)
	{
		float2 bary = barycentricCoord.SampleLevel(textureSampler, screenUV, 0.0).rg;
		if (bary.x < 0 || bary.y < 0)
		{
			color.rgb = float3(255, 194, 205) / 255.0;
		}
		else
		{
			color.rg = bary;
		}
	}

	// Gamma correction
	//float gamma = 1.0 / GAMMA_CORRECTION;
	//color.rgb = pow(color.rgb, float3(gamma, gamma, gamma));

	return color;
}
