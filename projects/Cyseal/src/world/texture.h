#pragma once

#include <memory>

class Texture;

// Application-side view of texture.
class TextureAsset final
{
public:
	TextureAsset(std::shared_ptr<Texture> inRHI = nullptr)
		: rhi(inRHI)
	{}

	inline std::shared_ptr<Texture> getGPUResource() const { return rhi; }
	inline void setGPUResource(std::shared_ptr<Texture> inRHI) { rhi = inRHI; }

private:
	std::shared_ptr<Texture> rhi;
};
