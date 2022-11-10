#include "common.hlsl"

// error X3596: By default, unbounded size descriptor tables are disabled
// to support frame captures in graphics tools. Use of unbounded (or large)
// size descriptor tables can produce unusually large and potentially unusable
// frame captures in graphics tools.  Please specify a reasonably small upper
// bound on table size.  If that isn't an option, unbounded size descriptor
// tables can be enabled using the compiler using switch: /enable_unbounded_descriptor_tables
#define TEMP_MAX_SRVS 1024

// ------------------------------------------------------------------------
// Resource bindings (common)

struct PushConstants
{
    uint objectId;
};

ConstantBuffer<PushConstants> pushConstants : register(b0);
ConstantBuffer<SceneUniform> sceneUniform   : register(b1);
StructuredBuffer<MeshData> gpuSceneBuffer   : register(t0);

uint getObjectId()     { return pushConstants.objectId; }
MeshData getMeshData() { return gpuSceneBuffer[pushConstants.objectId]; }

// ------------------------------------------------------------------------
// Resource bindings (material-specific)

struct Material
{
    float4 albedoMultiplier;
    uint   albedoTextureIndex; float3 _pad0;
};
ConstantBuffer<Material> materials[]        : register(b0, space1); // bindless in another space
Texture2D albedoTextures[TEMP_MAX_SRVS]     : register(t0, space1); // bindless in another space
SamplerState albedoSampler                  : register(s0);

Material getMaterial() { return materials[getObjectId()]; }

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

    float3 positionWS : POSITION;   // in world space
    float3 normalWS   : NORMAL;     // in world space
    float2 texcoord   : TEXCOORD0;
};

Interpolants mainVS(VertexInput input)
{
    Interpolants output;

    MeshData meshData = getMeshData();
    float4x4 modelMatrix = meshData.modelMatrix;

    float4x4 MVP = mul(modelMatrix, sceneUniform.viewProjMatrix);
    output.svPosition = mul(float4(input.position, 1.0), MVP);

    output.positionWS = mul(float4(input.position, 1.0), modelMatrix).xyz;

    // #todo-shader: Should renormalize if model matrix has non-uniform scaling
    // I can't find float4x4 -> float3x3 conversion in MSDN??? what???
    // Should be normalize(mul(input.normal, transpose(inverse(modelMatrix3x3))));
    output.normalWS = normalize(mul(float4(input.normal, 0.0), modelMatrix).xyz);

    output.texcoord = input.texcoord;

    return output;
}

// ------------------------------------------------------------------------
// Pixel shader

struct PixelOutput
{
    float4 sceneColor   : SV_TARGET0;
    float4 thinGBufferA : SV_TARGET1;
};

PixelOutput mainPS(Interpolants interpolants)
{
    MeshData meshData = getMeshData();
    Material material = getMaterial();

    // Variables
    float3 N = normalize(interpolants.normalWS);

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

    // Fake indirect lighting to distinguish with black background.
    diffuse += float3(0.02, 0.02, 0.02);

    float3 luminance = diffuse + specular;

    PixelOutput output;
    output.sceneColor = float4(luminance, 1.0);
    output.thinGBufferA = float4(N, 1.0);
    return output;
}
