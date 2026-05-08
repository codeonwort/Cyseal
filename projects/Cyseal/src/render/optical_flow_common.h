#pragma once

#include "core/int_types.h"
#include "rhi/rhi_forward.h"

const uint32 kOpticalFlowBlockSize = 8;

// See ffx_opticalflow_prepare_luma.h
enum class OpticalFlowBackbufferTransferFunction : uint32
{
	LinearLdrToLuminance                  = 0,
	PQCorrectedHdrToPerceivedLuminance    = 1,
	SCRGBCorrectedHdrToPerceivedLuminance = 2,

	Count,
};

struct OpticalFlowPassOutput
{
	uint32              opticalFlowVectorSizeX          = 0;
	uint32              opticalFlowVectorSizeY          = 0;
	Texture*            opticalFlowVectorTexture        = nullptr;
	ShaderResourceView* opticalFlowVectorSRV            = nullptr;
	Texture*            sceneChangeDetectionTexture     = nullptr;
	ShaderResourceView* sceneChangeDetectionSRV         = nullptr;
};

// Ported from <FidelityFX_SDK>/sdk/include/FidelityFX/gpu/spd/ffx_spd.h
void ffxSpdSetup(
	uint32 dispatchThreadGroupCountXY[2],
	uint32 workGroupOffset[2],
	uint32 numWorkGroupsAndMips[2],
	const uint32 rectInfo[4],
	const int32 mips);
