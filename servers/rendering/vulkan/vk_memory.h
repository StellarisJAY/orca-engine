#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <optional>

// =============================================================================
// Vulkan 显存分配（G-06 决策，2026-11）。
//
// 本期自建"每资源独占 allocation"：一个 VkImage/VkBuffer 对应一块
// vkAllocateMemory，不做内存块子分配。理由：
//   1. thirdparty/ 没有 VMA，新增源码或预编译库会扩大依赖面；
//   2. 当前资源数量少、单块尺寸由 vkGetImageMemoryRequirements 直接给出，
//      独占分配已经能让 validation layer 干净；
//   3. 分配入口集中在 vk_memory.*，以后改为内存块子分配或接入 VMA 时，
//      资源创建代码（vk_resource.cpp / vk_buffer.cpp）不需要改调用方式。
//
// 本模块只负责"选内存类型 + vkAllocateMemory"，不负责 bind 与生命周期；
// 绑定由资源对象完成，释放顺序由持有者的成员声明顺序保证（image 必须先于
// memory 销毁）。
// =============================================================================

namespace vk_memory
{
    /// @brief 在物理设备的内存类型里找第一个同时满足 type_bits 与 required 的索引。
    ///
    /// @param memory_properties 物理设备内存属性（vkGetPhysicalDeviceMemoryProperties）
    /// @param type_bits 资源的内存类型掩码（VkMemoryRequirements::memoryTypeBits）
    /// @param required 必须全部置位的内存属性
    /// @return 内存类型索引；没有兼容类型时返回 std::nullopt
    std::optional<uint32_t> find_memory_type_index(const vk::PhysicalDeviceMemoryProperties &memory_properties,
                                                   uint32_t type_bits,
                                                   vk::MemoryPropertyFlags required);

    /// @brief 为一块资源分配独占显存。
    ///
    /// 先按 required 找内存类型；找不到时降级到任意兼容类型并打印警告，
    /// 这样在个别没有设备本地内存的实现上创建资源不会直接失败。
    /// 注意：调用方必须保证 requirements 来自即将绑定的那个资源。
    ///
    /// @param dedicated_info 可选。驱动声明 requiresDedicatedAllocation/prefersDedicatedAllocation
    ///        时传入对应的 VkMemoryDedicatedAllocateInfo（pNext 链接），否则传 nullptr。
    /// @return 成功时返回 VkDeviceMemory；找不到内存类型或分配失败时返回 `{nullptr}`。
    vk::raii::DeviceMemory allocate_device_memory(const vk::raii::Device &device,
                                                  const vk::PhysicalDeviceMemoryProperties &memory_properties,
                                                  const vk::MemoryRequirements &requirements,
                                                  vk::MemoryPropertyFlags required,
                                                  const vk::MemoryDedicatedAllocateInfo *dedicated_info = nullptr);
} // namespace vk_memory
