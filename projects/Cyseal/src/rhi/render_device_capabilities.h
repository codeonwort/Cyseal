#pragma once

#include "util/enum_util.h"
#include "core/assertion.h"

// Look for D3D12_FEATURE_DATA_D3D12_OPTIONS[N] in d3d12.h

// D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS
enum class EMultiSampleLevel : uint32
{
	x2    = 0,
	x4    = 1,
	x8    = 2,
	x16   = 3,

	Count,
};

inline uint32 toSampleCount(EMultiSampleLevel level)
{
	switch (level)
	{
		case EMultiSampleLevel::x2: return 2;
		case EMultiSampleLevel::x4: return 4;
		case EMultiSampleLevel::x8: return 8;
		case EMultiSampleLevel::x16: return 16;
		default: CHECK_NO_ENTRY();
	}
	return 0xffffffff;
}

// D3D12_RAYTRACING_TIER
enum class ERaytracingTier : uint8
{
	NotSupported,
	Tier_1_0,
	Tier_1_1,
	Tier_1_2,
	
	MaxTier = Tier_1_2
};

// D3D12_VARIABLE_SHADING_RATE_TIER
enum class EVariableShadingRateTier : uint8
{
	NotSupported,
	Tier_1,
	Tier_2,

	MaxTier = Tier_2
};

// D3D12_MESH_SHADER_TIER
enum class EMeshShaderTier : uint8
{
	NotSupported,
	Tier_1,

	MaxTier = Tier_1
};

// D3D12_SAMPLER_FEEDBACK_TIER
enum class ESamplerFeedbackTier : uint8
{
	NotSupported,
	Tier_0_9,
	Tier_1_0,

	MaxTier = Tier_1_0
};

inline const char* toString(ERaytracingTier tier)
{
	switch (tier)
	{
		case ERaytracingTier::NotSupported: return "NotSupported";
		case ERaytracingTier::Tier_1_0:     return "Tier_1_0    ";
		case ERaytracingTier::Tier_1_1:     return "Tier_1_1    ";
		case ERaytracingTier::Tier_1_2:     return "Tier_1_2    ";
	}
	CHECK_NO_ENTRY();
	return "";
}
inline const char* toString(EVariableShadingRateTier tier)
{
	switch (tier)
	{
		case EVariableShadingRateTier::NotSupported: return "NotSupported";
		case EVariableShadingRateTier::Tier_1:       return "Tier_1      ";
		case EVariableShadingRateTier::Tier_2:       return "Tier_2      ";
	}
	CHECK_NO_ENTRY();
	return "";
}
inline const char* toString(EMeshShaderTier tier)
{
	switch (tier)
	{
		case EMeshShaderTier::NotSupported: return "NotSupported";
		case EMeshShaderTier::Tier_1:       return "Tier_1      ";
	}
	CHECK_NO_ENTRY();
	return "";
}
inline const char* toString(ESamplerFeedbackTier tier)
{
	switch (tier)
	{
		case ESamplerFeedbackTier::NotSupported: return "NotSupported";
		case ESamplerFeedbackTier::Tier_0_9:     return "Tier_0_9    ";
		case ESamplerFeedbackTier::Tier_1_0:     return "Tier_1_0    ";
	}
	CHECK_NO_ENTRY();
	return "";
}
