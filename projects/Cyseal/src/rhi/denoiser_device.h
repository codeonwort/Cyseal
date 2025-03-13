#pragma once

#include "OpenImageDenoise/oidn.h"
#include <vector>

class Texture;
class RenderCommandList;

class DenoiserDevice
{

public:
	void create();
	void destroy();
	
	// Resize internal resources, if needed.
	void recreateResources(uint32 imageWidth, uint32 imageHeight);

	// @param inColor Noisy raytracing result.
	// @param inAlbedo Clean albedo image.
	// @param inNormal Clean surface normal image.
	// @param outResult Raw buffer that contains denoised result.
	// @return false if unable to denoise.
	bool denoise(Texture* inColor, Texture* inAlbedo, Texture* inNormal, std::vector<uint8>& outResult);

	bool isValid() const;

private:
	bool checkNoDeviceError();

	OIDNDevice oidnDevice = nullptr;
	OIDNFilter oidnFilter = nullptr;

	uint32 width = 0;
	uint32 height = 0;
	size_t oidnBufferPixelByteStride = 0;
	size_t oidnBufferSize = 0;
	OIDNBuffer oidnColorBuffer = nullptr;
	OIDNBuffer oidnAlbedoBuffer = nullptr;
	OIDNBuffer oidnNormalBuffer = nullptr;
	OIDNBuffer oidnDenoisedBuffer = nullptr;

};
