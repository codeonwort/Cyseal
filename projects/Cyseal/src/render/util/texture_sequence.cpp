#include "texture_sequence.h"
#include "rhi/render_device.h"

static constexpr uint32 HISTORY_COUNT = 2;

void TextureSequence::initialize(EPixelFormat inPixelFormat, ETextureAccessFlags inTextureFlags, const wchar_t* inDebugName)
{
	pixelFormat = inPixelFormat;
	textureFlags = inTextureFlags;
	debugNameBase = inDebugName;
}

void TextureSequence::resizeTextures(RenderCommandList* commandList, uint32 inWidth, uint32 inHeight)
{
	if (width != inWidth || height != inHeight)
	{
		width = inWidth;
		height = inHeight;

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			pixelFormat, textureFlags, width, height, 1, 1, 0);

		for (uint32 i = 0; i < HISTORY_COUNT; ++i)
		{
			if (history[i] != nullptr)
			{
				commandList->enqueueDeferredDealloc(history[i].release());
			}

			std::wstring debugName = debugNameBase + std::to_wstring(i);
			history[i] = UniquePtr<Texture>(gRenderDevice->createTexture(texDesc));
			history[i]->setDebugName(debugName.c_str());

			if (ENUM_HAS_FLAG(textureFlags, ETextureAccessFlags::UAV))
			{
				historyUAV[i] = UniquePtr<UnorderedAccessView>(gRenderDevice->createUAV(history[i].get(),
					UnorderedAccessViewDesc{
						.format         = pixelFormat,
						.viewDimension  = EUAVDimension::Texture2D,
						.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
					}
				));
			}
			if (ENUM_HAS_FLAG(textureFlags, ETextureAccessFlags::SRV))
			{
				historySRV[i] = UniquePtr<ShaderResourceView>(gRenderDevice->createSRV(history[i].get(),
					ShaderResourceViewDesc{
						.format              = pixelFormat,
						.viewDimension       = ESRVDimension::Texture2D,
						.texture2D           = Texture2DSRVDesc{
							.mostDetailedMip = 0,
							.mipLevels       = 1,
							.planeSlice      = 0,
							.minLODClamp     = 0.0f,
						},
					}
				));
			}
		}
	}
}

Texture* TextureSequence::getTexture(uint32 ix) const
{
	CHECK(width != 0 && height != 0);
	return history[ix].get();
}

UnorderedAccessView* TextureSequence::getUAV(uint32 ix) const
{
	CHECK(ENUM_HAS_FLAG(textureFlags, ETextureAccessFlags::UAV));
	return historyUAV[ix].get();
}

ShaderResourceView* TextureSequence::getSRV(uint32 ix) const
{
	CHECK(ENUM_HAS_FLAG(textureFlags, ETextureAccessFlags::SRV));
	return historySRV[ix].get();
}
