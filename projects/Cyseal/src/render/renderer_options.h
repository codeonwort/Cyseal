#pragma once

enum class ERendererType
{
	Standard,
	Null,
};

enum class EBufferVisualizationMode : uint32
{
	None             = 0,
	Albedo           = 1,
	Roughness        = 2,
	Normal           = 3,
	DirectLighting   = 4,
	IndirectSpecular = 5,

	Count,
};

enum class EIndirectSpecularMode
{
	Disabled         = 0,
	ForceMirror      = 1,
	BRDF             = 2,

	Count
};

enum class EPathTracingMode : uint32
{
	Disabled         = 0,
	Offline          = 1,
	Realtime         = 2,

	Count
};

inline const char** getBufferVisualizationModeNames()
{
	static const char* strings[] =
	{
		"None",
		"Albedo",
		"Roughness",
		"NormalWS",
		"DirectLighting",
		"IndirectSpecular",
	};
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
	return strings;
}

inline const char** getPathTracingModeNames()
{
	static const char* strings[] =
	{
		"Disabled",
		"Offline",
		"Realtime",
	};
	return strings;
}

struct RendererOptions
{
	bool bEnableIndirectDraw = true;
	bool bEnableGPUCulling = true;

	EBufferVisualizationMode bufferVisualization = EBufferVisualizationMode::None;

	EIndirectSpecularMode indirectSpecular = EIndirectSpecularMode::ForceMirror;

	EPathTracingMode pathTracing = EPathTracingMode::Disabled;
	bool bCameraHasMoved = false;

	bool anyRayTracingEnabled() const
	{
		bool bIndirectSpecular = indirectSpecular != EIndirectSpecularMode::Disabled;
		bool bPathTracing = pathTracing != EPathTracingMode::Disabled;
		return bIndirectSpecular || bPathTracing;
	}
};
