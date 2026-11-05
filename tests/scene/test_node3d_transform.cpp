#include "doctest/doctest.h"
#include "3d/node3d.h"
#include "math/basis.h"
#include "math/math_funcs.h"
#include "math/transform_3d.h"

namespace
{
    Transform3D make_trs(const Vector3 &axis, real_t deg, const Vector3 &scale, const Vector3 &origin)
    {
        return Transform3D(Basis(axis, Math::deg_to_rad(deg)).scaled(scale), origin);
    }
}

TEST_CASE("Node3D: set_transform 保留完整 TRS")
{
    Node3D node;
    CHECK(node.get_transform().is_equal_approx(Transform3D()));

    const Transform3D transform = make_trs(
        Vector3(1, 2, 3).normalized(), 42, Vector3(2, 0.5, 1.5), Vector3(1, -2, 3));
    node.set_transform(transform);

    // 局部/全局变换与输入一致
    CHECK(node.get_transform().is_equal_approx(transform));
    CHECK(node.get_global_transform().is_equal_approx(transform));

    // position/scale 反解正确
    CHECK(node.get_position().is_equal_approx(Vector3(1, -2, 3)));
    CHECK(node.get_scale().is_equal_approx(Vector3(2, 0.5, 1.5)));

    // 反解出的 rotation/scale 能重建出原矩阵：set_position 会用内部 TRS 重新计算局部变换
    node.set_position(transform.origin);
    CHECK(node.get_transform().is_equal_approx(transform));
}

TEST_CASE("Node3D: set_transform 支持负缩放")
{
    Node3D node;

    Basis mirrored = Basis(Vector3::UP, Math::deg_to_rad(30)).scaled(Vector3(2, 3, 4));
    mirrored.columns[0] *= -1;
    const Transform3D transform(mirrored, Vector3(0, 1, 0));

    node.set_transform(transform);

    CHECK(node.get_global_transform().is_equal_approx(transform));
    CHECK(node.get_transform().is_equal_approx(transform));
    // 约定镜像归到 X 轴
    CHECK(node.get_scale().is_equal_approx(Vector3(-2, 3, 4)));
}

TEST_CASE("Node3D: 带旋转的父节点下的子节点变换")
{
    Node3D parent;
    Node3D child;
    parent.add_child(&child);

    const Transform3D parent_transform = make_trs(Vector3::UP, 90, Vector3(1, 1, 1), Vector3(5, 0, 0));
    parent.set_transform(parent_transform);

    const Transform3D child_local = make_trs(Vector3::RIGHT, 45, Vector3(2, 2, 2), Vector3(0, 2, 0));
    child.set_transform(child_local);

    CHECK(child.get_transform().is_equal_approx(child_local));
    CHECK(child.get_global_transform().is_equal_approx(parent.get_global_transform() * child_local));

    // 父节点再变换时子节点全局变换跟着更新
    parent.set_position(Vector3(6, 0, 0));
    CHECK(child.get_global_transform().is_equal_approx(parent.get_global_transform() * child_local));

    // 子节点位置/缩放的 getter 与矩阵一致
    CHECK(child.get_position().is_equal_approx(child_local.origin));
    CHECK(child.get_scale().is_equal_approx(Vector3(2, 2, 2)));
}
