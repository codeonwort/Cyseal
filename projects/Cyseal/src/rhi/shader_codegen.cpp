#include "shader_codegen.h"
#include "core/platform.h"
#include "core/assertion.h"
#include "util/string_conversion.h"

#include <filesystem>

#if PLATFORM_WINDOWS
	#include <Windows.h>
	#define NAMED_PIPE_SPIRV_CODEGEN "\\\\.\\pipe\\spirv_codegen_pipe"
#endif

// #todo-rhi: No idea how to customize it.
#define D3D_SHADER_MODEL_FOR_SPIRV D3D_SHADER_MODEL_6_6

static std::filesystem::path getDxcPath()
{
	std::filesystem::path dxcPath = "../../external/dxc/bin/x64/dxc.exe";
	CHECK(std::filesystem::exists(dxcPath));
	return std::filesystem::absolute(dxcPath);
}

ShaderCodegen::ShaderCodegen()
{
	dxcPath = getDxcPath().string();
}

std::string ShaderCodegen::hlslToSpirv(
	bool bEmitBytecode,
	const char* inFilename,
	const char* inEntryPoint,
	EShaderStage stageFlag,
	std::initializer_list<std::wstring> defines)
{
	std::wstring targetProfileW = getD3DShaderProfile(D3D_SHADER_MODEL_FOR_SPIRV, stageFlag);
	std::string targetProfile;
	wstr_to_str(targetProfileW, targetProfile);

	// https://github.com/Microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#vulkan-command-line-options

	std::stringstream ss;
	ss << dxcPath;
	ss << " -spirv";
	ss << " -T " << targetProfile;
	ss << " -E " << inEntryPoint;
	for (const std::wstring& defW : defines)
	{
		std::string def;
		wstr_to_str(defW, def);
		ss << " -D" << def;
	}
	ss << " -fspv-reflect"; // Emits additional SPIR-V instructions to aid reflection.
	ss << " -enable-16bit-types";
	ss << ' ' << inFilename;
	if (bEmitBytecode)
	{
		ss << " -Fo " << NAMED_PIPE_SPIRV_CODEGEN;
	}

	std::string cmd = ss.str();
	return readProcessOutput(cmd, bEmitBytecode);
}

std::string ShaderCodegen::readProcessOutput(const std::string& cmd, bool bEmitBytecode)
{
#if PLATFORM_WINDOWS
	if (bEmitBytecode)
	{
		HANDLE hNamedPipe = ::CreateNamedPipeA(
			NAMED_PIPE_SPIRV_CODEGEN,
			PIPE_ACCESS_INBOUND,
			PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
			1,
			4096,
			4096,
			0,
			NULL);
		CHECK(hNamedPipe != NULL);

		CHECK(TRUE == ::SetHandleInformation(hNamedPipe, HANDLE_FLAG_INHERIT, 0));

		PROCESS_INFORMATION piProcInfo;
		::ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

		STARTUPINFOA siStartInfo;
		::ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
		siStartInfo.cb = sizeof(STARTUPINFO);
		//siStartInfo.hStdError = hNamedPipe;
		//siStartInfo.hStdOutput = hNamedPipe; // Replaced with -Fo option.
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

		CHECK(TRUE == ::CreateProcessA(
			NULL,
			(char*)cmd.c_str(),
			NULL,
			NULL,
			FALSE, // bInheritHandles
			0,
			NULL,
			NULL,
			&siStartInfo,
			&piProcInfo));

		CHECK(WAIT_OBJECT_0 == ::WaitForSingleObject(piProcInfo.hProcess, 3000));

		std::stringstream ss;
		CHAR buf[4096];
		DWORD nRead = 0;
		for (;;)
		{
			bool bRead = ::ReadFile(hNamedPipe, buf, sizeof(buf), &nRead, NULL);
			if (!bRead || nRead == 0) break;
			for (DWORD i = 0; i < nRead; ++i)
			{
				ss << buf[i];
			}
		}

		CHECK(TRUE == ::CloseHandle(hNamedPipe));
		CHECK(TRUE == ::CloseHandle(piProcInfo.hProcess));
		CHECK(TRUE == ::CloseHandle(piProcInfo.hThread));

		return ss.str();
	}
	else
	{
		SECURITY_ATTRIBUTES saAttr{
			.nLength              = sizeof(SECURITY_ATTRIBUTES),
			.lpSecurityDescriptor = NULL,
			.bInheritHandle       = TRUE,
		};

		HANDLE readHandle = NULL;
		HANDLE writeHandle = NULL;
		CHECK(TRUE == ::CreatePipe(&readHandle, &writeHandle, &saAttr, 0));
		CHECK(TRUE == ::SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0));

		PROCESS_INFORMATION piProcInfo;
		::ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

		STARTUPINFOA siStartInfo;
		::ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
		siStartInfo.cb = sizeof(STARTUPINFO);
		siStartInfo.hStdError = writeHandle;
		siStartInfo.hStdOutput = writeHandle;
		//siStartInfo.hStdInput = readHandle;
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

		CHECK(TRUE == ::CreateProcessA(
			NULL,
			(char*)cmd.c_str(),
			NULL,
			NULL,
			TRUE, // bInheritHandles
			0,
			NULL,
			NULL,
			&siStartInfo,
			&piProcInfo));

		CHECK(WAIT_OBJECT_0 == ::WaitForSingleObject(piProcInfo.hProcess, 3000));

		// If not closed here, ReadFile() will hang.
		CHECK(TRUE == ::CloseHandle(writeHandle));

		std::stringstream ss;
		CHAR buf[4096];
		DWORD nRead = 0;
		for (;;)
		{
			bool bRead = ::ReadFile(readHandle, buf, sizeof(buf), &nRead, NULL);
			if (!bRead || nRead == 0) break;
			for (DWORD i = 0; i < nRead; ++i)
			{
				ss << buf[i];
			}
		}

		CHECK(TRUE == ::CloseHandle(readHandle));
		CHECK(TRUE == ::CloseHandle(piProcInfo.hProcess));
		CHECK(TRUE == ::CloseHandle(piProcInfo.hThread));

		return ss.str();
	}
#else
	#error "Not implemented"
#endif
}
