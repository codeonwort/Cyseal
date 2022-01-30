#pragma once

#include "core/core_minimal.h"
#include <vector>

class VertexBuffer;
class IndexBuffer;
class Material;

class StaticMeshSection
{
public:
	VertexBuffer* positionBuffer;
	IndexBuffer*  indexBuffer;
	Material*     material;
};

class StaticMesh
{
public:
	virtual ~StaticMesh();

	void addSection(VertexBuffer* positionBuffer, IndexBuffer* indexBuffer, Material* material);

	inline const std::vector<StaticMeshSection>& getSections() const { return sections; }

	inline Transform& getTransform() { return transform; }
	inline const Transform& getTransform() const { return transform; }

private:
	std::vector<StaticMeshSection> sections;

	Transform transform;
};
