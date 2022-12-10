#include "texture_manager.h"
#include "render_device.h"
#include "render_command.h"
#include "gpu_resource.h"
#include "core/assertion.h"

#define MAX_SRV_DESCRIPTORS 1024
#define MAX_RTV_DESCRIPTORS 64
#define MAX_DSV_DESCRIPTORS 64
#define MAX_UAV_DESCRIPTORS 1024

TextureManager* gTextureManager = nullptr;

void TextureManager::initialize()
{
	createSystemTextures();
}

void TextureManager::destroy()
{
	//
}

void TextureManager::createSystemTextures()
{
	struct InitSysTex
	{
		uint8 color[4];
		std::shared_ptr<TextureAsset>& texturePtr;
		const wchar_t* debugName;
		bool bIsCube;
	};

	systemTexture_grey2D = std::make_shared<TextureAsset>();
	systemTexture_white2D = std::make_shared<TextureAsset>();
	systemTexture_black2D = std::make_shared<TextureAsset>();
	systemTexture_red2D = std::make_shared<TextureAsset>();
	systemTexture_green2D = std::make_shared<TextureAsset>();
	systemTexture_blue2D = std::make_shared<TextureAsset>();
	systemTexture_blackCube = std::make_shared<TextureAsset>();

	std::vector<InitSysTex>* initTablePtr = new std::vector<InitSysTex>({
		{ { 127, 127, 127, 255 }, systemTexture_grey2D   , L"Texture_SystemGrey2D"   , false },
		{ { 255, 255, 255, 255 }, systemTexture_white2D  , L"Texture_SystemWhite2D"  , false },
		{ { 000, 000, 000, 255 }, systemTexture_black2D  , L"Texture_SystemBlack2D"  , false },
		{ { 255, 000, 000, 255 }, systemTexture_red2D    , L"Texture_SystemRed2D"    , false },
		{ { 000, 255, 000, 255 }, systemTexture_green2D  , L"Texture_SystemGreen2D"  , false },
		{ { 000, 000, 255, 255 }, systemTexture_blue2D   , L"Texture_SystemBlue2D"   , false },
		{ { 000, 000, 000, 000 }, systemTexture_blackCube, L"Texture_SystemBlackCube", true  },
	});

	ENQUEUE_RENDER_COMMAND(CreateSystemTextures)(
		[initTablePtr](RenderCommandList& commandList)
		{
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

				uint32 cnt = desc.bIsCube ? 6 : 1;
				for (uint32 i = 0; i < cnt; ++i)
				{
					tex->uploadData(commandList, desc.color, 4, 4, i);
				}
				tex->setDebugName(desc.debugName);

				desc.texturePtr->setGPUResource(std::shared_ptr<Texture>(tex));
			}
			commandList.enqueueDeferredDealloc(initTablePtr);
		}
	);
}
