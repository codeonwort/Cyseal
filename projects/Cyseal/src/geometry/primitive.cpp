#include "primitive.h"
#include <math.h>
#include <map>

// http://blog.andreaskahler.com/2009/06/creating-icosphere-mesh-in-code.html
void GeometryGenerator::icosphere(uint32 iterations, Geometry& outGeometry)
{
	std::map<int64, int32> middlePointIndexCache;
	int32 index = 0;

	auto addVertex = [&](const vec3& v) -> int32
	{
		outGeometry.positions.push_back(normalize(v));
		return index++;
	};

	auto getMiddlePoint = [&](int32 p1, int32 p2) -> int32
	{
		// first check if we have it already
		bool firstIsSmaller = p1 < p2;
		int64 smallerIndex = firstIsSmaller ? p1 : p2;
		int64 greaterIndex = firstIsSmaller ? p2 : p1;
		int64 key = (smallerIndex << 32) + greaterIndex;

		auto it = middlePointIndexCache.find(key);
		if (it != middlePointIndexCache.end())
		{
			return it->second;
		}

		// not in cache, calculate it
		vec3 point1 = outGeometry.positions[p1];
		vec3 point2 = outGeometry.positions[p2];
		vec3 middle(
			(point1.x + point2.x) / 2.0f,
			(point1.y + point2.y) / 2.0f,
			(point1.z + point2.z) / 2.0f);

		// add vertex makes sure point is on unit sphere
		int32 i = addVertex(middle);

		// store it, return index
		middlePointIndexCache.insert(std::make_pair(key, i));
		return i;
	};

	struct TriangleIndices
	{
		int32 v1;
		int32 v2;
		int32 v3;
		TriangleIndices(int32 _v1, int32 _v2, int32 _v3)
			: v1(_v1), v2(_v2), v3(_v3)
		{}
	};

	float t = (1.0f + sqrtf(5.0f)) / 2.0f;

	addVertex(vec3(-1, t, 0));
	addVertex(vec3(1, t, 0));
	addVertex(vec3(-1, -t, 0));
	addVertex(vec3(1, -t, 0));

	addVertex(vec3(0, -1, t));
	addVertex(vec3(0, 1, t));
	addVertex(vec3(0, -1, -t));
	addVertex(vec3(0, 1, -t));

	addVertex(vec3(t, 0, -1));
	addVertex(vec3(t, 0, 1));
	addVertex(vec3(-t, 0, -1));
	addVertex(vec3(-t, 0, 1));

	// create 20 triangles of the icosahedron
	std::vector<TriangleIndices> faces;

	// 5 faces around point 0
	faces.push_back(TriangleIndices(0, 11, 5));
	faces.push_back(TriangleIndices(0, 5, 1));
	faces.push_back(TriangleIndices(0, 1, 7));
	faces.push_back(TriangleIndices(0, 7, 10));
	faces.push_back(TriangleIndices(0, 10, 11));

	// 5 adjacent faces
	faces.push_back(TriangleIndices(1, 5, 9));
	faces.push_back(TriangleIndices(5, 11, 4));
	faces.push_back(TriangleIndices(11, 10, 2));
	faces.push_back(TriangleIndices(10, 7, 6));
	faces.push_back(TriangleIndices(7, 1, 8));

	// 5 faces around point 3
	faces.push_back(TriangleIndices(3, 9, 4));
	faces.push_back(TriangleIndices(3, 4, 2));
	faces.push_back(TriangleIndices(3, 2, 6));
	faces.push_back(TriangleIndices(3, 6, 8));
	faces.push_back(TriangleIndices(3, 8, 9));

	// 5 adjacent faces
	faces.push_back(TriangleIndices(4, 9, 5));
	faces.push_back(TriangleIndices(2, 4, 11));
	faces.push_back(TriangleIndices(6, 2, 10));
	faces.push_back(TriangleIndices(8, 6, 7));
	faces.push_back(TriangleIndices(9, 8, 1));

	// refine triangles
	for (auto i = 0u; i < iterations; i++)
	{
		std::vector<TriangleIndices> faces2;
		for(const auto& tri : faces)
		{
			// replace triangle by 4 triangles
			int32 a = getMiddlePoint(tri.v1, tri.v2);
			int32 b = getMiddlePoint(tri.v2, tri.v3);
			int32 c = getMiddlePoint(tri.v3, tri.v1);

			faces2.push_back(TriangleIndices(tri.v1, a, c));
			faces2.push_back(TriangleIndices(tri.v2, b, a));
			faces2.push_back(TriangleIndices(tri.v3, c, b));
			faces2.push_back(TriangleIndices(a, b, c));
		}
		faces = faces2;
	}

	// done, now add triangles to mesh
	for(const auto& tri : faces)
	{
		outGeometry.indices.push_back(tri.v1);
		outGeometry.indices.push_back(tri.v2);
		outGeometry.indices.push_back(tri.v3);
	}

}
