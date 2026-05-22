#include "world_specular.h"

#include "rhi/vertex_buffer_pool.h"
#include "rhi/texture_manager.h"
#include "render/static_mesh.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"
#include "geometry/meso_geometry.h"
#include "world/material_asset.h"

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
#define CREATE_GROUND        1

void World_Specular::onInitialize()
{
	camera->lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	camera->perspective(CAMERA_FOV_Y, camera->getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

#if CREATE_GROUND
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
#endif

	scene->sun.direction = SUN_DIRECTION;
	scene->sun.illuminance = SUN_ILLUMINANCE;
}

void World_Specular::onTerminate()
{
	delete ground;
}

void World_Specular::onTick(float deltaSeconds)
{
	//
}
