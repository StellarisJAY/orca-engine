#include "doctest/doctest.h"
#include "scene/scene.h"
#include "mesh/mesh.h"
#include "os/memory.h"

#include <string>

namespace
{
    Ref<Mesh> make_triangle_mesh()
    {
        Mesh::Surface surface;
        surface.vertices = {Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0)};
        surface.normals = {Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1)};
        surface.uvs = {Vector2(0, 0), Vector2(1, 0), Vector2(0, 1)};
        surface.indices = {0, 1, 2};

        Ref<Mesh> mesh = Ref(memnew(Mesh));
        mesh->add_surface(std::move(surface));
        return mesh;
    }

    /// @brief 构造一个与 loader 产物一致的、children/parent 自洽的 PackedScene。
    PackedScene *build_sample_scene(const Ref<Mesh> &mesh)
    {
        PackedScene *scene = memnew(PackedScene);
        scene->set_scene_name("sample");

        PackedScene::Node root;
        root.name = "root";
        root.kind = PackedScene::NodeKind::MeshInstance;
        root.local_transform.origin = Vector3(1, 2, 3);
        root.mesh = mesh;
        root.children.push_back(1);

        PackedScene::Node child;
        child.name = "child";
        child.parent_idx = 0;
        child.local_transform.origin = Vector3(4, 5, 6);

        scene->add_node(root);
        scene->add_node(child);
        scene->add_root(0);
        return scene;
    }
} // namespace

TEST_CASE("PackedScene: add_node/add_root 建立索引")
{
    const Ref<Mesh> mesh = make_triangle_mesh();
    PackedScene *scene = build_sample_scene(mesh);

    REQUIRE(scene->get_nodes().size() == 2);
    REQUIRE(scene->get_roots().size() == 1);

    // 索引即节点 id，add_node 的返回值与数组下标一致
    CHECK(scene->get_nodes()[0].name == "root");
    CHECK(scene->get_nodes()[1].name == "child");
    CHECK(scene->get_roots()[0] == 0);
    CHECK(scene->get_nodes()[0].children[0] == 1);
    CHECK(scene->get_nodes()[1].parent_idx == 0);
    CHECK(scene->get_scene_name() == "sample");

    // 纯 CPU 资源，没有 GPU RID
    CHECK(scene->get_rid() == 0);
    CHECK(scene->validate());

    memdelete(scene);
}

TEST_CASE("PackedScene: duplicate 深拷贝节点数组并共享 Mesh")
{
    const Ref<Mesh> mesh = make_triangle_mesh();
    PackedScene *original = build_sample_scene(mesh);

    Ref<Resource> duplicated_resource = original->duplicate();
    REQUIRE(duplicated_resource.is_valid());

    Ref<PackedScene> duplicated = duplicated_resource.cast<PackedScene>();
    REQUIRE(duplicated.is_valid());
    CHECK(duplicated.ptr() != original);

    REQUIRE(duplicated->get_nodes().size() == 2);
    REQUIRE(duplicated->get_roots().size() == 1);
    CHECK(duplicated->get_roots()[0] == 0);
    CHECK(duplicated->get_scene_name() == "sample");
    CHECK(duplicated->get_nodes()[0].name == "root");
    CHECK(duplicated->get_nodes()[0].kind == PackedScene::NodeKind::MeshInstance);
    CHECK(duplicated->get_nodes()[1].parent_idx == 0);
    CHECK(duplicated->validate());

    // Mesh 按 Ref 共享，不是深拷贝
    CHECK(duplicated->get_nodes()[0].mesh.ptr() == mesh.ptr());

    // 修改原场景的节点数组不影响副本
    PackedScene::Node extra;
    original->add_node(extra);
    CHECK(original->get_nodes().size() == 3);
    CHECK(duplicated->get_nodes().size() == 2);

    memdelete(original);
}

TEST_CASE("PackedScene: validate 识别越界与父子不一致")
{
    const Ref<Mesh> mesh = make_triangle_mesh();
    PackedScene *scene = build_sample_scene(mesh);
    REQUIRE(scene->validate());

    // 1) children 越界
    {
        PackedScene::Node bad_root = scene->get_nodes()[0];
        bad_root.children = {7};
        PackedScene *invalid = memnew(PackedScene);
        invalid->add_node(bad_root);
        std::string error;
        CHECK_FALSE(invalid->validate(&error));
        CHECK_FALSE(error.empty());
        memdelete(invalid);
    }

    // 2) parent 越界
    {
        PackedScene::Node bad_child = scene->get_nodes()[1];
        bad_child.parent_idx = 9;
        PackedScene *invalid = memnew(PackedScene);
        invalid->add_node(scene->get_nodes()[0]);
        invalid->add_node(bad_child);
        std::string error;
        CHECK_FALSE(invalid->validate(&error));
        CHECK_FALSE(error.empty());
        memdelete(invalid);
    }

    // 3) 父节点 children 里列的节点，其 parent 指向别处
    {
        PackedScene::Node bad_root = scene->get_nodes()[0];
        bad_root.children = {1};
        PackedScene *invalid = memnew(PackedScene);
        invalid->add_node(bad_root);
        PackedScene::Node orphan;
        orphan.parent_idx = -1;
        invalid->add_node(orphan);
        std::string error;
        CHECK_FALSE(invalid->validate(&error));
        CHECK_FALSE(error.empty());
        memdelete(invalid);
    }

    // 4) roots 里包含非根节点
    {
        PackedScene *invalid = build_sample_scene(mesh);
        invalid->add_root(1);
        std::string error;
        CHECK_FALSE(invalid->validate(&error));
        CHECK_FALSE(error.empty());
        memdelete(invalid);
    }

    // 5) roots 越界
    {
        PackedScene *invalid = build_sample_scene(mesh);
        invalid->add_root(42);
        std::string error;
        CHECK_FALSE(invalid->validate(&error));
        CHECK_FALSE(error.empty());
        memdelete(invalid);
    }

    memdelete(scene);
}
