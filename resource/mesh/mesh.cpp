#include "mesh.h"
#include "os/memory.h"
#include "math/math_funcs.h"

Ref<Resource> Mesh::duplicate() const
{
    Mesh *copy = memnew(Mesh);
    copy->vertices = vertices;
    copy->uvs = uvs;
    copy->indices = indices;
    copy->normals = normals;
    copy->tangents = tangents;
    copy->bitangents = bitangents;
    copy->rid_dirty = true;
    return Ref<Resource>(copy);
}

void Mesh::compute_tangents()
{
    tangents.resize(vertices.size());
    bitangents.resize(vertices.size());
    std::printf("indices:%d\n", indices.size());
    for (uint32_t i = 0; i < indices.size(); i += 3)
    {
        const int i1 = indices[i], i2 = indices[i + 1], i3 = indices[i + 2];
        const Vector3 p1 = vertices[i1];
        const Vector3 p2 = vertices[i2];
        const Vector3 p3 = vertices[i3];

        const Vector2 uv1 = uvs[i1];
        const Vector2 uv2 = uvs[i2];
        const Vector2 uv3 = uvs[i3];

        const Vector3 n1 = normals[i1];
        const Vector3 n2 = normals[i2];
        const Vector3 n3 = normals[i3];

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
        // det=0,并非三角形
        if (Math::is_zero_approx(det))
            continue;
        Vector3 T = ((E1 * dV2 - E2 * dV1) * (1.0 / det)).normalized();
        Vector3 B = ((E2 * dU1 - E1 * dU2) * (1.0 / det)).normalized();

        // 顶点切线累加
        tangents[i1] += T;
        tangents[i2] += T;
        tangents[i3] += T;
        bitangents[i1] += B;
        bitangents[i2] += B;
        bitangents[i3] += B;
    }

    // 对所有切线做正交化和归一
    for (uint32_t i = 0; i < tangents.size(); i++)
    {
        // TODO 施密特正交化
        // 归一化
        tangents[i].normalize();
        bitangents[i].normalize();
    }
}

Mesh::~Mesh()
{
    // RID 由渲染服务器持有并释放；资源模块不依赖渲染层，这里保持空实现。
}
