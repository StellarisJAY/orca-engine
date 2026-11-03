#pragma once

#include "keyboard.h"
#include "mouse_button.h"
#include "os/memory.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <unordered_set>

/// @brief 自定义输入动作，由多个按键的任意一个触发
struct InputAction
{
    std::string action_code;
    std::vector<KeyboardKeyCode> keys;
    std::vector<MouseButton> mouse_buttons;

    bool is_valid() const { return !action_code.empty() && (!keys.empty() || !mouse_buttons.empty()); }
};

class InputMap
{
public:
    friend class Input;
    /// @brief 添加自定义输入动作，无效动作和按键冲突动作将添加失败
    /// @param action 动作
    /// @return 是否添加成功
    bool add_action(const InputAction &action);

    /// @brief 删除自定义输入动作
    /// @param action_code
    void remove_action(const std::string action_code);

    /// @brief 获取自定义输入动作
    /// @param action_code 动作code
    /// @return 动作
    const InputAction &get_action(const std::string action_code) const;

    /// @brief 用键盘按键匹配自定义动作
    /// @param key 键盘按键
    /// @return 自定义动作集合
    const std::unordered_set<std::string> &match_actions_by_key(KeyboardKeyCode key) const;

    /// @brief 用鼠标按键匹配自定义动作
    /// @param mb 鼠标按键
    /// @return 自定义动作集合
    const std::unordered_set<std::string> &match_actions_by_mouse_button(MouseButton mb) const;

    static InputMap *get_singleton() { return singleton; }
private:
    static void create_singleton();
    static void destroy_singleton();
private:
    // 单例，实例由Input负责创建和销毁
    static InputMap *singleton;
    std::unordered_map<std::string, InputAction> input_actions;

    std::vector<std::unordered_set<std::string>> keycode_to_actions;
    std::vector<std::unordered_set<std::string>> mouse_button_to_actions;
};

inline InputMap *InputMap::singleton;

inline bool InputMap::add_action(const InputAction &action)
{
    // action 无效
    if (!action.is_valid())
        return false;
    // code 重复
    if (input_actions.count(action.action_code) != 0)
        return false;
    
    input_actions.emplace(std::pair(action.action_code, action));
    for(KeyboardKeyCode key_code : action.keys)
    {
        keycode_to_actions[static_cast<uint16_t>(key_code)].emplace(action.action_code);
    }
    for(MouseButton mb : action.mouse_buttons)
    {
        mouse_button_to_actions[static_cast<uint8_t>(mb)].emplace(action.action_code);
    }
    return true;
}

inline void InputMap::remove_action(const std::string action_code)
{
    input_actions.erase(action_code);
}

inline const InputAction &InputMap::get_action(const std::string action_code) const
{
    return input_actions.at(action_code);
}

inline const std::unordered_set<std::string> &InputMap::match_actions_by_key(KeyboardKeyCode key) const
{
    return keycode_to_actions[static_cast<uint16_t>(key)];
}

inline const std::unordered_set<std::string> &InputMap::match_actions_by_mouse_button(MouseButton mb) const
{
    return mouse_button_to_actions[static_cast<uint8_t>(mb)];
}

inline void InputMap::create_singleton()
{
    singleton = memnew(InputMap);
    singleton->keycode_to_actions.resize(static_cast<uint16_t>(KeyboardKeyCode::MAX));
    singleton->mouse_button_to_actions.resize(static_cast<uint8_t>(MouseButton::Max));
}

inline void InputMap::destroy_singleton()
{
    memdelete(singleton);
    singleton = nullptr;
}