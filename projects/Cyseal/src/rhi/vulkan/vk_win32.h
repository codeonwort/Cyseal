// Windows specific Vulkan code

#pragma once

#if COMPILE_BACKEND_VULKAN

#include <Volk/volk.h>
#include <Windows.h>

VkSurfaceKHR createVkSurfaceKHR_win32(VkInstance vkInstance, void* nativeWindowHandle);

#endif // COMPILE_BACKEND_VULKAN
