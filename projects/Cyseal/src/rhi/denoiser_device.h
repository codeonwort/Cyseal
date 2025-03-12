#pragma once

#include "OpenImageDenoise/oidn.h"

class Texture;

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
	// @param outDenoised Texture to write the denoising result.
	// @return false if unable to denoise.
	bool denoise(Texture* inColor, Texture* inAlbedo, Texture* inNormal, Texture* outDenoised);

	bool isValid() const;

private:
	bool checkNoDeviceError();

	OIDNDevice oidnDevice = nullptr;
	OIDNFilter oidnFilter = nullptr;

	uint32 width = 0;
	uint32 height = 0;
	OIDNBuffer oidnColorBuffer = nullptr;
	OIDNBuffer oidnAlbedoBuffer = nullptr;
	OIDNBuffer oidnNormalBuffer = nullptr;
	OIDNBuffer oidnDenoisedBuffer = nullptr;

};
