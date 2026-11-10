#include "vk_rendering_device.h"
#include <set>
#include <array>

#ifdef NODEBUG
constexpr bool ENABLE_VALIDATION_LAYER = false;
#else
constexpr bool ENABLE_VALIDATION_LAYER = true;
#endif

/// @brief debug回调函数
static VKAPI_ATTR uint32_t VKAPI_CALL debugMessageFunc(
    vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
    vk::DebugUtilsMessengerCallbackDataEXT const *pCallbackData,
    void *pUserData)
{
    std::printf("validation layer: %s\n", pCallbackData->pMessage);
    return false;
}

constexpr std::array<const char *, 1> REQUIRED_LAYERS{"VK_LAYER_KHRONOS_validation"};
constexpr std::array<const char *, 1> DEVICE_EXTENSIONS{vk::KHRSwapchainExtensionName};

std::vector<const char *> get_required_extensions(const RenderingWindowInfo &window_info)
{
    std::vector<const char *> extensions(window_info.vulkan_instance_extensions);
    if constexpr (ENABLE_VALIDATION_LAYER)
    {
        extensions.emplace_back(vk::EXTDebugUtilsExtensionName);
    }
    return extensions;
}

// ===========================================================================
// 验证层 & debug
// ===========================================================================

bool VkRenderingDevice::check_validation_layer_support() const
{
    const auto layers = context.enumerateInstanceLayerProperties();
    std::set<std::string> requiredLayers(REQUIRED_LAYERS.begin(), REQUIRED_LAYERS.end());
    for (const auto &layer : layers)
    {
        requiredLayers.erase(layer.layerName);
    }
    return requiredLayers.empty();
}

void VkRenderingDevice::create_debug_messenger()
{
    vk::DebugUtilsMessageSeverityFlagsEXT serverity_flags(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    vk::DebugUtilsMessageTypeFlagsEXT message_type_flags(
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    vk::DebugUtilsMessengerCreateInfoEXT create_info(
        {},                 // flag
        serverity_flags,    // messageSeverity
        message_type_flags, // messageType
        &debugMessageFunc   // 最后，pfnUserCallback
    );
    debug_messenger = instance.createDebugUtilsMessengerEXT(create_info);
}

// ===========================================================================
// 实例 & surface
// ===========================================================================

void VkRenderingDevice::create_instance()
{
    if constexpr (ENABLE_VALIDATION_LAYER)
    {
        if (!check_validation_layer_support())
            throw std::runtime_error("validation layer not supported");
    }
    // 创建vk实例
    const vk::ApplicationInfo application_info(
        "orca-engine",
        1,
        "No Engine",
        1,
        vk::makeApiVersion(0, 1, 4, 0));
    vk::InstanceCreateInfo create_info(
        {},
        &application_info);
    // 开启验证层
    if constexpr (ENABLE_VALIDATION_LAYER)
    {
        create_info.setPEnabledLayerNames(REQUIRED_LAYERS);
    }
    // window surface扩展和debug扩展
    std::vector<const char *> extensions = get_required_extensions(window_info);
    create_info.setPEnabledExtensionNames(extensions);

    instance = context.createInstance(create_info);
}

bool VkRenderingDevice::create_surface()
{
    // 无窗口（离屏）或平台未提供桥接时不创建表面，后续 init_swapchain() 会失败提示。
    if (!window_info.create_vulkan_surface)
        return false;
    uint64_t raw_surface = 0;
    if (!window_info.create_vulkan_surface(*instance, raw_surface))
    {
        std::printf("[ERROR] failed to create vulkan surface from platform window\n");
        return false;
    }
    // 64 位平台上 VkSurfaceKHR 是非分派句柄（指针），这里在同一指针宽度内转换。
    surface = vk::raii::SurfaceKHR(instance, reinterpret_cast<VkSurfaceKHR>(raw_surface));
    std::printf("[INFO] created window surface\n");
    return true;
}

// ===========================================================================
// 物理设备 & 逻辑设备
// ===========================================================================

/// @brief 查询物理设备队列族
/// @param physicalDevice 物理设备
/// @return 队列族
QueueFamilyIndices VkRenderingDevice::findQueueFamilies(const vk::raii::PhysicalDevice &physicalDevice) const
{
    QueueFamilyIndices indices;
    // std::vector<vk::QueueFamilyProperties>
    const auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    int i = 0;
    for (const auto &queueFamily : queueFamilies)
    {
        // 图形队列族
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
        {
            indices.graphicsFamily = i;
        }
        // 呈现队列族，检查物理设备是否支持该队列族
        if (physicalDevice.getSurfaceSupportKHR(i, surface))
        {
            indices.presentFamily = i;
        }

        if (indices.isComplete())
            break;

        ++i;
    }
    return indices;
}

/// @brief 检查物理设备是否支持需要的扩展
static bool check_device_extension_support(const vk::raii::PhysicalDevice &device)
{
    // std::vector<vk::ExtensionProperties>
    const auto availableExtensions = device.enumerateDeviceExtensionProperties();
    std::set<std::string> requiredExtensions(DEVICE_EXTENSIONS.begin(), DEVICE_EXTENSIONS.end());
    for (const auto &extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

/// @brief 物理设备是否满足要求
bool VkRenderingDevice::is_physical_device_suitable(const vk::raii::PhysicalDevice &physicalDevice)
{
    if (const auto indices = findQueueFamilies(physicalDevice);
        !indices.isComplete())
        return false;

    if (const bool extensions_supported = check_device_extension_support(physicalDevice);
        !extensions_supported)
        return false;

    if (const auto swapchain_support = query_swapchain_support(physicalDevice);
        swapchain_support.formats.empty() || swapchain_support.presentModes.empty())
        return false;

    return true;
}

/// @brief 临时的物理设备评分机制
/// @param physicalDevice
/// @return
int rateDeviceSuitability(const vk::raii::PhysicalDevice &physicalDevice)
{
    const auto properties = physicalDevice.getProperties();
    const auto features = physicalDevice.getFeatures();

    // 必要性功能检查
    if (!features.geometryShader)
    {
        return 0;
    }

    int score = 0;
    // 独立显卡加分
    if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
    {
        score += 1000;
    }
    // 最大纹理尺寸影响图形质量加分
    score += properties.limits.maxImageDimension2D;

    return score;
}

/// @brief 选择物理设备
/// @return 是否有可用物理设备
bool VkRenderingDevice::select_physical_device()
{
    const auto physical_devices = instance.enumeratePhysicalDevices();
    if (physical_devices.empty())
    {
        std::printf("[WARN] no available physical device for Vulkan\n");
        return false;
    }

    // 找到第一个可用的物理设备
    for (const auto &device : physical_devices)
    {
        if (is_physical_device_suitable(device))
        {
            physical_device = device;
            break;
        }
    }
    // TODO 多显卡按评分选最好的
    if (physical_device == nullptr)
    {
        std::printf("[WARN] no suitable physical device for Vulkan\n");
        return false;
    }

    const auto &properties = physical_device.getProperties();
    std::printf("[INFO] Using Physical Device: %s\n", properties.deviceName);
    return true;
}

/// @brief 创建逻辑设备
/// @return 创建是否成功
bool VkRenderingDevice::create_logical_device()
{
    // 逻辑设备创建队列族队列
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    // 图形队列族和呈现队列族
    const auto [graphics, present] = findQueueFamilies(physical_device);
    std::set<uint32_t> unique_queue_families = {graphics.value(), present.value()};

    constexpr float queue_priority = 1.0f;
    for (uint32_t queue_family : unique_queue_families)
    {
        vk::DeviceQueueCreateInfo queue_create_info;
        queue_create_info.queueFamilyIndex = queue_family;
        queue_create_info.setQueuePriorities(queue_priority);
        queue_create_infos.emplace_back(queue_create_info);
    }

    // 创建逻辑设备
    vk::DeviceCreateInfo create_info;
    create_info.setQueueCreateInfos(queue_create_infos);
    create_info.pEnabledFeatures = &device_features;
    create_info.setPEnabledExtensionNames(DEVICE_EXTENSIONS);
    device = physical_device.createDevice(create_info);

    // 获取图形队列
    graphics_queue = device.getQueue(graphics.value(), 0);
    present_queue = device.getQueue(present.value(), 0);
    std::printf("[INFO] Created Vulkan Logical Device\n");
    return true;
}

bool VkRenderingDevice::init(const RenderingWindowInfo &info)
{
    window_info = info;
    create_instance();
    create_debug_messenger();
    if (!create_surface())
        return false;
    return true;
}

bool VkRenderingDevice::init_device()
{
    // 选择物理设备
    if (!select_physical_device())
        return false;
    // 创建逻辑设备
    if (!create_logical_device())
        return false;
    return true;
}

// ===========================================================================
// 交换链
// ===========================================================================

SwapChainSupportDetails VkRenderingDevice::query_swapchain_support(const vk::raii::PhysicalDevice &physicalDevice) const
{
    SwapChainSupportDetails details;
    details.capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    details.formats = physicalDevice.getSurfaceFormatsKHR(surface);
    details.presentModes = physicalDevice.getSurfacePresentModesKHR(surface);
    return details;
}

/// @brief 选择窗口输出格式
/// @param available_formats 物理设备可用格式
/// @return 最合适的输出格式
static vk::SurfaceFormatKHR choose_swap_surface_format(const std::vector<vk::SurfaceFormatKHR> &available_formats)
{
    // 优先 sRGB RGBA 32位颜色
    for (const auto &availableFormat : available_formats)
    {
        if (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
            availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            return availableFormat;
    }
    // 没有sRGB 32位，回退默认的第一个格式
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
    int width, height;
    // TODO framebuffer size
    // glfwGetFramebufferSize(m_window, &width, &height);
    vk::Extent2D actualExtent(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));

    actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return actualExtent;
}

bool VkRenderingDevice::init_swapchain()
{
    const auto [capabilities, available_formats, present_modes] = query_swapchain_support(physical_device);
    const auto surface_format = choose_swap_surface_format(available_formats);
    const auto present_mode = choose_swap_surface_present_mode(present_modes);
    const auto extent = choose_swap_extent(capabilities);

    // 交换链图像数量
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
    {
        image_count = capabilities.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR createInfo;
    createInfo.surface = surface;
    createInfo.minImageCount = image_count;                 // 交换链图像数量
    createInfo.imageFormat = surface_format.format;         // 图像格式
    createInfo.imageColorSpace = surface_format.colorSpace; // 图像色彩空间
    createInfo.imageExtent = extent;                        // 图像分辨率
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    return false;
}