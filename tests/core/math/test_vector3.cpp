#include "doctest/doctest.h"
#include "math/math_funcs.h"
#include "math/vector3.h"

#include <stdexcept>

using doctest::Approx;

TEST_CASE("Vector3: construction and component access")
{
    Vector3 def;
    CHECK(def.x == 0);
    CHECK(def.y == 0);
    CHECK(def.z == 0);

    Vector3 v(3, 4, 5);
    CHECK(v.x == 3);
    CHECK(v.y == 4);
    CHECK(v.z == 5);
    CHECK(v[0] == 3);
    CHECK(v[1] == 4);
    CHECK(v[2] == 5);

    v[2] = 7;
    CHECK(v.z == 7);

    const Vector3 &cv = v;
    CHECK(cv[1] == 4);
}

TEST_CASE("Vector3: out-of-range indexing throws")
{
    Vector3 v(1, 2, 3);
    CHECK_THROWS_AS(v[3], std::out_of_range);
    CHECK_THROWS_AS(v[-1], std::out_of_range);

    const Vector3 cv(1, 2, 3);
    CHECK_THROWS_AS(cv[3], std::out_of_range);
    CHECK_THROWS_AS(cv[-1], std::out_of_range);
}

TEST_CASE("Vector3: static constants")
{
    CHECK(Vector3::ZERO == Vector3(0, 0, 0));
    CHECK(Vector3::UP == Vector3(0, 1, 0));
    CHECK(Vector3::DOWN == Vector3(0, -1, 0));
    CHECK(Vector3::LEFT == Vector3(-1, 0, 0));
    CHECK(Vector3::RIGHT == Vector3(1, 0, 0));
    CHECK(Vector3::FORWARD == Vector3(0, 0, -1));
    CHECK(Vector3::BACKWARD == Vector3(0, 0, 1));
}

TEST_CASE("Vector3: arithmetic operators")
{
    const Vector3 a(1, 2, 3);
    const Vector3 b(4, 5, 6);

    CHECK((a + b) == Vector3(5, 7, 9));
    CHECK((b - a) == Vector3(3, 3, 3));
    CHECK((a * b) == Vector3(4, 10, 18));
    CHECK((a * 2.0) == Vector3(2, 4, 6));
    CHECK((a / 2.0) == Vector3(0.5, 1, 1.5));

    Vector3 c = a;
    c += b;
    CHECK(c == Vector3(5, 7, 9));
    c -= b;
    CHECK(c == a);
    c *= b;
    CHECK(c == Vector3(4, 10, 18));
    c /= b;
    CHECK(c == a);
    c *= 2.0;
    CHECK(c == Vector3(2, 4, 6));
    c /= 2.0;
    CHECK(c == a);
}

TEST_CASE("Vector3: division by zero throws")
{
    const Vector3 a(1, 2, 3);
    CHECK_THROWS_AS(a / 0.0, std::invalid_argument);
    CHECK_THROWS_AS(a / Vector3(1, 0, 1), std::invalid_argument);
    CHECK_THROWS_AS(a / Vector3(1, 1, 0), std::invalid_argument);

    Vector3 b(1, 2, 3);
    CHECK_THROWS_AS(b /= 0.0, std::invalid_argument);
    CHECK_THROWS_AS(b /= Vector3(0, 1, 1), std::invalid_argument);
}

TEST_CASE("Vector3: equality and ordering")
{
    CHECK(Vector3(1, 2, 3) == Vector3(1, 2, 3));
    CHECK(Vector3(1, 2, 3) != Vector3(1, 2, 4));
    CHECK_FALSE(Vector3(1, 2, 3) == Vector3(1, 3, 3));

    CHECK(Vector3(2, 0, 0) > Vector3(1, 9, 9));
    CHECK(Vector3(1, 5, 0) > Vector3(1, 4, 9));
    CHECK(Vector3(1, 4, 0) < Vector3(1, 5, 0));
    CHECK(Vector3(1, 4, 0) <= Vector3(1, 4, 0));
    CHECK(Vector3(1, 4, 0) >= Vector3(1, 4, 0));
}

TEST_CASE("Vector3: length")
{
    CHECK(Vector3(3, 4, 0).length_squared() == 25);
    CHECK(Vector3(3, 4, 0).length() == 5);

    CHECK(Vector3(1, 2, 2).length_squared() == 9);
    CHECK(Vector3(1, 2, 2).length() == 3);
}

TEST_CASE("Vector3: normalization")
{
    const Vector3 v(3, 4, 0);
    const Vector3 n = v.normalized();
    CHECK(n.x == Approx(0.6));
    CHECK(n.y == Approx(0.8));
    CHECK(n.z == Approx(0.0));
    CHECK(n.length_squared() == Approx(1.0));
    CHECK(n.is_normalized());

    Vector3 z(0, 0, 0);
    CHECK(z.normalized().is_zero());

    Vector3 m(3, 4, 0);
    m.normalize();
    CHECK(m.x == Approx(0.6));
    CHECK(m.y == Approx(0.8));

    CHECK(Vector3(1, 0, 0).is_normalized());
    CHECK_FALSE(Vector3(2, 0, 0).is_normalized());
}

TEST_CASE("Vector3: limit_length")
{
    const Vector3 v(10, 0, 0);
    const Vector3 limited = v.limit_length(5);
    CHECK(limited.x == Approx(5.0));
    CHECK(limited.y == Approx(0.0));
    CHECK(limited.z == Approx(0.0));

    const Vector3 small(2, 0, 0);
    CHECK(small.limit_length(5) == small);
}

TEST_CASE("Vector3: distance")
{
    const Vector3 a(0, 0, 0);
    const Vector3 b(2, 3, 6);
    CHECK(a.distance_to_squared(b) == 49);
    CHECK(a.distance_to(b) == 7);
    CHECK(a.distance_to(a) == 0);
}

TEST_CASE("Vector3: dot and cross")
{
    const Vector3 a(1, 2, 3);
    const Vector3 b(4, 5, 6);
    CHECK(a.dot(b) == 32);
    CHECK(a.dot(a) == a.length_squared());

    CHECK(a.cross(b) == Vector3(-3, 6, -3));
    CHECK(b.cross(a) == Vector3(3, -6, 3));
    CHECK(a.cross(a).is_zero());
    CHECK(Vector3::RIGHT.cross(Vector3::UP) == Vector3::BACKWARD);
}

TEST_CASE("Vector3: project")
{
    CHECK(Vector3(1, 2, 3).project(Vector3(1, 0, 0)).is_equal_approx(Vector3(1, 0, 0)));
    CHECK(Vector3(1, 2, 3).project(Vector3(0, 1, 0)).is_equal_approx(Vector3(0, 2, 0)));
    CHECK(Vector3(1, 2, 3).project(Vector3(1, 1, 1)).is_equal_approx(Vector3(2, 2, 2)));

    // 目标向量的长度不影响投影结果
    CHECK(Vector3(1, 2, 3).project(Vector3(0, 0, 2)).is_equal_approx(Vector3(0, 0, 3)));

    // 垂直于目标 -> 零向量
    CHECK(Vector3(1, 2, 0).project(Vector3(0, 0, 1)).is_zero_approx());

    // 平行于目标 -> 自身
    CHECK(Vector3(2, 4, 6).project(Vector3(1, 2, 3)).is_equal_approx(Vector3(2, 4, 6)));

    // 零向量投影仍为零
    CHECK(Vector3::ZERO.project(Vector3(1, 2, 3)).is_zero_approx());

    const Vector3 v(1, 2, 3);
    const Vector3 to(1, 2, 2);
    const Vector3 p = v.project(to);

    // 幂等：再次投影结果不变
    CHECK(p.project(to).is_equal_approx(p));

    // 投影结果与目标共线
    CHECK(p.cross(to).is_zero_approx());

    // 投影长度不超过原向量长度（柯西-施瓦茨）
    CHECK(p.length() <= v.length() + 1e-12);
}

TEST_CASE("Vector3: abs, ceil and floor")
{
    const Vector3 v(-1.5, 2.5, -0.5);
    CHECK(v.abs() == Vector3(1.5, 2.5, 0.5));
    CHECK(v.ceil() == Vector3(-1, 3, 0));
    CHECK(v.floor() == Vector3(-2, 2, -1));
}

TEST_CASE("Vector3: clamp")
{
    CHECK(Vector3(5, -5, 0.5).clamp(0.0, 1.0) == Vector3(1, 0, 0.5));

    CHECK(Vector3(5, 5, 5).clamp(Vector3(0, 0, 0), Vector3(1, 1, 1)) == Vector3(1, 1, 1));
    CHECK(Vector3(-5, -5, -5).clamp(Vector3(0, 0, 0), Vector3(1, 1, 1)) == Vector3(0, 0, 0));
}

TEST_CASE("Vector3: zero and is_same")
{
    Vector3 v(1, 2, 3);
    CHECK_FALSE(v.is_zero());
    v.zero();
    CHECK(v.is_zero());
    CHECK(v == Vector3(0, 0, 0));

    CHECK(Vector3(1, 2, 3).is_same(Vector3(1, 2, 3)));
    CHECK_FALSE(Vector3(1, 2, 3).is_same(Vector3(1, 2, 4)));
}

TEST_CASE("Vector3: approximate comparison")
{
    CHECK(Vector3(1e-9, -1e-12, 0).is_zero_approx());
    CHECK_FALSE(Vector3(0.001, 0, 0).is_zero_approx());

    CHECK(Vector3(1 + 1e-9, 2, 3).is_equal_approx(Vector3(1, 2, 3)));
    CHECK_FALSE(Vector3(1.1, 2, 3).is_equal_approx(Vector3(1, 2, 3)));
}

TEST_CASE("Vector3: rotate about principal axes")
{
    // 右手定则
    CHECK(Vector3::RIGHT.rotated(Vector3::UP, Math::PI / 2.0).is_equal_approx(Vector3::FORWARD));
    CHECK(Vector3::FORWARD.rotated(Vector3::UP, Math::PI / 2.0).is_equal_approx(Vector3::LEFT));
    CHECK(Vector3::UP.rotated(Vector3::RIGHT, Math::PI / 2.0).is_equal_approx(Vector3::BACKWARD));
    CHECK(Vector3::BACKWARD.rotated(Vector3::RIGHT, Math::PI / 2.0).is_equal_approx(Vector3::DOWN));
    CHECK(Vector3::RIGHT.rotated(Vector3::BACKWARD, Math::PI / 2.0).is_equal_approx(Vector3::UP));
    CHECK(Vector3::UP.rotated(Vector3::BACKWARD, Math::PI / 2.0).is_equal_approx(Vector3::LEFT));
}

TEST_CASE("Vector3: rotate matches cross product for perpendicular vectors")
{
    // 绕单位轴 n 旋转 90° 时，垂直于 n 的向量映射到 n × v
    CHECK(Vector3::RIGHT.rotated(Vector3::UP, Math::PI / 2.0)
              .is_equal_approx(Vector3::UP.cross(Vector3::RIGHT)));
}

TEST_CASE("Vector3: rotate about arbitrary axis")
{
    const Vector3 axis(Math::SQRT13, Math::SQRT13, Math::SQRT13); // 单位方向 (1,1,1)

    CHECK(Vector3::RIGHT.rotated(axis, Math::deg_to_rad(120)).is_equal_approx(Vector3::UP));
    CHECK(Vector3::UP.rotated(axis, Math::deg_to_rad(120)).is_equal_approx(Vector3::BACKWARD));
    CHECK(Vector3::BACKWARD.rotated(axis, Math::deg_to_rad(120)).is_equal_approx(Vector3::RIGHT));
}

TEST_CASE("Vector3: rotation preserves length")
{
    const Vector3 v(1, 2, 3);
    const Vector3 axis(Math::SQRT13, Math::SQRT13, Math::SQRT13);

    CHECK(v.rotated(axis, Math::deg_to_rad(37)).length() == Approx(v.length()));
    CHECK(v.rotated(axis, Math::deg_to_rad(200)).length() == Approx(v.length()));
    CHECK(Vector3(4, -2, 7).rotated(Vector3::UP, 1.9).length() == Approx(Vector3(4, -2, 7).length()));
}

TEST_CASE("Vector3: zero and full rotations leave vector unchanged")
{
    const Vector3 v(1, 2, 3);
    CHECK(v.rotated(Vector3::UP, 0).is_equal_approx(v));
    CHECK(v.rotated(Vector3::UP, Math::PI * 2.0).is_equal_approx(v));

    CHECK(Vector3::ZERO.rotated(Vector3::UP, Math::PI / 2.0).is_zero());
}

TEST_CASE("Vector3: vector parallel to axis is unchanged")
{
    CHECK(Vector3(0, 3, 0).rotated(Vector3::UP, 1.234).is_equal_approx(Vector3(0, 3, 0)));
    CHECK(Vector3(5, 0, 0).rotated(Vector3::RIGHT, Math::deg_to_rad(45)).is_equal_approx(Vector3(5, 0, 0)));
}

TEST_CASE("Vector3: opposite rotations cancel")
{
    const Vector3 v(1, 2, 3);
    const Vector3 axis = Vector3(1, 2, 2) / 3.0;

    CHECK(v.rotated(axis, Math::deg_to_rad(90))
              .rotated(axis, Math::deg_to_rad(-90))
              .is_equal_approx(v));
}

TEST_CASE("Vector3: rotations about the same axis compose")
{
    const Vector3 v(1, 2, 3);
    const Vector3 axis = Vector3(1, 2, 2) / 3.0;
    const real_t a = Math::deg_to_rad(30);
    const real_t b = Math::deg_to_rad(50);

    CHECK(v.rotated(axis, a).rotated(axis, b).is_equal_approx(v.rotated(axis, a + b)));
}

TEST_CASE("Vector3: rotate mutates in place, rotated does not")
{
    Vector3 v(3, 4, 0);
    v.rotate(Vector3::BACKWARD, Math::PI / 2.0);
    CHECK(v.is_equal_approx(Vector3(-4, 3, 0)));

    const Vector3 original(3, 4, 0);
    const Vector3 result = original.rotated(Vector3::BACKWARD, Math::PI / 2.0);
    CHECK(result.is_equal_approx(Vector3(-4, 3, 0)));
    CHECK(original.is_equal_approx(Vector3(3, 4, 0)));
}

TEST_CASE("Vector3: rotate with non-normalized axis is a no-op")
{
    const Vector3 v(1, 2, 3);

    CHECK(v.rotated(Vector3(0, 2, 0), Math::PI / 2.0).is_equal_approx(v));

    Vector3 m(1, 2, 3);
    m.rotate(Vector3(0, 2, 0), Math::PI / 2.0);
    CHECK(m.is_equal_approx(v));
}
