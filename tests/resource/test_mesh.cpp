#include "doctest/doctest.h"
#include "mesh/mesh.h"
#include "object/rid.h"

TEST_CASE("Mesh: rid dirty 状态")
{
    Mesh mesh;

    // 新建的 Mesh 数据还没上传
    REQUIRE(mesh.is_rid_dirty());
    CHECK(mesh.get_rid() == 0);

    // 创建 RID 不等于数据已上传，必须显式 mark_rid_clean
    mesh.set_rid(static_cast<RID>(42));
    CHECK(mesh.get_rid() == static_cast<RID>(42));
    CHECK(mesh.is_rid_dirty());

    mesh.mark_rid_clean();
    CHECK_FALSE(mesh.is_rid_dirty());

    // 顶点数据后续变更时重新标脏
    mesh.mark_rid_dirty();
    CHECK(mesh.is_rid_dirty());

    // 重复调用 mark_rid_clean 是幂等的
    mesh.mark_rid_clean();
    mesh.mark_rid_clean();
    CHECK_FALSE(mesh.is_rid_dirty());
}

TEST_CASE("Mesh: surface 计数")
{
    Mesh mesh;
    CHECK(mesh.get_surface_count() == 0);
    CHECK(mesh.is_empty());

    Mesh::Surface surface;
    surface.vertices = {Vector3::ZERO, Vector3::RIGHT, Vector3::UP};
    surface.indices = {0, 1, 2};
    mesh.add_surface(surface);

    CHECK(mesh.get_surface_count() == 1);
    CHECK_FALSE(mesh.is_empty());
    CHECK(mesh.get_vertex_count() == 3);
    CHECK(mesh.get_triangle_count() == 1);
}
