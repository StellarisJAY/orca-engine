#pragma once

#include "basis.h"

struct Transform3D
{
    Basis basis;
    Vector3 origin;

    Transform3D(const Basis &basis, const Vector3 &origin): basis(basis), origin(origin) {}
    Transform3D(const Vector3& scales, const Vector3& origin);
    Transform3D(const Vector3 &axis, real_t angle, const Vector3 &origin);
    Transform3D(const Vector3 &axis, real_t angle, const Vector3 &scale, const Vector3 &origin);

    void scale(real_t scale_x, real_t scale_y, real_t scale_z);
    void scale(const Vector3 &scales);
    void rotate(const Vector3 &axis, real_t angle);
    void translate(real_t t_x, real_t t_y, real_t t_z);
    void translate(const Vector3 &translations);

    Transform3D scaled(real_t scale_x, real_t scale_y, real_t scale_z) const;
    Transform3D scaled(const Vector3 &scales) const;
    Transform3D rotated(const Vector3 &axis, real_t angle) const;
    Transform3D translated(real_t t_x, real_t t_y, real_t t_z) const;
    Transform3D translated(const Vector3 &translations) const;

    void affine_invert();
    Transform3D affine_inverse() const;

    Transform3D operator*(const Transform3D &other) const;
    void operator*=(const Transform3D &other);

    Vector3 apply(const Vector3 &vector) const;

    inline Transform3D operator*(real_t val) const;
    inline void operator*=(real_t val);

    bool operator==(const Transform3D &other) const
    {
        return basis == other.basis && origin == other.origin;
    }

    bool operator!=(const Transform3D &other) const
    {
        return basis != other.basis || origin != other.origin;
    }

    bool is_equal_approx(const Transform3D &other) const
    {
        return basis.is_equal_approx(other.basis) && origin.is_equal_approx(other.origin);
    }

    real_t tdotx(const Vector3 &vector) const 
    {
        return basis.columns[0].x * vector.x + basis.columns[1].x * vector.y + basis.columns[2].x * vector.z;
    }

    real_t tdoty(const Vector3 &vector) const 
    {
        return basis.columns[0].y * vector.x + basis.columns[1].y * vector.y + basis.columns[2].y * vector.z;
    }

    real_t tdotz(const Vector3 &vector) const 
    {
        return basis.columns[0].z * vector.x + basis.columns[1].z * vector.y + basis.columns[2].z * vector.z;
    }
};

inline Transform3D Transform3D::operator*(real_t val) const
{
    auto result = *this;
    result *= val;
    return result;
}

inline void Transform3D::operator*=(real_t val)
{
    origin *= val;
    basis *= val;
}