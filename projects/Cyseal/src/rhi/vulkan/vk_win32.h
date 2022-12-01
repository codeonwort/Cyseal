// Windows specific Vulkan code

#pragma once

#if COMPILE_BACKEND_VULKAN

#include <vulkan/vulkan_core.h>
#include <Windows.h>

VkSurfaceKHR createVkSurfaceKHR_win32(
	VkInstance vkInstance,
	void* nativeWindowHandle);

#endif // COMPILE_BACKEND_VULKAN
