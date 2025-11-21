#include "app.h"
#include "world1.h"
#include "world2.h"

#include "core/core_minimal.h"
#include "memory/memory_tracker.h"
#include "rhi/render_device_capabilities.h"
#include "util/profiling.h"
#include "imgui.h"

/* -------------------------------------------------------
					CONFIGURATION
--------------------------------------------------------*/
#define RENDERER_PRESET      0

#if RENDERER_PRESET == 0
	#define RAW_API          ERenderDeviceRawAPI::DirectX12
	#define RENDERER_TYPE    ERendererType::Standard
#elif RENDERER_PRESET == 1
	#define RAW_API          ERenderDeviceRawAPI::DirectX12
	#define RENDERER_TYPE    ERendererType::Null
#elif RENDERER_PRESET == 2
	#define RAW_API          ERenderDeviceRawAPI::Vulkan
	#define RENDERER_TYPE    ERendererType::Standard
#elif RENDERER_PRESET == 3
	#define RAW_API          ERenderDeviceRawAPI::Vulkan
	#define RENDERER_TYPE    ERendererType::Null
#endif

#define WINDOW_TYPE          EWindowType::WINDOWED
#define DOUBLE_BUFFERING     false
#define RAYTRACING_TIER      ERaytracingTier::MaxTier

// Camera position and direction can be overriden by world.
#define CAMERA_POSITION      vec3(50.0f, 0.0f, 30.0f)
#define CAMERA_LOOKAT        vec3(50.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f
// Per second
#define CAMERA_SPEED_FORWARD 20.0f
#define CAMERA_SPEED_RIGHT   20.0f

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
	SwapChainCreateParams swapChainParams{
		.bHeadless          = false,
		.nativeWindowHandle = getHWND(),
		.windowType         = WINDOW_TYPE,
		.windowWidth        = getWindowWidth(),
		.windowHeight       = getWindowHeight(),
	};

	CysealEngineCreateParams engineInit{
		.renderDevice = RenderDeviceCreateParams{
			.swapChainParams  = swapChainParams,
			.rawAPI           = RAW_API,
			.raytracingTier   = RAYTRACING_TIER,
			.bDoubleBuffering = DOUBLE_BUFFERING,
		},
		.rendererType = RENDERER_TYPE,
	};
	cysealEngine.startup(engineInit);

	// May overwritten by world.
	camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
	camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

	world = new(EMemoryTag::World) WORLD_CLASS;
	world->preinitialize(&scene, &camera, &appState);
	world->onInitialize();

	return true;
}

void TestApplication::onTick(float deltaSeconds)
{
	// #todo-renderthread: Start to render using prev frame's scene proxy.

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
			camera.moveForward(moveZ * deltaSeconds * CAMERA_SPEED_FORWARD);
			camera.moveRight(moveX * deltaSeconds * CAMERA_SPEED_RIGHT);
		}
		appState.rendererOptions.bCameraHasMoved = bCameraHasMoved;

		const uint32 PATH_TRACING_MAX_FRAMES = (uint32)appState.pathTracingMaxFrames;

		if (appState.rendererOptions.pathTracing == EPathTracingMode::Disabled)
		{
			appState.pathTracingNumFrames = 0;
			appState.rendererOptions.pathTracingDenoiserState = EPathTracingDenoiserState::WaitForFrameAccumulation;
		}
		else if (appState.rendererOptions.pathTracing == EPathTracingMode::Offline)
		{
			if (bCameraHasMoved)
			{
				appState.pathTracingNumFrames = 0;
				appState.rendererOptions.pathTracingDenoiserState = EPathTracingDenoiserState::WaitForFrameAccumulation;
			}
			else
			{
				appState.pathTracingNumFrames += 1;
				if (appState.pathTracingNumFrames == PATH_TRACING_MAX_FRAMES)
				{
					appState.rendererOptions.pathTracingDenoiserState = EPathTracingDenoiserState::DenoiseNow;
				}
				else if (appState.pathTracingNumFrames > PATH_TRACING_MAX_FRAMES)
				{
					appState.rendererOptions.pathTracingDenoiserState = EPathTracingDenoiserState::KeepDenoisingResult;
				}
			}
		}
		else if (appState.rendererOptions.pathTracing == EPathTracingMode::Realtime)
		{
			appState.pathTracingNumFrames = (std::min)(appState.pathTracingNumFrames + 1, PATH_TRACING_MAX_FRAMES);
			appState.rendererOptions.pathTracingDenoiserState = EPathTracingDenoiserState::WaitForFrameAccumulation;
		}
		else if (appState.rendererOptions.pathTracing == EPathTracingMode::RealtimeDenoising)
		{
			appState.pathTracingNumFrames = (std::min)(appState.pathTracingNumFrames + 1, PATH_TRACING_MAX_FRAMES);
			appState.rendererOptions.pathTracingDenoiserState = EPathTracingDenoiserState::DenoiseNow;
		}
		else
		{
			CHECK_NO_ENTRY();
		}

		world->onTick(deltaSeconds);
	}

	// #todo: Move rendering loop to engine
	{
		SCOPED_CPU_EVENT(ExecuteRenderer);

		scene.updateMeshLODs(camera, appState.rendererOptions);

		SceneProxy* sceneProxy = scene.createProxy();

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
			ImGui::Checkbox("Depth Prepass", &appState.rendererOptions.bEnableDepthPrepass);

			ImGui::SeparatorText("Debug Visualization");
			if (ImGui::BeginTable("##Debug Visualization", 2))
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("Mode");
				ImGui::TableNextColumn(); ImGui::Combo("##Debug Visualization Mode", &appState.selectedBufferVisualizationMode, getBufferVisualizationModeNames(), (int32)EBufferVisualizationMode::Count);
				ImGui::EndTable();
			}
			appState.rendererOptions.bufferVisualization = (EBufferVisualizationMode)appState.selectedBufferVisualizationMode;

			ImGui::SeparatorText("Ray Tracing");
			if (ImGui::BeginTable("##Ray Tracing", 2))
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("Ray Traced Shadows");
				ImGui::TableNextColumn(); ImGui::Combo("##Ray Traced Shadows", &appState.selectedRayTracedShadowsMode, getRayTracedShadowsModeNames(), (int32)ERayTracedShadowsMode::Count);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("Indirect Diffuse Reflection");
				ImGui::TableNextColumn(); ImGui::Combo("##Indirect Diffuse Reflection", &appState.selectedIndirectDiffuseMode, getIndirectDiffuseModeNames(), (int32)EIndirectDiffuseMode::Count);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("Indirect Specular Reflection");
				ImGui::TableNextColumn(); ImGui::Combo("##Indirect Specular Reflection", &appState.selectedIndirectSpecularMode, getIndirectSpecularModeNames(), (int32)EIndirectSpecularMode::Count);
				ImGui::EndTable();
			}
			appState.rendererOptions.rayTracedShadows = (ERayTracedShadowsMode)appState.selectedRayTracedShadowsMode;
			appState.rendererOptions.indirectDiffuse = (EIndirectDiffuseMode)appState.selectedIndirectDiffuseMode;
			appState.rendererOptions.indirectSpecular = (EIndirectSpecularMode)appState.selectedIndirectSpecularMode;

			const int32 pathTracingModeOld = appState.selectedPathTracingMode;
			const int32 pathTracingMaxFramesOld = appState.pathTracingMaxFrames;
			ImGui::SeparatorText("Path Tracing");
			if (ImGui::BeginTable("##Path Tracing", 2))
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("Mode");
				ImGui::TableNextColumn(); ImGui::Combo("##Path Tracing Mode", &appState.selectedPathTracingMode, getPathTracingModeNames(), (int32)EPathTracingMode::Count);
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("Max Frames");
				ImGui::TableNextColumn(); ImGui::InputInt("##Path Tracing Max Frames", &appState.pathTracingMaxFrames);
				// #todo-pathtracing: UI for Wavefront Path Tracing.
				#if 0
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::Text("Path Tracing Kernel");
				ImGui::TableNextColumn(); ImGui::Combo("##Path Tracing Kernel", &appState.selectedPathTracingKernel, getPathTracingKernelNames(), (int32)EPathTracingKernel::Count);
				#endif
				ImGui::EndTable();
			}
			appState.rendererOptions.pathTracing = (EPathTracingMode)appState.selectedPathTracingMode;
			appState.rendererOptions.pathTracingKernel = (EPathTracingKernel)appState.selectedPathTracingKernel;
			appState.pathTracingMaxFrames = (std::max)(appState.pathTracingMaxFrames, 1);
			if (pathTracingModeOld != appState.selectedPathTracingMode || pathTracingMaxFramesOld != appState.pathTracingMaxFrames)
			{
				appState.pathTracingNumFrames = 0;
				appState.rendererOptions.pathTracingDenoiserState = EPathTracingDenoiserState::WaitForFrameAccumulation;
				appState.rendererOptions.bCameraHasMoved = true;
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

			ImGui::SeparatorText("Memory");
			for (uint32 i = 0; i < (uint32)EMemoryTag::Count; ++i)
			{
				ImGui::Text("Tag: %u, bytes = %u", i, MemoryTracker::get().getTotalBytes((EMemoryTag)i));
			}
			
			ImGui::End();
		}
		cysealEngine.renderImgui();

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
