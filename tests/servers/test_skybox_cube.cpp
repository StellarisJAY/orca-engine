#include "doctest/doctest.h"
#include "skybox_cube.h"

#include <cmath>
#include <set>
#include <tuple>

TEST_CASE("SkyboxCube: geometry only contains vertices and triangle indices")
{
    std::vector<Vector3> vertices;
    std::vector<uint32_t> indices;
    SkyboxCube::create_geometry(vertices, indices);

    // 8 个角 + 12 个三角形（6 面 × 2），没有法线/UV/切线数据。
    REQUIRE(vertices.size() == 8);
    REQUIRE(indices.size() == 36);
    CHECK(indices.size() % 3 == 0);

    std::set<std::tuple<real_t, real_t, real_t>> unique_positions;
    for (const Vector3 &vertex : vertices)
    {
        CHECK(std::abs(std::abs(vertex.x) - 1.0f) < 1e-5f);
        CHECK(std::abs(std::abs(vertex.y) - 1.0f) < 1e-5f);
        CHECK(std::abs(std::abs(vertex.z) - 1.0f) < 1e-5f);
        unique_positions.emplace(vertex.x, vertex.y, vertex.z);
    }
    CHECK(unique_positions.size() == vertices.size());

    for (const uint32_t index : indices)
        CHECK(index < vertices.size());
}

TEST_CASE("SkyboxCube: triangles face outward")
{
    std::vector<Vector3> vertices;
    std::vector<uint32_t> indices;
    SkyboxCube::create_geometry(vertices, indices);

    for (size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const Vector3 &a = vertices[indices[i]];
        const Vector3 &b = vertices[indices[i + 1]];
        const Vector3 &c = vertices[indices[i + 2]];
        const Vector3 normal = (b - a).cross(c - a);
        const Vector3 center = (a + b + c) / 3.0f;

        // 三角形非退化，且法线朝外（与三角形中心同向），
        // 保证从立方体内部看六个面都能正常渲染。
        CHECK(normal.length_squared() > 1e-5f);
        CHECK(normal.dot(center) > 0.0f);
    }
}
