#include "d3d_texture.h"
#include "d3d_device.h"
#include "d3d_pipeline_state.h"
#include "d3d_render_command.h"
#include "d3d_resource_view.h"
#include "d3d_into.h"
#include "core/assertion.h"

void D3DTexture::initialize(const TextureCreateParams& params)
{
	createParams = params;

	auto device = getD3DDevice()->getRawDevice();
	D3D12_RESOURCE_DESC textureDesc = into_d3d::textureDesc(params);
	
	// Validate desc
	const bool isColorTarget = 0 != (params.accessFlags & ETextureAccessFlags::COLOR_ALL);
	const bool isDepthTarget = 0 != (params.accessFlags & ETextureAccessFlags::DSV);
	{
		// Can't be both color target and depth target
		CHECK(!isColorTarget || !isDepthTarget);

		if (isDepthTarget)
		{
			CHECK(textureDesc.Format == DXGI_FORMAT_D16_UNORM
				|| textureDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT
				|| textureDesc.Format == DXGI_FORMAT_D32_FLOAT
				|| textureDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT);
		}
	}

	// #todo-dx12: Texture clear value
	bool bNeedsClearValue = false;
	D3D12_CLEAR_VALUE optClearValue;
	optClearValue.Format = textureDesc.Format;
	if (isColorTarget && (0 != (params.accessFlags & ETextureAccessFlags::RTV)))
	{
		bNeedsClearValue = true;
		optClearValue.Color[0] = 0.0f;
		optClearValue.Color[1] = 0.0f;
		optClearValue.Color[2] = 0.0f;
		optClearValue.Color[3] = 0.0f;
	}
	else if (isDepthTarget && (0 != (params.accessFlags & ETextureAccessFlags::DSV)))
	{
		bNeedsClearValue = true;
		optClearValue.DepthStencil.Depth = 1.0f;
		optClearValue.DepthStencil.Stencil = 0;
	}

	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	if (isColorTarget && (0 != (params.accessFlags & ETextureAccessFlags::CPU_WRITE)))
	{
		initialState |= D3D12_RESOURCE_STATE_COPY_DEST;
	}
	else if (isDepthTarget && (0 != (params.accessFlags & ETextureAccessFlags::DSV)))
	{
		initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	}

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(device->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		initialState,
		bNeedsClearValue ? &optClearValue : nullptr,
		IID_PPV_ARGS(&rawResource)));

	if (0 != (params.accessFlags & ETextureAccessFlags::CPU_WRITE))
	{
		// #todo-rhi: Properly count subresources?
		const uint32 numSubresources = textureDesc.DepthOrArraySize;
		const UINT64 uploadBufferSize = ::GetRequiredIntermediateSize(rawResource.Get(), 0, numSubresources);

		auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		HR(device->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)));
	}

	// #wip: Don't create texture views from within a texture.
	
	if (0 != (params.accessFlags & ETextureAccessFlags::SRV))
	{
		// #todo-texture: SRV ViewDimension
		CHECK(textureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

		ShaderResourceViewDesc srvDesc{};
		srvDesc.format                    = createParams.format;
		srvDesc.viewDimension             = ESRVDimension::Texture2D;
		srvDesc.texture2D.mostDetailedMip = 0;
		srvDesc.texture2D.mipLevels       = textureDesc.MipLevels;
		srvDesc.texture2D.planeSlice      = 0;
		srvDesc.texture2D.minLODClamp     = 0.0f;

		srv = std::unique_ptr<ShaderResourceView>(gRenderDevice->createSRV(this, srvDesc));
		srvHeap = srv->getSourceHeap();
		srvDescriptorIndex = srv->getDescriptorIndexInHeap();
	}

	if (0 != (params.accessFlags & ETextureAccessFlags::RTV) && params.numLayers == 1)
	{
		// #todo-texture: RTV ViewDimension
		CHECK(textureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

		D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
		viewDesc.Format = textureDesc.Format;
		viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		viewDesc.Texture2D.MipSlice = 0;
		viewDesc.Texture2D.PlaneSlice = 0;

		getD3DDevice()->allocateRTVHandle(rtvHeap, rtvHandle, rtvDescriptorIndex);
		device->CreateRenderTargetView(rawResource.Get(), &viewDesc, rtvHandle);

		rtv = std::make_unique<D3DRenderTargetView>();
		rtv->setCPUHandle(rtvHandle);
	}

	if (0 != (params.accessFlags & ETextureAccessFlags::DSV))
	{
		// #todo-texture: DSV ViewDimension
		CHECK(textureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

		D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc;
		viewDesc.Format = textureDesc.Format;
		viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		viewDesc.Flags = D3D12_DSV_FLAG_NONE;
		viewDesc.Texture2D.MipSlice = 0;

		getD3DDevice()->allocateDSVHandle(dsvHeap, dsvHandle, dsvDescriptorIndex);
		device->CreateDepthStencilView(rawResource.Get(), &viewDesc, dsvHandle);

		dsv = std::make_unique<D3DDepthStencilView>();
		dsv->setCPUHandle(dsvHandle);
	}

	if (0 != (params.accessFlags & ETextureAccessFlags::UAV))
	{
		// #todo-texture: UAV ViewDimension
		CHECK(textureDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D);

		UnorderedAccessViewDesc uavDesc{};
		uavDesc.format               = createParams.format;
		uavDesc.viewDimension        = EUAVDimension::Texture2D;
		uavDesc.texture2D.mipSlice   = 0;
		uavDesc.texture2D.planeSlice = 0;

		uav = std::unique_ptr<UnorderedAccessView>(gRenderDevice->createUAV(this, uavDesc));
		uavHeap = uav->getSourceHeap();
		uavDescriptorIndex = uav->getDescriptorIndexInHeap();
	}
}

void D3DTexture::uploadData(
	RenderCommandList& commandList,
	const void* buffer,
	uint64 rowPitch,
	uint64 slicePitch,
	uint32 subresourceIndex)
{
	CHECK(0 != (createParams.accessFlags & ETextureAccessFlags::CPU_WRITE));

	D3D12_SUBRESOURCE_DATA textureData{
		.pData      = buffer,
		.RowPitch   = (LONG_PTR)rowPitch,
		.SlicePitch = (LONG_PTR)slicePitch,
	};

	if (bIsPixelShaderResourceState)
	{
		TextureMemoryBarrier barrierAfter{
			ETextureMemoryLayout::PIXEL_SHADER_RESOURCE,
			ETextureMemoryLayout::COPY_DEST,
			this,
		};
		commandList.resourceBarriers(0, nullptr, 1, &barrierAfter);
	}

	// [ RESOURCE_MANIPULATION ERROR #864: COPYTEXTUREREGION_INVALIDSRCOFFSET ]
	// Offset must be a multiple of 512.
	uint64 slicePitchAligned = (slicePitch + 511) & ~511;

	UINT64 ret = ::UpdateSubresources(
		static_cast<D3DRenderCommandList*>(&commandList)->getRaw(),
		rawResource.Get(),
		textureUploadHeap.Get(), slicePitchAligned * subresourceIndex,
		subresourceIndex, 1, &textureData);
	CHECK(ret != 0);

	TextureMemoryBarrier barrierAfter{
		ETextureMemoryLayout::COPY_DEST,
		ETextureMemoryLayout::PIXEL_SHADER_RESOURCE,
		this,
	};
	commandList.resourceBarriers(0, nullptr, 1, &barrierAfter);
	bIsPixelShaderResourceState = true;
}

void D3DTexture::setDebugName(const wchar_t* debugName)
{
	rawResource->SetName(debugName);
}

RenderTargetView* D3DTexture::getRTV() const
{
	return rtv.get();
}

ShaderResourceView* D3DTexture::getSRV() const
{
	return srv.get();
}

DepthStencilView* D3DTexture::getDSV() const
{
	return dsv.get();
}

UnorderedAccessView* D3DTexture::getUAV() const
{
	return uav.get();
}
