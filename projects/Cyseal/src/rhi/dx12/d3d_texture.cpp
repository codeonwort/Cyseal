#include "d3d_texture.h"
#include "d3d_device.h"
#include "d3d_pipeline_state.h"
#include "d3d_render_command.h"
#include "d3d_resource_view.h"
#include "d3d_into.h"
#include "core/assertion.h"
#include "rhi/rhi_policy.h"

void D3DTexture::initialize(const TextureCreateParams& params)
{
	createParams = params;

	auto device = getD3DDevice()->getRawDevice();
	D3D12_RESOURCE_DESC textureDesc = into_d3d::textureDesc(params);
	
	// Validate desc
	const bool isColorTarget = ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::COLOR_ALL);
	const bool isDepthTarget = ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::DSV);
	{
		// Can't be both color target and depth target
		CHECK(!isColorTarget || !isDepthTarget);

		if (isDepthTarget)
		{
			CHECK(textureDesc.Format == DXGI_FORMAT_D16_UNORM
				|| textureDesc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT
				|| textureDesc.Format == DXGI_FORMAT_D32_FLOAT
				|| textureDesc.Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT
				|| textureDesc.Format == DXGI_FORMAT_R24G8_TYPELESS
				|| textureDesc.Format == DXGI_FORMAT_R32G8X24_TYPELESS);
		}
	}

	// #todo-dx12: Texture clear value
	bool bNeedsClearValue = false;
	D3D12_CLEAR_VALUE optClearValue;

	optClearValue.Format = textureDesc.Format;
	if (optClearValue.Format == DXGI_FORMAT_R24G8_TYPELESS) optClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	if (optClearValue.Format == DXGI_FORMAT_R32G8X24_TYPELESS) optClearValue.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT;

	if (isColorTarget && ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::RTV))
	{
		bNeedsClearValue = true;
		optClearValue.Color[0] = params.optimalClearColor[0];
		optClearValue.Color[1] = params.optimalClearColor[1];
		optClearValue.Color[2] = params.optimalClearColor[2];
		optClearValue.Color[3] = params.optimalClearColor[3];
	}
	else if (isDepthTarget && ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::DSV))
	{
		bNeedsClearValue = true;
		optClearValue.DepthStencil.Depth = getDeviceFarDepth();
		optClearValue.DepthStencil.Stencil = 0;
	}

	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	if (isColorTarget && ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::CPU_WRITE))
	{
		initialState |= D3D12_RESOURCE_STATE_COPY_DEST;
	}
	else if (isDepthTarget && ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::DSV))
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

	if (ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::CPU_WRITE))
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
}

void D3DTexture::uploadData(
	RenderCommandList& commandList,
	const void* buffer,
	uint64 rowPitch,
	uint64 slicePitch,
	uint32 subresourceIndex)
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, ETextureAccessFlags::CPU_WRITE));

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
