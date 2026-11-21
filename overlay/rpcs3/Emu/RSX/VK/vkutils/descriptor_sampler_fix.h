#pragma once

#include <span>
#include <vector>

namespace vk::descriptor_sampler
{
	// The RSX layouts use mutable samplers. Keep the original writes and resource
	// lifetimes intact, and borrow the renderer's placeholder only for missing samplers.
	template <typename Fallback, typename Failure, typename Update>
	void submit(std::span<const VkWriteDescriptorSet> writes, Fallback fallback, Failure fail, Update update)
	{
		std::vector<VkWriteDescriptorSet> repaired;
		std::vector<std::vector<VkDescriptorImageInfo>> storage;
		VkSampler placeholder = VK_NULL_HANDLE;

		for (size_t i = 0; i < writes.size(); ++i)
		{
			const auto& write = writes[i];
			const bool combined = write.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			if (!combined && write.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLER)
				continue;
			if (!write.pImageInfo)
			{
				fail(write.dstBinding, write.dstArrayElement);
				return;
			}
			for (uint32_t j = 0; j < write.descriptorCount; ++j)
			{
				const auto& image = write.pImageInfo[j];
				if (combined && image.imageView == VK_NULL_HANDLE)
				{
					fail(write.dstBinding, write.dstArrayElement + j);
					return;
				}
				if (image.sampler != VK_NULL_HANDLE)
					continue;
				if (placeholder == VK_NULL_HANDLE)
					placeholder = fallback();
				if (placeholder == VK_NULL_HANDLE)
				{
					fail(write.dstBinding, write.dstArrayElement + j);
					return;
				}
				if (repaired.empty())
					repaired.assign(writes.begin(), writes.end());
				if (repaired[i].pImageInfo == write.pImageInfo)
				{
					storage.emplace_back(write.pImageInfo, write.pImageInfo + write.descriptorCount);
					repaired[i].pImageInfo = storage.back().data();
				}
				storage.back()[j].sampler = placeholder;
			}
		}

		update(repaired.empty() ? writes : std::span<const VkWriteDescriptorSet>{repaired});
	}
}
