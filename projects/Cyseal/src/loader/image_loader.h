#pragma once

#include "core/int_types.h"
#include <string>

struct ImageLoadData
{
	~ImageLoadData()
	{
		if (buffer != nullptr)
		{
			delete buffer;
			buffer = nullptr;
		}
	}

	inline uint64 getRowPitch() const { return uint64(width) * uint64(numComponents); }
	inline uint64 getSlicePitch() const { return getRowPitch() * uint64(height); }

	uint8* buffer = nullptr;
	uint32 length = 0; // Size in bytes
	uint32 width = 0;
	uint32 height = 0;
	uint32 numComponents = 0; // # of 8-bit components
};

class ImageLoader
{
public:
	// Returns null if failed. You need to free the memory manually.
	ImageLoadData* load(const std::wstring& path, bool flipY = false, bool useResourceFinder = true);

	/// <summary>
	/// Save rgba8 data as png.
	/// </summary>
	/// <param name="wPath">Path to write the image.</param>
	/// <param name="rgba8Data">Pointer to rgba8 buffer (each pixel is 4 bytes)</param>
	/// <param name="width">Image width.</param>
	/// <param name="height">Image height.</param>
	/// <param name="rowPitch">Total bytes of a row. If 0, assumed to be (width * 4).</param>
	/// <returns>True if save was successful, false otherwise.</returns>
	static bool saveAsPng(const std::wstring& wPath, void* rgba8Data, uint32 width, uint32 height, uint64 rowPitch = 0);

private:
	//
};
