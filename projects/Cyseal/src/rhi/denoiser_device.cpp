#include "denoiser_device.h"
#include "core/platform.h"
#include "util/logging.h"

// #todo-oidn: GPU version need to know which OS and graphics API are used.
// DenoiserDevice will need to be subclassed for each RHI backend.

#if PLATFORM_WINDOWS
	// #todo-oidn: oidn is not Windows-only but I'm downloading Windows pre-built binaries.
	#pragma comment(lib, "OpenImageDenoise.lib")
#else
	#error Not supported yet
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDenoiserDevice);

static std::wstring getOIDNDeviceTypeString(OIDNDeviceType type)
{
	switch (type)
	{
		case OIDN_DEVICE_TYPE_DEFAULT : return L"Default";
		case OIDN_DEVICE_TYPE_CPU     : return L"CPU";
		case OIDN_DEVICE_TYPE_SYCL    : return L"SYCL";
		case OIDN_DEVICE_TYPE_CUDA    : return L"CUDA";
		case OIDN_DEVICE_TYPE_HIP     : return L"HIP";
		case OIDN_DEVICE_TYPE_METAL   : return L"METAL";
		default                       : return L"<unknown>";
	}
}

void DenoiserDevice::create()
{
	oidnDevice = oidnNewDevice(OIDN_DEVICE_TYPE_DEFAULT);
	oidnCommitDevice(oidnDevice);

	if (checkNoDeviceError())
	{
		// Device info
		int32 deviceType = oidnGetDeviceInt(oidnDevice, "type");
		std::wstring deviceTypeStr = getOIDNDeviceTypeString((OIDNDeviceType)deviceType);
		int32 majorVer = oidnGetDeviceInt(oidnDevice, "versionMajor");
		int32 minorVer = oidnGetDeviceInt(oidnDevice, "versionMinor");
		int32 patchVer = oidnGetDeviceInt(oidnDevice, "versionPatch");

		CYLOG(LogDenoiserDevice, Log, TEXT("Intel OpenImageDenoise type=%s ver=%d.%d.%d"),
			deviceTypeStr.c_str(), majorVer, minorVer, patchVer);

		// #todo-oidn: GPU version
		// externalMemoryTypes
		//OIDNExternalMemoryTypeFlag externalMemoryTypes = (OIDNExternalMemoryTypeFlag)oidnGetDeviceInt(oidnDevice, "externalMemoryTypes");
		//bool bSupportsGpuMemory =
		//	ENUM_HAS_FLAG(externalMemoryTypes, OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE) &&
		//	ENUM_HAS_FLAG(externalMemoryTypes, OIDN_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32);

		// OIDNFilter
		oidnFilter = oidnNewFilter(oidnDevice, "RT");
		oidnSetFilterBool(oidnFilter, "hdr", true);
	}
	else
	{
		destroy();
	}
}

void DenoiserDevice::destroy()
{
	oidnReleaseFilter(oidnFilter);
	oidnReleaseBuffer(oidnColorBuffer);
	oidnReleaseBuffer(oidnAlbedoBuffer);
	oidnReleaseBuffer(oidnNormalBuffer);
	oidnReleaseBuffer(oidnDenoisedBuffer);
	oidnReleaseDevice(oidnDevice);
	oidnFilter = nullptr;
	oidnColorBuffer = nullptr;
	oidnAlbedoBuffer = nullptr;
	oidnNormalBuffer = nullptr;
	oidnDenoisedBuffer = nullptr;
	oidnDevice = nullptr;
}

void DenoiserDevice::recreateResources(uint32 imageWidth, uint32 imageHeight)
{
	if (width == imageWidth && height == imageHeight)
	{
		return;
	}
	width = imageWidth;
	height = imageHeight;

	if (oidnColorBuffer != nullptr) oidnReleaseBuffer(oidnColorBuffer);
	if (oidnAlbedoBuffer != nullptr) oidnReleaseBuffer(oidnAlbedoBuffer);
	if (oidnNormalBuffer != nullptr) oidnReleaseBuffer(oidnNormalBuffer);
	if (oidnDenoisedBuffer != nullptr) oidnReleaseBuffer(oidnDenoisedBuffer);

	const size_t pixelSize = 3 * sizeof(float);
	const size_t bufferSize = width * height * pixelSize;

	//oidnNewSharedBufferFromWin32Handle(oidnDevice, OIDN_EXTERNAL_MEMORY_TYPE_FLAG_D3D12_RESOURCE, 
	oidnColorBuffer = oidnNewBufferWithStorage(oidnDevice, bufferSize, OIDN_STORAGE_DEVICE);
	oidnAlbedoBuffer = oidnNewBufferWithStorage(oidnDevice, bufferSize, OIDN_STORAGE_DEVICE);
	oidnNormalBuffer = oidnNewBufferWithStorage(oidnDevice, bufferSize, OIDN_STORAGE_DEVICE);
	oidnDenoisedBuffer = oidnNewBufferWithStorage(oidnDevice, bufferSize, OIDN_STORAGE_DEVICE);
	checkNoDeviceError();
}

bool DenoiserDevice::denoise(Texture* noisy, Texture* albedo, Texture* normal, Texture* denoised)
{
	if (!isValid())
	{
		return false;
	}

	// #todo-oidn: Copy input textures to oidn buffers.
	// ...

	//oidnSetFilterImage(oidnFilter, "color", oidnColorBuffer, OIDN_FORMAT_FLOAT3, width, height, 0, 0, 0);
	//oidnSetFilterImage(oidnFilter, "albedo", oidnAlbedoBuffer, OIDN_FORMAT_FLOAT3, width, height, 0, 0, 0);
	//oidnSetFilterImage(oidnFilter, "normal", oidnNormalBuffer, OIDN_FORMAT_FLOAT3, width, height, 0, 0, 0);
	//oidnSetFilterImage(oidnFilter, "output", oidnDenoisedBuffer, OIDN_FORMAT_FLOAT3, width, height, 0, 0, 0);
	//oidnCommitFilter(oidnFilter);
	//oidnExecuteFilter(oidnFilter);

	// #todo-oidn: Copy denoised buffer to output.
	// ...

	return true;
}

bool DenoiserDevice::isValid() const
{
	return oidnDevice != nullptr
		&& oidnColorBuffer != nullptr
		&& oidnAlbedoBuffer != nullptr
		&& oidnNormalBuffer != nullptr
		&& oidnDenoisedBuffer != nullptr;
}

bool DenoiserDevice::checkNoDeviceError()
{
	const char* oidnErr;
	if (oidnGetDeviceError(oidnDevice, &oidnErr) != OIDN_ERROR_NONE)
	{
		CYLOG(LogDenoiserDevice, Error, L"oidn error: %S", oidnErr);
		return false;
	}
	return true;
}
