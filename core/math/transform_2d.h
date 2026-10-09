#pragma once

#include "vector2.h"
#include "math_defs.h"

struct Transform2D
{
    Vector2 columns[3] = {
        {1, 0},
        {0, 1},
        {0, 0}};

    Transform2D(const Vector2 M[2], const Vector2 &t)
    {
        columns[0] = M[0];
        columns[1] = M[1];
        columns[2] = t;
    }

    Transform2D(real_t x0, real_t x1, real_t y0, real_t y1, real_t w0, real_t w1) 
    {
        columns[0][0] = x0; columns[0][1] = x1;
        columns[1][0] = y0; columns[1][1] = y1;
        columns[2][0] = w0; columns[2][1] = w1;
    }

    Transform2D(const Vector2 &x, const Vector2 &y, const Vector2 &w) 
    {
        columns[0][0] = x[0]; columns[0][1] = x[1];
        columns[1][0] = y[0]; columns[1][1] = y[1];
        columns[2][0] = w[0]; columns[2][1] = w[1];
    }

    void set(const Vector2 &x, const Vector2 &y, const Vector2 &w)
    {
        columns[0] = x;
        columns[1] = y;
        columns[2] = w;
    }

    inline real_t tdotx(const Vector2 &vector) const
    {
        return columns[0][0] * vector.x + columns[1][0] * vector.y;
    }
    inline real_t tdoty(const Vector2 &vector) const
    {
        return columns[0][1] * vector.x + columns[1][1] * vector.y;
    }

    Transform2D(real_t angle, Vector2 position);
    Transform2D(real_t angle, Vector2 scale, Vector2 position);

    void scale(real_t scale_x, real_t scale_y);
    void rotate(real_t angle);
    void translate(real_t tx, real_t ty);
    void translate(const Vector2 &translation);

    Transform2D scaled(real_t sx, real_t sy) const;
    Transform2D rotated(real_t angle) const;
    Transform2D translated(real_t tx, real_t ty) const;
    Transform2D translated(const Vector2 &translation) const;

    void affine_invert();
    Transform2D affine_inverse() const;

    Vector2 get_origin() const { return columns[2]; }
    void set_origin(const Vector2 &origin) { columns[2] = origin; }

    real_t get_rotation() const;

    Vector2 apply(const Vector2 &vector) const;

    Transform2D operator*(const Transform2D &other) const;
    void operator*=(const Transform2D &other);

    inline Transform2D operator*(real_t val) const;
    inline void operator*=(real_t val);

    bool operator==(const Transform2D &other) const
    {
        return columns[0] == other.columns[0] && columns[1] == other.columns[1] && other.columns[2] == columns[2];
    }
    bool operator!=(const Transform2D &other) const
    {
        return columns[0] != other.columns[0] || columns[1] != other.columns[1] || other.columns[2] != columns[2];
    }

    bool is_equal_approx(const Transform2D &other) const
    {
        return columns[0].is_equal_approx(other.columns[0]) &&
            columns[1].is_equal_approx(other.columns[1]) &&
            columns[2].is_equal_approx(other.columns[2]);
    }
};

inline Transform2D Transform2D::operator*(real_t val) const
{
    Transform2D t = *this;
    t *= val;
    return t;
}
inline void Transform2D::operator*=(real_t val)
{
    columns[0] *= val;
    columns[1] *= val;
    columns[2] *= val;
}
