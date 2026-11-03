#pragma once

#include <cstdint>
#include "math_defs.h"
#include "math_funcs.h"

struct Color
{
    union
    {
        struct
        {
            uint8_t R;
            uint8_t G;
            uint8_t B;
            uint8_t A;
        };
        uint8_t columns[4] = {255, 255, 255, 255};
    };

    Color() = default;
    Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : R(r), G(g), B(b), A(a) {}
    Color(uint8_t r, uint8_t g, uint8_t b) : R(r), G(g), B(b), A(255) {}

    bool operator==(const Color &other) const { return R == other.R && G == other.G && B == other.B && A == other.A; }
    bool operator!=(const Color &other) const { return R != other.R || G != other.G || B != other.B || A != other.A; }

    void normalize_rgb(real_t *data) const
    {
        data[0] = (real_t)R / 255.0;
        data[1] = (real_t)G / 255.0;
        data[2] = (real_t)B / 255.0;
    }
    void normalize_rgba(real_t *data) const
    {
        data[0] = (real_t)R / 255.0;
        data[1] = (real_t)G / 255.0;
        data[2] = (real_t)B / 255.0;
        data[3] = (real_t)A / 255.0;
    }

    void linear(real_t *data) const
    {
        normalize_rgb(data);
        data[0] = SRGBToLinear(data[0]);
        data[1] = SRGBToLinear(data[1]);
        data[2] = SRGBToLinear(data[2]);
    }

    real_t SRGBToLinear(real_t c) const
    {
        return (c <= 0.04045f) ? (c / 12.92f)
                               : Math::pow((c + 0.055f) / 1.055f, 2.4f);
    }
};