#pragma once

#include "resource.h"
#include "object/rid.h"
#include "math/vector3.h"
#include "math/vector2.h"
#include <cstdint>
#include <vector>

/// @brief CPU 侧网格资源。
///
/// 资源模块不依赖渲染服务器：这里只保存顶点 / 法线 / UV / 索引等 CPU 数据，
/// 以及一个可选的 RID。RID 由渲染服务器在后续阶段回填，通过
/// is_rid_dirty() / set_rid() 追踪“数据已变更、需要重新上传”的状态。
class Mesh: public Resource
{
public:
    struct Surface {
        std::vector<Vector3> vertices;
        std::vector<Vector3> normals;
        std::vector<Vector2> uvs;
        std::vector<uint32_t> indices;

        bool is_empty() const { return vertices.empty(); }
        uint32_t get_vertex_count() const { return static_cast<uint32_t>(vertices.size()); }
        uint32_t get_index_count() const { return static_cast<uint32_t>(indices.size()); }
        uint32_t get_triangle_count() const { return get_index_count() / 3; }
    };

private:
    RID rid = 0;
    bool rid_dirty = true;
    std::vector<Surface> surfaces;

public:
    RID get_rid() const override { return rid; }
    Ref<Resource> duplicate() const override;

    int get_surface_count() const { return static_cast<int>(surfaces.size()); }

    Surface &get_surface(int index) { return surfaces[index]; }
    const Surface &get_surface(int index) const { return surfaces[index]; }

    std::vector<Surface> &get_surfaces() { return surfaces; }
    const std::vector<Surface> &get_surfaces() const { return surfaces; }

    void add_surface(const Surface &surface)
    {
        surfaces.push_back(surface);
        mark_rid_dirty();
    }

    void clear_surfaces()
    {
        surfaces.clear();
        mark_rid_dirty();
    }

    /// @brief 数据相对 GPU 是否已过期（需要重新上传）。
    bool is_rid_dirty() const { return rid_dirty; }

    /// @brief 由渲染服务器上传完成后回填 RID。
    void set_rid(RID p_rid)
    {
        rid = p_rid;
        rid_dirty = false;
    }

    void mark_rid_dirty() { rid_dirty = true; }

    ~Mesh();
};
