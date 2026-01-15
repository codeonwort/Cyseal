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

#define LOAD_PBRT_FILE       1
#define CREATE_TEST_MESHES   1

// living-room contains an invalid leaf texture only for pbrt format :/
// It's tungsten and mitsuba versions are fine.
struct PBRTLoadDesc
{
	std::wstring filename;
	vec3         position  = vec3(0, 0, 0);
	vec3         scale     = vec3(1, 1, 1);
	vec3         axis      = vec3(0, 1, 0); // For rotation
	float        angle     = 0.0f;          // For rotation
};
#define PBRT_LOAD_DESC_00    PBRTLoadDesc{ L"external/pbrt4/living-room/scene-v4.pbrt",             vec3(50.0f, -5.0f, 0.0f), vec3(10.0f) }
#define PBRT_LOAD_DESC_01    PBRTLoadDesc{ L"external/pbrt4_bedroom/bedroom/scene-v4.pbrt",         vec3(50.0f, -5.0f, 0.0f), vec3(10.0f) }
#define PBRT_LOAD_DESC_02    PBRTLoadDesc{ L"external/pbrt4_house/house/scene-v4.pbrt",             vec3(50.0f, -5.0f, 0.0f), vec3(10.0f) }
#define PBRT_LOAD_DESC_03    PBRTLoadDesc{ L"external/pbrt4_dining_room/dining-room/scene-v4.pbrt", vec3(50.0f, -5.0f, 0.0f), vec3(10.0f) }
// #todo-pbrt-parser: Need to increase VERTEX_BUFFER_POOL_SIZE and INDEX_BUFFER_POOL_SIZE (like 640 MiB each)
#define PBRT_LOAD_DESC_04    PBRTLoadDesc{ L"external/pbrt4_sanmiguel/sanmiguel-entry.pbrt",        vec3(50.0f, -5.0f, 0.0f), vec3(1.0f), vec3(1, 0, 0), 90 }
#define PBRT_LOAD_DESC       PBRT_LOAD_DESC_01
PBRTLoadDesc pbrtLoadDesc  = PBRT_LOAD_DESC;

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
	for (StaticMesh* pbrtMesh : pbrtMeshes)
	{
		delete pbrtMesh;
	}
	for (StaticMesh* pbrtInst : pbrtInstancedMeshes)
	{
		delete pbrtInst;
	}
	pbrtMeshes.clear();
	pbrtInstancedMeshes.clear();
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
		[texWeak = WeakPtr<TextureAsset>(albedoTexture), imageBlob](RenderCommandList& commandList)
		{
			SharedPtr<TextureAsset> tex = texWeak.lock();
			CHECK(tex != nullptr);

			TextureCreateParams params = TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
				imageBlob->width, imageBlob->height, 1);

			Texture* texture = gRenderDevice->createTexture(params);
			texture->uploadData(commandList, imageBlob->buffer, imageBlob->getRowPitch(), imageBlob->getSlicePitch());
			texture->setDebugName(TEXT("Texture_albedoTest"));

			tex->setGPUResource(SharedPtr<Texture>(texture));

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
		MesoGeometryAssets::addStaticMeshSections(ground, 0, geomAssets, material);

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
		MesoGeometryAssets::addStaticMeshSections(wallA, 0, geomAssets, material);

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
		MesoGeometryAssets::addStaticMeshSections(glassBox, 0, geomAssets, material);

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
			[texWeak = WeakPtr<TextureAsset>(skyboxTexture), skyboxBlobs](RenderCommandList& commandList) {
				SharedPtr<TextureAsset> tex = texWeak.lock();
				CHECK(tex != nullptr);

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

				tex->setGPUResource(SharedPtr<Texture>(texture));

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
	PBRT4Scene* pbrtScene = pbrtLoader.loadFromFile(pbrtLoadDesc.filename);
	
	MaterialAsset* M_curtains = pbrtLoader.findNamedMaterial("Curtains");
	if (M_curtains != nullptr)
	{
		M_curtains->bDoubleSided = true;
	}

	if (pbrtScene != nullptr)
	{
		PBRT4Scene::ToCyseal ret = PBRT4Scene::toCyseal(pbrtScene);

		pbrtMeshes = std::move(ret.rootObjects);
		for (StaticMesh* pbrtMesh : pbrtMeshes)
		{
			pbrtMesh->setPosition(pbrtLoadDesc.position);
			pbrtMesh->setScale(pbrtLoadDesc.scale);
			pbrtMesh->setRotation(pbrtLoadDesc.axis, pbrtLoadDesc.angle);

			scene->addStaticMesh(pbrtMesh);
		}

		pbrtInstancedMeshes = std::move(ret.instancedObjects);
		for (StaticMesh* pbrtInst : pbrtInstancedMeshes)
		{
			pbrtInst->setPosition(pbrtLoadDesc.position);
			pbrtInst->setScale(pbrtLoadDesc.scale);
			pbrtInst->setRotation(pbrtLoadDesc.axis, pbrtLoadDesc.angle);

			scene->addStaticMesh(pbrtInst);
		}

		ENQUEUE_RENDER_COMMAND(DeallocPbrtScene)(
			[pbrtScene](RenderCommandList& commandList) {
				commandList.enqueueDeferredDealloc(pbrtScene);
			}
		);
	}
}
