#pragma once

#include "render/gpu_resource.h"
#include "render/texture.h"
#include "d3d_util.h"

class D3DTexture : public Texture
{
public:
	void initialize(const TextureCreateParams& params);

private:
	WRL::ComPtr<ID3D12Resource> rawResource;
};
