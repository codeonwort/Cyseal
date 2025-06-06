#pragma once

enum class ERendererType
{
	Standard,
	Null,
};

enum class EBufferVisualizationMode : uint32
{
	None             = 0,
	MaterialId       = 1,
	Albedo           = 2,
	Roughness        = 3,
	MetalMask        = 4,
	Normal           = 5,
	DirectLighting   = 6,
	RayTracedShadows = 7,
	IndirectDiffuse  = 8,
	IndirectSpecular = 9,
	VelocityMap      = 10,

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

enum class EIndirectSpecularMode : uint32
{
	Disabled         = 0,
	ForceMirror      = 1,
	BRDF             = 2,

	Count
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
	};
	static_assert(_countof(strings) == (int)EBufferVisualizationMode::Count);
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
	bool bEnableIndirectDraw = true;
	bool bEnableGPUCulling = true;

	EBufferVisualizationMode bufferVisualization = EBufferVisualizationMode::None;

	ERayTracedShadowsMode rayTracedShadows = ERayTracedShadowsMode::Disabled;
	EIndirectDiffuseMode indirectDiffuse = EIndirectDiffuseMode::Disabled;
	EIndirectSpecularMode indirectSpecular = EIndirectSpecularMode::ForceMirror;

	EPathTracingMode pathTracing = EPathTracingMode::Disabled;
	bool bCameraHasMoved = false;
	EPathTracingDenoiserState pathTracingDenoiserState = EPathTracingDenoiserState::WaitForFrameAccumulation;
	EPathTracingKernel pathTracingKernel = EPathTracingKernel::MegaKernel;

	bool anyRayTracingEnabled() const
	{
		bool bShadows = rayTracedShadows != ERayTracedShadowsMode::Disabled;
		bool bIndirectDiffuse = indirectDiffuse != EIndirectDiffuseMode::Disabled;
		bool bIndirectSpecular = indirectSpecular != EIndirectSpecularMode::Disabled;
		bool bPathTracing = pathTracing != EPathTracingMode::Disabled;
		return bShadows || bIndirectDiffuse || bIndirectSpecular || bPathTracing;
	}
};
