#include "vk_win32.h"
#include "util/logging.h"

#include <Windows.h>
#include <vulkan/vulkan_win32.h>

DECLARE_LOG_CATEGORY(LogVulkan);

VkSurfaceKHR createVkSurfaceKHR_win32(
	VkInstance vkInstance,
	void* nativeWindowHandle)
{
	VkResult vkRet;
	VkWin32SurfaceCreateInfoKHR sci;
	PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR;

	vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(vkInstance, "vkCreateWin32SurfaceKHR");
	if (!vkCreateWin32SurfaceKHR)
	{
		CYLOG(LogVulkan, Fatal, L"VK_KHR_win32_surface extension is missing");
		return VK_NULL_HANDLE;
	}

	memset(&sci, 0, sizeof(sci));
	sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	sci.hinstance = ::GetModuleHandle(NULL);
	sci.hwnd = (HWND)nativeWindowHandle;
	sci.flags = 0;

	const VkAllocationCallbacks* allocator = nullptr;
	VkSurfaceKHR surfaceKHR;
	vkRet = vkCreateWin32SurfaceKHR(vkInstance, &sci, allocator, &surfaceKHR);
	if (vkRet != VK_SUCCESS)
	{
		CYLOG(LogVulkan, Fatal, L"Failed to create a KHR surface");
		return VK_NULL_HANDLE;
	}

	return surfaceKHR;
}
