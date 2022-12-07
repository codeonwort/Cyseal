#pragma once

#include "gpu_resource.h"
#include "gpu_resource_binding.h"
#include "world/gpu_resource_asset.h"

#include <memory>
#include <vector>

// #todo-renderdevice: Manage all texture memory here?
// Currently each texture holds a committed resource.
class TextureManager final
{
public:
	TextureManager() = default;
	~TextureManager() = default;

	void initialize();
	void destroy();

	std::shared_ptr<TextureAsset> getSystemTextureGrey2D()  const { return systemTexture_grey2D;  }
	std::shared_ptr<TextureAsset> getSystemTextureWhite2D() const { return systemTexture_white2D; }
	std::shared_ptr<TextureAsset> getSystemTextureBlack2D() const { return systemTexture_black2D; }
	std::shared_ptr<TextureAsset> getSystemTextureRed2D()   const { return systemTexture_red2D;   }
	std::shared_ptr<TextureAsset> getSystemTextureGreen2D() const { return systemTexture_green2D; }
	std::shared_ptr<TextureAsset> getSystemTextureBlue2D()  const { return systemTexture_blue2D;  }

private:
	void createSystemTextures();

	std::shared_ptr<TextureAsset> systemTexture_grey2D;
	std::shared_ptr<TextureAsset> systemTexture_white2D;
	std::shared_ptr<TextureAsset> systemTexture_black2D;
	std::shared_ptr<TextureAsset> systemTexture_red2D;
	std::shared_ptr<TextureAsset> systemTexture_green2D;
	std::shared_ptr<TextureAsset> systemTexture_blue2D;
};

extern TextureManager* gTextureManager;
