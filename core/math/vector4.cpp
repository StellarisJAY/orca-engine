#include "vector4.h"
#include "math_funcs.h"

void Vector4::normalize()
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
        w = w / l;
    }
}

Vector4 Vector4::normalized() const
{
    auto v = *this;
    v.normalize();
    return v;
}

bool Vector4::is_normalized() const
{
    return length_squared() == 1;
}

real_t Vector4::length() const
{
    return Math::sqrt(length_squared());
}

real_t Vector4::length_squared() const
{
    return x * x + y * y + z * z + w * w;
}

Vector4 Vector4::limit_length(real_t length) const
{
    const real_t l = length_squared();
    const real_t limit = length * length;
    if (l <= limit)
        return *this;
    return normalized() * length;
}

real_t Vector4::distance_to(const Vector4 &other) const
{
    return Math::sqrt(distance_to_squared(other));
}

real_t Vector4::distance_to_squared(const Vector4 &other) const
{
    return Math::pow2(x - other.x) + Math::pow2(y - other.y) + Math::pow2(z - other.z) + Math::pow2(w - other.w);
}

real_t Vector4::angle_to(const Vector4 &other) const
{
    const real_t angle_cos = dot(other) / (length() * other.length());
    return Math::acos(angle_cos);
}

real_t Vector4::dot(const Vector4 &other) const
{
    return x * other.x + y * other.y + z * other.z + w * other.w;
}

Vector4 Vector4::project(const Vector4 &to) const
{
    return to.normalized() * (dot(to) / to.length());
}

void Vector4::zero()
{
    x = 0;
    y = 0;
    z = 0;
    w = 0;
}
bool Vector4::is_zero() const { return x == 0 && y == 0 && z == 0 && w == 0; }
bool Vector4::is_same(const Vector4 &other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
bool Vector4::is_zero_approx() const { return Math::is_zero_approx(x) && Math::is_zero_approx(y) && Math::is_zero_approx(z) && Math::is_zero_approx(w); }
bool Vector4::is_equal_approx(const Vector4 &other) const { return Math::is_equal_approx(x, other.x) && Math::is_equal_approx(y, other.y) && Math::is_equal_approx(z, other.z) && Math::is_equal_approx(w, other.w); }

Vector4 Vector4::abs() const { return Vector4(Math::abs(x), Math::abs(y), Math::abs(z), Math::abs(w)); }
Vector4 Vector4::ceil() const { return Vector4(Math::ceil(x), Math::ceil(y), Math::ceil(z), Math::ceil(w)); }
Vector4 Vector4::floor() const { return Vector4(Math::floor(x), Math::floor(y), Math::floor(z), Math::floor(w)); }
Vector4 Vector4::clamp(const Vector4 &min, const Vector4 &max) const
{
    return Vector4(Math::clamp(x, min.x, max.x), Math::clamp(y, min.y, max.y), Math::clamp(z, min.z, max.z), Math::clamp(w, min.w, max.w));
}

Vector4 Vector4::clamp(real_t min, real_t max) const
{
    return Vector4(Math::clamp(x, min, max), Math::clamp(y, min, max), Math::clamp(z, min, max), Math::clamp(w, min, max));
}
