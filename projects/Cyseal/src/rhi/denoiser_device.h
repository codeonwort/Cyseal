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

	/// <summary>
	/// Execute denoiser with given input images.
	/// </summary>
	/// <param name="inColor">Noisy raytracing result.</param>
	/// <param name="inAlbedo">Clean albedo image.</param>
	/// <param name="inNormal">Clean surface normal image.</param>
	/// <param name="inputWidth">Width of input images.</param>
	/// <param name="inputHeight">Height of input images.</param>
	/// <param name="outResult">Raw buffer that contains denoised result</param>
	/// <returns>false if unable to denoise</returns>
	bool denoise(void* inColor, void* inAlbedo, void* inNormal, uint32 inputWidth, uint32 inputHeight, std::vector<uint8>& outResult);

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
