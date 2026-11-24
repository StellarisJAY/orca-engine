#include "vk_rendering_device.h"
#include "vk_internal.h"
#include "vk_format.h"
#include "vk_memory.h"
#include "rhi_validation.h"
#include "os/memory.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace
{
    void print_vk_error(const char *what, const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan %s failed: %s\n", what, error.what());
    }

    /// @brief staging buffer 扩容时的粒度：避免小步增长反复重建 VkBuffer/VkDeviceMemory。
    constexpr uint64_t STAGING_CAPACITY_ALIGNMENT = 4096;
} // namespace

/// @brief 创建缓冲区并为它分配、绑定独占显存（G-06：每资源一块 allocation）。
///
/// usage 映射到 VkBufferUsageFlags 后创建 VkBuffer，再按 vkGetBufferMemoryRequirements2
/// 的结果分配设备本地内存并绑定。创建后的内容未定义，必须随后用 queue_write_buffer
/// 写入（与纹理"创建 + queue_write_texture"的时序一致）。
GPUBufferHandle VkRenderingDevice::create_buffer(const GPUBufferDesc &desc)
{
    GPUBufferHandle buffer;

    std::string error;
    if (!rhi_validate_buffer_desc(desc, error))
    {
        std::printf("[ERROR] Vulkan create_buffer(%s): %s\n", desc.label.c_str(), error.c_str());
        return buffer;
    }

    // 设备能力上限：uniform / storage 缓冲的大小分别受 maxUniformBufferRange、
    // maxStorageBufferRange 限制（VUID-VkBufferCreateInfo-size-*）。
    const vk::PhysicalDeviceProperties properties = physical_device.getProperties();
    if ((desc.usage & GPUBufferUsage::UNIFORM) != 0 &&
        desc.size > static_cast<uint64_t>(properties.limits.maxUniformBufferRange))
    {
        std::printf("[ERROR] Vulkan create_buffer(%s): size %llu exceeds maxUniformBufferRange %u\n",
                    desc.label.c_str(), static_cast<unsigned long long>(desc.size),
                    properties.limits.maxUniformBufferRange);
        return buffer;
    }
    if ((desc.usage & GPUBufferUsage::STORAGE) != 0 &&
        desc.size > static_cast<uint64_t>(properties.limits.maxStorageBufferRange))
    {
        std::printf("[ERROR] Vulkan create_buffer(%s): size %llu exceeds maxStorageBufferRange %u\n",
                    desc.label.c_str(), static_cast<unsigned long long>(desc.size),
                    properties.limits.maxStorageBufferRange);
        return buffer;
    }

    VkBufferHandleData *data = nullptr;
    try
    {
        vk::BufferCreateInfo create_info;
        create_info.setSize(static_cast<vk::DeviceSize>(desc.size));
        create_info.setUsage(vk_buffer_usage_from_flags(desc.usage));
        // 只从图形队列使用：exclusive 共享模式，queueFamilyIndexCount 保持 0。
        create_info.setSharingMode(vk::SharingMode::eExclusive);

        data = memnew(VkBufferHandleData);
        data->buffer = device.createBuffer(create_info);

        // 与纹理一致：同时查询 dedicated 需求，驱动要求/倾向独占分配时把
        // VkMemoryDedicatedAllocateInfo 链进 vkAllocateMemory，避免 validation 报警。
        vk::BufferMemoryRequirementsInfo2 requirements_info;
        requirements_info.setBuffer(*data->buffer);
        auto requirements_chain =
            device.getBufferMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
                requirements_info);
        const vk::MemoryRequirements &memory_requirements =
            requirements_chain.get<vk::MemoryRequirements2>().memoryRequirements;
        const vk::MemoryDedicatedRequirements &dedicated_requirements =
            requirements_chain.get<vk::MemoryDedicatedRequirements>();

        vk::MemoryDedicatedAllocateInfo dedicated_info;
        dedicated_info.setBuffer(*data->buffer);
        const vk::MemoryDedicatedAllocateInfo *dedicated_info_ptr = nullptr;
        if (dedicated_requirements.requiresDedicatedAllocation || dedicated_requirements.prefersDedicatedAllocation)
            dedicated_info_ptr = &dedicated_info;

        data->memory = vk_memory::allocate_device_memory(device, memory_properties, memory_requirements,
                                                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                         dedicated_info_ptr);
        if (data->memory == nullptr)
        {
            std::printf("[ERROR] Vulkan create_buffer(%s): failed to allocate device memory\n",
                        desc.label.c_str());
            memdelete(data);
            data = nullptr;
            return {};
        }

        try
        {
            (*device).bindBufferMemory(*data->buffer, *data->memory, 0);
        }
        catch (const vk::SystemError &bind_error)
        {
            print_vk_error(("bind_buffer_memory(" + desc.label + ")").c_str(), bind_error);
            memdelete(data);
            data = nullptr;
            return {};
        }
        data->allocation_size = memory_requirements.size;

        data->size = desc.size;
        data->usage = desc.usage;
        data->label = desc.label;

        buffer.handle = alloc_handle_id();
        buffer.data = data;

        std::printf("[INFO] Vulkan created buffer(%s): %llu byte(s), usage 0x%02x, %.2f KiB\n",
                    desc.label.c_str(), static_cast<unsigned long long>(data->size), data->usage,
                    static_cast<double>(data->allocation_size) / 1024.0);
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(("create_buffer(" + desc.label + ")").c_str(), error);
        memdelete(data);
        return {};
    }
    return buffer;
}

/// @brief 保证 staging buffer 容量不小于 size。
///
/// 只增不减：容量向上取整到 4 KiB；需要扩容时创建新的 VkBuffer + host-visible 显存，
/// 然后按"先替换 buffer、再替换 memory"的顺序赋值，保证旧 buffer 先于它绑定的
/// 旧 memory 释放（Vulkan 要求）。
bool VkRenderingDevice::ensure_staging_capacity(uint64_t size, const char *what)
{
    if (size == 0)
    {
        std::printf("[ERROR] Vulkan %s: staging size is 0\n", what);
        return false;
    }
    if (staging_buffer.buffer != nullptr && staging_buffer.capacity >= size)
        return true;

    const uint64_t new_capacity =
        (size + STAGING_CAPACITY_ALIGNMENT - 1) / STAGING_CAPACITY_ALIGNMENT * STAGING_CAPACITY_ALIGNMENT;

    try
    {
        vk::raii::Buffer new_buffer =
            device.createBuffer(vk::BufferCreateInfo{vk::BufferCreateFlags{}, static_cast<vk::DeviceSize>(new_capacity),
                                                     vk::BufferUsageFlagBits::eTransferSrc});

        const vk::MemoryRequirements requirements = (*device).getBufferMemoryRequirements(*new_buffer);
        // staging 必须可被 CPU 映射；严格模式：不满足就报错，而不是映射一块不可写内存。
        vk::raii::DeviceMemory new_memory = vk_memory::allocate_device_memory(
            device, memory_properties, requirements,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, nullptr,
            /*allow_fallback=*/false);
        if (new_memory == nullptr)
        {
            std::printf("[ERROR] Vulkan %s: failed to allocate host-visible staging memory\n", what);
            return false;
        }
        (*device).bindBufferMemory(*new_buffer, *new_memory, 0);

        // 赋值顺序不能反：先替换 buffer（销毁旧 buffer），再替换 memory（销毁旧 memory）。
        staging_buffer.buffer = std::move(new_buffer);
        staging_buffer.memory = std::move(new_memory);
        staging_buffer.capacity = new_capacity;
        return true;
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(what, error);
        return false;
    }
}

/// @brief 写入缓冲区区间（staging → vkCmdCopyBuffer），同步提交。
///
/// 时序：与 queue_write_texture 相同的同步语义——临时录制命令并立即提交，等待 fence
/// 完成后才返回，因此写入一定先于之后提交的帧。要求缓冲区创建时带 COPY_DST 用途。
///
/// staging 是设备级复用的 host-visible buffer（见 ensure_staging_capacity），每次写入
/// 只做一次 map/memcpy/flush/unmap；写入区间由共享层 rhi_validate_buffer_write 校验。
void VkRenderingDevice::queue_write_buffer(GPUBufferHandle handle, uint64_t offset, const void *data, uint64_t size)
{
    if (!handle.is_valid() || handle.data == nullptr)
    {
        std::printf("[ERROR] Vulkan queue_write_buffer: invalid buffer handle\n");
        return;
    }
    auto buffer_data = static_cast<VkBufferHandleData *>(handle.data);

    if ((buffer_data->usage & GPUBufferUsage::COPY_DST) == 0)
    {
        std::printf("[ERROR] Vulkan queue_write_buffer(%s): buffer was not created with COPY_DST usage\n",
                    buffer_data->label.c_str());
        return;
    }
    if (data == nullptr)
    {
        std::printf("[ERROR] Vulkan queue_write_buffer(%s): data pointer is null\n", buffer_data->label.c_str());
        return;
    }

    std::string error;
    if (!rhi_validate_buffer_write(buffer_data->size, offset, size, error))
    {
        std::printf("[ERROR] Vulkan queue_write_buffer(%s): %s\n", buffer_data->label.c_str(), error.c_str());
        return;
    }

    const std::string what = "queue_write_buffer(" + buffer_data->label + ")";
    if (!ensure_staging_capacity(size, what.c_str()))
        return;

    try
    {
        void *mapped = staging_buffer.memory.mapMemory(0, static_cast<vk::DeviceSize>(size));
        std::memcpy(mapped, data, static_cast<size_t>(size));
        // 无论实际内存类型是否 coherent，刷一次都合法；非 coherent 时才真正生效。
        vk::MappedMemoryRange mapped_range;
        mapped_range.setMemory(*staging_buffer.memory);
        mapped_range.setOffset(0);
        mapped_range.setSize(VK_WHOLE_SIZE);
        device.flushMappedMemoryRanges(mapped_range);
        staging_buffer.memory.unmapMemory();

        const bool submitted = submit_immediate_commands(what.c_str(), [&](const vk::raii::CommandBuffer &command_buffer) {
            // host 写入 → transfer 读取：staging 内存由 CPU 写、由拷贝命令读。
            vk::MemoryBarrier host_to_transfer;
            host_to_transfer.setSrcAccessMask(vk::AccessFlagBits::eHostWrite);
            host_to_transfer.setDstAccessMask(vk::AccessFlagBits::eTransferRead);
            (*command_buffer).pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                                              vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{},
                                              host_to_transfer, {}, {});

            vk::BufferCopy copy_region;
            copy_region.setSrcOffset(0);
            copy_region.setDstOffset(static_cast<vk::DeviceSize>(offset));
            copy_region.setSize(static_cast<vk::DeviceSize>(size));
            (*command_buffer).copyBuffer(*staging_buffer.buffer, *buffer_data->buffer, copy_region);

            // transfer 写入 → 后续读取。目标阶段用 eAllCommands + MEMORY_READ/WRITE：
            // 缓冲区可能被 vertex/index/uniform 中的任意一种读取，调用方也不一定在同一个
            // submission 里使用它；等 V6 引入真正的帧提交后这条依赖仍然成立。
            vk::MemoryBarrier transfer_to_read;
            transfer_to_read.setSrcAccessMask(vk::AccessFlagBits::eTransferWrite);
            transfer_to_read.setDstAccessMask(vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite);
            (*command_buffer).pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                              vk::PipelineStageFlagBits::eAllCommands, vk::DependencyFlags{},
                                              transfer_to_read, {}, {});
        });

        if (!submitted)
            std::printf("[ERROR] Vulkan %s: failed to submit copy commands\n", what.c_str());
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error(what.c_str(), error);
    }
}

/// @brief 销毁缓冲区（memdelete 触发 RAII 析构，buffer 先于 memory 释放）。
///
/// 当前没有帧提交队列，立即销毁是安全的；V6 引入帧 fence 后需要改成"帧完成后延迟
/// 释放"（审计清单 D-15）。
void VkRenderingDevice::destroy_buffer(GPUBufferHandle handle)
{
    if (!handle.is_valid() || handle.data == nullptr)
        return;
    memdelete(static_cast<VkBufferHandleData *>(handle.data));
}
