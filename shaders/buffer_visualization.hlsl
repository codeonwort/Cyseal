#include "common.hlsl"

// Should match with EBufferVisualizationMode
#define MODE_NONE                0
#define MODE_MATERIAL_ID         1
#define MODE_ALBEDO              2
#define MODE_ROUGHNESS           3
#define MODE_METAL_MASK          4
#define MODE_NORMAL              5
#define MODE_DIRECT_LIGHTING     6
#define MODE_RAY_TRACED_SHADOWS  7
#define MODE_INDIRECT_DIFFUSE    8
#define MODE_INDIRECT_SPECULAR   9
#define MODE_VELOCITY_MAP        10
#define MODE_VISIBILITY_BUFFER   11
#define MODE_BARYCENTRIC_COORD   12
#define MODE_VIS_MATERIAL_ID     13
#define MODE_VIS_ALBEDO          14
#define MODE_VIS_ROUGHNESS       15
#define MODE_VIS_METAL_MASK      16
#define MODE_OPTICAL_FLOW_VECTOR 17
#define MODE_INTERPOLATED_FRAME  18

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint modeEnum;
	uint width;
	uint height;
	uint opticalFlowVectorPackedSize; // low 16-bit: width, high 16-bit: height
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants      : register(b0, space0);

ConstantBuffer<SceneUniform>  sceneUniform;

Texture2D<GBUFFER0_DATATYPE>  gbuffer0           : register(t0, space0);
Texture2D<GBUFFER1_DATATYPE>  gbuffer1           : register(t1, space0);
Texture2D                     sceneColor         : register(t2, space0);
Texture2D                     shadowMask         : register(t3, space0);
Texture2D                     indirectDiffuse    : register(t4, space0);
Texture2D                     indirectSpecular   : register(t5, space0);
Texture2D                     velocityMap        : register(t6, space0);
Texture2D<uint>               visibilityBuffer   : register(t7, space0);
Texture2D                     barycentricCoord   : register(t8, space0);
Texture2D<GBUFFER0_DATATYPE>  visGBuffer0        : register(t9, space0);
Texture2D<GBUFFER1_DATATYPE>  visGBuffer1        : register(t10, space0);
Texture2D<uint>               opticalFlowVectorX : register(t11, space0);
Texture2D<uint>               opticalFlowVectorY : register(t12, space0);
Texture2D                     interpolatedFrame  : register(t13, space0);

SamplerState                  textureSampler     : register(s0, space0);

uint2 unpackOpticalFlowVectorTextureSize()
{
	uint xy = pushConstants.opticalFlowVectorPackedSize;
	return uint2(xy & 0xffff, (xy >> 16) & 0xffff);
}

// ------------------------------------------------------------------------
// From ffx_frameinterpolation_common.h

// #wip: Cleanup.

const uint MOTION_VECTOR_FIELD_ENTRY_BIT_COUNT = 32;

// Make sure all bit counts add up to MOTION_VECTOR_FIELD_ENTRY_BIT_COUNT
const uint MOTION_VECTOR_FIELD_VECTOR_COEFFICIENT_BIT_COUNT = 16;
const uint MOTION_VECTOR_FIELD_PRIORITY_LOW_BIT_COUNT = 5;
const uint MOTION_VECTOR_FIELD_PRIORITY_HIGH_BIT_COUNT = 10;
const uint MOTION_VECTOR_PRIMARY_VECTOR_INDICATION_BIT_COUNT = 1;

const uint MOTION_VECTOR_FIELD_PRIMARY_VECTOR_INDICATION_BIT = (1U << (MOTION_VECTOR_FIELD_ENTRY_BIT_COUNT - 1));

const uint PRIORITY_LOW_MAX = (1U << MOTION_VECTOR_FIELD_PRIORITY_LOW_BIT_COUNT) - 1;
const uint PRIORITY_HIGH_MAX = (1U << MOTION_VECTOR_FIELD_PRIORITY_HIGH_BIT_COUNT) - 1;

const uint PRIORITY_LOW_OFFSET = MOTION_VECTOR_FIELD_VECTOR_COEFFICIENT_BIT_COUNT;
const uint PRIORITY_HIGH_OFFSET = PRIORITY_LOW_OFFSET + MOTION_VECTOR_FIELD_PRIORITY_LOW_BIT_COUNT;
const uint PRIMARY_VECTOR_INDICATION_OFFSET = PRIORITY_HIGH_OFFSET + MOTION_VECTOR_FIELD_PRIORITY_HIGH_BIT_COUNT;

struct VectorFieldEntry
{
	float2 fMotionVector;
	float  uHighPriorityFactor;
	float  uLowPriorityFactor;
	bool   bValid;
	bool   bPrimary;
	bool   bSecondary;
	bool   bInPainted;
	float  fVelocity;
	bool   bNegOutside;
	bool   bPosOutside;
};

struct BilinearSamplingData
{
	int2 iOffsets[4];
	float fWeights[4];
	int2 iBasePos;
};

VectorFieldEntry NewVectorFieldEntry()
{
	VectorFieldEntry vfe;
	vfe.fMotionVector = float2(0.0, 0.0);
	vfe.uHighPriorityFactor = 0.0;
	vfe.uLowPriorityFactor = 0.0;
	vfe.bValid = false;
	vfe.bPrimary = false;
	vfe.bSecondary = false;
	vfe.bInPainted = false;
	vfe.fVelocity = 0.0;
	vfe.bNegOutside = false;
	vfe.bPosOutside = false;
	return vfe;
}

BilinearSamplingData GetBilinearSamplingData(float2 fUv, int2 iSize)
{
	BilinearSamplingData data;

	float2 fPxSample = (fUv * iSize) - float2(0.5f, 0.5f);
	data.iBasePos = int2(floor(fPxSample));
	float2 fPxFrac = frac(fPxSample);

	data.iOffsets[0] = int2(0, 0);
	data.iOffsets[1] = int2(1, 0);
	data.iOffsets[2] = int2(0, 1);
	data.iOffsets[3] = int2(1, 1);

	data.fWeights[0] = (1 - fPxFrac.x) * (1 - fPxFrac.y);
	data.fWeights[1] = (fPxFrac.x) * (1 - fPxFrac.y);
	data.fWeights[2] = (1 - fPxFrac.x) * (fPxFrac.y);
	data.fWeights[3] = (fPxFrac.x) * (fPxFrac.y);

	return data;
}

int2 DisplaySize()
{
	return int2(pushConstants.width, pushConstants.height);
}

float4 getMotionVectorColor(float2 fMotionVector)
{
	return float4(0.5f + fMotionVector * DisplaySize() * 0.1f, 0.5f, 1.0f);
}

int2 GetOpticalFlowSize()
{
	const float2 opticalFlowScale = 1.0 / float2(DisplaySize());
	const int opticalFlowBlockSize = 8;
	
	int2 iOpticalFlowSize = (1.0f / opticalFlowScale) / float2(opticalFlowBlockSize.xx);

	return iOpticalFlowSize;
}

int2 GetOpticalFlowSize2()
{
	return GetOpticalFlowSize() * 1;
}

bool IsOnScreen(int2 pos, int2 size)
{
	return all((uint2(pos) < uint2(size)));
}

bool IsUvInside(float2 fUv)
{
	return (fUv.x > 0.0f && fUv.x < 1.0f) && (fUv.y > 0.0f && fUv.y < 1.0f);
}

uint2 LoadOpticalFlowFieldMv(int2 iPxSample)
{
	// optical flow pass generates a single int2 texture.
	// frame gen pass takes it and generates two uint textures, each for x and y.
#if 1
	uint packedX = opticalFlowVectorX[iPxSample];
	uint packedY = opticalFlowVectorY[iPxSample];
	return uint2(packedX, packedY);
#else
	uint2 packedXY = opticalFlowVector.Load(int3(iPxSample, 0)).xy;
	return packedXY;
#endif
}

float2 ffxUnpackF32(uint a)
{
	return f16tof32(uint2(a & 0xFFFF, a >> 16));
}

bool PackedVectorFieldEntryIsPrimary(uint packedEntry)
{
	return ((packedEntry & MOTION_VECTOR_FIELD_PRIMARY_VECTOR_INDICATION_BIT) != 0);
}

void UnpackVectorFieldEntries(uint2 packed, out VectorFieldEntry vfElement)
{
	vfElement.uHighPriorityFactor = float((packed.x >> PRIORITY_HIGH_OFFSET) & PRIORITY_HIGH_MAX) / PRIORITY_HIGH_MAX;
	vfElement.uLowPriorityFactor = float((packed.x >> PRIORITY_LOW_OFFSET) & PRIORITY_LOW_MAX) / PRIORITY_LOW_MAX;

	vfElement.bPrimary = PackedVectorFieldEntryIsPrimary(packed.x);
	vfElement.bValid = (vfElement.uHighPriorityFactor > 0.0f);
	vfElement.bSecondary = vfElement.bValid && !vfElement.bPrimary;

	// Reverse priority factor for secondary vectors
	if (vfElement.bSecondary)
	{
		vfElement.uHighPriorityFactor = 1.0f - vfElement.uHighPriorityFactor;
	}

	vfElement.fMotionVector.x = ffxUnpackF32(packed.x).x;
	vfElement.fMotionVector.y = ffxUnpackF32(packed.y).x;
	vfElement.bInPainted = false;
}

void SampleOpticalFlowMotionVectorField(float2 fUv, out VectorFieldEntry vfElement)
{
	const float scaleFactor = 1.0f;

	BilinearSamplingData bilinearInfo = GetBilinearSamplingData(fUv, int2(GetOpticalFlowSize2() * scaleFactor));

	vfElement = NewVectorFieldEntry();

	float fWeightSum = 0.0f;
	for (int iSampleIndex = 0; iSampleIndex < 4; iSampleIndex++)
	{
		const int2 iOffset = bilinearInfo.iOffsets[iSampleIndex];
		const int2 iSamplePos = bilinearInfo.iBasePos + iOffset;

		if (IsOnScreen(iSamplePos, int2(GetOpticalFlowSize2() * scaleFactor)))
		{
			const float fWeight = bilinearInfo.fWeights[iSampleIndex];

			VectorFieldEntry fOfVectorSample = NewVectorFieldEntry();
			int2 packedOpticalFlowMv = int2(LoadOpticalFlowFieldMv(iSamplePos));
			UnpackVectorFieldEntries(packedOpticalFlowMv, fOfVectorSample);

			vfElement.fMotionVector += fOfVectorSample.fMotionVector * fWeight;
			vfElement.uHighPriorityFactor += fOfVectorSample.uHighPriorityFactor * fWeight;
			vfElement.uLowPriorityFactor += fOfVectorSample.uLowPriorityFactor * fWeight;

			fWeightSum += fWeight;
		}
	}

	if (fWeightSum > 0.0f)
	{
		vfElement.fMotionVector /= fWeightSum;
		vfElement.uHighPriorityFactor /= fWeightSum;
		vfElement.uLowPriorityFactor /= fWeightSum;
	}

	vfElement.bNegOutside = !IsUvInside(fUv - vfElement.fMotionVector);
	vfElement.bPosOutside = !IsUvInside(fUv + vfElement.fMotionVector);
	vfElement.fVelocity = length(vfElement.fMotionVector);
}

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

float3 materialIdToRandomColor(uint id)
{
	float x = float((id * (id + 17) * (id + 15)) & 0xFF) / 255.0;
	float y = float(((id + 4) * (id + 13) * (id + 11)) & 0xFF) / 255.0;
	float z = float(((id * 5) * (id + 21) * (id + 19)) & 0xFF) / 255.0;
	return float3(x, y, z);
}

float4 mainPS(Interpolants interpolants) : SV_TARGET
{
	float2 fullscreenUV = interpolants.uv;
	fullscreenUV.y = 1.0 - fullscreenUV.y;
	
	float2 scaledUV = fullscreenUV * sceneUniform.screenResolution.xy * sceneUniform.unscaledScreenResolution.zw;

	uint modeEnum = pushConstants.modeEnum;
	float4 color = float4(0.0, 0.0, 0.0, 1.0);

	GBUFFER0_DATATYPE encodedGBuffer0 = gbuffer0.Load(int3(interpolants.posH.xy, 0));
	GBUFFER1_DATATYPE encodedGBuffer1 = gbuffer1.Load(int3(interpolants.posH.xy, 0));
	GBufferData gbufferData = decodeGBuffers(encodedGBuffer0, encodedGBuffer1);
	
	float2 bary = barycentricCoord.Load(int3(interpolants.posH.xy, 0)).xy;
	bool bInvalidBary = (bary.x < 0 || bary.y < 0);
	const float3 PINK = float3(255, 194, 205) / 255.0;
	
	GBUFFER0_DATATYPE encodedVisGBuffer0 = visGBuffer0.Load(int3(interpolants.posH.xy, 0));
	GBUFFER1_DATATYPE encodedVisGBuffer1 = visGBuffer1.Load(int3(interpolants.posH.xy, 0));
	GBufferData visGBufferData = decodeGBuffers(encodedVisGBuffer0, encodedVisGBuffer1);

	if (modeEnum == MODE_MATERIAL_ID)
	{
		color.rgb = materialIdToRandomColor(gbufferData.materialID);
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
		color.rgb = sceneColor.SampleLevel(textureSampler, scaledUV, 0.0).rgb;
	}
	else if (modeEnum == MODE_RAY_TRACED_SHADOWS)
	{
		color.rgb = shadowMask.SampleLevel(textureSampler, scaledUV, 0.0).rrr;
	}
	else if (modeEnum == MODE_INDIRECT_DIFFUSE)
	{
		color.rgb = indirectDiffuse.SampleLevel(textureSampler, scaledUV, 0.0).rgb;
	}
	else if (modeEnum == MODE_INDIRECT_SPECULAR)
	{
		color.rgb = indirectSpecular.SampleLevel(textureSampler, scaledUV, 0.0).rgb;
	}
	else if (modeEnum == MODE_VELOCITY_MAP)
	{
		float2 vel = velocityMap.SampleLevel(textureSampler, scaledUV, 0.0).rg;
		color.rg = 50.0 * abs(vel);
	}
	else if (modeEnum == MODE_VISIBILITY_BUFFER)
	{
		int2 coord = int2(fullscreenUV * float2(pushConstants.width, pushConstants.height));
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
		color.rgb = bInvalidBary ? PINK : float3(bary, 0);
	}
	else if (modeEnum == MODE_VIS_MATERIAL_ID)
	{
		color.rgb = bInvalidBary ? PINK : materialIdToRandomColor(visGBufferData.materialID);
	}
	else if (modeEnum == MODE_VIS_ALBEDO)
	{
		color.rgb = bInvalidBary ? PINK : visGBufferData.albedo;
	}
	else if (modeEnum == MODE_VIS_ROUGHNESS)
	{
		color.rgb = bInvalidBary ? PINK : visGBufferData.roughness.rrr;
	}
	else if (modeEnum == MODE_VIS_METAL_MASK)
	{
		color.rgb = bInvalidBary ? PINK : visGBufferData.metalMask.rrr;
	}
	else if (modeEnum == MODE_OPTICAL_FLOW_VECTOR)
	{
#if 0
		uint2 ofvSize = unpackOpticalFlowVectorTextureSize();
		int2 coord = int2(scaledUV * float2(ofvSize.x, ofvSize.y));

		int2 flow = opticalFlowVector.Load(int3(coord, 0)).rg;
		color.r = saturate(0.5 + 0.5 * (float(flow.r) / 4.0));
		color.g = saturate(0.5 + 0.5 * (float(flow.g) / 4.0));
		color.b = 0;
#else
		VectorFieldEntry ofMv;
		SampleOpticalFlowMotionVectorField(scaledUV, ofMv);
		
		color.rgb = getMotionVectorColor(ofMv.fMotionVector);
#endif
	}
	else if (modeEnum == MODE_INTERPOLATED_FRAME)
	{
		color.rgb = interpolatedFrame.SampleLevel(textureSampler, scaledUV, 0.0).rgb;
	}

	// Gamma correction
	//float gamma = 1.0 / GAMMA_CORRECTION;
	//color.rgb = pow(color.rgb, float3(gamma, gamma, gamma));

	return color;
}
