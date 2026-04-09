#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_render_utils.h"
#include "../rhi/test_rhi_utils.h"

#include "core/engine.h"
#include "core/smart_pointer.h"
#include "core/win/windows_application.h"
#include "geometry/meso_geometry.h"
#include "geometry/primitive.h"
#include "geometry/procedural.h"
#include "world/scene.h"
#include "world/camera.h"
#include "world/material_asset.h"
#include "loader/image_loader.h"
#include "render/static_mesh.h"
#include "rhi/render_command.h"
#include "rhi/texture_manager.h"
#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"
#include "util/string_conversion.h"

#include <array>

#define SUN_DIRECTION        normalize(vec3(-1.0f, -1.0f, -1.0f))
#define SUN_ILLUMINANCE      (10.0f * vec3(1.0f, 1.0f, 1.0f))

#define CAMERA_POSITION      vec3(0.0f, 0.0f, 50.0f)
#define CAMERA_LOOKAT        vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

#define WINDOW_X             200
#define WINDOW_Y             200
#define WINDOW_WIDTH         256
#define WINDOW_HEIGHT        256

const int32 nPrepassConfigs = 2;
const int32 nVisBufferConfigs = 2;
const int32 nGpuCullingConfigs = 2;
const int32 nIndirectDrawConfigs = (int32)EIndirectDrawMode::Count;
const int32 nTotalConfigs = nPrepassConfigs * nVisBufferConfigs * nGpuCullingConfigs * nIndirectDrawConfigs;

struct ActualImage_GpuDriven
{
	std::vector<uint8> images[nTotalConfigs];
	std::wstring actualTags[nTotalConfigs];
	uint32 width;
	uint32 height;
};

class GpuDrivenApplication : public WindowsApplication
{
public:
	GpuDrivenApplication(CysealEngine* inEngine, ERenderDeviceRawAPI inGraphicsAPI, ActualImage_GpuDriven* inActualImage)
		: cysealEngine(inEngine)
		, graphicsAPI(inGraphicsAPI)
		, actualImage(inActualImage)
	{}

protected:
	virtual bool onInitialize() override
	{
		SwapChainCreateParams swapChainParams{
			.bHeadless          = false,
			.nativeWindowHandle = getHWND(),
			.windowType         = EWindowType::WINDOWED,
			.windowWidth        = WINDOW_WIDTH,
			.windowHeight       = WINDOW_HEIGHT,
		};
		CysealEngineCreateParams engineInit{
			.renderDevice = RenderDeviceCreateParams{
				.swapChainParams  = swapChainParams,
				.rawAPI           = graphicsAPI,
			},
			.rendererType = ERendererType::Standard,
		};

		cysealEngine->startup(engineInit);

		camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
		camera.perspective(CAMERA_FOV_Y, getAspectRatio(), CAMERA_Z_NEAR, CAMERA_Z_FAR);

		createScene();

		actualImage->width = gRenderDevice->getSwapChain()->getBackbufferWidth();
		actualImage->height = gRenderDevice->getSwapChain()->getBackbufferHeight();

		TextureCreateParams cameraColorParams = TextureCreateParams::texture2D(
			EPixelFormat::R8G8B8A8_UNORM,
			ETextureAccessFlags::RTV | ETextureAccessFlags::CPU_READBACK,
			actualImage->width, actualImage->height);
		cameraColor = gRenderDevice->createTexture(cameraColorParams);

		return true;
	}

	virtual void onTerminate() override
	{
		delete cameraColor;
		for (StaticMesh* sm : staticMeshes)
		{
			delete sm;
		}
		scene.skyboxTexture = nullptr;
		cysealEngine->shutdown();
	}

	virtual void onTick(float deltaSeconds) override
	{
		bool bNeedReadback = frameCounter != 0;

		static int32 prepassConfig = 0;
		static int32 visBufferConfig = 0;
		static int32 gpuCullingConfig = 0;
		static int32 indirectDrawConfig = 0;
		static int32 configIndex = 0;

		SceneProxy* sceneProxy = scene.createProxy();
		RendererOptions rendererOptions{};
		rendererOptions.rayTracedShadows = ERayTracedShadowsMode::Disabled;
		rendererOptions.indirectDiffuse = EIndirectDiffuseMode::Disabled;
		rendererOptions.indirectSpecular = EIndirectSpecularMode::Disabled;
		rendererOptions.pathTracing = EPathTracingMode::Disabled;
		if (bNeedReadback)
		{
			rendererOptions.finalRenderTarget = cameraColor;
			rendererOptions.bEnableDepthPrepass = (bool)prepassConfig;
			rendererOptions.bEnableVisibilityBuffer = (bool)visBufferConfig;
			rendererOptions.bEnableGPUCulling = (bool)gpuCullingConfig;
			rendererOptions.indirectDrawMode = (EIndirectDrawMode)indirectDrawConfig;
		}

		cysealEngine->beginImguiNewFrame();
		/* It won't intervene the result as there's no GUI if I invoke nothing in ImGui. */
		cysealEngine->renderImgui();

		cysealEngine->renderScene(sceneProxy, &camera, rendererOptions);

		delete sceneProxy;

		if (bNeedReadback)
		{
			RenderCommandList* commandList = beginRendering();
			auto handle = cameraColor->requestReadback(commandList, Texture::ReadbackRegion::mip0(cameraColor));
			finishRendering(commandList);
			Assert::IsTrue(handle->bAvailable);

			uint8* readbackData = reinterpret_cast<uint8*>(handle->readbackData);
			
			std::vector<uint8>& targetImage = actualImage->images[configIndex];
			std::wstring& actualTag = actualImage->actualTags[configIndex];
			
			targetImage.assign(readbackData, readbackData + handle->totalBytes);

			const char* sApi = (graphicsAPI == ERenderDeviceRawAPI::DirectX12) ? "d3d" : "vk";
			const char* sPrepass = (prepassConfig == 0) ? "noprepass" : "prepass";
			const char* sVisBuffer = (visBufferConfig == 0) ? "novisbuf" : "visbuf";
			const char* sCulling = (gpuCullingConfig == 0) ? "nocull" : "gpucull";
			const char* sIndirect = (indirectDrawConfig == 0) ? "direct" : (indirectDrawConfig == 1) ? "cpugen" : "gpugen";

			char msg[256]; std::wstring wMsg;
			sprintf_s(msg, "TestGpuDriven/%s/%s_%s_%s_%s.png", sApi, sPrepass, sVisBuffer, sCulling, sIndirect);
			str_to_wstr(msg, wMsg);
			actualTag = wMsg;

			++configIndex;

			indirectDrawConfig += 1;
			if (indirectDrawConfig == (int)EIndirectDrawMode::Count)
			{
				indirectDrawConfig = 0;
				gpuCullingConfig += 1;
				if (gpuCullingConfig == 2)
				{
					gpuCullingConfig = 0;
					visBufferConfig += 1;
					if (visBufferConfig == 2)
					{
						visBufferConfig = 0;
						prepassConfig += 1;
						if (prepassConfig == 2)
						{
							Assert::IsTrue(configIndex == nTotalConfigs);
							terminateApplication();
						}
					}
				}
			}
		}

		++frameCounter;
	}

private:
	void createScene()
	{
		DirectionalLight sun;
		sun.direction = SUN_DIRECTION;
		sun.illuminance = SUN_ILLUMINANCE;

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
			material->albedoMultiplier = vec3(0.2f);
			material->roughness = 0.1f;
			baseMaterials.push_back(material);
		}

		struct CreateParams
		{
			vec3 center;
			float radius;
			float height;
			uint32 numLoop;
			uint32 numMeshes;
		};
		CreateParams createParams{
			.center = vec3(0.0f, -12.0f, 0.0f),
			.radius = 24.0f,
			.height = 40.0f,
			.numLoop = 2,
			.numMeshes = 32
		};

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

				auto material = baseMaterials[meshIx % baseMaterials.size()];
				MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(geom);
				MesoGeometryAssets::addStaticMeshSections(staticMesh, lod, geomAssets, material);
			}

			float k = meshIx / (float)createParams.numMeshes;
			float theta = (float)createParams.numLoop * (2.0f * Cymath::PI) * k;
			vec3 deltaPos = createParams.radius * vec3(Cymath::cos(theta), 0.0f, Cymath::sin(theta));
			deltaPos.y += createParams.height * k;
			vec3 startPos = createParams.center + deltaPos;

			staticMesh->setPosition(startPos);
			staticMesh->setRotation(normalize(vec3(0.5f, 1.0f, 0.3f)), k * 360.0f);
			staticMesh->setScale(3.0f);

			staticMeshes.push_back(staticMesh);
		}

		scene.sun = std::move(sun);
		scene.skyboxTexture = nullptr; // No skybox
		for (StaticMesh* sm : staticMeshes) scene.addStaticMesh(sm);
	}

	RenderCommandList* beginRendering()
	{
		RenderCommandList* commandList = gRenderDevice->getCommandListForCustomCommand();
		RenderCommandAllocator* commandAllocator = gRenderDevice->getCommandAllocator(0);

		commandList->reset(commandAllocator);
		return commandList;
	}
	void finishRendering(RenderCommandList* commandList)
	{
		RenderCommandAllocator* commandAllocator = gRenderDevice->getCommandAllocator(0);
		RenderCommandQueue* commandQueue = gRenderDevice->getCommandQueue();
		SwapChain* nullSwapChain = nullptr;

		commandList->close();
		commandAllocator->markValid();
		commandQueue->executeCommandList(commandList, nullSwapChain);
		gRenderDevice->flushCommandQueue();
	}

private:
	CysealEngine* cysealEngine = nullptr;
	ERenderDeviceRawAPI graphicsAPI;

	Scene scene;
	Camera camera;
	std::vector<StaticMesh*> staticMeshes;
	int32 frameCounter = 0;

	Texture* cameraColor = nullptr;
	ActualImage_GpuDriven* actualImage = nullptr;
};

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestGpuDrivenBase
	{
	protected:
		void RenderGpuDriven()
		{
			ActualImage_GpuDriven actualImage;

			HWND nativeWindowHandle = NULL;

			CysealEngine cysealEngine;

			WindowsApplication* app = new GpuDrivenApplication(&cysealEngine, graphicsAPI, &actualImage);
			app->setWindowTitle(L"Hello world");
			app->setWindowPosition(WINDOW_X, WINDOW_Y);
			app->setWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);

			ApplicationCreateParams createParams;
			createParams.nativeWindowHandle = nativeWindowHandle;
			createParams.applicationName = L"TestGpuDriven";

			// Enters the main loop.
			EApplicationReturnCode ret = app->launch(createParams);
			static_cast<void>(ret);

			Assert::IsTrue(ret == EApplicationReturnCode::Ok);

			int32 invalidImgCount = 0;
			for (int32 i = 0; i < nTotalConfigs; ++i)
			{
				std::vector<uint8>& image = actualImage.images[i];
				const std::wstring& actualTag = actualImage.actualTags[i];
				uint32 numDiff = render_test::compareRefImageToRgba8ui(L"TestGpuDriven/ref.png", image.data());
				render_test::saveRgba8uiImage(actualTag.data(), image.data(), actualImage.width, actualImage.height);
				
				invalidImgCount += (0 != numDiff) ? 1 : 0;
			}
			Assert::AreEqual(0, invalidImgCount);
		}
	};

	TEST_CLASS(TestGpuDrivenD3D12), TestGpuDrivenBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(RenderGpuDriven)
		{
			TestGpuDrivenBase::RenderGpuDriven();
		}
	};

	// #todo-test: VK can't run SceneRenderer yet.
#if 0
	TEST_CLASS(TestGpuDrivenVulkan), TestGpuDrivenBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(RenderGpuDriven)
		{
			TestGpuDrivenBase::RenderGpuDriven();
		}
	};
#endif
}
