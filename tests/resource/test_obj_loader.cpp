#include "doctest/doctest.h"
#include "loaders/obj_loader.h"
#include "mesh/mesh.h"

#include <cstdio>
#include <fstream>
#include <string>

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
}

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
    
    Mesh *mesh = static_cast<Mesh *>(resource.ptr());
    
    const Mesh::Surface &surface = mesh->get_surface(0);
    
    // 四边形被扇形三角化为 2 个三角形，共享 4 个唯一顶点。
    CHECK(surface.get_vertex_count() == 4);
    CHECK(surface.get_index_count() == 6);
    CHECK(surface.get_triangle_count() == 2);

    CHECK(surface.vertices[0] == Vector3(0, 0, 0));
    CHECK(surface.vertices[2] == Vector3(1, 1, 0));
    CHECK(surface.normals[0] == Vector3(0, 0, 1));
    CHECK(surface.uvs[2] == Vector2(1, 1));
    CHECK(surface.indices[0] == 0);
    CHECK(surface.indices[1] == 1);
    CHECK(surface.indices[2] == 2);
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

    Mesh *mesh = static_cast<Mesh *>(resource.ptr());
    const Mesh::Surface &surface = mesh->get_surface(0);
    CHECK(surface.get_vertex_count() == 4);
    CHECK(surface.get_index_count() == 6);
}

TEST_CASE("ObjMeshLoader: computes normals when missing")
{
    Ref<Resource> resource = load_resource(
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    REQUIRE(resource.is_valid());

    Mesh *mesh = static_cast<Mesh *>(resource.ptr());
    const Mesh::Surface &surface = mesh->get_surface(0);
    REQUIRE(surface.get_index_count() == 3);
    CHECK(surface.normals[0] == Vector3(0, 0, 1));
    CHECK(surface.normals[1] == Vector3(0, 0, 1));
    CHECK(surface.normals[2] == Vector3(0, 0, 1));
}

TEST_CASE("ObjMeshLoader: ignores malformed lines and comments")
{
    Ref<Resource> resource = load_resource(
        "# comment\n"
        "\n"
        "g group\n"
        "usemtl none\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f 1 2 3\n");
    REQUIRE(resource.is_valid());

    Mesh *mesh = static_cast<Mesh *>(resource.ptr());
    const Mesh::Surface &surface = mesh->get_surface(0);
    CHECK(surface.get_vertex_count() == 3);
    CHECK(surface.get_index_count() == 3);
}

TEST_CASE("Mesh: add_surface marks rid dirty and set_rid clears it")
{
    Mesh mesh;
    CHECK(mesh.is_rid_dirty());

    Mesh::Surface surface;
    surface.vertices.emplace_back(0, 0, 0);
    surface.indices.push_back(0);
    mesh.add_surface(surface);
    CHECK(mesh.is_rid_dirty());

    mesh.set_rid(42);
    CHECK(mesh.get_rid() == 42);
    CHECK_FALSE(mesh.is_rid_dirty());

    mesh.mark_rid_dirty();
    CHECK(mesh.is_rid_dirty());
}
