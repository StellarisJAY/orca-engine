#include "scene_tree.h"
#include "node.h"
#include "3d/node3d.h"
#include "doctest/doctest.h"

TEST_CASE("Node3D global_transform")
{
    Node3D root = Node3D();
    Node3D node1 = Node3D();
    Node3D node2 = Node3D();
    root.add_child(&node1);
    node1.add_child(&node2);

    root.set_position({1, 1, 1});
    node1.set_position({1, 1, 1});
    node2.set_position({1, 1, 1});
    
    const auto pr = root.get_global_transform().origin;
    const auto p1 = node1.get_global_transform().origin;
    const auto p2 = node2.get_global_transform().origin;

    CHECK(pr == Vector3{1,1,1});
    CHECK(p1 == Vector3{2,2,2});
    CHECK(p2 == Vector3{3,3,3});
}