#include "doctest/doctest.h"
#include "math/math_funcs.h"
#include "math/transform_3d.h"

using doctest::Approx;

namespace
{
    const real_t HALF_PI = Math::PI / 2.0;

    Transform3D identity_transform()
    {
        return Transform3D(Basis(), Vector3::ZERO);
    }
}

TEST_CASE("Transform3D: apply")
{
    const Transform3D id = identity_transform();
    CHECK(id.apply(Vector3(1, 2, 3)) == Vector3(1, 2, 3));

    // 平移变换
    const Transform3D tr(Vector3::ONES, Vector3(5, -2, 1));
    CHECK(tr.apply(Vector3(1, 1, 1)).is_equal_approx(Vector3(6, -1, 2)));

    // 缩放变换
    const Transform3D sc(Vector3(2, 3, 4), Vector3::ZERO);
    CHECK(sc.apply(Vector3(1, 1, 1)).is_equal_approx(Vector3(2, 3, 4)));

    // 旋转变换，绕 Y +90°：+X -> -Z
    const Transform3D rot(Vector3::UP, HALF_PI, Vector3::ZERO);
    CHECK(rot.apply(Vector3::RIGHT).is_equal_approx(Vector3::FORWARD));

    // 一般仿射：B * v + t
    const Basis b(Vector3::UP, HALF_PI);
    const Transform3D m(b, Vector3(1, 2, 3));
    CHECK(m.apply(Vector3(0, 0, 1)).is_equal_approx(b.apply(Vector3(0, 0, 1)) + Vector3(1, 2, 3)));
    CHECK(m.apply(Vector3::ZERO).is_equal_approx(Vector3(1, 2, 3)));
}

TEST_CASE("Transform3D: scale")
{
    Transform3D t = identity_transform();
    t.scale(2, 3, 4);
    CHECK(t.is_equal_approx(Transform3D(Basis(2, 0, 0, 0, 3, 0, 0, 0, 4), Vector3::ZERO)));

    // Vector3 重载
    Transform3D v = identity_transform();
    v.scale(Vector3(2, 3, 4));
    CHECK(v == t);

    // scale(1,1,1) 恒等
    Transform3D one(Basis(2, 0, 0, 0, 3, 0, 0, 0, 4), Vector3(1, 2, 3));
    const Transform3D kept = one;
    one.scale(1, 1, 1);
    CHECK(one.is_equal_approx(kept));

    // 同时缩放线性部分与原点
    Transform3D m = identity_transform();
    m.origin = Vector3(1, 2, 3);
    m.scale(2, 3, 4);
    CHECK(m.origin.is_equal_approx(Vector3(2, 6, 12)));

    // scaled() 不修改原对象
    const Transform3D original = identity_transform();
    const Transform3D scaled = original.scaled(2, 3, 4);
    CHECK(scaled.is_equal_approx(Transform3D(Basis(2, 0, 0, 0, 3, 0, 0, 0, 4), Vector3::ZERO)));
    CHECK(original.is_equal_approx(identity_transform()));

    // Vector3 版 scaled()
    CHECK(original.scaled(Vector3(2, 3, 4)).is_equal_approx(scaled));
}

TEST_CASE("Transform3D: rotate")
{
    Transform3D t = identity_transform();
    t.rotate(Vector3::UP, HALF_PI);
    CHECK(t.basis.is_equal_approx(Basis(Vector3::UP, HALF_PI)));
    CHECK(t.origin.is_equal_approx(Vector3::ZERO));

    // rotate(axis, 0) 恒等
    Transform3D z(Basis(Vector3::UP, HALF_PI), Vector3(1, 2, 3));
    const Transform3D zc = z;
    z.rotate(Vector3::UP, 0);
    CHECK(z.is_equal_approx(zc));

    // 旋转同样作用于原点
    Transform3D m = identity_transform();
    m.origin = Vector3(2, 0, 0);
    m.rotate(Vector3::UP, HALF_PI);
    CHECK(m.origin.is_equal_approx(Vector3(0, 0, -2)));

    // rotated() 不修改原对象
    const Transform3D original = identity_transform();
    const Transform3D rotated = original.rotated(Vector3::UP, HALF_PI);
    CHECK(rotated.basis.is_equal_approx(Basis(Vector3::UP, HALF_PI)));
    CHECK(original.is_equal_approx(identity_transform()));

    // 连续旋转相加
    Transform3D r = identity_transform();
    r.rotate(Vector3::UP, HALF_PI / 2.0);
    r.rotate(Vector3::UP, HALF_PI / 2.0);
    CHECK(r.basis.is_equal_approx(Basis(Vector3::UP, HALF_PI)));
}

TEST_CASE("Transform3D: translate")
{
    Transform3D t = identity_transform();
    t.translate(3, 4, 5);
    CHECK(t.is_equal_approx(Transform3D(Basis(), Vector3(3, 4, 5))));

    // Vector3 重载
    Transform3D v = identity_transform();
    v.translate(Vector3(3, 4, 5));
    CHECK(v.is_equal_approx(t));

    // translate(0,0,0) 恒等
    Transform3D z(Basis(Vector3::UP, HALF_PI), Vector3(1, 2, 3));
    const Transform3D zc = z;
    z.translate(0, 0, 0);
    CHECK(z.is_equal_approx(zc));

    // 只改变原点，线性部分不变
    Transform3D m(Basis(Vector3::UP, HALF_PI), Vector3(1, 2, 3));
    m.translate(1, -2, 4);
    CHECK(m.basis.is_equal_approx(Basis(Vector3::UP, HALF_PI)));
    CHECK(m.origin.is_equal_approx(Vector3(2, 0, 7)));

    // 平移可累加
    Transform3D acc = identity_transform();
    acc.translate(1, 1, 1);
    acc.translate(2, 3, 4);
    CHECK(acc.origin.is_equal_approx(Vector3(3, 4, 5)));

    // translated() 不修改原对象
    const Transform3D original = identity_transform();
    const Transform3D moved = original.translated(3, 4, 5);
    CHECK(moved.is_equal_approx(Transform3D(Basis(), Vector3(3, 4, 5))));
    CHECK(original.is_equal_approx(identity_transform()));
    CHECK(original.translated(Vector3(3, 4, 5)).is_equal_approx(moved));
}

TEST_CASE("Transform3D: multiply")
{
    const Transform3D I = identity_transform();
    const Transform3D T1(Basis(), Vector3(3, 4, 5));
    const Transform3D T2(Basis(), Vector3(-1, 2, 0));
    const Basis b(Vector3::UP, HALF_PI);
    const Transform3D M(b, Vector3(6, 7, 8));

    // 单位元
    CHECK((I * M) == M);
    CHECK((M * I) == M);

    // 平移复合：T1 * T2 平移量为 (3-1, 4+2, 5+0)
    CHECK((T1 * T2).origin.is_equal_approx(Vector3(2, 6, 5)));

    // operator* 等价于对向量连续应用：先 other 后 this
    const Vector3 v(1, 2, 3);
    CHECK((M * T1).apply(v).is_equal_approx(M.apply(T1.apply(v))));
    CHECK((T1 * M).apply(v).is_equal_approx(T1.apply(M.apply(v))));

    // 结合律
    CHECK(((M * T1) * T2).apply(v).is_equal_approx((M * (T1 * T2)).apply(v)));

    // 不可交换
    CHECK((T1 * M) != (M * T1));

    // operator*= 与 operator* 一致
    Transform3D a = M;
    a *= T1;
    CHECK(a.is_equal_approx(M * T1));

    // 与标量相乘：同时缩放线性部分与原点
    CHECK((M * 2.0).basis.is_equal_approx(M.basis.scaled(Vector3(2, 2, 2))));
    CHECK((M * 2.0).origin.is_equal_approx(Vector3(12, 14, 16)));

    Transform3D c = M;
    c *= 2.0;
    CHECK(c.is_equal_approx(M * 2.0));
}

TEST_CASE("Transform3D: affine_inverse")
{
    const Transform3D I = identity_transform();
    const Transform3D T(Basis(), Vector3(3, 4, 5));
    const Transform3D S(Basis(2, 0, 0, 0, 3, 0, 0, 0, 4), Vector3::ZERO);
    const Basis b(Vector3::UP, HALF_PI);
    const Transform3D M(b, Vector3(6, 7, 8));
    const Transform3D RS(Basis(Vector3::BACKWARD, Math::deg_to_rad(20)).scaled(Vector3(2, 3, 4)),
                         Vector3(-1, 2, 3));

    // 单位变换的逆是自身
    CHECK(I.affine_inverse().is_equal_approx(I));

    // A * A^-1 == I 且 A^-1 * A == I
    CHECK((T * T.affine_inverse()).is_equal_approx(I));
    CHECK((T.affine_inverse() * T).is_equal_approx(I));
    CHECK((S * S.affine_inverse()).is_equal_approx(I));
    CHECK((M * M.affine_inverse()).is_equal_approx(I));
    CHECK((M.affine_inverse() * M).is_equal_approx(I));
    CHECK((RS * RS.affine_inverse()).is_equal_approx(I));
    CHECK((RS.affine_inverse() * RS).is_equal_approx(I));

    // 具体逆矩阵
    CHECK(T.affine_inverse().is_equal_approx(Transform3D(Basis(), Vector3(-3, -4, -5))));
    CHECK(S.affine_inverse().is_equal_approx(
        Transform3D(Basis(0.5, 0, 0, 0, 1.0 / 3.0, 0, 0, 0, 0.25), Vector3::ZERO)));
    CHECK(M.affine_inverse().basis.is_equal_approx(b.inverse()));
    CHECK(M.affine_inverse().origin.is_equal_approx(b.inverse().apply(Vector3(6, 7, 8)) * -1.0));

    // apply 往返：逆变换抵消正变换
    const Vector3 v(1, 2, 3);
    CHECK(M.affine_inverse().apply(M.apply(v)).is_equal_approx(v));
    CHECK(M.apply(M.affine_inverse().apply(v)).is_equal_approx(v));
    CHECK(RS.affine_inverse().apply(RS.apply(v)).is_equal_approx(v));

    // 二次求逆回到自身
    CHECK(M.affine_inverse().affine_inverse().is_equal_approx(M));
    CHECK(RS.affine_inverse().affine_inverse().is_equal_approx(RS));

    // affine_inverse() 不修改原对象
    const Transform3D original(b, Vector3(6, 7, 8));
    const Transform3D inverted = original.affine_inverse();
    CHECK(original.is_equal_approx(Transform3D(b, Vector3(6, 7, 8))));
    CHECK(inverted.is_equal_approx(original.affine_inverse()));

    // affine_invert() 就地求逆
    Transform3D inplace(b, Vector3(6, 7, 8));
    inplace.affine_invert();
    CHECK(inplace.is_equal_approx(inverted));
}
