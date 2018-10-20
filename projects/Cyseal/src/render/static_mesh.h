#pragma once

#include "core/core_minimal.h"
#include <vector>

class VertexBuffer;
class IndexBuffer;
class Material;

class StaticMeshSection
{

public:
	VertexBuffer* vertexBuffer;
	IndexBuffer*  indexBuffer;
	Material*     material;

};

class StaticMesh
{

public:
	virtual ~StaticMesh();

	void addSection(VertexBuffer* vertexBuffer, IndexBuffer* indexBuffer, Material* material);

	inline const std::vector<StaticMeshSection>& getSections() const { return sections; }

private:
	std::vector<StaticMeshSection> sections;

};
