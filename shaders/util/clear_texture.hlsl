#define TEXTURE_DIMENSION_1D  0
#define TEXTURE_DIMENSION_2D  1
#define TEXTURE_DIMENSION_3D  2

#define TEXTURE_FORMAT_FLOAT4 0
#define TEXTURE_FORMAT_FLOAT2 1
#define TEXTURE_FORMAT_FLOAT1 2
#define TEXTURE_FORMAT_UINT4  3
#define TEXTURE_FORMAT_UINT2  4
#define TEXTURE_FORMAT_UINT1  5
#define TEXTURE_FORMAT_INT4   6
#define TEXTURE_FORMAT_INT2   7
#define TEXTURE_FORMAT_INT1   8

#if !defined(TEXTURE_DIMENSION_ENUM)
	#error TEXTURE_DIMENSION_ENUM is not defined
#elif TEXTURE_DIMENSION_ENUM == TEXTURE_DIMENSION_1D
	#define TEXTURE_DIMENSION RWTexture1D
#elif TEXTURE_DIMENSION_ENUM == TEXTURE_DIMENSION_2D
	#define TEXTURE_DIMENSION RWTexture2D
#elif TEXTURE_DIMENSION_ENUM == TEXTURE_DIMENSION_3D
	#define TEXTURE_DIMENSION RWTexture3D
#else
	#error TEXTURE_DIMENSION_ENUM is defined but the value is invalid
#endif

#if !defined(TEXTURE_FORMAT_ENUM)
	#error TEXTURE_FORMAT_ENUM is not defined
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_FLOAT4
	#define TEXTURE_FORMAT float4
	#define CLEAR_VALUE_TYPE float4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_FLOAT2
	#define TEXTURE_FORMAT float2
	#define CLEAR_VALUE_TYPE float4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_FLOAT1
	#define TEXTURE_FORMAT float1
	#define CLEAR_VALUE_TYPE float4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_UINT4
	#define TEXTURE_FORMAT uint4
	#define CLEAR_VALUE_TYPE uint4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_UINT2
	#define TEXTURE_FORMAT uint2
	#define CLEAR_VALUE_TYPE uint4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_UINT1
	#define TEXTURE_FORMAT uint1
	#define CLEAR_VALUE_TYPE uint4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_INT4
	#define TEXTURE_FORMAT int4
	#define CLEAR_VALUE_TYPE int4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_INT2
	#define TEXTURE_FORMAT int2
	#define CLEAR_VALUE_TYPE int4
#elif TEXTURE_FORMAT_ENUM == TEXTURE_FORMAT_INT1
	#define TEXTURE_FORMAT int1
	#define CLEAR_VALUE_TYPE int4
#else
	#error TEXTURE_FORMAT_ENUM is defined but the value is invalid
#endif

struct PushConstants
{
	uint width;
	uint height;
	uint depth;
	uint _pad0;
	CLEAR_VALUE_TYPE clearValue;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants>       pushConstants;

TEXTURE_DIMENSION<TEXTURE_FORMAT>   rwTexture;

#if TEXTURE_DIMENSION_ENUM == TEXTURE_DIMENSION_1D
[numthreads(64, 1, 1)]
void clear1D(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x < pushConstants.width)
	{
		rwTexture[tid.x] = pushConstants.clearValue;
	}
}
#endif

#if TEXTURE_DIMENSION_ENUM == TEXTURE_DIMENSION_2D
[numthreads(8, 8, 1)]
void clear2D(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x < pushConstants.width && tid.y < pushConstants.height)
	{
		rwTexture[tid.xy] = pushConstants.clearValue;
	}
}
#endif

#if TEXTURE_DIMENSION_ENUM == TEXTURE_DIMENSION_3D
[numthreads(4, 4, 4)]
void clear3D(uint3 tid : SV_DispatchThreadID)
{
	if (tid.x < pushConstants.width && tid.y < pushConstants.height && tid.z < pushConstants.depth)
	{
		rwTexture[tid.xyz] = pushConstants.clearValue;
	}
}
#endif
