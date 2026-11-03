#pragma once

#include "math/vector2.h"
#include <cstdint>
#include <string>
#include "keyboard.h"
#include "mouse_button.h"
#include <vector>
#include <unordered_map>

/// @brief 引擎输入事件类型
enum class InputEventType: uint8_t
{
    None = 0,
    Key,            // 键盘按下/抬起
    MouseButton,    // 鼠标键按下/抬起
    MouseMotion,    // 鼠标移动（绝对位置 + 相对位移）
    MouseWheel,     // 滚轮
    WindowFocus,    // 窗口获得/失去焦点
};

/// @brief 引擎输入事件
struct InputEvent
{
    InputEventType type = InputEventType::None;

    bool pressed = false; // 键盘按键事件，true为按下，false为释放
    bool echo = false; // 持续按下重复

    KeyboardKeyCode key_code = KeyboardKeyCode::None; // 键盘按键
    MouseButton mouse_button = MouseButton::None; // 鼠标按键

    Vector2 position = Vector2::ZERO; // 当前位置
    Vector2 relative = Vector2::ZERO; // 相对上一帧的位移
    Vector2 scroll = Vector2::ZERO; // x,y轴双轴滚动

    bool focus = false; // 窗口聚焦

    InputEventType get_type() const { return type; }
};

/// @brief 游戏输入系统
class Input
{
    static Input *singleton;
public:
    static void create_singleton();
    static void destroy_singleton();
    static Input *get_singleton() { return singleton; }
public:

    /// @brief 解析输入事件，由application层注册到platform/window层的事件回调,
    /// @brief 将platform层事件转换成Input层事件
    /// @param event 
    void parse_input_event(const InputEvent &event);

    // 键盘事件
    
    bool is_key_pressed(KeyboardKeyCode key) const;
    bool is_key_released(KeyboardKeyCode key) const;

    // 鼠标按键事件

    bool is_mouse_button_pressed(MouseButton button) const;
    bool is_mouse_button_released(MouseButton button) const;

    // 鼠标移动事件

    Vector2 get_mouse_position() const;
    Vector2 get_mouse_velocity() const;

    // 动作事件（InputMap）

    bool is_action_pressed(std::string action) const;
    bool is_action_just_pressed(std::string action) const;
    bool is_action_just_released(std::string action) const;

    // 鼠标模式，显示、隐藏、捕获
    enum class MouseMode: uint8_t {
        Visible = 0,
        Hidden = 1,
        Captured = 2,
    };
    void set_mouse_mode(MouseMode mode);
    /// @brief 一帧开始，整理buffered_events，匹配命中的自定义输入动作
    void start_frame();
    /// @brief 一帧结束，清理当前帧的事件
    void end_frame();
private:
    /// @brief 解析输入事件的自定义输入动作
    /// @param event 输入事件
    void parse_event_actions(const InputEvent &event);
private:
    std::vector<InputEvent> buffered_events;
    std::vector<bool> key_pressed;
    std::vector<bool> mouse_button_pressed;
    /// @brief input记录的当前帧
    uint64_t frame_counter = 0;
    /// @brief 动作在当前帧是否被按下
    std::unordered_map<std::string, bool> action_pressed;
    /// @brief 记录动作按下的首帧
    std::unordered_map<std::string, uint64_t> action_pressed_at_frame;

    Vector2 mouse_position = Vector2::ZERO;
    Vector2 mouse_delta = Vector2::ZERO;
};

// 静态单例的类外定义（与 RenderingServer 的写法一致），缺了它会在链接期报 undefined reference。
inline Input *Input::singleton = nullptr;