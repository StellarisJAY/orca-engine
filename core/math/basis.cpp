#include "basis.h"

void Basis::set_axis_angle(const Vector3 &axis, real_t angle) 
{   
    // TODO exception
    if(!axis.is_normalized()) return;
    // 列主序的罗德里格公式
    const real_t x = axis.x; const real_t y = axis.y; const real_t z = axis.z;
    const real_t c = Math::cos(angle);
    const real_t s = Math::sin(angle);
    const real_t t = 1 - c;
    Basis rotation;
    rotation.columns[0] = Vector3(c+x*x*t, y*x*t+z*s, z*x*t-y*s);
    rotation.columns[1] = Vector3(x*y*t-z*s, c+y*y*t, z*y*t+x*s);
    rotation.columns[2] = Vector3(x*z*t+y*s, y*z*t-x*s, c+z*z*t);

    *this = rotation;
}

void Basis::set_axis_angle_and_scale(const Vector3 &axis, real_t angle, const Vector3 &scales)
{
    // TODO exception
    if(!axis.is_normalized() || scales.is_zero_approx()) return;
    // 缩放矩阵
    Basis scale; scale.scale(scales);
    // 列主序的罗德里格公式
    const real_t x = axis.x; const real_t y = axis.y; const real_t z = axis.z;
    const real_t c = Math::cos(angle);
    const real_t s = Math::sin(angle);
    const real_t t = 1 - c;
    Basis rotation;
    rotation.columns[0] = Vector3(c+x*x*t, y*x*t+z*s, z*x*t-y*s);
    rotation.columns[1] = Vector3(x*y*t-z*s, c+y*y*t, z*y*t+x*s);
    rotation.columns[2] = Vector3(x*z*t+y*s, y*z*t-x*s, c+z*z*t);

    // 先缩放，再旋转
    *this = rotation * scale;
}

void Basis::invert()
{
    real_t m00 = columns[0].x, m10 = columns[0].y, m20 = columns[0].z;
    real_t m01 = columns[1].x, m11 = columns[1].y, m21 = columns[1].z;
    real_t m02 = columns[2].x, m12 = columns[2].y, m22 = columns[2].z;

    const real_t det = Vector3::det3(columns);
    // TODO det=0, 奇异矩阵，Basis正常情况下不可能出现
    if(Math::is_zero_approx(det)) return;
    const real_t inv_det = 1 / det;
    // out 的第 0 列 = M^{-1} 的第 0 列 = (inv00, inv10, inv20)
    columns[0] = Vector3(
        (m11 * m22 - m12 * m21) * inv_det,
        (m12 * m20 - m10 * m22) * inv_det,
        (m10 * m21 - m11 * m20) * inv_det
    );
    // 第 1 列
    columns[1] = Vector3(
        (m02 * m21 - m01 * m22) * inv_det,
        (m00 * m22 - m02 * m20) * inv_det,
        (m01 * m20 - m00 * m21) * inv_det
    );
    // 第 2 列
    columns[2] = Vector3(
        (m01 * m12 - m02 * m11) * inv_det,
        (m02 * m10 - m00 * m12) * inv_det,
        (m00 * m11 - m01 * m10) * inv_det
    );
}

Basis Basis::inverse() const
{
    auto result = *this;
    result.invert();
    return result;
}

real_t Basis::get_axis_angle(const Vector3 &axis) const 
{
    return 0.0;   
}

void Basis::scale(const Vector3 &scales) 
{
    if(scales.x < 0 || scales.y < 0 || scales.z < 0) return;
    if(Math::is_zero_approx(scales.x) 
        || Math::is_zero_approx(scales.y) 
        || Math::is_zero_approx(scales.z)) return;
    columns[0] *= scales.x;
    columns[1] *= scales.y;
    columns[2] *= scales.z;
}

Basis Basis::scaled(const Vector3 &scales) const 
{
    Basis basis = *this;
    basis.scale(scales);
    return basis;
}

Vector3 Basis::apply(const Vector3 &vector) const 
{
    return Vector3(
        tdotx(vector),
        tdoty(vector),
        tdotz(vector)
    );
}