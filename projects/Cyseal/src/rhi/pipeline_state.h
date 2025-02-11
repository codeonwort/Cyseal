#pragma once

#include "rhi_forward.h"
#include "pixel_format.h"
#include "core/int_types.h"
#include "util/enum_util.h"

#include <vector>
#include <string>

class VertexBuffer;
class IndexBuffer;

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
	// #todo-rhi: CONTROL_POINT_PATCHLIST
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
	bool frontCCW                                     = true; // NOTE: D3D12 uses CW by default but I'll use CCW.
	int32 depthBias                                   = 0;
	float depthBiasClamp                              = 0.0f;
	float slopeScaledDepthBias                        = 0.0f;
	bool depthClipEnable                              = true;
	bool multisampleEnable                            = false;
	bool antialisedLineEnable                         = false;
	uint32 forcedSampleCount                          = 0;
	EConservativeRasterizationMode conservativeRaster = EConservativeRasterizationMode::Off;

	// For fullscreen triangle pass
	static RasterizerDesc FrontCull()
	{
		RasterizerDesc desc{};
		desc.cullMode = ECullMode::Front;
		return desc;
	}
};

// D3D12_BLEND
// VkBlendFactor
enum class EBlend : uint8
{
	Zero             = 1,
	One              = 2,
	SrcColor         = 3,
	InvSrcColor      = 4,
	SrcAlpha         = 5,
	InvSrcAlpha      = 6,
	DestAlpha        = 7,
	InvDestAlpha     = 8,
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
ENUM_CLASS_FLAGS(EColorWriteEnable);

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
	bool alphaToCoverageEnable  = false;
	bool independentBlendEnable = false;
	RenderTargetBlendDesc renderTarget[8];
};

enum class EDepthClearFlags : uint8
{
	DEPTH   = 0x1,
	STENCIL = 0x2,
	DEPTH_STENCIL = DEPTH | STENCIL
};
ENUM_CLASS_FLAGS(EDepthClearFlags);

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

	static DepthstencilDesc NoDepth()
	{
		return DepthstencilDesc{
			false,
			EDepthWriteMask::Zero,
			EComparisonFunc::Always
		};
	}
	static DepthstencilDesc StandardSceneDepth()
	{
		return DepthstencilDesc{
			true,
			EDepthWriteMask::All,
			EComparisonFunc::Less,
			false,
			0xff,
			0xff,
			{ EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always },
			{ EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always }
		};
	}
	static DepthstencilDesc ReverseZSceneDepth()
	{
		return DepthstencilDesc{
			true,
			EDepthWriteMask::All,
			EComparisonFunc::Greater,
			false,
			0xff,
			0xff,
			{ EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always },
			{ EStencilOp::Keep, EStencilOp::Keep, EStencilOp::Keep, EComparisonFunc::Always }
		};
	}
};

struct Viewport
{
	float topLeftX;
	float topLeftY;
	float width;
	float height;
	float minDepth;
	float maxDepth;
};

struct ScissorRect
{
	uint32 left;
	uint32 top;
	uint32 right;
	uint32 bottom;
};

//////////////////////////////////////////////////////////////////////////
// #todo-rhi: Moved from gpu_resource_binding.h

// D3D12_SHADER_VISIBILITY
enum class EShaderVisibility : uint8
{
	All      = 0, // Compute always use this; so does RT.
	Vertex   = 1,
	Hull     = 2,
	Domain   = 3,
	Geometry = 4,
	Pixel    = 5
	// #todo-rhi: EShaderVisibility - Amplication, Mesh
};

// D3D12_FILTER
enum class ETextureFilter : uint16
{
	MIN_MAG_MIP_POINT                          = 0,
	MIN_MAG_POINT_MIP_LINEAR                   = 0x1,
	MIN_POINT_MAG_LINEAR_MIP_POINT             = 0x4,
	MIN_POINT_MAG_MIP_LINEAR                   = 0x5,
	MIN_LINEAR_MAG_MIP_POINT                   = 0x10,
	MIN_LINEAR_MAG_POINT_MIP_LINEAR            = 0x11,
	MIN_MAG_LINEAR_MIP_POINT                   = 0x14,
	MIN_MAG_MIP_LINEAR                         = 0x15,
	ANISOTROPIC                                = 0x55,
	COMPARISON_MIN_MAG_MIP_POINT               = 0x80,
	COMPARISON_MIN_MAG_POINT_MIP_LINEAR        = 0x81,
	COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT  = 0x84,
	COMPARISON_MIN_POINT_MAG_MIP_LINEAR        = 0x85,
	COMPARISON_MIN_LINEAR_MAG_MIP_POINT        = 0x90,
	COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR = 0x91,
	COMPARISON_MIN_MAG_LINEAR_MIP_POINT        = 0x94,
	COMPARISON_MIN_MAG_MIP_LINEAR              = 0x95,
	COMPARISON_ANISOTROPIC                     = 0xd5,
	MINIMUM_MIN_MAG_MIP_POINT                  = 0x100,
	MINIMUM_MIN_MAG_POINT_MIP_LINEAR           = 0x101,
	MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT     = 0x104,
	MINIMUM_MIN_POINT_MAG_MIP_LINEAR           = 0x105,
	MINIMUM_MIN_LINEAR_MAG_MIP_POINT           = 0x110,
	MINIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR    = 0x111,
	MINIMUM_MIN_MAG_LINEAR_MIP_POINT           = 0x114,
	MINIMUM_MIN_MAG_MIP_LINEAR                 = 0x115,
	MINIMUM_ANISOTROPIC                        = 0x155,
	MAXIMUM_MIN_MAG_MIP_POINT                  = 0x180,
	MAXIMUM_MIN_MAG_POINT_MIP_LINEAR           = 0x181,
	MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT     = 0x184,
	MAXIMUM_MIN_POINT_MAG_MIP_LINEAR           = 0x185,
	MAXIMUM_MIN_LINEAR_MAG_MIP_POINT           = 0x190,
	MAXIMUM_MIN_LINEAR_MAG_POINT_MIP_LINEAR    = 0x191,
	MAXIMUM_MIN_MAG_LINEAR_MIP_POINT           = 0x194,
	MAXIMUM_MIN_MAG_MIP_LINEAR                 = 0x195,
	MAXIMUM_ANISOTROPIC                        = 0x1d5
};

// D3D12_TEXTURE_ADDRESS_MODE
enum class ETextureAddressMode : uint8
{
	Wrap       = 1,
	Mirror     = 2,
	Clamp      = 3,
	Border     = 4,
	MirrorOnce = 5
};

// D3D12_STATIC_BORDER_COLOR
enum EStaticBorderColor : uint8
{
	TransparentBlack = 0,
	OpaqueBlack      = 1,
	OpaqueWhite      = 2
};

// D3D12_STATIC_SAMPLER_DESC
struct StaticSamplerDesc
{
	std::string name;
	ETextureFilter filter              = ETextureFilter::MIN_MAG_MIP_POINT;
	ETextureAddressMode addressU       = ETextureAddressMode::Wrap;
	ETextureAddressMode addressV       = ETextureAddressMode::Wrap;
	ETextureAddressMode addressW       = ETextureAddressMode::Wrap;
	float mipLODBias                   = 0.0f;
	uint32 maxAnisotropy               = 1;
	EComparisonFunc comparisonFunc     = EComparisonFunc::Always;
	EStaticBorderColor borderColor     = EStaticBorderColor::TransparentBlack;
	float minLOD                       = 0.0f;
	float maxLOD                       = 0.0f;
	//uint32 shaderRegister              = 0; // Read from shader reflection
	//uint32 registerSpace               = 0; // Read from shader reflection
	EShaderVisibility shaderVisibility = EShaderVisibility::All;
};

//////////////////////////////////////////////////////////////////////////
// Graphics & compute pipeline

// D3D12_GRAPHICS_PIPELINE_STATE_DESC
// VkGraphicsPipelineCreateInfo
struct GraphicsPipelineDesc
{
	// Root signature is created internally in RHI backend.
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

	std::vector<StaticSamplerDesc> staticSamplers;
};

// D3D12_COMPUTE_PIPELINE_STATE_DESC
// VkComputePipelineCreateInfo
struct ComputePipelineDesc
{
	// Root signature is created internally in RHI backend.
	ShaderStage* cs = nullptr;
	uint32 nodeMask = 0; // #todo-mgpu
	// #todo-crossapi: D3D12_CACHED_PIPELINE_STATE CachedPSO;
	// #todo-crossapi: D3D12_PIPELINE_STATE_FLAGS  Flags;

	std::vector<StaticSamplerDesc> staticSamplers;
};

// ID3D12PipelineState
// VkPipeline
// NOTE: RTPSO is represented by RaytracingPipelineStateObject, not this.
class PipelineState
{
public:
	virtual ~PipelineState() = default;
};
class GraphicsPipelineState : public PipelineState {};
class ComputePipelineState : public PipelineState {};

//////////////////////////////////////////////////////////////////////////
// Raytracing pipeline

// D3D12_HIT_GROUP_TYPE
enum class ERaytracingHitGroupType
{
	Triangles,
	ProceduralPrimitive
};

struct RaytracingPipelineStateObjectDesc
{
	std::wstring hitGroupName;
	ERaytracingHitGroupType hitGroupType = ERaytracingHitGroupType::Triangles;

	ShaderStage* raygenShader = nullptr;
	ShaderStage* closestHitShader = nullptr;
	ShaderStage* missShader = nullptr;
	// #todo-dxr: anyHitShader, intersectionShader
	//ShaderStage* anyHitShader = nullptr;
	//ShaderStage* intersectionShader = nullptr;

	// https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#resource-binding
	// Local root signature  : Arguments come from individual shader tables
	// Global root signature : Arguments are shared across all raytracing shaders and compute PSOs on CommandLists
	// Only specify parameter names for local root signatures.
	// Global root signature is auto-generated by RaytracingPipelineStateObject.
	std::vector<std::string> raygenLocalParameters;
	std::vector<std::string> closestHitLocalParameters;
	std::vector<std::string> missLocalParameters;

	uint32 maxPayloadSizeInBytes = 0;
	uint32 maxAttributeSizeInBytes = 0;
	uint32 maxTraceRecursionDepth = 1;

	std::vector<StaticSamplerDesc> staticSamplers;
};

// ID3D12StateObject (RTPSO)
class RaytracingPipelineStateObject
{
public:
	virtual ~RaytracingPipelineStateObject() = default;
};

// - Describes the arguments for a local root signature.
// - For now, no struct for shader record.
//   ( shader record = {shader identifier, local root arguments for the shader} )
class RaytracingShaderTable
{
public:
	virtual ~RaytracingShaderTable() = default;

	virtual void uploadRecord(
		uint32 recordIndex,
		ShaderStage* raytracingShader,
		void* rootArgumentData,
		uint32 rootArgumentSize) = 0;

	virtual void uploadRecord(
		uint32 recordIndex,
		const wchar_t* shaderExportName,
		void* rootArgumentData,
		uint32 rootArgumentSize) = 0;
};

// D3D12_DISPATCH_RAYS_DESC
struct DispatchRaysDesc
{
	RaytracingShaderTable* raygenShaderTable = nullptr;
	RaytracingShaderTable* missShaderTable = nullptr;
	RaytracingShaderTable* hitGroupTable = nullptr;
	//ShaderTable* callableShaderTable = nullptr; // #todo-dxr: callableShaderTable

	uint32 width;
	uint32 height;
	uint32 depth;
};

// ------------------------------------------------------------------------
// Indirect draw

// D3D12_INDIRECT_ARGUMENT_TYPE
enum class EIndirectArgumentType : uint32
{
	DRAW = 0,
	DRAW_INDEXED,
	DISPATCH,
	VERTEX_BUFFER_VIEW,
	INDEX_BUFFER_VIEW,
	CONSTANT,
	CONSTANT_BUFFER_VIEW,
	SHADER_RESOURCE_VIEW,
	UNORDERED_ACCESS_VIEW,
	DISPATCH_RAYS,
	DISPATCH_MESH
};

// D3D12_INDIRECT_ARGUMENT_DESC
struct IndirectArgumentDesc
{
	EIndirectArgumentType type;
	std::string name;
	// NOTE: This union is not used for types unspecified below.
	union
	{
		struct
		{
			uint32 slot;
		} vertexBuffer; // EIndirectArgumentType::VERTEX_BUFFER_VIEW
		struct
		{
			// 'name' is a common field.
			uint32 destOffsetIn32BitValues;
			uint32 num32BitValuesToSet;
		} constant; // EIndirectArgumentType::CONSTANT
		struct
		{
			// 'name' is a common field.
		} constantBufferView; // EIndirectArgumentType::CONSTANT_BUFFER_VIEW
		struct
		{
			// 'name' is a common field.
		} shaderResourceView; // EIndirectArgumentType::SHADER_RESOURCE_VIEW
		struct
		{
			// 'name' is a common field.
		} unorderedAccessView; // EIndirectArgumentType::UNORDERED_ACCESS_VIEW
	};
};

// D3D12_COMMAND_SIGNATURE_DESC
struct CommandSignatureDesc
{
	//uint32 byteStride; // RHI should calculate this...
	std::vector<IndirectArgumentDesc> argumentDescs;
	uint32 nodeMask;
};

// ID3D12RootSignature
class CommandSignature
{
public:
	virtual ~CommandSignature() = default;
};

// RHI-agnostic interface to fill indirect commands.
// This is just a memory writer and not a GPU resource,
// but requires different implementations for different backends.
class IndirectCommandGenerator
{
public:
	virtual ~IndirectCommandGenerator() = default;

	virtual void initialize(const CommandSignatureDesc& desc, uint32 maxCommandCount) = 0;

	virtual void resizeMaxCommandCount(uint32 newMaxCount) = 0;

	virtual void beginCommand(uint32 commandIx) = 0;

	virtual void writeConstant32(uint32 constant) = 0;
	virtual void writeVertexBufferView(VertexBuffer* vbuffer) = 0;
	virtual void writeIndexBufferView(IndexBuffer* ibuffer) = 0;
	virtual void writeDrawArguments(
		uint32 vertexCountPerInstance,
		uint32 instanceCount,
		uint32 startVertexLocation,
		uint32 startInstanceLocation) = 0;
	virtual void writeDrawIndexedArguments(
		uint32 indexCountPerInstance,
		uint32 instanceCount,
		uint32 startIndexLocation,
		int32  baseVertexLocation,
		uint32 startInstanceLocation) = 0;
	virtual void writeDispatchArguments(
		uint32 threadGroupCountX,
		uint32 threadGroupCountY,
		uint32 threadGroupCountZ) = 0;
	virtual void writeConstantBufferView(ConstantBufferView* view) = 0;
	virtual void writeShaderResourceView(ShaderResourceView* view) = 0;
	virtual void writeUnorderedAccessView(UnorderedAccessView* view) = 0;
	// #todo-indirect-draw: What should I write for dispatchRays()? D3D12_DISPATCH_RAYS_DESC?
	//virtual void writeDispatchRaysArguments(...) = 0;
	virtual void writeDispatchMeshArguments(
		uint32 threadGroupCountX,
		uint32 threadGroupCountY,
		uint32 threadGroupCountZ) = 0;

	virtual void endCommand() = 0;

	virtual uint32 getMaxCommandCount() const = 0;
	virtual uint32 getCommandByteStride() const = 0;
	virtual void copyToBuffer(RenderCommandList* commandList, uint32 numCommands, Buffer* destBuffer, uint64 destOffset) = 0;
};
