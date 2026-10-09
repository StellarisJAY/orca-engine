#pragma once

#include<cstdint>
#include <memory>
#include <functional>

struct WindowDesc
{
    int width = 800;
    int height = 600;
    const char *title = "orca-engine";
    bool resizable = false;
    bool visible = true;
    bool fullscreen = false;

    WindowDesc() = default;
    WindowDesc(int width, int height) : width(width), height(height) {}
};

struct NativeWindowHandle
{
    void *window = nullptr;
    void *display = nullptr;
    void *instance = nullptr;
};

struct KeyCode
{
    
};

enum class WindowEventType
{
    Key,
    MouseButton,
    WindowSize,
    FramebufferSize,
    Close,
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
    virtual NativeWindowHandle get_native_handle() const = 0;
    virtual void poll_event() = 0;
    virtual void get_size(int &w, int &h) const = 0;
    virtual void get_frame_buffer_size(int &w, int &h) const = 0;
    void register_event_callback(const EventCallback &callback);
    void emit(const WindowEvent &event) const;

    static std::unique_ptr<Window> create();
};