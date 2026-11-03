#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "rendering_device.h" // RenderingDriverType / RenderingWindowInfo
// 键位与鼠标枚举复用 core/input 的引擎定义，避免平台层和输入层出现两套枚举。
#include "keyboard.h"      // KeyboardKeyCode
#include "mouse_button.h"  // MouseButton

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

/// @brief 平台层窗口事件类型。
///
/// 平台层只负责把 GLFW / SDL3 的原生事件翻译成这里的统一事件，
/// 不包含任何图形 API 或场景层概念。上层（application / core/input）再把
/// WindowEvent 转成 InputEvent 并交给场景树分发。
enum class WindowEventType
{
    None = 0,
    Key,             ///< 键盘按下 / 抬起（自动重复通过 repeat 标记）
    MouseButton,     ///< 鼠标键按下 / 抬起
    MouseMotion,     ///< 鼠标移动：绝对坐标 + 相对位移
    MouseWheel,      ///< 滚轮（双轴，兼容触摸板）
    TextInput,       ///< 文本输入，已解码为 Unicode 码点
    CursorEnter,     ///< 光标进入 / 离开窗口客户区
    Focus,           ///< 窗口获得 / 失去键盘焦点
    WindowSize,      ///< 窗口逻辑尺寸变化（单位：窗口坐标）
    FramebufferSize, ///< 帧缓冲像素尺寸变化（高 DPI 下与 WindowSize 不同）
    Close,           ///< 用户请求关闭窗口
};

/// @brief 事件发生时按住的修饰键，位掩码。
enum WindowKeyModifier : uint16_t
{
    WINDOW_MOD_NONE = 0,
    WINDOW_MOD_SHIFT = 1 << 0,
    WINDOW_MOD_CTRL = 1 << 1,
    WINDOW_MOD_ALT = 1 << 2,
    WINDOW_MOD_SUPER = 1 << 3,
    WINDOW_MOD_CAPS_LOCK = 1 << 4,
    WINDOW_MOD_NUM_LOCK = 1 << 5,
};

/// @brief 鼠标键按住状态位掩码，用于 MouseMotion / MouseButton 事件。
enum WindowMouseButtonMask : uint32_t
{
    WINDOW_MOUSE_MASK_NONE = 0,
    WINDOW_MOUSE_MASK_LEFT = 1u << 0,
    WINDOW_MOUSE_MASK_RIGHT = 1u << 1,
    WINDOW_MOUSE_MASK_MIDDLE = 1u << 2,
};

/// @brief 平台层统一事件。
///
/// 所有坐标都是**窗口坐标**（客户区左上角为原点，y 向下），
/// 高 DPI 下与帧缓冲像素不相等；渲染需要的像素尺寸请用 get_frame_buffer_size()。
struct WindowEvent
{
    WindowEventType type = WindowEventType::None;

    // ---- 通用 ----
    uint16_t modifiers = WINDOW_MOD_NONE; ///< WindowKeyModifier 位组合
    bool pressed = false;                 ///< Key / MouseButton：按下为 true
    bool repeat = false;                  ///< Key：是否是自动重复

    // ---- 键盘 ----
    KeyboardKeyCode key_code = KeyboardKeyCode::None;

    // ---- 文本 ----
    char32_t unicode = 0;

    // ---- 鼠标 ----
    MouseButton mouse_button = MouseButton::None;
    uint32_t button_mask = WINDOW_MOUSE_MASK_NONE; ///< 事件发生时的鼠标键按住状态
    float x = 0.0f;                                ///< 窗口坐标
    float y = 0.0f;
    float delta_x = 0.0f; ///< 相对上一次移动的位移（捕获鼠标时使用）
    float delta_y = 0.0f;
    float scroll_x = 0.0f; ///< 滚轮横向格数，右为正
    float scroll_y = 0.0f; ///< 滚轮纵向格数，远离用户为正

    // ---- 光标 / 焦点 ----
    bool entered = false; ///< CursorEnter：进入为 true
    bool focused = false; ///< Focus：获得焦点为 true

    // ---- 尺寸 ----
    int w = 0;
    int h = 0;
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
    // 窗口逻辑尺寸（窗口坐标），GUI / 鼠标坐标使用这一套。
    virtual void get_size(int &w, int &h) const = 0;
    // 帧缓冲像素尺寸，渲染视口 / aspect 使用这一套。
    virtual void get_frame_buffer_size(int &w, int &h) const = 0;
    // 呈现当前帧。OpenGL 需要（交换缓冲），Vulkan 由后端在 present 时完成，这里为空操作。
    virtual void swap_buffers() = 0;

    void register_event_callback(const EventCallback &callback);
    void emit(const WindowEvent &event) const;

    static std::unique_ptr<Window> create();
};
