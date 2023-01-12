#include "mesh_splatting.h"

#include "rhi/render_command.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/texture_manager.h"
#include "render/static_mesh.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"

void MeshSplatting::createResources(const CreateParams& createParams)
{
	std::shared_ptr<TextureAsset> baseTextures[] = {
		gTextureManager->getSystemTextureWhite2D(),
		gTextureManager->getSystemTextureRed2D(),
		gTextureManager->getSystemTextureGreen2D(),
		gTextureManager->getSystemTextureBlue2D(),
	};
	std::vector<std::shared_ptr<Material>> baseMaterials;
	for (const auto& baseTex : baseTextures)
	{
		auto material = std::make_shared<Material>();
		material->albedoTexture = baseTex;
		material->albedoMultiplier[0] = 0.1f;
		material->albedoMultiplier[1] = 0.1f;
		material->albedoMultiplier[2] = 0.1f;
		material->roughness = 0.0f;
		baseMaterials.push_back(material);
	}

	for (uint32 meshIx = 0; meshIx < createParams.numMeshes; ++meshIx)
	{
		StaticMesh* staticMesh = new StaticMesh;

		for (uint32 lod = 0; lod < 2; ++lod)
		{
			Geometry* geom = new Geometry;
			if (meshIx % 2)
			{
				ProceduralGeometry::icosphere(*geom, lod == 0 ? 3 : 1);
			}
			else
			{
				ProceduralGeometry::cube(*geom, 1.0f, 1.0f, 1.0f);
			}

			auto positionBufferAsset = std::make_shared<VertexBufferAsset>();
			auto nonPositionBufferAsset = std::make_shared<VertexBufferAsset>();
			auto indexBufferAsset = std::make_shared<IndexBufferAsset>();

			ENQUEUE_RENDER_COMMAND(UploadMeshBuffers)(
				[positionBufferAsset,
				nonPositionBufferAsset,
				indexBufferAsset,
				geom](RenderCommandList& commandList)
				{
					auto positionBuffer = gVertexBufferPool->suballocate(geom->getPositionBufferTotalBytes());
					auto nonPositionBuffer = gVertexBufferPool->suballocate(geom->getNonPositionBufferTotalBytes());
					auto indexBuffer = gIndexBufferPool->suballocate(geom->getIndexBufferTotalBytes(), geom->getIndexFormat());

					positionBuffer->updateData(&commandList, geom->getPositionBlob(), geom->getPositionStride());
					nonPositionBuffer->updateData(&commandList, geom->getNonPositionBlob(), geom->getNonPositionStride());
					indexBuffer->updateData(&commandList, geom->getIndexBlob(), geom->getIndexFormat());

					positionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(positionBuffer));
					nonPositionBufferAsset->setGPUResource(std::shared_ptr<VertexBuffer>(nonPositionBuffer));
					indexBufferAsset->setGPUResource(std::shared_ptr<IndexBuffer>(indexBuffer));

					commandList.enqueueDeferredDealloc(geom);
				}
			);

			auto material = baseMaterials[meshIx % baseMaterials.size()];
			staticMesh->addSection(lod, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset, material);
		}

		float theta = (float)createParams.numLoop * (2.0f * Cymath::PI) * (meshIx / (float)createParams.numMeshes);
		vec3 deltaPos = createParams.radius * vec3(Cymath::cos(theta), 0.0f, Cymath::sin(theta));
		deltaPos.y += (meshIx % 2) ? 0.0f : Cymath::randFloatRange(-1.0f, 1.0f);
		deltaPos.y += createParams.height * (meshIx / (float)createParams.numMeshes);
		vec3 startPos = createParams.center + deltaPos;

		staticMeshesStartPos.push_back(startPos);
		staticMesh->setPosition(startPos);
		staticMesh->setRotation(normalize(vec3(0.5f, 1.0f, 0.3f)), Cymath::randFloatRange(0.0f, 360.0f));
		staticMesh->setScale(3.0f);

		staticMeshes.push_back(staticMesh);
	}
}

void MeshSplatting::destroyResources()
{
	for (StaticMesh* sm : staticMeshes)
	{
		delete sm;
	}
	staticMeshes.clear();
}

void MeshSplatting::tick(float deltaSeconds)
{
	static float ballTime = 0.0f;
	ballTime += deltaSeconds;
	for (size_t i = 0; i < staticMeshes.size(); ++i)
	{
		vec3 p = staticMeshesStartPos[i];
		if (i % 3 == 0) p.x += 0.5f * Cymath::cos(ballTime);
		else if (i % 3 == 1) p.y += 0.5f * Cymath::cos(ballTime);
		else if (i % 3 == 2) p.z += 0.5f * Cymath::cos(ballTime);
		staticMeshes[i]->setPosition(p);

		//const float MESH_SCALE = 1.0f;
		//staticMeshes[i]->setScale(MESH_SCALE * (1.0f + 0.3f * Cymath::sin(i + ballTime)));
	}
}
