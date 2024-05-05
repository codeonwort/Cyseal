#pragma once

#include "gpu_resource.h"
#include "gpu_resource_binding.h"
#include "world/gpu_resource_asset.h"
#include "core/smart_pointer.h"

// #todo-renderdevice: Manage all texture memory here?
// Currently each texture holds a committed resource.
class TextureManager final
{
public:
	TextureManager() = default;
	~TextureManager() = default;

	void initialize();
	void destroy();

	SharedPtr<TextureAsset> getSystemTextureGrey2D()    const { return systemTexture_grey2D;    }
	SharedPtr<TextureAsset> getSystemTextureWhite2D()   const { return systemTexture_white2D;   }
	SharedPtr<TextureAsset> getSystemTextureBlack2D()   const { return systemTexture_black2D;   }
	SharedPtr<TextureAsset> getSystemTextureRed2D()     const { return systemTexture_red2D;     }
	SharedPtr<TextureAsset> getSystemTextureGreen2D()   const { return systemTexture_green2D;   }
	SharedPtr<TextureAsset> getSystemTextureBlue2D()    const { return systemTexture_blue2D;    }
	SharedPtr<TextureAsset> getSystemTextureBlackCube() const { return systemTexture_blackCube; }

private:
	void createSystemTextures();

	SharedPtr<TextureAsset> systemTexture_grey2D;
	SharedPtr<TextureAsset> systemTexture_white2D;
	SharedPtr<TextureAsset> systemTexture_black2D;
	SharedPtr<TextureAsset> systemTexture_red2D;
	SharedPtr<TextureAsset> systemTexture_green2D;
	SharedPtr<TextureAsset> systemTexture_blue2D;
	SharedPtr<TextureAsset> systemTexture_blackCube;
};

extern TextureManager* gTextureManager;
