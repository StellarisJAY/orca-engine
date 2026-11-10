#include "vk_rendering_device.h"
#include <set>

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
        if (physical_device.getSurfaceSupportKHR(i, surface))
        {
            indices.presentFamily = i;
        }

        if (indices.isComplete())
            break;

        ++i;
    }
    return indices;
}

void VkRenderingDevice::create_instance()
{
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
    create_info.enabledExtensionCount = static_cast<uint32_t>(window_info.vulkan_instance_extensions.size());
    create_info.ppEnabledExtensionNames = window_info.vulkan_instance_extensions.data();
    instance = context.createInstance(create_info);
}

void VkRenderingDevice::create_debug_messenger()
{
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

bool VkRenderingDevice::create_logical_device()
{
    // 逻辑设备创建队列族队列
    std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;
    vk::DeviceQueueCreateInfo queue_create_info;
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
    create_info.setQueueCreateInfos(queue_create_info);
    create_info.pEnabledFeatures = &device_features;
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

bool VkRenderingDevice::is_physical_device_suitable(const vk::raii::PhysicalDevice &physicalDevice)
{
    const auto indices = findQueueFamilies(physicalDevice);

    return indices.isComplete();
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

bool VkRenderingDevice::init_swapchain()
{
    return false;
}