#pragma once

#include "core/smart_pointer.h"
#include "rhi/rhi_forward.h"
#include "rhi/texture.h"

class TextureSequence
{
public:
	void initialize(EPixelFormat inPixelFormat, ETextureAccessFlags inTextureFlags, const wchar_t* inDebugName);

	void resizeTextures(RenderCommandList* commandList, uint32 inWidth, uint32 inHeight);

	Texture* getTexture(uint32 ix) const;
	UnorderedAccessView* getUAV(uint32 ix) const;
	ShaderResourceView* getSRV(uint32 ix) const;

private:
	EPixelFormat        pixelFormat    = EPixelFormat::UNKNOWN;
	ETextureAccessFlags textureFlags   = (ETextureAccessFlags)0;
	uint32              width          = 0;
	uint32              height         = 0;
	std::wstring        debugNameBase;

	UniquePtr<Texture> history[2];
	UniquePtr<UnorderedAccessView> historyUAV[2];
	UniquePtr<ShaderResourceView> historySRV[2];
};
