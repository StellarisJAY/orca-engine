#pragma once

#include "math_defs.h"
#include "math_funcs.h"
#include "vector3.h"

struct Basis
{
    // 三个坐标轴的单位向量（列向量）
    // 注意：列主序
    Vector3 columns[3] = {
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1}};

    Basis() {}

    void set(real_t p_xx, real_t p_xy, real_t p_xz, real_t p_yx, real_t p_yy, real_t p_yz, real_t p_zx, real_t p_zy, real_t p_zz)
    {
        columns[0][0] = p_xx;
        columns[0][1] = p_xy;
        columns[0][2] = p_xz;
        columns[1][0] = p_yx;
        columns[1][1] = p_yy;
        columns[1][2] = p_yz;
        columns[2][0] = p_zx;
        columns[2][1] = p_zy;
        columns[2][2] = p_zz;
    }

    void set_axis_angle(const Vector3 &axis, real_t angle);
    real_t get_axis_angle(const Vector3 &axis) const;

    void set_axis_angle_and_scale(const Vector3 &axis, real_t angle, const Vector3 &scales);

    void invert();
    Basis inverse() const;

    void scale(const Vector3 &scales);
    Basis scaled(const Vector3 &scales) const;

    Basis(const Vector3 &axis, real_t angle) { set_axis_angle(axis, angle); }
    Basis(const Vector3 &axis, real_t angle, const Vector3 &scales) { set_axis_angle_and_scale(axis, angle, scales); }

    // 将basis应用到向量上
    Vector3 apply(const Vector3 &vector) const;

    inline Basis operator*(const Basis &matrix) const;
    inline void operator*=(const Basis &matrix);

    inline void operator*=(real_t value) { columns[0] *= value; columns[1] *= value; columns[2] *= value; }

    inline constexpr bool operator==(const Basis &other) const 
    { 
        return columns[0] == other.columns[0] 
            && columns[1] == other.columns[1] 
            && columns[2] == other.columns[2]; 
    }
    inline constexpr bool operator!=(const Basis &other) const
    {
        return columns[0] != other.columns[0] 
            || columns[1] != other.columns[1] 
            || columns[2] != other.columns[2]; 
    }

    inline bool is_equal_approx(const Basis &other) const
    {
        return columns[0].is_equal_approx(other.columns[0]) 
            && columns[1].is_equal_approx(other.columns[1])
            && columns[2].is_equal_approx(other.columns[2]);
    }

    // 转置成行主序的点乘
    inline real_t tdotx(const Vector3 &vector) const
    {
        return columns[0][0] * vector[0] + columns[1][0] * vector[1] + columns[2][0] * vector[2];
    }
    inline  real_t tdoty(const Vector3 &vector) const
    {
        return columns[0][1] * vector[0] + columns[1][1] * vector[1] + columns[2][1] * vector[2];
    }
    inline real_t tdotz(const Vector3 &vector) const
    {
        return columns[0][2] * vector[0] + columns[1][2] * vector[1] + columns[2][2] * vector[2];
    }

    Basis(real_t p_xx, real_t p_xy, real_t p_xz, real_t p_yx, real_t p_yy, real_t p_yz, real_t p_zx, real_t p_zy, real_t p_zz) : columns{
                                                                                                                                               {p_xx, p_xy, p_xz},
                                                                                                                                               {p_yx, p_yy, p_yz},
                                                                                                                                               {p_zx, p_zy, p_zz},
                                                                                                                                           }
    {
    }
};

// 乘法，遵守矩阵乘法的左右原则
inline Basis Basis::operator*(const Basis &matrix) const
{
    return Basis(
        tdotx(matrix.columns[0]), tdoty(matrix.columns[0]), tdotz(matrix.columns[0]),
        tdotx(matrix.columns[1]), tdoty(matrix.columns[1]), tdotz(matrix.columns[1]),
        tdotx(matrix.columns[2]), tdoty(matrix.columns[2]), tdotz(matrix.columns[2]));
}

// 乘法，遵守矩阵乘法的左右原则
inline void Basis::operator*=(const Basis &matrix)
{
    set(
        tdotx(matrix.columns[0]), tdoty(matrix.columns[0]), tdotz(matrix.columns[0]),
        tdotx(matrix.columns[1]), tdoty(matrix.columns[1]), tdotz(matrix.columns[1]),
        tdotx(matrix.columns[2]), tdoty(matrix.columns[2]), tdotz(matrix.columns[2])
    );
}