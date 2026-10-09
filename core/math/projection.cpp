#include "projection.h"
#include "math_funcs.h"

Projection Projection::create_orthogonal(real_t left, real_t right, real_t bottom, real_t top, real_t near, real_t far)
{
    Projection orthogonal;
    orthogonal.columns[0].x = 2 / (right-left);
    orthogonal.columns[1].y = 2 / (top - bottom);
    orthogonal.columns[2].z = -2 / (far - near);
    orthogonal.columns[3] = {-(right+left)/(right-left), -(top+bottom)/(top-bottom), -(far+near)/(far-near), 1};
    return orthogonal;
}

Projection Projection::create_perspective(real_t fovy_deg, real_t aspect, real_t near, real_t far)
{
    Projection perspective;
    const real_t tanHalfFov = Math::tan(Math::deg_to_rad(fovy_deg/2));
    perspective.columns[0].x = 1 / (aspect * tanHalfFov);
    perspective.columns[1].y = 1 / tanHalfFov;
    perspective.columns[2].z = (far + near) / (near - far);
    perspective.columns[2].w = -1;
    perspective.columns[3].z = 2*far*near / (near-far);
    perspective.columns[3].w = 0;
    return perspective;
}

real_t Projection::get_fovy() const
{
    if(is_orthogonal()) return 0;
    return Math::rad_to_deg(Math::atan(1 / columns[1].y)) * 2;
}

real_t Projection::get_near() const
{
    const real_t division = -2 / columns[2].z;
    const real_t sum = -division * columns[3].z;
    return (sum - division)/2;
}

real_t Projection::get_far() const
{
    const real_t division = -2 / columns[2].z;
    const real_t sum = -division * columns[3].z;
    return (sum + division)/2;
}

real_t Projection::get_aspect() const
{
    return columns[1].y / columns[0].x; 
}

bool Projection::is_orthogonal() const
{
    return columns[3].w == 1;
}
