#pragma once

#include "core/int_types.h"
#include <string>

struct ImageLoadData
{
	~ImageLoadData()
	{
		if (buffer != nullptr)
		{
			free(buffer);
			buffer = nullptr;
		}
	}

	uint8* buffer = nullptr;
	uint32 length = 0; // Size in bytes
	uint32 width = 0;
	uint32 height = 0;
	uint32 numComponents = 0; // # of 8-bit components
};

class ImageLoader
{
public:
	// Returns true if successful, false otherwise.
	bool load(const std::wstring& path, ImageLoadData& outData);

private:
	//
};
