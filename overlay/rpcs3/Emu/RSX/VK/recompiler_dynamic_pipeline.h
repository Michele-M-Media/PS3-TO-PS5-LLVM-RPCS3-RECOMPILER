#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace vk::recompiler_dynamic {
struct commands {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkCmdSetPrimitiveTopologyEXT topology{};
    PFN_vkCmdSetCullModeEXT cull{};
    PFN_vkCmdSetFrontFaceEXT front{};
    PFN_vkCmdSetDepthWriteEnableEXT depth_write{};
    PFN_vkCmdSetColorBlendEnableEXT blend{};
    PFN_vkCmdSetColorBlendEquationEXT equation{};
    PFN_vkCmdSetColorWriteMaskEXT mask{};

    explicit operator bool() const
    {
        return device && topology && cull && front && depth_write && blend && equation && mask;
    }
};

inline commands draw_commands;

inline constexpr VkDynamicState states[]{
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT,
    VK_DYNAMIC_STATE_CULL_MODE_EXT,
    VK_DYNAMIC_STATE_FRONT_FACE_EXT,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,
    VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT,
    VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT,
    VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,
};
inline constexpr unsigned depth_only_state_count = 4;

inline bool ordinary_blend(VkBlendFactor factor)
{
    return factor == VK_BLEND_FACTOR_ZERO || factor == VK_BLEND_FACTOR_ONE ||
        factor == VK_BLEND_FACTOR_SRC_ALPHA || factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
}

template <typename Pipeline>
bool eligible(const Pipeline& pipe)
{
    const auto key = pipe.renderpass_key;
    const auto& state = pipe.state;
    const auto color = static_cast<VkFormat>(key & 0xff);
    const auto depth = static_cast<VkFormat>((key >> 8) & 0xff);
    const auto layouts = (key >> 22) & 0x7fff;
    const auto topology = state.ia.topology;
    const bool ordinary_depth = depth == VK_FORMAT_D24_UNORM_S8_UINT || depth == VK_FORMAT_D32_SFLOAT_S8_UINT;
    const bool depth_only = ordinary_depth && color == VK_FORMAT_UNDEFINED && state.cs.attachmentCount == 0 &&
        (layouts == 3 || layouts == 4);
    const bool single_color = color == VK_FORMAT_R16G16B16A16_SFLOAT || color == VK_FORMAT_B8G8R8A8_UNORM ||
        color == VK_FORMAT_R32_SFLOAT;
    const bool color_layout = (depth == VK_FORMAT_UNDEFINED && layouts == 2) ||
        (ordinary_depth && (layouts == 26 || layouts == 28 || layouts == 34));
    const bool with_color = single_color && state.cs.attachmentCount == 1 && color_layout;
    if ((!depth_only && !with_color) ||
        ((key >> 16) & 0x3f) != 1 || (key >> 37) ||
        (topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP) ||
        state.ia.primitiveRestartEnable || state.cs.logicOpEnable ||
        (with_color && state.ds.stencilTestEnable) || state.rs.rasterizerDiscardEnable)
        return false;

    if (depth_only)
        return true;
    const auto& blend = state.att_state[0];
    return !blend.blendEnable || (blend.colorBlendOp == VK_BLEND_OP_ADD && blend.alphaBlendOp == VK_BLEND_OP_ADD &&
        ordinary_blend(blend.srcColorBlendFactor) && ordinary_blend(blend.dstColorBlendFactor) &&
        ordinary_blend(blend.srcAlphaBlendFactor) && ordinary_blend(blend.dstAlphaBlendFactor));
}

template <typename Pipeline>
Pipeline canonical(Pipeline pipe, bool enabled)
{
    if (enabled && eligible(pipe))
    {
        auto& state = pipe.state;
        state.ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        state.rs.cullMode = VK_CULL_MODE_NONE;
        state.rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        state.ds.depthWriteEnable = VK_FALSE;
        if (state.cs.attachmentCount)
            state.att_state[0] = {};
    }
    return pipe;
}

template <typename Pipeline>
void apply(VkCommandBuffer buffer, const Pipeline& original)
{
    if (!draw_commands || !eligible(original))
        return;
    const auto& state = original.state;
    draw_commands.topology(buffer, state.ia.topology);
    draw_commands.cull(buffer, state.rs.cullMode);
    draw_commands.front(buffer, state.rs.frontFace);
    draw_commands.depth_write(buffer, state.ds.depthWriteEnable);
    if (!state.cs.attachmentCount)
        return;
    const auto& blend = state.att_state[0];
    const VkColorBlendEquationEXT equation{
        blend.srcColorBlendFactor, blend.dstColorBlendFactor, blend.colorBlendOp,
        blend.srcAlphaBlendFactor, blend.dstAlphaBlendFactor, blend.alphaBlendOp,
    };
    draw_commands.blend(buffer, 0, 1, &blend.blendEnable);
    draw_commands.equation(buffer, 0, 1, &equation);
    draw_commands.mask(buffer, 0, 1, &blend.colorWriteMask);
}
}
