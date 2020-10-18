#pragma once

#include "core/int_types.h"
#include "pixel_format.h"
#include <vector>

class RootSignature;
class ShaderStage;

//////////////////////////////////////////////////////////////////////////
// Components of pipeline state

// D3D12_PRIMITIVE_TOPOLOGY
// Bind information about the primitive type, and data order that describes input data for the input assembler stage.
enum class EPrimitiveTopology : uint8
{
	UNDEFINED         = 0,
	POINTLIST         = 1,
	LINELIST          = 2,
	LINESTRIP         = 3,
	TRIANGLELIST      = 4,
	TRIANGLESTRIP     = 5,
	LINELIST_ADJ      = 10,
	LINESTRIP_ADJ     = 11,
	TRIANGLELIST_ADJ  = 12,
	TRIANGLESTRIP_ADJ = 13
	// #todo: CONTROL_POINT_PATCHLIST
};

// D3D12_PRIMITIVE_TOPOLOGY_TYPE
// Specifies how the pipeline interprets geometry or hull shader input primitives.
enum class EPrimitiveTopologyType : uint8
{
	Undefined = 0,
	Point     = 1,
	Line      = 2,
	Triangle  = 3,
	Patch     = 4
};

// D3D12_INPUT_CLASSIFICATION
// VkVertexInputRate
enum class EVertexInputClassification : uint8
{
	PerVertex,
	PerInstance
};

// D3D12_INPUT_ELEMENT_DESC
// VkVertexInputAttributeDescription
struct VertexInputElement
{
	const char* semantic;
	uint32 semanticIndex;
	EPixelFormat format;
	uint32 inputSlot;
	uint32 alignedByteOffset;
	EVertexInputClassification inputSlotClass;
	uint32 instanceDataStepRate;
};

// D3D12_INPUT_LAYOUT_DESC
// VkPipelineVertexInputStateCreateInfo
struct VertexInputLayout
{
	VertexInputLayout() = default;
	VertexInputLayout(std::initializer_list<VertexInputElement> inElements)
		: elements(inElements) {}

	std::vector<VertexInputElement> elements;
};

struct SampleDesc
{
	uint32 count   = 1;
	uint32 quality = 0;
};

// D3D12_FILL_MODE
// VkPolygonMode
enum class EFillMode : uint8
{
	Line = 2,
	Fill = 3,
	//Point,             // #todo-crossapi: vk only?
	//FillRectangleNV    // #todo-crossapi: vk only?
};

// D3D12_CULL_MODE
// VkCullModeFlags
enum class ECullMode : uint8
{
	None  = 1,
	Front = 2,
	Back  = 3,
	//FrontAndBack, // #todo-crossapi: vk only?
};

// D3D12_CONSERVATIVE_RASTERIZATION_MODE
// VkPipelineRasterizationConservativeStateCreateInfoEXT (VK_EXT_conservative_rasterization)
enum class EConservativeRasterizationMode : uint8
{
	Off = 0,
	On  = 1
};

// D3D12_RASTERIZER_DESC
// VkPipelineRasterizationStateCreateInfo
struct RasterizerDesc
{
	EFillMode fillMode                                = EFillMode::Fill;
	ECullMode cullMode                                = ECullMode::Back;
	bool frontCCW                                     = false;
	int32 depthBias                                   = 0;
	float depthBiasClamp                              = 0.0f;
	float slopeScaledDepthBias                        = 0.0f;
	bool depthClipEnable                              = true;
	bool multisampleEnable                            = false;
	bool antialisedLineEnable                         = false;
	uint32 forcedSampleCount                          = 0;
	EConservativeRasterizationMode conservativeRaster = EConservativeRasterizationMode::Off;
};

// D3D12_BLEND
enum class EBlend : uint8
{
	Zero             = 1,
	One              = 2,
	SrcColor         = 3,
	InvSrcColor      = 4,
	SrcAlpha         = 5,
	InvSrcAlpha      = 6,
	DestAlpha        = 7,
	InvDescAlpha     = 8,
	DestColor        = 9,
	InvDestColor     = 10,
	SrcAlphaSaturate = 11,
	BlendFactor      = 14,
	InvBlendFactor   = 15,
	Src1Color        = 16,
	InvSrc1Color     = 17,
	Src1Alpha        = 18,
	InvSrc1Alpha     = 19
};

// D3D12_BLEND_OP
enum class EBlendOp : uint8
{
	Add         = 1,
	Subtract    = 2,
	RevSubtract = 3,
	Min         = 4,
	Max         = 5
};

// D3D12_LOGIC_OP
enum class ELogicOp : uint8
{
	Clear        = 0,
	Set          = 1,
	Copy         = 2,
	CopyInverted = 3,
	Noop         = 4,
	Invert       = 5,
	And          = 6,
	Nand         = 7,
	Or           = 8,
	Nor          = 9,
	Xor          = 10,
	Equivalent   = 11,
	AndReverse   = 12,
	AndInverted  = 13,
	OrReverse    = 14,
	OrInverted   = 15
};

enum class EColorWriteEnable : uint8
{
	Red   = 1,
	Green = 2,
	Blue  = 4,
	Alpha = 8,
	All   = (Red | Green | Blue | Alpha)
};

// D3D12_RENDER_TARGET_BLEND_DESC
// VkPipelineColorBlendAttachmentState
struct RenderTargetBlendDesc
{
	bool blendEnable                        = false;
	bool logicOpEnable                      = false;
	EBlend srcBlend                         = EBlend::One;
	EBlend destBlend                        = EBlend::Zero;
	EBlendOp blendOp                        = EBlendOp::Add;
	EBlend srcBlendAlpha                    = EBlend::One;
	EBlend destBlendAlpha                   = EBlend::Zero;
	EBlendOp blendOpAlpha                   = EBlendOp::Add;
	ELogicOp logicOp                        = ELogicOp::Noop;
	EColorWriteEnable renderTargetWriteMask = EColorWriteEnable::All;
};

// D3D12_BLEND_DESC
// VkPipelineColorBlendStateCreateInfo
struct BlendDesc
{
	bool alphaToCoverageEnable;
	bool independentBlendEnable;
	RenderTargetBlendDesc renderTarget[8];
};

// D3D12_DEPTH_WRITE_MASK
// #todo-vulkan: ?
enum class EDepthWriteMask : uint8
{
	Zero = 0,
	All = 1
};

// D3D12_STENCIL_OP
// #todo-vulkan: ?
enum class EStencilOp : uint8
{
	Keep              = 1,
	Zero              = 2,
	Replace           = 3,
	IncrementSaturate = 4,
	DecrementSaturate = 5,
	Invert            = 6,
	Increment         = 7,
	Decrement         = 8
};

// D3D12_COMPARISON_FUNC
// VkCompareOp
enum class EComparisonFunc : uint8
{
	Never        = 1,
	Less         = 2,
	Equal        = 3,
	LessEqual    = 4,
	Greater      = 5,
	NotEqual     = 6,
	GreaterEqual = 7,
	Always       = 8
};

// D3D12_DEPTH_STENCILOP_DESC
// #todo-vulkan: ?
struct DepthstencilOpDesc
{
	EStencilOp stencilFailOp;
	EStencilOp stencilDepthFailOp;
	EStencilOp stencilPassOp;
	EComparisonFunc stencilFunc;
};

// D3D12_DEPTH_STENCIL_DESC
// VkPipelineDepthStencilStateCreateInfo
struct DepthstencilDesc
{
	bool depthEnable               = true;
	EDepthWriteMask depthWriteMask = EDepthWriteMask::All;
	EComparisonFunc depthFunc      = EComparisonFunc::Less;
	bool stencilEnable             = false;
	uint8 stencilReadMask          = 0xff;
	uint8 stencilWriteMask         = 0xff;
	DepthstencilOpDesc frontFace   = { EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always };
	DepthstencilOpDesc backFace    = { EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always };
};

//////////////////////////////////////////////////////////////////////////
// Pipeline state

// D3D12_GRAPHICS_PIPELINE_STATE_DESC
struct GraphicsPipelineDesc
{
	RootSignature* rootSignature = nullptr;
	ShaderStage* vs = nullptr;
	ShaderStage* ps = nullptr;
	ShaderStage* ds = nullptr;
	ShaderStage* hs = nullptr;
	ShaderStage* gs = nullptr;
	// #todo-crossapi: D3D12_STREAM_OUTPUT_DESC StreamOutput
	BlendDesc blendDesc;
	uint32 sampleMask;
	RasterizerDesc rasterizerDesc;
	DepthstencilDesc depthstencilDesc;
	VertexInputLayout inputLayout;
	// #todo-crossapi: D3D12_INDEX_BUFFER_STRIP_CUT_VALUE IBStripCutValue
	EPrimitiveTopologyType primitiveTopologyType;
	uint32 numRenderTargets;
	EPixelFormat rtvFormats[8] = {
		EPixelFormat::UNKNOWN, EPixelFormat::UNKNOWN, EPixelFormat::UNKNOWN, EPixelFormat::UNKNOWN,
		EPixelFormat::UNKNOWN, EPixelFormat::UNKNOWN, EPixelFormat::UNKNOWN, EPixelFormat::UNKNOWN };
	EPixelFormat dsvFormat = EPixelFormat::UNKNOWN;
	SampleDesc sampleDesc;
	// #todo-crossapi: UINT NodeMask
	// #todo-crossapi: D3D12_CACHED_PIPELINE_STATE CachedPSO
	// #todo-crossapi: D3D12_PIPELINE_STATE_FLAGS Flags
};

// ID3D12PipelineState
// VkPipeline
class PipelineState
{
};
