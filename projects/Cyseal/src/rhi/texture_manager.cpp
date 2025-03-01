#include "texture_manager.h"
#include "render_device.h"
#include "render_command.h"
#include "gpu_resource.h"
#include "core/assertion.h"
#include "util/resource_finder.h"
#include "loader/image_loader.h"

#include <vector>

#define MAX_SRV_DESCRIPTORS 1024
#define MAX_RTV_DESCRIPTORS 64
#define MAX_DSV_DESCRIPTORS 64
#define MAX_UAV_DESCRIPTORS 1024

#define STBN_DIR            L"external/NVidiaSTBNUnzippedAssets/STBN/"
#define STBN_WIDTH          128
#define STBN_HEIGHT         128
#define STBN_SLICES         64
std::wstring STBN_FILEPATH(size_t ix)
{
	wchar_t buf[256];
	swprintf_s(buf, L"%s%s%d.png", STBN_DIR, L"stbn_unitvec3_cosine_2Dx1D_128x128x64_", (int32)ix);
	return buf;
}

TextureManager* gTextureManager = nullptr;

void TextureManager::initialize()
{
	createSystemTextures();
	createBlueNoiseTextures();
}

void TextureManager::destroy()
{
	systemTexture_grey2D.reset();
	systemTexture_white2D.reset();
	systemTexture_black2D.reset();
	systemTexture_red2D.reset();
	systemTexture_green2D.reset();
	systemTexture_blue2D.reset();
	systemTexture_blackCube.reset();

	blueNoise_vec3cosine.reset();
}

void TextureManager::createSystemTextures()
{
	struct InitSysTex
	{
		uint8 color[4];
		SharedPtr<TextureAsset>& texturePtr;
		const wchar_t* debugName;
		bool bIsCube;
	};

	systemTexture_grey2D = makeShared<TextureAsset>();
	systemTexture_white2D = makeShared<TextureAsset>();
	systemTexture_black2D = makeShared<TextureAsset>();
	systemTexture_red2D = makeShared<TextureAsset>();
	systemTexture_green2D = makeShared<TextureAsset>();
	systemTexture_blue2D = makeShared<TextureAsset>();
	systemTexture_blackCube = makeShared<TextureAsset>();

	std::vector<InitSysTex>* initTablePtr = new std::vector<InitSysTex>({
		{ { 127, 127, 127, 255 }, systemTexture_grey2D   , L"Texture_SystemGrey2D"   , false },
		{ { 255, 255, 255, 255 }, systemTexture_white2D  , L"Texture_SystemWhite2D"  , false },
		{ { 000, 000, 000, 255 }, systemTexture_black2D  , L"Texture_SystemBlack2D"  , false },
		{ { 255, 000, 000, 255 }, systemTexture_red2D    , L"Texture_SystemRed2D"    , false },
		{ { 000, 255, 000, 255 }, systemTexture_green2D  , L"Texture_SystemGreen2D"  , false },
		{ { 000, 000, 255, 255 }, systemTexture_blue2D   , L"Texture_SystemBlue2D"   , false },
		{ { 000, 000, 000, 000 }, systemTexture_blackCube, L"Texture_SystemBlackCube", true  },
	});

	for (size_t ix = 0; ix < initTablePtr->size(); ++ix)
	{
		const InitSysTex& desc = (*initTablePtr)[ix];

		TextureCreateParams params;
		if (desc.bIsCube == false)
		{
			params = TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
				1, 1, 1);
		}
		else
		{
			params = TextureCreateParams::textureCube(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
				1, 1, 1);
		}

		Texture* tex = gRenderDevice->createTexture(params);
		tex->setDebugName(desc.debugName);

		desc.texturePtr->setGPUResource(SharedPtr<Texture>(tex));
	}

	ENQUEUE_RENDER_COMMAND(UploadSystemTextureData)(
		[initTablePtr](RenderCommandList& commandList)
		{
			for (size_t ix = 0; ix < initTablePtr->size(); ++ix)
			{
				const InitSysTex& desc = (*initTablePtr)[ix];
				Texture* tex = desc.texturePtr->getGPUResource().get();

				uint32 cnt = desc.bIsCube ? 6 : 1;
				for (uint32 i = 0; i < cnt; ++i)
				{
					tex->uploadData(commandList, desc.color, 4, 4, i);
				}
			}
			commandList.enqueueDeferredDealloc(initTablePtr);
		}
	);
}

void TextureManager::createBlueNoiseTextures()
{
	ImageLoader loader;
	std::vector<ImageLoadData*> blobs(STBN_SLICES, nullptr);
	for (size_t ix = 0; ix < STBN_SLICES; ++ix)
	{
		std::wstring filepath = STBN_FILEPATH(ix);
		filepath = ResourceFinder::get().find(filepath);
		
		blobs[ix] = loader.load(filepath);
	}

	const uint64 rowPitch = blobs[0]->getRowPitch();
	const uint64 slicePitch = blobs[0]->getSlicePitch();

	uint8* totalBlob = new uint8[slicePitch * STBN_SLICES];
	for (size_t ix = 0; ix < STBN_SLICES; ++ix)
	{
		memcpy_s(totalBlob + ix * slicePitch, slicePitch, blobs[ix]->buffer, slicePitch);
		delete blobs[ix];
	}
	blobs.clear();

	TextureCreateParams params = TextureCreateParams::texture3D(
		EPixelFormat::R8G8B8A8_UNORM,
		ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
		STBN_WIDTH, STBN_HEIGHT, STBN_SLICES, 1);
	Texture* tex = gRenderDevice->createTexture(params);
	tex->setDebugName(L"STBNVec3Cosine");

	blueNoise_vec3cosine = makeShared<TextureAsset>();
	blueNoise_vec3cosine->setGPUResource(SharedPtr<Texture>(tex));

	ENQUEUE_RENDER_COMMAND(UploadSTBN)(
		[totalBlob, rowPitch, slicePitch, tex](RenderCommandList& commandList)
		{
			tex->uploadData(commandList, totalBlob, rowPitch, slicePitch, 0);
			commandList.enqueueDeferredDealloc(totalBlob);
		}
	);
}
