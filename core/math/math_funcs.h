#pragma once

#include <cmath>
#include <algorithm>
#include "math_defs.h"

namespace Math
{
    inline double sqrt(double value) { return std::sqrt(value);}
    inline double pow2(double value) { return value * value; }
    inline double pow(double value, double p) { return std::pow(value, p); }
    inline double acos(double value) { return std::acos(value); }
    inline double abs(double value) { return std::abs(value); }
    inline double floor(double value) { return std::floor(value); }
    inline double ceil(double value) { return std::ceil(value); }

    inline double min(double x, double y) { return std::min(x, y); }
    inline double max(double x, double y) { return std::max(x, y); }

    inline double atan2(double y, double x) { return std::atan2(y, x); }
    inline double atan(double value) { return std::atan(value); }
    inline double sin(double value) { return std::sin(value); }
    inline double cos(double value) { return std::cos(value); }
    inline double tan(double value) { return std::tan(value); }

    inline double deg_to_rad(double deg) { return deg * Math::PI / 180.0; }
    inline double rad_to_deg(double rad) { return rad * 180.0 / Math::PI; }

    inline bool is_equal_approx(real_t a, real_t b)
    {
        if (a == b) return true;
        real_t tolerance = CMP_EPSILON * Math::abs(a);
        if (tolerance < CMP_EPSILON) tolerance = CMP_EPSILON;
        return Math::abs(a - b) < tolerance;
    }

    inline bool is_zero_approx(real_t value)
    {
        return Math::abs(value) < CMP_EPSILON;
    }

    template<typename T>
    inline T clamp(T x, T min, T max)
    {
        return std::max(min, std::min(max, x));
    }
}