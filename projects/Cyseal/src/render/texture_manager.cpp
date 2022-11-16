#include "texture_manager.h"
#include "render_device.h"
#include "texture.h"
#include "core/assertion.h"

#define MAX_SRV_DESCRIPTORS 1024
#define MAX_RTV_DESCRIPTORS 64
#define MAX_DSV_DESCRIPTORS 64
#define MAX_UAV_DESCRIPTORS 1024

TextureManager* gTextureManager = nullptr;

void TextureManager::initialize()
{
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::SRV;
		desc.numDescriptors = MAX_SRV_DESCRIPTORS;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		srvHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::RTV;
		desc.numDescriptors = MAX_RTV_DESCRIPTORS;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		rtvHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::DSV;
		desc.numDescriptors = MAX_DSV_DESCRIPTORS;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		dsvHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::UAV;
		desc.numDescriptors = MAX_UAV_DESCRIPTORS;
		desc.flags = EDescriptorHeapFlags::None;
		desc.nodeMask = 0;

		uavHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
	}

	createSystemTextures();
}

void TextureManager::destroy()
{
	for (Texture* tex : systemTextures)
	{
		delete tex;
	}
}

uint32 TextureManager::allocateSRVIndex()
{
	CHECK(nextSRVIndex < MAX_SRV_DESCRIPTORS);
	return nextSRVIndex++;
}

uint32 TextureManager::allocateRTVIndex()
{
	CHECK(nextSRVIndex < MAX_SRV_DESCRIPTORS);
	return nextSRVIndex++;
}

uint32 TextureManager::allocateDSVIndex()
{
	CHECK(nextDSVIndex < MAX_DSV_DESCRIPTORS);
	return nextDSVIndex++;
}

uint32 TextureManager::allocateUAVIndex()
{
	CHECK(nextUAVIndex < MAX_UAV_DESCRIPTORS);
	return nextUAVIndex++;
}

void TextureManager::createSystemTextures()
{
	struct InitSysTex
	{
		uint8 color[4];
		Texture** texturePtr;
		const wchar_t* debugName;
	};

	InitSysTex initTable[] = {
		{ { 127, 127, 127, 255 }, &systemTexture_grey2D , L"Texture_SystemGrey2D"  },
		{ { 255, 255, 255, 255 }, &systemTexture_white2D, L"Texture_SystemWhite2D" },
		{ { 000, 000, 000, 255 }, &systemTexture_black2D, L"Texture_SystemBlack2D" },
		{ { 255, 000, 000, 255 }, &systemTexture_red2D  , L"Texture_SystemRed2D"   },
		{ { 000, 255, 000, 255 }, &systemTexture_green2D, L"Texture_SystemGreen2D" },
		{ { 000, 000, 255, 255 }, &systemTexture_blue2D , L"Texture_SystemBlue2D"  },
	};
	std::vector<Texture*>* systemTexturesPtr = &systemTextures;
	ENQUEUE_RENDER_COMMAND(CreateSystemTextureGrey2D)(
		[&initTable, systemTexturesPtr](RenderCommandList& commandList)
		{
			for (uint32 i = 0; i < _countof(initTable); ++i)
			{
				TextureCreateParams params = TextureCreateParams::texture2D(
					EPixelFormat::R8G8B8A8_UNORM,
					ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
					1, 1, 1);
				
				Texture* tex = gRenderDevice->createTexture(params);
				tex->uploadData(commandList, initTable[i].color, 4, 4);
				tex->setDebugName(initTable[i].debugName);

				*(initTable[i].texturePtr) = tex;
				systemTexturesPtr->push_back(tex);
			}
		}
	);
	FLUSH_RENDER_COMMANDS();
}
