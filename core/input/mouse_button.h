#pragma once

#include <cstdint>

enum class MouseButton : uint8_t
{
    None = 0,
    ButtonLeft = 1,
    ButtonRight = 2,
    ButtonMiddle = 3,
    Max,
};

// 鼠标模式，显示、隐藏、捕获
enum class MouseMode : uint8_t
{
    /// @brief 显示鼠标指针
    Visible = 0,
    /// @brief 隐藏鼠标指针，但保留鼠标指针位置移动事件
    Hidden = 1,
    /// @brief 捕获鼠标指针，鼠标指针固定在屏幕中央且隐藏，保留相对移动事件
    Captured = 2,
};