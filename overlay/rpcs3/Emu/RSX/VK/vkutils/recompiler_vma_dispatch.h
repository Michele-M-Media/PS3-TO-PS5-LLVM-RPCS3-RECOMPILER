#pragma once

#include <vk_mem_alloc.h>

inline bool recompiler_bind_vma_dispatch(VmaAllocatorCreateInfo& creation, VmaVulkanFunctions& functions,
                                   PFN_vkGetInstanceProcAddr instance_proc,
                                   PFN_vkGetDeviceProcAddr device_proc)
{
    if (!creation.instance || !creation.physicalDevice || !creation.device || !instance_proc || !device_proc)
        return false;
    functions = {};
    functions.vkGetInstanceProcAddr = instance_proc;
    functions.vkGetDeviceProcAddr = device_proc;
    creation.pVulkanFunctions = &functions;
    return true;
}
