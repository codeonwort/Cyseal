#pragma once

#include "gpu_resource.h"
#include "gpu_resource_binding.h"

#include <memory>
#include <vector>

class Texture;

// #todo-renderdevice: Manage all texture memory here?
// Currently each texture holds a committed resource.
class TextureManager final
{
public:
	TextureManager() = default;
	~TextureManager() = default;

	void initialize();
	void destroy();

	std::shared_ptr<Texture> getSystemTextureGrey2D()  const { return systemTexture_grey2D;  }
	std::shared_ptr<Texture> getSystemTextureWhite2D() const { return systemTexture_white2D; }
	std::shared_ptr<Texture> getSystemTextureBlack2D() const { return systemTexture_black2D; }
	std::shared_ptr<Texture> getSystemTextureRed2D()   const { return systemTexture_red2D;   }
	std::shared_ptr<Texture> getSystemTextureGreen2D() const { return systemTexture_green2D; }
	std::shared_ptr<Texture> getSystemTextureBlue2D()  const { return systemTexture_blue2D;  }

private:
	void createSystemTextures();

	std::shared_ptr<Texture> systemTexture_grey2D  = nullptr;
	std::shared_ptr<Texture> systemTexture_white2D = nullptr;
	std::shared_ptr<Texture> systemTexture_black2D = nullptr;
	std::shared_ptr<Texture> systemTexture_red2D   = nullptr;
	std::shared_ptr<Texture> systemTexture_green2D = nullptr;
	std::shared_ptr<Texture> systemTexture_blue2D  = nullptr;
};

extern TextureManager* gTextureManager;
