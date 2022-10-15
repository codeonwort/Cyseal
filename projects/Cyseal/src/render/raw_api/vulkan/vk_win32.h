// Windows specific Vulkan code

#pragma once

#include <vulkan/vulkan_core.h>
#include <Windows.h>

VkSurfaceKHR createVkSurfaceKHR_win32(
	VkInstance vkInstance,
	void* nativeWindowHandle);
