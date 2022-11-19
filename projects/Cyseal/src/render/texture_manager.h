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

	Texture* getSystemTextureGrey2D()  const { return systemTexture_grey2D;  }
	Texture* getSystemTextureWhite2D() const { return systemTexture_white2D; }
	Texture* getSystemTextureBlack2D() const { return systemTexture_black2D; }
	Texture* getSystemTextureRed2D()   const { return systemTexture_red2D;   }
	Texture* getSystemTextureGreen2D() const { return systemTexture_green2D; }
	Texture* getSystemTextureBlue2D()  const { return systemTexture_blue2D;  }

private:
	void createSystemTextures();

	Texture* systemTexture_grey2D  = nullptr;
	Texture* systemTexture_white2D = nullptr;
	Texture* systemTexture_black2D = nullptr;
	Texture* systemTexture_red2D   = nullptr;
	Texture* systemTexture_green2D = nullptr;
	Texture* systemTexture_blue2D  = nullptr;
	std::vector<Texture*> systemTextures;
};

extern TextureManager* gTextureManager;
