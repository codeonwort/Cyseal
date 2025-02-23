
// #todo-hlsl: Parameterize
#define GAMMA_CORRECTION 2.2
#define EXPOSURE         1.0

// https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

// ------------------------------------------------------------------------
// Resource bindings

Texture2D sceneColor           : register(t0);
Texture2D indirectDiffuse      : register(t1);
Texture2D indirectSpecular     : register(t2);
SamplerState sceneColorSampler : register(s0);

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

    float4 color = sceneColor.SampleLevel(sceneColorSampler, screenUV, 0.0);
    color.rgb += indirectDiffuse.SampleLevel(sceneColorSampler, screenUV, 0.0).rgb;
    color.rgb += indirectSpecular.SampleLevel(sceneColorSampler, screenUV, 0.0).rgb;

    // Reinhard tone mapper
    //color.rgb = float3(1.0, 1.0, 1.0) - exp(-color.rgb * EXPOSURE);

    // ACES tone mapper
    color.rgb = ACESFitted(color.rgb);

    // Gamma correction
    float gamma = 1.0 / GAMMA_CORRECTION;
    color.rgb = pow(color.rgb, float3(gamma, gamma, gamma));

    return color;
}
