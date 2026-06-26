// ---------------------------------------------------------
// Shader parameters

struct PushConstants { uint2 size; };

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

RWTexture2D<float4>           inputTexture0;
RWTexture2D<float4>           inputTexture1;
RWTexture2D<float4>           outputTexture;

// ---------------------------------------------------------
// Shader

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	if (any(tid.xy >= pushConstants.size))
	{
		return;
	}
	
	float4 input0 = inputTexture0[tid.xy];
	float4 input1 = inputTexture1[tid.xy];
	
	outputTexture[tid.xy] = input0 + input1;
}
