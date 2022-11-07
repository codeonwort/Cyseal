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

void StaticMesh::addSection(
	uint32 lod,
	VertexBuffer* positionBuffer,
	VertexBuffer* nonPositionBuffer,
	IndexBuffer* indexBuffer,
	Material* material)
{
	StaticMeshSection section;
	section.positionBuffer    = positionBuffer;
	section.nonPositionBuffer = nonPositionBuffer;
	section.indexBuffer       = indexBuffer;
	section.material          = material;

	if (LODs.size() <= lod)
	{
		LODs.resize(lod + 1);
	}
	LODs[lod].sections.emplace_back(section);
}
