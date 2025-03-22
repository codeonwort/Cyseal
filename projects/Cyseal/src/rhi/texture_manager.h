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

	inline SharedPtr<TextureAsset> getSystemTextureGrey2D()    const { return systemTexture_grey2D;    }
	inline SharedPtr<TextureAsset> getSystemTextureWhite2D()   const { return systemTexture_white2D;   }
	inline SharedPtr<TextureAsset> getSystemTextureBlack2D()   const { return systemTexture_black2D;   }
	inline SharedPtr<TextureAsset> getSystemTextureRed2D()     const { return systemTexture_red2D;     }
	inline SharedPtr<TextureAsset> getSystemTextureGreen2D()   const { return systemTexture_green2D;   }
	inline SharedPtr<TextureAsset> getSystemTextureBlue2D()    const { return systemTexture_blue2D;    }
	inline SharedPtr<TextureAsset> getSystemTextureBlackCube() const { return systemTexture_blackCube; }

	inline SharedPtr<TextureAsset> getSTBNVec3Cosine()         const { return blueNoise_vec3cosine;    }

private:
	void createSystemTextures();
	void createBlueNoiseTextures();

	SharedPtr<TextureAsset> systemTexture_grey2D;
	SharedPtr<TextureAsset> systemTexture_white2D;
	SharedPtr<TextureAsset> systemTexture_black2D;
	SharedPtr<TextureAsset> systemTexture_red2D;
	SharedPtr<TextureAsset> systemTexture_green2D;
	SharedPtr<TextureAsset> systemTexture_blue2D;
	SharedPtr<TextureAsset> systemTexture_blackCube;

	SharedPtr<TextureAsset> blueNoise_vec3cosine;
};

extern TextureManager* gTextureManager;
