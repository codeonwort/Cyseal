#include "world2.h"

#include "rhi/vertex_buffer_pool.h"
#include "rhi/texture_manager.h"
#include "render/static_mesh.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"

#define SUN_DIRECTION        normalize(vec3(-1.0f, -1.0f, -1.0f))
#define SUN_ILLUMINANCE      (2.0f * vec3(1.0f, 1.0f, 1.0f))

#define CAMERA_POSITION      vec3(0.0f, 20.0f, 100.0f)
#define CAMERA_LOOKAT        vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

static void uploadMeshGeometry(Geometry* G,
	SharedPtr<VertexBufferAsset> positionBufferAsset,
	SharedPtr<VertexBufferAsset> nonPositionBufferAsset,
	SharedPtr<IndexBufferAsset> indexBufferAsset)
{
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
}

void World2::onInitialize()
{
	// #wip: camera's position and appState->cameraLocation
	// #wip: getAspectRatio()
	camera->lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	//camera->perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);
	appState->cameraLocation = CAMERA_POSITION;

	Geometry* planeGeometry = new Geometry;
	ProceduralGeometry::plane(*planeGeometry,
		100.0f, 100.0f, 2, 2, ProceduralGeometry::EPlaneNormal::Y);
	AABB localBounds = planeGeometry->localBounds;

	SharedPtr<VertexBufferAsset> positionBufferAsset = makeShared<VertexBufferAsset>();
	SharedPtr<VertexBufferAsset> nonPositionBufferAsset = makeShared<VertexBufferAsset>();
	SharedPtr<IndexBufferAsset> indexBufferAsset = makeShared<IndexBufferAsset>();
	uploadMeshGeometry(planeGeometry, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset);

	auto material = makeShared<Material>();
	material->albedoMultiplier = vec3(1.0f, 1.0f, 1.0f);
	material->albedoTexture = gTextureManager->getSystemTextureGrey2D();
	material->roughness = 0.0f;

	ground = new StaticMesh;
	ground->addSection(0, positionBufferAsset, nonPositionBufferAsset, indexBufferAsset, material, localBounds);
	ground->setPosition(vec3(0.0f, -10.0f, 0.0f));

	scene->addStaticMesh(ground);

	scene->sun.direction = SUN_DIRECTION;
	scene->sun.illuminance = SUN_ILLUMINANCE;
}

void World2::onTerminate()
{
	delete ground;
}

void World2::onTick(float deltaSeconds)
{
	//
}
