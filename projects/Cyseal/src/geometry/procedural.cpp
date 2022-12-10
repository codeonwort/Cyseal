#include "procedural.h"
#include "core/cymath.h"
#include "core/assertion.h"
#include <map>

#define pi 3.14159265358979323846f

namespace ProceduralGeometry
{

	void plane(
		Geometry& outGeometry,
		float sizeX /*= 1.0f */, float sizeY /*= 1.0f */,
		uint32 numCellsX /*= 1*/, uint32 numCellsY /*= 1*/,
		EPlaneNormal up /*= EPlaneNormal::Z*/)
	{
		CHECK(numCellsX > 0 && numCellsY > 0);

		const uint32 numVertices = (numCellsX + 1) * (numCellsY + 1);
		const float segW = sizeX / numCellsX, segH = sizeY / numCellsY;
		const float x0 = -0.5f * sizeY, y0 = -0.5f * sizeY;

		outGeometry.resizeNumVertices(numVertices);
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
	}

	void crumpledPaper(
		Geometry& outGeometry,
		float sizeX, float sizeY,
		uint32 numCellsX, uint32 numCellsY,
		float peak,
		EPlaneNormal up /*= EPlaneNormal::Z*/)
	{
		ProceduralGeometry::plane(outGeometry, sizeX, sizeY, numCellsX, numCellsY, up);

		// Add random spike
		for (size_t i = 0; i < outGeometry.positions.size(); ++i)
		{
			vec3 v = outGeometry.normals[i];
			v *= peak * Cymath::randFloat();

			outGeometry.positions[i] += v;
		}

		outGeometry.recalculateNormals();
		outGeometry.finalize();
	}

	void cube(
		Geometry& outGeometry,
		float sizeX /*= 1.0f*/, float sizeY /*= 1.0f*/, float sizeZ /*= 1.0f*/)
	{
		outGeometry.reserveNumVertices(24);
		outGeometry.reserveNumIndices(36);

		float x = 0.5f * sizeX;
		float y = 0.5f * sizeY;
		float z = 0.5f * sizeZ;

		auto pushNormals = [ns = &(outGeometry.normals)](const vec3& n) {
			for (auto i = 0; i < 4; ++i) ns->push_back(n);
		};
		auto pushIndices = [ix = &(outGeometry.indices)](uint32 baseIx) {
			ix->push_back(baseIx + 0); ix->push_back(baseIx + 2); ix->push_back(baseIx + 1);
			ix->push_back(baseIx + 0); ix->push_back(baseIx + 3); ix->push_back(baseIx + 2);
		};

		// front
		outGeometry.positions.push_back(vec3(-x, +y, +z));
		outGeometry.positions.push_back(vec3(+x, +y, +z));
		outGeometry.positions.push_back(vec3(+x, -y, +z));
		outGeometry.positions.push_back(vec3(-x, -y, +z));
		pushNormals(vec3(0.0f, 0.0f, 1.0f));
		pushIndices(0);

		// back
		outGeometry.positions.push_back(vec3(+x, +y, -z));
		outGeometry.positions.push_back(vec3(-x, +y, -z));
		outGeometry.positions.push_back(vec3(-x, -y, -z));
		outGeometry.positions.push_back(vec3(+x, -y, -z));
		pushNormals(vec3(0.0f, 0.0f, -1.0f));
		pushIndices(4);

		// right
		outGeometry.positions.push_back(vec3(+x, +y, +z));
		outGeometry.positions.push_back(vec3(+x, +y, -z));
		outGeometry.positions.push_back(vec3(+x, -y, -z));
		outGeometry.positions.push_back(vec3(+x, -y, +z));
		pushNormals(vec3(0.0f, 0.0f, 1.0f));
		pushIndices(8);

		// left
		outGeometry.positions.push_back(vec3(-x, +y, -z));
		outGeometry.positions.push_back(vec3(-x, +y, +z));
		outGeometry.positions.push_back(vec3(-x, -y, +z));
		outGeometry.positions.push_back(vec3(-x, -y, -z));
		pushNormals(vec3(0.0f, 0.0f, -1.0f));
		pushIndices(12);

		// up
		outGeometry.positions.push_back(vec3(-x, +y, -z));
		outGeometry.positions.push_back(vec3(+x, +y, -z));
		outGeometry.positions.push_back(vec3(+x, +y, +z));
		outGeometry.positions.push_back(vec3(-x, +y, +z));
		pushNormals(vec3(0.0f, 1.0f, 0.0f));
		pushIndices(16);

		// down
		outGeometry.positions.push_back(vec3(-x, -y, +z));
		outGeometry.positions.push_back(vec3(+x, -y, +z));
		outGeometry.positions.push_back(vec3(+x, -y, -z));
		outGeometry.positions.push_back(vec3(-x, -y, -z));
		pushNormals(vec3(0.0f, -1.0f, 0.0f));
		pushIndices(20);

		// Same texcoord distribution for all faces
		for (auto i = 0; i < 6; ++i)
		{
			outGeometry.texcoords.push_back(vec2(0.0f, 0.0f));
			outGeometry.texcoords.push_back(vec2(1.0f, 0.0f));
			outGeometry.texcoords.push_back(vec2(1.0f, 1.0f));
			outGeometry.texcoords.push_back(vec2(0.0f, 1.0f));
		}

		outGeometry.finalize();
	}

	// http://blog.andreaskahler.com/2009/06/creating-icosphere-mesh-in-code.html
	void icosphere(Geometry& outGeometry, uint32 iterations)
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
		outGeometry.resizeNumVertices(tempPositions.size());
		for (size_t i = 0; i < tempPositions.size(); ++i)
		{
			outGeometry.positions[i] = tempPositions[i];
			outGeometry.normals[i] = normalize(tempPositions[i]);
			outGeometry.texcoords[i].x = 0.5f * outGeometry.normals[i].x + 0.5f;
			outGeometry.texcoords[i].y = 0.5f * outGeometry.normals[i].y + 0.5f;
		}
		for (const auto& tri : faces)
		{
			// CCW winding
			outGeometry.indices.push_back(tri.v1);
			outGeometry.indices.push_back(tri.v2);
			outGeometry.indices.push_back(tri.v3);
		}
		outGeometry.finalize();
	}

	void spikeBall(
		Geometry& outGeometry,
		uint32 subdivisions, float phase, float peak)
	{
		icosphere(outGeometry, subdivisions);

		// Add random spike
		float t = phase;
		for (vec3& pos : outGeometry.positions)
		{
			float spike = 1.0f + peak * 0.5f * (1.0f + Cymath::sin(t));
			pos *= spike;
			t += 0.137f;
		}

		outGeometry.recalculateNormals();
		outGeometry.finalize();
	}

	void twistedCube(
		Geometry& outGeometry,
		float width, float height,
		uint32 numLayers, float layerHeight, float angleDelta)
	{
		// 4 sides per layer = (16 * N) vertices, top & bottom = 8 vertices
		outGeometry.reserveNumVertices(16 * numLayers + 8);
		outGeometry.reserveNumIndices(24 * numLayers + 12);
		angleDelta = angleDelta * pi / 180.0f;
		const float rightAngle = pi / 2.0f;

		auto rotate2D = [](float& x, float& y, float dt) {
			float x2 = x * cos(dt) - y * sin(dt);
			float y2 = x * sin(dt) + y * cos(dt);
			x = x2; y = y2;
		};

		float x0 = -0.5f * width, y0 = 0.5f * height;
		float x1 = +0.5f * width, y1 = 0.5f * height;
		float z = 0.0f;
		float angle = 0.0f;
		uint32 ix = 0;
		for (uint32 layer = 0; layer <= numLayers; ++layer)
		{
			outGeometry.positions.push_back(vec3(x0, z, y0));
			outGeometry.positions.push_back(vec3(x1, z, y1));
			outGeometry.texcoords.push_back(vec2(0.0f, (float)layer / numLayers));
			outGeometry.texcoords.push_back(vec2(1.0f, (float)layer / numLayers));

			if (layer != numLayers)
			{
				outGeometry.indices.push_back(ix);
				outGeometry.indices.push_back(ix + 1);
				outGeometry.indices.push_back(ix + 3);
				outGeometry.indices.push_back(ix);
				outGeometry.indices.push_back(ix + 3);
				outGeometry.indices.push_back(ix + 2);
				angle += angleDelta;
				z += layerHeight;
			}
			ix += 2;

			rotate2D(x0, y0, angleDelta);
			rotate2D(x1, y1, angleDelta);
		}

		uint32 sideNumPos = (uint32)outGeometry.positions.size();
		uint32 sideNumIx = (uint32)outGeometry.indices.size();
		for (uint32 sideIx = 1; sideIx <= 3; ++sideIx)
		{
			for (uint32 i = 0; i < sideNumPos; ++i)
			{
				vec3 p = outGeometry.positions[i];
				rotate2D(p.x, p.z, rightAngle * sideIx);
				outGeometry.positions.push_back(p);
				outGeometry.texcoords.push_back(outGeometry.texcoords[i]);
			}
			for (uint32 i = 0; i < sideNumIx; ++i)
			{
				outGeometry.indices.push_back(outGeometry.indices[i] + sideNumPos * sideIx);
			}
		}

		ix = (uint32)outGeometry.positions.size();
		outGeometry.positions.push_back(vec3(-0.5f * width, 0.0f, -0.5f * height));
		outGeometry.positions.push_back(vec3(+0.5f * width, 0.0f, -0.5f * height));
		outGeometry.positions.push_back(vec3(-0.5f * width, 0.0f, +0.5f * height));
		outGeometry.positions.push_back(vec3(+0.5f * width, 0.0f, +0.5f * height));
		outGeometry.texcoords.push_back(vec2(0.0f, 0.0f));
		outGeometry.texcoords.push_back(vec2(1.0f, 0.0f));
		outGeometry.texcoords.push_back(vec2(0.0f, 1.0f));
		outGeometry.texcoords.push_back(vec2(1.0f, 1.0f));
		outGeometry.indices.push_back(ix);
		outGeometry.indices.push_back(ix + 1);
		outGeometry.indices.push_back(ix + 3);
		outGeometry.indices.push_back(ix);
		outGeometry.indices.push_back(ix + 3);
		outGeometry.indices.push_back(ix + 2);

		ix += 4;
		vec3 top0 = vec3(-0.5f * width, z, -0.5f * height);
		vec3 top1 = vec3(+0.5f * width, z, -0.5f * height);
		vec3 top2 = vec3(-0.5f * width, z, +0.5f * height);
		vec3 top3 = vec3(+0.5f * width, z, +0.5f * height);
		rotate2D(top0.x, top0.z, angle);
		rotate2D(top1.x, top1.z, angle);
		rotate2D(top2.x, top2.z, angle);
		rotate2D(top3.x, top3.z, angle);
		outGeometry.positions.push_back(top0);
		outGeometry.positions.push_back(top1);
		outGeometry.positions.push_back(top2);
		outGeometry.positions.push_back(top3);
		outGeometry.texcoords.push_back(vec2(0.0f, 0.0f));
		outGeometry.texcoords.push_back(vec2(1.0f, 0.0f));
		outGeometry.texcoords.push_back(vec2(0.0f, 1.0f));
		outGeometry.texcoords.push_back(vec2(1.0f, 1.0f));
		outGeometry.indices.push_back(ix);
		outGeometry.indices.push_back(ix + 3);
		outGeometry.indices.push_back(ix + 1);
		outGeometry.indices.push_back(ix);
		outGeometry.indices.push_back(ix + 2);
		outGeometry.indices.push_back(ix + 3);

		outGeometry.recalculateNormals();
		outGeometry.finalize();
	}

} // end of namespace ProceduralGeometry
