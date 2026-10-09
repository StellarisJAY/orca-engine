#include "doctest/doctest.h"
#include "math/math_funcs.h"
#include "math/transform_2d.h"

#include <cmath>

using doctest::Approx;

TEST_CASE("Transform2D: apply")
{
    const real_t cos45 = Math::cos(Math::PI/4);
    const real_t sin45 = Math::sin(Math::PI/4);
    const real_t cos90 = Math::cos(Math::PI/2);
    const real_t sin90 = Math::sin(Math::PI/2);
    const real_t cosn45 = Math::cos(-Math::PI/4);
    const real_t sinn45 = Math::sin(-Math::PI/4);
    const real_t cosn90 = Math::cos(-Math::PI/2);
    const real_t sinn90 = Math::sin(-Math::PI/2);

    Vector2 one(1, 1);
    Transform2D scale2(2, 0, 0, 2, 0, 0);
    Transform2D scaleHalf(0.5, 0, 0, 0.5, 0, 0);
    Transform2D scale1(1, 0, 0, 1, 0, 0);
    
    Transform2D translateLeft(1, 0, 0, 1, -1, 0);
    Transform2D translateRight(1, 0, 0, 1, 1, 0);
    Transform2D translateUp(1, 0, 0, 1, 0, 1);
    Transform2D translateDown(1, 0, 0, 1, 0, -1);
    Transform2D translateOrigin(1, 0, 0, 1, -1, -1);

    Transform2D rotate45(cos45, sin45, -sin45, cos45, 0, 0);
    Transform2D rotate90(cos90, sin90, -sin90, cos90, 0, 0);
    Transform2D rotaten45(cosn45, sinn45, -sinn45, cosn45, 0, 0);
    Transform2D rotaten90(cosn90, sinn90, -sinn90, cosn90, 0, 0);

    CHECK(scale2.apply(one).is_equal_approx({2, 2}));
    CHECK(scaleHalf.apply(one).is_equal_approx({0.5, 0.5}));
    CHECK(scale1.apply(one).is_equal_approx({1, 1}));

    CHECK(translateLeft.apply(one).is_equal_approx({0, 1}));
    CHECK(translateRight.apply(one).is_equal_approx({2, 1}));
    CHECK(translateDown.apply(one).is_equal_approx({1, 0}));
    CHECK(translateUp.apply(one).is_equal_approx({1, 2}));
    CHECK(translateOrigin.apply(one).is_equal_approx({0, 0}));

    CHECK(rotate45.apply(one).is_equal_approx({0, Math::SQRT2}));
    CHECK(rotate90.apply(one).is_equal_approx({-1, 1}));
    CHECK(rotaten45.apply(one).is_equal_approx({Math::SQRT2, 0}));
    CHECK(rotaten90.apply(one).is_equal_approx({1, -1}));
}

TEST_CASE("Transform2D: multiply")
{
    const Transform2D I(1, 0, 0, 1, 0, 0);
    const Transform2D T1(1, 0, 0, 1, 3, 4);   // 平移 (3,4)
    const Transform2D T2(1, 0, 0, 1, -1, 2);  // 平移 (-1,2)
    const Transform2D R90(0, 1, -1, 0, 0, 0); // 旋转 90°
    const Transform2D S2(2, 0, 0, 2, 0, 0);   // 缩放 2
    const Transform2D M(2, 3, 4, 5, 6, 7);    // 一般仿射

    const real_t cos45 = Math::cos(Math::PI / 4);
    const real_t sin45 = Math::sin(Math::PI / 4);
    const Transform2D R45(cos45, sin45, -sin45, cos45, 0, 0);

    // 单位元
    CHECK((I * M) == M);
    CHECK((M * I) == M);

    // 平移的复合：T1 * T2 等价于先 T2 后 T1，即平移 (3-1, 4+2)
    CHECK((T1 * T2) == Transform2D(1, 0, 0, 1, 2, 6));

    // 纯线性变换（平移为 0）的复合
    CHECK((R90 * R90) == Transform2D(-1, 0, 0, -1, 0, 0));
    CHECK((S2 * S2).apply(Vector2(1, 1)).is_equal_approx(Vector2(4, 4)));

    // 旋转 45° 两次等于旋转 90°
    CHECK((R45 * R45).apply(Vector2(1, 1)).is_equal_approx(R90.apply(Vector2(1, 1))));

    // operator* 等价于对向量连续应用：先 other 后 this
    CHECK((M * T1).apply(Vector2(1, 2)).is_equal_approx(M.apply(T1.apply(Vector2(1, 2)))));
    CHECK((T1 * S2).apply(Vector2(1, 2)).is_equal_approx(T1.apply(S2.apply(Vector2(1, 2)))));

    // 结合律
    CHECK(((M * T1) * T2).apply(Vector2(1, 2))
              .is_equal_approx((M * (T1 * T2)).apply(Vector2(1, 2))));

    // 不可交换
    CHECK((T1 * R90) != (R90 * T1));

    // operator*= 与 operator* 一致
    Transform2D a = M;
    a *= T1;
    CHECK(a == (M * T1));

    Transform2D b = M;
    b *= I;
    CHECK(b == M);

    // 与标量相乘
    CHECK((M * 2.0) == Transform2D(4, 6, 8, 10, 12, 14));
    CHECK((M * 0.5) == Transform2D(1, 1.5, 2, 2.5, 3, 3.5));
    CHECK((M * 1.0) == M);

    Transform2D c = M;
    c *= 2.0;
    CHECK(c == Transform2D(4, 6, 8, 10, 12, 14));
}

TEST_CASE("Transform2D: scale")
{
    // 单位变换上缩放
    Transform2D t(1, 0, 0, 1, 0, 0);
    t.scale(2, 3);
    CHECK(t == Transform2D(2, 0, 0, 3, 0, 0));
    CHECK(t.apply(Vector2(1, 1)).is_equal_approx(Vector2(2, 3)));

    // scale(1,1) 为恒等
    Transform2D one(2, 0, 0, 3, 4, 5);
    const Transform2D kept = one;
    one.scale(1, 1);
    CHECK(one == kept);

    // 同时缩放线性部分与平移（在父坐标系中缩放）
    Transform2D m(1, 0, 0, 1, 4, 6);
    m.scale(2, 3);
    CHECK(m == Transform2D(2, 0, 0, 3, 8, 18));

    // scaled() 不修改原对象
    const Transform2D original(1, 0, 0, 1, 4, 6);
    const Transform2D scaled = original.scaled(2, 3);
    CHECK(scaled == Transform2D(2, 0, 0, 3, 8, 18));
    CHECK(original == Transform2D(1, 0, 0, 1, 4, 6));

    // 缩放旋转后的基
    const real_t c = Math::cos(Math::PI / 2);
    const real_t s = Math::sin(Math::PI / 2);
    Transform2D r(c, s, -s, c, 0, 0);
    r.scale(2, 2);
    CHECK(r.is_equal_approx(Transform2D(2 * c, 2 * s, -2 * s, 2 * c, 0, 0)));
}

TEST_CASE("Transform2D: rotate")
{
    // 单位变换旋转 90°
    Transform2D t(1, 0, 0, 1, 0, 0);
    t.rotate(Math::PI / 2);
    CHECK(t.apply(Vector2(1, 0)).is_equal_approx(Vector2(0, 1)));
    CHECK(t.apply(Vector2(0, 1)).is_equal_approx(Vector2(-1, 0)));

    // rotate(0) 恒等
    Transform2D z(1, 0, 0, 1, 2, 3);
    const Transform2D zc = z;
    z.rotate(0);
    CHECK(z.is_equal_approx(zc));

    // 旋转同样作用于平移分量（在父坐标系中旋转）
    Transform2D m(1, 0, 0, 1, 2, 0);
    m.rotate(Math::PI / 2);
    CHECK(m.apply(Vector2(0, 0)).is_equal_approx(Vector2(0, 2)));

    // translated 后再 rotate：先平移后旋转
    Transform2D order(1, 0, 0, 1, 0, 0);
    order.translate(1, 0);
    order.rotate(Math::PI / 2);
    CHECK(order.apply(Vector2(0, 0)).is_equal_approx(Vector2(0, 1)));

    // rotated() 不修改原对象
    const Transform2D original(1, 0, 0, 1, 1, 0);
    const Transform2D rotated = original.rotated(Math::PI / 2);
    CHECK(rotated.apply(Vector2(1, 1)).is_equal_approx(Vector2(-1, 2)));
    CHECK(original.apply(Vector2(1, 1)).is_equal_approx(Vector2(2, 1)));

    // 连续旋转相加
    Transform2D r(1, 0, 0, 1, 0, 0);
    r.rotate(Math::PI / 4);
    r.rotate(Math::PI / 4);
    CHECK(r.apply(Vector2(1, 0)).is_equal_approx(Vector2(0, 1)));
}

TEST_CASE("Transform2D: translate")
{
    // 单位变换平移
    Transform2D t(1, 0, 0, 1, 0, 0);
    t.translate(3, 4);
    CHECK(t == Transform2D(1, 0, 0, 1, 3, 4));

    // Vector2 重载
    Transform2D v(1, 0, 0, 1, 0, 0);
    v.translate(Vector2(3, 4));
    CHECK(v == t);

    // translate(0,0) 恒等
    Transform2D z(2, 0, 0, 2, 1, 1);
    z.translate(0, 0);
    CHECK(z == Transform2D(2, 0, 0, 2, 1, 1));

    // 只改变原点，线性部分不变
    Transform2D m(2, 0, 0, 3, 4, 5);
    m.translate(1, -2);
    CHECK(m == Transform2D(2, 0, 0, 3, 5, 3));

    // 平移可累加
    Transform2D acc(1, 0, 0, 1, 0, 0);
    acc.translate(1, 1);
    acc.translate(2, 3);
    CHECK(acc == Transform2D(1, 0, 0, 1, 3, 4));

    // translated() 不修改原对象
    const Transform2D original(1, 0, 0, 1, 0, 0);
    const Transform2D moved = original.translated(3, 4);
    CHECK(moved == Transform2D(1, 0, 0, 1, 3, 4));
    CHECK(original == Transform2D(1, 0, 0, 1, 0, 0));
    CHECK(original.translated(Vector2(3, 4)) == moved);
}

TEST_CASE("Transform2D: affine_inverse")
{
    const Transform2D I(1, 0, 0, 1, 0, 0);
    const Transform2D R90(0, 1, -1, 0, 0, 0); // 旋转 90°
    const Transform2D S(2, 0, 0, 3, 0, 0);    // 缩放
    const Transform2D Tr(1, 0, 0, 1, 5, -2);  // 平移
    const Transform2D M(2, 3, 4, 5, 6, 7);    // 一般仿射（det = -2）

    // 单位变换的逆是自身
    CHECK(I.affine_inverse().is_equal_approx(I));

    // 逆与自身相乘为单位阵
    CHECK((R90 * R90.affine_inverse()).is_equal_approx(I));
    CHECK((R90.affine_inverse() * R90).is_equal_approx(I));
    CHECK((S * S.affine_inverse()).is_equal_approx(I));
    CHECK((Tr * Tr.affine_inverse()).is_equal_approx(I));
    CHECK((M * M.affine_inverse()).is_equal_approx(I));
    CHECK((M.affine_inverse() * M).is_equal_approx(I));

    // 具体逆矩阵
    CHECK(R90.affine_inverse().is_equal_approx(Transform2D(0, -1, 1, 0, 0, 0)));
    CHECK(S.affine_inverse().is_equal_approx(Transform2D(0.5, 0, 0, 1.0 / 3.0, 0, 0)));
    CHECK(Tr.affine_inverse().is_equal_approx(Transform2D(1, 0, 0, 1, -5, 2)));
    CHECK(M.affine_inverse().is_equal_approx(Transform2D(-2.5, 1.5, 2, -1, 1, -2)));

    // apply 往返：逆变换抵消正变换
    const Vector2 v(2, 3);
    CHECK(M.affine_inverse().apply(M.apply(v)).is_equal_approx(v));
    CHECK(M.apply(M.affine_inverse().apply(v)).is_equal_approx(v));

    // 二次求逆回到自身
    CHECK(M.affine_inverse().affine_inverse().is_equal_approx(M));
    CHECK(R90.affine_inverse().affine_inverse().is_equal_approx(R90));

    // affine_inverse() 不修改原对象
    const Transform2D original(2, 3, 4, 5, 6, 7);
    const Transform2D inverted = original.affine_inverse();
    CHECK(original.is_equal_approx(Transform2D(2, 3, 4, 5, 6, 7)));
    CHECK(inverted.is_equal_approx(original.affine_inverse()));

    // affine_invert() 就地求逆
    Transform2D inplace(2, 3, 4, 5, 6, 7);
    inplace.affine_invert();
    CHECK(inplace.is_equal_approx(inverted));
}