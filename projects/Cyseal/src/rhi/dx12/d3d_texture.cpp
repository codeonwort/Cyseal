#include "d3d_texture.h"
#include "d3d_device.h"
#include "d3d_pipeline_state.h"
#include "d3d_render_command.h"
#include "d3d_resource_view.h"
#include "d3d_into.h"
#include "core/assertion.h"
#include "rhi/rhi_policy.h"

// https://stackoverflow.com/questions/40339138/convert-dxgi-format-to-a-bpp
size_t bitsPerPixel(DXGI_FORMAT fmt)
{
	switch (fmt)
	{
	case DXGI_FORMAT_R32G32B32A32_TYPELESS:
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
	case DXGI_FORMAT_R32G32B32A32_UINT:
	case DXGI_FORMAT_R32G32B32A32_SINT:
		return 128;

	case DXGI_FORMAT_R32G32B32_TYPELESS:
	case DXGI_FORMAT_R32G32B32_FLOAT:
	case DXGI_FORMAT_R32G32B32_UINT:
	case DXGI_FORMAT_R32G32B32_SINT:
		return 96;

	case DXGI_FORMAT_R16G16B16A16_TYPELESS:
	case DXGI_FORMAT_R16G16B16A16_FLOAT:
	case DXGI_FORMAT_R16G16B16A16_UNORM:
	case DXGI_FORMAT_R16G16B16A16_UINT:
	case DXGI_FORMAT_R16G16B16A16_SNORM:
	case DXGI_FORMAT_R16G16B16A16_SINT:
	case DXGI_FORMAT_R32G32_TYPELESS:
	case DXGI_FORMAT_R32G32_FLOAT:
	case DXGI_FORMAT_R32G32_UINT:
	case DXGI_FORMAT_R32G32_SINT:
	case DXGI_FORMAT_R32G8X24_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
	case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
	case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
	case DXGI_FORMAT_Y416:
	case DXGI_FORMAT_Y210:
	case DXGI_FORMAT_Y216:
		return 64;

	case DXGI_FORMAT_R10G10B10A2_TYPELESS:
	case DXGI_FORMAT_R10G10B10A2_UNORM:
	case DXGI_FORMAT_R10G10B10A2_UINT:
	case DXGI_FORMAT_R11G11B10_FLOAT:
	case DXGI_FORMAT_R8G8B8A8_TYPELESS:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
	case DXGI_FORMAT_R8G8B8A8_UINT:
	case DXGI_FORMAT_R8G8B8A8_SNORM:
	case DXGI_FORMAT_R8G8B8A8_SINT:
	case DXGI_FORMAT_R16G16_TYPELESS:
	case DXGI_FORMAT_R16G16_FLOAT:
	case DXGI_FORMAT_R16G16_UNORM:
	case DXGI_FORMAT_R16G16_UINT:
	case DXGI_FORMAT_R16G16_SNORM:
	case DXGI_FORMAT_R16G16_SINT:
	case DXGI_FORMAT_R32_TYPELESS:
	case DXGI_FORMAT_D32_FLOAT:
	case DXGI_FORMAT_R32_FLOAT:
	case DXGI_FORMAT_R32_UINT:
	case DXGI_FORMAT_R32_SINT:
	case DXGI_FORMAT_R24G8_TYPELESS:
	case DXGI_FORMAT_D24_UNORM_S8_UINT:
	case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
	case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
	case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
	case DXGI_FORMAT_R8G8_B8G8_UNORM:
	case DXGI_FORMAT_G8R8_G8B8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
	case DXGI_FORMAT_B8G8R8A8_TYPELESS:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
	case DXGI_FORMAT_B8G8R8X8_TYPELESS:
	case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
	case DXGI_FORMAT_AYUV:
	case DXGI_FORMAT_Y410:
	case DXGI_FORMAT_YUY2:
		return 32;

	case DXGI_FORMAT_P010:
	case DXGI_FORMAT_P016:
		return 24;

	case DXGI_FORMAT_R8G8_TYPELESS:
	case DXGI_FORMAT_R8G8_UNORM:
	case DXGI_FORMAT_R8G8_UINT:
	case DXGI_FORMAT_R8G8_SNORM:
	case DXGI_FORMAT_R8G8_SINT:
	case DXGI_FORMAT_R16_TYPELESS:
	case DXGI_FORMAT_R16_FLOAT:
	case DXGI_FORMAT_D16_UNORM:
	case DXGI_FORMAT_R16_UNORM:
	case DXGI_FORMAT_R16_UINT:
	case DXGI_FORMAT_R16_SNORM:
	case DXGI_FORMAT_R16_SINT:
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_A8P8:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 16;

	case DXGI_FORMAT_NV12:
	case DXGI_FORMAT_420_OPAQUE:
	case DXGI_FORMAT_NV11:
		return 12;

	case DXGI_FORMAT_R8_TYPELESS:
	case DXGI_FORMAT_R8_UNORM:
	case DXGI_FORMAT_R8_UINT:
	case DXGI_FORMAT_R8_SNORM:
	case DXGI_FORMAT_R8_SINT:
	case DXGI_FORMAT_A8_UNORM:
	case DXGI_FORMAT_AI44:
	case DXGI_FORMAT_IA44:
	case DXGI_FORMAT_P8:
		return 8;

	case DXGI_FORMAT_R1_UNORM:
		return 1;

	case DXGI_FORMAT_BC1_TYPELESS:
	case DXGI_FORMAT_BC1_UNORM:
	case DXGI_FORMAT_BC1_UNORM_SRGB:
	case DXGI_FORMAT_BC4_TYPELESS:
	case DXGI_FORMAT_BC4_UNORM:
	case DXGI_FORMAT_BC4_SNORM:
		return 4;
	default:
		CHECK_NO_ENTRY();
		return 0;
	}
}

void D3DTexture::initialize(const TextureCreateParams& params)
{
	createParams = params;

	auto rawDevice = device->getRawDevice();
	D3D12_RESOURCE_DESC textureDesc = into_d3d::textureDesc(params);

	const size_t bytesPerPixel = bitsPerPixel(textureDesc.Format) / 8;
	rowPitch = (textureDesc.Width * bytesPerPixel + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
	
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
		optClearValue.DepthStencil.Depth = params.optimalClearDepth;
		optClearValue.DepthStencil.Stencil = params.optimalClearStencil;
	}

	D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
	lastMemoryLayout = ETextureMemoryLayout::COMMON;
	if (isColorTarget && ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::CPU_WRITE))
	{
		initialState |= D3D12_RESOURCE_STATE_COPY_DEST;
		saveLastMemoryLayout(ETextureMemoryLayout::COPY_DEST);
	}
	else if (isDepthTarget && ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::DSV))
	{
		initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		saveLastMemoryLayout(ETextureMemoryLayout::DEPTH_STENCIL_TARGET);
	}

	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	HR(rawDevice->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&textureDesc,
		initialState,
		bNeedsClearValue ? &optClearValue : nullptr,
		IID_PPV_ARGS(&rawResource)));

	// #todo-rhi: Properly count subresources?
	uint32 numSubresources = textureDesc.DepthOrArraySize;
	if (params.dimension == ETextureDimension::TEXTURE3D)
	{
		numSubresources = 1;
	}
	const UINT64 uploadBufferSize = ::GetRequiredIntermediateSize(rawResource.Get(), 0, numSubresources);
	readbackBufferSize = uploadBufferSize;

	if (ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::CPU_WRITE))
	{
		auto uploadHeapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
		HR(rawDevice->CreateCommittedResource(
			&uploadHeapProps,
			D3D12_HEAP_FLAG_NONE,
			&uploadBufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&textureUploadHeap)));
	}

	if (ENUM_HAS_FLAG(params.accessFlags, ETextureAccessFlags::CPU_READBACK))
	{
		auto readbackHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
		auto readbackBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(readbackBufferSize);
		HR(rawDevice->CreateCommittedResource(
			&readbackHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&readbackBufferDesc,
			D3D12_RESOURCE_STATE_COPY_DEST,
			nullptr,
			IID_PPV_ARGS(&readbackBuffer)));
		
		readbackFootprintDesc = D3D12_TEXTURE_COPY_LOCATION{
			.pResource        = readbackBuffer.Get(),
			.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
			.PlacedFootprint  = D3D12_PLACED_SUBRESOURCE_FOOTPRINT{
				.Offset       = 0,
				.Footprint    = D3D12_SUBRESOURCE_FOOTPRINT{
					.Format   = textureDesc.Format,
					.Width    = (UINT)textureDesc.Width,
					.Height   = (UINT)textureDesc.Height,
					.Depth    = (UINT)textureDesc.DepthOrArraySize,
					.RowPitch = (UINT)rowPitch
				}
			}
		};
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

	if (lastMemoryLayout != ETextureMemoryLayout::COPY_DEST)
	{
		TextureMemoryBarrier barrierBefore{ lastMemoryLayout, ETextureMemoryLayout::COPY_DEST, this };
		commandList.resourceBarriers(0, nullptr, 1, &barrierBefore);
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

	if (lastMemoryLayout != ETextureMemoryLayout::COPY_DEST)
	{
		TextureMemoryBarrier barrierAfter{ ETextureMemoryLayout::COPY_DEST, lastMemoryLayout, this };
		commandList.resourceBarriers(0, nullptr, 1, &barrierAfter);
	}
}

bool D3DTexture::prepareReadback(RenderCommandList* commandList)
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, ETextureAccessFlags::CPU_READBACK));

	if (lastMemoryLayout != ETextureMemoryLayout::COPY_SRC)
	{
		TextureMemoryBarrier barrierBefore{ lastMemoryLayout, ETextureMemoryLayout::COPY_SRC, this };
		commandList->resourceBarriers(0, nullptr, 1, &barrierBefore);
	}

	ID3D12GraphicsCommandList4* d3dCommandList = static_cast<D3DRenderCommandList*>(commandList)->getRaw();

	D3D12_TEXTURE_COPY_LOCATION pSrc{
		.pResource        = rawResource.Get(),
		.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
		.SubresourceIndex = 0,
	};
	D3D12_BOX srcRegion{
		.left   = 0,
		.top    = 0,
		.front  = 0,
		.right  = createParams.width,
		.bottom = createParams.height,
		.back   = 1,
	};
	d3dCommandList->CopyTextureRegion(&readbackFootprintDesc, 0, 0, 0, &pSrc, &srcRegion);

	if (lastMemoryLayout != ETextureMemoryLayout::COPY_SRC)
	{
		TextureMemoryBarrier barrierAfter{ ETextureMemoryLayout::COPY_SRC, lastMemoryLayout, this };
		commandList->resourceBarriers(0, nullptr, 1, &barrierAfter);
	}

	bReadbackPrepared = true;

	return true;
}

bool D3DTexture::readbackData(void* dst)
{
	CHECK(ENUM_HAS_FLAG(createParams.accessFlags, ETextureAccessFlags::CPU_READBACK));

	if (!bReadbackPrepared)
	{
		return false;
	}

	D3D12_RANGE readbackBufferRange{ 0, readbackBufferSize };
	void* src = nullptr;
	readbackBuffer->Map(0, &readbackBufferRange, &src);

	memcpy_s(dst, readbackBufferSize, src, readbackBufferSize);

	D3D12_RANGE emptyRange{ 0, 0 };
	readbackBuffer->Unmap(0, &emptyRange);

	return true;
}

void D3DTexture::setDebugName(const wchar_t* debugName)
{
	rawResource->SetName(debugName);
}
