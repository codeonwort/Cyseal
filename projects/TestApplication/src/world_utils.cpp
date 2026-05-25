#include "world_utils.h"

#include "loader/image_loader.h"
#include "core/engine.h"

#include <string>
#include <array>

namespace worldUtils
{
	SharedPtr<TextureAsset> createSkyboxAsset(const wchar_t* inDebugName)
	{
		SharedPtr<TextureAsset> skyboxTexture = nullptr;

		ImageLoader imageLoader;

		std::wstring skyboxFilepaths[] = {
			L"skybox_Footballfield/posx.jpg", L"skybox_Footballfield/negx.jpg",
			L"skybox_Footballfield/posy.jpg", L"skybox_Footballfield/negy.jpg",
			L"skybox_Footballfield/posz.jpg", L"skybox_Footballfield/negz.jpg",
		};
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

		if (bValidSkyboxBlobs)
		{
			skyboxTexture = makeShared<TextureAsset>();
			std::wstring debugName = (inDebugName != nullptr) ? inDebugName : L"";

			ENQUEUE_RENDER_COMMAND(CreateSkybox)(
				[texWeak = WeakPtr<TextureAsset>(skyboxTexture), debugName, skyboxBlobs](RenderCommandList& commandList) {
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
						if (debugName.size() > 0)
						{
							texture->setDebugName(debugName.c_str());
						}

						tex->setGPUResource(SharedPtr<Texture>(texture));

						for (ImageLoadData* imageBlob : skyboxBlobs)
						{
							commandList.enqueueDeferredDealloc(imageBlob);
						}
				}
			);
		}

		return skyboxTexture;
	}
}
