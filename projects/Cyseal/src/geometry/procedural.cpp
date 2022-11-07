#include "procedural.h"
#include "core/cymath.h"
#include "core/assertion.h"
#include <map>

namespace ProceduralGeometry
{

	void plane(
		Geometry& outGeometry,
		float sizeX, float sizeY,
		uint32 numCellsX /*= 1*/, uint32 numCellsY /*= 1*/,
		EPlaneNormal up /*= EPlaneNormal::Z*/)
	{
		CHECK(numCellsX > 0 && numCellsY > 0);

		const uint32 numVertices = (numCellsX + 1) * (numCellsY + 1);
		const float segW = sizeX / numCellsX, segH = sizeY / numCellsY;
		const float x0 = -0.5f * sizeY, y0 = -0.5f * sizeY;

		outGeometry.initNumVertices(numVertices);
		outGeometry.indices.resize(numCellsX * numCellsY * 6);

		auto& positions = outGeometry.positions;
		auto& normals = outGeometry.normals;
		auto& texcoords = outGeometry.texcoords;
		auto& indices = outGeometry.indices;

		size_t k = 0;
		for (auto i = 0u; i <= numCellsX; i++) {
			for (auto j = 0u; j <= numCellsY; j++) {
				positions[k] = vec3(x0 + segW * j, y0 + segH * i, 0.0f);
				//texcoords[k] = vec2((float)j / numCellsX, (float)i / numCellsY);
				texcoords[k] = vec2((float)j, (float)i);
				normals[k] = vec3(0.0f, 0.0f, 1.0f);
				k++;
			}
		}

		if (up == EPlaneNormal::X) {
			for (auto i = 0u; i < positions.size(); ++i) {
				positions[i].z = -positions[i].x;
				positions[i].x = 0.0f;
				normals[i] = vec3(1.0f, 0.0f, 0.0f);
			}
		} else if (up == EPlaneNormal::Y) {
			for (auto i = 0u; i < positions.size(); ++i) {
				positions[i].z = -positions[i].y;
				positions[i].y = 0.0f;
				normals[i] = vec3(0.0f, 1.0f, 0.0f);
			}
		}

		k = 0;
		for (auto i = 0u; i < numCellsY; i++) {
			auto baseY = i * (numCellsX + 1);
			for (auto j = 0u; j < numCellsX; j++) {
				indices[k] = baseY + j;
				indices[k + 1] = baseY + j + 1;
				indices[k + 2] = baseY + j + (numCellsX + 1);
				indices[k + 3] = baseY + j + 1;
				indices[k + 4] = baseY + j + (numCellsX + 1) + 1;
				indices[k + 5] = baseY + j + (numCellsX + 1);
				k += 6;
			}
		}

		outGeometry.finalize();

		// #todo-wip: CCW rule
#if 0
		for (uint32 i = 0; i < indices.size(); i += 3)
		{
			uint32 temp = indices[i + 1];
			indices[i + 1] = indices[i + 2];
			indices[i + 2] = temp;
		}
#endif
	}

	// http://blog.andreaskahler.com/2009/06/creating-icosphere-mesh-in-code.html
	void icosphere(uint32 iterations, Geometry& outGeometry)
	{
		std::map<int64, int32> middlePointIndexCache;
		int32 index = 0;

		std::vector<vec3> tempPositions;

		auto addVertex = [&](const vec3& v) -> int32
		{
			tempPositions.push_back(normalize(v));
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
			vec3 point1 = tempPositions[p1];
			vec3 point2 = tempPositions[p2];
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
			for (const auto& tri : faces)
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
		outGeometry.initNumVertices(tempPositions.size());
		for (size_t i = 0; i < tempPositions.size(); ++i)
		{
			outGeometry.positions[i] = tempPositions[i];
			outGeometry.normals[i] = normalize(tempPositions[i]);
			outGeometry.texcoords[i].x = 0.5f * outGeometry.normals[i].x + 0.5f;
			outGeometry.texcoords[i].y = 0.5f * outGeometry.normals[i].y + 0.5f;
		}
		for (const auto& tri : faces)
		{
			outGeometry.indices.push_back(tri.v1);
			outGeometry.indices.push_back(tri.v2);
			outGeometry.indices.push_back(tri.v3);
		}
		outGeometry.finalize();
	}

	void spikeBall(uint32 subdivisions, float phase, float peak, Geometry& outGeometry)
	{
		icosphere(subdivisions, outGeometry);

		// Add random spike
		float t = phase;
		for (vec3& pos : outGeometry.positions)
		{
			float spike = 1.0f + peak * 0.5f * (1.0f + Cymath::sin(t));
			pos *= spike;
			t += 0.137f;
		}

		// Recalculate vertex normals
		outGeometry.normals.resize(outGeometry.positions.size(), vec3(0.0f, 0.0f, 0.0f));
		for (uint32 i = 0; i < outGeometry.indices.size(); i += 3)
		{
			uint32 i0 = outGeometry.indices[i + 0];
			uint32 i1 = outGeometry.indices[i + 1];
			uint32 i2 = outGeometry.indices[i + 2];

			vec3 p1 = outGeometry.positions[i1] - outGeometry.positions[i0];
			vec3 p2 = outGeometry.positions[i2] - outGeometry.positions[i0];
			vec3 n = cross(p1, p2);
			if (dot(n, n) > 1e-6)
			{
				n = normalize(n);
				outGeometry.normals[i0] += n;
				outGeometry.normals[i1] += n;
				outGeometry.normals[i2] += n;
			}
		}
		for (uint32 i = 0; i < outGeometry.normals.size(); ++i)
		{
			outGeometry.normals[i] = normalize(outGeometry.normals[i]);
		}
		outGeometry.finalize();
	}

} // end of namespace ProceduralGeometry
