#include "common.hlsl"

// ------------------------------------------------------------------------
// Resource bindings

struct PushConstants
{
	uint packedInputSize;  // low 16-bit: width, high 16-bit: height
    uint packedOutputSize; // same as above
	uint outputMip;
};

ConstantBuffer<PushConstants> pushConstants;
Texture2D                     inputTexture;
RWTexture2D<float>            outputTexture;

uint2 unpackInputSize()
{
	uint xy = pushConstants.packedInputSize;
	return uint2(xy & 0xffff, (xy >> 16) & 0xffff);
}
uint2 unpackOutputSize()
{
	uint xy = pushConstants.packedOutputSize;
	return uint2(xy & 0xffff, (xy >> 16) & 0xffff);
}

// ------------------------------------------------------------------------
// Kernel: CopyMip0

[numthreads(8, 8, 1)]
void copyMip0CS(uint3 tid: SV_DispatchThreadID)
{
	uint2 outputSize = unpackOutputSize();
	if (tid.x < outputSize.x && tid.y < outputSize.y)
	{
		outputTexture[tid.xy].r = inputTexture.Load(int3(tid.xy, 0)).r;
	}
}

// ------------------------------------------------------------------------
// Kernel: Downsample

int3 clampCoord(uint x, uint y, uint2 inputSize, uint sourceMip)
{
	return int3(min(x, inputSize.x - 1), min(y, inputSize.y - 1), sourceMip);
}

[numthreads(8, 8, 1)]
void downsampleCS(uint3 tid : SV_DispatchThreadID)
{
	uint2 inputSize = unpackInputSize();
	uint2 outputSize = unpackOutputSize();
	uint sourceMip = pushConstants.outputMip - 1;
	
	if (tid.x < outputSize.x && tid.y < outputSize.y)
	{
		float d00 = inputTexture.Load(clampCoord(tid.x * 2 + 0, tid.y * 2 + 0, inputSize, sourceMip)).r;
		float d10 = inputTexture.Load(clampCoord(tid.x * 2 + 1, tid.y * 2 + 0, inputSize, sourceMip)).r;
		float d01 = inputTexture.Load(clampCoord(tid.x * 2 + 0, tid.y * 2 + 1, inputSize, sourceMip)).r;
		float d11 = inputTexture.Load(clampCoord(tid.x * 2 + 1, tid.y * 2 + 1, inputSize, sourceMip)).r;
		
#if REVERSE_Z
		float d = min(d00, min(d10, min(d01, d11)));
#else
		float d = max(d00, max(d10, max(d01, d11)));
#endif
		
		outputTexture[tid.xy] = d;
	}
}
