#pragma once

#include "world/gpu_resource_asset.h"

namespace worldUtils
{
	SharedPtr<TextureAsset> createSkyboxAsset(const wchar_t* inDebugName = L"Texture_skybox");
};
