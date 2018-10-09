#pragma once

#include <list>

class VertexBuffer;
class IndexBuffer;
class Material;

class StaticMeshSection
{
	VertexBuffer* vertexBuffer;
	IndexBuffer* indexBuffer;
	Material* material;
};

class StaticMesh
{

public:
	//

private:
	std::list<StaticMeshSection> sections;

};
