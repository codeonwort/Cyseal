#include "static_mesh.h"
#include "gpu_resource.h"

StaticMesh::~StaticMesh()
{
	// #todo: Static meshes might share same buffers and materials
	//for (auto& section : sections)
	//{
	//	delete section.vertexBuffer;
	//	delete section.indexBuffer;
	//	if (section.material)
	//	{
	//		delete section.material;
	//	}
	//}
}

void StaticMesh::addSection(VertexBuffer* vertexBuffer, IndexBuffer* indexBuffer, Material* material)
{
	StaticMeshSection section;
	section.vertexBuffer = vertexBuffer;
	section.indexBuffer  = indexBuffer;
	section.material     = material;

	sections.emplace_back(section);
}
