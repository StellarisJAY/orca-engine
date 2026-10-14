#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "rendering_device.h" // RenderingDriverType / RenderingWindowInfo

struct WindowDesc
{
    int width = 800;
    int height = 600;
    const char *title = "orca-engine";
    bool resizable = false;
    bool visible = true;
    bool fullscreen = false;
    // 该窗口要为哪个图形后端创建（决定是否创建 GL 上下文等）。
    // 默认为 Vulkan 以保持历史行为，使用默认后端时请显式传入 RenderingDevice::get_default_driver()。
    RenderingDriverType driver = RenderingDriverType::Vulkan;

    WindowDesc() = default;
    WindowDesc(int width, int height) : width(width), height(height) {}
};

enum class WindowEventType
{
    Key,
    MouseButton,
    WindowSize,
    FramebufferSize,
    Close,
};

struct KeyCode
{
};

struct WindowEvent
{
    WindowEventType type;
    KeyCode keyCode;
    int w;
    int h;
};

class Window
{
public:
    using EventCallback = std::function<void(const WindowEvent &event)>;

private:
    std::vector<EventCallback> callbacks = {};

public:
    virtual ~Window() = default;

    virtual bool init(const WindowDesc &desc) = 0;
    virtual void shutdown() = 0;
    virtual bool get_should_close() const = 0;
    // 返回后端无关的原生窗口/上下文信息，供 RenderingDevice 初始化。
    // 平台层不直接依赖任何图形 API 类型。
    virtual RenderingWindowInfo get_rendering_window_info() const = 0;
    virtual void poll_event() = 0;
    virtual void get_size(int &w, int &h) const = 0;
    virtual void get_frame_buffer_size(int &w, int &h) const = 0;
    // 呈现当前帧。OpenGL 需要（交换缓冲），Vulkan 由后端在 present 时完成，这里为空操作。
    virtual void swap_buffers() = 0;

    void register_event_callback(const EventCallback &callback);
    void emit(const WindowEvent &event) const;

    static std::unique_ptr<Window> create();
};
