
#if defined(WRITE_PASS)

RWStructuredBuffer<uint> rwBuffer;

[numthreads(1, 1, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	rwBuffer[tid.x] = tid.x;
}

#elif defined(READ_PASS)

StructuredBuffer<uint> bufferA;
StructuredBuffer<uint> bufferB;
RWStructuredBuffer<uint> rwBuffer;

[numthreads(1, 1, 1)]
void mainCS(uint3 tid : SV_DispatchThreadID)
{
	uint x1 = bufferA.Load(tid.x);
	uint x2 = bufferB.Load(tid.x);
	rwBuffer[tid.x] = x1 + x2;
}

#else
	#error No symbol was defined
#endif
