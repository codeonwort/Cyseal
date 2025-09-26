#include "shader_dxc_common.h"

const wchar_t* getD3DShaderModelString(D3D_SHADER_MODEL shaderModel)
{
	switch (shaderModel)
	{
		case D3D_SHADER_MODEL_6_0: return L"6_0";
		case D3D_SHADER_MODEL_6_1: return L"6_1";
		case D3D_SHADER_MODEL_6_2: return L"6_2";
		case D3D_SHADER_MODEL_6_3: return L"6_3";
		case D3D_SHADER_MODEL_6_4: return L"6_4";
		case D3D_SHADER_MODEL_6_5: return L"6_5";
		case D3D_SHADER_MODEL_6_6: return L"6_6";
		case D3D_SHADER_MODEL_6_7: return L"6_7";
		default: CHECK_NO_ENTRY();
	}
	return L"?_?";
}

const wchar_t* getD3DShaderStagePrefix(EShaderStage stage)
{
	switch (stage)
	{
		case EShaderStage::VERTEX_SHADER:          return L"vs_";
		case EShaderStage::HULL_SHADER:            return L"hs_";
		case EShaderStage::DOMAIN_SHADER:          return L"ds_";
		case EShaderStage::GEOMETRY_SHADER:        return L"gs_";
		case EShaderStage::PIXEL_SHADER:           return L"ps_";
		case EShaderStage::COMPUTE_SHADER:         return L"cs_";
		case EShaderStage::MESH_SHADER:            return L"ms_";
		case EShaderStage::AMPLICATION_SHADER:     return L"as_";
		case EShaderStage::RT_RAYGEN_SHADER:
		case EShaderStage::RT_ANYHIT_SHADER:
		case EShaderStage::RT_CLOSESTHIT_SHADER:
		case EShaderStage::RT_MISS_SHADER:
		case EShaderStage::RT_INTERSECTION_SHADER: return L"lib_";
		default: CHECK_NO_ENTRY();
	}
	return L"??_";
}

std::wstring getD3DShaderProfile(D3D_SHADER_MODEL shaderModel, EShaderStage type)
{
	std::wstring profile = getD3DShaderStagePrefix(type);
	profile += getD3DShaderModelString(shaderModel);
	return profile;
}
