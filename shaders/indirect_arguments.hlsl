#include "common.hlsl"

// Indirect draw definitions
// https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing
// https://microsoft.github.io/DirectX-Specs/d3d/IndirectDrawing.html#indirect-argument-buffer-structures

#define D3D12_GPU_VIRTUAL_ADDRESS uint2
#define DXGI_FORMAT               uint
#define UINT64                    uint2
#define UINT                      uint
#define INT                       int
struct D3D12_GPU_VIRTUAL_ADDRESS_RANGE
{
	D3D12_GPU_VIRTUAL_ADDRESS StartAddress;
	UINT64                    SizeInBytes;
};

struct D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE
{
	D3D12_GPU_VIRTUAL_ADDRESS StartAddress;
	UINT64                    SizeInBytes;
	UINT64                    StrideInBytes;
};

struct D3D12_DRAW_ARGUMENTS
{
	UINT VertexCountPerInstance;
	UINT InstanceCount;
	UINT StartVertexLocation;
	UINT StartInstanceLocation;
};

struct D3D12_DRAW_INDEXED_ARGUMENTS
{
	UINT IndexCountPerInstance;
	UINT InstanceCount;
	UINT StartIndexLocation;
	INT  BaseVertexLocation;
	UINT StartInstanceLocation;
};

struct D3D12_DISPATCH_ARGUMENTS
{
	UINT ThreadGroupCountX;
	UINT ThreadGroupCountY;
	UINT ThreadGroupCountZ;
};

struct D3D12_DISPATCH_MESH_ARGUMENTS
{
    UINT ThreadGroupCountX;
    UINT ThreadGroupCountY;
    UINT ThreadGroupCountZ;
};

struct D3D12_VERTEX_BUFFER_VIEW
{
	D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
	UINT                      SizeInBytes;
	UINT                      StrideInBytes;
};

struct D3D12_INDEX_BUFFER_VIEW
{
	D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
	UINT                      SizeInBytes;
	DXGI_FORMAT               Format;
};

struct D3D12_CONSTANT_BUFFER_VIEW_DESC
{
	D3D12_GPU_VIRTUAL_ADDRESS BufferLocation;
	UINT                      SizeInBytes;
};

struct D3D12_DISPATCH_RAYS_DESC
{
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE RayGenerationShaderRecord;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE MissShaderTable;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE HitGroupTable;
	D3D12_GPU_VIRTUAL_ADDRESS_RANGE_AND_STRIDE CallableShaderTable;
    UINT Width;
    UINT Height;
    UINT Depth;
};
