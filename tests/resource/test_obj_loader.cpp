#include "doctest/doctest.h"
#include "loaders/obj_loader.h"
#include "mesh/mesh.h"
#include "material/material.h"
#include "texture/image_texture.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <utility>

namespace
{
    const char *TEST_OBJ_PATH = "orca_obj_loader_test.obj";

    void write_obj(const std::string &content)
    {
        std::ofstream file(TEST_OBJ_PATH, std::ios::binary | std::ios::trunc);
        file << content;
    }

    Ref<Resource> load_resource(const std::string &content)
    {
        write_obj(content);
        ObjMeshLoader loader;
        Ref<Resource> resource = loader.load(TEST_OBJ_PATH);
        std::remove(TEST_OBJ_PATH);
        return resource;
    }

    /// @brief 让缓存测试即使 REQUIRE 失败也能恢复全局单例。
    struct LoaderCacheGuard
    {
        LoaderCacheGuard()
        {
            ResourceCache::create_singleton();
            ResourceLoader::create_singleton();
        }

        ~LoaderCacheGuard()
        {
            ResourceLoader::destroy_singleton();
            ResourceCache::destroy_singleton();
            std::remove(TEST_OBJ_PATH);
        }
    };
} // namespace

TEST_CASE("ObjMeshLoader: parses a quad with normals and uvs")
{
    Ref<Resource> resource = load_resource(
        "# simple quad\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 1 1\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "f 1/1/1 2/2/1 3/3/1 4/4/1\n");
    REQUIRE(resource.is_valid());

    Ref<Mesh> mesh = resource.cast<Mesh>();
    REQUIRE(mesh.is_valid());
    REQUIRE(mesh->get_surface_count() == 1);

    const Mesh::Surface &surface = mesh->get_surface(0);

    // 四边形被扇形三角化为 2 个三角形，共享 4 个唯一顶点。
    CHECK(surface.vertices.size() == 4);
    CHECK(surface.indices.size() == 6);
    CHECK(mesh->get_triangle_count() == 2);

    CHECK(surface.vertices[0] == Vector3(0, 0, 0));
    CHECK(surface.vertices[2] == Vector3(1, 1, 0));
    CHECK(surface.normals[0] == Vector3(0, 0, 1));
    CHECK(surface.uvs[2] == Vector2(1, 1));
    CHECK(surface.indices[0] == 0);
    CHECK(surface.indices[1] == 1);
    CHECK(surface.indices[2] == 2);

    // 没有材质也必须使用 UV 解算切线：sphere.obj 这类模型是在加载完成后
    // 才由 MeshInstance3D 注入材质，加载期 material 仍为空。
    // 该 quad 的 UV 与 +X/+Y 对齐，UV 切线应为 (1,0,0)，副切线为 (0,1,0)；
    // 若误走基于法线的兜底基，切线会变成 (-1,0,0)。
    CHECK(surface.tangents.size() == surface.vertices.size());
    CHECK(surface.bitangents.size() == surface.vertices.size());
    CHECK(surface.tangents[0].x == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(surface.tangents[0].y == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(surface.bitangents[0].y == doctest::Approx(1.0f).epsilon(1e-3));
}

TEST_CASE("ObjMeshLoader: resolves negative indices")
{
    Ref<Resource> resource = load_resource(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f -4 -3 -2 -1\n");
    REQUIRE(resource.is_valid());

    Ref<Mesh> mesh = resource.cast<Mesh>();
    REQUIRE(mesh.is_valid());
    const Mesh::Surface &surface = mesh->get_surface(0);
    CHECK(surface.vertices.size() == 4);
    CHECK(surface.indices.size() == 6);
}

TEST_CASE("ObjMeshLoader: computes normals when missing")
{
    Ref<Resource> resource = load_resource(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    REQUIRE(resource.is_valid());

    Ref<Mesh> mesh = resource.cast<Mesh>();
    REQUIRE(mesh.is_valid());
    const Mesh::Surface &surface = mesh->get_surface(0);
    REQUIRE(surface.indices.size() == 3);
    CHECK(surface.normals[0] == Vector3(0, 0, 1));
    CHECK(surface.normals[1] == Vector3(0, 0, 1));
    CHECK(surface.normals[2] == Vector3(0, 0, 1));
}

TEST_CASE("ObjMeshLoader: fills only the normals that are missing")
{
    // 第一个三角形带自定义法线，第二个三角形不引用法线；
    // 补算法线时不能覆盖已有的 (1,0,0)。
    Ref<Resource> resource = load_resource(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "v 1 1 0\n"
        "vn 1 0 0\n"
        "f 1//1 2//1 3//1\n"
        "f 2 4 3\n");
    REQUIRE(resource.is_valid());

    Ref<Mesh> mesh = resource.cast<Mesh>();
    REQUIRE(mesh.is_valid());
    const Mesh::Surface &surface = mesh->get_surface(0);
    REQUIRE(surface.vertices.size() == 6);

    CHECK(surface.normals[0] == Vector3(1, 0, 0));
    CHECK(surface.normals[1] == Vector3(1, 0, 0));
    CHECK(surface.normals[2] == Vector3(1, 0, 0));
    CHECK(surface.normals[3] == Vector3(0, 0, 1));
    CHECK(surface.normals[4] == Vector3(0, 0, 1));
    CHECK(surface.normals[5] == Vector3(0, 0, 1));
}

TEST_CASE("ObjMeshLoader: skips faces with fewer than three vertices")
{
    Ref<Resource> resource = load_resource(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2\n"
        "f 1 2 3\n");
    REQUIRE(resource.is_valid());

    Ref<Mesh> mesh = resource.cast<Mesh>();
    REQUIRE(mesh.is_valid());
    // 第一个 f 行只有两个角点，不能产生空 surface。
    CHECK(mesh->get_surface_count() == 1);
    const Mesh::Surface &surface = mesh->get_surface(0);
    CHECK(surface.vertices.size() == 3);
    CHECK(surface.indices.size() == 3);
}

TEST_CASE("Mesh: uses UV tangents when the material samples a normal map")
{
    Mesh::Surface surface;
    surface.vertices = {
        Vector3(0, 0, 0),
        Vector3(1, 0, 0),
        Vector3(0, 1, 0),
        Vector3(1, 1, 0),
    };
    surface.normals.assign(4, Vector3(0, 0, 1));
    surface.uvs = {
        Vector2(0, 0),
        Vector2(1, 0),
        Vector2(0, 1),
        Vector2(1, 1),
    };
    surface.indices = {0, 1, 2, 2, 1, 3};

    surface.material = Ref(memnew(StandardMaterial));
    surface.material->set_texture_normal(Ref(memnew(ImageTexture)));

    Mesh mesh;
    mesh.add_surface(std::move(surface));
    mesh.compute_tangents();

    const Mesh::Surface &result = mesh.get_surface(0);
    REQUIRE(result.tangents.size() == 4);
    CHECK(result.tangents[0].dot(result.normals[0]) == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(result.tangents[0].x == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(result.tangents[0].length() == doctest::Approx(1.0f).epsilon(1e-4));
}

TEST_CASE("ResourceLoader: caches the same mesh instance by url")
{
    LoaderCacheGuard guard;

    write_obj(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");

    {
        Ref<Resource> first = ResourceLoader::get_singleton()->load(TEST_OBJ_PATH);
        REQUIRE(first.is_valid());
        CHECK(first->get_url() == TEST_OBJ_PATH);

        Ref<Resource> second = ResourceLoader::get_singleton()->load(TEST_OBJ_PATH);
        CHECK(second.ptr() == first.ptr());
        CHECK(ResourceCache::get_singleton()->get_resource(TEST_OBJ_PATH).ptr() == first.ptr());
    }

    // 资源最后一个 Ref 释放后，弱引用缓存应自动摘除。
    CHECK(ResourceCache::get_singleton()->get_resource(TEST_OBJ_PATH).is_null());
}
