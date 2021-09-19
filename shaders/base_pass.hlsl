
struct IdConstant
{
    uint objectId;
};
struct MaterialConstants
{
    row_major float4x4 mvpTransform; // #todo-matrix: Use column-major?
    float4 color;
};

ConstantBuffer<IdConstant> objectConstants : register(b0);
ConstantBuffer<MaterialConstants> materialConstants[] : register(b1);

Texture2D albedoTexture : register(t0);
SamplerState albedoSampler : register(s0);

uint getObjectId() { return objectConstants.objectId; }
MaterialConstants getMaterialData() { return materialConstants[getObjectId()]; }

// ------------------------------------------------------------------------

struct VertexInput
{
    float3 posL : POSITION;
};

struct VertexOutput
{
    float4 posH : SV_POSITION;
    float3 normal : NORMAL;
};

VertexOutput mainVS(VertexInput input)
{
    VertexOutput output;

    MaterialConstants material = getMaterialData();

    // (row vector) * (row-major matrix)
    output.posH = mul(float4(input.posL, 1.0), material.mvpTransform);

    // (column-major matrix) * (column vector)
    //output.posH = mul(mvpTransform, float4(input.posL, 1.0));

    output.normal = normalize(input.posL);

    return output;
}

float4 mainPS(VertexOutput vout) : SV_TARGET
{
    MaterialConstants material = getMaterialData();

    // Variables
    float3 L = normalize(float3(0.0, -1.0, -1.0));
    float3 N = normalize(vout.normal);
    float NdotL = max(0.0, dot(N, -L));

    // Material properties
    float3 albedo = float3(material.color.rgb);

    // Lighting
    float3 diffuse = albedo * NdotL;
    float3 specular = float3(0.0, 0.0, 0.0);

    float3 luminance = diffuse + specular;
    float opacity = material.color.a;

    return float4(luminance, opacity);
}
