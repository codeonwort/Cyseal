#pragma once

#include "rhi/pixel_format.h"

static const EPixelFormat PF_visibilityBuffer = EPixelFormat::R32_UINT;
static const EPixelFormat PF_barycentric      = EPixelFormat::R16G16_FLOAT;
static const EPixelFormat PF_sceneColor       = EPixelFormat::R32G32B32A32_FLOAT;
static const EPixelFormat PF_velocityMap      = EPixelFormat::R16G16_FLOAT;

static const uint32 NUM_GBUFFERS = 2;
static const EPixelFormat PF_gbuffers[NUM_GBUFFERS] = {
	EPixelFormat::R32G32B32A32_UINT, //EPixelFormat::R16G16B16A16_FLOAT,
	EPixelFormat::R16G16B16A16_FLOAT,
};
