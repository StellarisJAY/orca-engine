#include "doctest/doctest.h"
#include "math/math_funcs.h"
#include "math/vector2.h"

#include <cmath>
#include <stdexcept>

using doctest::Approx;

namespace
{
    constexpr double PI = 3.14159265358979323846;
}

TEST_CASE("Vector2: construction and component access")
{
    Vector2 def;
    CHECK(def.x == 0);
    CHECK(def.y == 0);

    Vector2 v(3, 4);
    CHECK(v.x == 3);
    CHECK(v.y == 4);
    CHECK(v[0] == 3);
    CHECK(v[1] == 4);

    v[0] = 5;
    CHECK(v.x == 5);

    const Vector2 &cv = v;
    CHECK(cv[1] == 4);

    CHECK(v.width == 5);
    CHECK(v.height == 4);
}

TEST_CASE("Vector2: out-of-range indexing throws")
{
    Vector2 v(1, 2);
    CHECK_THROWS_AS(v[2], std::out_of_range);
    CHECK_THROWS_AS(v[-1], std::out_of_range);

    const Vector2 cv(1, 2);
    CHECK_THROWS_AS(cv[2], std::out_of_range);
    CHECK_THROWS_AS(cv[-1], std::out_of_range);
}

TEST_CASE("Vector2: arithmetic operators")
{
    const Vector2 a(1, 2);
    const Vector2 b(3, 4);

    CHECK((a + b) == Vector2(4, 6));
    CHECK((b - a) == Vector2(2, 2));
    CHECK((a * b) == Vector2(3, 8));
    CHECK((a * 2.0) == Vector2(2, 4));
    CHECK((a / b) == Vector2(1.0 / 3.0, 0.5));
    CHECK((b / 2.0) == Vector2(1.5, 2));

    Vector2 c = a;
    c += b;
    CHECK(c == Vector2(4, 6));
    c -= b;
    CHECK(c == Vector2(1, 2));
    c *= b;
    CHECK(c == Vector2(3, 8));
    c /= b;
    CHECK(c == Vector2(1, 2));
    c *= 2.0;
    CHECK(c == Vector2(2, 4));
    c /= 2.0;
    CHECK(c == Vector2(1, 2));
}

TEST_CASE("Vector2: division by zero throws")
{
    const Vector2 a(1, 2);
    CHECK_THROWS_AS(a / 0.0, std::invalid_argument);
    CHECK_THROWS_AS(a / Vector2(1, 0), std::invalid_argument);
    CHECK_THROWS_AS(a / Vector2(0, 1), std::invalid_argument);

    Vector2 b(1, 2);
    CHECK_THROWS_AS(b /= 0.0, std::invalid_argument);
    CHECK_THROWS_AS(b /= Vector2(0, 1), std::invalid_argument);
}

TEST_CASE("Vector2: equality and ordering")
{
    CHECK(Vector2(1, 2) == Vector2(1, 2));
    CHECK(Vector2(1, 2) != Vector2(2, 1));
    CHECK_FALSE(Vector2(1, 2) == Vector2(1, 3));

    CHECK(Vector2(2, 0) > Vector2(1, 9));
    CHECK(Vector2(1, 5) > Vector2(1, 4));
    CHECK(Vector2(1, 4) < Vector2(1, 5));
    CHECK(Vector2(1, 4) <= Vector2(1, 4));
    CHECK(Vector2(1, 4) >= Vector2(1, 4));
}

TEST_CASE("Vector2: length")
{
    const Vector2 v(3, 4);
    CHECK(v.length_squared() == 25);
    CHECK(v.length() == 5);
}

TEST_CASE("Vector2: normalization")
{
    const Vector2 v(3, 4);
    const Vector2 n = v.normalized();
    CHECK(n.x == Approx(0.6));
    CHECK(n.y == Approx(0.8));
    CHECK(n.length_squared() == Approx(1.0));
    CHECK(n.is_normalized());

    Vector2 z(0, 0);
    CHECK(z.normalized().is_zero());

    Vector2 m(3, 4);
    m.normalize();
    CHECK(m.x == Approx(0.6));
    CHECK(m.y == Approx(0.8));

    CHECK(Vector2(1, 0).is_normalized());
    CHECK_FALSE(Vector2(2, 0).is_normalized());
}

TEST_CASE("Vector2: limit_length")
{
    const Vector2 v(10, 0);
    const Vector2 limited = v.limit_length(5);
    CHECK(limited.x == Approx(5.0));
    CHECK(limited.y == Approx(0.0));

    const Vector2 small(2, 0);
    CHECK(small.limit_length(5) == small);
}

TEST_CASE("Vector2: distance")
{
    const Vector2 a(0, 0);
    const Vector2 b(3, 4);
    CHECK(a.distance_to_squared(b) == 25);
    CHECK(a.distance_to(b) == 5);
    CHECK(a.distance_to(a) == 0);
}

TEST_CASE("Vector2: dot and cross")
{
    const Vector2 a(1, 2);
    const Vector2 b(3, 4);
    CHECK(a.dot(b) == 11);
    CHECK(a.cross(b) == -2);
    CHECK(a.dot(a) == a.length_squared());
}

TEST_CASE("Vector2: project")
{
    CHECK(Vector2(3, 4).project(Vector2(1, 0)).is_equal_approx(Vector2(3, 0)));
    CHECK(Vector2(3, 4).project(Vector2(0, 1)).is_equal_approx(Vector2(0, 4)));
    CHECK(Vector2(3, 4).project(Vector2(1, 1)).is_equal_approx(Vector2(3.5, 3.5)));

    // 目标向量的长度不影响投影结果
    CHECK(Vector2(3, 4).project(Vector2(2, 0)).is_equal_approx(Vector2(3, 0)));
    CHECK(Vector2(3, 4).project(Vector2(0, 5)).is_equal_approx(Vector2(0, 4)));

    // 垂直于目标 -> 零向量
    CHECK(Vector2(3, 0).project(Vector2(0, 2)).is_zero_approx());

    // 平行于目标 -> 自身
    CHECK(Vector2(2, 4).project(Vector2(1, 2)).is_equal_approx(Vector2(2, 4)));

    // 零向量投影仍为零
    CHECK(Vector2::ZERO.project(Vector2(3, 4)).is_zero_approx());

    const Vector2 v(3, 4);
    const Vector2 to(1, 2);
    const Vector2 p = v.project(to);

    // 幂等：再次投影结果不变
    CHECK(p.project(to).is_equal_approx(p));

    // 投影结果与目标共线
    CHECK(p.cross(to) == Approx(0.0));

    // 投影长度不超过原向量长度（柯西-施瓦茨）
    CHECK(p.length() <= v.length() + 1e-12);
}

TEST_CASE("Vector2: angles")
{
    CHECK(Vector2(1, 0).angle() == Approx(0.0));
    CHECK(Vector2(0, 1).angle() == Approx(PI / 2));
    CHECK(Vector2(1, 0).angle_to(Vector2(0, 1)) == Approx(PI / 2));
    CHECK(Vector2(1, 0).angle_to(Vector2(1, 0)) == Approx(0.0));

    const Vector2 dir = Vector2::from_angle(PI / 2);
    CHECK(dir.x == Approx(0.0));
    CHECK(dir.y == Approx(1.0));
}

TEST_CASE("Vector2: abs, ceil and floor")
{
    const Vector2 v(-1.5, 2.5);
    CHECK(v.abs() == Vector2(1.5, 2.5));
    CHECK(v.ceil() == Vector2(-1, 3));
    CHECK(v.floor() == Vector2(-2, 2));
}

TEST_CASE("Vector2: clamp")
{
    CHECK(Vector2(5, -5).clamp(0.0, 1.0) == Vector2(1, 0));

    CHECK(Vector2(5, 5).clamp(Vector2(0, 0), Vector2(1, 1)) == Vector2(1, 1));
    CHECK(Vector2(-5, -5).clamp(Vector2(0, 0), Vector2(1, 1)) == Vector2(0, 0));
}

TEST_CASE("Vector2: zero and is_same")
{
    Vector2 v(1, 2);
    CHECK_FALSE(v.is_zero());
    v.zero();
    CHECK(v.is_zero());
    CHECK(v == Vector2(0, 0));

    CHECK(Vector2(1, 2).is_same(Vector2(1, 2)));
    CHECK_FALSE(Vector2(1, 2).is_same(Vector2(1, 3)));
}

TEST_CASE("Vector2: direction_to")
{
    Vector2 origin(0, 0);
    Vector2 origin2(1, 2);
    Vector2 target1(5, 2);
    CHECK(origin.direction_to(target1) == Vector2(5, 2));
    CHECK(target1.direction_to(origin) == Vector2(-5, -2));
    CHECK(origin2.direction_to(target1) == Vector2(4, 0));
    CHECK(target1.direction_to(origin2) == Vector2(-4, 0));
}

TEST_CASE("Vector2: rotated")
{
    const Vector2 v = Vector2::RIGHT;
    const real_t sqrt2_over_2 = std::sqrt(2.0) / 2.0;

    CHECK(v.rotated(Math::deg_to_rad(90)).is_equal_approx(Vector2(0, 1)));
    CHECK(v.rotated(Math::deg_to_rad(180)).is_equal_approx(Vector2(-1, 0)));
    CHECK(v.rotated(Math::deg_to_rad(270)).is_equal_approx(Vector2(0, -1)));
    CHECK(v.rotated(Math::deg_to_rad(360)).is_equal_approx(Vector2(1, 0)));
    CHECK(v.rotated(Math::deg_to_rad(45)).is_equal_approx(Vector2(sqrt2_over_2, sqrt2_over_2)));
    CHECK(v.rotated(Math::deg_to_rad(-45)).is_equal_approx(Vector2(sqrt2_over_2, -sqrt2_over_2)));
    CHECK(v.rotated(Math::deg_to_rad(-90)).is_equal_approx(Vector2(0, -1)));
}

TEST_CASE("Vector2: approximate comparison")
{
    CHECK(Vector2(1, 0).rotated(Math::PI / 2).is_equal_approx(Vector2(0, 1)));
    CHECK(Vector2(6.123233995736766e-17, 1).is_equal_approx(Vector2(0, 1)));
    CHECK_FALSE(Vector2(1, 0).is_equal_approx(Vector2(0, 1)));

    CHECK(Vector2(0, 0).is_zero_approx());
    CHECK(Vector2(6.123233995736766e-17, -1e-12).is_zero_approx());
    CHECK_FALSE(Vector2(0.001, 0).is_zero_approx());
}
