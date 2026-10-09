#pragma once

#include <iostream>
#include <stdexcept>
#include "math_defs.h"

struct Vector2
{
    static const Vector2 LEFT;
	static const Vector2 RIGHT;
	static const Vector2 UP;
	static const Vector2 DOWN;
    static const Vector2 ZERO;

	static constexpr int AXIS_COUNT = 2;

	enum Axis {
		AXIS_X,
		AXIS_Y,
	};

    union
    {
        struct {
            real_t x;
            real_t y;
        };

        struct {
            real_t width;
            real_t height;
        };

        real_t coords[2] = {0, 0};
    };
    

    const real_t &operator[](int axis) const 
    {
        if(!((unsigned int)axis < 2)) throw std::out_of_range("axis out of range");
        return coords[axis];
    }

    real_t &operator[](int axis) 
    {
        if(!((unsigned int)axis < 2)) throw std::out_of_range("axis out of range");
        return coords[axis];
    }

    void normalize();
    Vector2 normalized() const;
    bool is_normalized() const;

    real_t length() const;
    real_t length_squared() const;
    Vector2 limit_length(real_t length) const;

    real_t distance_to(const Vector2 &other) const;
    real_t distance_to_squared(const Vector2 &other) const;
    real_t angle_to(const Vector2 &other) const;
    
    real_t dot(const Vector2 &other) const;
    real_t cross(const Vector2 &other) const;
    Vector2 project(const Vector2 &to) const;

    Vector2 direction_to(const Vector2 &target) const;
    Vector2 rotated(real_t angle) const;

    void zero();
    bool is_zero() const;
    bool is_same(const Vector2 &other) const;
    bool is_equal_approx(const Vector2 &other) const;
    bool is_zero_approx() const;

    constexpr Vector2 operator+(const Vector2 &other) const;
    constexpr void operator+=(const Vector2 &other);

    constexpr Vector2 operator-(const Vector2 &other) const;
    constexpr void operator-=(const Vector2 &other);

    constexpr Vector2 operator*(const Vector2 &other) const;
    constexpr Vector2 operator*(real_t value) const;
    constexpr void operator*=(const Vector2 &other);
    constexpr void operator*=(real_t value);

    constexpr Vector2 operator/(const Vector2 &other) const;
    constexpr Vector2 operator/(real_t value) const;
    constexpr void operator/=(const Vector2 &other);
    constexpr void operator/=(real_t value);

    constexpr bool operator==(const Vector2 &other) const;
    constexpr bool operator!=(const Vector2 &other) const;

    constexpr bool operator>(const Vector2 &other) const { return x == other.x ? y > other.y : x > other.x; }
    constexpr bool operator<(const Vector2 &other) const { return x == other.x ? y < other.y : x < other.x; }
    constexpr bool operator<=(const Vector2 &other) const { return x == other.x ? (y <= other.y) : (x < other.x); }
	constexpr bool operator>=(const Vector2 &other) const { return x == other.x ? (y >= other.y) : (x > other.x); }

    static inline constexpr real_t det2(const Vector2 columns[2])
    {
        return columns[0].x * columns[1].y - columns[0].y * columns[1].x;    
    }

    real_t angle() const;
    static Vector2 from_angle(real_t angle);

    Vector2 abs() const;
    Vector2 ceil() const;
    Vector2 floor() const;
    Vector2 clamp(const Vector2 &min, const Vector2 &max) const;
    Vector2 clamp(real_t min, real_t max) const;

    constexpr Vector2() : x(0),y(0) {}
    constexpr Vector2(real_t x, real_t y) : x(x), y(y) {}
};

inline constexpr Vector2 Vector2::UP = {0, 1};
inline constexpr Vector2 Vector2::DOWN = {0, -1};
inline constexpr Vector2 Vector2::LEFT = {-1, 0};
inline constexpr Vector2 Vector2::RIGHT = {1, 0};
inline constexpr Vector2 Vector2::ZERO = {0, 0};

constexpr Vector2 Vector2::operator+(const Vector2 &other) const
{
    Vector2 sum = *this;
    sum += other;
    return sum;
}

constexpr void Vector2::operator+=(const Vector2 &other)
{
    x += other.x;
    y += other.y;
}

constexpr Vector2 Vector2::operator-(const Vector2 &other) const
{
    Vector2 res = *this;
    res -= other;
    return res;
}

constexpr void Vector2::operator-=(const Vector2 &other)
{
    x -= other.x;
    y -= other.y;
}

constexpr Vector2 Vector2::operator*(const Vector2 &other) const
{
    Vector2 res = *this;
    res *= other;
    return res;
}

constexpr Vector2 Vector2::operator*(real_t value) const
{
    Vector2 res = *this;
    res *= value;
    return res;
}

constexpr void Vector2::operator*=(const Vector2 &other)
{
    x *= other.x;
    y *= other.y;
}

constexpr void Vector2::operator*=(real_t value)
{
    x *= value;
    y *= value;
}

constexpr Vector2 Vector2::operator/(const Vector2 &other) const
{
    if (other.x == 0 || other.y == 0) throw std::invalid_argument("zero division");
    Vector2 res = *this;
    res /= other;
    return res;
}

constexpr Vector2 Vector2::operator/(real_t value) const
{
    if (value == 0) throw std::invalid_argument("zero division");
    Vector2 res = *this;
    res /= value;
    return res;
}

constexpr void Vector2::operator/=(const Vector2 &other)
{
    if (other.x == 0 || other.y == 0) throw std::invalid_argument("zero division");
    x /= other.x;
    y /= other.y;
}

constexpr void Vector2::operator/=(real_t value)
{
    if (value == 0) throw std::invalid_argument("zero division");
    x /= value;
    y /= value;
}

constexpr bool Vector2::operator==(const Vector2 &other) const { return x == other.x && y == other.y; }
constexpr bool Vector2::operator!=(const Vector2 &other) const { return x != other.x || y != other.y; }