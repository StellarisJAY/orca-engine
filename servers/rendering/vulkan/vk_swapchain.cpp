#include "vk_rendering_device.h"
#include "vk_format.h"
#include "vk_memory.h"
#include "rhi_validation.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <vector>

namespace
{
    void print_vk_error(const char *what, const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan %s failed: %s\n", what, error.what());
    }

    /// @brief 选择内部深度缓冲的 Vulkan 格式（G-07）。
    ///
    /// 优先 `eD32Sfloat`：接口 `Format::Depth32Float` 能表达它，绝大多数实现都支持；
    /// 其次 `D24S8` / `D32SfloatS8`（只作为后端内部资源存在，不要求能被接口 Format 表达）。
    /// 候选格式都要求 optimalTiling 支持深度/模板附件。
    vk::Format choose_swapchain_depth_format(const vk::raii::PhysicalDevice &physical_device)
    {
        const std::array<vk::Format, 3> candidates = {
            vk::Format::eD32Sfloat,
            vk::Format::eD24UnormS8Uint,
            vk::Format::eD32SfloatS8Uint,
        };

        for (vk::Format candidate : candidates)
        {
            const vk::FormatProperties properties = physical_device.getFormatProperties(candidate);
            if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
                return candidate;
        }
        return vk::Format::eUndefined;
    }

    /// @brief 深度格式对应的 image aspect：纯深度格式只有 depth，深度模板格式要带 stencil。
    vk::ImageAspectFlags depth_format_aspect(vk::Format format)
    {
        switch (format)
        {
        case vk::Format::eD16UnormS8Uint:
        case vk::Format::eD24UnormS8Uint:
        case vk::Format::eD32SfloatS8Uint:
            return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        default:
            return vk::ImageAspectFlagBits::eDepth;
        }
    }
} // namespace

Format VkRenderingDevice::get_swapchain_format() const
{
    Format format = Format::Undefined;
    if (!vk_format_from_vulkan(swapchain_image_format, format))
    {
        std::printf("[WARN] Vulkan swapchain format is not representable by interface Format\n");
        return Format::Undefined;
    }
    return format;
}

void VkRenderingDevice::get_swapchain_size(uint32_t &width, uint32_t &height) const
{
    if (swapchain_image_extent.width != 0 && swapchain_image_extent.height != 0)
    {
        width = swapchain_image_extent.width;
        height = swapchain_image_extent.height;
        return;
    }
    width = window_info.width;
    height = window_info.height;
}

SwapChainSupportDetails VkRenderingDevice::query_swapchain_support(const vk::raii::PhysicalDevice &physicalDevice) const
{
    SwapChainSupportDetails details;
    details.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    details.formats = physicalDevice.getSurfaceFormatsKHR(surface);
    details.presentModes = physicalDevice.getSurfacePresentModesKHR(surface);
    return details;
}

/// @brief 选择窗口输出格式。
///
/// 优先返回"接口 Format 能表达"的 sRGB 格式（这样管线/渲染目标的格式可以
/// 与交换链实际格式保持一致），其次返回可表达的非 sRGB 格式，最后才回退到
/// 驱动给出的第一个格式并打印警告。
static vk::SurfaceFormatKHR choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR> &available_formats)
{
    if (available_formats.empty())
        return {};

    vk::SurfaceFormatKHR fallback_representable = available_formats[0];
    bool has_representable = false;

    for (const auto &available_format : available_formats)
    {
        Format orca_format = Format::Undefined;
        if (!vk_format_from_vulkan(available_format.format, orca_format))
            continue;
        if (available_format.colorSpace != vk::ColorSpaceKHR::eSrgbNonlinear)
            continue;

        if (rhi_format_is_srgb(orca_format))
            return available_format;

        if (!has_representable)
        {
            fallback_representable = available_format;
            has_representable = true;
        }
    }

    if (has_representable)
        return fallback_representable;
    return available_formats[0];
}

/// @brief 选择窗口的呈现模式，优先三缓冲垂直同步，否则FIFO垂直同步
/// @param available_present_modes 设备可用的模式
/// @return 最佳模式
static vk::PresentModeKHR choose_swap_surface_present_mode(const std::vector<vk::PresentModeKHR> &available_present_modes)
{
    // 优先选mailbox，三缓冲垂直同步模式
    for (const auto &availablePresentMode : available_present_modes)
    {
        if (availablePresentMode == vk::PresentModeKHR::eMailbox)
        {
            return availablePresentMode;
        }
    }
    // 没有mailbox，回退到FIFO垂直同步
    return vk::PresentModeKHR::eFifo;
}

/// @brief
/// @param capabilities
/// @return
vk::Extent2D VkRenderingDevice::choose_swap_extent(const vk::SurfaceCapabilitiesKHR &capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }
    // framebuffer size
    vk::Extent2D actualExtent(window_info.width, window_info.height);

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}

/// @brief 创建每张交换链图像的 color image view（默认帧缓冲的颜色附件）。
bool VkRenderingDevice::create_swapchain_image_views()
{
    if (swapchain_images.empty())
    {
        std::printf("[ERROR] Vulkan create_swapchain_image_views: swapchain has no images\n");
        return false;
    }

    try
    {
        swapchain_image_views.clear();
        swapchain_image_views.reserve(swapchain_images.size());

        for (vk::Image image : swapchain_images)
        {
            vk::ImageViewCreateInfo create_info;
            create_info.setImage(image);
            create_info.setViewType(vk::ImageViewType::e2D);
            create_info.setFormat(swapchain_image_format);

            vk::ImageSubresourceRange range;
            range.setAspectMask(vk::ImageAspectFlagBits::eColor);
            range.setBaseMipLevel(0);
            range.setLevelCount(1);
            range.setBaseArrayLayer(0);
            range.setLayerCount(1);
            create_info.setSubresourceRange(range);

            // 用 emplace_back 就地构造 RAII 对象，异常时已创建的 view 由 vector 自动释放。
            swapchain_image_views.emplace_back(device, create_info);
        }
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_swapchain_image_views", error);
        return false;
    }
    return true;
}

/// @brief 为每张交换链图像创建一份深度缓冲（image + view + 独占显存，G-07）。
bool VkRenderingDevice::create_swapchain_depth_buffers()
{
    const vk::Format depth_format = choose_swapchain_depth_format(physical_device);
    if (depth_format == vk::Format::eUndefined)
    {
        std::printf("[ERROR] Vulkan create_swapchain_depth_buffers: no depth-stencil format supported\n");
        return false;
    }

    const vk::ImageAspectFlags depth_aspect = depth_format_aspect(depth_format);
    vk::DeviceSize total_allocation = 0;

    try
    {
        swapchain_depths.clear();
        swapchain_depths.reserve(swapchain_images.size());

        for (size_t i = 0; i < swapchain_images.size(); ++i)
        {
            swapchain_depths.emplace_back();
            SwapchainDepth &depth = swapchain_depths.back();

            vk::ImageCreateInfo create_info;
            create_info.setImageType(vk::ImageType::e2D);
            create_info.setFormat(depth_format);
            create_info.setExtent(
                vk::Extent3D{swapchain_image_extent.width, swapchain_image_extent.height, 1});
            create_info.setMipLevels(1);
            create_info.setArrayLayers(1);
            create_info.setSamples(vk::SampleCountFlagBits::e1);
            create_info.setTiling(vk::ImageTiling::eOptimal);
            create_info.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment);
            create_info.setSharingMode(vk::SharingMode::eExclusive);
            create_info.setInitialLayout(vk::ImageLayout::eUndefined);

            depth.image = device.createImage(create_info);

            // 与 vk_resource.cpp 的纹理分配相同：按 image 自身的内存需求选类型位，
            // 驱动要求/倾向独占分配时把 VkMemoryDedicatedAllocateInfo 链进分配调用。
            vk::ImageMemoryRequirementsInfo2 requirements_info;
            requirements_info.setImage(*depth.image);
            const auto requirements_chain =
                device.getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
                    requirements_info);
            const vk::MemoryRequirements &memory_requirements =
                requirements_chain.get<vk::MemoryRequirements2>().memoryRequirements;
            const vk::MemoryDedicatedRequirements &dedicated_requirements =
                requirements_chain.get<vk::MemoryDedicatedRequirements>();

            vk::MemoryDedicatedAllocateInfo dedicated_info;
            dedicated_info.setImage(*depth.image);
            const vk::MemoryDedicatedAllocateInfo *dedicated_info_ptr = nullptr;
            if (dedicated_requirements.requiresDedicatedAllocation ||
                dedicated_requirements.prefersDedicatedAllocation)
                dedicated_info_ptr = &dedicated_info;

            depth.memory = vk_memory::allocate_device_memory(device, memory_properties, memory_requirements,
                                                             vk::MemoryPropertyFlagBits::eDeviceLocal,
                                                             dedicated_info_ptr);
            if (depth.memory == nullptr)
            {
                std::printf("[ERROR] Vulkan create_swapchain_depth_buffers: failed to allocate depth memory "
                            "for image %zu\n",
                            i);
                return false;
            }

            (*device).bindImageMemory(*depth.image, *depth.memory, 0);
            depth.allocation_size = memory_requirements.size;
            total_allocation += depth.allocation_size;

            vk::ImageViewCreateInfo view_info;
            view_info.setImage(*depth.image);
            view_info.setViewType(vk::ImageViewType::e2D);
            view_info.setFormat(depth_format);

            vk::ImageSubresourceRange range;
            range.setAspectMask(depth_aspect);
            range.setBaseMipLevel(0);
            range.setLevelCount(1);
            range.setBaseArrayLayer(0);
            range.setLayerCount(1);
            view_info.setSubresourceRange(range);

            depth.view = device.createImageView(view_info);
        }
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_swapchain_depth_buffers", error);
        return false;
    }

    swapchain_depth_format = depth_format;
    std::printf("[INFO] Vulkan created swapchain depth: format=%d, %zu image(s), %.2f KiB\n",
                static_cast<int>(depth_format), swapchain_depths.size(),
                static_cast<double>(total_allocation) / 1024.0);
    return true;
}

/// @brief 创建默认帧缓冲的 render pass，并为每张交换链图像创建 framebuffer。
///
/// 经典 render pass 方案（G-09）：附件固定为 "交换链 color + 内部深度"。
/// - color：initialLayout = Undefined，finalLayout = PresentSrcKHR（可以直接 present）；
/// - depth：initialLayout = Undefined，finalLayout = DepthStencilAttachmentOptimal。
///
/// 注意 loadOp/storeOp 是 render pass 的固有状态，不能像清屏值那样在 begin 时覆盖。
/// 这里对整个默认帧缓冲统一采用 "color Clear + Store、depth Clear + DontCare"：
/// 每帧从清屏开始，深度内容不需要跨帧保留。`encoder_begin_render_pass` 接上默认
/// 帧缓冲时（V3-3）应使用同一 load/store 约定；如果将来要支持 Load，需要按
/// load/store 组合缓存 render pass + framebuffer。
bool VkRenderingDevice::create_swapchain_render_pass_and_framebuffers()
{
    if (swapchain_image_views.empty() || swapchain_depths.size() != swapchain_image_views.size())
    {
        std::printf("[ERROR] Vulkan create_swapchain_render_pass_and_framebuffers: "
                    "image view/depth count mismatch\n");
        return false;
    }

    try
    {
        vk::AttachmentDescription color_attachment;
        color_attachment.setFormat(swapchain_image_format);
        color_attachment.setSamples(vk::SampleCountFlagBits::e1);
        color_attachment.setLoadOp(vk::AttachmentLoadOp::eClear);
        color_attachment.setStoreOp(vk::AttachmentStoreOp::eStore);
        color_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
        color_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
        color_attachment.setInitialLayout(vk::ImageLayout::eUndefined);
        color_attachment.setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

        vk::AttachmentDescription depth_attachment;
        depth_attachment.setFormat(swapchain_depth_format);
        depth_attachment.setSamples(vk::SampleCountFlagBits::e1);
        depth_attachment.setLoadOp(vk::AttachmentLoadOp::eClear);
        depth_attachment.setStoreOp(vk::AttachmentStoreOp::eDontCare);
        depth_attachment.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare);
        depth_attachment.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
        depth_attachment.setInitialLayout(vk::ImageLayout::eUndefined);
        depth_attachment.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::AttachmentReference color_reference(0, vk::ImageLayout::eColorAttachmentOptimal);
        vk::AttachmentReference depth_reference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal);

        vk::SubpassDescription subpass;
        subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
        subpass.setColorAttachments(color_reference);
        subpass.setPDepthStencilAttachment(&depth_reference);

        // 外部 → subpass：保证清屏/附件布局转换与后续颜色/深度写入的可见性。
        vk::SubpassDependency incoming;
        incoming.setSrcSubpass(vk::SubpassExternal);
        incoming.setDstSubpass(0);
        incoming.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                 vk::PipelineStageFlagBits::eEarlyFragmentTests);
        incoming.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                 vk::PipelineStageFlagBits::eEarlyFragmentTests);
        incoming.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentWrite |
                                  vk::AccessFlagBits::eDepthStencilAttachmentWrite);

        // subpass → 外部：颜色写入先于把 finalLayout 转成 PresentSrcKHR（供 present 使用）。
        vk::SubpassDependency outgoing;
        outgoing.setSrcSubpass(0);
        outgoing.setDstSubpass(vk::SubpassExternal);
        outgoing.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
        outgoing.setSrcAccessMask(vk::AccessFlagBits::eColorAttachmentWrite);
        outgoing.setDstStageMask(vk::PipelineStageFlagBits::eBottomOfPipe);

        std::array<vk::AttachmentDescription, 2> attachments = {color_attachment, depth_attachment};
        std::array<vk::SubpassDependency, 2> dependencies = {incoming, outgoing};

        vk::RenderPassCreateInfo render_pass_info;
        render_pass_info.setAttachments(attachments);
        render_pass_info.setSubpasses(subpass);
        render_pass_info.setDependencies(dependencies);

        swapchain_render_pass = device.createRenderPass(render_pass_info);

        swapchain_framebuffers.clear();
        swapchain_framebuffers.reserve(swapchain_image_views.size());

        for (size_t i = 0; i < swapchain_image_views.size(); ++i)
        {
            std::array<vk::ImageView, 2> framebuffer_attachments = {
                *swapchain_image_views[i],
                *swapchain_depths[i].view,
            };

            vk::FramebufferCreateInfo create_info;
            create_info.setRenderPass(*swapchain_render_pass);
            create_info.setAttachments(framebuffer_attachments);
            create_info.setWidth(swapchain_image_extent.width);
            create_info.setHeight(swapchain_image_extent.height);
            create_info.setLayers(1);

            swapchain_framebuffers.emplace_back(device, create_info);
        }
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_swapchain_render_pass_and_framebuffers", error);
        return false;
    }
    return true;
}

/// @brief 创建每帧在飞的 acquire/present 同步对象（V3-2 / D-24 的基础设施）。
bool VkRenderingDevice::create_frame_sync_objects()
{
    try
    {
        frame_syncs.clear();
        frame_syncs.reserve(MAX_FRAMES_IN_FLIGHT);

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            frame_syncs.emplace_back();
            FrameSync &sync = frame_syncs.back();

            sync.image_available = device.createSemaphore(vk::SemaphoreCreateInfo{});
            sync.render_finished = device.createSemaphore(vk::SemaphoreCreateInfo{});

            // 初始为 signaled：第一帧等待 in_flight 时不会永久阻塞。
            vk::FenceCreateInfo fence_info;
            fence_info.setFlags(vk::FenceCreateFlagBits::eSignaled);
            sync.in_flight = device.createFence(fence_info);
        }
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("create_frame_sync_objects", error);
        return false;
    }
    return true;
}

/// @brief 释放所有依赖交换链的资源（保留 swapchain 本身）。
void VkRenderingDevice::destroy_swapchain_dependents()
{
    // 顺序不可颠倒：framebuffer 必须早于 render pass；view 必须早于它引用的 image；
    // 所有依赖对象必须早于 swapchain 本身。
    swapchain_framebuffers.clear();
    swapchain_render_pass.clear();
    swapchain_depths.clear();
    swapchain_image_views.clear();
    frame_syncs.clear();
    swapchain_images.clear();
    swapchain_depth_format = vk::Format::eUndefined;
    current_swapchain_image = UINT32_MAX;
    current_frame = 0;
}

/// @brief 初始化交换链及默认帧缓冲依赖资源。
///
/// 覆盖 I-03/D-16/V3-1：交换链 image view、每图像深度缓冲、默认 render pass 与
/// 每图像 framebuffer、acquire/present 同步对象。重复调用时把旧 swapchain 交给
/// 驱动的 `oldSwapchain` 并在创建前释放旧依赖资源，因此可被 resize（V3-4）复用。
/// @return 是否成功
bool VkRenderingDevice::init_swapchain()
{
    if (surface == nullptr)
    {
        std::printf("[ERROR] Vulkan init_swapchain: no window surface\n");
        return false;
    }

    try
    {
        const auto [capabilities, available_formats, present_modes] = query_swapchain_support(physical_device);
        if (available_formats.empty())
        {
            std::printf("[ERROR] Vulkan init_swapchain: surface reports no formats\n");
            return false;
        }
        if (!(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment))
        {
            std::printf("[ERROR] Vulkan init_swapchain: surface does not support color attachment usage\n");
            return false;
        }

        const auto surface_format = choose_swap_surface_format(available_formats);
        const auto present_mode = choose_swap_surface_present_mode(present_modes);
        const auto extent = choose_swap_extent(capabilities);
        if (extent.width == 0 || extent.height == 0)
        {
            std::printf("[ERROR] Vulkan init_swapchain: surface reports a zero-sized extent\n");
            return false;
        }

        // 交换链图像数量
        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
        {
            image_count = capabilities.maxImageCount;
        }

        // 重建时先释放引用旧 image 的对象，再把旧 swapchain 交给驱动（D-16/V3-4）。
        // 旧 swapchain 本身在下面的赋值时由 RAII 释放。
        const vk::SwapchainKHR old_swapchain = *swapchain;
        if (old_swapchain != nullptr)
        {
            // 旧 image 可能仍被在途帧引用；重建前先等待设备空闲（V3-4 会走这条路径）。
            device.waitIdle();
        }
        destroy_swapchain_dependents();

        vk::SwapchainCreateInfoKHR create_info;
        create_info.surface = surface;
        create_info.minImageCount = image_count;                 // 交换链图像数量
        create_info.imageFormat = surface_format.format;         // 图像格式
        create_info.imageColorSpace = surface_format.colorSpace; // 图像色彩空间
        create_info.imageExtent = extent;                        // 图像分辨率
        create_info.presentMode = present_mode;                  // 呈现模式
        create_info.imageArrayLayers = 1;
        vk::ImageUsageFlags image_usage = vk::ImageUsageFlagBits::eColorAttachment;
        if (capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc)
            image_usage |= vk::ImageUsageFlagBits::eTransferSrc;
        create_info.imageUsage = image_usage;
        create_info.preTransform = capabilities.currentTransform; // 图像变换，使用设备默认
        create_info.clipped = true; // 裁剪遮挡像素
        create_info.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque; // 暂时忽略透明像素
        create_info.oldSwapchain = old_swapchain; // 重建时让驱动复用旧交换链资源
        // 设置图像共享模式
        // 如果物理设备的图形和呈现是两个队列族，需要设置eConcurrent模式
        const auto [graphics_family, present_family] = findQueueFamilies(physical_device);
        std::vector<uint32_t> queue_family_indices{graphics_family.value(), present_family.value()};
        if (graphics_family != present_family)
        {
            create_info.imageSharingMode = vk::SharingMode::eConcurrent;
            create_info.setQueueFamilyIndices(queue_family_indices);
        }
        else
        {
            create_info.imageSharingMode = vk::SharingMode::eExclusive;
        }

        swapchain = device.createSwapchainKHR(create_info);
        swapchain_images = swapchain.getImages();
        swapchain_image_format = surface_format.format;
        swapchain_image_extent = extent;
        current_swapchain_image = UINT32_MAX;
        current_frame = 0;

        if (!create_swapchain_image_views())
        {
            destroy_swapchain_dependents();
            return false;
        }
        if (!create_swapchain_depth_buffers())
        {
            destroy_swapchain_dependents();
            return false;
        }
        if (!create_swapchain_render_pass_and_framebuffers())
        {
            destroy_swapchain_dependents();
            return false;
        }
        if (!create_frame_sync_objects())
        {
            destroy_swapchain_dependents();
            return false;
        }
    }
    catch (const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan init_swapchain failed: %s\n", error.what());
        destroy_swapchain_dependents();
        return false;
    }

    Format orca_format = Format::Undefined;
    if (vk_format_from_vulkan(swapchain_image_format, orca_format))
    {
        std::printf("[INFO] Vulkan created swapchain: %ux%u, %u images, format=%d\n",
                    swapchain_image_extent.width, swapchain_image_extent.height,
                    static_cast<uint32_t>(swapchain_images.size()), static_cast<int>(orca_format));
    }
    else
    {
        std::printf("[WARN] Vulkan swapchain format cannot be represented by interface Format\n");
    }

    std::printf("[INFO] Vulkan swapchain resources: %zu image view(s), %zu depth image(s), "
                "%zu framebuffer(s), %u frame sync set(s)\n",
                swapchain_image_views.size(), swapchain_depths.size(), swapchain_framebuffers.size(),
                MAX_FRAMES_IN_FLIGHT);
    return true;
}

/// @brief 重建交换链，window resize 事件触发（I-04）。
///
/// 重建统一走 `init_swapchain` 的 oldSwapchain 路径：它先 `device.waitIdle()`，
/// 再按依赖顺序释放旧的 image view / 深度 / render pass / framebuffer / 同步对象，
/// 然后把旧交换链交给驱动并重建全部资源，因此 resize 后不会残留引用旧 image 的对象。
///
/// 尺寸来源以 surface 为准：Windows 下 `currentExtent` 等于窗口客户区像素尺寸，
/// `window_info` 只是 surface 未给出 extent 时的回退（见 `choose_swap_extent`）。
/// 因此：
/// - 目标是 0 尺寸（窗口最小化）时只记录尺寸并推迟重建，等下一次非零 resize；
/// - 计算出的 extent 与当前一致时不重建（live resize 会连续触发同尺寸事件）；
/// - 平台 resize 事件转发仍属于 V3-5，`present` 仍属于 V3-4。
void VkRenderingDevice::resize_swapchain(uint32_t width, uint32_t height)
{
    window_info.width = width;
    window_info.height = height;

    if (width == 0 || height == 0)
    {
        std::printf("[INFO] Vulkan resize_swapchain: %ux%u, deferring rebuild until non-zero size\n",
                    width, height);
        return;
    }

    if (device == nullptr || surface == nullptr)
    {
        std::printf("[WARN] Vulkan resize_swapchain: called before init/init_device\n");
        return;
    }

    vk::Extent2D target_extent {};
    try
    {
        const vk::SurfaceCapabilitiesKHR capabilities = physical_device.getSurfaceCapabilitiesKHR(surface);
        target_extent = choose_swap_extent(capabilities);
    }
    catch (const vk::SystemError &error)
    {
        print_vk_error("resize_swapchain(getSurfaceCapabilitiesKHR)", error);
        return;
    }

    // surface 当前不能提供有效尺寸（例如正在最小化）：推迟，避免创建非法交换链。
    if (target_extent.width == 0 || target_extent.height == 0)
    {
        std::printf("[INFO] Vulkan resize_swapchain: surface extent is 0, deferring rebuild\n");
        return;
    }

    // 目标 extent 与当前交换链一致且依赖资源完整：不需要重建（window_info 已更新，供回退使用）。
    // 之所以检查 framebuffer 非空，是为了在“上次重建失败、依赖资源已被释放”时重新走一遍重建。
    if (swapchain != nullptr && !swapchain_framebuffers.empty() &&
        target_extent.width == swapchain_image_extent.width &&
        target_extent.height == swapchain_image_extent.height)
    {
        return;
    }

    if (!init_swapchain())
    {
        std::printf("[ERROR] Vulkan resize_swapchain: rebuild failed (target %ux%u)\n",
                    target_extent.width, target_extent.height);
        return;
    }

    uint32_t actual_width = 0;
    uint32_t actual_height = 0;
    get_swapchain_size(actual_width, actual_height);
    std::printf("[INFO] Vulkan resized swapchain: %ux%u (requested %ux%u)\n", actual_width, actual_height,
                width, height);
}
