
// ------------------------------------------------------------------------
// Resource bindings

struct IdConstant
{
    uint objectId;
};
struct MaterialConstants
{
    float4x4 mvpTransform;
    float4 albedoMultiplier;
};

ConstantBuffer<IdConstant> objectConstants : register(b0);
ConstantBuffer<MaterialConstants> materialConstants[] : register(b1);

Texture2D albedoTexture : register(t0);
SamplerState albedoSampler : register(s0);

uint getObjectId() { return objectConstants.objectId; }
MaterialConstants getMaterialData() { return materialConstants[getObjectId()]; }

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

    MaterialConstants material = getMaterialData();

    output.posH = mul(float4(input.posL, 1.0), material.mvpTransform);

    output.normal = normalize(input.posL);

    // todo-wip: Bind a vertex buffer for UV
    output.uv = frac(input.posL.xy);

    return output;
}

// ------------------------------------------------------------------------
// Pixel shader

float4 mainPS(Interpolants interpolants) : SV_TARGET
{
    MaterialConstants material = getMaterialData();

    // Variables
    float3 Wi = -normalize(float3(0.0, -1.0, 1.0));
    float3 N = normalize(interpolants.normal);
    float NdotL = max(0.0, dot(N, Wi));

    // Material properties
    float3 albedo = albedoTexture.SampleLevel(albedoSampler, interpolants.uv, 0.0).rgb;
    albedo *= material.albedoMultiplier.rgb;

    // Lighting
    float3 Li = 5.0 * float3(1.0, 1.0, 1.0);
    float3 diffuse = albedo * Li * NdotL;
    float3 specular = float3(0.0, 0.0, 0.0);

    float3 radiance = diffuse + specular;
    //float opacity = material.color.a;

    return float4(radiance, 1.0);
}
