#include "vulkan_runtime.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

// The title can persist the decision in its game log as well as stderr.
extern "C" void recompiler_log_display_mode(const char* message) __attribute__((weak));

namespace
{
    void log_mode(const char* event, const VkDisplayModeParametersKHR& mode, std::uint32_t requested)
    {
        char message[192];
        std::snprintf(message, sizeof(message),
            "RECOMPILER_DISPLAY_MODE: %s width=%u height=%u mode_millihz=%u requested_millihz=%u",
            event, mode.visibleRegion.width, mode.visibleRegion.height, mode.refreshRate, requested);
        std::fprintf(stderr, "%s\n", message);
        if (recompiler_log_display_mode)
            recompiler_log_display_mode(message);
    }

    template <typename T, typename Enumerate>
    VkResult enumerate(std::vector<T>& values, Enumerate query)
    {
        for (unsigned retry = 0; retry != 4; ++retry)
        {
            std::uint32_t count = 0;
            VkResult result = query(&count, nullptr);
            if (result != VK_SUCCESS && result != VK_INCOMPLETE)
                return result;
            if (count > 4096)
                return VK_ERROR_INITIALIZATION_FAILED;
            values.resize(count);
            if (!count)
                return VK_SUCCESS;
            result = query(&count, values.data());
            if (result == VK_SUCCESS)
            {
                values.resize(count);
                return VK_SUCCESS;
            }
            if (result != VK_INCOMPLETE)
                return result;
        }
        return VK_INCOMPLETE;
    }
}

namespace rpcs3::ps5
{
    VkResult create_display_surface(VkInstance instance, VkPhysicalDevice gpu,
                                    VkSurfaceKHR* surface)
    {
        if (!surface)
            return VK_ERROR_INITIALIZATION_FAILED;
        *surface = VK_NULL_HANDLE;
        if (!vkGetPhysicalDeviceDisplayPropertiesKHR || !vkGetDisplayModePropertiesKHR ||
            !vkGetPhysicalDeviceDisplayPlanePropertiesKHR || !vkGetDisplayPlaneSupportedDisplaysKHR ||
            !vkGetDisplayPlaneCapabilitiesKHR || !vkCreateDisplayPlaneSurfaceKHR)
            return VK_ERROR_EXTENSION_NOT_PRESENT;

        std::vector<VkDisplayPropertiesKHR> displays;
        VkResult result = enumerate(displays, [&](auto count, auto data)
        { return vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, count, data); });
        if (result != VK_SUCCESS)
            return result;
        std::vector<VkDisplayPlanePropertiesKHR> planes;
        result = enumerate(planes, [&](auto count, auto data)
        { return vkGetPhysicalDeviceDisplayPlanePropertiesKHR(gpu, count, data); });
        if (result != VK_SUCCESS)
            return result;

        const char* preference = std::getenv("RPCS3_PS5_DISPLAY_TARGET_MILLIHZ");
        const std::uint32_t requested = preference && std::strcmp(preference, "119880") == 0 ? 119880 : 60000;
        for (const auto& display : displays)
        {
            // Keep the native extent first and choose among the modes the display exposed.
            std::vector<VkDisplayModePropertiesKHR> modes;
            result = enumerate(modes, [&](auto count, auto data)
            { return vkGetDisplayModePropertiesKHR(gpu, display.display, count, data); });
            if (result != VK_SUCCESS)
                return result;
            for (const auto& mode : modes)
                log_mode("offered", mode.parameters, requested);
            auto score = [&](const VkDisplayModePropertiesKHR& mode)
            {
                const auto& p = mode.parameters;
                const bool native = p.visibleRegion.width == display.physicalResolution.width &&
                                    p.visibleRegion.height == display.physicalResolution.height;
                const auto hz = static_cast<std::int64_t>(p.refreshRate);
                return (native ? std::int64_t{0} : std::int64_t{1} << 40) +
                       (hz >= requested ? hz - requested : requested - hz);
            };
            std::stable_sort(modes.begin(), modes.end(), [&](const auto& left, const auto& right)
            { return score(left) < score(right); });
            for (std::uint32_t plane = 0; plane < planes.size(); ++plane)
            {
                if (planes[plane].currentDisplay != VK_NULL_HANDLE &&
                    planes[plane].currentDisplay != display.display)
                    continue;
                std::vector<VkDisplayKHR> supported;
                result = enumerate(supported, [&](auto count, auto data)
                { return vkGetDisplayPlaneSupportedDisplaysKHR(gpu, plane, count, data); });
                if (result != VK_SUCCESS)
                    return result;
                if (std::find(supported.begin(), supported.end(), display.display) == supported.end())
                    continue;
                if (!(display.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
                    continue;
                for (const auto& mode : modes)
                {
                    VkDisplayPlaneCapabilitiesKHR caps{};
                    result = vkGetDisplayPlaneCapabilitiesKHR(gpu, mode.displayMode, plane, &caps);
                    if (result != VK_SUCCESS)
                        return result;
                    const auto extent = mode.parameters.visibleRegion;
                    if (!extent.width || !extent.height ||
                        extent.width < caps.minSrcExtent.width || extent.width > caps.maxSrcExtent.width ||
                        extent.height < caps.minSrcExtent.height || extent.height > caps.maxSrcExtent.height ||
                        extent.width < caps.minDstExtent.width || extent.width > caps.maxDstExtent.width ||
                        extent.height < caps.minDstExtent.height || extent.height > caps.maxDstExtent.height ||
                        caps.minDstPosition.x > 0 || caps.maxDstPosition.x < 0 ||
                        caps.minDstPosition.y > 0 || caps.maxDstPosition.y < 0 ||
                        caps.minSrcPosition.x > 0 || caps.maxSrcPosition.x < 0 ||
                        caps.minSrcPosition.y > 0 || caps.maxSrcPosition.y < 0 ||
                        !(caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR))
                        continue;
                    VkDisplaySurfaceCreateInfoKHR info{};
                    info.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
                    info.displayMode = mode.displayMode;
                    info.planeIndex = plane;
                    info.planeStackIndex = planes[plane].currentStackIndex;
                    info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
                    info.globalAlpha = 1.0f;
                    info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
                    info.imageExtent = extent;
                    VkSurfaceKHR created = VK_NULL_HANDLE;
                    result = vkCreateDisplayPlaneSurfaceKHR(instance, &info, nullptr, &created);
                    if (result == VK_SUCCESS)
                    {
                        *surface = created;
                        log_mode("selected", mode.parameters, requested);
                    }
                    else if (requested == 119880 && result == VK_ERROR_INITIALIZATION_FAILED)
                    {
                        log_mode("surface_unavailable", mode.parameters, requested);
                        continue;
                    }
                    return result;
                }
            }
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }
}
