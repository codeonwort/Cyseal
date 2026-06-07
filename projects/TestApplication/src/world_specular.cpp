#include "world_specular.h"
#include "world_utils.h"

#include "rhi/vertex_buffer_pool.h"
#include "rhi/texture_manager.h"
#include "render/static_mesh.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"
#include "geometry/meso_geometry.h"
#include "world/material_asset.h"

#include "imgui.h"

#define SUN_DIRECTION        normalize(vec3(-1.0f, -1.0f, -1.0f))
#define SUN_ILLUMINANCE      (2.0f * vec3(1.0f, 1.0f, 1.0f))

#define CAMERA_POSITION      vec3(0.0f, 6.0f, 70.0f)
#define CAMERA_LOOKAT        vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

// #wip: Fix known issues
// - Find out why a path that bounces at both mirror and glass is black.
// - Black fireflies in world1. Different issue than (mirror + glass) above.
// - Artifacts at screen edge in world1. Even repro in perfect mirror mode. -> Only repro if use AMD denoiser?

void World_Specular::onInitialize()
{
	appState->selectedIndirectSpecularMode = (int32)EIndirectSpecularMode::BRDF;
	appState->rendererOptions.indirectSpecular = EIndirectSpecularMode::BRDF;

	camera->lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	camera->perspective(CAMERA_FOV_Y, camera->getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

	// Ground
	{
		Geometry* planeGeometry = new Geometry;
		ProceduralGeometry::plane(*planeGeometry, 100.0f, 100.0f, 2, 2, ProceduralGeometry::EPlaneNormal::Y);

		auto material = makeShared<MaterialAsset>();
		material->albedoMultiplier = vec3(0.05f, 0.05f, 0.05f);
		material->albedoTexture = gTextureManager->getSystemTextureWhite2D();
		material->setRoughness(0.05f);

		ground = new StaticMesh;
		ground->setPosition(vec3(0.0f, -10.0f, 0.0f));

		MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(planeGeometry);
		MesoGeometryAssets::addStaticMeshSections(ground, 0, geomAssets, material);

		scene->addStaticMesh(ground);
	}

	// Spawn boxes
	{
		boxSpawnParams = {
			{ .scale = vec3(15, 15, 15), .position = vec3(-30, -5, 30), .albedo = vec3(0.9f, 0.1f, 0.1f), .roughness = 0.9f },
			{ .scale = vec3(10, 10, 10), .position = vec3(-10, -5, 30), .albedo = vec3(0.1f, 0.9f, 0.1f), .roughness = 0.9f },
			{ .scale = vec3(10, 10, 10), .position = vec3(5, -5, 30),   .albedo = vec3(0.9f, 0.1f, 0.1f), .roughness = 0.01f },
			{ .scale = vec3(20, 20, 20), .position = vec3(30, -5, 30) , .albedo = vec3(0.1f, 0.9f, 0.1f), .roughness = 0.01f },
		};

		for (const BoxSpawnParams& params : boxSpawnParams)
		{
			Geometry* G = new Geometry;
			ProceduralGeometry::cube(*G);

			auto material = makeShared<MaterialAsset>();
			material->albedoMultiplier = params.albedo;
			material->albedoTexture = gTextureManager->getSystemTextureWhite2D();
			material->setRoughness(params.roughness);

			auto box = new StaticMesh;
			box->setScale(params.scale);
			box->setPosition(params.position);

			MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(G);
			MesoGeometryAssets::addStaticMeshSections(box, 0, geomAssets, material);

			boxes.push_back(box);
			scene->addStaticMesh(box);
		}
	}

	scene->skyboxTexture = worldUtils::createSkyboxAsset();

	scene->sun.direction = SUN_DIRECTION;
	scene->sun.illuminance = SUN_ILLUMINANCE;
}

void World_Specular::onTerminate()
{
	delete ground;
	for (StaticMesh* box : boxes) delete box;
	scene->skyboxTexture.reset();
}

void World_Specular::onRenderGUI()
{
	const char* windowName = "World_Specular";

	ImGui::Begin(windowName);

	for (uint32 i = 0; i < boxSpawnParams.size(); ++i)
	{
		std::string label = "box" + std::to_string(i) + " roughness";
		if (ImGui::SliderFloat(label.c_str(), &boxSpawnParams[i].roughness, 0.0f, 1.0f, "%.2f"))
		{
			MaterialAsset* mat = boxes[i]->getSections(0)[0].material.get();
			mat->setRoughness(boxSpawnParams[i].roughness);
		}
	}

	float windowX = ImGui::GetMainViewport()->Size.x - ImGui::GetWindowSize().x - 10.0f;
	ImGui::SetWindowPos(windowName, ImVec2(windowX, 10.0f));

	ImGui::End();
}

void World_Specular::onTick(float deltaSeconds)
{
	//
}
