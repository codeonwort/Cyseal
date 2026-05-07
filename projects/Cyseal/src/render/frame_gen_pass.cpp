#include "frame_gen_pass.h"
#include "render/util/clear_resource_pass.h"
#include "render/renderer_constants.h"
#include "rhi/rhi_policy.h"
#include "rhi/render_device.h"
#include "rhi/render_command.h"
#include "world/camera.h"

// doc: docs/techniques/frame-interpolation.md
// src: sdk/src/components/frameinterpolation/ffx_frameinterpolation.cpp

// Input: 2 back buffers + several resources shared with FSR3Upscaler and FfxOpticalFlow
// Output: An interpolated image between the 2 back buffers

// FFX_DECLARE_CB(FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION)
struct FrameInterpUniform
{
	int32           renderSize[2];
	int32           displaySize[2];

	float           displaySizeRcp[2];
	float           cameraNear;
	float           cameraFar;

	int32           upscalerTargetSize[2];
	int32           Mode;
	int32           reset;

	float           fDeviceToViewDepth[4]; // See setupDeviceDepthToViewSpaceDepthParams

	float           deltaTime; // In milliseconds
	int32           HUDLessAttachedFactor; // 1 or 0 (#wip: Is it for HUD upscaling?)
	int32           distortionFieldSize[2];

	float           opticalFlowScale[2];
	int32           opticalFlowBlockSize;
	uint32          dispatchFlags; // #wip: FfxFrameInterpolationDispatchFlags (FFX_FRAMEINTERPOLATION_DISPATCH_DRAW_DEBUG_TEAR_LINES, FFX_FRAMEINTERPOLATION_DISPATCH_DRAW_DEBUG_VIEW)

	int32           maxRenderSize[2];
	int32           opticalFlowHalfResMode;
	int32           NumInstances;

	int32           interpolationRectBase[2];
	int32           interpolationRectSize[2];

	float           debugBarColor[3];
	uint32          backBufferTransferFunction; // Transfer fn to convert interpolation source color data to linear RGB

	float           minMaxLuminance[2]; // Used when converting HDR colors to linear RGB
	float           fTanHalfFOV;
	int32           _pad1;

	float           fJitter[2];
	float           fMotionVectorScale[2];
};

// FFX_DECLARE_CB(FFX_FRAMEINTERPOLATION_BIND_CB_INPAINTING_PYRAMID)
struct InpaintingPyramidUniform
{
	uint32          mips;
	uint32          numWorkGroups;
	uint32          workGroupOffset[2];
};

// Ported from ffx_frameinterpolation.cpp
static void setupDeviceDepthToViewSpaceDepthParams(const Camera* camera, float viewSpaceToMetersFactor, float outDeviceToViewDepth[4])
{
	const bool bInverted = getReverseZPolicy() == EReverseZPolicy::Reverse;
	// #wip: reverse-z with non-infinite depth range?
	const bool bInfinite = bInverted; //(context->contextDescription.flags & FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE) == FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE;

	// make sure it has no impact if near and far plane values are swapped in dispatch params
	// the flags "inverted" and "infinite" will decide what transform to use
	float fMin = std::min(camera->getZNear(), camera->getZFar());
	float fMax = std::max(camera->getZNear(), camera->getZFar());

	if (bInverted)
	{
		float tmp = fMin;
		fMin = fMax;
		fMax = tmp;
	}

	// a 0 0 0   x
	// 0 b 0 0   y
	// 0 0 c d   z
	// 0 0 e 0   1

	const float fQ = fMax / (fMin - fMax);
	const float d = -1.0f; // for clarity

	const float matrix_elem_c[2][2] = {
		fQ,                     // non reversed, non infinite
		-1.0f - FLT_EPSILON,    // non reversed, infinite
		fQ,                     // reversed, non infinite
		0.0f + FLT_EPSILON      // reversed, infinite
	};

	const float matrix_elem_e[2][2] = {
		fQ * fMin,              // non reversed, non infinite
		-fMin - FLT_EPSILON,    // non reversed, infinite
		fQ * fMin,              // reversed, non infinite
		fMax,                   // reversed, infinite
	};

	outDeviceToViewDepth[0] = d * matrix_elem_c[bInverted][bInfinite];
	outDeviceToViewDepth[1] = matrix_elem_e[bInverted][bInfinite] * viewSpaceToMetersFactor;

	// revert x and y coords
	const float aspect = camera->getAspectRatio();
	const float cotHalfFovY = cosf(0.5f * camera->getFovYInRadians()) / sinf(0.5f * camera->getFovYInRadians());
	const float a = cotHalfFovY / aspect;
	const float b = cotHalfFovY;

	outDeviceToViewDepth[2] = (1.0f / a);
	outDeviceToViewDepth[3] = (1.0f / b);
}

void FrameGenPass::initialize(RenderDevice* inRenderDevice)
{
	device = inRenderDevice;
	cpuFrameIndex = 0;

	const uint32 swapchainCount = 2;
	reconstructedPrevDepthTextures.initialize(swapchainCount);
	reconstructedPrevDepthSRVs.initialize(swapchainCount);
	reconstructedPrevDepthUAVs.initialize(swapchainCount);
	dilatedMotionVectorTextures.initialize(swapchainCount);
	dilatedMotionVectorSRVs.initialize(swapchainCount);
	dilatedMotionVectorUAVs.initialize(swapchainCount);
	dilatedDepthTextures.initialize(swapchainCount);
	dilatedDepthSRVs.initialize(swapchainCount);
	dilatedDepthUAVs.initialize(swapchainCount);

	initializePipelines();
}

void FrameGenPass::runFrameGeneration(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	recreateResources(commandList, passInput);

	updateUniforms(commandList, passInput);
	preparePhase(commandList, swapchainIndex, passInput);
	dispatchPhase(commandList, swapchainIndex, passInput);

	cpuFrameIndex += 1;
}

void FrameGenPass::initializePipelines()
{
	const uint32 swapchainCount = 2;//device->maxFramesInFlight(); // Always need 2

	prepareDescriptor.initialize(device, L"FSR3_Prepare", swapchainCount, 0);
	frameInterpDescriptor.initialize(device, L"FSR3_FrameInterp", swapchainCount, sizeof(FrameInterpUniform));
	inpaintingPyramidDescriptor.initialize(device, L"FSR3_InpaintingPyramid", swapchainCount, sizeof(InpaintingPyramidUniform));

	reconstructPrevDepthDescriptor.initialize(device, L"FSR3_ReconstructPrevDepth", swapchainCount, 0);
	gameMotionVectorFieldDescriptor.initialize(device, L"FSR3_GameMotionVectorField", swapchainCount, 0);
	gameMotionVectorFieldInpaintingPyramidDescriptor.initialize(device, L"FSR3_GameMotionVectorFieldInpaintingPyramid", swapchainCount, 0);
	opticalFlowVectorFieldDescriptor.initialize(device, L"FSR3_OpticalFlowVectorField", swapchainCount, 0);
	disocclusionMaskDescriptor.initialize(device, L"FSR3_DisocclusionMask", swapchainCount, 0);
	interpolationDescriptor.initialize(device, L"FSR3_Interpolation", swapchainCount, 0);
	inpaintingDescriptor.initialize(device, L"FSR3_Inpainting", swapchainCount, 0);

	auto createPipeline = [device = this->device]
		(const char* debugName, const wchar_t* filepath, UniquePtr<ComputePipelineState>& pipeline)
	{
		std::vector<std::wstring> defines = { L"FFX_GPU", L"FFX_HLSL", L"FFX_HALF" };
		if (getReverseZPolicy() == EReverseZPolicy::Reverse)
		{
			// Not all FSR3 shaders require it, but no harm also.
			defines.push_back(L"FFX_FRAMEINTERPOLATION_OPTION_INVERTED_DEPTH");
		}

		ShaderStage* shader = device->createShader(EShaderStage::COMPUTE_SHADER, debugName);
		shader->declarePushConstants();
		shader->loadFromFile(filepath, "CS", defines);

		pipeline = UniquePtr<ComputePipelineState>(device->createComputePipelineState(
			ComputePipelineDesc{ .cs = shader, .nodeMask = 0 }
		));

		delete shader;
	};

	createPipeline("FSR3ReconstructAndDilateCS", L"amd/ffx_frameinterpolation_reconstruct_and_dilate_pass.hlsl", reconstructAndDilatePipeline);
	createPipeline("FSR3SetupCS", L"amd/ffx_frameinterpolation_setup_pass.hlsl", setupPipeline);
	createPipeline("FSR3ReconstructPrevDepthCS", L"amd/ffx_frameinterpolation_reconstruct_previous_depth_pass.hlsl", reconstructPrevDepthPipeline);
	createPipeline("FSR3GameMotionVectorFieldPipelineCS", L"amd/ffx_frameinterpolation_game_motion_vector_field_pass.hlsl", gameMotionVectorFieldPipeline);
	createPipeline("FSR3OpticalFlowVectorFieldPipelineCS", L"amd/ffx_frameinterpolation_optical_flow_vector_field_pass.hlsl", opticalFlowVectorFieldPipeline);
	createPipeline("FSR3DisocclusionMaskPipelineCS", L"amd/ffx_frameinterpolation_disocclusion_mask_pass.hlsl", disocclusionMaskPipeline);
	createPipeline("FSR3InterpolationPipelineCS", L"amd/ffx_frameinterpolation_pass.hlsl", interpolationPipeline);
	createPipeline("FSR3InpaintingPyramidPipelineCS", L"amd/ffx_frameinterpolation_compute_inpainting_pyramid_pass.hlsl", inpaintingPyramidPipeline);
	createPipeline("FSR3InpaintingPipelineCS", L"amd/ffx_frameinterpolation_inpainting_pass.hlsl", inpaintingPipeline);
	createPipeline("FSR3GameVectorFieldInpaintingPyramidPipelineCS", L"amd/ffx_frameinterpolation_compute_game_vector_field_inpainting_pyramid_pass.hlsl", gameVectorFieldInpaintingPyramidPipeline);
	createPipeline("FSR3DebugViewPipelineCS", L"amd/ffx_frameinterpolation_debug_view_pass.hlsl", debugViewPipeline);
}

void FrameGenPass::recreateResources(RenderCommandList* commandList, const FrameGenPassInput& passInput)
{
	auto nullOrWrongSize = [](const UniquePtr<Texture>& texture, uint32 width, uint32 height) -> bool {
		return texture == nullptr
			|| texture->getCreateParams().width != width
			|| texture->getCreateParams().height != height;
	};

	for (size_t i = 0; i < reconstructedPrevDepthTextures.size(); ++i)
	{
		if (nullOrWrongSize(reconstructedPrevDepthTextures[i], passInput.displaySizeX, passInput.displaySizeY))
		{
			commandList->enqueueDeferredDealloc(reconstructedPrevDepthTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(reconstructedPrevDepthSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(reconstructedPrevDepthUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R32_FLOAT,
				ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			reconstructedPrevDepthTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_FSR3_ReconstructedPrevDepth_%u", (uint32)i);
			reconstructedPrevDepthTextures[i]->setDebugName(debugName);

			reconstructedPrevDepthSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				reconstructedPrevDepthTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = reconstructedPrevDepthTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			reconstructedPrevDepthUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				reconstructedPrevDepthTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = reconstructedPrevDepthTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	if (nullOrWrongSize(reconstructedDepthInterpolatedFrameTexture, passInput.displaySizeX, passInput.displaySizeY))
	{
		commandList->enqueueDeferredDealloc(reconstructedDepthInterpolatedFrameTexture.release(), true);
		commandList->enqueueDeferredDealloc(reconstructedDepthInterpolatedFrameSRV.release(), true);
		commandList->enqueueDeferredDealloc(reconstructedDepthInterpolatedFrameUAV.release(), true);

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			EPixelFormat::R32_UINT,
			ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			passInput.displaySizeX, passInput.displaySizeY);
		reconstructedDepthInterpolatedFrameTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		reconstructedDepthInterpolatedFrameTexture->setDebugName(L"RT_FSR3_ReconstructedDepthInterpolatedFrame");

		reconstructedDepthInterpolatedFrameSRV = UniquePtr<ShaderResourceView>(device->createSRV(
			reconstructedDepthInterpolatedFrameTexture.get(),
			ShaderResourceViewDesc{
				.format              = reconstructedDepthInterpolatedFrameTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
		reconstructedDepthInterpolatedFrameUAV = UniquePtr<UnorderedAccessView>(device->createUAV(
			reconstructedDepthInterpolatedFrameTexture.get(),
			UnorderedAccessViewDesc{
				.format         = reconstructedDepthInterpolatedFrameTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
	}

	for (size_t i = 0; i < dilatedMotionVectorTextures.size(); ++i)
	{
		if (nullOrWrongSize(dilatedMotionVectorTextures[i], passInput.displaySizeX, passInput.displaySizeY))
		{
			commandList->enqueueDeferredDealloc(dilatedMotionVectorTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedMotionVectorSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedMotionVectorUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R16G16_FLOAT,
				// #wip: (FFX_RESOURCE_USAGE_RENDERTARGET | FFX_RESOURCE_USAGE_UAV | FFX_RESOURCE_USAGE_DCC_RENDERTARGET) ???
				ETextureAccessFlags::RTV | ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			dilatedMotionVectorTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_FSR3_DilatedMotionVector_%u", (uint32)i);
			dilatedMotionVectorTextures[i]->setDebugName(debugName);
			
			dilatedMotionVectorSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				dilatedMotionVectorTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = dilatedMotionVectorTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			dilatedMotionVectorUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				dilatedMotionVectorTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = dilatedMotionVectorTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	for (size_t i = 0; i < dilatedDepthTextures.size(); ++i)
	{
		if (nullOrWrongSize(dilatedDepthTextures[i], passInput.displaySizeX, passInput.displaySizeY))
		{
			commandList->enqueueDeferredDealloc(dilatedDepthTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedDepthSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(dilatedDepthUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R32_FLOAT,
				// #wip: (FFX_RESOURCE_USAGE_RENDERTARGET | FFX_RESOURCE_USAGE_UAV | FFX_RESOURCE_USAGE_DCC_RENDERTARGET) ???
				ETextureAccessFlags::RTV | ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			dilatedDepthTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_FSR3_DilatedDepth_%u", (uint32)i);
			dilatedDepthTextures[i]->setDebugName(debugName);
			
			dilatedDepthSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				dilatedDepthTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = dilatedDepthTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			dilatedDepthUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				dilatedDepthTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = dilatedDepthTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	for (uint32 i = 0; i < 2; ++i)
	{
		if (nullOrWrongSize(gameMotionVectorFieldTextures[i], passInput.displaySizeX, passInput.displaySizeY))
		{
			commandList->enqueueDeferredDealloc(gameMotionVectorFieldTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(gameMotionVectorFieldSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(gameMotionVectorFieldUAVs[i].release(), true);

			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R32_UINT,
				ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			gameMotionVectorFieldTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_FSR3_GameMotionVectorField_%s", i == 0 ? L"X" : L"Y");
			gameMotionVectorFieldTextures[i]->setDebugName(debugName);

			gameMotionVectorFieldSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				gameMotionVectorFieldTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = gameMotionVectorFieldTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			gameMotionVectorFieldUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				gameMotionVectorFieldTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = gameMotionVectorFieldTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	for (uint32 i = 0; i < 2; ++i)
	{
		if (nullOrWrongSize(opticalFlowMotionVectorFieldTextures[i], passInput.displaySizeX, passInput.displaySizeY))
		{
			commandList->enqueueDeferredDealloc(opticalFlowMotionVectorFieldTextures[i].release(), true);
			commandList->enqueueDeferredDealloc(opticalFlowMotionVectorFieldSRVs[i].release(), true);
			commandList->enqueueDeferredDealloc(opticalFlowMotionVectorFieldUAVs[i].release(), true);

			// #wip: Too big? (xy + 7) / 8 is enough?
			TextureCreateParams texDesc = TextureCreateParams::texture2D(
				EPixelFormat::R32_UINT,
				ETextureAccessFlags::UAV,
				passInput.displaySizeX, passInput.displaySizeY);
			opticalFlowMotionVectorFieldTextures[i] = UniquePtr<Texture>(device->createTexture(texDesc));

			wchar_t debugName[128];
			std::swprintf(debugName, _countof(debugName), L"RT_FSR3_OpticalFlowMotionVectorField_%s", i == 0 ? L"X" : L"Y");
			opticalFlowMotionVectorFieldTextures[i]->setDebugName(debugName);

			opticalFlowMotionVectorFieldSRVs[i] = UniquePtr<ShaderResourceView>(device->createSRV(
				opticalFlowMotionVectorFieldTextures[i].get(),
				ShaderResourceViewDesc{
					.format              = opticalFlowMotionVectorFieldTextures[i]->getCreateParams().format,
					.viewDimension       = ESRVDimension::Texture2D,
					.texture2D           = Texture2DSRVDesc{
						.mostDetailedMip = 0,
						.mipLevels       = 1,
						.planeSlice      = 0,
						.minLODClamp     = 0.0f,
					},
				}
			));
			opticalFlowMotionVectorFieldUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				opticalFlowMotionVectorFieldTextures[i].get(),
				UnorderedAccessViewDesc{
					.format         = opticalFlowMotionVectorFieldTextures[i]->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
				}
			));
		}
	}

	if (nullOrWrongSize(disocclusionMaskTexture, passInput.displaySizeX, passInput.displaySizeY))
	{
		commandList->enqueueDeferredDealloc(disocclusionMaskTexture.release(), true);
		commandList->enqueueDeferredDealloc(disocclusionMaskSRV.release(), true);
		commandList->enqueueDeferredDealloc(disocclusionMaskUAV.release(), true);

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			EPixelFormat::R8G8_UNORM,
			ETextureAccessFlags::UAV,
			passInput.displaySizeX, passInput.displaySizeY);
		disocclusionMaskTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		disocclusionMaskTexture->setDebugName(L"RT_FSR3_DisocclusionMask");

		disocclusionMaskSRV = UniquePtr<ShaderResourceView>(device->createSRV(
			disocclusionMaskTexture.get(),
			ShaderResourceViewDesc{
				.format              = disocclusionMaskTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
		disocclusionMaskUAV = UniquePtr<UnorderedAccessView>(device->createUAV(
			disocclusionMaskTexture.get(),
			UnorderedAccessViewDesc{
				.format         = disocclusionMaskTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
	}

	// 2 uints. See COUNTER_SPD and COUNTER_FRAME_INDEX_SINCE_LAST_RESET.
	if (counterBuffer == nullptr)
	{
		counterBuffer = UniquePtr<Buffer>(device->createBuffer(
			BufferCreateParams{
				.sizeInBytes = 2 * sizeof(uint32),
				.alignment   = 0,
				.accessFlags = EBufferAccessFlags::UAV,
			}
		));
		counterBuffer->setDebugName(L"Buffer_FSR3_Counter");

		counterSRV = UniquePtr<ShaderResourceView>(device->createSRV(counterBuffer.get(),
			ShaderResourceViewDesc{
				.format        = EPixelFormat::UNKNOWN,
				.viewDimension = ESRVDimension::Buffer,
				.buffer        = BufferSRVDesc{
					.firstElement        = 0,
					.numElements         = 2,
					.structureByteStride = sizeof(uint32),
					.flags               = EBufferSRVFlags::None,
				}
			}
		));
		counterUAV = UniquePtr<UnorderedAccessView>(device->createUAV(counterBuffer.get(),
			UnorderedAccessViewDesc{
				.format        = EPixelFormat::UNKNOWN,
				.viewDimension = EUAVDimension::Buffer,
				.buffer        = BufferUAVDesc{
					.firstElement         = 0,
					.numElements          = 2,
					.structureByteStride  = sizeof(uint32),
					.counterOffsetInBytes = 0,
					.flags                = EBufferUAVFlags::None,
				}
			}
		));
	}

	if (defaultDistortionFieldTexture == nullptr)
	{
		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			EPixelFormat::R8G8_UNORM, ETextureAccessFlags::SRV, 1, 1);
		texDesc.setOptimalClearColor(0, 0, 0, 0);
		defaultDistortionFieldTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		defaultDistortionFieldTexture->setDebugName(L"RT_FSR3_DefaultDistortionField");
		
		defaultDistortionFieldSRV = UniquePtr<ShaderResourceView>(device->createSRV(
			defaultDistortionFieldTexture.get(),
			ShaderResourceViewDesc{
				.format              = defaultDistortionFieldTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
	}

	if (nullOrWrongSize(prevInterpolationSourceTexture, passInput.displaySizeX, passInput.displaySizeY))
	{
		commandList->enqueueDeferredDealloc(prevInterpolationSourceTexture.release(), true);
		commandList->enqueueDeferredDealloc(prevInterpolationSourceSRV.release(), true);
		commandList->enqueueDeferredDealloc(prevInterpolationSourceUAV.release(), true);

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			PF_sceneColor,
			ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			passInput.displaySizeX, passInput.displaySizeY);
		prevInterpolationSourceTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		prevInterpolationSourceTexture->setDebugName(L"RT_FSR3_PrevInterpolationSource");
		
		prevInterpolationSourceSRV = UniquePtr<ShaderResourceView>(device->createSRV(
			prevInterpolationSourceTexture.get(),
			ShaderResourceViewDesc{
				.format              = prevInterpolationSourceTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
		prevInterpolationSourceUAV = UniquePtr<UnorderedAccessView>(device->createUAV(
			prevInterpolationSourceTexture.get(),
			UnorderedAccessViewDesc{
				.format         = prevInterpolationSourceTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
	}

	if (nullOrWrongSize(inpaintingPyramidTexture, passInput.displaySizeX, passInput.displaySizeY))
	{
		commandList->enqueueDeferredDealloc(inpaintingPyramidTexture.release(), true);
		for (size_t i = 0; i < _countof(inpaintingPyramidUAVs); ++i)
		{
			commandList->enqueueDeferredDealloc(inpaintingPyramidUAVs[i].release(), true);
		}

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			EPixelFormat::R16G16B16A16_FLOAT,
			ETextureAccessFlags::UAV,
			// #wip: What if inpaintingPyramid is not large enough to contain 13 mips?
			passInput.displaySizeX / 2, passInput.displaySizeY / 2, _countof(inpaintingPyramidUAVs));

		inpaintingPyramidTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		inpaintingPyramidTexture->setDebugName(L"RT_FSR3_InpaintingPyramidTexture");

		inpaintingPyramidSRV = UniquePtr<ShaderResourceView>(device->createSRV(
			inpaintingPyramidTexture.get(),
			ShaderResourceViewDesc{
				.format              = inpaintingPyramidTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = inpaintingPyramidTexture->getCreateParams().mipLevels,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
		
		for (size_t i = 0; i < _countof(inpaintingPyramidUAVs); ++i)
		{
			inpaintingPyramidUAVs[i] = UniquePtr<UnorderedAccessView>(device->createUAV(
				inpaintingPyramidTexture.get(),
				UnorderedAccessViewDesc{
					.format         = inpaintingPyramidTexture->getCreateParams().format,
					.viewDimension  = EUAVDimension::Texture2D,
					.texture2D      = Texture2DUAVDesc{ .mipSlice = (uint32)i, .planeSlice = 0 },
				}
			));
		}
	}

	// #wip: I don't know what this is. FidelityFX does not fill this texture.
	// The way this resource is used makes no sense.
	// It's declared as uint2, but LoadOpticalFlowConfidence() reads its y channel and return as float.
	// Then fConfidenceFactor = ffxMax(FFX_FRAMEINTERPOLATION_EPSILON, LoadOpticalFlowConfidence(samplePos)).
	// Finally, fConfidenceFactor is just not used at all :(
	// Let's just assume it's dead code and bind a black texture.
	if (nullOrWrongSize(opticalFlowConfidenceTexture, passInput.displaySizeX, passInput.displaySizeY))
	{
		commandList->enqueueDeferredDealloc(opticalFlowConfidenceTexture.release(), true);
		commandList->enqueueDeferredDealloc(opticalFlowConfidenceSRV.release(), true);

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			EPixelFormat::R16G16_UINT,
			ETextureAccessFlags::SRV,
			passInput.displaySizeX, passInput.displaySizeY);
		texDesc.setOptimalClearColor(0, 0, 0, 0);

		opticalFlowConfidenceTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		opticalFlowConfidenceTexture->setDebugName(L"RT_FSR3_OpticalFlowConfidence");

		opticalFlowConfidenceSRV = UniquePtr<ShaderResourceView>(device->createSRV(
			opticalFlowConfidenceTexture.get(),
			ShaderResourceViewDesc{
				.format              = opticalFlowConfidenceTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
	}

	if (nullOrWrongSize(interpolationOutputTexture, passInput.displaySizeX, passInput.displaySizeY))
	{
		commandList->enqueueDeferredDealloc(interpolationOutputTexture.release(), true);
		commandList->enqueueDeferredDealloc(interpolationOutputSRV.release(), true);
		commandList->enqueueDeferredDealloc(interpolationOutputUAV.release(), true);

		TextureCreateParams texDesc = TextureCreateParams::texture2D(
			PF_sceneColor,
			ETextureAccessFlags::SRV | ETextureAccessFlags::UAV,
			passInput.displaySizeX, passInput.displaySizeY);
		interpolationOutputTexture = UniquePtr<Texture>(device->createTexture(texDesc));
		interpolationOutputTexture->setDebugName(L"RT_FSR3_InterpolationOutput");
		
		interpolationOutputSRV = UniquePtr<ShaderResourceView>(device->createSRV(
			interpolationOutputTexture.get(),
			ShaderResourceViewDesc{
				.format              = interpolationOutputTexture->getCreateParams().format,
				.viewDimension       = ESRVDimension::Texture2D,
				.texture2D           = Texture2DSRVDesc{
					.mostDetailedMip = 0,
					.mipLevels       = 1,
					.planeSlice      = 0,
					.minLODClamp     = 0.0f,
				},
			}
		));
		interpolationOutputUAV = UniquePtr<UnorderedAccessView>(device->createUAV(
			interpolationOutputTexture.get(),
			UnorderedAccessViewDesc{
				.format         = interpolationOutputTexture->getCreateParams().format,
				.viewDimension  = EUAVDimension::Texture2D,
				.texture2D      = Texture2DUAVDesc{ .mipSlice = 0, .planeSlice = 0 },
			}
		));
	}
}

void FrameGenPass::updateUniforms(RenderCommandList* commandList, const FrameGenPassInput& passInput)
{
	ConstantBufferView* frameInterpUniformCBV = getCurrentFrameInterpUniformCBV();
	ConstantBufferView* inpaintingPyramidUniformCBV = getCurrentInpaintingPyramidUniformCBV();

	FrameInterpUniform fiUniformData{
		.renderSize                 = { passInput.renderSizeX, passInput.renderSizeY },
		.displaySize                = { passInput.displaySizeX, passInput.displaySizeY },
		.displaySizeRcp             = { 1.0f / (float)(passInput.displaySizeX), 1.0f / (float)(passInput.displaySizeY) },
		.cameraNear                 = passInput.camera->getZNear(),
		.cameraFar                  = passInput.camera->getZFar(),
		.upscalerTargetSize         = { passInput.renderSizeX, passInput.renderSizeY }, // #wip: upscale target size
		.Mode                       = 0, // #wip: What is this? No shader accesses it, even ffx source code does not use it.
		.reset                      = passInput.bReset,
		.fDeviceToViewDepth         = { 0, 0, 0, 0 }, // Set below
		.deltaTime                  = passInput.deltaTime, // #wip: Unit of deltaTime?
		.HUDLessAttachedFactor      = 0,
		.distortionFieldSize        = { 1, 1 },
		.opticalFlowScale           = { 1.0f / (float)(passInput.displaySizeX), 1.0f / (float)(passInput.displaySizeY) },
		.opticalFlowBlockSize       = kOpticalFlowBlockSize,
		.dispatchFlags              = passInput.dispatchFlags,
		.maxRenderSize              = { passInput.displaySizeX, passInput.displaySizeY },
		.opticalFlowHalfResMode     = 0, // #wip: opticalFlowHalfResMode
		.NumInstances               = 0, // #wip: NumInstances unused?
		.interpolationRectBase      = { 0, 0 },
		.interpolationRectSize      = { passInput.renderSizeX, passInput.renderSizeY },
		.debugBarColor              = { 1.0f, 0.0f, 0.0f },
		.backBufferTransferFunction = (uint32)passInput.backBufferTransferFunction,
		.minMaxLuminance            = { 0.0f, 65000.0f }, // #wip: minMaxLuminance
		.fTanHalfFOV                = 0.5f * std::tan(2.0f * std::atan(std::tan(passInput.camera->getFovYInRadians() * 0.5f) * passInput.camera->getAspectRatio())),
		._pad1                      = 0,
		.fJitter                    = { 0, 0 }, // #wip: jitter
		.fMotionVectorScale         = { 1.0f, 1.0f },
	};
	setupDeviceDepthToViewSpaceDepthParams(passInput.camera, 1.0f, fiUniformData.fDeviceToViewDepth);

	frameInterpUniformCBV->writeToGPU(commandList, &fiUniformData, sizeof(fiUniformData));

	uint32 dispatchThreadGroupCountXY[2];
	uint32 workGroupOffset[2];
	uint32 numWorkGroupsAndMips[2];
	uint32 rectInfo[4] = { 0, 0, (uint32)passInput.renderSizeX, (uint32)passInput.renderSizeY };
	ffxSpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo, -1);

	InpaintingPyramidUniform ipUniformData{
		.mips            = numWorkGroupsAndMips[1],
		.numWorkGroups   = numWorkGroupsAndMips[0],
		.workGroupOffset = { workGroupOffset[0], workGroupOffset[1] },
	};
	inpaintingPyramidUniformCBV->writeToGPU(commandList, &ipUniformData, sizeof(ipUniformData));
}

// See ffxFrameInterpolationPrepare().
// Generate FSR3 upscaler resources. If upscaler was used, then this method is not needed.
void FrameGenPass::preparePhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	SCOPED_DRAW_EVENT(commandList, FrameGenPrepare);

	const uint32 currResourceIndex = (cpuFrameIndex & 1);
	const uint32 prevResourceIndex = ((cpuFrameIndex + 1) & 1);

	auto uniformCBV = getCurrentFrameInterpUniformCBV();
	auto reconstructedPrevDepthTexture = reconstructedPrevDepthTextures[currResourceIndex].get();
	auto reconstructedPrevDepthUAV = reconstructedPrevDepthUAVs[currResourceIndex].get();
	auto dilatedMotionVectorTexture = dilatedMotionVectorTextures[currResourceIndex].get();
	auto dilatedMotionVectorUAV = dilatedMotionVectorUAVs[currResourceIndex].get();
	auto dilatedDepthTexture = dilatedDepthTextures[currResourceIndex].get();
	auto dilatedDepthUAV = dilatedDepthUAVs[currResourceIndex].get();

	// Clear estimated depth resources.
	auto fClearZero = ClearResourcePass::floatClearValue(getDeviceFarDepth(), 0, 0, 0);
	passInput.clearResourcePass->enqueueClear(reconstructedPrevDepthTexture, reconstructedPrevDepthUAV, fClearZero);
	passInput.clearResourcePass->executeClears(commandList, swapchainIndex);

	TextureBarrierAuto textureBarriers[] = {
		TextureBarrierAuto::toShaderResource(passInput.motionVectorTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toShaderResource(passInput.sceneDepthTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(reconstructedPrevDepthTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(dilatedMotionVectorTexture, EBarrierSync::COMPUTE_SHADING),
		TextureBarrierAuto::toUnorderedAccess(dilatedDepthTexture, EBarrierSync::COMPUTE_SHADING),
	};
	commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

	ShaderParameterTable SPT{};
	SPT.constantBuffer("cbFI", uniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
	SPT.texture("r_input_motion_vectors", passInput.motionVectorSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_INPUT_MOTION_VECTORS
	SPT.texture("r_input_depth", passInput.sceneDepthSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_INPUT_DEPTH
	SPT.rwTexture("rw_reconstructed_depth_previous_frame", reconstructedPrevDepthUAV); // FFX_FRAMEINTERPOLATION_BIND_UAV_RECONSTRUCTED_DEPTH_PREVIOUS_FRAME
	SPT.rwTexture("rw_dilated_motion_vectors", dilatedMotionVectorUAV); // FFX_FRAMEINTERPOLATION_BIND_UAV_DILATED_MOTION_VECTORS
	SPT.rwTexture("rw_dilated_depth", dilatedDepthUAV); // FFX_FRAMEINTERPOLATION_BIND_UAV_DILATED_DEPTH

	prepareDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
	auto descriptorHeap = prepareDescriptor.getDescriptorHeap(swapchainIndex);
	auto pipeline = reconstructAndDilatePipeline.get();

	commandList->setComputePipelineState(pipeline);
	commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

	uint32 dispatchX = (passInput.renderSizeX + 7) / 8;
	uint32 dispatchY = (passInput.renderSizeY + 7) / 8;
	commandList->dispatchCompute(dispatchX, dispatchY, 1);
}

// See ffxFrameInterpolationDispatch.
void FrameGenPass::dispatchPhase(RenderCommandList* commandList, uint32 swapchainIndex, const FrameGenPassInput& passInput)
{
	ConstantBufferView* frameInterpUniformCBV = getCurrentFrameInterpUniformCBV();
	ConstantBufferView* inpaintingPyramidUniformCBV = getCurrentInpaintingPyramidUniformCBV();

	const uint32 currResourceIndex = (cpuFrameIndex & 1);
	const uint32 prevResourceIndex = ((cpuFrameIndex + 1) & 1);

	const bool bReset = (interpolationDispatchCount == 0) || passInput.bReset;

	const bool bFrameID_decreased = passInput.frameID < prevFrameID;
	const bool bFrameID_skipped = (passInput.frameID - prevFrameID) > 1;
	const bool bDisjointFrameID = bFrameID_decreased || bFrameID_skipped;
	prevFrameID = passInput.frameID;
	interpolationDispatchCount++;

	const uint32 displayDispatchSizeX = (passInput.displaySizeX + 7) / 8;
	const uint32 displayDispatchSizeY = (passInput.displaySizeY + 7) / 8;

	const uint32 renderDispatchSizeX = (passInput.renderSizeX + 7) / 8;
	const uint32 renderDispatchSizeY = (passInput.renderSizeY + 7) / 8;

	const uint32 opticalFlowDispatchSizeX = (uint32)(passInput.displaySizeX / (float)kOpticalFlowBlockSize + 7) / 8;
	const uint32 opticalFlowDispatchSizeY = (uint32)(passInput.displaySizeY / (float)kOpticalFlowBlockSize + 7) / 8;

	const bool bExecutePreparationPasses = (false == bReset);

	// #wip: distortion field texture from passInput
	auto distortionFieldTexture            = defaultDistortionFieldTexture.get();
	auto distortionFieldSRV                = defaultDistortionFieldSRV.get();

	auto reconstructedPrevDepthTexture     = reconstructedPrevDepthTextures[currResourceIndex].get();
	auto reconstructedPrevDepthSRV         = reconstructedPrevDepthSRVs[currResourceIndex].get();
	auto reconstructedPrevDepthUAV         = reconstructedPrevDepthUAVs[currResourceIndex].get();
	auto dilatedMotionVectorTexture        = dilatedMotionVectorTextures[currResourceIndex].get();
	auto dilatedMotionVectorSRV            = dilatedMotionVectorSRVs[currResourceIndex].get();
	auto dilatedMotionVectorUAV            = dilatedMotionVectorUAVs[currResourceIndex].get();
	auto dilatedDepthTexture               = dilatedDepthTextures[currResourceIndex].get();
	auto dilatedDepthSRV                   = dilatedDepthSRVs[currResourceIndex].get();
	auto dilatedDepthUAV                   = dilatedDepthUAVs[currResourceIndex].get();
	auto currInterpolationSourceTexture    = passInput.sceneColorTexture;
	auto currInterpolationSourceSRV        = passInput.sceneColorSRV;

	{
		SCOPED_DRAW_EVENT(commandList, Setup);

		BufferBarrierAuto bufferBarriers[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, counterBuffer.get() },
		};
		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toShaderResource(passInput.opticalFlowPassOutput->sceneChangeDetectionTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(gameMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(gameMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(opticalFlowMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(opticalFlowMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(disocclusionMaskTexture.get(), EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);
		
		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
		SPT.texture("r_optical_flow_scd", passInput.opticalFlowPassOutput->sceneChangeDetectionSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_OPTICAL_FLOW_SCENE_CHANGE_DETECTION
		SPT.rwBuffer("rw_counters", counterUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_COUNTERS
		SPT.rwTexture("rw_game_motion_vector_field_x", gameMotionVectorFieldUAVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_GAME_MOTION_VECTOR_FIELD_X
		SPT.rwTexture("rw_game_motion_vector_field_y", gameMotionVectorFieldUAVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_GAME_MOTION_VECTOR_FIELD_Y
		SPT.rwTexture("rw_optical_flow_motion_vector_field_x", gameMotionVectorFieldUAVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_X
		SPT.rwTexture("rw_optical_flow_motion_vector_field_y", gameMotionVectorFieldUAVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_Y
		SPT.rwTexture("rw_disocclusion_mask", disocclusionMaskUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_DISOCCLUSION_MASK
		
		frameInterpDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());
		auto descriptorHeap = frameInterpDescriptor.getDescriptorHeap(swapchainIndex);
		auto pipeline = setupPipeline.get();

		commandList->setComputePipelineState(pipeline);
		commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);
		
		commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
	}

	auto scheduleDispatchGameVectorFieldInpaintingPyramid = [&]()
	{
		SCOPED_DRAW_EVENT(commandList, GameVectorFieldInpaintingPyramid);

		auto pipeline = gameVectorFieldInpaintingPyramidPipeline.get();
		auto& passDescriptor = gameMotionVectorFieldInpaintingPyramidDescriptor;

		// Auto exposure
		uint32 dispatchThreadGroupCountXY[2];
		uint32 workGroupOffset[2];
		uint32 numWorkGroupsAndMips[2];
		uint32 rectInfo[4] = { 0, 0, (uint32)passInput.renderSizeX, (uint32)passInput.renderSizeY };
		ffxSpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo, -1);

		BufferBarrierAuto bufferBarriers[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, counterBuffer.get() },
		};
		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toShaderResource(gameMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(gameMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(inpaintingPyramidTexture.get(), EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
		SPT.constantBuffer("cbInpaintingPyramid", inpaintingPyramidUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_INPAINTING_PYRAMID
		SPT.texture("r_game_motion_vector_field_x", gameMotionVectorFieldSRVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_GAME_MOTION_VECTOR_FIELD_X
		SPT.texture("r_game_motion_vector_field_y", gameMotionVectorFieldSRVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_GAME_MOTION_VECTOR_FIELD_Y
		SPT.rwBuffer("rw_counters", counterUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_COUNTERS
		for (size_t i = 0; i < _countof(inpaintingPyramidUAVs); ++i)
		{
			// rw_inpainting_pyramid0 ~ rw_inpainting_pyramidN (FFX_FRAMEINTERPOLATION_BIND_UAV_INPAINTING_PYRAMID_MIPMAP_0 ~ N)
			char msg[64];
			std::snprintf(msg, _countof(msg), "rw_inpainting_pyramid%u", (uint32)i);
			SPT.rwTexture(msg, inpaintingPyramidUAVs[i].get());
		}

		auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

		commandList->setComputePipelineState(pipeline);
		commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

		commandList->dispatchCompute(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);
	};

	if (bExecutePreparationPasses)
	{
		constexpr float zFar = getDeviceFarDepth();
		passInput.clearResourcePass->enqueueClear(
			reconstructedDepthInterpolatedFrameTexture.get(),
			reconstructedDepthInterpolatedFrameUAV.get(),
			ClearResourcePass::floatClearValue(zFar, zFar, zFar, zFar));
		passInput.clearResourcePass->executeClears(commandList, swapchainIndex);

		{
			SCOPED_DRAW_EVENT(commandList, ReconstructDepthPrevFrame);

			auto pipeline = reconstructPrevDepthPipeline.get();
			auto& passDescriptor = reconstructPrevDepthDescriptor;

			TextureBarrierAuto textureBarriers[] = {
				TextureBarrierAuto::toShaderResource(dilatedMotionVectorTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(dilatedDepthTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(currInterpolationSourceTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(distortionFieldTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toUnorderedAccess(reconstructedDepthInterpolatedFrameTexture.get(), EBarrierSync::COMPUTE_SHADING),
			};
			commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

			ShaderParameterTable SPT{};
			SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
			SPT.texture("r_dilated_motion_vectors", dilatedMotionVectorSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DILATED_MOTION_VECTORS
			SPT.texture("r_dilated_depth", dilatedDepthSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DILATED_DEPTH
			// #wip: not used but declared in ffx_frameinterpolation_reconstruct_previous_depth_pass.hlsl
			SPT.texture("r_current_interpolation_source", currInterpolationSourceSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_CURRENT_INTERPOLATION_SOURCE
			SPT.texture("r_input_distortion_field", distortionFieldSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DISTORTION_FIELD
			SPT.rwTexture("rw_reconstructed_depth_interpolated_frame", reconstructedDepthInterpolatedFrameUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_RECONSTRUCTED_DEPTH_INTERPOLATED_FRAME

			auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

			commandList->setComputePipelineState(pipeline);
			commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

			commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
		}

		{
			SCOPED_DRAW_EVENT(commandList, GameMotionVector);

			auto pipeline = gameMotionVectorFieldPipeline.get();
			auto& passDescriptor = gameMotionVectorFieldDescriptor;

			TextureBarrierAuto textureBarriers[] = {
				TextureBarrierAuto::toShaderResource(dilatedMotionVectorTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(dilatedDepthTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(prevInterpolationSourceTexture.get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(currInterpolationSourceTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(distortionFieldTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toUnorderedAccess(gameMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toUnorderedAccess(gameMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
			};
			commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

			ShaderParameterTable SPT{};
			SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
			SPT.texture("r_dilated_motion_vectors", dilatedMotionVectorSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DILATED_MOTION_VECTORS
			SPT.texture("r_dilated_depth", dilatedDepthSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DILATED_DEPTH
			SPT.texture("r_previous_interpolation_source", prevInterpolationSourceSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_PREVIOUS_INTERPOLATION_SOURCE
			SPT.texture("r_current_interpolation_source", currInterpolationSourceSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_CURRENT_INTERPOLATION_SOURCE
			SPT.texture("r_input_distortion_field", distortionFieldSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DISTORTION_FIELD
			SPT.rwTexture("rw_game_motion_vector_field_x", gameMotionVectorFieldUAVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_GAME_MOTION_VECTOR_FIELD_X
			SPT.rwTexture("rw_game_motion_vector_field_y", gameMotionVectorFieldUAVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_GAME_MOTION_VECTOR_FIELD_Y

			auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

			commandList->setComputePipelineState(pipeline);
			commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

			commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
		}
		
		scheduleDispatchGameVectorFieldInpaintingPyramid();

		{
			SCOPED_DRAW_EVENT(commandList, OpticalFlowVectorField);

			auto pipeline = opticalFlowVectorFieldPipeline.get();
			auto& passDescriptor = opticalFlowVectorFieldDescriptor;

			TextureBarrierAuto textureBarriers[] = {
				TextureBarrierAuto::toShaderResource(passInput.opticalFlowPassOutput->opticalFlowVectorTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(opticalFlowConfidenceTexture.get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(dilatedDepthTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(prevInterpolationSourceTexture.get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(currInterpolationSourceTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toUnorderedAccess(opticalFlowMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toUnorderedAccess(opticalFlowMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
			};
			commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

			ShaderParameterTable SPT{};
			SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
			SPT.texture("r_optical_flow", passInput.opticalFlowPassOutput->opticalFlowVectorSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_OPTICAL_FLOW
			SPT.texture("r_optical_flow_confidence", opticalFlowConfidenceSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_OPTICAL_FLOW_CONFIDENCE
			SPT.texture("r_dilated_depth", dilatedDepthSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DILATED_DEPTH
			SPT.texture("r_previous_interpolation_source", prevInterpolationSourceSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_PREVIOUS_INTERPOLATION_SOURCE
			SPT.texture("r_current_interpolation_source", currInterpolationSourceSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_CURRENT_INTERPOLATION_SOURCE
			SPT.rwTexture("rw_optical_flow_motion_vector_field_x", opticalFlowMotionVectorFieldUAVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_X
			SPT.rwTexture("rw_optical_flow_motion_vector_field_y", opticalFlowMotionVectorFieldUAVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_Y

			auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

			commandList->setComputePipelineState(pipeline);
			commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

			commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
		}

		{
			SCOPED_DRAW_EVENT(commandList, DisocclusionMask);

			auto pipeline = disocclusionMaskPipeline.get();
			auto& passDescriptor = disocclusionMaskDescriptor;

			TextureBarrierAuto textureBarriers[] = {
				TextureBarrierAuto::toShaderResource(gameMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(gameMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(reconstructedPrevDepthTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(dilatedDepthTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(reconstructedDepthInterpolatedFrameTexture.get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(inpaintingPyramidTexture.get(), EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toShaderResource(distortionFieldTexture, EBarrierSync::COMPUTE_SHADING),
				TextureBarrierAuto::toUnorderedAccess(disocclusionMaskTexture.get(), EBarrierSync::COMPUTE_SHADING),
			};
			commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

			ShaderParameterTable SPT{};
			SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
			SPT.texture("r_game_motion_vector_field_x", gameMotionVectorFieldSRVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_GAME_MOTION_VECTOR_FIELD_X
			SPT.texture("r_game_motion_vector_field_y", gameMotionVectorFieldSRVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_GAME_MOTION_VECTOR_FIELD_Y
			SPT.texture("r_reconstructed_depth_previous_frame", reconstructedPrevDepthSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_RECONSTRUCTED_DEPTH_PREVIOUS_FRAME
			SPT.texture("r_dilated_depth", dilatedDepthSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DILATED_DEPTH
			SPT.texture("r_reconstructed_depth_interpolated_frame", reconstructedDepthInterpolatedFrameSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_RECONSTRUCTED_DEPTH_INTERPOLATED_FRAME
			SPT.texture("r_inpainting_pyramid", inpaintingPyramidSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_INPAINTING_PYRAMID
			SPT.texture("r_input_distortion_field", distortionFieldSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_DISTORTION_FIELD
			SPT.rwTexture("rw_disocclusion_mask", disocclusionMaskUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_DISOCCLUSION_MASK

			auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

			commandList->setComputePipelineState(pipeline);
			commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

			commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
		}
	}

	// interpolationPipeline (pipelineFiScfi in ffx)
	{
		SCOPED_DRAW_EVENT(commandList, Interpolation);

		auto pipeline = interpolationPipeline.get();
		auto& passDescriptor = interpolationDescriptor;

		BufferBarrierAuto bufferBarriers[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::SHADER_RESOURCE, counterBuffer.get() },
		};
		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toShaderResource(gameMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(gameMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(opticalFlowMotionVectorFieldTextures[0].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(opticalFlowMotionVectorFieldTextures[1].get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(prevInterpolationSourceTexture.get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(currInterpolationSourceTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(disocclusionMaskTexture.get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(inpaintingPyramidTexture.get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(interpolationOutputTexture.get(), EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
		SPT.structuredBuffer("r_counters", counterSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_COUNTERS
		SPT.texture("r_game_motion_vector_field_x", gameMotionVectorFieldSRVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_GAME_MOTION_VECTOR_FIELD_X
		SPT.texture("r_game_motion_vector_field_y", gameMotionVectorFieldSRVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_GAME_MOTION_VECTOR_FIELD_Y
		SPT.texture("r_optical_flow_motion_vector_field_x", opticalFlowMotionVectorFieldSRVs[0].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_X
		SPT.texture("r_optical_flow_motion_vector_field_y", opticalFlowMotionVectorFieldSRVs[1].get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_OPTICAL_FLOW_MOTION_VECTOR_FIELD_Y
		SPT.texture("r_previous_interpolation_source", prevInterpolationSourceSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_PREVIOUS_INTERPOLATION_SOURCE
		SPT.texture("r_current_interpolation_source", currInterpolationSourceSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_CURRENT_INTERPOLATION_SOURCE
		SPT.texture("r_disocclusion_mask", disocclusionMaskSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_DISOCCLUSION_MASK
		SPT.texture("r_inpainting_pyramid", inpaintingPyramidSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_INPAINTING_PYRAMID
		SPT.rwTexture("rw_output", interpolationOutputUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT

		auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

		commandList->setComputePipelineState(pipeline);
		commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

		commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
	}

	{
		SCOPED_DRAW_EVENT(commandList, InpaintingPyramid);

		uint32 dispatchThreadGroupCountXY[2];
		uint32 workGroupOffset[2];
		uint32 numWorkGroupsAndMips[2];
		uint32 rectInfo[4] = { 0, 0, (uint32)passInput.displaySizeX, (uint32)passInput.displaySizeY };
		ffxSpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo, -1);

		auto pipeline = inpaintingPyramidPipeline.get();
		auto& passDescriptor = inpaintingPyramidDescriptor;

		BufferBarrierAuto bufferBarriers[] = {
			{ EBarrierSync::COMPUTE_SHADING, EBarrierAccess::UNORDERED_ACCESS, counterBuffer.get() },
		};
		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toShaderResource(interpolationOutputTexture.get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(inpaintingPyramidTexture.get(), EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(_countof(bufferBarriers), bufferBarriers, _countof(textureBarriers), textureBarriers, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
		SPT.constantBuffer("cbInpaintingPyramid", inpaintingPyramidUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_INPAINTING_PYRAMID
		SPT.texture("r_output", interpolationOutputSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_OUTPUT
		SPT.rwBuffer("rw_counters", counterUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_COUNTERS
		for (size_t i = 0; i < _countof(inpaintingPyramidUAVs); ++i)
		{
			// rw_inpainting_pyramid0 ~ rw_inpainting_pyramidN (FFX_FRAMEINTERPOLATION_BIND_UAV_INPAINTING_PYRAMID_MIPMAP_0 ~ N)
			char msg[64];
			std::snprintf(msg, _countof(msg), "rw_inpainting_pyramid%u", (uint32)i);
			SPT.rwTexture(msg, inpaintingPyramidUAVs[i].get());
		}

		auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

		commandList->setComputePipelineState(pipeline);
		commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

		commandList->dispatchCompute(dispatchThreadGroupCountXY[0], dispatchThreadGroupCountXY[1], 1);
	}

	{
		SCOPED_DRAW_EVENT(commandList, Inpainting);

		auto pipeline = inpaintingPipeline.get();
		auto& passDescriptor = inpaintingDescriptor;

		// #wip: Assumes PRESENT_BACKBUFFER == currInterpolationSourceTexture
		auto presentBackbufferSRV = currInterpolationSourceSRV;

		TextureBarrierAuto textureBarriers[] = {
			TextureBarrierAuto::toShaderResource(passInput.opticalFlowPassOutput->sceneChangeDetectionTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(inpaintingPyramidTexture.get(), EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toShaderResource(currInterpolationSourceTexture, EBarrierSync::COMPUTE_SHADING),
			TextureBarrierAuto::toUnorderedAccess(interpolationOutputTexture.get(), EBarrierSync::COMPUTE_SHADING),
		};
		commandList->barrierAuto(0, nullptr, _countof(textureBarriers), textureBarriers, 0, nullptr);

		ShaderParameterTable SPT{};
		SPT.constantBuffer("cbFI", frameInterpUniformCBV); // FFX_FRAMEINTERPOLATION_BIND_CB_FRAMEINTERPOLATION
		SPT.texture("r_optical_flow_scd", passInput.opticalFlowPassOutput->sceneChangeDetectionSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_OPTICAL_FLOW_SCENE_CHANGE_DETECTION
		SPT.texture("r_inpainting_pyramid", inpaintingPyramidSRV.get()); // FFX_FRAMEINTERPOLATION_BIND_SRV_INPAINTING_PYRAMID
		SPT.texture("r_present_backbuffer", presentBackbufferSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_PRESENT_BACKBUFFER
		SPT.texture("r_current_interpolation_source", currInterpolationSourceSRV); // FFX_FRAMEINTERPOLATION_BIND_SRV_CURRENT_INTERPOLATION_SOURCE
		SPT.rwTexture("rw_output", interpolationOutputUAV.get()); // FFX_FRAMEINTERPOLATION_BIND_UAV_OUTPUT

		auto descriptorHeap = passDescriptor.resizeDescriptorHeap(swapchainIndex, SPT.totalDescriptors());

		commandList->setComputePipelineState(pipeline);
		commandList->bindComputeShaderParameters(pipeline, &SPT, descriptorHeap);

		commandList->dispatchCompute(renderDispatchSizeX, renderDispatchSizeY, 1);
	}

	if (false /* draw debug view */)
	{
		// #wip: Dispatch debugViewPipeline
	}

	{
		SCOPED_DRAW_EVENT(commandList, StoreInterpolationSource);

		TextureBarrierAuto barriersBefore[] = {
			TextureBarrierAuto::toCopySource(currInterpolationSourceTexture),
			TextureBarrierAuto::toCopyDest(prevInterpolationSourceTexture.get()),
		};
		commandList->barrierAuto(0, nullptr, _countof(barriersBefore), barriersBefore, 0, nullptr);

		commandList->copyTexture2D(currInterpolationSourceTexture, prevInterpolationSourceTexture.get());
	}
}

ConstantBufferView* FrameGenPass::getCurrentFrameInterpUniformCBV()
{
	return frameInterpDescriptor.getUniformCBV(cpuFrameIndex & 1);
}

ConstantBufferView* FrameGenPass::getCurrentInpaintingPyramidUniformCBV()
{
	return inpaintingPyramidDescriptor.getUniformCBV(cpuFrameIndex & 1);
}
