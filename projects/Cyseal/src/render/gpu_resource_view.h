#pragma once

#include "core/int_types.h"

class RenderDevice;
class DescriptorHeap;
class VertexBuffer;
class Texture;
class IndexBuffer;
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
	// #todo-resource-view: At least merge structured/index/vertex buffers...
	enum class ESource { Texture, StructuredBuffer, AccelerationStructure, IndexBuffer, VertexBuffer };
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
	ShaderResourceView(IndexBuffer* inOwner)
		: ownerIndexBuffer(inOwner)
		, source(ESource::IndexBuffer)
	{}
	ShaderResourceView(VertexBuffer* inOwner)
		: ownerVertexBuffer(inOwner)
		, source(ESource::VertexBuffer)
	{}
	virtual ~ShaderResourceView() = default;
protected:
	ESource source;
	Texture* ownerTexture = nullptr;
	StructuredBuffer* ownerStructuredBuffer = nullptr;
	AccelerationStructure* ownerAccelStruct = nullptr;
	IndexBuffer* ownerIndexBuffer = nullptr;
	VertexBuffer* ownerVertexBuffer = nullptr;
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
