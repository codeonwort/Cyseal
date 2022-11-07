#pragma once

#include "core/core_minimal.h"
#include <vector>

class VertexBuffer;
class IndexBuffer;
class Material;

struct StaticMeshSection
{
	VertexBuffer* positionBuffer = nullptr;
	VertexBuffer* nonPositionBuffer = nullptr;
	IndexBuffer*  indexBuffer = nullptr;
	Material*     material = nullptr;
};

struct StaticMeshLOD
{
	std::vector<StaticMeshSection> sections;
};

class StaticMesh
{
public:
	virtual ~StaticMesh();

	void addSection(
		uint32 lod,
		VertexBuffer* positionBuffer,
		VertexBuffer* nonPositionBuffer,
		IndexBuffer* indexBuffer,
		Material* material);

	inline const std::vector<StaticMeshSection>& getSections(uint32 lod) const
	{
		CHECK(lod < LODs.size());
		return LODs[lod].sections;
	}

	inline Transform& getTransform() { return transform; }
	inline const Transform& getTransform() const { return transform; }

private:
	std::vector<StaticMeshLOD> LODs;

	Transform transform;
};
