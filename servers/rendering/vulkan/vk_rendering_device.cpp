#include "vk_rendering_device.h"
#include "math/math_funcs.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>

#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *callback_data,
    void *user_data)
{
    (void)type;
    (void)user_data;
    std::printf("[VK][severity %u] %s\n", uint32_t(severity), callback_data->pMessage);
    return VK_FALSE;
}
#endif

VkResult VkRenderingDevice::create_instance(VkInstanceCreateFlags flags)
{
    add_device_extension(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    // 平台层声明的实例扩展（surface 相关），由 RenderingWindowInfo 传入。
    for (const char *extension : window_info.vulkan_instance_extensions)
        add_instance_extension(extension);
#ifndef NDEBUG
    add_instance_layer("VK_LAYER_KHRONOS_validation");
    add_instance_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    // 过滤掉当前环境不支持的层与扩展，避免 vkCreateInstance 直接失败
    if (VkResult result = check_instance_layers(layer_names))
        return result;
    if (VkResult result = check_instance_extensions(extension_names, layer_names))
        return result;

    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "orca-engine",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName = "orca-engine",
        .engineVersion = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion = api_version};

    VkInstanceCreateInfo instanceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = flags,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = uint32_t(layer_names.size()),
        .ppEnabledLayerNames = layer_names.data(),
        .enabledExtensionCount = uint32_t(extension_names.size()),
        .ppEnabledExtensionNames = extension_names.data()};

    VkResult error = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
    if (error)
    {
        std::printf("[Error] failed to create vk instance, error code: %d\n", uint32_t(error));
        return error;
    }

    std::printf("Vulkan API Version: %d.%d.%d\n",
                VK_API_VERSION_MAJOR(api_version),
                VK_API_VERSION_MINOR(api_version),
                VK_API_VERSION_PATCH(api_version));

    if (VkResult result = create_debug_messenger())
    {
        std::printf("[Error] failed to create debug messenger, error code: %d\n", uint32_t(result));
        return result;
    }
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::create_surface()
{
    // 无窗口（离屏）或平台未提供桥接时不创建表面，后续 init_swapchain() 会失败提示。
    if (!window_info.create_vulkan_surface)
        return VK_SUCCESS;
    uint64_t raw_surface = 0;
    if (!window_info.create_vulkan_surface(instance, raw_surface))
    {
        std::printf("[ERROR] failed to create vulkan surface from platform window\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    // 64 位平台上 VkSurfaceKHR 是非分派句柄（指针），这里在同一指针宽度内转换。
    surface = reinterpret_cast<VkSurfaceKHR>(raw_surface);
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::create_debug_messenger()
{
#ifndef NDEBUG
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!func)
    {
        std::printf("[WARN] vkCreateDebugUtilsMessengerEXT not available\n");
        return VK_SUCCESS;
    }
    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = vk_debug_callback};
    return func(instance, &create_info, nullptr, &debugMessenger);
#else
    return VK_SUCCESS;
#endif
}

void VkRenderingDevice::destroy_debug_messenger()
{
#ifndef NDEBUG
    if (debugMessenger == VK_NULL_HANDLE)
        return;
    auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func)
        func(instance, debugMessenger, nullptr);
    debugMessenger = VK_NULL_HANDLE;
#endif
}

VkResult VkRenderingDevice::get_physical_devices()
{
    uint32_t device_count = 0;
    VkResult error = vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
    if (error)
    {
        std::printf("[ERROR] vulkan get physical devices count failed, error code: %d\n", uint32_t(error));
        return error;
    }
    if (device_count == 0)
    {
        std::printf("[WARN] no available physical device for vulkan\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    available_physical_devices.resize(device_count);
    error = vkEnumeratePhysicalDevices(instance, &device_count, available_physical_devices.data());
    if (error)
    {
        std::printf("[ERROR] vulkan get physical devices failed, error code: %d\n", uint32_t(error));
        return error;
    }
    queue_family_cache.assign(device_count, VK_QUEUE_FAMILY_IGNORED);
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::get_queue_family_indices(VkPhysicalDevice physicalDevice)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    if (!queueFamilyCount)
        return VK_ERROR_INITIALIZATION_FAILED;
    std::vector<VkQueueFamilyProperties> queueFamilyPropertieses(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyPropertieses.data());
    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        VkBool32
            supportGraphics = queueFamilyPropertieses[i].queueFlags & VK_QUEUE_GRAPHICS_BIT,
            supportPresentation = false,
            supportCompute = queueFamilyPropertieses[i].queueFlags & VK_QUEUE_COMPUTE_BIT;
        if (surface)
            if (VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportPresentation))
            {
                return result;
            }
        if (supportGraphics && supportCompute &&
            (!surface || supportPresentation))
        { // 短路执行，需要呈现的时候才需要判断supportPresentation
            queueFamilyIndex_graphics = queueFamilyIndex_compute = i;
            if (surface)
                queueFamilyIndex_presentation = i;
            return VK_SUCCESS;
        }
    }
    return VK_RESULT_MAX_ENUM;
}

VkResult VkRenderingDevice::determine_physical_device(uint32_t deviceIndex)
{
    if (deviceIndex >= available_physical_devices.size())
        return VK_ERROR_INITIALIZATION_FAILED;
    // 定义一个特殊值用于标记一个队列族索引已被找过但未找到
    static constexpr uint32_t notFound = INT32_MAX; //== VK_QUEUE_FAMILY_IGNORED & INT32_MAX
    // queue_family_cache用于为各个物理设备保存一份队列族索引
    if (queue_family_cache.size() != available_physical_devices.size())
        queue_family_cache.assign(available_physical_devices.size(), VK_QUEUE_FAMILY_IGNORED);

    // 如果队列族索引已被找过但未找到，返回VK_RESULT_MAX_ENUM
    if (queue_family_cache[deviceIndex] == notFound)
        return VK_RESULT_MAX_ENUM;

    // 如果队列族索引应被获取但还未被找过
    if (queue_family_cache[deviceIndex] == VK_QUEUE_FAMILY_IGNORED)
    {
        VkResult result = get_queue_family_indices(available_physical_devices[deviceIndex]);
        // 若GetQueueFamilyIndices(...)返回VK_SUCCESS或VK_RESULT_MAX_ENUM（vkGetPhysicalDeviceSurfaceSupportKHR(...)执行成功但没找齐所需队列族），
        // 说明对所需队列族索引已有结论，保存结果到queue_family_cache[deviceIndex]中
        if (result)
        {
            if (result == VK_RESULT_MAX_ENUM)
                queue_family_cache[deviceIndex] = notFound;
            return result;
        }
        else
            queue_family_cache[deviceIndex] = queueFamilyIndex_graphics;
    }
    // 若以上两个if分支皆不执行，则说明队列族索引已被获取，从queue_family_cache[deviceIndex]取得索引
    else
    {
        queueFamilyIndex_graphics = queueFamilyIndex_compute = queue_family_cache[deviceIndex];
        queueFamilyIndex_presentation = surface ? queue_family_cache[deviceIndex] : VK_QUEUE_FAMILY_IGNORED;
    }
    physical_device = available_physical_devices[deviceIndex];
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::create_device(VkDeviceCreateFlags flags)
{
    float queuePriority = 1.f;
    VkDeviceQueueCreateInfo queueCreateInfos[3] = {
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority},
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority},
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority}};
    uint32_t queueCreateInfoCount = 0;
    if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED)
        queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex = queueFamilyIndex_graphics;
    if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED &&
        queueFamilyIndex_presentation != queueFamilyIndex_graphics)
        queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex = queueFamilyIndex_presentation;
    if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED &&
        queueFamilyIndex_compute != queueFamilyIndex_graphics &&
        queueFamilyIndex_compute != queueFamilyIndex_presentation)
        queueCreateInfos[queueCreateInfoCount++].queueFamilyIndex = queueFamilyIndex_compute;
    VkPhysicalDeviceFeatures physicalDeviceFeatures;
    vkGetPhysicalDeviceFeatures(physical_device, &physicalDeviceFeatures);
    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .flags = flags,
        .queueCreateInfoCount = queueCreateInfoCount,
        .pQueueCreateInfos = queueCreateInfos,
        .enabledExtensionCount = uint32_t(device_extensions.size()),
        .ppEnabledExtensionNames = device_extensions.data(),
        .pEnabledFeatures = &physicalDeviceFeatures};
    if (VkResult result = vkCreateDevice(physical_device, &deviceCreateInfo, nullptr, &device))
    {
        std::printf("[ERROR] vulkan create device failed, error code: %d\n", uint32_t(result));
        return result;
    }
    if (queueFamilyIndex_graphics != VK_QUEUE_FAMILY_IGNORED)
        vkGetDeviceQueue(device, queueFamilyIndex_graphics, 0, &queue_graphics);
    if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED)
        vkGetDeviceQueue(device, queueFamilyIndex_presentation, 0, &queue_presentation);
    if (queueFamilyIndex_compute != VK_QUEUE_FAMILY_IGNORED)
        vkGetDeviceQueue(device, queueFamilyIndex_compute, 0, &queue_compute);
    vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);
    vkGetPhysicalDeviceMemoryProperties(physical_device, &physical_device_memory_properties);
    // 输出所用的物理设备名称
    std::printf("Using Physical Device: %s\n", physical_device_properties.deviceName);
    /*待Ch1-4填充*/
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::get_surface_formats()
{
    uint32_t count = 0;
    if (VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, nullptr))
    {
        std::printf("[ERROR] vulkan get surface formats count failed, error code: %d\n", uint32_t(result));
        return result;
    }
    if (!count)
    {
        std::printf("[ERROR] no surface format available for vulkan\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    available_surface_formats.resize(count);
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &count, available_surface_formats.data());
    return result;
}

VkResult VkRenderingDevice::set_surface_format(VkSurfaceFormatKHR format)
{
    chosen_surface_format = format;
    std::printf("Using Surface Format: %d, Color Space: %d\n",
                int32_t(format.format), int32_t(format.colorSpace));
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::choose_surface_format()
{
    if (available_surface_formats.empty())
        return VK_ERROR_INITIALIZATION_FAILED;

    // 优先选择 sRGB，其次 UNORM，最后回退到第一个可用格式
    for (const auto &format : available_surface_formats)
        if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return set_surface_format(format);

    for (const auto &format : available_surface_formats)
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM)
            return set_surface_format(format);

    return set_surface_format(available_surface_formats[0]);
}

VkResult VkRenderingDevice::get_present_modes()
{
    uint32_t count = 0;
    if (VkResult result = vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, nullptr))
    {
        std::printf("[ERROR] vulkan get present modes count failed, error code: %d\n", uint32_t(result));
        return result;
    }
    if (!count)
    {
        std::printf("[ERROR] no present mode available for vulkan\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    available_present_modes.resize(count);
    return vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &count, available_present_modes.data());
}

VkResult VkRenderingDevice::choose_present_mode()
{
    // MAILBOX 延迟较低；FIFO 是规范保证始终可用的模式
    for (const auto &mode : available_present_modes)
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            chosen_present_mode = mode;
            return VK_SUCCESS;
        }
    chosen_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::create_swapchain(VkSwapchainCreateFlagsKHR flags, VkSwapchainKHR oldSwapchain)
{
    // 确保已选定表面格式
    if (chosen_surface_format.format == VK_FORMAT_UNDEFINED)
    {
        if (get_surface_formats() != VK_SUCCESS || choose_surface_format() != VK_SUCCESS)
            return VK_ERROR_INITIALIZATION_FAILED;
    }

    // 获取物理设备和surface的capabilities
    VkSurfaceCapabilitiesKHR surface_cap;
    VkResult error = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_cap);
    if (error)
    {
        std::printf("[ERROR] vulkan get physical device surface capabilities failed, error code: %d\n", uint32_t(error));
        return error;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .flags = flags,
        .surface = surface,
    };

    // 图像数量
    create_info.minImageCount =
        surface_cap.minImageCount + (surface_cap.maxImageCount == 0) // 若maxImageCount为0，表示不限制图像数量，+1
        + (surface_cap.maxImageCount > surface_cap.minImageCount);   // 或者，若maxImageCount大于minImageCount，+1
    // 图像尺寸，使用surface的尺寸，没有则用默认尺寸
    swapchain_extent =
        surface_cap.currentExtent.width == UINT32_MAX
            ? VkExtent2D{
                  Math::clamp(defaultWindowSize.width, surface_cap.minImageExtent.width, surface_cap.maxImageExtent.width),
                  Math::clamp(defaultWindowSize.height, surface_cap.minImageExtent.height, surface_cap.maxImageExtent.height)}
            : surface_cap.currentExtent;
    create_info.imageExtent = swapchain_extent;

    create_info.imageArrayLayers = 1;
    create_info.preTransform = (surface_cap.supportedTransforms & surface_cap.currentTransform)
                                   ? surface_cap.currentTransform
                                   : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    // alpha通道处理模式：按优先级挑选一个被支持的模式
    {
        static const VkCompositeAlphaFlagBitsKHR composite_alpha_candidates[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        for (auto candidate : composite_alpha_candidates)
            if (surface_cap.supportedCompositeAlpha & candidate)
            {
                create_info.compositeAlpha = candidate;
                break;
            }
    }

    // 图像必须被用作颜色附件（VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT），
    // 最好还能被用作数据传送的目标（VK_IMAGE_USAGE_TRANSFER_DST_BIT），
    // 这样就能用vkCmdClearColorImage(...)清屏，还可以用vkCmdBlitImage(...)将图像直接搬运（而不必渲染）到屏幕上
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (surface_cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
        create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (surface_cap.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    else
    {
        std::printf("[WARN] vulkan swapchain image usage transfer not available\n");
    }

    // 选定的表面格式与呈现模式
    create_info.imageFormat = chosen_surface_format.format;
    create_info.imageColorSpace = chosen_surface_format.colorSpace;
    create_info.presentMode = chosen_present_mode;

    // 若图形与呈现使用不同队列族，需要并发共享
    uint32_t queue_family_indices[2] = { queueFamilyIndex_graphics, queueFamilyIndex_presentation };
    if (queueFamilyIndex_presentation != VK_QUEUE_FAMILY_IGNORED &&
        queueFamilyIndex_graphics != queueFamilyIndex_presentation)
    {
        create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices = queue_family_indices;
    }
    else
    {
        create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = oldSwapchain;

    error = vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain);
    if (error)
    {
        std::printf("[ERROR] vulkan create swapchain failed, error code: %d\n", uint32_t(error));
        return error;
    }

    // 取回交换链图像
    uint32_t image_count = 0;
    if ((error = vkGetSwapchainImagesKHR(device, swapchain, &image_count, nullptr)))
    {
        std::printf("[ERROR] vulkan get swapchain images count failed, error code: %d\n", uint32_t(error));
        return error;
    }
    if (!image_count)
    {
        std::printf("[ERROR] swapchain returned no image\n");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    swapchain_images.resize(image_count);
    if ((error = vkGetSwapchainImagesKHR(device, swapchain, &image_count, swapchain_images.data())))
    {
        std::printf("[ERROR] vulkan get swapchain images failed, error code: %d\n", uint32_t(error));
        return error;
    }

    // 为每张交换链图像创建视图
    swapchain_image_views.resize(image_count, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < image_count; i++)
    {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = chosen_surface_format.format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }};
        if ((error = vkCreateImageView(device, &view_info, nullptr, &swapchain_image_views[i])))
        {
            std::printf("[ERROR] vulkan create image view failed, error code: %d\n", uint32_t(error));
            return error;
        }
    }

    std::printf("Swapchain created: %ux%u, %u images, present mode %d\n",
                swapchain_extent.width, swapchain_extent.height, image_count, int32_t(chosen_present_mode));
    return VK_SUCCESS;
}

void VkRenderingDevice::cleanup_swapchain()
{
    if (device == VK_NULL_HANDLE)
        return;
    for (auto view : swapchain_image_views)
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    swapchain_image_views.clear();
    swapchain_images.clear();
    if (swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

VkResult VkRenderingDevice::recreate_swapchain()
{
    if (device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE)
        return VK_ERROR_INITIALIZATION_FAILED;

    vkDeviceWaitIdle(device);

    // 保留旧交换链用于创建新链，销毁其视图但暂不销毁旧链本身
    VkSwapchainKHR old_swapchain = swapchain;
    for (auto view : swapchain_image_views)
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(device, view, nullptr);
    swapchain_image_views.clear();
    swapchain_images.clear();
    swapchain = VK_NULL_HANDLE;

    VkResult result = create_swapchain(0, old_swapchain);
    if (result == VK_SUCCESS)
    {
        if (old_swapchain != VK_NULL_HANDLE)
            vkDestroySwapchainKHR(device, old_swapchain, nullptr);
    }
    else if (old_swapchain != VK_NULL_HANDLE)
    {
        // 新链创建失败，回退到旧链以便上层重试
        swapchain = old_swapchain;
    }
    return result;
}

bool VkRenderingDevice::init(const RenderingWindowInfo &window)
{
    window_info = window;
    if (create_instance() != VK_SUCCESS)
        return false;
    if (create_surface() != VK_SUCCESS)
        return false;
    if (get_physical_devices() != VK_SUCCESS)
        return false;
    return true;
}

bool VkRenderingDevice::init_device()
{
    if (determine_physical_device(0) != VK_SUCCESS)
        return false;
    if (create_device() != VK_SUCCESS)
        return false;
    return true;
}

bool VkRenderingDevice::init_swapchain()
{
    if (device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE)
    {
        std::printf("[ERROR] device and surface must be ready before init_swapchain()\n");
        return false;
    }
    if (get_surface_formats() != VK_SUCCESS)
        return false;
    if (choose_surface_format() != VK_SUCCESS)
        return false;
    if (get_present_modes() != VK_SUCCESS)
        return false;
    if (choose_present_mode() != VK_SUCCESS)
        return false;
    if (create_swapchain() != VK_SUCCESS)
        return false;
    return true;
}

void VkRenderingDevice::shutdown()
{
    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
        cleanup_swapchain();
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
        queue_graphics = VK_NULL_HANDLE;
        queue_presentation = VK_NULL_HANDLE;
        queue_compute = VK_NULL_HANDLE;
    }
    // surface 由本后端创建，因此也由本后端销毁（必须在 instance 之前）。
    if (surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    destroy_debug_messenger();
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
}

void VkRenderingDevice::add_instance_layer(const char *layer_name)
{
    for (auto layer : layer_names)
    {
        if (std::strcmp(layer, layer_name) == 0)
            return;
    }
    layer_names.push_back(layer_name);
}

void VkRenderingDevice::add_instance_extension(const char *extension_name)
{
    for (auto ext : extension_names)
    {
        if (std::strcmp(ext, extension_name) == 0)
            return;
    }
    extension_names.push_back(extension_name);
}

void VkRenderingDevice::add_device_extension(const char *extension_name)
{
    for (auto ext : device_extensions)
    {
        if (std::strcmp(ext, extension_name) == 0)
            return;
    }
    device_extensions.push_back(extension_name);
}

VkResult VkRenderingDevice::check_instance_layers(std::vector<const char *> &layers_to_check) const
{
    uint32_t layer_count = 0;
    VkResult result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    if (result != VK_SUCCESS)
    {
        std::printf("[ERROR] failed to get instance layer count, error code: %d\n", uint32_t(result));
        return result;
    }

    std::vector<VkLayerProperties> available_layers(layer_count);
    if (layer_count)
    {
        result = vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());
        if (result != VK_SUCCESS)
        {
            std::printf("[ERROR] failed to get available instance layers, error code: %d\n", uint32_t(result));
            return result;
        }
    }

    auto it = std::remove_if(layers_to_check.begin(), layers_to_check.end(),
                             [&](const char *name) {
                                 for (const auto &layer : available_layers)
                                     if (!std::strcmp(name, layer.layerName))
                                         return false;
                                 std::printf("[WARN] instance layer not available, skipped: %s\n", name);
                                 return true;
                             });
    layers_to_check.erase(it, layers_to_check.end());
    return VK_SUCCESS;
}

VkResult VkRenderingDevice::check_instance_extensions(std::vector<const char *> &extensions_to_check,
                                                      const std::vector<const char *> &layer_names) const
{
    // 收集全局扩展以及各层提供的扩展
    std::vector<VkExtensionProperties> available_extensions;
    auto append = [&](const char *layer_name) -> VkResult {
        uint32_t extension_count = 0;
        VkResult result = vkEnumerateInstanceExtensionProperties(layer_name, &extension_count, nullptr);
        if (result != VK_SUCCESS)
        {
            std::printf("[ERROR] failed to get instance extension count (layer: %s), error code: %d\n",
                        layer_name ? layer_name : "<global>", uint32_t(result));
            return result;
        }
        size_t base = available_extensions.size();
        available_extensions.resize(base + extension_count);
        if (extension_count)
        {
            result = vkEnumerateInstanceExtensionProperties(layer_name, &extension_count, available_extensions.data() + base);
            if (result != VK_SUCCESS)
                return result;
        }
        return VK_SUCCESS;
    };

    if (VkResult result = append(nullptr))
        return result;
    for (auto layer : layer_names)
        if (VkResult result = append(layer))
            return result;

    auto it = std::remove_if(extensions_to_check.begin(), extensions_to_check.end(),
                             [&](const char *name) {
                                 for (const auto &ext : available_extensions)
                                     if (!std::strcmp(name, ext.extensionName))
                                         return false;
                                 std::printf("[WARN] instance extension not available, skipped: %s\n", name);
                                 return true;
                             });
    extensions_to_check.erase(it, extensions_to_check.end());
    return VK_SUCCESS;
}
