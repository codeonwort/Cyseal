#pragma once

#include "gpu_resource.h"
#include "render_command.h"
#include "util/enum_util.h"

class RenderTargetView;

// ------------------------------------ //
// #todo-vulkan: Vulkan API for texture //
// 
// VkImage, VkDeviceMemory, VkImageCreateInfo,
// VkMemoryRequirements, VkMemoryAllocateInfo,
// vkCreateImage, vkGetImageMemoryRequirements,
// vkAllocateMemory, vkBindImageMemory, ...
// ------------------------------------ //

/*
typedef struct D3D12_RESOURCE_DESC
{
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;
} D3D12_RESOURCE_DESC;

typedef struct VkImageCreateInfo {
    VkStructureType          sType;
    const void*              pNext;
    VkImageCreateFlags       flags;
    VkImageType              imageType;
    VkFormat                 format;
    VkExtent3D               extent;
    uint32_t                 mipLevels;
    uint32_t                 arrayLayers;
    VkSampleCountFlagBits    samples;
    VkImageTiling            tiling;
    VkImageUsageFlags        usage;
    VkSharingMode            sharingMode;
    uint32_t                 queueFamilyIndexCount;
    const uint32_t*          pQueueFamilyIndices;
    VkImageLayout            initialLayout;
} VkImageCreateInfo;
*/

enum class ETextureDimension : uint8
{
    UNKNOWN = 0,
    TEXTURE1D = 1,
    TEXTURE2D = 2,
    TEXTURE3D = 3
};

enum class ETextureAccessFlags : uint32
{
    SRV          = 1 << 0,
    RTV          = 1 << 1,
    UAV          = 1 << 2,
    DSV          = 1 << 3,
    CPU_WRITE    = 1 << 4,

    COLOR_ALL = SRV | RTV | UAV
};
ENUM_CLASS_FLAGS(ETextureAccessFlags);

// D3D12_RESOURCE_DESC (CD3DX12_RESOURCE_DESC)
// VkImageCreateInfo
struct TextureCreateParams
{
    ETextureDimension dimension;
    EPixelFormat format;
    ETextureAccessFlags accessFlags;
    uint32 width;
    uint32 height;
    uint16 depth; // or array size
    uint16 mipLevels; // 0 means full mips
    uint32 sampleCount;
    uint32 sampleQuality;

    static TextureCreateParams texture2D(
        EPixelFormat inFormat,
        ETextureAccessFlags inAccessFlags,
        uint32 inWidth,
        uint32 inHeight,
        uint16 inMipLevels = 1,
        uint32 inSampleCount = 1,
        uint32 inSampleQuality = 0)
    {
        return TextureCreateParams{
            ETextureDimension::TEXTURE2D,
            inFormat,
            inAccessFlags,
            inWidth,
            inHeight,
            1,
            inMipLevels,
            inSampleCount,
            inSampleQuality
        };
    }
};

class Texture : public GPUResource
{
public:
    virtual void uploadData(RenderCommandList& commandList, const void* buffer, uint64 rowPitch, uint64 slicePitch) = 0;
    virtual void setDebugName(const wchar_t* debugName) = 0;

    virtual RenderTargetView* getRTV() const = 0;
    virtual ShaderResourceView* getSRV() const = 0;
    virtual DepthStencilView* getDSV() const = 0;

    // Element index in the descriptor heap from which the descriptor was created.
    virtual uint32 getSRVDescriptorIndex() const = 0;
    virtual uint32 getRTVDescriptorIndex() const = 0;
    virtual uint32 getDSVDescriptorIndex() const = 0;
    virtual uint32 getUAVDescriptorIndex() const = 0;

private:
    // #todo-texture: getUAV()
};
