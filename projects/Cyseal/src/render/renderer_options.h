#pragma once

#include "core/int_types.h"
#include "core/clamped_numeric.h"

enum class ERendererType
{
	Standard,
	Null,
};

// Also modify:
// - getBufferVisualizationModeNames()
// - buffer_visualization.hlsl
enum class EBufferVisualizationMode : uint32
{
	None                          = 0,
	MaterialId                    = 1,
	Albedo                        = 2,
	Roughness                     = 3,
	MetalMask                     = 4,
	Normal                        = 5,
	DirectLighting                = 6,
	RayTracedShadows              = 7,
	IndirectDiffuse               = 8,
	IndirectSpecular              = 9,
	VelocityMap                   = 10,
	VisibilityBufferPrimitiveID   = 11,
	VisibilityBufferBarycentricUV = 12,
	VisibilityBufferMaterialId    = 13,
	VisibilityBufferAlbedo        = 14,
	VisibilityBufferRoughness     = 15,
	VisibilityBufferMetalMask     = 16,
	OpticalFlowVector             = 17,
	InterpolatedFrame             = 18,
	FrameGenerationDebugView      = 19,

	Count,
};

enum class EIndirectDrawMode : uint32
{
	Disabled      = 0,
	PopulateOnCPU = 1, // Populate indirect draw arguments buffer on CPU and write to GPU.
	PopulateOnGPU = 2, // Populate indirect draw arguments buffer on GPU.

	Count,
};

enum class ERayTracedShadowsMode : uint32
{
	Disabled         = 0,
	HardShadows      = 1,

	Count
};

enum class EIndirectDiffuseMode : uint32
{
	Disabled         = 0,
	RandomSampled    = 1,
	STBNSampled      = 2,

	Count
};

enum class EIndirectDiffuseDebugMode : uint32
{
	Radiance         = 0,
	HistoryCount     = 1,
	Variance         = 2,

	Count
};

enum class EIndirectSpecularMode : uint32
{
	Disabled         = 0,
	ForceMirror      = 1,
	BRDF             = 2,

	Count
};

enum EIndirectSpecularDebugMode : uint32
{
	None                              = 0,
	ReflectedDirectionOnPrimaryHit    = 1,
	AlbedoOnSecondaryHit              = 2,

	Count,
};

enum class EPathTracingMode : uint32
{
	Disabled          = 0,
	Offline           = 1,
	Realtime          = 2,
	RealtimeDenoising = 3,

	Count
};

enum class EPathTracingKernel : uint32
{
	MegaKernel = 0,
	Wavefront  = 1,

	Count
};

enum class EPathTracingDenoiserState
{
	WaitForFrameAccumulation,
	DenoiseNow,
	KeepDenoisingResult,
};

inline const char** getBufferVisualizationModeNames()
{
	static const char* strings[] =
	{
		"None",
		"MaterialId",
		"Albedo",
		"Roughness",
		"MetalMask",
		"NormalWS",
		"DirectLighting",
		"RayTracedShadows",
		"IndirectDiffuse",
		"IndirectSpecular",
		"VelocityMap",
		"VisibilityBufferPrimitiveId",
		"VisibilityBufferBarycentricUV",
		"VisibilityBufferMaterialId",
		"VisibilityBufferAlbedo",
		"VisibilityBufferRoughness",
		"VisibilityBufferMetalMask",
		"OpticalFlowVector",
		"InterpolatedFrame",
		"FrameGenerationDebugView",
	};
	static_assert(_countof(strings) == (int)EBufferVisualizationMode::Count);
	return strings;
}

inline const char** getIndirectDrawModeNames()
{
	static const char* strings[] =
	{
		"Disabled",
		"PopulateOnCPU",
		"PopulateOnGPU",
	};
	static_assert(_countof(strings) == (int)EIndirectDrawMode::Count);
	return strings;
}

inline const char** getRayTracedShadowsModeNames()
{
	static const char* strings[] =
	{
		"Disabled",
		"HardShadows",
	};
	static_assert(_countof(strings) == (int)ERayTracedShadowsMode::Count);
	return strings;
}

inline const char** getIndirectDiffuseModeNames()
{
	static const char* strings[] =
	{
		"Disabled",
		"RandomSampled",
		"STBNSampled",
	};
	static_assert(_countof(strings) == (int)EIndirectDiffuseMode::Count);
	return strings;
}

inline const char** getIndirectDiffuseDebugModeNames()
{
	static const char* strings[] =
	{
		"Radiance",
		"HistoryCount",
		"Variance",
	};
	static_assert(_countof(strings) == (int)EIndirectDiffuseDebugMode::Count);
	return strings;
}

inline const char** getIndirectSpecularModeNames()
{
	static const char* strings[] =
	{
		"Disabled",
		"ForceMirror",
		"BRDF",
	};
	static_assert(_countof(strings) == (int)EIndirectSpecularMode::Count);
	return strings;
}

inline const char** getIndirectSpecularDebugModeNames()
{
	static const char* strings[] =
	{
		"None",
		"ReflectedDirectionOnPrimaryHit",
		"AlbedoOnSecondaryHit",
	};
	static_assert(_countof(strings) == (int)EIndirectSpecularDebugMode::Count);
	return strings;
}

inline const char** getPathTracingModeNames()
{
	static const char* strings[] =
	{
		"Disabled",
		"Offline",
		"Realtime",
		"RealtimeDenoising",
	};
	static_assert(_countof(strings) == (int)EPathTracingMode::Count);
	return strings;
}

inline const char** getPathTracingKernelNames()
{
	static const char* strings[] =
	{
		"MegaKernel",
		"Wavefront",
	};
	static_assert(_countof(strings) == (int)EPathTracingKernel::Count);
	return strings;
}

struct RendererOptions
{
	// Presentation
	bool                       bForceVSync = true;

	// Indirect draw
	EIndirectDrawMode          indirectDrawMode = EIndirectDrawMode::PopulateOnGPU;
	bool                       bEnableGPUCulling = true;

	// Depth and visibility pass
	bool                       bEnableDepthPrepass = true;
	bool                       bEnableVisibilityBuffer = true;

	// Frame generation
	bool                       bGenerateFrame = true;
	float                      prevFrameTime = 0.0f; // In milliseconds, used for frame pacing.

	// Debug visualization
	EBufferVisualizationMode   bufferVisualization = EBufferVisualizationMode::None;

	// Ray traced shadows
	ERayTracedShadowsMode      rayTracedShadows = ERayTracedShadowsMode::Disabled;

	// Ray traced indirect diffuse
	struct IndirectDiffuse
	{
		EIndirectDiffuseMode      mode       = EIndirectDiffuseMode::Disabled;
		EIndirectDiffuseDebugMode debugMode  = EIndirectDiffuseDebugMode::Radiance;
		uint32                    randomSeed = 0;
		float                     cPhi       = 4.0f;
		float                     nPhi       = 128.0f;
		float                     pPhi       = 1.0f;
		int32                     blurCount  = 5;
	} indirectDiffuse;

	// Ray traced indirect specular
	EIndirectSpecularMode      indirectSpecular = EIndirectSpecularMode::Disabled;
	EIndirectSpecularDebugMode indirectSpecularDebugMode = EIndirectSpecularDebugMode::None;
	uint32                     indirectSpecularRandomSeed = 0;

	// Path tracing
	EPathTracingMode           pathTracing = EPathTracingMode::Disabled;
	uint32                     pathTracingRandomSeed = 0;
	bool                       bCameraHasMoved = false;
	EPathTracingDenoiserState  pathTracingDenoiserState = EPathTracingDenoiserState::WaitForFrameAccumulation;
	EPathTracingKernel         pathTracingKernel = EPathTracingKernel::MegaKernel;

	// Render target
	class Texture*             finalRenderTarget = nullptr; // If specified, render the result to it. If null, render to backbuffer.
	inline uint32              getResolutionScale() const { return resolutionScaleAvailable() ? resolutionScale : 100; }
	void                       setResolutionScale(uint32 value) { resolutionScale = value; }

private:
	Clamped<uint32>            resolutionScale{ 100, 25, 100 }; // Without resizing internal render targets, control the scale of render resolution.

public:
	inline bool anyRayTracingEnabled() const
	{
		bool bShadows = rayTracedShadows != ERayTracedShadowsMode::Disabled;
		bool bIndirectDiffuse = indirectDiffuse.mode != EIndirectDiffuseMode::Disabled;
		bool bIndirectSpecular = indirectSpecular != EIndirectSpecularMode::Disabled;
		bool bPathTracing = pathTracing != EPathTracingMode::Disabled;
		return bShadows || bIndirectDiffuse || bIndirectSpecular || bPathTracing;
	}

	inline bool renderToBackbuffer() const { return finalRenderTarget == nullptr; }

	// #todo-oidn: It seems OIDN does not support denoising subregion of oidn buffers.
	// I can resize oidn buffers whenever render scale changes, but using CPU version of OIDN for realtime purpose is not gonna work well anyway,
	// so let's disable render scale for path tracing, for now. Let's remove this limitation when I do serious work on denoiser.
	inline bool resolutionScaleAvailable() const { return pathTracing == EPathTracingMode::Disabled; }
};
