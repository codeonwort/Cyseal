#pragma once

#include "core/int_types.h"

#include <string>

namespace render_test
{
	/// <summary>
	/// Convert rgba32f image to rgba8ui image.
	/// </summary>
	/// <param name="src">rgba32f image data.</param>
	/// <param name="numPixels">Width * height.</param>
	/// <returns></returns>
	std::vector<uint8> rgba32f_to_rgba8ui(float* src, uint32 numPixels);

	/// <summary>
	/// Compare reference image against the actual render data.
	/// </summary>
	/// <param name="refImagePath">The path to reference PNG image, relative to the solution directory.</param>
	/// <param name="imageActual">Pointer to rgba8 image data.</param>
	/// <returns>The number of different pixels.</returns>
	uint32 compareRefImageToRgba8ui(const wchar_t* refImagePath, uint8* imageActual);

	/// <summary>
	/// Similar to compareRefImageToRgba8ui, but the actual data is converted to rgba8ui before comparison.
	/// </summary>
	/// <param name="refImagePath"></param>
	/// <param name="imageActual"></param>
	/// <returns></returns>
	uint32 compareRefImageToRgba32f(const wchar_t* refImagePath, float* imageActual);

	/// <summary>
	/// Save rgba8ui image as PNG.
	/// </summary>
	/// <param name="filepath"></param>
	/// <param name="rgba8Image"></param>
	/// <param name="width"></param>
	/// <param name="height"></param>
	/// <returns></returns>
	bool saveRgba8uiImage(const wchar_t* filepath, uint8* rgba8Image, uint32 width, uint32 height);

	/// <summary>
	/// Similar to saveRgba8uiImage, but the image data is converted to rgba8ui before save.
	/// </summary>
	/// <param name="filepath"></param>
	/// <param name="rgba8Image"></param>
	/// <param name="width"></param>
	/// <param name="height"></param>
	/// <returns></returns>
	bool saveRgba32fImage(const wchar_t* filepath, float* rgba32fImage, uint32 width, uint32 height);
}
