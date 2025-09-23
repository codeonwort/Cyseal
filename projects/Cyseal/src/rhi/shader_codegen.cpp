#include "shader_codegen.h"
#include "core/platform.h"
#include "core/assertion.h"
#include "util/string_conversion.h"

#include <filesystem>

#if PLATFORM_WINDOWS
	#include <Windows.h>
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
	const char* inFilename,
	const char* inEntryPoint,
	EShaderStage stageFlag,
	std::initializer_list<std::wstring> defines)
{
	std::wstring targetProfileW = getD3DShaderProfile(D3D_SHADER_MODEL_FOR_SPIRV, stageFlag);
	std::string targetProfile;
	wstr_to_str(targetProfileW, targetProfile);

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
	ss << ' ' << inFilename;

	std::string cmd = ss.str();
	return readProcessOutput(cmd);
}

std::string ShaderCodegen::readProcessOutput(const std::string& cmd)
{
#if PLATFORM_WINDOWS
	// I dunno just copy & pasted MSDN.

	SECURITY_ATTRIBUTES saAttr;
	saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
	saAttr.bInheritHandle = TRUE;
	saAttr.lpSecurityDescriptor = NULL;

	HANDLE readHandle = NULL;
	HANDLE writeHandle = NULL;
	CHECK(::CreatePipe(&readHandle, &writeHandle, &saAttr, 0));
	CHECK(::SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0));

	PROCESS_INFORMATION piProcInfo;
	ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

	STARTUPINFOA siStartInfo;
	::ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
	siStartInfo.cb = sizeof(STARTUPINFO); 
	siStartInfo.hStdError = writeHandle;
	siStartInfo.hStdOutput = writeHandle;
	//siStartInfo.hStdInput = readHandle;
	siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

	BOOL bSuccess = ::CreateProcessA(NULL,
		(char*)cmd.c_str(),
		NULL,
		NULL,
		TRUE,
		0,
		NULL,
		NULL,
		&siStartInfo,
		&piProcInfo);
	CHECK(bSuccess);

	DWORD waitResult = ::WaitForSingleObject(piProcInfo.hProcess, 3000);
	CHECK(waitResult == WAIT_OBJECT_0);

	// If not closed here, ReadFile() will hang.
	::CloseHandle(writeHandle);

	std::stringstream ss;
	CHAR buf[1024];
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

	::CloseHandle(readHandle);
	::CloseHandle(piProcInfo.hProcess);
	::CloseHandle(piProcInfo.hThread);
	
	return ss.str();
#else
	#error "Not implemented"
#endif
}
