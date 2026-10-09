#include "transform_2d.h"
#include "math_funcs.h"

Transform2D Transform2D::operator*(const Transform2D &other) const
{
    Transform2D t = *this;
    t *= other;
    return t;
}

void Transform2D::operator*=(const Transform2D &other)
{
    // 增广3x3矩阵
    // A = [MA, tA]  B = [MB, tB]
    //     [0 ,  1]      [ 0,  1]
    // AB = [MA*MB, MA*tB + tA]
    //      [    0,          1]
    // 去掉增广部分，只计算MA*MB和MA*tB+tA
    const Vector2 w = Vector2(tdotx(other.columns[2]), tdoty(other.columns[2])) + columns[2];
    real_t x0 = tdotx(other.columns[0]);
    real_t x1 = tdoty(other.columns[0]);
    real_t y0 = tdotx(other.columns[1]);
    real_t y1 = tdoty(other.columns[1]);
    set(
        {x0, x1},
        {y0, y1},
        w);
}

Transform2D::Transform2D(real_t angle, Vector2 scale, Vector2 position)
{
    const Transform2D t_scale(scale.x, 0, 0, scale.y, 0, 0);
    const Transform2D t_translation(1, 0, 0, 1, position.x, position.y);
    const Transform2D t_rotation(Math::cos(angle), Math::sin(angle), -Math::sin(angle), Math::cos(angle), 0, 0);

    *this = t_translation * (t_rotation * (t_scale * (*this)));
}

Transform2D::Transform2D(real_t angle, Vector2 position)
{
    const Transform2D t_translation(1, 0, 0, 1, position.x, position.y);
    const Transform2D t_rotation(Math::cos(angle), Math::sin(angle), -Math::sin(angle), Math::cos(angle), 0, 0);

    *this = t_translation * (t_rotation * (*this));
}

void Transform2D::scale(real_t scale_x, real_t scale_y)
{
    *this = Transform2D(scale_x, 0, 0, scale_y, 0, 0) * (*this);
}

void Transform2D::rotate(real_t angle) 
{
    *this = Transform2D(angle, Vector2::ZERO) * (*this);
}

void Transform2D::translate(real_t tx, real_t ty)
{
    *this = Transform2D(1, 0, 0, 1, tx, ty) * (*this);
}

void Transform2D::translate(const Vector2 &translation) 
{
    *this = Transform2D(1, 0, 0, 1, translation.x, translation.y) * (*this);
}

Transform2D Transform2D::scaled(real_t sx, real_t sy) const 
{
    Transform2D t = *this;
    t.scale(sx, sy);
    return t;
}
Transform2D Transform2D::rotated(real_t angle) const 
{
    Transform2D t = *this;
    t.rotate(angle);
    return t;
}
Transform2D Transform2D::translated(real_t tx, real_t ty) const
{
    Transform2D t = *this;
    t.translate(tx, ty);
    return t;
}
Transform2D Transform2D::translated(const Vector2 &translation) const
{
    Transform2D t = *this;
    t.translate(translation);
    return t;
}

void Transform2D::affine_invert()
{
    // 仿射求逆
    // A = [M, t]
    //     [0, 1]
    // A^(-1) = [M^(-1), -(M^(-1)) * t]
    //          [0     , 1            ]
    const real_t det = Vector2::det2(columns);
    const Vector2 inverse_m[2] = {{columns[1].y/det, -columns[0].y/det}, {-columns[1].x/det, columns[0].x/det}};

    const real_t tx = inverse_m[0].x * columns[2].x + inverse_m[1].x * columns[2].y;
    const real_t ty = inverse_m[0].y * columns[2].x + inverse_m[1].y * columns[2].y;
    *this = Transform2D(inverse_m, {-tx, -ty});
}

Transform2D Transform2D::affine_inverse() const
{
    Transform2D t = *this;
    t.affine_invert();
    return t;
}

// 将Transform2D应用到向量
Vector2 Transform2D::apply(const Vector2 &vector) const
{
    // 增广向量和矩阵
    // T = [M, t]  V = [a]
    //     [0, 1]      [1]
    // T*V = [M*a + t]
    //       [   1   ]
    // 只需要计算 M * a + t
    return Vector2(tdotx(vector), tdoty(vector)) + columns[2];
}

real_t Transform2D::get_rotation() const
{
    return Math::atan2(columns[0].y, columns[0].x);
}