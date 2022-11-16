#include "primitive.h"

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

	bFinalized = true;
}
