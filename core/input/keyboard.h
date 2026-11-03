#pragma once

#include <cstdint>

enum class KeyboardKeyCode: uint16_t
{
    None = 0,
    A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Escape, Enter, Ctrl, Shift, Tab, Backspace, Space, Alt, Delete,
    Left, Right, Up, Down, Home, End, PageUp, PageDown, Insert,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    MAX
};