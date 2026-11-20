#pragma once

#include "recompiler_shader_warmup_cache.h"
#include "recompiler_feedback_pass.h"
#include "Emu/cache_utils.hpp"
#include "Emu/System.h"
#include "Emu/system_config.h"
#include "Utilities/File.h"

#include <bit>

namespace vk::recompiler_warmup
{
	using enum program_common::interpreter::compiler_option;
	using recompiler_shader_warmup::profile;

	// Vulkan structs are copied individually and all process pointers are excluded.
	struct record
	{
		u64 options;
		u64 renderpass;
		VkPipelineInputAssemblyStateCreateInfo input;
		VkPipelineDepthStencilStateCreateInfo depth;
		VkPipelineColorBlendAttachmentState attachments[4];
		VkPipelineColorBlendStateCreateInfo blend;
		VkPipelineRasterizationStateCreateInfo raster;
		VkPipelineMultisampleStateCreateInfo multisample;
		VkSampleMask sample_mask;

		vk::pipeline_props properties() const
		{
			vk::pipeline_props result{};
			result.renderpass_key = renderpass;
			result.state.ia = input;
			result.state.ds = depth;
			std::copy(std::begin(attachments), std::end(attachments), std::begin(result.state.att_state));
			result.state.cs = blend;
			result.state.cs.pAttachments = result.state.att_state;
			result.state.rs = raster;
			result.state.ms = multisample;
			result.state.temp_storage.msaa_sample_mask = sample_mask;
			return result;
		}

		bool operator==(const record& other) const
		{
			if (std::memcmp(this, &other, sizeof(record)) == 0)
				return true;
			return options == other.options && properties() == other.properties();
		}
	};
	static_assert(std::is_trivially_copyable_v<record>);

	inline bool valid_record(const record& data, const vk::render_device& device)
	{
		constexpr u64 valid_options = COMPILER_OPT_ALL_VS_MASK | COMPILER_OPT_ALL_FS_MASK;
		if ((data.options & ~valid_options) || std::popcount(data.options & COMPILER_OPT_ALPHA_TEST_MASK) > 1 ||
			data.blend.attachmentCount > 4 || data.input.pNext || data.depth.pNext || data.blend.pNext ||
			data.raster.pNext || data.multisample.pNext || data.blend.pAttachments || data.multisample.pSampleMask ||
			data.input.sType != VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO ||
			data.depth.sType != VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO ||
			data.blend.sType != VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO ||
			data.raster.sType != VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO ||
			data.multisample.sType != VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO ||
			data.multisample.rasterizationSamples != VK_SAMPLE_COUNT_1_BIT || ((data.renderpass >> 16) & 0x3f) != 1 ||
			(data.renderpass >> 37) || (data.depth.depthBoundsTestEnable && !device.get_depth_bounds_support()))
		{
			return false;
		}

		// Validate render states before creating Vulkan pipelines from the journal.
		const auto color = static_cast<VkFormat>(data.renderpass & 0xff);
		const auto depth = static_cast<VkFormat>((data.renderpass >> 8) & 0xff);
		if (color != VK_FORMAT_UNDEFINED && color != VK_FORMAT_B8G8R8A8_UNORM &&
			color != VK_FORMAT_R16G16B16A16_SFLOAT && color != VK_FORMAT_R32_SFLOAT)
			return false;
		if (depth != VK_FORMAT_UNDEFINED && depth != VK_FORMAT_D24_UNORM_S8_UINT && depth != VK_FORMAT_D32_SFLOAT_S8_UINT)
			return false;
		if ((data.blend.attachmentCount > 0) != (color != VK_FORMAT_UNDEFINED) ||
			(color == VK_FORMAT_UNDEFINED && depth == VK_FORMAT_UNDEFINED))
			return false;
		for (unsigned i = 0; i < 5; ++i)
		{
			const u64 layout = (data.renderpass >> (22 + 3 * i)) & 7;
			const auto attachment_count = data.blend.attachmentCount + (depth != VK_FORMAT_UNDEFINED);
			if ((i < attachment_count) != (layout != 0))
				return false;
			if (layout > 3 && (layout != recompiler_feedback::feedback_layout || !device.get_framebuffer_loops_support()))
				return false;
		}
		if (color == VK_FORMAT_R32_SFLOAT &&
			std::any_of(data.attachments, data.attachments + data.blend.attachmentCount,
				[](const auto& attachment) { return attachment.blendEnable; }) &&
			!(device.get_format_properties(color).optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT))
			return false;
		return (color == VK_FORMAT_UNDEFINED || (device.get_format_properties(color).optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) &&
			(depth == VK_FORMAT_UNDEFINED || (device.get_format_properties(depth).optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT));
	}

	inline record from_pipeline(u64 options, const vk::pipeline_props& props)
	{
		record data{};
		data.options = options;
		data.renderpass = props.renderpass_key;
		data.input = props.state.ia;
		data.depth = props.state.ds;
		std::copy(std::begin(props.state.att_state), std::end(props.state.att_state), std::begin(data.attachments));
		data.blend = props.state.cs;
		data.blend.pAttachments = nullptr;
		data.raster = props.state.rs;
		data.multisample = props.state.ms;
		data.sample_mask = props.state.temp_storage.msaa_sample_mask;
		return data;
	}

	struct journal
	{
		std::string path;
		profile device_profile{};
		std::vector<record> records;
		std::vector<record> warmed;

		void open(const vk::render_device& device)
		{
			*this = {};
			if (Emu.GetTitleID().empty() || g_cfg.video.disable_on_disk_shader_cache)
				return;
			const std::string cache = rpcs3::cache::get_ppu_cache();
			if (cache.empty())
				return;
			path = cache + "recompiler-rsx-warmup-v1.bin";
			VkPhysicalDeviceProperties properties{};
			vkGetPhysicalDeviceProperties(device.gpu(), &properties);
			device_profile = {properties.vendorID, properties.deviceID, properties.driverVersion, properties.apiVersion};
			static_assert(VK_UUID_SIZE == 4 * sizeof(std::uint32_t));
			std::memcpy(device_profile.data() + 4, properties.pipelineCacheUUID, VK_UUID_SIZE);
			const fs::file file(path);
			if (!file)
				return;
			const auto size = file.size();
			constexpr auto limit = sizeof(recompiler_shader_warmup::file_header) + sizeof(record) * recompiler_shader_warmup::capacity;
			if (size > limit)
			{
				rsx_log.warning("RECOMPILER_SHADER_WARMUP: journal too large");
				return;
			}
			std::vector<std::byte> bytes(size);
			if (file.read(bytes.data(), size) != size)
				return;
			if (auto decoded = recompiler_shader_warmup::decode<record>(device_profile, bytes))
			{
				for (const auto& item : *decoded)
				{
					if (valid_record(item, device))
						records.push_back(item);
				}
			}
			else
			{
				rsx_log.warning("RECOMPILER_SHADER_WARMUP: incompatible or damaged journal");
			}
		}

		void learn(u64 options, const vk::pipeline_props& props, s64 elapsed_us, const vk::render_device& device)
		{
			if (path.empty() || elapsed_us < recompiler_shader_warmup::learn_threshold_us)
				return;
			const auto item = from_pipeline(options, props);
			if (!valid_record(item, device) || !recompiler_shader_warmup::remember(records, item, elapsed_us))
				return;
			Emu.CallFromMainThread([file_path = path, bytes = recompiler_shader_warmup::encode<record>(device_profile, records), options, key = item.renderpass]
			{
				fs::pending_file file(file_path);
				if (file.file && file.file.write(bytes.data(), bytes.size()) == bytes.size() && file.commit())
					rsx_log.notice("RECOMPILER_SHADER_WARMUP: saved flags=0x%llx renderpass=0x%llx", options, key);
				else
					rsx_log.warning("RECOMPILER_SHADER_WARMUP: journal write failed (%s)", fs::g_tls_error);
			}, nullptr, false);
		}

		bool begin_preload() const
		{
			const std::string marker = path + ".in-progress";
			if (fs::is_file(marker))
			{
				rsx_log.warning("RECOMPILER_SHADER_WARMUP: retained journal after interrupted preload");
				return false;
			}
			fs::pending_file file(marker);
			constexpr char pending = '1';
			if (file.file && file.file.write(&pending, sizeof(pending)) == sizeof(pending) && file.commit(false))
				return true;
			rsx_log.warning("RECOMPILER_SHADER_WARMUP: could not mark preload (%s)", fs::g_tls_error);
			return false;
		}

		void end_preload() const
		{
			if (!fs::remove_file(path + ".in-progress"))
				rsx_log.warning("RECOMPILER_SHADER_WARMUP: could not release preload marker (%s)", fs::g_tls_error);
		}

		void log_hit(u64 options, const vk::pipeline_props& props)
		{
			const auto found = std::find_if(warmed.begin(), warmed.end(), [&](const record& item)
			{
				return item.options == options && item.renderpass == props.renderpass_key && item.properties() == props;
			});
			if (found != warmed.end())
			{
				rsx_log.notice("RECOMPILER_SHADER_WARMUP: hit flags=0x%llx renderpass=0x%llx", options, props.renderpass_key);
				warmed.erase(found);
			}
		}
	};
}
