#pragma once

#include "core/int_types.h"

class RenderDevice;
class VertexBuffer;
class Texture;

enum class EResourceViewDimension
{
	Buffer,
	TEXTURE_1D,
	TEXTURE_2D,
	TEXTURE_3D
};

class RenderTargetView
{
};

class DepthStencilView
{
};

class ShaderResourceView
{
public:
	ShaderResourceView(Texture* inOwner) : owner(inOwner) {}
protected:
	Texture* owner;
};

class UnorderedAccessView
{
};

class ConstantBufferView
{
public:
	virtual ~ConstantBufferView() = default;

	virtual void upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex) = 0;

	virtual uint32 getDescriptorIndexInHeap(uint32 bufferingIndex) const = 0;
};
