#include "d3d_render_command.h"
#include "d3d_resource.h"
#include "d3d_resource_view.h"

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
	commandAllocator = static_cast<D3DRenderCommandAllocator*>(device->getCommandAllocator());

	auto rawDevice = device->getRawDevice();
	auto rawAllocator = commandAllocator->getRaw();

	HR( rawDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		rawAllocator,
		nullptr,
		IID_PPV_ARGS(commandList.GetAddressOf()))
	);
	HR( commandList->Close() );
}

void D3DRenderCommandList::reset()
{
	HR( commandList->Reset(commandAllocator->getRaw(), nullptr) );
}

void D3DRenderCommandList::close()
{
	HR( commandList->Close() );
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
	auto d3dResource = static_cast<D3DResource*>(resource);
	auto rawResource = d3dResource->getRaw();

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
	EClearFlags clearFlags,
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
	auto d3dRTV = static_cast<D3DRenderTargetView*>(RTV);
	auto d3dDSV = static_cast<D3DDepthStencilView*>(DSV);
	auto rawRTV = d3dRTV->getRaw();
	auto rawDSV = d3dDSV->getRaw();

	commandList->OMSetRenderTargets(1, &rawRTV, true, &rawDSV);
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
