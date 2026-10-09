#include "transform_3d.h"

Transform3D::Transform3D(const Vector3 &scales, const Vector3 &origin) : origin(origin)
{
    basis.scale(scales);
}

Transform3D::Transform3D(const Vector3 &axis, real_t angle, const Vector3 &origin) : origin(origin)
{
    basis.set_axis_angle(axis, angle);
}

Transform3D::Transform3D(const Vector3 &axis, real_t angle, const Vector3 &scale, const Vector3 &origin) : origin(origin)
{
    basis.set_axis_angle_and_scale(axis, angle, scale);
}

void Transform3D::scale(real_t scale_x, real_t scale_y, real_t scale_z)
{
    *this = Transform3D({scale_x, scale_y, scale_z}, Vector3::ZERO) * (*this);
}

void Transform3D::scale(const Vector3 &scales)
{
    *this = Transform3D(scales, Vector3::ZERO) * (*this);
}

void Transform3D::rotate(const Vector3 &axis, real_t angle)
{
    *this = Transform3D(axis, angle, Vector3::ZERO) * (*this);
}

void Transform3D::translate(real_t t_x, real_t t_y, real_t t_z)
{
    *this = Transform3D(Vector3::ONES, {t_x, t_y, t_z}) * (*this);
}

void Transform3D::translate(const Vector3 &translations)
{
    *this = Transform3D(Vector3::ONES, translations) * (*this);
}

Transform3D Transform3D::scaled(real_t scale_x, real_t scale_y, real_t scale_z) const
{
    Transform3D result = *this;
    result.scale(scale_x, scale_y, scale_z);
    return result;
}

Transform3D Transform3D::scaled(const Vector3 &scales) const
{
    Transform3D result = *this;
    result.scale(scales);
    return result;
}

Transform3D Transform3D::rotated(const Vector3 &axis, real_t angle) const
{
    Transform3D result = *this;
    result.rotate(axis, angle);
    return result;
}

Transform3D Transform3D::translated(real_t t_x, real_t t_y, real_t t_z) const
{
    Transform3D result = *this;
    result.translate(t_x, t_y, t_z);
    return result;
}

Transform3D Transform3D::translated(const Vector3 &translations) const
{
    Transform3D result = *this;
    result.translate(translations);
    return result;
}

void Transform3D::affine_invert()
{
    const Basis basis_inverse = basis.inverse();
    origin = basis_inverse.apply(origin) * -1;
    basis = basis_inverse;
}

Transform3D Transform3D::affine_inverse() const
{
    Transform3D result = *this;
    result.affine_invert();
    return result;
}

Transform3D Transform3D::operator*(const Transform3D &other) const
{
    auto result = *this;
    result *= other;
    return result;
}

void Transform3D::operator*=(const Transform3D &other)
{
    // A = [MA, tA], B = [MB, tB]
    //   = [ 0,  1]    = [ 0,  1]
    // C = [MA*MB, MA*tB+tA]
    //   = [    0,        1]
    const Basis newBasis = basis * other.basis;
    const Vector3 newOrigin = basis.apply(other.origin) + origin;
    basis = newBasis;
    origin = newOrigin;
}

Vector3 Transform3D::apply(const Vector3 &vector) const
{
    // M = [B, t], V = [xyz]
    //     [0, 1]      [ 1 ]
    // V1 = MV = B*xyz + t
    return basis.apply(vector) + origin;
}
