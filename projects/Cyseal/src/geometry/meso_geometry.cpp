#include "meso_geometry.h"
#include "primitive.h"
#include "rhi/render_command.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/buffer.h"
#include "render/static_mesh.h"
#include "world/gpu_resource_asset.h"

bool MesoGeometry::needsToPartition(const Geometry* G, uint32 maxTriangleCount)
{
	const size_t totalTriangles = G->indices.size() / 3;
	return totalTriangles > maxTriangleCount;
}

std::vector<MesoGeometry>* MesoGeometry::partitionByTriangleCount(const Geometry* G, uint32 maxTriangleCount)
{
	const size_t totalTriangles = G->indices.size() / 3;
	const size_t numGeometries = (totalTriangles + maxTriangleCount - 1) / maxTriangleCount;
	
	std::vector<MesoGeometry>* mesoList = new std::vector<MesoGeometry>;

	uint32 firstTriangle = 0;
	uint32 remainingTriangles = (uint32)totalTriangles;
	for (size_t i = 0; i < numGeometries; ++i)
	{
		uint32 numTriangles = (std::min)(maxTriangleCount, remainingTriangles);

		MesoGeometry meso;
		meso.indices.reserve(numTriangles);

		std::vector<vec3> mesoPositions; // For local bounds
		mesoPositions.reserve(numTriangles * 3);

		for (uint32 j = firstTriangle; j < firstTriangle + numTriangles; ++j)
		{
			uint32 i0 = G->indices[j * 3 + 0];
			uint32 i1 = G->indices[j * 3 + 1];
			uint32 i2 = G->indices[j * 3 + 2];
			meso.indices.push_back(i0);
			meso.indices.push_back(i1);
			meso.indices.push_back(i2);

			mesoPositions.push_back(G->positions[i0]);
			mesoPositions.push_back(G->positions[i1]);
			mesoPositions.push_back(G->positions[i2]);
		}

		meso.localBounds = Geometry::calculateAABB(mesoPositions);

		mesoList->emplace_back(meso);

		firstTriangle += maxTriangleCount;
		remainingTriangles -= maxTriangleCount;
	}

	return mesoList;
}

MesoGeometryAssets MesoGeometryAssets::createFrom(const Geometry* G)
{
	MesoGeometryAssets assets;

	if (MesoGeometry::needsToPartition(G, 0xffff))
	{
		std::vector<MesoGeometry>* mesoList = MesoGeometry::partitionByTriangleCount(G, 0xffff);
		const size_t numMeso = mesoList->size();

		assets.positionBufferAsset = makeShared<VertexBufferAsset>();
		assets.nonPositionBufferAsset = makeShared<VertexBufferAsset>();
		assets.indexBufferAsset.resize(numMeso);
		assets.localBounds.resize(numMeso);
		for (size_t i = 0; i < numMeso; ++i)
		{
			assets.indexBufferAsset[i] = makeShared<IndexBufferAsset>();
			assets.localBounds[i] = (*mesoList)[i].localBounds;
		}

		ENQUEUE_RENDER_COMMAND(UploadMesoGeometries)(
			[G, mesoList, posAsset = assets.positionBufferAsset,
			nonposAsset = assets.nonPositionBufferAsset, idxAssets = assets.indexBufferAsset]
			(RenderCommandList& commandList)
			{
				auto positionBuffer = gVertexBufferPool->suballocate(G->getPositionBufferTotalBytes());
				positionBuffer->updateData(&commandList, G->getPositionBlob(), G->getPositionStride());
				posAsset->setGPUResource(SharedPtr<VertexBuffer>(positionBuffer));

				auto nonPositionBuffer = gVertexBufferPool->suballocate(G->getNonPositionBufferTotalBytes());
				nonPositionBuffer->updateData(&commandList, G->getNonPositionBlob(), G->getNonPositionStride());
				nonposAsset->setGPUResource(SharedPtr<VertexBuffer>(nonPositionBuffer));

				for (size_t i = 0; i < mesoList->size(); ++i)
				{
					const MesoGeometry& meso = mesoList->at(i);
					auto indexBuffer = gIndexBufferPool->suballocate(meso.getIndexBufferTotalBytes(), G->getIndexFormat());
					indexBuffer->updateData(&commandList, meso.getIndexBlob(), G->getIndexFormat());
					idxAssets[i]->setGPUResource(SharedPtr<IndexBuffer>(indexBuffer));
				}

				commandList.enqueueDeferredDealloc(G);
				commandList.enqueueDeferredDealloc(mesoList);
			}
		);
	}
	else
	{
		SharedPtr<VertexBufferAsset> positionBufferAsset = makeShared<VertexBufferAsset>();
		SharedPtr<VertexBufferAsset> nonPositionBufferAsset = makeShared<VertexBufferAsset>();
		SharedPtr<IndexBufferAsset> indexBufferAsset = makeShared<IndexBufferAsset>();
		AABB localBounds = G->localBounds;

		ENQUEUE_RENDER_COMMAND(UploadMeshGeometry)(
			[G, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset](RenderCommandList& commandList)
			{
				auto positionBuffer = gVertexBufferPool->suballocate(G->getPositionBufferTotalBytes());
				auto nonPositionBuffer = gVertexBufferPool->suballocate(G->getNonPositionBufferTotalBytes());
				auto indexBuffer = gIndexBufferPool->suballocate(G->getIndexBufferTotalBytes(), G->getIndexFormat());

				positionBuffer->updateData(&commandList, G->getPositionBlob(), G->getPositionStride());
				nonPositionBuffer->updateData(&commandList, G->getNonPositionBlob(), G->getNonPositionStride());
				indexBuffer->updateData(&commandList, G->getIndexBlob(), G->getIndexFormat());

				positionBufferAsset->setGPUResource(SharedPtr<VertexBuffer>(positionBuffer));
				nonPositionBufferAsset->setGPUResource(SharedPtr<VertexBuffer>(nonPositionBuffer));
				indexBufferAsset->setGPUResource(SharedPtr<IndexBuffer>(indexBuffer));

				commandList.enqueueDeferredDealloc(G);
			}
		);

		assets.positionBufferAsset = positionBufferAsset;
		assets.nonPositionBufferAsset = nonPositionBufferAsset;
		assets.indexBufferAsset.push_back(indexBufferAsset);
		assets.localBounds.push_back(localBounds);
	}

	return assets;
}

void MesoGeometryAssets::addStaticMeshSections(StaticMesh* mesh, const MesoGeometryAssets& assets, SharedPtr<MaterialAsset> material)
{
	for (size_t i = 0; i < assets.numMeso(); ++i)
	{
		mesh->addSection(0,
			assets.positionBufferAsset,
			assets.nonPositionBufferAsset,
			assets.indexBufferAsset[i],
			material,
			assets.localBounds[i]);
	}
}
