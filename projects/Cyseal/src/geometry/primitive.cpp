#include "primitive.h"

#include <algorithm>

static AABB calculateAABB(const std::vector<vec3>& positions)
{
	vec3 minV(0.0f, 0.0f, 0.0f), maxV(0.0f, 0.0f, 0.0f);
	if (positions.size() > 0)
	{
		minV = vec3(FLT_MAX, FLT_MAX, FLT_MAX);
		maxV = vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const vec3& v : positions)
		{
			minV = vecMin(minV, v);
			maxV = vecMax(maxV, v);
		}
	}
	return AABB::fromMinMax(minV, maxV);
}

bool Geometry::needsToPartition(const Geometry& G, uint32 maxTriangleCount)
{
	const size_t totalTriangles = G.indices.size() / 3;
	return totalTriangles > maxTriangleCount;
}

std::vector<MesoGeometry>* Geometry::partitionByTriangleCount(const Geometry& G, uint32 maxTriangleCount)
{
	const size_t totalTriangles = G.indices.size() / 3;
	const size_t numGeometries = (totalTriangles + maxTriangleCount - 1) / maxTriangleCount;
	
	std::vector<MesoGeometry>* mesoList = new std::vector<MesoGeometry>(numGeometries);

	uint32 firstTriangle = 0;
	uint32 remainingTriangles = (uint32)totalTriangles;
	for (size_t i = 0; i < numGeometries; ++i)
	{
		uint32 numTriangles = (std::min)(maxTriangleCount, remainingTriangles);

		MesoGeometry meso;
		meso.indices.reserve(numTriangles);

		std::vector<vec3> mesoPositions; // For local bounds
		mesoPositions.reserve(numTriangles * 3);

		for (uint32 i = firstTriangle; i < firstTriangle + numTriangles; ++i)
		{
			uint32 i0 = G.indices[i * 3 + 0];
			uint32 i1 = G.indices[i * 3 + 1];
			uint32 i2 = G.indices[i * 3 + 2];
			meso.indices.push_back(i0);
			meso.indices.push_back(i1);
			meso.indices.push_back(i2);

			mesoPositions.push_back(G.positions[i0]);
			mesoPositions.push_back(G.positions[i1]);
			mesoPositions.push_back(G.positions[i2]);
		}

		meso.localBounds = calculateAABB(mesoPositions);

		mesoList->emplace_back(meso);

		firstTriangle += maxTriangleCount;
		remainingTriangles -= maxTriangleCount;
	}

	return mesoList;
}

void Geometry::resizeNumVertices(size_t num)
{
	positions.resize(num);
	normals.resize(num);
	texcoords.resize(num);
}

void Geometry::resizeNumIndices(size_t num)
{
	indices.resize(num);
}

void Geometry::reserveNumVertices(size_t num)
{
	positions.clear();
	normals.clear();
	texcoords.clear();
	positions.reserve(num);
	normals.reserve(num);
	texcoords.reserve(num);
}

void Geometry::reserveNumIndices(size_t num)
{
	indices.clear();
	indices.reserve(num);
}

void Geometry::recalculateNormals()
{
	normals.resize(positions.size(), vec3(0.0f, 0.0f, 0.0f));
	for (uint32 i = 0; i < indices.size(); i += 3)
	{
		uint32 i0 = indices[i + 0];
		uint32 i1 = indices[i + 1];
		uint32 i2 = indices[i + 2];

		vec3 p1 = positions[i1] - positions[i0];
		vec3 p2 = positions[i2] - positions[i0];
		vec3 n = cross(p1, p2);
		if (dot(n, n) > 1e-6)
		{
			n = normalize(n);
			normals[i0] += n;
			normals[i1] += n;
			normals[i2] += n;
		}
	}
	for (uint32 i = 0; i < normals.size(); ++i)
	{
		normals[i] = normalize(normals[i]);
	}
}

void Geometry::calculateLocalBounds()
{
	localBounds = calculateAABB(positions);
}

void Geometry::finalize()
{
	CHECK(positions.size() == normals.size() && normals.size() == texcoords.size());

	float numComponents = 0;
	numComponents += 3; // normal
	numComponents += 2; // texcoord

	nonPositionBlob.clear();
	nonPositionBlob.reserve((size_t)(positions.size() * numComponents));
	for (size_t i = 0; i < positions.size(); ++i)
	{
		nonPositionBlob.push_back(normals[i].x);
		nonPositionBlob.push_back(normals[i].y);
		nonPositionBlob.push_back(normals[i].z);
		nonPositionBlob.push_back(texcoords[i].x);
		nonPositionBlob.push_back(texcoords[i].y);
	}

	calculateLocalBounds();

	bFinalized = true;
}
