#pragma once

#include "rhi/pixel_format.h"

static const EPixelFormat PF_visibilityBuffer = EPixelFormat::R32_UINT;
static const EPixelFormat PF_barycentric      = EPixelFormat::R16G16_FLOAT;
static const EPixelFormat PF_sceneColor       = EPixelFormat::R32G32B32A32_FLOAT;
static const EPixelFormat PF_velocityMap      = EPixelFormat::R16G16_FLOAT;
static const EPixelFormat PF_finalSceneColor  = EPixelFormat::R8G8B8A8_UNORM;

static const uint32 NUM_GBUFFERS = 2;
static const EPixelFormat PF_gbuffers[NUM_GBUFFERS] = {
	EPixelFormat::R32G32B32A32_UINT, //EPixelFormat::R16G16B16A16_FLOAT,
	EPixelFormat::R16G16B16A16_FLOAT,
};

// https://github.com/microsoft/DirectX-Specs/blob/master/d3d/PlanarDepthStencilDDISpec.md
// NOTE: Also need to change backbufferDepthFormat in render_device.h
#if 0
// Depth 24-bit, Stencil 8-bit
static const EPixelFormat PF_sceneDepth = EPixelFormat::R24G8_TYPELESS;
static const EPixelFormat PF_sceneDepthDSV = EPixelFormat::D24_UNORM_S8_UINT;
static const EPixelFormat PF_sceneDepthSRV = EPixelFormat::R24_UNORM_X8_TYPELESS;
#else
// Depth 32-bit, Stencil 8-bit
static const EPixelFormat PF_sceneDepth = EPixelFormat::R32G8X24_TYPELESS;
static const EPixelFormat PF_sceneDepthDSV = EPixelFormat::D32_FLOAT_S8_UINT;
static const EPixelFormat PF_sceneDepthSRV = EPixelFormat::R32_FLOAT_X8X24_TYPELESS;
#endif
