#include "pch.h"
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

#include "test_render_utils.h"
#include "../rhi/test_rhi_utils.h"

#include "core/engine.h"
#include "core/smart_pointer.h"
#include "core/console_application.h"
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

#define SUN_DIRECTION        normalize(vec3(1.0f, -5.0f, 1.0f))
#define SUN_ILLUMINANCE      (10.0f * vec3(1.0f, 1.0f, 1.0f))

#define CAMERA_POSITION      vec3(0.0f, 0.0f, 50.0f)
#define CAMERA_LOOKAT        vec3(0.0f, 0.0f, 0.0f)
#define CAMERA_UP            vec3(0.0f, 1.0f, 0.0f)
#define CAMERA_FOV_Y         70.0f
#define CAMERA_Z_NEAR        0.01f
#define CAMERA_Z_FAR         10000.0f

#define WINDOW_WIDTH         1024
#define WINDOW_HEIGHT        1024
#define ASPECT_RATIO         ((float)WINDOW_WIDTH / (float)WINDOW_HEIGHT)

#define SAMPLES_PER_PIXEL    32

struct ActualImage_PathTracing
{
	void init(size_t imageCount, uint32 inWidth, uint32 inHeight)
	{
		images.resize(imageCount);
		actualTags.resize(imageCount);
		refTags.resize(imageCount);
		width = inWidth;
		height = inHeight;
	}

	std::vector<std::vector<uint8>> images;
	std::vector<std::wstring> actualTags;
	std::vector<std::wstring> refTags;
	uint32 width;
	uint32 height;
};

static const std::wstring skyboxFilepaths[] = {
	L"skybox_Footballfield/posx.jpg", L"skybox_Footballfield/negx.jpg",
	L"skybox_Footballfield/posy.jpg", L"skybox_Footballfield/negy.jpg",
	L"skybox_Footballfield/posz.jpg", L"skybox_Footballfield/negz.jpg",
};

class PathTracingApplication : public ConsoleApplication
{
public:
	PathTracingApplication(CysealEngine* inEngine, ERenderDeviceRawAPI inGraphicsAPI, ActualImage_PathTracing* inActualImage)
		: cysealEngine(inEngine)
		, graphicsAPI(inGraphicsAPI)
		, actualImage(inActualImage)
	{}

protected:
	virtual void onExecute() override
	{
		onInitialize();
		while (onTick()) {}
		onTerminate();
	}

	bool onInitialize()
	{
		CysealEngineCreateParams engineInit{
			.renderDevice = RenderDeviceCreateParams{
				.swapChainParams  = SwapChainCreateParams::noSwapChain(),
				.rawAPI           = graphicsAPI,
			},
			.rendererType = ERendererType::Standard,
		};

		cysealEngine->startup(engineInit);
		cysealEngine->setRenderResolution(WINDOW_WIDTH, WINDOW_HEIGHT);

		camera.lookAt(CAMERA_POSITION, CAMERA_LOOKAT, CAMERA_UP);
		camera.perspective(CAMERA_FOV_Y, ASPECT_RATIO, CAMERA_Z_NEAR, CAMERA_Z_FAR);

		createScene();

		configHandle = configPermutation.init();

		actualImage->init(configPermutation.numTotalConfigs(), WINDOW_WIDTH, WINDOW_HEIGHT);

		TextureCreateParams cameraColorParams = TextureCreateParams::texture2D(
			EPixelFormat::R8G8B8A8_UNORM,
			ETextureAccessFlags::RTV | ETextureAccessFlags::CPU_READBACK,
			actualImage->width, actualImage->height);
		cameraColor = gRenderDevice->createTexture(cameraColorParams);

		return true;
	}

	void onTerminate()
	{
		delete cameraColor;
		for (StaticMesh* sm : staticMeshes)
		{
			delete sm;
		}
		scene.skyboxTexture = nullptr;
		cysealEngine->shutdown();
	}

	bool onTick()
	{
		bool bNeedReadback = frameCounter >= SAMPLES_PER_PIXEL;

		SceneProxy* sceneProxy = scene.createProxy();
		RendererOptions rendererOptions{};
		rendererOptions.finalRenderTarget = cameraColor;
		rendererOptions.rayTracedShadows = ERayTracedShadowsMode::Disabled;
		rendererOptions.indirectDiffuse = EIndirectDiffuseMode::Disabled;
		rendererOptions.indirectSpecular = EIndirectSpecularMode::Disabled;
		rendererOptions.pathTracing = EPathTracingMode::Offline;
		rendererOptions.pathTracingDenoiserState = bNeedReadback
			? EPathTracingDenoiserState::DenoiseNow
			: EPathTracingDenoiserState::WaitForFrameAccumulation;

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

			const int32 configIndex = configHandle.getLinearIx();

			uint8* readbackData = reinterpret_cast<uint8*>(handle->readbackData);
			
			std::vector<uint8>& targetImage = actualImage->images[configIndex];
			std::wstring& actualTag = actualImage->actualTags[configIndex];
			std::wstring& refTag = actualImage->refTags[configIndex];
			
			targetImage.assign(readbackData, readbackData + handle->totalBytes);

			const char* sApi = (graphicsAPI == ERenderDeviceRawAPI::DirectX12) ? "d3d" : "vk";

			char msg[256]; std::wstring wMsg;
			sprintf_s(msg, "TestPathTracing/%s/spp%d.png", sApi, SAMPLES_PER_PIXEL);
			str_to_wstr(msg, wMsg);
			actualTag = wMsg;
			sprintf_s(msg, "TestPathTracing/spp%d.png", SAMPLES_PER_PIXEL);
			str_to_wstr(msg, wMsg);
			refTag = wMsg;

			if (configPermutation.advance() == false) return false;
		}

		++frameCounter;
		return true;
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

		// Ground
		{
			StaticMesh* staticMesh = new StaticMesh;
			Geometry* geom = new Geometry;
			ProceduralGeometry::plane(*geom, 1, 1, 1, 1, ProceduralGeometry::EPlaneNormal::Y);
			MesoGeometryAssets geomAssets = MesoGeometryAssets::createFrom(geom);
			MesoGeometryAssets::addStaticMeshSections(staticMesh, 0, geomAssets, baseMaterials[0]);
			staticMesh->setPosition(vec3(0, -5.0f, 0));
			staticMesh->setRotation(vec3(1, 0, 0), -5.0f);
			staticMesh->setScale(100.0f);
			staticMeshes.push_back(staticMesh);
		}

		scene.sun = std::move(sun);
		scene.skyboxTexture = createSkybox();
		for (StaticMesh* sm : staticMeshes) scene.addStaticMesh(sm);
	}

	SharedPtr<TextureAsset> createSkybox()
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

		return skyboxTexture;
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
	ActualImage_PathTracing* actualImage = nullptr;

	render_test::ConfigPermutation configPermutation;
	render_test::ConfigPermutation::ConfigHandle configHandle;
};

namespace UnitTest
{
	template<ERenderDeviceRawAPI graphicsAPI>
	class TestPathTracingBase
	{
	protected:
		void RenderPathTracing()
		{
			ActualImage_PathTracing actualImage;

			HWND nativeWindowHandle = NULL;

			CysealEngine cysealEngine;

			ConsoleApplication* app = new PathTracingApplication(&cysealEngine, graphicsAPI, &actualImage);

			ApplicationCreateParams createParams;
			createParams.nativeWindowHandle = nativeWindowHandle;
			createParams.applicationName = L"TestPathTracing";

			// Enters the main loop.
			EApplicationReturnCode ret = app->launch(createParams);
			static_cast<void>(ret);

			Assert::IsTrue(ret == EApplicationReturnCode::Ok);

			int32 invalidImgCount = 0;
			for (int32 i = 0; i < actualImage.images.size(); ++i)
			{
				std::vector<uint8>& image = actualImage.images[i];
				const std::wstring& actualTag = actualImage.actualTags[i];
				const std::wstring& refTag = actualImage.refTags[i];
				uint32 numDiff = render_test::compareRefImageToRgba8ui(refTag.data(), image.data());
				render_test::saveRgba8uiImage(actualTag.data(), image.data(), actualImage.width, actualImage.height);
				
				invalidImgCount += (0 != numDiff) ? 1 : 0;
			}
			Assert::AreEqual(0, invalidImgCount);
		}
	};

	TEST_CLASS(TestPathTracingD3D12), TestPathTracingBase<ERenderDeviceRawAPI::DirectX12>
	{
	public:
		TEST_METHOD(RenderPathTracing)
		{
			TestPathTracingBase::RenderPathTracing();
		}
	};

	// #todo-test: VK can't run SceneRenderer yet.
#if 0
	TEST_CLASS(TestPathTracingVulkan), TestPathTracingBase<ERenderDeviceRawAPI::Vulkan>
	{
	public:
		TEST_METHOD(RenderPathTracing)
		{
			TestPathTracingBase::RenderPathTracing();
		}
	};
#endif
}
