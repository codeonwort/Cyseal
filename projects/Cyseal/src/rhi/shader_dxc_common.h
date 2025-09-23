#pragma once

#include "shader_common.h"
#include "d3d12.h"

const wchar_t* getD3DShaderModelString(D3D_SHADER_MODEL shaderModel);
const wchar_t* getD3DShaderStagePrefix(EShaderStage stage);
std::wstring getD3DShaderProfile(D3D_SHADER_MODEL shaderModel, EShaderStage type);
