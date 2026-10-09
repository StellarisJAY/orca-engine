#include "vector2.h"
#include "math_funcs.h"

void Vector2::normalize()
{
    real_t l = length_squared();
    if (l == 0)
    {
        zero();
    }
    else
    {
        l = Math::sqrt(l);
        x = x / l;
        y = y / l;
    }
}

Vector2 Vector2::normalized() const
{
    auto v = *this;
    v.normalize();
    return v;
}

bool Vector2::is_normalized() const
{
    return length_squared() == 1;
}

real_t Vector2::length() const
{
    return Math::sqrt(length_squared());
}

real_t Vector2::length_squared() const
{
    return x * x + y * y;
}

Vector2 Vector2::limit_length(real_t length) const
{
    const real_t l = length_squared();
    const real_t limit = length * length;
    if (l <= limit)
        return *this;
    Vector2 result = normalized() * length;
    return result;
}

real_t Vector2::distance_to(const Vector2 &other) const
{
    return Math::sqrt(distance_to_squared(other));
}

real_t Vector2::distance_to_squared(const Vector2 &other) const
{
    return Math::pow2(x - other.x) + Math::pow2(y - other.y);
}

real_t Vector2::angle_to(const Vector2 &other) const
{
    const real_t angle_cos = dot(other) / (length() * other.length());
    return Math::acos(angle_cos);
}

real_t Vector2::dot(const Vector2 &other) const
{
    return x * other.x + y * other.y;
}

real_t Vector2::cross(const Vector2 &other) const
{
    return x * other.y - y * other.x;
}

Vector2 Vector2::project(const Vector2 &to) const
{
    return to.normalized() * (dot(to) / to.length());
}

Vector2 Vector2::direction_to(const Vector2 &target) const
{
    return Vector2(target.x - x, target.y - y);
}

Vector2 Vector2::rotated(real_t angle) const
{
    const real_t sine = Math::sin(angle);
    const real_t cosi = Math::cos(angle);
    return Vector2(
        x * cosi - y * sine,
        x * sine + y * cosi);
}

void Vector2::zero()
{
    x = 0;
    y = 0;
}
bool Vector2::is_zero() const { return x == 0 && y == 0; }
bool Vector2::is_same(const Vector2 &other) const { return x == other.x && y == other.y; }
bool Vector2::is_equal_approx(const Vector2 &other) const { return Math::is_equal_approx(x, other.x) && Math::is_equal_approx(y, other.y); }
bool Vector2::is_zero_approx() const { return Math::is_zero_approx(x) && Math::is_zero_approx(y); }

real_t Vector2::angle() const
{
    return Math::atan2(y, x);
}

Vector2 Vector2::from_angle(real_t angle)
{
    return Vector2(Math::cos(angle), Math::sin(angle));
}

Vector2 Vector2::abs() const { return Vector2(Math::abs(x), Math::abs(y)); }
Vector2 Vector2::ceil() const { return Vector2(Math::ceil(x), Math::ceil(y)); }
Vector2 Vector2::floor() const { return Vector2(Math::floor(x), Math::floor(y)); }
Vector2 Vector2::clamp(const Vector2 &min, const Vector2 &max) const
{
    return Vector2(Math::clamp(x, min.x, max.x), Math::clamp(y, min.y, max.y));
}

Vector2 Vector2::clamp(real_t min, real_t max) const
{
    return Vector2(Math::clamp(x, min, max), Math::clamp(y, min, max));
}
