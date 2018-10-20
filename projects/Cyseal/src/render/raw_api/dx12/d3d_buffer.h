#pragma once

#include "render/buffer.h"
#include "d3d_util.h"

class RenderDevice;
class D3DDevice;

class D3DVertexBuffer : public VertexBuffer
{

public:
	virtual void initialize(void* initialData, uint32_t sizeInBytes, uint32_t strideInBytes) override;
	virtual void updateData(void* data, uint32_t sizeInBytes, uint32_t strideInBytes) override;

	D3D12_VERTEX_BUFFER_VIEW getView() const;

private:
	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// #todo: destroy after the command list is executed and realized.
	WRL::ComPtr<ID3D12Resource> uploadBuffer;

	D3D12_VERTEX_BUFFER_VIEW view;

};

class D3DIndexBuffer : public IndexBuffer
{
	
public:
	virtual void initialize(void* initialData, uint32_t sizeInBytes, EPixelFormat format) override;

	virtual void updateData(void* data, uint32_t sizeInBytes, EPixelFormat format) override;

	virtual uint32_t getIndexCount() override;

	D3D12_INDEX_BUFFER_VIEW getView() const;

private:
	WRL::ComPtr<ID3D12Resource> defaultBuffer;

	// #todo: destroy after the command list is executed and realized.
	WRL::ComPtr<ID3D12Resource> uploadBuffer;

	D3D12_INDEX_BUFFER_VIEW view;

	uint32_t indexCount;

};
