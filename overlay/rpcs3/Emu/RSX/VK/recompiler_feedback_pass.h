#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace vk::recompiler_feedback {
inline constexpr unsigned layout_base = 22;
inline constexpr unsigned feedback_layout = 4;
enum class plane { color, depth };

inline VkPipelineCreateFlags flags(std::uint64_t key, unsigned colors, bool enabled)
{
    if (!enabled || colors > 4)
        return 0;
    VkPipelineCreateFlags result = 0;
    for (unsigned i = 0; i < colors; ++i)
    {
        if (((key >> (layout_base + i * 3)) & 7) == feedback_layout)
            result |= VK_PIPELINE_CREATE_COLOR_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
    }
    if (((key >> 8) & 255) && ((key >> (layout_base + colors * 3)) & 7) == feedback_layout)
        result |= VK_PIPELINE_CREATE_DEPTH_STENCIL_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT;
    return result;
}

inline std::uint64_t companion(std::uint64_t key, unsigned colors, plane attachment)
{
    const auto color_format = static_cast<VkFormat>(key & 255);
    const auto depth_format = static_cast<VkFormat>((key >> 8) & 255);
    const auto layout = (key >> layout_base) & 0x7fff;
    if ((key >> 37) || ((key >> 16) & 0x3f) != 1 ||
        (depth_format != VK_FORMAT_D24_UNORM_S8_UINT && depth_format != VK_FORMAT_D32_SFLOAT_S8_UINT))
        return 0;
    const bool ordinary_color = colors == 1 && layout == 26 &&
        (color_format == VK_FORMAT_R16G16B16A16_SFLOAT || color_format == VK_FORMAT_B8G8R8A8_UNORM);
    const bool ordinary_depth = colors == 0 && layout == 3 && color_format == VK_FORMAT_UNDEFINED;
    if (attachment == plane::color && !ordinary_color)
        return 0;
    if (attachment == plane::depth && !ordinary_color && !ordinary_depth)
        return 0;
    const unsigned shift = layout_base + (attachment == plane::depth ? colors * 3 : 0);
    return (key & ~(std::uint64_t{7} << shift)) | (std::uint64_t{feedback_layout} << shift);
}
}
