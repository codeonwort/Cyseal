#include "texture_manager.h"
#include "render_device.h"
#include "texture.h"
#include "core/assertion.h"

#define MAX_SRV_DESCRIPTORS 1024
#define MAX_RTV_DESCRIPTORS 64

TextureManager* gTextureManager = nullptr;

void TextureManager::initialize()
{
	{
		DescriptorHeapDesc desc;
		desc.type = EDescriptorHeapType::CBV_SRV_UAV;
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

	createSystemTextures();
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

void TextureManager::createSystemTextures()
{
	uint8 grey2DData[] = { 127, 127, 127, 255 };

	Texture*& grey2DPtr = systemTexture_grey2D;
	ENQUEUE_RENDER_COMMAND(CreateSystemTextureGrey2D)(
		[&grey2DPtr, &grey2DData](RenderCommandList& commandList)
		{
			TextureCreateParams params = TextureCreateParams::texture2D(
				EPixelFormat::R8G8B8A8_UNORM,
				ETextureAccessFlags::SRV | ETextureAccessFlags::CPU_WRITE,
				1, 1, 1);
			grey2DPtr = gRenderDevice->createTexture(params);
			grey2DPtr->uploadData(commandList, grey2DData, 4, 4);
			grey2DPtr->setDebugName(L"Texture_SystemGrey2D");
		}
	);
	FLUSH_RENDER_COMMANDS();
}
