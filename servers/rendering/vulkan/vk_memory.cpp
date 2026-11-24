#include "vk_memory.h"

#include <cstdio>

namespace vk_memory
{
    std::optional<uint32_t> find_memory_type_index(const vk::PhysicalDeviceMemoryProperties &memory_properties,
                                                   uint32_t type_bits,
                                                   vk::MemoryPropertyFlags required)
    {
        for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
        {
            if ((type_bits & (1u << i)) == 0)
                continue;
            if ((memory_properties.memoryTypes[i].propertyFlags & required) != required)
                continue;
            return i;
        }
        return std::nullopt;
    }

    vk::raii::DeviceMemory allocate_device_memory(const vk::raii::Device &device,
                                                  const vk::PhysicalDeviceMemoryProperties &memory_properties,
                                                  const vk::MemoryRequirements &requirements,
                                                  vk::MemoryPropertyFlags required,
                                                  const vk::MemoryDedicatedAllocateInfo *dedicated_info,
                                                  bool allow_fallback)
    {
        std::optional<uint32_t> memory_type_index =
            find_memory_type_index(memory_properties, requirements.memoryTypeBits, required);
        if (!memory_type_index && allow_fallback)
        {
            // 常见的 required 是 eDeviceLocal；个别软件/移动实现没有完全匹配的类型，
            // 这里降级为"只要类型位兼容就分配"，避免资源创建直接失败。
            std::printf("[WARN] Vulkan: no memory type matches required flags, falling back to any compatible type\n");
            memory_type_index = find_memory_type_index(memory_properties, requirements.memoryTypeBits, {});
        }
        if (!memory_type_index)
        {
            std::printf("[ERROR] Vulkan: no compatible memory type (memoryTypeBits=0x%x)\n", requirements.memoryTypeBits);
            return vk::raii::DeviceMemory{nullptr};
        }

        vk::MemoryAllocateInfo allocate_info;
        allocate_info.allocationSize = requirements.size;
        allocate_info.memoryTypeIndex = *memory_type_index;
        if (dedicated_info != nullptr)
            allocate_info.setPNext(dedicated_info);

        try
        {
            return device.allocateMemory(allocate_info);
        }
        catch (const vk::SystemError &error)
        {
            std::printf("[ERROR] Vulkan allocate memory failed: %s\n", error.what());
            return vk::raii::DeviceMemory{nullptr};
        }
    }
} // namespace vk_memory
