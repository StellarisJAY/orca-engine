#include "vk_rendering_device.h"
#include "vk_format.h"

#include <cstdio>
#include <vector>

namespace
{
    void print_vk_error(const char *what, const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan %s failed: %s\n", what, error.what());
    }
} // namespace

/// @brief 呈现一帧并推进帧边界（V3-4/V6-3）。
///
/// 帧同步的三段式（与 `queue_submit` 配套）：
///   acquire（默认帧缓冲 begin 时）→ submit（queue_submit，等 image_available、
///   signal render_finished）→ present（本函数，等 render_finished、vkQueuePresentKHR）。
///
/// 本帧没有使用默认帧缓冲时（没有 acquire 交换链图像）不做呈现，只推进帧槽位；
/// 交换链 out-of-date 时重建交换链（重建会 waitIdle 并重建全部依赖资源与同步对象）。
/// 已 acquire 但本帧没有 submit 的异常情形：呈现未渲染内容把图像还给 presentation
/// engine，并重建 image_available semaphore（它仍处于 signaled，不能重复 signal）。
void VkRenderingDevice::present()
{
    if (device == nullptr || present_queue == nullptr)
    {
        std::printf("[ERROR] Vulkan present: device/present queue 尚未初始化\n");
        return;
    }

    const bool has_image = current_swapchain_image != UINT32_MAX;
    FrameSync *sync = (!frame_syncs.empty() && current_frame < frame_syncs.size())
                          ? &frame_syncs[current_frame]
                          : nullptr;

    try
    {
        if (!has_image)
        {
            std::printf("[INFO] Vulkan present: 本帧没有 acquire 交换链图像，跳过呈现\n");
        }
        else if (swapchain == nullptr || current_swapchain_image >= swapchain_images.size())
        {
            std::printf("[ERROR] Vulkan present: 当前交换链图像索引 %u 无效\n", current_swapchain_image);
            return;
        }
        else
        {
            std::vector<vk::Semaphore> wait_semaphores;
            if (frame_submitted && sync != nullptr)
            {
                wait_semaphores.push_back(*sync->render_finished);
            }
            else if (frame_submitted)
            {
                std::printf("[WARN] Vulkan present: 帧同步对象缺失，无法等待渲染完成\n");
            }
            else
            {
                // 已 acquire 但没有提交：图像内容仍是上一帧/未定义，直接呈现场景不会崩溃，
                // 但属于调用方漏了 queue_submit；上面的写回逻辑会重建 acquire semaphore。
                std::printf("[WARN] Vulkan present: 默认帧缓冲已 acquire 但本帧没有 queue_submit，"
                            "将呈现未渲染内容\n");
            }

            vk::PresentInfoKHR present_info;
            const vk::SwapchainKHR raw_swapchain = *swapchain;
            present_info.setWaitSemaphores(wait_semaphores);
            present_info.setSwapchains(raw_swapchain);
            present_info.setImageIndices(current_swapchain_image);

            // 走底层 vk::Queue::presentKHR（返回 Result 而不抛异常），便于区分
            // eErrorOutOfDateKHR 与 eSuboptimalKHR 两种可恢复状态。
            const vk::Result present_result = (*present_queue).presentKHR(present_info);
            if (present_result == vk::Result::eErrorOutOfDateKHR)
            {
                std::printf("[INFO] Vulkan present: 交换链 out-of-date，重建交换链\n");
                if (!init_swapchain())
                {
                    std::printf("[ERROR] Vulkan present: 交换链重建失败\n");
                    return;
                }
                // init_swapchain 已重建同步对象并重置 current_swapchain_image/current_frame。
                return;
            }
            if (present_result == vk::Result::eSuboptimalKHR)
            {
                std::printf("[WARN] Vulkan present: suboptimal KHR（交换链与 surface 尺寸不再完全匹配，"
                            "建议 resize_swapchain）\n");
            }
            else if (present_result != vk::Result::eSuccess)
            {
                std::printf("[ERROR] Vulkan present: vkQueuePresentKHR 返回 %d\n",
                            static_cast<int>(present_result));
                return;
            }
        }
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("present", error);
        return;
    }

    // 异常路径：acquire 成功但本帧没有提交，image_available semaphore 仍处于
    // signaled 状态；二进制 semaphore 不能重复 signal，重建它供下一帧 acquire 使用。
    if (has_image && !frame_submitted && sync != nullptr)
        sync->image_available = device.createSemaphore(vk::SemaphoreCreateInfo{});

    // 帧边界推进：present 之后交换链图像交还 presentation engine，下一帧必须重新 acquire；
    // 同时轮换 frame_syncs 槽位（MAX_FRAMES_IN_FLIGHT）。
    current_swapchain_image = UINT32_MAX;
    frame_submitted = false;
    frame_fence_reset = false;
    if (!frame_syncs.empty())
        current_frame = (current_frame + 1) % static_cast<uint32_t>(frame_syncs.size());
}

void VkRenderingDevice::shutdown()
{
    try
    {
        if (device != nullptr)
            device.waitIdle();
    }
    catch (const vk::SystemError &error)
    {
        std::printf("[WARN] Vulkan device waitIdle during shutdown failed: %s\n", error.what());
    }
}

void VkRenderingDevice::queue_wait_idle()
{
    try
    {
        if (device != nullptr)
            device.waitIdle();
    }
    catch (const vk::SystemError &error)
    {
        std::printf("[WARN] Vulkan device waitIdle failed: %s\n", error.what());
    }
}

uint64_t VkRenderingDevice::submit_frame()
{
    return ++frame_index;
}
