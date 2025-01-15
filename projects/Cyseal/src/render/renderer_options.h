#pragma once

enum class ERendererType
{
	Standard,
	Null,
};

enum class EReverseZPolicy : uint8_t
{
	Traditional = 0,
	Reverse = 1
};
// All other codebase should consider this value.
constexpr EReverseZPolicy getReverseZPolicy()
{
	return EReverseZPolicy::Reverse;
}
constexpr float getDeviceFarDepth()
{
	return (getReverseZPolicy() == EReverseZPolicy::Reverse) ? 0.0f : 1.0f;
}
constexpr float getDeviceNearDepth()
{
	return (getReverseZPolicy() == EReverseZPolicy::Reverse) ? 1.0f : 0.0f;
}

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
