#pragma once

#include "math_defs.h"
#include <stdexcept>

struct Vector4
{
    static const Vector4 ZERO;
    static const Vector4 ONES;

    static constexpr int AXIS_COUNT = 4;

    enum Axis
    {
        AxisX,
        AxisY,
        AxisZ,
        AxisW,
    };

    union
    {
        struct
        {
            real_t x;
            real_t y;
            real_t z;
            real_t w;
        };

        real_t coords[4] = {0, 0, 0, 0};
    };

    const real_t &operator[](int axis) const
    {
        if (!((unsigned int)axis < 4))
            throw std::out_of_range("axis out of range");
        return coords[axis];
    }

    real_t &operator[](int axis)
    {
        if (!((unsigned int)axis < 4))
            throw std::out_of_range("axis out of range");
        return coords[axis];
    }

    void normalize();
    Vector4 normalized() const;
    bool is_normalized() const;

    real_t length() const;
    real_t length_squared() const;
    Vector4 limit_length(real_t length) const;

    real_t distance_to(const Vector4 &other) const;
    real_t distance_to_squared(const Vector4 &other) const;
    real_t angle_to(const Vector4 &other) const;

    real_t dot(const Vector4 &other) const;
    Vector4 project(const Vector4 &to) const;

    void zero();
    bool is_zero() const;
    bool is_same(const Vector4 &other) const;
    bool is_zero_approx() const;
    bool is_equal_approx(const Vector4 &other) const;

    constexpr Vector4 operator+(const Vector4 &other) const;
    constexpr void operator+=(const Vector4 &other);

    constexpr Vector4 operator-(const Vector4 &other) const;
    constexpr void operator-=(const Vector4 &other);

    constexpr Vector4 operator*(const Vector4 &other) const;
    constexpr Vector4 operator*(real_t value) const;
    constexpr void operator*=(const Vector4 &other);
    constexpr void operator*=(real_t value);

    constexpr Vector4 operator/(const Vector4 &other) const;
    constexpr Vector4 operator/(real_t value) const;
    constexpr void operator/=(const Vector4 &other);
    constexpr void operator/=(real_t value);

    constexpr bool operator==(const Vector4 &other) const;
    constexpr bool operator!=(const Vector4 &other) const;

    constexpr bool operator>(const Vector4 &other) const
    {
        if (x != other.x) return x > other.x;
        if (y != other.y) return y > other.y;
        if (z != other.z) return z > other.z;
        return w > other.w;
    }
    constexpr bool operator<(const Vector4 &other) const
    {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        if (z != other.z) return z < other.z;
        return w < other.w;
    }
    constexpr bool operator<=(const Vector4 &other) const
    {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        if (z != other.z) return z < other.z;
        return w <= other.w;
    }
    constexpr bool operator>=(const Vector4 &other) const
    {
        if (x != other.x) return x > other.x;
        if (y != other.y) return y > other.y;
        if (z != other.z) return z > other.z;
        return w >= other.w;
    }

    Vector4 abs() const;
    Vector4 ceil() const;
    Vector4 floor() const;
    Vector4 clamp(const Vector4 &min, const Vector4 &max) const;
    Vector4 clamp(real_t min, real_t max) const;

    constexpr Vector4() : x(0), y(0), z(0), w(0) {}
    constexpr Vector4(real_t x, real_t y, real_t z, real_t w) : x(x), y(y), z(z), w(w) {}
};

inline constexpr Vector4 Vector4::ZERO = {0, 0, 0, 0};
inline constexpr Vector4 Vector4::ONES = {1, 1, 1, 1};

constexpr Vector4 Vector4::operator+(const Vector4 &other) const
{
    Vector4 sum = *this;
    sum += other;
    return sum;
}

constexpr void Vector4::operator+=(const Vector4 &other)
{
    x += other.x;
    y += other.y;
    z += other.z;
    w += other.w;
}

constexpr Vector4 Vector4::operator-(const Vector4 &other) const
{
    Vector4 res = *this;
    res -= other;
    return res;
}

constexpr void Vector4::operator-=(const Vector4 &other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
    w -= other.w;
}

constexpr Vector4 Vector4::operator*(const Vector4 &other) const
{
    Vector4 res = *this;
    res *= other;
    return res;
}

constexpr Vector4 Vector4::operator*(real_t value) const
{
    Vector4 res = *this;
    res *= value;
    return res;
}

constexpr void Vector4::operator*=(const Vector4 &other)
{
    x *= other.x;
    y *= other.y;
    z *= other.z;
    w *= other.w;
}

constexpr void Vector4::operator*=(real_t value)
{
    x *= value;
    y *= value;
    z *= value;
    w *= value;
}

constexpr Vector4 Vector4::operator/(const Vector4 &other) const
{
    if (other.x == 0 || other.y == 0 || other.z == 0 || other.w == 0) throw std::invalid_argument("zero division");
    Vector4 res = *this;
    res /= other;
    return res;
}

constexpr Vector4 Vector4::operator/(real_t value) const
{
    if (value == 0) throw std::invalid_argument("zero division");
    Vector4 res = *this;
    res /= value;
    return res;
}

constexpr void Vector4::operator/=(const Vector4 &other)
{
    if (other.x == 0 || other.y == 0 || other.z == 0 || other.w == 0) throw std::invalid_argument("zero division");
    x /= other.x;
    y /= other.y;
    z /= other.z;
    w /= other.w;
}

constexpr void Vector4::operator/=(real_t value)
{
    if (value == 0) throw std::invalid_argument("zero division");
    x /= value;
    y /= value;
    z /= value;
    w /= value;
}

constexpr bool Vector4::operator==(const Vector4 &other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
constexpr bool Vector4::operator!=(const Vector4 &other) const { return x != other.x || y != other.y || z != other.z || w != other.w; }
