#include "d3d_render_command.h"
#include "d3d_resource.h"
#include "d3d_resource_view.h"
#include "d3d_pipeline_state.h"
#include "d3d_buffer.h"
#include "core/assertion.h"
#include <vector>

void D3DRenderCommandAllocator::initialize(RenderDevice* renderDevice)
{
	device = static_cast<D3DDevice*>(renderDevice);

	HR( device->getRawDevice()->CreateCommandAllocator(
			D3D12_COMMAND_LIST_TYPE_DIRECT,
			IID_PPV_ARGS(allocator.GetAddressOf()))
	);
}

void D3DRenderCommandAllocator::reset()
{
	HR( allocator->Reset() );
}

void D3DRenderCommandList::initialize(RenderDevice* renderDevice)
{
	device = static_cast<D3DDevice*>(renderDevice);

	auto rawDevice = device->getRawDevice();
	// #todo-dx12: Use the first allocator to create a command list
	// but this list will be reset with a different allocator every frame.
	// Is it safe? (No error in my PC but does the spec guarantees it?)
	auto tempAllocator = static_cast<D3DRenderCommandAllocator*>(device->getCommandAllocator(0))->getRaw();

	HR( rawDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		tempAllocator,
		nullptr, // Initial state
		IID_PPV_ARGS(commandList.GetAddressOf()))
	);
	HR( commandList->Close() );
}

void D3DRenderCommandList::reset(RenderCommandAllocator* allocator)
{
	ID3D12CommandAllocator* d3dAllocator = static_cast<D3DRenderCommandAllocator*>(allocator)->getRaw();
	HR( commandList->Reset(d3dAllocator, nullptr) );
}

void D3DRenderCommandList::close()
{
	HR( commandList->Close() );
}

// #todo: move to into_d3d namespace (d3d_pipeline_state.h)
static D3D12_PRIMITIVE_TOPOLOGY getD3DPrimitiveTopology(EPrimitiveTopology topology)
{
	switch (topology)
	{
	case EPrimitiveTopology::UNDEFINED:
		return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		break;
	case EPrimitiveTopology::POINTLIST:
		return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
		break;
	case EPrimitiveTopology::LINELIST:
		return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
		break;
	case EPrimitiveTopology::LINESTRIP:
		return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
		break;
	case EPrimitiveTopology::TRIANGLELIST:
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		break;
	case EPrimitiveTopology::TRIANGLESTRIP:
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
		break;
	case EPrimitiveTopology::LINELIST_ADJ:
		return D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
		break;
	case EPrimitiveTopology::LINESTRIP_ADJ:
		return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
		break;
	case EPrimitiveTopology::TRIANGLELIST_ADJ:
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
		break;
	case EPrimitiveTopology::TRIANGLESTRIP_ADJ:
		return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
		break;
	default:
		CHECK_NO_ENTRY();
		break;
	}
	return D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

void D3DRenderCommandList::iaSetPrimitiveTopology(EPrimitiveTopology topology)
{
	auto d3dTopology = getD3DPrimitiveTopology(topology);
	commandList->IASetPrimitiveTopology(d3dTopology);
}

void D3DRenderCommandList::iaSetVertexBuffers(
	int32 startSlot,
	uint32 numViews,
	VertexBuffer* const* vertexBuffers)
{
	std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
	views.resize(numViews);

	for (uint32 i = 0; i < numViews; ++i)
	{
		auto buffer = static_cast<D3DVertexBuffer*>(vertexBuffers[i]);
		views[i] = buffer->getView();
	}

	commandList->IASetVertexBuffers(startSlot, numViews, &views[0]);
}

void D3DRenderCommandList::iaSetIndexBuffer(IndexBuffer* indexBuffer)
{
	auto buffer = static_cast<D3DIndexBuffer*>(indexBuffer);
	commandList->IASetIndexBuffer(&buffer->getView());
}

void D3DRenderCommandList::rsSetViewport(const Viewport& viewport)
{
	D3D12_VIEWPORT rawViewport{
		viewport.topLeftX,
		viewport.topLeftY,
		viewport.width,
		viewport.height,
		viewport.minDepth,
		viewport.maxDepth
	};
	commandList->RSSetViewports(1, &rawViewport);
}

void D3DRenderCommandList::rsSetScissorRect(const ScissorRect& scissorRect)
{
	D3D12_RECT rect{
		static_cast<LONG>(scissorRect.left),
		static_cast<LONG>(scissorRect.top),
		static_cast<LONG>(scissorRect.right),
		static_cast<LONG>(scissorRect.bottom)
	};
	commandList->RSSetScissorRects(1, &rect);
}

void D3DRenderCommandList::transitionResource(
	GPUResource* resource,
	EGPUResourceState stateBefore,
	EGPUResourceState stateAfter)
{
	ID3D12Resource* rawResource = static_cast<D3DResource*>(resource)->getRaw();

	commandList->ResourceBarrier(
		1, &CD3DX12_RESOURCE_BARRIER::Transition(
			rawResource,
			(D3D12_RESOURCE_STATES)stateBefore,
			(D3D12_RESOURCE_STATES)stateAfter));
}

void D3DRenderCommandList::clearRenderTargetView(RenderTargetView* RTV, const float* rgba)
{
	auto d3dRTV = static_cast<D3DRenderTargetView*>(RTV);
	auto rawRTV = d3dRTV->getRaw();

	commandList->ClearRenderTargetView(rawRTV, rgba, 0, nullptr);
}

void D3DRenderCommandList::clearDepthStencilView(
	DepthStencilView* DSV,
	EDepthClearFlags clearFlags,
	float depth,
	uint8_t stencil)
{
	auto d3dDSV = static_cast<D3DDepthStencilView*>(DSV);
	auto rawDSV = d3dDSV->getRaw();

	commandList->ClearDepthStencilView(
		rawDSV, (D3D12_CLEAR_FLAGS)clearFlags,
		depth, stencil,
		0, nullptr);
}

void D3DRenderCommandList::omSetRenderTarget(RenderTargetView* RTV, DepthStencilView* DSV)
{
	CHECK(RTV != nullptr); // RTV should exist, DSV can be null

	D3D12_CPU_DESCRIPTOR_HANDLE rawRTV = static_cast<D3DRenderTargetView*>(RTV)->getRaw();
	D3D12_CPU_DESCRIPTOR_HANDLE rawDSV = { NULL };
	if (DSV != nullptr)
	{
		rawDSV = static_cast<D3DDepthStencilView*>(DSV)->getRaw();
	}

	commandList->OMSetRenderTargets(1, &rawRTV, true, (DSV != nullptr ? &rawDSV : nullptr));
}

void D3DRenderCommandList::setPipelineState(PipelineState* state)
{
	auto rawState = static_cast<D3DPipelineState*>(state)->getRaw();
	commandList->SetPipelineState(rawState);
}

void D3DRenderCommandList::setGraphicsRootSignature(RootSignature* rootSignature)
{
	auto rawSignature = static_cast<D3DRootSignature*>(rootSignature)->getRaw();
	commandList->SetGraphicsRootSignature(rawSignature);
}

void D3DRenderCommandList::setDescriptorHeaps(uint32 count, DescriptorHeap* const* heaps)
{
	std::vector<ID3D12DescriptorHeap*> rawHeaps;
	rawHeaps.resize(count);
	for (uint32 i = 0; i < count; ++i)
	{
		rawHeaps[i] = static_cast<D3DDescriptorHeap*>(heaps[i])->getRaw();
	}
	commandList->SetDescriptorHeaps(count, rawHeaps.data());
}

void D3DRenderCommandList::setGraphicsRootDescriptorTable(
	uint32 rootParameterIndex,
	DescriptorHeap* descriptorHeap,
	uint32 descriptorStartOffset)
{
	ID3D12DescriptorHeap* rawHeap = static_cast<D3DDescriptorHeap*>(descriptorHeap)->getRaw();
	D3D12_GPU_DESCRIPTOR_HANDLE tableHandle = rawHeap->GetGPUDescriptorHandleForHeapStart();
	tableHandle.ptr += (uint64)descriptorStartOffset * (uint64)device->getDescriptorSizeCbvSrvUav();
	
	commandList->SetGraphicsRootDescriptorTable(rootParameterIndex, tableHandle);
}

void D3DRenderCommandList::setGraphicsRootDescriptorSRV(
	uint32 rootParameterIndex,
	ShaderResourceView* srv)
{
	D3DShaderResourceView* d3dSRV = static_cast<D3DShaderResourceView*>(srv);
	D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = d3dSRV->getGPUVirtualAddress();

	commandList->SetGraphicsRootShaderResourceView(rootParameterIndex, gpuAddr);
}

void D3DRenderCommandList::setGraphicsRootConstant32(
	uint32 rootParameterIndex,
	uint32 constant32,
	uint32 destOffsetIn32BitValues)
{
	commandList->SetGraphicsRoot32BitConstant(
		rootParameterIndex,
		constant32,
		destOffsetIn32BitValues);
}

void D3DRenderCommandList::drawIndexedInstanced(
	uint32 indexCountPerInstance,
	uint32 instanceCount,
	uint32 startIndexLocation,
	int32 baseVertexLocation,
	uint32 startInstanceLocation)
{
	commandList->DrawIndexedInstanced(
		indexCountPerInstance,
		instanceCount,
		startIndexLocation,
		baseVertexLocation,
		startInstanceLocation);
}

void D3DRenderCommandList::drawInstanced(
	uint32 vertexCountPerInstance,
	uint32 instanceCount,
	uint32 startVertexLocation,
	uint32 startInstanceLocation)
{
	commandList->DrawInstanced(
		vertexCountPerInstance,
		instanceCount,
		startVertexLocation,
		startInstanceLocation);
}

void D3DRenderCommandQueue::initialize(RenderDevice* renderDevice)
{
	device = static_cast<D3DDevice*>(renderDevice);

	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	HR( device->getRawDevice()->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)) );
}

void D3DRenderCommandQueue::executeCommandList(class RenderCommandList* commandList)
{
	auto rawList = static_cast<D3DRenderCommandList*>(commandList);
	ID3D12CommandList* const lists[] = { rawList->getRaw() };
	queue->ExecuteCommandLists(1, lists);
}
