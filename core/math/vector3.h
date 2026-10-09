#pragma once

#include "math_defs.h"
#include <iostream>
#include <stdexcept>

struct Vector3
{
    static const Vector3 ZERO;
    static const Vector3 UP;
    static const Vector3 DOWN;
    static const Vector3 FORWARD;
    static const Vector3 BACKWARD;
    static const Vector3 LEFT;
    static const Vector3 RIGHT;
    static const Vector3 ONES;

    enum Axis
    {
        AxisX,
        AxisY,
        AxisZ,
    };

    union
    {
        struct
        {
            real_t x;
            real_t y;
            real_t z;
        };

        real_t coords[3] = {0, 0, 0};
    };

    const real_t &operator[](int axis) const
    {
        if (!((unsigned int)axis < 3))
            throw std::out_of_range("axis out of range");
        return coords[axis];
    }

    real_t &operator[](int axis)
    {
        if (!((unsigned int)axis < 3))
            throw std::out_of_range("axis out of range");
        return coords[axis];
    }

    void normalize();
    Vector3 normalized() const;
    bool is_normalized() const;

    real_t length() const;
    real_t length_squared() const;
    Vector3 limit_length(real_t length) const;

    real_t distance_to(const Vector3 &other) const;
    real_t distance_to_squared(const Vector3 &other) const;
    real_t angle_to(const Vector3 &other) const;

    void rotate(const Vector3 &axis, real_t angle);
    Vector3 rotated(const Vector3 &axis, real_t angle) const;

    real_t dot(const Vector3 &other) const;
    Vector3 cross(const Vector3 &other) const;
    Vector3 project(const Vector3 &to) const;

    void zero();
    bool is_zero() const;
    bool is_same(const Vector3 &other) const;
    bool is_zero_approx() const;
    bool is_equal_approx(const Vector3 &other) const;

    constexpr Vector3 operator+(const Vector3 &other) const;
    constexpr void operator+=(const Vector3 &other);

    constexpr Vector3 operator-(const Vector3 &other) const;
    constexpr void operator-=(const Vector3 &other);

    constexpr Vector3 operator*(const Vector3 &other) const;
    constexpr Vector3 operator*(real_t value) const;
    constexpr void operator*=(const Vector3 &other);
    constexpr void operator*=(real_t value);

    constexpr Vector3 operator/(const Vector3 &other) const;
    constexpr Vector3 operator/(real_t value) const;
    constexpr void operator/=(const Vector3 &other);
    constexpr void operator/=(real_t value);

    constexpr bool operator==(const Vector3 &other) const;
    constexpr bool operator!=(const Vector3 &other) const;

    constexpr bool operator>(const Vector3 &other) const { return x == other.x ? y > other.y : x > other.x; }
    constexpr bool operator<(const Vector3 &other) const { return x == other.x ? y < other.y : x < other.x; }
    constexpr bool operator<=(const Vector3 &other) const { return x == other.x ? (y <= other.y) : (x < other.x); }
    constexpr bool operator>=(const Vector3 &other) const { return x == other.x ? (y >= other.y) : (x > other.x); }

    Vector3 abs() const;
    Vector3 ceil() const;
    Vector3 floor() const;
    Vector3 clamp(const Vector3 &min, const Vector3 &max) const;
    Vector3 clamp(real_t min, real_t max) const;

    constexpr Vector3() : x(0), y(0), z(0) {}
    constexpr Vector3(real_t x, real_t y, real_t z) : x(x), y(y), z(z) {}

    inline static constexpr real_t det3(const Vector3 matrix[3]);
};

inline constexpr Vector3 Vector3::ZERO = {0, 0, 0};
inline constexpr Vector3 Vector3::UP = {0, 1, 0};
inline constexpr Vector3 Vector3::DOWN = {0, -1, 0};
inline constexpr Vector3 Vector3::LEFT = {-1, 0, 0};
inline constexpr Vector3 Vector3::RIGHT = {1, 0, 0};
inline constexpr Vector3 Vector3::FORWARD = {0, 0, -1};
inline constexpr Vector3 Vector3::BACKWARD = {0, 0, 1};
inline constexpr Vector3 Vector3::ONES = {1, 1, 1};

constexpr Vector3 Vector3::operator+(const Vector3 &other) const
{
    Vector3 sum = *this;
    sum += other;
    return sum;
}

constexpr void Vector3::operator+=(const Vector3 &other)
{
    x += other.x;
    y += other.y;
    z += other.z;
}

constexpr Vector3 Vector3::operator-(const Vector3 &other) const
{
    Vector3 res = *this;
    res -= other;
    return res;
}

constexpr void Vector3::operator-=(const Vector3 &other)
{
    x -= other.x;
    y -= other.y;
    z -= other.z;
}

constexpr Vector3 Vector3::operator*(const Vector3 &other) const
{
    Vector3 res = *this;
    res *= other;
    return res;
}

constexpr Vector3 Vector3::operator*(real_t value) const
{
    Vector3 res = *this;
    res *= value;
    return res;
}

constexpr void Vector3::operator*=(const Vector3 &other)
{
    x *= other.x;
    y *= other.y;
    z *= other.z;
}

constexpr void Vector3::operator*=(real_t value)
{
    x *= value;
    y *= value;
    z *= value;
}

constexpr Vector3 Vector3::operator/(const Vector3 &other) const
{
    if (other.x == 0 || other.y == 0 || other.z == 0) throw std::invalid_argument("zero division");
    Vector3 res = *this;
    res /= other;
    return res;
}

constexpr Vector3 Vector3::operator/(real_t value) const
{
    if (value == 0) throw std::invalid_argument("zero division");
    Vector3 res = *this;
    res /= value;
    return res;
}

constexpr void Vector3::operator/=(const Vector3 &other)
{
    if (other.x == 0 || other.y == 0 || other.z == 0) throw std::invalid_argument("zero division");
    x /= other.x;
    y /= other.y;
    z /= other.z;
}

constexpr void Vector3::operator/=(real_t value)
{
    if (value == 0) throw std::invalid_argument("zero division");
    x /= value;
    y /= value;
    z /= value;
}

constexpr bool Vector3::operator==(const Vector3 &other) const { return x == other.x && y == other.y && z == other.z; }
constexpr bool Vector3::operator!=(const Vector3 &other) const { return x != other.x || y != other.y || z != other.z; }

constexpr real_t Vector3::det3(const Vector3 columns[3])
{
    return columns[0].x * (columns[1].y * columns[2].z - columns[1].z * columns[2].y)
        - columns[0].y * (columns[1].x * columns[2].z - columns[1].z * columns[2].x)
        + columns[0].z * (columns[1].x * columns[2].y - columns[1].y * columns[2].x);
}