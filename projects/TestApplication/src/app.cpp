#include "app.h"
#include "world1.h"
#include "world2.h"

#include "core/core_minimal.h"
#include "rhi/render_device_capabilities.h"
#include "util/profiling.h"
#include "imgui.h"

/* -------------------------------------------------------
					CONFIGURATION
--------------------------------------------------------*/
// 0: DX12 + Standard renderer
// 1: Vulkan + Null renderer
// 2: Vulkan + Standard renderer (WIP)
#define RENDERER_PRESET      0

#if RENDERER_PRESET == 0
	#define RAW_API          ERenderDeviceRawAPI::DirectX12
	#define RENDERER_TYPE    ERendererType::Standard
#elif RENDERER_PRESET == 1
	#define RAW_API          ERenderDeviceRawAPI::Vulkan
	#define RENDERER_TYPE    ERendererType::Null
#elif RENDERER_PRESET == 2
	#define RAW_API          ERenderDeviceRawAPI::Vulkan
	#define RENDERER_TYPE    ERendererType::Standard
#endif
#define WINDOW_TYPE          EWindowType::WINDOWED

#define DOUBLE_BUFFERING     true
#define RAYTRACING_TIER      ERaytracingTier::MaxTier

// Can be overriden by world
#define CAMERA_POSITION      vec3(50.0f, 0.0f, 30.0f)
#define CAMERA_LOOKAT        vec3(50.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

// #todo-world: Select world
#define WORLD_CLASS          World1
//#define WORLD_CLASS          World2


/* -------------------------------------------------------
					APPLICATION
--------------------------------------------------------*/
CysealEngine cysealEngine;

DEFINE_LOG_CATEGORY_STATIC(LogApplication);

bool TestApplication::onInitialize()
{
	CysealEngineCreateParams engineInit;
	engineInit.renderDevice.rawAPI             = RAW_API;
	engineInit.renderDevice.nativeWindowHandle = getHWND();
	engineInit.renderDevice.windowType         = WINDOW_TYPE;
	engineInit.renderDevice.windowWidth        = getWindowWidth();
	engineInit.renderDevice.windowHeight       = getWindowHeight();
	engineInit.renderDevice.raytracingTier     = RAYTRACING_TIER;
	engineInit.renderDevice.bDoubleBuffering   = DOUBLE_BUFFERING;
	engineInit.rendererType                    = RENDERER_TYPE;

	cysealEngine.startup(engineInit);

	// May overwritten by world.
	camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

	world = new WORLD_CLASS;
	world->preinitialize(&scene, &camera, &appState);
	world->onInitialize();

	return true;
}

void TestApplication::onTick(float deltaSeconds)
{
	{
		SCOPED_CPU_EVENT(WorldLogic);

		wchar_t buf[256];
		float newFPS = 1.0f / deltaSeconds;
		framesPerSecond += 0.05f * (newFPS - framesPerSecond);
		swprintf_s(buf, L"Hello World / FPS: %.2f", framesPerSecond);
		setWindowTitle(std::wstring(buf));

		// Control camera by user input.
		bool bCameraHasMoved = false;
		{
			float moveX = ImGui::IsKeyDown(ImGuiKey_A) ? -1.0f : ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f;
			float moveZ = ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : ImGui::IsKeyDown(ImGuiKey_S) ? -1.0f : 0.0f;
			float rotateY = ImGui::IsKeyDown(ImGuiKey_Q) ? -1.0f : ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f;
			float rotateX = ImGui::IsKeyDown(ImGuiKey_Z) ? 1.0f : ImGui::IsKeyDown(ImGuiKey_C) ? -1.0f : 0.0f;

			bCameraHasMoved = (moveX != 0.0f || moveZ != 0.0f || rotateY != 0.0f || rotateX != 0.0f);

			camera.rotatePitch(rotateX * deltaSeconds * 45.0f);
			camera.rotateYaw(rotateY * deltaSeconds * 45.0f);
			camera.moveForward(moveZ * deltaSeconds * 10.0f);
			camera.moveRight(moveX * deltaSeconds * 10.0f);
		}
		appState.rendererOptions.bCameraHasMoved = bCameraHasMoved;

		if (appState.rendererOptions.pathTracing == EPathTracingMode::Disabled)
		{
			appState.pathTracingNumFrames = 0;
		}
		else if (appState.rendererOptions.pathTracing == EPathTracingMode::Offline)
		{
			appState.pathTracingNumFrames += 1;
			if (bCameraHasMoved) appState.pathTracingNumFrames = 0;
		}
		else
		{
			appState.pathTracingNumFrames += 1;
			if (appState.pathTracingNumFrames > 64) appState.pathTracingNumFrames = 64;
		}

		world->onTick(deltaSeconds);
	}

	// #todo: Move rendering loop to engine
	{
		SCOPED_CPU_EVENT(ExecuteRenderer);

		if (bViewportNeedsResize)
		{
			cysealEngine.setRenderResolution(newViewportWidth, newViewportHeight);
			bViewportNeedsResize = false;
		}

		cysealEngine.beginImguiNewFrame();
		{
			//ImGui::ShowDemoWindow(0);

			ImGui::Begin("Cyseal");

			ImGui::SeparatorText("Rendering options");
			ImGui::Checkbox("Base Pass - Indirect Draw", &appState.rendererOptions.bEnableIndirectDraw);
			if (!appState.rendererOptions.bEnableIndirectDraw)
			{
				ImGui::BeginDisabled();
			}
			ImGui::Checkbox("Base Pass - GPU Culling", &appState.rendererOptions.bEnableGPUCulling);
			if (!appState.rendererOptions.bEnableIndirectDraw)
			{
				ImGui::EndDisabled();
			}

			ImGui::SeparatorText("Debug Visualization");
			ImGui::Combo("Debug Mode", &appState.selectedBufferVisualizationMode, getBufferVisualizationModeNames(), (int32)EBufferVisualizationMode::Count);
			appState.rendererOptions.bufferVisualization = (EBufferVisualizationMode)appState.selectedBufferVisualizationMode;

			ImGui::SeparatorText("Ray Tracing");
			ImGui::Combo("Ray Traced Shadows", &appState.selectedRayTracedShadowsMode, getRayTracedShadowsModeNames(), (int32)ERayTracedShadowsMode::Count);
			ImGui::Combo("Indirect Specular Reflection", &appState.selectedIndirectSpecularMode, getIndirectSpecularModeNames(), (int32)EIndirectSpecularMode::Count);
			appState.rendererOptions.rayTracedShadows = (ERayTracedShadowsMode)appState.selectedRayTracedShadowsMode;
			appState.rendererOptions.indirectSpecular = (EIndirectSpecularMode)appState.selectedIndirectSpecularMode;

			ImGui::SeparatorText("Path Tracing");
			auto prevPathTracingMode = appState.selectedPathTracingMode;
			ImGui::Combo("Path Tracing Mode", &appState.selectedPathTracingMode, getPathTracingModeNames(), (int32)EPathTracingMode::Count);
			appState.rendererOptions.pathTracing = (EPathTracingMode)appState.selectedPathTracingMode;
			if (appState.selectedPathTracingMode != prevPathTracingMode)
			{
				appState.pathTracingNumFrames = 0;
			}
			ImGui::Text("Frames: %u", appState.pathTracingNumFrames);

			ImGui::SeparatorText("Control");
			ImGui::Text("WASD : move camera");
			ImGui::Text("QE   : rotate camera");

			ImGui::SeparatorText("Info");
			if (appState.rendererOptions.anyRayTracingEnabled())
			{
				ImGui::Text("Static Mesh LOD is disabled if any raytracing is enabled");
			}
			else
			{
				ImGui::Text("Static Mesh LOD is enabled");
			}
			
			ImGui::End();
		}
		cysealEngine.renderImgui();

		SceneProxy* sceneProxy = scene.createProxy();

		cysealEngine.renderScene(sceneProxy, &camera, appState.rendererOptions);

		delete sceneProxy;
	}
}

void TestApplication::onTerminate()
{
	world->onTerminate();

	cysealEngine.shutdown();
}

void TestApplication::onWindowResize(uint32 newWidth, uint32 newHeight)
{
	bViewportNeedsResize = true;
	newViewportWidth = newWidth;
	newViewportHeight = newHeight;

	camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);
}
