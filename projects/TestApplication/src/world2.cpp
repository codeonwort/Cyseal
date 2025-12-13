#include "world2.h"

#include "rhi/vertex_buffer_pool.h"
#include "rhi/texture_manager.h"
#include "render/static_mesh.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"
#include "geometry/meso_geometry.h"

#define SUN_DIRECTION        normalize(vec3(-1.0f, -1.0f, -1.0f))
#define SUN_ILLUMINANCE      (2.0f * vec3(1.0f, 1.0f, 1.0f))

#define CAMERA_POSITION      vec3(0.0f, 6.0f, 70.0f)
#define CAMERA_LOOKAT        vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f
#define BALL_ROWS            4
#define BALL_COLS            6
#define BALL_NUM_LOD         3

void World2::onInitialize()
{
	camera->lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	camera->perspective(CAMERA_FOV_Y, camera->getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

	{
		Geometry* planeGeometry = new Geometry;
		ProceduralGeometry::plane(*planeGeometry,
			100.0f, 100.0f, 2, 2, ProceduralGeometry::EPlaneNormal::Y);

		MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(planeGeometry);

		auto material = makeShared<MaterialAsset>();
		material->albedoMultiplier = vec3(1.0f, 1.0f, 1.0f);
		material->albedoTexture = gTextureManager->getSystemTextureGrey2D();
		material->roughness = 1.0f;

		ground = new StaticMesh;
		ground->setPosition(vec3(0.0f, -10.0f, 0.0f));
		MesoGeometryAssets::addStaticMeshSections(ground, 0, geomAssets, material);

		scene->addStaticMesh(ground);
	}
	{
		const uint32 TOTAL_BALLS = BALL_ROWS * BALL_COLS;
		//const uint32 TOTAL_MESHES = BALL_NUM_LOD * TOTAL_BALLS;
		
		SharedPtr<TextureAsset> baseTextures[] = {
			gTextureManager->getSystemTextureWhite2D(),
			gTextureManager->getSystemTextureRed2D(),
			gTextureManager->getSystemTextureGreen2D(),
			gTextureManager->getSystemTextureBlue2D(),
		};
		std::vector<SharedPtr<MaterialAsset>> baseMaterials;
		for (const auto& baseTex : baseTextures)
		{
			auto material = makeShared<MaterialAsset>();
			material->albedoTexture = baseTex;
			material->albedoMultiplier = vec3(0.5f, 0.5f, 0.5f);
			material->roughness = 1.0f;
			baseMaterials.push_back(material);
		}

		balls.reserve(TOTAL_BALLS);
		for (uint32 row = 0; row < BALL_ROWS; ++row)
		{
			for (uint32 col = 0; col < BALL_COLS; ++col)
			{
				float x = (float)col * 6.0f;
				float y = 1.0f;
				float z = 50.0f - (float)row * 10.0f;

				auto ball = new StaticMesh;
				ball->setScale(2.0f);
				ball->setPosition(vec3(x, y, z));
				balls.emplace_back(ball);
				scene->addStaticMesh(ball);

				for (uint32 lod = 0; lod < 3; ++lod)
				{
					Geometry* G = new Geometry;
					ProceduralGeometry::icosphere(*G, 2 - lod);

					MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(G);
					auto material = baseMaterials[(row ^ col) % baseMaterials.size()];

					MesoGeometryAssets::addStaticMeshSections(ball, lod, geomAssets, material);
				}
			}
		}
	}

	scene->sun.direction = SUN_DIRECTION;
	scene->sun.illuminance = SUN_ILLUMINANCE;
}

void World2::onTerminate()
{
	delete ground;
	for (auto ball : balls)
	{
		delete ball;
	}
}

void World2::onTick(float deltaSeconds)
{
	//
}
