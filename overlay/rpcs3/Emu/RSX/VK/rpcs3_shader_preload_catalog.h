#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace vk::shader_preload_catalog
{
// Active formats from RPCS3's Vulkan shader_interpreter::preload.
inline constexpr VkFormat source_formats[] = {
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_R8G8_UNORM,
    VK_FORMAT_R32_SFLOAT,
    VK_FORMAT_R8_UNORM,
};

struct preset
{
    VkFormat color;
    VkFormat depth;
    std::uint8_t color_count;
    bool blend;
};

// Expand a single-target key made by VKRenderPass.cpp, with no input attachments.
// Its overload's third argument is the sample count in the native core.
constexpr std::uint64_t expand_color_targets(std::uint64_t key, std::uint8_t color_count)
{
    constexpr unsigned layout_start = 22;
    constexpr unsigned layout_width = 3;
    constexpr std::uint64_t layout_mask = 7;
    const auto color_layout = (key >> layout_start) & layout_mask;
    const auto depth_layout = (key >> (layout_start + layout_width)) & layout_mask;
    auto layouts = depth_layout << (color_count * layout_width);
    for (unsigned index = 0; index < color_count; ++index)
        layouts |= color_layout << (index * layout_width);
    return (key & ((std::uint64_t{1} << layout_start) - 1)) | (layouts << layout_start);
}

template <typename FormatFeatures>
std::vector<preset> enumerate(VkFormat depth, unsigned color_limit, FormatFeatures features)
{
    std::vector<preset> result;
    if (!color_limit)
        return result;

    const bool has_depth = depth != VK_FORMAT_UNDEFINED &&
        (features(depth) & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
    for (const auto color : source_formats)
    {
        const auto supported = features(color);
        if (!(supported & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
            continue;

        result.push_back({color, VK_FORMAT_UNDEFINED, 1, false});
        if (!has_depth)
            continue;

        for (std::uint8_t count = 1; count <= std::min(color_limit, 4u); ++count)
            result.push_back({color, depth, count, false});

        if (color != VK_FORMAT_R32_SFLOAT && (supported & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT))
            result.push_back({color, depth, 1, true});
    }
    return result;
}
}
