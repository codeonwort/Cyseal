
struct VertexInput
{
    float3 posL : POSITION;
};

struct VertexOutput
{
    float4 posH : SV_POSITION;
};

VertexOutput mainVS(VertexInput input)
{
    VertexOutput output;

    output.posH = float4(input.posL, 1.0);

    return output;
}

float4 mainPS(VertexOutput vout) : SV_TARGET
{
	float r = vout.posH.z;
    return float4(r, r, r, 1.0);
}
