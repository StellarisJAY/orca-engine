#include "mesh.h"
#include "os/memory.h"
#include "math/math_funcs.h"

Ref<Resource> Mesh::duplicate() const
{
    Mesh *copy = memnew(Mesh);
    copy->surfaces = surfaces;
    return Ref<Resource>(copy);
}

namespace
{
    /// @brief 为缺失或退化的切线生成一组稳定的正交基。
    void make_fallback_tangent(const Vector3 &normal, Vector3 &tangent, Vector3 &bitangent)
    {
        const Vector3 up = Math::abs(normal.y) < 0.999f
                         ? Vector3(0, 1, 0)
                         : Vector3(1, 0, 0);
        tangent = normal.cross(up);
        if (tangent.length_squared() < 1e-12f)
            tangent = Vector3(1, 0, 0);
        tangent.normalize();
        bitangent = normal.cross(tangent);
    }
} // namespace

void compute_surface_tangents(Mesh::Surface &surface)
{
    std::vector<Vector3> &tangents = surface.tangents;
    std::vector<Vector3> &bitangents = surface.bitangents;
    std::vector<Vector3> &vertices = surface.vertices;
    std::vector<Vector3> &normals = surface.normals;
    std::vector<Vector2> &uvs = surface.uvs;
    std::vector<uint32_t> &indices = surface.indices;

    const size_t vertex_count = vertices.size();
    tangents.assign(vertex_count, Vector3::ZERO);
    bitangents.assign(vertex_count, Vector3::ZERO);

    const bool has_normals = normals.size() == vertex_count;
    const bool has_uvs = uvs.size() == vertex_count;

    if (has_uvs)
    {
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const size_t i1 = indices[i];
            const size_t i2 = indices[i + 1];
            const size_t i3 = indices[i + 2];
            if (i1 >= vertex_count || i2 >= vertex_count || i3 >= vertex_count)
                continue;

            const Vector3 p1 = vertices[i1];
            const Vector3 p2 = vertices[i2];
            const Vector3 p3 = vertices[i3];

            const Vector2 uv1 = uvs[i1];
            const Vector2 uv2 = uvs[i2];
            const Vector2 uv3 = uvs[i3];

            // 解方程：
            // E1 = T*dU1 + B*dV1
            // E2 = T*dU2 + B*dV2
            // [T] = 1/det * [dV2, -dV1] [E1]
            // [B]           [-dU2, dU1] [E2]
            const Vector3 E1 = p2 - p1;
            const Vector3 E2 = p3 - p1;
            const Vector2 dUV1 = uv2 - uv1;
            const Vector2 dUV2 = uv3 - uv1;
            const real_t dU1 = dUV1.x, dV1 = dUV1.y, dU2 = dUV2.x, dV2 = dUV2.y;
            const real_t det = dU1 * dV2 - dV1 * dU2;
            // det=0 表示 UV 退化，跳过累加，最后用兜底基填充。
            if (Math::is_zero_approx(det))
                continue;

            Vector3 T = (E1 * dV2 - E2 * dV1) * (1.0 / det);
            Vector3 B = (E2 * dU1 - E1 * dU2) * (1.0 / det);
            if (T.length_squared() < 1e-12f || B.length_squared() < 1e-12f)
                continue;

            T.normalize();
            B.normalize();

            // 顶点切线累加
            tangents[i1] += T;
            tangents[i2] += T;
            tangents[i3] += T;
            bitangents[i1] += B;
            bitangents[i2] += B;
            bitangents[i3] += B;
        }
    }

    // 对所有切线做施密特正交化；零向量使用根据法线生成的兜底基，
    // 避免 normalize(0) 产生 NaN 并传到 shader。
    for (size_t i = 0; i < vertex_count; ++i)
    {
        Vector3 normal = has_normals ? normals[i] : Vector3(0, 1, 0);
        if (normal.length_squared() < 1e-12f)
            normal = Vector3(0, 1, 0);
        else
            normal.normalize();

        Vector3 tangent = tangents[i];
        tangent -= normal * normal.dot(tangent);

        if (tangent.length_squared() < 1e-12f)
        {
            make_fallback_tangent(normal, tangent, bitangents[i]);
        }
        else
        {
            tangent.normalize();
            Vector3 bitangent = normal.cross(tangent);
            // 保留 UV 解算出的手性。
            if (bitangents[i].dot(bitangent) < 0.0f)
                bitangent *= -1.0f;
            bitangents[i] = bitangent;
        }

        tangents[i] = tangent;
    }
}

void Mesh::compute_tangents()
{
    for(Surface &surface : surfaces)
    {
        compute_surface_tangents(surface);
    }
}

Mesh::~Mesh()
{
    // RID 由渲染服务器持有并释放；资源模块不依赖渲染层，这里保持空实现。
}

size_t Mesh::get_vertex_count() const
{
    size_t total = 0;
    for(const Surface &surface : surfaces)
    {
        total += surface.vertices.size();
    }
    return total;
}

size_t Mesh::get_triangle_count() const
{
    size_t total = 0;
    for(const Surface &surface : surfaces)
    {
        total += surface.indices.size() / 3;
    }
    return total;
}

size_t Mesh::get_surface_count() const
{
    return surfaces.size();
}
