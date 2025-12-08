#include "common.hlsl"

// error X3596: By default, unbounded size descriptor tables are disabled
// to support frame captures in graphics tools. Use of unbounded (or large)
// size descriptor tables can produce unusually large and potentially unusable
// frame captures in graphics tools.  Please specify a reasonably small upper
// bound on table size.  If that isn't an option, unbounded size descriptor
// tables can be enabled using the compiler using switch: /enable_unbounded_descriptor_tables
#define TEMP_MAX_SRVS 1024

#ifndef DEPTH_PREPASS
	#define DEPTH_PREPASS 0
#endif

#ifndef VISIBILITY_BUFFER
	#define VISIBILITY_BUFFER 0
#endif

// ------------------------------------------------------------------------
// Resource bindings (common)

struct PushConstants
{
    uint objectId;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants>  pushConstants  : register(b0);

ConstantBuffer<SceneUniform>   sceneUniform   : register(b1);
StructuredBuffer<GPUSceneItem> gpuSceneBuffer : register(t0);
#if !DEPTH_PREPASS
StructuredBuffer<Material>     materials      : register(t1);
Texture2D                      shadowMask     : register(t2);
#endif

uint getObjectId() { return pushConstants.objectId; }
GPUSceneItem getGPUSceneItem() { return gpuSceneBuffer[pushConstants.objectId]; }

// ------------------------------------------------------------------------
// Resource bindings (material-specific)

#if !DEPTH_PREPASS
Texture2D albedoTextures[TEMP_MAX_SRVS]       : register(t0, space1); // bindless in another space
SamplerState albedoSampler                    : register(s0);

Material getMaterial() { return materials[getObjectId()]; }
#endif

// ------------------------------------------------------------------------
// Vertex shader

struct VertexInput
{
    // All in model space
    float3 position   : POSITION;
    float3 normal     : NORMAL;
    float2 texcoord   : TEXCOORD0;
};

struct Interpolants
{
    float4 svPosition : SV_POSITION;

    float3 positionLS : POSITION0;  // in local space
    //float3 positionWS : POSITION1;  // in world space
    float3 normalWS   : NORMAL;     // in world space
    float2 texcoord   : TEXCOORD0;
};

Interpolants mainVS(VertexInput input)
{
    Interpolants output;

    GPUSceneItem sceneItem = getGPUSceneItem();
    float4x4 localToWorld = sceneItem.localToWorld;

    float4x4 MVP = mul(localToWorld, sceneUniform.viewProjMatrix);
    output.svPosition = mul(float4(input.position, 1.0), MVP);

    output.positionLS = input.position;
    //output.positionWS = mul(float4(input.position, 1.0), localToWorld).xyz;

    // #todo-shader: Should renormalize if model matrix has non-uniform scaling
    // I can't find float4x4 -> float3x3 conversion in MSDN??? what???
    // Should be normalize(mul(input.normal, transpose(inverse(localToWorld3x3))));
    output.normalWS = normalize(mul(float4(input.normal, 0.0), localToWorld).xyz);

    output.texcoord = input.texcoord;

    return output;
}

// ------------------------------------------------------------------------
// Pixel shader

#if DEPTH_PREPASS && !VISIBILITY_BUFFER

void mainPS(Interpolants interpolants) {}

#elif DEPTH_PREPASS && VISIBILITY_BUFFER

struct PixelOutput
{
    uint visibility : SV_TARGET0;
};

PixelOutput mainPS(Interpolants interpolants, uint primID : SV_PrimitiveId)
{
	VisibilityBufferData unpacked;
	unpacked.objectID = getObjectId();
	unpacked.primitiveID = primID;
	uint packed = encodeVisibilityBuffer(unpacked);

	PixelOutput output;
	output.visibility = packed;
	return output;
}

#else

struct PixelOutput
{
    float4            sceneColor  : SV_TARGET0;
    GBUFFER0_DATATYPE gbuffer0    : SV_TARGET1;
    GBUFFER1_DATATYPE gbuffer1    : SV_TARGET2;
    float2            velocityMap : SV_TARGET3;
};

PixelOutput mainPS(Interpolants interpolants)
{
    GPUSceneItem sceneItem = getGPUSceneItem();
    Material material = getMaterial();

    // Variables
    float3 P = mul(float4(interpolants.positionLS, 1.0), sceneItem.localToWorld).xyz;
    float3 prevP = mul(float4(interpolants.positionLS, 1.0), sceneItem.prevLocalToWorld).xyz;
    float3 N = normalize(interpolants.normalWS);
    
    // #todo-basepass: Flip N for double-sided materials.
    // Currently not doing so is better because indirect diffuse pass will construct a ray that 'penetrates' the surface.
    // To do this correctly:
    //   1. Permutate not only pipeline states but also base pass shaders so that only double-sided materials pay for this.
    //   2. Let reflection/refraction deal with back-facing normals correctly.
    //float3 viewDir = normalize(interpolants.positionWS - sceneUniform.cameraPosition);
    //if (dot(viewDir, N) > 0) N *= -1;

    // Material properties
    Texture2D albedoTex = albedoTextures[material.albedoTextureIndex];
    float3 albedo = albedoTex.SampleLevel(albedoSampler, interpolants.texcoord, 0.0).rgb;
    albedo *= material.albedoMultiplier.rgb;

    // Direct lighting
    float3 diffuse = float3(0.0, 0.0, 0.0);
    float3 specular = float3(0.0, 0.0, 0.0);
    {
        // Sun
        float3 Li = sceneUniform.sunIlluminance.rgb;
        float3 Wi = -sceneUniform.sunDirection.xyz;
        float NdotL = max(0.0, dot(N, Wi));
        diffuse += albedo * Li * NdotL;
    }

    float3 luminance = diffuse + specular;

    float shadow = shadowMask.Load(int3(interpolants.svPosition.xy, 0)).r;
    luminance *= shadow;

    GBufferData gbufferData;
    gbufferData.albedo            = albedo;
    gbufferData.roughness         = material.roughness;
    gbufferData.normalWS          = N;
    gbufferData.metalMask         = material.metalMask;
    gbufferData.materialID        = material.materialID;
    gbufferData.indexOfRefraction = material.indexOfRefraction;
    
    float4 clipPos = mul(float4(P, 1.0), sceneUniform.viewProjMatrix);
    float4 prevClipPos = mul(float4(prevP, 1.0), sceneUniform.prevViewProjMatrix);
    clipPos /= clipPos.w;
    prevClipPos /= prevClipPos.w;
    float2 uv0 = clipSpaceToTextureUV(clipPos);
    float2 uv1 = clipSpaceToTextureUV(prevClipPos);
    
    PixelOutput output;
    output.sceneColor = float4(luminance, 1.0);
    encodeGBuffers(gbufferData, output.gbuffer0, output.gbuffer1);
    output.velocityMap = uv0 - uv1;
    return output;
}

#endif
