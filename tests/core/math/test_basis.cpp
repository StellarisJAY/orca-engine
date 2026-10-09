#include "doctest/doctest.h"
#include "math/math_funcs.h"
#include "math/basis.h"
#include "math/vector3.h"

#include <cmath>

using doctest::Approx;

namespace
{
    const real_t HALF_PI = Math::PI / 2.0;

    Basis identity_basis()
    {
        return Basis(1, 0, 0, 0, 1, 0, 0, 0, 1);
    }

    // 单位轴 (1,1,1)/sqrt(3)，其 length_squared 精确等于 1
    Vector3 diagonal_axis()
    {
        return Vector3(Math::SQRT13, Math::SQRT13, Math::SQRT13);
    }
}

TEST_CASE("Basis: default construction is identity")
{
    Basis b;
    CHECK(b == identity_basis());
    CHECK(b.columns[0] == Vector3(1, 0, 0));
    CHECK(b.columns[1] == Vector3(0, 1, 0));
    CHECK(b.columns[2] == Vector3(0, 0, 1));
    CHECK(b.apply(Vector3(3, 4, 5)) == Vector3(3, 4, 5));
}

TEST_CASE("Basis: set and equality")
{
    Basis b;
    b.set(1, 2, 3, 4, 5, 6, 7, 8, 9);

    CHECK(b == Basis(1, 2, 3, 4, 5, 6, 7, 8, 9));
    CHECK(b.columns[0] == Vector3(1, 2, 3));
    CHECK(b.columns[1] == Vector3(4, 5, 6));
    CHECK(b.columns[2] == Vector3(7, 8, 9));

    CHECK(b != identity_basis());
    CHECK_FALSE(b.is_equal_approx(identity_basis()));
    CHECK(identity_basis().is_equal_approx(identity_basis()));
}

TEST_CASE("Basis: is_equal_approx tolerance")
{
    CHECK(identity_basis().is_equal_approx(identity_basis()));

    const Basis near(1 + 1e-9, 0, 0, 0, 1, 0, 0, 0, 1);
    CHECK(near.is_equal_approx(identity_basis()));

    const Basis far(1.1, 0, 0, 0, 1, 0, 0, 0, 1);
    CHECK_FALSE(far.is_equal_approx(identity_basis()));
}

TEST_CASE("Basis: tdot rows")
{
    const Basis m(1, 2, 3, 4, 5, 6, 7, 8, 9);
    const Vector3 v(1, 10, 100);

    // 行向量点乘：row0·v = 1*1 + 4*10 + 7*100
    CHECK(m.tdotx(v) == 741);
    CHECK(m.tdoty(v) == 852);
    CHECK(m.tdotz(v) == 963);

    CHECK(m.tdotx(Vector3(1, 0, 0)) == m.columns[0][0]);
    CHECK(m.tdoty(Vector3(0, 1, 0)) == m.columns[1][1]);
    CHECK(m.tdotz(Vector3(0, 0, 1)) == m.columns[2][2]);
}

TEST_CASE("Basis: apply")
{
    const Basis m(1, 2, 3, 4, 5, 6, 7, 8, 9);
    const Vector3 v(1, 10, 100);

    // apply 等价于三个 tdot
    CHECK(m.apply(v) == Vector3(m.tdotx(v), m.tdoty(v), m.tdotz(v)));
    CHECK(m.apply(v) == Vector3(741, 852, 963));

    // 单位基向量映射到对应列
    CHECK(m.apply(Vector3(1, 0, 0)) == m.columns[0]);
    CHECK(m.apply(Vector3(0, 1, 0)) == m.columns[1]);
    CHECK(m.apply(Vector3(0, 0, 1)) == m.columns[2]);
    CHECK(m.apply(Vector3(0, 0, 0)).is_zero());

    // 线性性
    const Vector3 a(1, 2, 3);
    const Vector3 b(4, 5, 6);
    CHECK((m.apply(a) + m.apply(b)).is_equal_approx(m.apply(a + b)));
    CHECK(m.apply(a * 2.0).is_equal_approx(m.apply(a) * 2.0));
}

TEST_CASE("Basis: scale")
{
    const Basis origin;

    CHECK(origin.scaled({1, 1, 1}) == Basis(1, 0, 0, 0, 1, 0, 0, 0, 1));
    CHECK(origin.scaled({2, 2, 2}) == Basis(2, 0, 0, 0, 2, 0, 0, 0, 2));
    CHECK(origin.scaled({0.5, 0.5, 0.5}) == Basis(0.5, 0, 0, 0, 0.5, 0, 0, 0, 0.5));
    CHECK(origin.scaled({0.5, 1, 2.0}) == Basis(0.5, 0, 0, 0, 1, 0, 0, 0, 2));

    // 非正缩放因子被忽略
    CHECK(origin.scaled({0, 0, 0}) == origin);
    CHECK(origin.scaled({-1, -1, -1}) == origin);
}

TEST_CASE("Basis: scale is per-column on a rotated basis")
{
    const Basis rot(Vector3::UP, HALF_PI);

    const Basis s = rot.scaled(Vector3(2, 3, 4));
    CHECK(s.columns[0].is_equal_approx(rot.columns[0] * 2.0));
    CHECK(s.columns[1].is_equal_approx(rot.columns[1] * 3.0));
    CHECK(s.columns[2].is_equal_approx(rot.columns[2] * 4.0));

    // 不影响原对象
    CHECK(rot.is_equal_approx(Basis(Vector3::UP, HALF_PI)));

    // 就地缩放
    Basis n(Vector3::UP, HALF_PI);
    n.scale(Vector3(2, 3, 4));
    CHECK(n.is_equal_approx(s));
}

TEST_CASE("Basis: scale rejects non-positive factors")
{
    const Basis b(1, 2, 3, 4, 5, 6, 7, 8, 9);

    Basis v = b;
    v.scale(Vector3(-1, 1, 1));
    CHECK(v == b);

    v = b;
    v.scale(Vector3(1, 0, 1));
    CHECK(v == b);

    v = b;
    v.scale(Vector3(1, 1, -2));
    CHECK(v == b);
}

TEST_CASE("Basis: set_axis_angle about principal axes")
{
    CHECK(Basis(Vector3::RIGHT, HALF_PI).is_equal_approx(Basis(1, 0, 0, 0, 0, 1, 0, -1, 0)));
    CHECK(Basis(Vector3::UP, HALF_PI).is_equal_approx(Basis(0, 0, -1, 0, 1, 0, 1, 0, 0)));
    CHECK(Basis(Vector3::BACKWARD, HALF_PI).is_equal_approx(Basis(0, 1, 0, -1, 0, 0, 0, 0, 1)));

    CHECK(Basis(Vector3::UP, Math::PI).is_equal_approx(Basis(-1, 0, 0, 0, 1, 0, 0, 0, -1)));
    CHECK(Basis(Vector3::UP, -HALF_PI).is_equal_approx(Basis(0, 0, 1, 0, 1, 0, -1, 0, 0)));
}

TEST_CASE("Basis: rotation direction via apply")
{
    // 右手定则，绕 +X 旋转 +90°：+Y -> +Z，+Z -> -Y
    const Basis rx(Vector3::RIGHT, HALF_PI);
    CHECK(rx.apply(Vector3::UP).is_equal_approx(Vector3::BACKWARD));
    CHECK(rx.apply(Vector3::BACKWARD).is_equal_approx(Vector3::DOWN));

    // 绕 +Y 旋转 +90°：+X -> -Z，-Z -> -X
    const Basis ry(Vector3::UP, HALF_PI);
    CHECK(ry.apply(Vector3::RIGHT).is_equal_approx(Vector3::FORWARD));
    CHECK(ry.apply(Vector3::FORWARD).is_equal_approx(Vector3::LEFT));

    // 绕 +Z 旋转 +90°：+X -> +Y，+Y -> -X
    const Basis rz(Vector3::BACKWARD, HALF_PI);
    CHECK(rz.apply(Vector3::RIGHT).is_equal_approx(Vector3::UP));
    CHECK(rz.apply(Vector3::UP).is_equal_approx(Vector3::LEFT));
}

TEST_CASE("Basis: arbitrary axis rotation")
{
    const Basis r(diagonal_axis(), Math::deg_to_rad(120));

    // 绕 (1,1,1) 旋转 120° 循环置换三个轴
    CHECK(r.apply(Vector3::RIGHT).is_equal_approx(Vector3::UP));
    CHECK(r.apply(Vector3::UP).is_equal_approx(Vector3::BACKWARD));
    CHECK(r.apply(Vector3::BACKWARD).is_equal_approx(Vector3::RIGHT));

    // 旋转轴本身保持不变
    CHECK(r.apply(diagonal_axis()).is_equal_approx(diagonal_axis()));
}

TEST_CASE("Basis: rotation matrices are orthonormal and right-handed")
{
    const Basis rotations[] = {
        Basis(Vector3::RIGHT, HALF_PI),
        Basis(Vector3::UP, HALF_PI),
        Basis(Vector3::BACKWARD, HALF_PI),
        Basis(diagonal_axis(), Math::deg_to_rad(120)),
        Basis(Vector3(1, 2, 2) / 3.0, Math::PI),
    };

    for (const Basis &r : rotations)
    {
        const Vector3 &c0 = r.columns[0];
        const Vector3 &c1 = r.columns[1];
        const Vector3 &c2 = r.columns[2];

        CHECK(c0.length() == Approx(1.0));
        CHECK(c1.length() == Approx(1.0));
        CHECK(c2.length() == Approx(1.0));

        CHECK(c0.dot(c1) == Approx(0.0));
        CHECK(c0.dot(c2) == Approx(0.0));
        CHECK(c1.dot(c2) == Approx(0.0));

        // 右手系：col0 × col1 == col2
        CHECK(c0.cross(c1).is_equal_approx(c2));
    }
}

TEST_CASE("Basis: half-turn about arbitrary axis is an involution")
{
    const Vector3 axis(1, 2, 2);
    const Basis r180(axis / 3.0, Math::PI);

    CHECK(r180.is_equal_approx(Basis(-7.0 / 9, 4.0 / 9, 4.0 / 9,
                                    4.0 / 9, -1.0 / 9, 8.0 / 9,
                                    4.0 / 9, 8.0 / 9, -1.0 / 9)));
    CHECK((r180 * r180).is_equal_approx(identity_basis()));
}

TEST_CASE("Basis: zero and full turns are identity")
{
    CHECK(Basis(Vector3::UP, 0).is_equal_approx(identity_basis()));
    CHECK(Basis(Vector3::UP, Math::PI * 2.0).is_equal_approx(identity_basis()));
    CHECK(Basis(diagonal_axis(), 0).is_equal_approx(identity_basis()));
}

TEST_CASE("Basis: opposite rotations cancel")
{
    const Basis fwd(Vector3::UP, HALF_PI);
    const Basis back(Vector3::UP, -HALF_PI);

    CHECK((fwd * back).is_equal_approx(identity_basis()));
    CHECK((back * fwd).is_equal_approx(identity_basis()));
    CHECK((fwd * back).apply(Vector3(1, 2, 3)).is_equal_approx(Vector3(1, 2, 3)));
}

TEST_CASE("Basis: composition equals angle addition")
{
    const Basis r90(Vector3::UP, HALF_PI);
    const Basis r180(Vector3::UP, Math::PI);

    CHECK((r90 * r90).is_equal_approx(r180));
    CHECK((r90 * r90).is_equal_approx(Basis(Vector3::UP, HALF_PI + HALF_PI)));
}

TEST_CASE("Basis: multiplication with identity")
{
    const Basis m(1, 2, 3, 4, 5, 6, 7, 8, 9);
    CHECK((m * identity_basis()) == m);
    CHECK((identity_basis() * m) == m);
}

TEST_CASE("Basis: multiplication applies right side first")
{
    const Basis rx(Vector3::RIGHT, HALF_PI);
    const Basis ry(Vector3::UP, HALF_PI);
    const Vector3 v(1, 2, 3);

    CHECK((rx * ry).apply(v).is_equal_approx(rx.apply(ry.apply(v))));

    // 矩阵乘法不可交换
    CHECK_FALSE((rx * ry).is_equal_approx(ry * rx));
}

TEST_CASE("Basis: operator*= matches operator*")
{
    const Basis m(Vector3::RIGHT, HALF_PI);
    const Basis n(Vector3::UP, HALF_PI);

    Basis a = m;
    a *= n;
    CHECK(a.is_equal_approx(m * n));

    Basis b = m;
    b *= identity_basis();
    CHECK(b.is_equal_approx(m));
}

TEST_CASE("Basis: set_axis_angle_and_scale")
{
    const Basis rot(Vector3::UP, HALF_PI);

    CHECK(Basis(Vector3::UP, HALF_PI, {1, 1, 1}).is_equal_approx(rot));

    const Basis scaled(Vector3::UP, HALF_PI, {2, 3, 4});
    CHECK(scaled.columns[0].is_equal_approx(rot.columns[0] * 2.0));
    CHECK(scaled.columns[1].is_equal_approx(rot.columns[1] * 3.0));
    CHECK(scaled.columns[2].is_equal_approx(rot.columns[2] * 4.0));

    // 先缩放再旋转
    CHECK(scaled.apply(Vector3(1, 1, 1)).is_equal_approx(rot.apply(Vector3(2, 3, 4))));

    // 缩放分量全为零时保持单位阵
    CHECK(Basis(Vector3::UP, HALF_PI, {0, 0, 0}).is_equal_approx(identity_basis()));
}

TEST_CASE("Basis: invalid axis is a no-op")
{
    const Basis original(1, 2, 3, 4, 5, 6, 7, 8, 9);

    Basis b = original;
    b.set_axis_angle(Vector3(0, 2, 0), HALF_PI);
    CHECK(b == original);

    b = original;
    b.set_axis_angle_and_scale(Vector3(0, 2, 0), HALF_PI, {1, 1, 1});
    CHECK(b == original);

    // 新建的 basis 遇到非法轴时保持单位阵
    CHECK(Basis(Vector3(0, 2, 0), HALF_PI).is_equal_approx(identity_basis()));
}

TEST_CASE("Basis: set_axis_angle replaces existing contents")
{
    Basis b(1, 2, 3, 4, 5, 6, 7, 8, 9);
    b.set_axis_angle(Vector3::UP, HALF_PI);
    CHECK(b.is_equal_approx(Basis(Vector3::UP, HALF_PI)));
}

TEST_CASE("Basis: inverse")
{
    // 单位阵的逆是自身
    CHECK(identity_basis().inverse().is_equal_approx(identity_basis()));

    // 缩放的逆
    const Basis s(2, 0, 0, 0, 3, 0, 0, 0, 4);
    CHECK(s.inverse().is_equal_approx(Basis(0.5, 0, 0, 0, 1.0 / 3.0, 0, 0, 0, 0.25)));

    // 旋转的逆 = 转置 = 反向旋转
    const Basis r(Vector3::UP, HALF_PI);
    const Basis rT(r.columns[0].x, r.columns[1].x, r.columns[2].x,
                   r.columns[0].y, r.columns[1].y, r.columns[2].y,
                   r.columns[0].z, r.columns[1].z, r.columns[2].z);
    CHECK(r.inverse().is_equal_approx(rT));
    CHECK(r.inverse().is_equal_approx(Basis(Vector3::UP, -HALF_PI)));

    // 一般（含错切）矩阵的逆
    const Basis shear(1, 2, 0, 0, 1, 0, 0, 0, 1);
    CHECK(shear.inverse().is_equal_approx(Basis(1, -2, 0, 0, 1, 0, 0, 0, 1)));

    // M * M^-1 == I 且 M^-1 * M == I
    const Basis m(Vector3::UP, Math::deg_to_rad(37));
    const Basis rot_scaled = Basis(Vector3::BACKWARD, Math::deg_to_rad(20)).scaled(Vector3(2, 3, 4));
    CHECK((r * r.inverse()).is_equal_approx(identity_basis()));
    CHECK((r.inverse() * r).is_equal_approx(identity_basis()));
    CHECK((s * s.inverse()).is_equal_approx(identity_basis()));
    CHECK((m * m.inverse()).is_equal_approx(identity_basis()));
    CHECK((rot_scaled * rot_scaled.inverse()).is_equal_approx(identity_basis()));
    CHECK((rot_scaled.inverse() * rot_scaled).is_equal_approx(identity_basis()));

    // apply 往返：逆变换抵消正变换
    const Vector3 v(1, 2, 3);
    CHECK(m.inverse().apply(m.apply(v)).is_equal_approx(v));
    CHECK(rot_scaled.inverse().apply(rot_scaled.apply(v)).is_equal_approx(v));

    // 二次求逆回到自身
    CHECK(m.inverse().inverse().is_equal_approx(m));
    CHECK(rot_scaled.inverse().inverse().is_equal_approx(rot_scaled));

    // inverse() 不修改原对象
    const Basis original(Vector3::UP, Math::deg_to_rad(60));
    const Basis inverted = original.inverse();
    CHECK(original.is_equal_approx(Basis(Vector3::UP, Math::deg_to_rad(60))));
    CHECK(inverted.is_equal_approx(original.inverse()));

    // invert() 就地求逆
    Basis inplace(Vector3::UP, Math::deg_to_rad(60));
    inplace.invert();
    CHECK(inplace.is_equal_approx(inverted));
}
