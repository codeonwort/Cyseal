#include "texture_manager.h"
#include "render_device.h"
#include "core/assertion.h"

#define MAX_TEXTURE_DESCRIPTORS 1024

TextureManager* gTextureManager = nullptr;

TextureManager::TextureManager()
	: srvHeap(nullptr)
	, srvIndex(0)
{
}

TextureManager::~TextureManager()
{
}

void TextureManager::initialize()
{
	DescriptorHeapDesc desc;
	desc.type           = EDescriptorHeapType::CBV_SRV_UAV;
	desc.numDescriptors = MAX_TEXTURE_DESCRIPTORS;
	desc.flags          = EDescriptorHeapFlags::None;
	desc.nodeMask       = 0;

	srvHeap = std::unique_ptr<DescriptorHeap>(gRenderDevice->createDescriptorHeap(desc));
}

uint32 TextureManager::allocateSRVIndex()
{
	CHECK(srvIndex < MAX_TEXTURE_DESCRIPTORS);
	return srvIndex++;
}
