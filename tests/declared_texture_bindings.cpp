#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using u32 = std::uint32_t;
constexpr u32 umax = ~u32{};
constexpr int VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT = 0, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT = 0;
constexpr int VK_IMAGE_ASPECT_STENCIL_BIT = 0, VK_IMAGE_ASPECT_DEPTH_BIT = 0;
constexpr int VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER = 0, VK_FALSE = 0;
constexpr int VK_FILTER_NEAREST = 0, VK_SAMPLER_MIPMAP_MODE_NEAREST = 0, VK_BORDER_COLOR_INT_OPAQUE_BLACK = 0;
constexpr int RSX_SHADER_CONTROL_EMULATE_DEPTH_COMPARE = 1;
using VkImageViewType = int;

namespace vk {
struct viewable_image;
struct image_view {
    viewable_image* image() const;
};
struct viewable_image {
    image_view* get_view(int, int) { return &view; }
    image_view view;
};
viewable_image backing;
image_view* fallback = &backing.view;
viewable_image* image_view::image() const { return &backing; }
struct sampler {
    template <typename... Args> explicit sampler(Args...) {}
};
sampler neutral;
inline sampler& null_sampler() { return neutral; }
inline image_view* null_image_view(int, int) { return fallback; }
inline int get_view_type(int type) { return type; }
namespace glsl { constexpr int binding_set_index_vertex = 0, binding_set_index_fragment = 1; }
struct texture_cache {
    struct sampled_image_descriptor {
        image_view* image_handle = nullptr;
        int external_subresource_desc = 0;
        bool validate() const { return image_handle != nullptr; }
    };
    image_view* create_temporary_subresource(int, int) { return nullptr; }
};
}

namespace rsx {
constexpr int default_remap_vector = 0;
struct texture {
    bool active = false;
    bool enabled() const { return active; }
};
struct {
    std::array<texture, 16> fragment_textures;
    std::array<texture, 4> vertex_textures;
} method_registers;
}

struct VkDescriptorImageInfoEx { vk::image_view& view; vk::sampler& sampler; };
struct program_mock {
    std::vector<u32> bound;
    void bind_uniform(const VkDescriptorImageInfoEx&, u32 set, u32 binding)
    {
        assert(binding != umax);
        bound.push_back(100 * set + binding);
    }
};
struct bindings {
    u32 ftex_location[16];
    u32 ftex_stencil_location[16];
    u32 vtex_location[4];
    u32 frag_depth_input_location = umax;
};
struct shader_data {
    struct { int redirected_textures = 0; } texture_state;
    int ctrl = 0;
    int get_texture_dimension(u32) const { return 0; }
};
struct {
    template <typename... Args> void error(Args...) {}
} rsx_log;
template <typename T> T ensure(T value) { assert(value); return value; }

struct VKGSRender {
    struct { u32 referenced_textures_mask = 0; } current_fp_metadata, current_vp_metadata;
    program_mock* m_program = nullptr;
    bindings* m_fs_binding_table = nullptr;
    bindings* m_vs_binding_table = nullptr;
    std::array<std::unique_ptr<vk::texture_cache::sampled_image_descriptor>, 16> fs_sampler_state;
    std::array<std::unique_ptr<vk::texture_cache::sampled_image_descriptor>, 4> vs_sampler_state;
    std::array<vk::sampler*, 16> fs_sampler_handles{};
    std::array<vk::sampler*, 4> vs_sampler_handles{};
    shader_data current_fragment_program, current_vertex_program;
    int* m_current_command_buffer = nullptr;
    int* m_device = nullptr;
    struct { std::pair<int, vk::viewable_image*> m_bound_depth_stencil; } m_rtts;
    vk::texture_cache m_texture_cache;
    std::unique_ptr<vk::sampler> m_stencil_mirror_sampler;
    void validate_image_layout_for_read_access(int, vk::image_view*, int, vk::texture_cache::sampled_image_descriptor*) {}
    bool bind_texture_env();
};

#include "bind_texture_env.inc"

int main()
{
    int object = 0;
    program_mock program;
    bindings declared;
    for (auto& slot : declared.ftex_location) slot = umax;
    for (auto& slot : declared.ftex_stencil_location) slot = umax;
    for (auto& slot : declared.vtex_location) slot = umax;
    declared.ftex_location[2] = 4;
    declared.ftex_stencil_location[2] = 5;
    declared.vtex_location[1] = 7;
    VKGSRender renderer;
    renderer.m_program = &program;
    renderer.m_fs_binding_table = &declared;
    renderer.m_vs_binding_table = &declared;
    renderer.m_current_command_buffer = &object;
    renderer.m_device = &object;
    renderer.current_fp_metadata.referenced_textures_mask = 0b101;
    assert(!renderer.bind_texture_env());
    assert((program.bound == std::vector<u32>{104, 105, 7}));

    program.bound.clear();
    rsx::method_registers.fragment_textures[2].active = true;
    renderer.fs_sampler_state[2] = std::make_unique<vk::texture_cache::sampled_image_descriptor>();
    renderer.fs_sampler_state[2]->image_handle = vk::fallback;
    // The image was uploaded while its sampler was unavailable.
    assert(!renderer.bind_texture_env());
    assert((program.bound == std::vector<u32>{104, 105, 7}));
    program.bound.clear();
    renderer.fs_sampler_handles[2] = &vk::neutral;
    renderer.current_fragment_program.texture_state.redirected_textures = 1 << 2;
    assert(!renderer.bind_texture_env());
    assert((program.bound == std::vector<u32>{104, 105, 7}));
}
