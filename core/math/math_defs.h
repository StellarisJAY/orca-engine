#pragma once

typedef float real_t;

namespace Math
{
    inline constexpr real_t PI = 3.14159265358979323846;
    inline constexpr real_t E = 2.7182818284590452353602874714;
    inline constexpr real_t CMP_EPSILON = 0.00001;
    inline constexpr real_t SQRT2 = 1.4142135623730950488016887242;
    inline constexpr real_t SQRT3 = 1.7320508075688772935274463415059;
    inline constexpr real_t SQRT12 = 0.7071067811865475244008443621048490;
    inline constexpr real_t SQRT13 = 0.57735026918962576450914878050196;
    inline constexpr real_t LN2 = 0.6931471805599453094172321215;
}

enum EulerOrder{
    YXZ,
    XYZ,
    ZXY,
    ZYX
};
