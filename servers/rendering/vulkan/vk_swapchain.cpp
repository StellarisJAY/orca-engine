#include "vk_rendering_device.h"
#include "vk_format.h"
#include "rhi_validation.h"

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

/// @brief 初始化交换链
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

        const auto surface_format = choose_swap_surface_format(available_formats);
        const auto present_mode = choose_swap_surface_present_mode(present_modes);
        const auto extent = choose_swap_extent(capabilities);

        // 交换链图像数量
        uint32_t image_count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
        {
            image_count = capabilities.maxImageCount;
        }

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
        create_info.oldSwapchain = nullptr; // 屏幕大小变化需要切换swapchain，但此时初始化不需要
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
    }
    catch (const vk::SystemError &error)
    {
        std::printf("[ERROR] Vulkan init_swapchain failed: %s\n", error.what());
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
    return true;
}

/// @brief 重建交换链，window resize事件触发
/// @param width 新的宽
/// @param height 新的高
void VkRenderingDevice::resize_swapchain(uint32_t width, uint32_t height)
{
    window_info.width = width;
    window_info.height = height;
    // TODO 重建交换链
}