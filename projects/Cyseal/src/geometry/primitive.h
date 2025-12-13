#pragma once

#include "core/types.h"
#include "core/aabb.h"
#include "core/assertion.h"
#include "rhi/pixel_format.h"
#include <vector>

struct Geometry
{
	friend struct MesoGeometry;

public:
	std::vector<vec3> positions;
	std::vector<vec3> normals;
	std::vector<vec2> texcoords;
	std::vector<uint32> indices;

	AABB localBounds;

public:
	// Don't wanna include std::vector in aabb.h...
	// This header is already polluted so let's put it here :p
	static AABB calculateAABB(const std::vector<vec3>& positions);

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
