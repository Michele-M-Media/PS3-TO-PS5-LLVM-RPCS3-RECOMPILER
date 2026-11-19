#pragma once
#include "swapchain_core.h"

namespace vk
{
    using swapchain_NATIVE = native_swapchain_base;

    static VkSurfaceKHR make_WSI_surface(VkInstance instance, VkPhysicalDevice gpu,
                                        display_handle_t, WSI_config* config)
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        config->supports_automatic_wm_reports = false;
        CHECK_RESULT(rpcs3::ps5::create_display_surface(instance, gpu, &surface));
        return surface;
    }
}
