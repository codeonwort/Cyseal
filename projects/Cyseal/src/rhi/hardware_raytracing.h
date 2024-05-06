#pragma once

#include "gpu_resource.h"

class Buffer;
class IndexBuffer;
class VertexBuffer;

//////////////////////////////////////////////////////////////////////////
// Raytracing resource

// D3D12_RAYTRACING_GEOMETRY_TYPE
enum class ERaytracingGeometryType
{
	Triangles,
	ProceduralPrimitiveAABB
};

// D3D12_RAYTRACING_GEOMETRY_FLAGS
enum class ERaytracingGeometryFlags : uint32
{
	None                        = 0,
	Opaque                      = 1 << 0,
	NoDuplicateAnyhitInvocation = 1 << 1
};
ENUM_CLASS_FLAGS(ERaytracingGeometryFlags);

// D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC
struct RaytracingGeometryTrianglesDesc
{
	// Assumes this buffer contains a series of 3x4 matrices in compact,
	// So that k-th matrix starts from (48 * k * transformIndex) of this buffer.
	Buffer* transform3x4Buffer = nullptr;
	uint32 transformIndex = 0;

	EPixelFormat indexFormat;
	EPixelFormat vertexFormat;
	uint32 indexCount;
	uint32 vertexCount;
	IndexBuffer* indexBuffer;
	VertexBuffer* vertexBuffer;
};

// D3D12_RAYTRACING_GEOMETRY_DESC
struct RaytracingGeometryDesc
{
	ERaytracingGeometryType type;
	ERaytracingGeometryFlags flags;
	union
	{
		RaytracingGeometryTrianglesDesc triangles;
		// #todo-dxr: RaytracingGeometryAABBsDesc
	};
};

// D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC
struct BLASInstanceInitDesc
{
	BLASInstanceInitDesc()
	{
		::memset(instanceTransform, 0, sizeof(instanceTransform));
		instanceTransform[0][0] = 1.0f;
		instanceTransform[1][1] = 1.0f;
		instanceTransform[2][2] = 1.0f;
	}

	std::vector<RaytracingGeometryDesc> geomDescs;
	float instanceTransform[3][4];
};

struct BLASInstanceUpdateDesc
{
	uint32 blasIndex;
	float instanceTransform[3][4];
};

class AccelerationStructure : public GPUResource
{
public:
	virtual ~AccelerationStructure() = default;

	virtual void rebuildTLAS(
		RenderCommandList* commandList,
		uint32 numInstanceUpdates,
		const BLASInstanceUpdateDesc* updateDescs) = 0;

	virtual ShaderResourceView* getSRV() const = 0;
};
