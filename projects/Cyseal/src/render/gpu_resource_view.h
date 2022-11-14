#pragma once

#include "core/int_types.h"

class RenderDevice;
class DescriptorHeap;
class VertexBuffer;
class Texture;
class StructuredBuffer;
class AccelerationStructure;

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
protected:
	// #todo-resource-view: ByteBuffer? Or just buffer?
	// Also no reason vertex/index buffers can't be used as SRV,
	// though I've got no plan for such usage for now.
	enum class ESource { Texture, StructuredBuffer, AccelerationStructure };
public:
	ShaderResourceView(Texture* inOwner)
		: ownerTexture(inOwner)
		, source(ESource::Texture)
	{}
	ShaderResourceView(StructuredBuffer* inOwner)
		: ownerStructuredBuffer(inOwner)
		, source(ESource::StructuredBuffer)
	{}
	ShaderResourceView(AccelerationStructure* inOwner)
		: ownerAccelStruct(inOwner)
		, source(ESource::AccelerationStructure)
	{}
protected:
	ESource source;
	Texture* ownerTexture = nullptr;
	StructuredBuffer* ownerStructuredBuffer = nullptr;
	AccelerationStructure* ownerAccelStruct = nullptr;
};

class UnorderedAccessView
{
protected:
	// #todo-resource-view: Same problem with ShaderResourceView::ESource
	enum class ESource { Texture, StructuredBuffer };
public:
	UnorderedAccessView(Texture* inOwner)
		: ownerTexture(inOwner)
		, source(ESource::Texture)
	{}
	UnorderedAccessView(StructuredBuffer* inOwner)
		: ownerStructuredBuffer(inOwner)
		, source(ESource::StructuredBuffer)
	{}
protected:
	ESource source;
	Texture* ownerTexture = nullptr;
	StructuredBuffer* ownerStructuredBuffer = nullptr;
};

class ConstantBufferView
{
public:
	virtual ~ConstantBufferView() = default;

	virtual void upload(void* data, uint32 sizeInBytes, uint32 bufferingIndex) = 0;

	virtual DescriptorHeap* getSourceHeap() = 0;
	virtual uint32 getDescriptorIndexInHeap(uint32 bufferingIndex) const = 0;
};
