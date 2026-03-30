#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_render_utils.h"
#include "../rhi/test_rhi_utils.h"

#include "core/engine.h"
#include "core/win/windows_application.h"
#include "world/scene.h"
#include "world/camera.h"
#include "loader/image_loader.h"
#include "rhi/render_command.h"
#include "rhi/dx12/d3d_device.h"
#include "rhi/vulkan/vk_device.h"

#include <array>

#define CAMERA_POSITION      vec3(50.0f, 0.0f, 30.0f)
#define CAMERA_LOOKAT        vec3(50.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

#define WINDOW_X             200
#define WINDOW_Y             200
// #wip: Readback crashes if NPOT.
#define WINDOW_WIDTH         512
#define WINDOW_HEIGHT        512

static const std::wstring skyboxFilepaths[] = {
	L"skybox_Footballfield/posx.jpg", L"skybox_Footballfield/negx.jpg",
	L"skybox_Footballfield/posy.jpg", L"skybox_Footballfield/negy.jpg",
	L"skybox_Footballfield/posz.jpg", L"skybox_Footballfield/negz.jpg",
};

class SkyboxApplication : public WindowsApplication
{
public:
	SkyboxApplication(CysealEngine* inEngine, ERenderDeviceRawAPI inGraphicsAPI, std::vector<uint8>* inImageData)
		: cysealEngine(inEngine)
		, graphicsAPI(inGraphicsAPI)
		, imageData(inImageData)
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

		exitCounter = 0;

		createResources();

		TextureCreateParams cameraColorParams = TextureCreateParams::texture2D(
			EPixelFormat::R8G8B8A8_UNORM,
			ETextureAccessFlags::RTV | ETextureAccessFlags::CPU_READBACK,
			WINDOW_WIDTH, WINDOW_HEIGHT);
		cameraColor = gRenderDevice->createTexture(cameraColorParams);

		return true;
	}

	virtual void onTerminate() override
	{
		delete cameraColor;
		scene.skyboxTexture = nullptr;
		cysealEngine->shutdown();
	}

	virtual void onTick(float deltaSeconds) override
	{
		if (exitCounter++ > 10)
		{
			terminateApplication();
		}
		else
		{
			SceneProxy* sceneProxy = scene.createProxy();
			RendererOptions rendererOptions{};
			if (imageData->size() == 0)
			{
				rendererOptions.finalRenderTarget = cameraColor;
			}

			cysealEngine->beginImguiNewFrame();
			/* It won't intervene the result as there's no GUI if I invoke nothing in ImGui. */
			cysealEngine->renderImgui();

			cysealEngine->renderScene(sceneProxy, &camera, rendererOptions);

			delete sceneProxy;

			if (imageData->size() == 0)
			{
				RenderCommandList* commandList = beginRendering();
				auto handle = cameraColor->requestReadback(commandList, Texture::ReadbackRegion::mip0(cameraColor));
				finishRendering();
				Assert::IsTrue(handle->bAvailable);
#if 1
				uint8* readbackData = reinterpret_cast<uint8*>(handle->readbackData);
				imageData->assign(readbackData, readbackData + handle->totalBytes);
#else
				float* readbackData = reinterpret_cast<float*>(handle->readbackData);
				*imageData = render_test::rgba32f_to_rgba8ui(readbackData, WINDOW_WIDTH * WINDOW_HEIGHT);
#endif
			}
		}
	}

private:
	void createResources()
	{
		ImageLoader imageLoader;
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
		Assert::IsTrue(bValidSkyboxBlobs, L"Failed to load skybox images");

		// 1. Upload texture data.
		SharedPtr<TextureAsset> skyboxTexture = makeShared<TextureAsset>();
		ENQUEUE_RENDER_COMMAND(CreateSkybox)(
			[texWeak = WeakPtr<TextureAsset>(skyboxTexture), skyboxBlobs](RenderCommandList& commandList)
			{
				SharedPtr<TextureAsset> tex = texWeak.lock();
				CHECK(tex != nullptr);

				TextureCreateParams params = TextureCreateParams::textureCube(
					EPixelFormat::R8G8B8A8_UNORM,
					ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
					skyboxBlobs[0]->width, skyboxBlobs[0]->height, 1);

				Texture* texture = gRenderDevice->createTexture(params);
				for (uint32 i = 0; i < 6; ++i)
				{
					texture->uploadData(&commandList,
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
		scene.skyboxTexture = skyboxTexture;
	}

	RenderCommandList* getCommandList()
	{
		return gRenderDevice->getCommandListForCustomCommand();
	}
	RenderCommandList* beginRendering()
	{
		RenderCommandList* commandList = getCommandList();
		RenderCommandAllocator* commandAllocator = gRenderDevice->getCommandAllocator(0);

		commandList->reset(commandAllocator);
		return commandList;
	}
	void finishRendering()
	{
		RenderCommandList* commandList = getCommandList();
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
	uint32 exitCounter = 0;

	Texture* cameraColor = nullptr;
	std::vector<uint8>* imageData = nullptr;
};

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestSkyboxBase
	{
	protected:
		void RenderSkybox()
		{
			std::vector<uint8> actualImage;

			HWND nativeWindowHandle = NULL;

			CysealEngine cysealEngine;

			WindowsApplication* app = new SkyboxApplication(&cysealEngine, graphicsAPI, &actualImage);
			app->setWindowTitle(L"Hello world");
			app->setWindowPosition(WINDOW_X, WINDOW_Y);
			app->setWindowSize(WINDOW_WIDTH, WINDOW_HEIGHT);

			ApplicationCreateParams createParams;
			createParams.nativeWindowHandle = nativeWindowHandle;
			createParams.applicationName = L"TestSkybox";

			// Enters the main loop.
			EApplicationReturnCode ret = app->launch(createParams);
			static_cast<void>(ret);

			Assert::IsTrue(ret == EApplicationReturnCode::Ok);

			uint32 numDiff = render_test::compareRefImageToRgba8ui(L"TestSkybox/ref.png", actualImage.data());
			render_test::saveRgba8uiImage(L"TestSkybox/actual.png", actualImage.data(), WINDOW_WIDTH, WINDOW_HEIGHT);
			Assert::AreEqual(0u, numDiff);
		}
	};

	TEST_CLASS(TestSkyboxD3D12), TestSkyboxBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(RenderSkybox)
		{
			TestSkyboxBase::RenderSkybox();
		}
	};

	TEST_CLASS(TestSkyboxVulkan), TestSkyboxBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(RenderSkybox)
		{
			TestSkyboxBase::RenderSkybox();
		}
	};
}
