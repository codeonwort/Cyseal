#include "world1.h"

#include "rhi/render_device.h"
#include "rhi/vertex_buffer_pool.h"
#include "rhi/texture_manager.h"
#include "render/static_mesh.h"
#include "render/material.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"
#include "geometry/meso_geometry.h"
#include "world/gpu_resource_asset.h"
#include "loader/image_loader.h"
#include "loader/pbrt_loader.h"
#include "loader/ply_loader.h"

#include <array>

#define SUN_DIRECTION        normalize(vec3(-1.0f, -1.0f, -1.0f))
#define SUN_ILLUMINANCE      (2.0f * vec3(1.0f, 1.0f, 1.0f))

// #todo: DX12 + Null renderer -> SystemTexture2DWhite not free'd?
#define LOAD_PBRT_FILE       1
#define CREATE_TEST_MESHES   1

// living-room contains an invalid leaf texture only for pbrt format :/
// It's tungsten and mitsuba versions are fine.
#define PBRT_FILEPATH_00     L"external/pbrt4/living-room/scene-v4.pbrt"
#define PBRT_FILEPATH_01     L"external/pbrt4_bedroom/bedroom/scene-v4.pbrt"
#define PBRT_FILEPATH_02     L"external/pbrt4_house/house/scene-v4.pbrt"
#define PBRT_FILEPATH_03     L"external/pbrt4_dining_room/dining-room/scene-v4.pbrt"
#define PBRT_FILEPATH        PBRT_FILEPATH_01

#define CRUMPLED_MESHES      0

void World1::onInitialize()
{
	prepareScene();
}

void World1::onTick(float deltaSeconds)
{
	// Animate meshes.
	if (appState->rendererOptions.pathTracing == EPathTracingMode::Disabled)
	{
		static float elapsed = 0.0f;
		elapsed += deltaSeconds;

		if (CREATE_TEST_MESHES)
		{
			//ground->getTransform().setScale(1.0f + 0.2f * cosf(elapsed));
			ground->setRotation(vec3(0.0f, 1.0f, 0.0f), elapsed * 15.0f);

			// Animate balls to see if update of BLAS instance transforms is going well.
			meshSplatting.tick(deltaSeconds);
		}
	}
}

// #todo-fatal: Crash on terminate
void World1::onTerminate()
{
	if (CREATE_TEST_MESHES)
	{
		meshSplatting.destroyResources();
		delete ground;
		delete wallA;
		delete glassBox;
	}

#if LOAD_PBRT_FILE
	if (pbrtMesh != nullptr) delete pbrtMesh;
#endif

	scene->skyboxTexture.reset();
}

void World1::prepareScene()
{
	if (CREATE_TEST_MESHES) createTestMeshes();
	createSkybox();
	if (LOAD_PBRT_FILE) createPbrtResources();

	scene->sun.direction = SUN_DIRECTION;
	scene->sun.illuminance = SUN_ILLUMINANCE;
}

void World1::createTestMeshes()
{
	ImageLoader imageLoader;
	ImageLoadData* imageBlob = imageLoader.load(L"bee.png");
	if (imageBlob == nullptr)
	{
		imageBlob = new ImageLoadData;

		// Fill random image
		imageBlob->numComponents = 4;
		imageBlob->width = 256;
		imageBlob->height = 256;
		imageBlob->length = 256 * 256 * 4;
		imageBlob->buffer = new uint8[imageBlob->length];
		int32 p = 0;
		for (int32 y = 0; y < 256; ++y)
		{
			for (int32 x = 0; x < 256; ++x)
			{
				imageBlob->buffer[p] = (uint8)(x ^ y);
				imageBlob->buffer[p+1] = (uint8)(x ^ y);
				imageBlob->buffer[p+2] = (uint8)(x ^ y);
				imageBlob->buffer[p+3] = 0xff;
				p += 4;
			}
		}
	}

	SharedPtr<TextureAsset> albedoTexture = makeShared<TextureAsset>();
	ENQUEUE_RENDER_COMMAND(CreateTexture)(
		[albedoTexture, imageBlob](RenderCommandList& commandList)
		{
			TextureCreateParams params = TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
				imageBlob->width, imageBlob->height, 1);

			Texture* texture = gRenderDevice->createTexture(params);
			texture->uploadData(commandList, imageBlob->buffer, imageBlob->getRowPitch(), imageBlob->getSlicePitch());
			texture->setDebugName(TEXT("Texture_albedoTest"));

			albedoTexture->setGPUResource(SharedPtr<Texture>(texture));

			commandList.enqueueDeferredDealloc(imageBlob);
		}
	);

	meshSplatting.createResources(
		MeshSplatting::CreateParams{
			.center = vec3(0.0f, -4.0f, 0.0f),
			.radius = 16.0f,
			.height = 20.0f,
			.numLoop = 2,
			.numMeshes = 32
		}
	);
	for (StaticMesh* sm : meshSplatting.getStaticMeshes())
	{
		scene->addStaticMesh(sm);
	}

	// Ground
	{
		Geometry* planeGeometry = new Geometry;
#if CRUMPLED_MESHES
		ProceduralGeometry::crumpledPaper(*planeGeometry,
			100.0f, 100.0f, 16, 16,
			2.0f,
			ProceduralGeometry::EPlaneNormal::Y);
#else
		ProceduralGeometry::plane(*planeGeometry,
			100.0f, 100.0f, 2, 2,
			ProceduralGeometry::EPlaneNormal::Y);
#endif
		MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(planeGeometry);

		auto material = makeShared<MaterialAsset>();
		material->albedoMultiplier = vec3(0.1f);
		material->albedoTexture = gTextureManager->getSystemTextureWhite2D();
		material->roughness = 0.05f;
		material->bDoubleSided = true;

		ground = new StaticMesh;
		ground->setPosition(vec3(0.0f, -10.0f, 0.0f));
		MesoGeometryAssets::addStaticMeshSections(ground, geomAssets, material);

		scene->addStaticMesh(ground);
	}

	// wallA
	{
		Geometry* planeGeometry = new Geometry;
#if CRUMPLED_MESHES
		ProceduralGeometry::crumpledPaper(*planeGeometry,
			50.0f, 50.0f, 16, 16,
			1.0f,
			ProceduralGeometry::EPlaneNormal::X);
#else
		ProceduralGeometry::plane(*planeGeometry,
			50.0f, 50.0f, 2, 2,
			ProceduralGeometry::EPlaneNormal::X);
#endif
		MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(planeGeometry);

		auto material = makeShared<MaterialAsset>();
		material->albedoMultiplier = vec3(0.1f);
		material->albedoTexture = albedoTexture;
		material->roughness = 0.1f;
		material->bDoubleSided = true;

		wallA = new StaticMesh;
		wallA->setPosition(vec3(-25.0f, 0.0f, 0.0f));
		wallA->setRotation(vec3(0.0f, 0.0f, 1.0f), -10.0f);
		MesoGeometryAssets::addStaticMeshSections(wallA, geomAssets, material);

		scene->addStaticMesh(wallA);
	}

	// glassBox
	{
		Geometry* geometry = new Geometry;
		ProceduralGeometry::icosphere(*geometry, 1);
		AABB localBounds = geometry->localBounds;

		auto material = makeShared<MaterialAsset>();
		material->materialID = EMaterialId::Glass;
		material->albedoMultiplier = vec3(0.0f);
		material->albedoTexture = gTextureManager->getSystemTextureWhite2D();
		material->roughness = 0.1f;
		material->indexOfRefraction = IoR::CrownGlass;

		MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(geometry);

		glassBox = new StaticMesh;
		glassBox->setScale(10.0f);
		MesoGeometryAssets::addStaticMeshSections(glassBox, geomAssets, material);

		scene->addStaticMesh(glassBox);
	}
}

void World1::createSkybox()
{
	ImageLoader imageLoader;

	// Skybox
	std::wstring skyboxFilepaths[] = {
		L"skybox_Footballfield/posx.jpg", L"skybox_Footballfield/negx.jpg",
		L"skybox_Footballfield/posy.jpg", L"skybox_Footballfield/negy.jpg",
		L"skybox_Footballfield/posz.jpg", L"skybox_Footballfield/negz.jpg",
	};
	std::array<ImageLoadData*, 6> skyboxBlobs = { nullptr, };
	bool bValidSkyboxBlobs = true;
	for (uint32 i = 0; i < 6; ++i)
	{
		skyboxBlobs[i] = imageLoader.load(skyboxFilepaths[i]);
		bValidSkyboxBlobs = bValidSkyboxBlobs
			&& (skyboxBlobs[i] != nullptr)
			&& (skyboxBlobs[i]->width == skyboxBlobs[0]->width)
			&& (skyboxBlobs[i]->height == skyboxBlobs[0]->height);
	}
	if (bValidSkyboxBlobs)
	{
		SharedPtr<TextureAsset> skyboxTexture = makeShared<TextureAsset>();
		ENQUEUE_RENDER_COMMAND(CreateSkybox)(
			[skyboxTexture, skyboxBlobs](RenderCommandList& commandList) {
				TextureCreateParams params = TextureCreateParams::textureCube(
					EPixelFormat::R8G8B8A8_UNORM,
					ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
					skyboxBlobs[0]->width, skyboxBlobs[0]->height, 1);

				Texture* texture = gRenderDevice->createTexture(params);
				for (uint32 i = 0; i < 6; ++i)
				{
					texture->uploadData(commandList,
						skyboxBlobs[i]->buffer,
						skyboxBlobs[i]->getRowPitch(),
						skyboxBlobs[i]->getSlicePitch(),
						i);
				}
				texture->setDebugName(TEXT("Texture_skybox"));

				skyboxTexture->setGPUResource(SharedPtr<Texture>(texture));

				for (ImageLoadData* imageBlob : skyboxBlobs)
				{
					commandList.enqueueDeferredDealloc(imageBlob);
				}
			}
		);
		scene->skyboxTexture = skyboxTexture;
	}
}

void World1::createPbrtResources()
{
	// #todo-pathtracing: Something messed up if the pbrt mesh is added prior to other meshes.
	// Currently only pbrt mesh contains multiple mesh sections.
	// It's highly suspicious that mesh index, gpu scene item index, and material index are out of sync.
	PBRT4Loader pbrtLoader;
	PBRT4Scene* pbrtScene = pbrtLoader.loadFromFile(PBRT_FILEPATH);
	
	MaterialAsset* M_curtains = pbrtLoader.findLoadedMaterial("Curtains");
	if (M_curtains != nullptr)
	{
		M_curtains->bDoubleSided = true;
	}

	if (pbrtScene != nullptr)
	{
		const size_t numTriangleMeshes = pbrtScene->triangleMeshes.size();
		const size_t numPbrtMeshes = pbrtScene->plyMeshes.size();
		const size_t totalSubMeshes = numTriangleMeshes + numPbrtMeshes;
		std::vector<Geometry*> pbrtGeometries(totalSubMeshes, nullptr);
		std::vector<SharedPtr<VertexBufferAsset>> positionBufferAssets(totalSubMeshes, nullptr);
		std::vector<SharedPtr<VertexBufferAsset>> nonPositionBufferAssets(totalSubMeshes, nullptr);
		std::vector<SharedPtr<IndexBufferAsset>> indexBufferAssets(totalSubMeshes, nullptr);
		std::vector<SharedPtr<MaterialAsset>> subMaterials(totalSubMeshes, nullptr);
		for (size_t i = 0; i < totalSubMeshes; ++i)
		{
			Geometry* pbrtGeometry = pbrtGeometries[i] = new Geometry;

			if (i < numTriangleMeshes)
			{
				auto& triMesh = pbrtScene->triangleMeshes[i];

				pbrtGeometry->positions = std::move(triMesh.positionBuffer);
				pbrtGeometry->normals = std::move(triMesh.normalBuffer);
				pbrtGeometry->texcoords = std::move(triMesh.texcoordBuffer);
				pbrtGeometry->indices = std::move(triMesh.indexBuffer);
				pbrtGeometry->recalculateNormals();
				pbrtGeometry->finalize();

				subMaterials[i] = triMesh.material;
			}
			else
			{
				PLYMesh* plyMesh = pbrtScene->plyMeshes[i - numTriangleMeshes];

				pbrtGeometry->positions = std::move(plyMesh->positionBuffer);
				pbrtGeometry->normals = std::move(plyMesh->normalBuffer);
				pbrtGeometry->texcoords = std::move(plyMesh->texcoordBuffer);
				pbrtGeometry->indices = std::move(plyMesh->indexBuffer);
				pbrtGeometry->recalculateNormals();
				pbrtGeometry->finalize();

				subMaterials[i] = plyMesh->material;
			}

			positionBufferAssets[i] = makeShared<VertexBufferAsset>();
			nonPositionBufferAssets[i] = makeShared<VertexBufferAsset>();
			indexBufferAssets[i] = makeShared<IndexBufferAsset>();
		}

		auto fallbackMaterial = makeShared<MaterialAsset>();
		fallbackMaterial->albedoMultiplier = vec3(1.0f, 1.0f, 1.0f);
		fallbackMaterial->albedoTexture = gTextureManager->getSystemTextureGrey2D();
		fallbackMaterial->roughness = 1.0f;

		pbrtMesh = new StaticMesh;
		for (size_t i = 0; i < totalSubMeshes; ++i)
		{
			auto material = (subMaterials[i] != nullptr) ? subMaterials[i] : fallbackMaterial;
			MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(pbrtGeometries[i]);
			MesoGeometryAssets::addStaticMeshSections(pbrtMesh, geomAssets, material);
		}
		pbrtMesh->setPosition(vec3(50.0f, -5.0f, 0.0f));
		pbrtMesh->setScale(10.0f);

		scene->addStaticMesh(pbrtMesh);
	}
}
