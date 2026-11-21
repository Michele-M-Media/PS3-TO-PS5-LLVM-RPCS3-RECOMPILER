#include <vulkan/vulkan.h>

#include <array>
#include <cassert>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

#include <descriptor_sampler_fix.h>

int main()
{
	VkInstanceCreateInfo instance_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	VkInstance instance{};
	assert(vkCreateInstance(&instance_info, nullptr, &instance) == VK_SUCCESS);
	uint32_t physical_count = 1;
	VkPhysicalDevice physical{};
	const auto enumerate = vkEnumeratePhysicalDevices(instance, &physical_count, &physical);
	assert(enumerate == VK_SUCCESS || enumerate == VK_INCOMPLETE);
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
	queue.queueCount = 1;
	queue.pQueuePriorities = &priority;
	VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	device_info.queueCreateInfoCount = 1;
	device_info.pQueueCreateInfos = &queue;
	VkDevice device{};
	assert(vkCreateDevice(physical, &device_info, nullptr, &device) == VK_SUCCESS);

	VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	VkSampler sampler{};
	assert(vkCreateSampler(device, &sampler_info, nullptr, &sampler) == VK_SUCCESS);

	VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
	image_info.extent = {1, 1, 1};
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	VkImage image{};
	assert(vkCreateImage(device, &image_info, nullptr, &image) == VK_SUCCESS);
	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(device, image, &requirements);
	VkMemoryAllocateInfo memory_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	memory_info.allocationSize = requirements.size;
	while (!(requirements.memoryTypeBits & (1u << memory_info.memoryTypeIndex)))
		++memory_info.memoryTypeIndex;
	VkDeviceMemory memory{};
	assert(vkAllocateMemory(device, &memory_info, nullptr, &memory) == VK_SUCCESS);
	assert(vkBindImageMemory(device, image, memory, 0) == VK_SUCCESS);
	VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	view_info.image = image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = image_info.format;
	view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	VkImageView view{};
	assert(vkCreateImageView(device, &view_info, nullptr, &view) == VK_SUCCESS);

	VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
	VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	layout_info.bindingCount = 1;
	layout_info.pBindings = &binding;
	VkDescriptorSetLayout layout{};
	assert(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &layout) == VK_SUCCESS);
	VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
	VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 1;
	pool_info.pPoolSizes = &size;
	VkDescriptorPool pool{};
	assert(vkCreateDescriptorPool(device, &pool_info, nullptr, &pool) == VK_SUCCESS);
	VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	allocate.descriptorPool = pool;
	allocate.descriptorSetCount = 1;
	allocate.pSetLayouts = &layout;
	VkDescriptorSet set{};
	assert(vkAllocateDescriptorSets(device, &allocate, &set) == VK_SUCCESS);

	std::array<VkDescriptorImageInfo, 2> images{{
		{sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
		{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	}};
	VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
	write.dstSet = set;
	write.descriptorCount = images.size();
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = images.data();
	uint32_t writes = 0;
	auto submit = [&](std::span<const VkWriteDescriptorSet> patched)
	{
		assert(patched.size() == 1);
		assert(patched[0].pImageInfo[0].sampler == sampler);
		assert(patched[0].pImageInfo[1].sampler == sampler);
		assert(patched[0].pImageInfo[1].imageView == view);
		assert(patched[0].pImageInfo[1].imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		vkUpdateDescriptorSets(device, 1, patched.data(), 0, nullptr);
		++writes;
	};
	auto failure = [](uint32_t, uint32_t) { throw std::runtime_error("invalid sampler binding"); };
	vk::descriptor_sampler::submit({&write, 1}, [&] { return sampler; }, failure, submit);
	assert(writes == 1 && images[1].sampler == VK_NULL_HANDLE);

	images[1].sampler = sampler;
	vk::descriptor_sampler::submit({&write, 1}, []() -> VkSampler { assert(false); return {}; }, failure,
		[&](std::span<const VkWriteDescriptorSet> patched)
		{
			assert(patched.data() == &write);
			submit(patched);
		});
	assert(writes == 2);
	images[1].sampler = VK_NULL_HANDLE;
	try
	{
		vk::descriptor_sampler::submit({&write, 1}, []() -> VkSampler { return {}; }, failure, submit);
		assert(false);
	}
	catch (const std::runtime_error&)
	{
		assert(writes == 2);
	}
	images[1].imageView = VK_NULL_HANDLE;
	try
	{
		vk::descriptor_sampler::submit({&write, 1}, [&] { return sampler; }, failure, submit);
		assert(false);
	}
	catch (const std::runtime_error&)
	{
		assert(writes == 2);
	}

	vkDestroyDescriptorPool(device, pool, nullptr);
	vkDestroyDescriptorSetLayout(device, layout, nullptr);
	vkDestroySampler(device, sampler, nullptr);
	vkDestroyImageView(device, view, nullptr);
	vkDestroyImage(device, image, nullptr);
	vkFreeMemory(device, memory, nullptr);
	vkDestroyDevice(device, nullptr);
	vkDestroyInstance(instance, nullptr);
	std::cout << "VULKAN_DESCRIPTOR_SAMPLER_PASS\n";
}
