#include "doctest/doctest.h"
#include "math/math_funcs.h"
#include "math/projection.h"

using doctest::Approx;

namespace
{
    // 把齐次向量经投影矩阵变换后做透视除法，得到 NDC
    Vector4 project_ndc(const Projection &projection, const Vector4 &v)
    {
        real_t clip[4];
        for (int i = 0; i < 4; i++)
        {
            clip[i] = projection.columns[0][i] * v.x
                + projection.columns[1][i] * v.y
                + projection.columns[2][i] * v.z
                + projection.columns[3][i] * v.w;
        }
        return Vector4(clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3], 1);
    }
}

TEST_CASE("Projection: create_orthogonal matrix")
{
    const Projection p = Projection::create_orthogonal(-2, 2, -1, 1, 1, 10);

    CHECK(p.columns[0].is_equal_approx(Vector4(0.5, 0, 0, 0)));
    CHECK(p.columns[1].is_equal_approx(Vector4(0, 1, 0, 0)));
    CHECK(p.columns[2].is_equal_approx(Vector4(0, 0, -2.0 / 9.0, 0)));
    CHECK(p.columns[3].is_equal_approx(Vector4(0, 0, -11.0 / 9.0, 1)));

    CHECK(p.is_orthogonal());
}

TEST_CASE("Projection: orthogonal maps the frustum to NDC")
{
    const Projection p = Projection::create_orthogonal(-2, 2, -1, 1, 1, 10);

    // x in [left,right] -> [-1, 1]
    CHECK(project_ndc(p, Vector4(-2, 0, 0, 1)).x == Approx(-1.0));
    CHECK(project_ndc(p, Vector4(2, 0, 0, 1)).x == Approx(1.0));

    // y in [bottom,top] -> [-1, 1]
    CHECK(project_ndc(p, Vector4(0, -1, 0, 1)).y == Approx(-1.0));
    CHECK(project_ndc(p, Vector4(0, 1, 0, 1)).y == Approx(1.0));

    // z: near plane (z=-1) -> -1, far plane (z=-10) -> +1
    CHECK(project_ndc(p, Vector4(0, 0, -1, 1)).z == Approx(-1.0));
    CHECK(project_ndc(p, Vector4(0, 0, -10, 1)).z == Approx(1.0));

    // 中心点投影到 NDC 原点
    const Vector4 center = project_ndc(p, Vector4(0, 0, -5.5, 1));
    CHECK(center.x == Approx(0.0));
    CHECK(center.y == Approx(0.0));
    CHECK(center.z == Approx(0.0));
}

TEST_CASE("Projection: orthogonal accessors")
{
    const Projection p = Projection::create_orthogonal(-2, 2, -1, 1, 1, 10);

    CHECK(p.get_near() == Approx(1.0));
    CHECK(p.get_far() == Approx(10.0));
    // (right-left)/(top-bottom)
    CHECK(p.get_aspect() == Approx(2.0));
    CHECK(p.is_orthogonal());
}

TEST_CASE("Projection: create_perspective matrix")
{
    const real_t aspect = 16.0 / 9.0;
    const Projection p = Projection::create_perspective(90, aspect, 1, 10);
    const real_t f = 1.0 / Math::tan(Math::deg_to_rad(45)); // = 1

    CHECK(p.columns[0].is_equal_approx(Vector4(f / aspect, 0, 0, 0)));
    CHECK(p.columns[1].is_equal_approx(Vector4(0, f, 0, 0)));
    // M22 = (near+far)/(near-far), M32 = -1
    CHECK(p.columns[2].is_equal_approx(Vector4(0, 0, (1.0 + 10.0) / (1.0 - 10.0), -1)));
    // M23 = 2*far*near/(near-far), M33 = 0
    CHECK(p.columns[3].is_equal_approx(Vector4(0, 0, 2.0 * 10.0 * 1.0 / (1.0 - 10.0), 0)));

    CHECK_FALSE(p.is_orthogonal());
}

TEST_CASE("Projection: perspective accessors")
{
    const real_t aspect = 16.0 / 9.0;
    const Projection p = Projection::create_perspective(60, aspect, 1, 100);

    CHECK(p.get_aspect() == Approx(aspect));
    CHECK(p.get_fovy() == Approx(60.0));
    CHECK_FALSE(p.is_orthogonal());
}

TEST_CASE("Projection: perspective maps near/far to -1/+1")
{
    const Projection p = Projection::create_perspective(90, 16.0 / 9.0, 1, 10);

    // z: near plane (z=-1) -> -1, far plane (z=-10) -> +1
    CHECK(project_ndc(p, Vector4(0, 0, -1, 1)).z == Approx(-1.0));
    CHECK(project_ndc(p, Vector4(0, 0, -10, 1)).z == Approx(1.0));

    // 近平面处 x 边界（near*tan(fovy/2)*aspect）映射到 ±1
    const real_t half_height = 1 * Math::tan(Math::deg_to_rad(45)); // near * tan(fovy/2)
    const real_t half_width = half_height * (16.0 / 9.0);
    CHECK(project_ndc(p, Vector4(half_width, 0, -1, 1)).x == Approx(1.0));
    CHECK(project_ndc(p, Vector4(-half_width, 0, -1, 1)).x == Approx(-1.0));
}

TEST_CASE("Projection: is_orthogonal distinguishes the two")
{
    CHECK(Projection::create_orthogonal(-1, 1, -1, 1, 1, 10).is_orthogonal());
    CHECK_FALSE(Projection::create_perspective(60, 1.5, 1, 10).is_orthogonal());
}
