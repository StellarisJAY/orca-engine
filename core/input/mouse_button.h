#pragma once

#include <cstdint>

enum class MouseButton: uint8_t
{
    None = 0,
    ButtonLeft = 1,
    ButtonRight = 2,
    ButtonMiddle = 3,
    Max,
};