
// ------------------------------------------------------------------------
// Resource bindings

struct IdConstant
{
    uint objectId;
};

struct Material
{
    float4x4 modelMatrix;
    float4 albedoMultiplier;
};

struct SceneUniform
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 viewProjMatrix;

    float4 sunDirection;   // (x, y, z, ?)
    float4 sunIlluminance; // (r, g, b, ?)
};

ConstantBuffer<IdConstant> objectConstants : register(b0);
ConstantBuffer<SceneUniform> sceneUniform : register(b1);
// #todo-shader: It seems glslangValidator can't translate HLSL unbounded array.
ConstantBuffer<Material> materialConstants[] : register(b2);

Texture2D albedoTexture : register(t0);
SamplerState albedoSampler : register(s0);

uint getObjectId() { return objectConstants.objectId; }
// #todo-shader: glslangValidator can't translate this?
Material getMaterialData() { return materialConstants[getObjectId()]; }

// ------------------------------------------------------------------------
// Vertex shader

struct VertexInput
{
    float3 posL : POSITION;
};

struct Interpolants
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

Interpolants mainVS(VertexInput input)
{
    Interpolants output;

    Material material = getMaterialData();

    float4x4 MVP = mul(material.modelMatrix, sceneUniform.viewProjMatrix);
    output.posH = mul(float4(input.posL, 1.0), MVP);

    output.normal = normalize(input.posL);

    // todo-wip: Bind a vertex buffer for UV
    output.uv = frac(input.posL.xy);

    return output;
}

// ------------------------------------------------------------------------
// Pixel shader

float4 mainPS(Interpolants interpolants) : SV_TARGET
{
    Material material = getMaterialData();

    // Variables
    float3 N = normalize(interpolants.normal);

    // Material properties
    float3 albedo = albedoTexture.SampleLevel(albedoSampler, interpolants.uv, 0.0).rgb;
    albedo *= material.albedoMultiplier.rgb;

    // Lighting
    float3 diffuse = float3(0.0, 0.0, 0.0);
    float3 specular = float3(0.0, 0.0, 0.0);
    {
        // Sun
        float3 Li = sceneUniform.sunIlluminance.rgb; //5.0 * float3(1.0, 1.0, 1.0);
        float3 Wi = -sceneUniform.sunDirection.xyz;
        float NdotL = max(0.0, dot(N, Wi));
        diffuse += albedo * Li * NdotL;
    }

    float3 outLuminance = diffuse + specular;
    //float opacity = material.color.a;

    return float4(outLuminance, 1.0);
}
