#define MODE_NONE              0
#define MODE_DIRECT_LIGHTING   1
#define MODE_INDIRECT_SPECULAR 2

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
    uint modeEnum;
};

ConstantBuffer<PushConstants> pushConstants : register(b0, space0);
Texture2D sceneColor                        : register(t0, space0);
Texture2D indirectSpecular                  : register(t1, space0);
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

    if (modeEnum == MODE_DIRECT_LIGHTING)
    {
        color.rgb = sceneColor.SampleLevel(textureSampler, screenUV, 0.0).rgb;
    }
    else if (modeEnum == MODE_INDIRECT_SPECULAR)
    {
        color.rgb = indirectSpecular.SampleLevel(textureSampler, screenUV, 0.0).rgb;
    }

    // Gamma correction
    //float gamma = 1.0 / GAMMA_CORRECTION;
    //color.rgb = pow(color.rgb, float3(gamma, gamma, gamma));

    return color;
}
