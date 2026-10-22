#pragma once
#include "rendering_device.h"
#include <vector>
#include <vulkan/vulkan_core.h>

class VkRenderingDevice : public RenderingDevice
{
    static constexpr VkExtent2D defaultWindowSize = { 1280, 720 };
private:
    RenderingWindowInfo window_info;
    uint32_t api_version = VK_API_VERSION_1_0;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    std::vector<const char *> layer_names = {};
    std::vector<const char *> extension_names = {};

    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties physical_device_properties;
    VkPhysicalDeviceMemoryProperties physical_device_memory_properties;
    std::vector<VkPhysicalDevice> available_physical_devices;
    // 与 available_physical_devices 一一对应，缓存已查询过的队列族索引
    std::vector<uint32_t> queue_family_cache = {};

    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex_graphics = VK_QUEUE_FAMILY_IGNORED;
    uint32_t queueFamilyIndex_presentation = VK_QUEUE_FAMILY_IGNORED;
    uint32_t queueFamilyIndex_compute = VK_QUEUE_FAMILY_IGNORED;
    VkQueue queue_graphics = VK_NULL_HANDLE;
    VkQueue queue_presentation = VK_NULL_HANDLE;
    VkQueue queue_compute = VK_NULL_HANDLE;

    std::vector<const char *> device_extensions = {};

    std::vector<VkSurfaceFormatKHR> available_surface_formats = {};
    VkSurfaceFormatKHR chosen_surface_format = { VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    std::vector<VkPresentModeKHR> available_present_modes = {};
    VkPresentModeKHR chosen_present_mode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D swapchain_extent = defaultWindowSize;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;

    VkResult create_instance(VkInstanceCreateFlags flags = 0);
    VkResult create_surface();
    VkResult create_debug_messenger();
    void destroy_debug_messenger();
    VkResult get_physical_devices();
    // 判定一个物理设备是否满足需求
    VkResult get_queue_family_indices(VkPhysicalDevice physicalDevice);
    VkResult determine_physical_device(uint32_t deviceIndex = 0);

    VkResult create_device(VkDeviceCreateFlags flags = 0);

    VkResult get_surface_formats();
    VkResult set_surface_format(VkSurfaceFormatKHR format);
    VkResult choose_surface_format();
    VkResult get_present_modes();
    VkResult choose_present_mode();
    VkResult create_swapchain(VkSwapchainCreateFlagsKHR flags = 0, VkSwapchainKHR oldSwapchain = VK_NULL_HANDLE);
    VkResult recreate_swapchain();
    void cleanup_swapchain();

    void add_instance_layer(const char *layer_name);
    void add_instance_extension(const char *extension_name);
    void add_device_extension(const char *extension_name);

    VkResult check_instance_layers(std::vector<const char *> &layers_to_check) const;
    VkResult check_instance_extensions(std::vector<const char *> &extensions_to_check,
                                       const std::vector<const char *> &layer_names) const;
public:
    RenderingDriverType get_driver_type() const override { return RenderingDriverType::Vulkan; }

    bool init(const RenderingWindowInfo &window) override;
    bool init_device() override;
    bool init_swapchain() override;
    void shutdown() override;

    void *get_handle() const override { return instance; }
    void *get_device_handle() const override { return device; }


    // Vulkan 尚未实现
    uint32_t create_mesh_surface(
        const std::vector<Vector3> &,
        const std::vector<Vector3> &,
        const std::vector<Vector2> &,
        const std::vector<Vector3> &,
        const std::vector<Vector3> &,
        const std::vector<uint32_t> &) override { return 0; }
    void destroy_mesh_surface(uint32_t) override {}
    void draw_call(const DrawItem &item) override {}
    void clear() override {}

    uint64_t create_shader_program(const std::string &vertex_src, const std::string &frag_src) override { return 0; };
    void destroy_shader_program(uint64_t program) override {};
    void bind_shader_program(uint64_t program) override {};
    void set_uniform_mat4(uint64_t program, const char *uniform, const real_t *data) override {};
    void set_uniform_vec4(uint64_t program, const char *uniform, const real_t *data) override {};
    void set_uniform_float(uint64_t program, const char *uniform, float data) override {};
    void set_uniform_vec3(uint64_t program, const char *uniform, const real_t *data) override {};
    void set_uniform_int(uint64_t program, const char *uniform, int data) override {}

    uint32_t create_texture_2d(const TextureDesc &desc, const void *pixels) { return 0; }
    void bind_texture_2d(uint32_t unit, uint32_t texture) {}
    void destroy_texture_2d(uint32_t texture) {}
};
