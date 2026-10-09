#include "doctest/doctest.h"
#include "math/math_funcs.h"
#include "math/vector4.h"

#include <stdexcept>

using doctest::Approx;

TEST_CASE("Vector4: construction and component access")
{
    Vector4 def;
    CHECK(def.x == 0);
    CHECK(def.y == 0);
    CHECK(def.z == 0);
    CHECK(def.w == 0);

    Vector4 v(1, 2, 3, 4);
    CHECK(v.x == 1);
    CHECK(v.y == 2);
    CHECK(v.z == 3);
    CHECK(v.w == 4);
    CHECK(v[0] == 1);
    CHECK(v[1] == 2);
    CHECK(v[2] == 3);
    CHECK(v[3] == 4);

    v[3] = 7;
    CHECK(v.w == 7);

    const Vector4 &cv = v;
    CHECK(cv[2] == 3);
}

TEST_CASE("Vector4: out-of-range indexing throws")
{
    Vector4 v(1, 2, 3, 4);
    CHECK_THROWS_AS(v[4], std::out_of_range);
    CHECK_THROWS_AS(v[-1], std::out_of_range);

    const Vector4 cv(1, 2, 3, 4);
    CHECK_THROWS_AS(cv[4], std::out_of_range);
    CHECK_THROWS_AS(cv[-1], std::out_of_range);
}

TEST_CASE("Vector4: static constants")
{
    CHECK(Vector4::ZERO == Vector4(0, 0, 0, 0));
    CHECK(Vector4::ONES == Vector4(1, 1, 1, 1));
}

TEST_CASE("Vector4: arithmetic operators")
{
    const Vector4 a(1, 2, 3, 4);
    const Vector4 b(4, 3, 2, 1);

    CHECK((a + b) == Vector4(5, 5, 5, 5));
    CHECK((b - a) == Vector4(3, 1, -1, -3));
    CHECK((a * b) == Vector4(4, 6, 6, 4));
    CHECK((a * 2.0) == Vector4(2, 4, 6, 8));
    CHECK((a / 2.0) == Vector4(0.5, 1, 1.5, 2));
    CHECK((b / a) == Vector4(4, 1.5, 2.0 / 3.0, 0.25));

    Vector4 c = a;
    c += b;
    CHECK(c == Vector4(5, 5, 5, 5));
    c -= b;
    CHECK(c == a);
    c *= b;
    CHECK(c == Vector4(4, 6, 6, 4));
    c /= b;
    CHECK(c == a);
    c *= 2.0;
    CHECK(c == Vector4(2, 4, 6, 8));
    c /= 2.0;
    CHECK(c == a);
}

TEST_CASE("Vector4: division by zero throws")
{
    const Vector4 a(1, 2, 3, 4);
    CHECK_THROWS_AS(a / 0.0, std::invalid_argument);
    CHECK_THROWS_AS(a / Vector4(1, 1, 1, 0), std::invalid_argument);
    CHECK_THROWS_AS(a / Vector4(0, 1, 1, 1), std::invalid_argument);

    Vector4 b(1, 2, 3, 4);
    CHECK_THROWS_AS(b /= 0.0, std::invalid_argument);
    CHECK_THROWS_AS(b /= Vector4(1, 1, 0, 1), std::invalid_argument);
}

TEST_CASE("Vector4: equality and ordering")
{
    CHECK(Vector4(1, 2, 3, 4) == Vector4(1, 2, 3, 4));
    CHECK(Vector4(1, 2, 3, 4) != Vector4(1, 2, 3, 5));
    CHECK_FALSE(Vector4(1, 2, 3, 4) == Vector4(1, 2, 4, 4));

    CHECK(Vector4(2, 0, 0, 0) > Vector4(1, 9, 9, 9));
    CHECK(Vector4(1, 5, 0, 0) > Vector4(1, 4, 9, 9));
    CHECK(Vector4(1, 2, 5, 0) > Vector4(1, 2, 4, 9));
    CHECK(Vector4(1, 2, 3, 5) > Vector4(1, 2, 3, 4));
    CHECK(Vector4(1, 2, 3, 4) < Vector4(1, 2, 3, 5));
    CHECK(Vector4(1, 2, 3, 4) <= Vector4(1, 2, 3, 4));
    CHECK(Vector4(1, 2, 3, 4) >= Vector4(1, 2, 3, 4));
}

TEST_CASE("Vector4: length")
{
    CHECK(Vector4(1, 2, 2, 4).length_squared() == 25);
    CHECK(Vector4(1, 2, 2, 4).length() == 5);

    CHECK(Vector4(1, 0, 0, 0).length_squared() == 1);
    CHECK(Vector4(1, 1, 1, 1).length_squared() == 4);
}

TEST_CASE("Vector4: normalization")
{
    const Vector4 v(3, 4, 0, 0);
    const Vector4 n = v.normalized();
    CHECK(n.x == Approx(0.6));
    CHECK(n.y == Approx(0.8));
    CHECK(n.z == Approx(0.0));
    CHECK(n.w == Approx(0.0));
    CHECK(n.length_squared() == Approx(1.0));
    CHECK(n.is_normalized());

    Vector4 z(0, 0, 0, 0);
    CHECK(z.normalized().is_zero());

    Vector4 m(3, 4, 0, 0);
    m.normalize();
    CHECK(m.x == Approx(0.6));
    CHECK(m.y == Approx(0.8));

    CHECK(Vector4(1, 0, 0, 0).is_normalized());
    CHECK_FALSE(Vector4(2, 0, 0, 0).is_normalized());
}

TEST_CASE("Vector4: limit_length")
{
    const Vector4 v(10, 0, 0, 0);
    const Vector4 limited = v.limit_length(5);
    CHECK(limited.x == Approx(5.0));
    CHECK(limited.y == Approx(0.0));
    CHECK(limited.z == Approx(0.0));
    CHECK(limited.w == Approx(0.0));

    const Vector4 small(2, 0, 0, 0);
    CHECK(small.limit_length(5) == small);
}

TEST_CASE("Vector4: distance")
{
    const Vector4 a(0, 0, 0, 0);
    const Vector4 b(1, 2, 2, 4);
    CHECK(a.distance_to_squared(b) == 25);
    CHECK(a.distance_to(b) == 5);
    CHECK(a.distance_to(a) == 0);
}

TEST_CASE("Vector4: dot")
{
    const Vector4 a(1, 2, 3, 4);
    const Vector4 b(4, 3, 2, 1);
    CHECK(a.dot(b) == 20);
    CHECK(a.dot(a) == a.length_squared());
    CHECK(a.dot(Vector4::ZERO) == 0);
}

TEST_CASE("Vector4: angle_to")
{
    CHECK(Vector4(1, 0, 0, 0).angle_to(Vector4(1, 0, 0, 0)) == Approx(0.0));
    CHECK(Vector4(1, 0, 0, 0).angle_to(Vector4(0, 1, 0, 0)) == Approx(Math::PI / 2.0));
    CHECK(Vector4(1, 1, 0, 0).angle_to(Vector4(1, 0, 0, 0)) == Approx(Math::PI / 4.0));
    CHECK(Vector4(1, 0, 0, 0).angle_to(Vector4(-1, 0, 0, 0)) == Approx(Math::PI));
}

TEST_CASE("Vector4: project")
{
    CHECK(Vector4(1, 2, 3, 4).project(Vector4(1, 0, 0, 0)).is_equal_approx(Vector4(1, 0, 0, 0)));
    CHECK(Vector4(3, 4, 0, 0).project(Vector4(1, 1, 0, 0)).is_equal_approx(Vector4(3.5, 3.5, 0, 0)));

    // 垂直于目标 -> 零向量
    CHECK(Vector4(1, 2, 0, 0).project(Vector4(0, 0, 1, 0)).is_zero_approx());

    // 平行于目标 -> 自身
    CHECK(Vector4(2, 4, 6, 8).project(Vector4(1, 2, 3, 4)).is_equal_approx(Vector4(2, 4, 6, 8)));

    // 零向量投影仍为零
    CHECK(Vector4::ZERO.project(Vector4(1, 2, 3, 4)).is_zero_approx());

    const Vector4 v(1, 2, 3, 4);
    const Vector4 to(1, 2, 2, 1);
    const Vector4 p = v.project(to);

    // 幂等
    CHECK(p.project(to).is_equal_approx(p));
    // 残差垂直于目标
    CHECK((v - p).dot(to) == Approx(0.0));
}

TEST_CASE("Vector4: abs, ceil and floor")
{
    const Vector4 v(-1.5, 2.5, -0.5, 1.5);
    CHECK(v.abs() == Vector4(1.5, 2.5, 0.5, 1.5));
    CHECK(v.ceil() == Vector4(-1, 3, 0, 2));
    CHECK(v.floor() == Vector4(-2, 2, -1, 1));
}

TEST_CASE("Vector4: clamp")
{
    CHECK(Vector4(5, -5, 0.5, 9).clamp(0.0, 1.0) == Vector4(1, 0, 0.5, 1));

    CHECK(Vector4(5, 5, 5, 5).clamp(Vector4(0, 0, 0, 0), Vector4(1, 1, 1, 1)) == Vector4(1, 1, 1, 1));
    CHECK(Vector4(-5, -5, -5, -5).clamp(Vector4(0, 0, 0, 0), Vector4(1, 1, 1, 1)) == Vector4(0, 0, 0, 0));
}

TEST_CASE("Vector4: zero and is_same")
{
    Vector4 v(1, 2, 3, 4);
    CHECK_FALSE(v.is_zero());
    v.zero();
    CHECK(v.is_zero());
    CHECK(v == Vector4(0, 0, 0, 0));

    CHECK(Vector4(1, 2, 3, 4).is_same(Vector4(1, 2, 3, 4)));
    CHECK_FALSE(Vector4(1, 2, 3, 4).is_same(Vector4(1, 2, 3, 5)));
}

TEST_CASE("Vector4: approximate comparison")
{
    CHECK(Vector4(1e-9, -1e-12, 0, 0).is_zero_approx());
    CHECK_FALSE(Vector4(0.001, 0, 0, 0).is_zero_approx());

    CHECK(Vector4(1 + 1e-9, 2, 3, 4).is_equal_approx(Vector4(1, 2, 3, 4)));
    CHECK_FALSE(Vector4(1.1, 2, 3, 4).is_equal_approx(Vector4(1, 2, 3, 4)));
}
