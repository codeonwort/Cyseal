
struct PushConstants
{
	uint textureWidth;
	uint textureHeight;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

#if defined(WRITE_PASS)

RWTexture2D<float4> rwTexture;

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	float2 uv = float2(tid.xy) / float2(pushConstants.textureWidth, pushConstants.textureHeight);
	rwTexture[tid.xy] = float4(uv.x, uv.y, 0, 1);
}

#elif defined(READ_PASS)

Texture2D<float4> textureA;
Texture2D<float4> textureB;
RWTexture2D<float4> rwTexture;

[numthreads(8, 8, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	float4 colorA = textureA.Load(int3(tid.xy, 0));
	float4 colorB = textureB.Load(int3(tid.xy, 0));
	rwTexture[tid.xy] = float4(colorA.xyz + colorB.xyz, 1);
}

#else
	#error No symbol was defined
#endif
