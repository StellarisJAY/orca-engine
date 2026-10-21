#pragma once

#include "math_defs.h"
#include "vector3.h"
#include "math_funcs.h"
#include "transform_3d.h"

struct Quaternion
{
    union
    {
        struct
        {
            real_t x;
            real_t y;
            real_t z;
            real_t w;
        };

        real_t components[4] = {0, 0, 0, 0};
    };

    Quaternion() = default;
    Quaternion(real_t x, real_t y, real_t z, real_t w) : x(x), y(y), z(z), w(w) {}
    Quaternion(const Vector3 axis, real_t angle)
    {
        x = axis.x * Math::sin(angle / 2.0);
        y = axis.y * Math::sin(angle / 2.0);
        z = axis.z * Math::sin(angle / 2.0);
        w = Math::cos(angle / 2.0);
    }

    /// @brief 四元数转旋转矩阵
    /// @return 旋转Transform
    Transform3D get_transform() const;
    
    /// @brief 四元数转欧拉角
    /// @param order 欧拉角顺序
    /// @return x,y,z旋转
    Vector3 euler_angles(EulerOrder order) const;
};

inline Transform3D Quaternion::get_transform() const
{
    Transform3D transform;
    transform.basis.columns[0] = Vector3(
        1 - 2 * (y * y + z * z),
        2 * (x * y + w * z),
        2 * (x * z - w * y));
    transform.basis.columns[1] = Vector3(
        2 * (x * y - w * z),
        1 - 2 * (x * x + z * z),
        2 * (y * z + w * x));
    transform.basis.columns[2] = Vector3(
        2 * (x * z + w * y),
        2 * (y * z - w * x),
        1 - 2 * (x * x + y * y));
    return transform;
}

inline Vector3 Quaternion::euler_angles(EulerOrder order) const
{
    // 基向量按列存储，columns[col][row] 对应旋转矩阵 R 的元素
    const Basis b = get_transform().basis;
    const real_t R00 = b.columns[0].x, R10 = b.columns[0].y, R20 = b.columns[0].z;
    const real_t R01 = b.columns[1].x, R11 = b.columns[1].y, R21 = b.columns[1].z;
    const real_t R02 = b.columns[2].x, R12 = b.columns[2].y, R22 = b.columns[2].z;

    // 1 - 1e-6 留出足够余量，避免 float 精度误差使本应处于万向锁的姿态
    // 落到普通分支，从而产生跳变
    const real_t eps = (real_t)1 - (real_t)1e-6;
    const real_t half_pi = (real_t)Math::PI / (real_t)2;

    switch (order)
    {
    case EulerOrder::XYZ:
    {
        // R = Rz * Ry * Rx
        if (Math::abs(R20) < eps)
        {
            return Vector3(
                Math::atan2(R21, R22),
                Math::asin(Math::clamp(-R20, (real_t)-1, (real_t)1)),
                Math::atan2(R10, R00));
        }

        // 万向锁：只有 x - z 或 x + z 可确定，约定 z = 0
        const real_t sign = R20 < 0 ? 1 : -1;
        return Vector3(
            Math::atan2(sign * R01, sign * R02),
            sign * half_pi,
            0);
    }
    case EulerOrder::YXZ:
    {
        // R = Rz * Rx * Ry
        if (Math::abs(R21) < eps)
        {
            return Vector3(
                Math::asin(Math::clamp(R21, (real_t)-1, (real_t)1)),
                Math::atan2(-R20, R22),
                Math::atan2(-R01, R11));
        }

        // 万向锁：只有 y + z 或 y - z 可确定，约定 z = 0
        return Vector3(
            R21 > 0 ? half_pi : -half_pi,
            Math::atan2(R02, R00),
            0);
    }
    case EulerOrder::ZYX:
    {
        // R = Rx * Ry * Rz
        if (Math::abs(R02) < eps)
        {
            return Vector3(
                Math::atan2(-R12, R22),
                Math::asin(Math::clamp(R02, (real_t)-1, (real_t)1)),
                Math::atan2(-R01, R00));
        }

        // 万向锁：只有 x - z 或 x + z 可确定，约定 z = 0
        const real_t sign = R02 > 0 ? 1 : -1;
        return Vector3(
            Math::atan2(sign * R10, R11),
            sign * half_pi,
            0);
    }
    }

    return Vector3();
}
