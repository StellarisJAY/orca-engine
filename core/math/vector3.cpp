#include "vector3.h"
#include "math_funcs.h"
#include "basis.h"

void Vector3::normalize()
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
        z = z / l;
    }
}

Vector3 Vector3::normalized() const
{
    auto v = *this;
    v.normalize();
    return v;
}

bool Vector3::is_normalized() const
{
    return length_squared() == 1;
}

real_t Vector3::length() const
{
    return Math::sqrt(length_squared());
}

real_t Vector3::length_squared() const
{
    return x * x + y * y + z * z;
}

Vector3 Vector3::limit_length(real_t length) const
{
    const real_t l = length_squared();
    const real_t limit = length * length;
    if (l <= limit)
        return *this;
    return normalized() * length;
}

real_t Vector3::distance_to(const Vector3 &other) const
{
    return Math::sqrt(distance_to_squared(other));
}

real_t Vector3::distance_to_squared(const Vector3 &other) const
{
    return Math::pow2(x - other.x) + Math::pow2(y - other.y) + Math::pow2(z - other.z);
}

real_t Vector3::angle_to(const Vector3 &other) const
{
    const real_t angle_cos = dot(other) / (length() * other.length());
    return Math::acos(angle_cos);
}

void Vector3::rotate(const Vector3 &axis, real_t angle) 
{
    const Basis rotation(axis, angle);
    *this = rotation.apply(*this);
}

Vector3 Vector3::rotated(const Vector3 &axis, real_t angle) const 
{
    const Basis rotation(axis, angle);
    return rotation.apply(*this);
}

real_t Vector3::dot(const Vector3 &other) const
{
    return x * other.x + y * other.y + z * other.z;
}

Vector3 Vector3::project(const Vector3 &to) const
{
    return to.normalized() * (dot(to) / to.length());
}

Vector3 Vector3::cross(const Vector3 &other) const
{
    return Vector3(
        y * other.z - z * other.y,
        z * other.x - x * other.z,
        x * other.y - y * other.x);
}

void Vector3::zero()
{
    x = 0;
    y = 0;
    z = 0;
}
bool Vector3::is_zero() const { return x == 0 && y == 0 && z == 0; }
bool Vector3::is_same(const Vector3 &other) const { return x == other.x && y == other.y && z == other.z; }
bool Vector3::is_zero_approx() const { return Math::is_zero_approx(x) && Math::is_zero_approx(y) && Math::is_zero_approx(z); }
bool Vector3::is_equal_approx(const Vector3 &other) const { return Math::is_equal_approx(x, other.x) && Math::is_equal_approx(y, other.y) && Math::is_equal_approx(z, other.z); }

Vector3 Vector3::abs() const { return Vector3(Math::abs(x), Math::abs(y), Math::abs(z)); }
Vector3 Vector3::ceil() const { return Vector3(Math::ceil(x), Math::ceil(y), Math::ceil(z)); }
Vector3 Vector3::floor() const { return Vector3(Math::floor(x), Math::floor(y), Math::floor(z)); }
Vector3 Vector3::clamp(const Vector3 &min, const Vector3 &max) const
{
    return Vector3(Math::clamp(x, min.x, max.x), Math::clamp(y, min.y, max.y), Math::clamp(z, min.z, max.z));
}

Vector3 Vector3::clamp(real_t min, real_t max) const
{
    return Vector3(Math::clamp(x, min, max), Math::clamp(y, min, max), Math::clamp(z, min, max));
}
