#pragma once

#include "core/types.h"
#include "core/aabb.h"
#include "core/assertion.h"
#include "rhi/pixel_format.h"
#include <vector>

struct MesoGeometry
{
	std::vector<uint32> indices;
	AABB localBounds;

	inline uint32 getIndexBufferTotalBytes() const
	{
		return (uint32)(indices.size() * sizeof(uint32));
	}

	inline void* getIndexBlob() const
	{
		return (void*)indices.data();
	}
};

struct Geometry
{
	std::vector<vec3> positions;
	std::vector<vec3> normals;
	std::vector<vec2> texcoords;
	std::vector<uint32> indices;

	AABB localBounds;

// Utils for visibility buffer.
public:
	static bool needsToPartition(const Geometry& G, uint32 maxTriangleCount);

	/// <summary>
	/// Divide a Geometry into multiple MesoGeometry instances so that each one's triangle count does not exceed the threshold.
	/// They all share the same vertex buffer data. Only their index buffer data + local bounds differ.
	/// </summary>
	/// <param name="G">The geomety to divide.</param>
	/// <param name="maxTriangleCount">Max triangle count for each Geometry.</param>
	/// <returns>A vector of MesoGeometry. CAUTION: The caller must deallocate the vector manually.</returns>
	static std::vector<MesoGeometry>* partitionByTriangleCount(const Geometry& G, uint32 maxTriangleCount);

public:
	void resizeNumVertices(size_t num); // CAUTION: Don't use push_back()
	void resizeNumIndices(size_t num);  // CAUTION: Don't use push_back()

	void reserveNumVertices(size_t num);
	void reserveNumIndices(size_t num);

	void recalculateNormals();

	void calculateLocalBounds();

	// Geometry should be finalized before uploading to GPU.
	void finalize();

	inline uint32 getPositionStride() const
	{
		return (uint32)(sizeof(vec3));
	}
	inline uint32 getPositionBufferTotalBytes() const
	{
		return (uint32)(positions.size() * getPositionStride());
	}
	inline void* getPositionBlob() const
	{
		return (void*)positions.data();
	}

	inline uint32 getNonPositionStride() const
	{
		CHECK(bFinalized);
		// normal, texcoord
		return (uint32)(sizeof(vec3) + sizeof(vec2));
	}
	inline uint32 getNonPositionBufferTotalBytes() const
	{
		CHECK(bFinalized);
		return (uint32)(positions.size() * getNonPositionStride());
	}
	inline void* getNonPositionBlob() const
	{
		CHECK(bFinalized);
		return (void*)nonPositionBlob.data();
	}

	inline uint32 getIndexBufferTotalBytes() const
	{
		return (uint32)(indices.size() * sizeof(uint32));
	}
	inline void* getIndexBlob() const
	{
		return (void*)indices.data();
	}
	inline EPixelFormat getIndexFormat() const
	{
		return EPixelFormat::R32_UINT;
	}

private:
	std::vector<float> nonPositionBlob;
	bool bFinalized = false;
};
