#include "primitive.h"

void Geometry::initNumVertices(size_t num)
{
	positions.resize(num);
	normals.resize(num);
	texcoords.resize(num);
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
