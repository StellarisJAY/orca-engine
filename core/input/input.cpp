#include "input.h"
#include "os/memory.h"
#include "input_map.h"

void Input::create_singleton()
{
    singleton = memnew(Input);
    singleton->key_pressed.resize(static_cast<uint8_t>(KeyboardKeyCode::MAX));
    singleton->mouse_button_pressed.resize(static_cast<uint8_t>(MouseButton::Max));
    InputMap::create_singleton();
}

void Input::destroy_singleton()
{
    InputMap::destroy_singleton();
    memdelete(singleton);
    singleton = nullptr;
}

void Input::parse_input_event(const InputEvent &event)
{
    switch (event.type)
    {
    case InputEventType::Key:
        if (event.key_code != KeyboardKeyCode::None && event.key_code < KeyboardKeyCode::MAX)
        {
            key_pressed[static_cast<uint16_t>(event.key_code)] = event.pressed;
            buffered_events.push_back(event);
        }
        break;
    case InputEventType::MouseButton:
        if (event.mouse_button != MouseButton::None && event.mouse_button < MouseButton::Max)
        {
            mouse_button_pressed[static_cast<uint8_t>(event.mouse_button)] = event.pressed;
            buffered_events.push_back(event);
        }
    case InputEventType::MouseMotion:
        if (!event.relative.is_zero_approx())
        {
            mouse_position = event.position;
            mouse_delta = event.relative;
            buffered_events.push_back(event);
        }
        break;
    case InputEventType::MouseWheel:
        if (!event.scroll.is_zero_approx())
        {
            buffered_events.push_back(event);
        }
        break;
    default:
        break;
    }
    // 处理event的自定义输入动作
    parse_event_actions(event);
}

bool Input::is_key_pressed(KeyboardKeyCode key) const
{
    return key_pressed[static_cast<uint16_t>(key)];
}

bool Input::is_key_released(KeyboardKeyCode key) const
{
    return !key_pressed[static_cast<uint16_t>(key)];
}

bool Input::is_mouse_button_pressed(MouseButton button) const
{
    return mouse_button_pressed[static_cast<uint8_t>(button)];
}

bool Input::is_mouse_button_released(MouseButton button) const
{
    return !mouse_button_pressed[static_cast<uint8_t>(button)];
}

Vector2 Input::get_mouse_position() const
{
    return mouse_position;
}

Vector2 Input::get_mouse_velocity() const
{
    return mouse_delta; // TODO to velocity
}

bool Input::is_action_pressed(std::string action_code) const
{
    if(auto iter = action_pressed.find(action_code); iter != action_pressed.end())
    {
        return iter->second;
    }
    return false;
}

bool Input::is_action_just_pressed(std::string action_code) const
{
    if(auto iter = action_pressed_at_frame.find(action_code); iter != action_pressed_at_frame.end())
    {
        return is_action_pressed(action_code) && iter->second == frame_counter;
    }
    return false;
}

bool Input::is_action_just_released(std::string action_code) const
{
    if(auto iter = action_pressed_at_frame.find(action_code); iter != action_pressed_at_frame.end())
    {
        return !is_action_pressed(action_code) && iter->second == frame_counter - 1;
    }
    return false;
}

void Input::start_frame()
{
    frame_counter++;
}

void Input::end_frame()
{
    buffered_events.clear();
}

void Input::parse_event_actions(const InputEvent &event)
{
    std::unordered_set<std::string> actions;
    // 现在仅键盘输入和鼠标按键输入可配置为自定义输入动作，TODO 支持手柄输入
    if (event.type == InputEventType::MouseButton)
        actions = InputMap::get_singleton()->match_actions_by_mouse_button(event.mouse_button);
    else if (event.type == InputEventType::Key)
        actions = InputMap::get_singleton()->match_actions_by_key(event.key_code);

    for (std::string action_code : actions)
    {
        action_pressed.insert_or_assign(action_code, event.pressed);
        // 非重复按下事件，记录第一次按下的帧
        if (!event.echo)
            action_pressed_at_frame.insert_or_assign(action_code, frame_counter);
        // 释放action，在action_pressed_at_frame保存最后按下的帧为前一帧
        if(!event.pressed)
            action_pressed_at_frame.insert_or_assign(action_code, frame_counter - 1);
    }
}
