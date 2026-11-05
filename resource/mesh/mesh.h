#pragma once

#include "resource.h"
#include "object/rid.h"
#include "math/vector3.h"
#include "math/vector2.h"
#include "object/ref_counted.h"
#include "material/material.h"
#include <cstdint>
#include <utility>
#include <vector>

/// @brief CPU 侧网格资源。
///
/// 资源模块不依赖渲染服务器：这里只保存顶点 / 法线 / UV / 索引等 CPU 数据，
/// 以及一个可选的 RID。RID 由渲染服务器在后续阶段回填，通过
/// is_rid_dirty() / mark_rid_dirty() / mark_rid_clean() 追踪
/// “数据已变更、需要重新上传”的状态。
class Mesh : public Resource
{
public:
    struct Surface
    {
        /// @brief 顶点
        std::vector<Vector3> vertices;
        /// @brief 法线
        std::vector<Vector3> normals;
        /// @brief uv
        std::vector<Vector2> uvs;
        /// @brief 顶点切线
        std::vector<Vector3> tangents;
        /// @brief 顶点副切线
        std::vector<Vector3> bitangents;
        /// @brief 面，顶点索引
        std::vector<uint32_t> indices;
        /// @brief 材质
        Ref<StandardMaterial> material;
    };
private:
    RID rid = 0;
    bool rid_dirty = true;
    std::vector<Surface> surfaces;
public:
    RID get_rid() const override { return rid; }
    Ref<Resource> duplicate() const override;

    /// @brief 数据相对 GPU 是否已过期（需要重新上传）。
    bool is_rid_dirty() const { return rid_dirty; }

    /// @brief 创建/回填 GPU 侧的 RID。
    /// 注意：不会清除 dirty 标志；surface 上传完成后要显式调用 mark_rid_clean()。
    void set_rid(RID p_rid) { rid = p_rid; }

    void mark_rid_dirty() { rid_dirty = true; }

    /// @brief 标记 CPU 数据已经完整上传到渲染服务器
    void mark_rid_clean() { rid_dirty = false; }

    /// @brief 加载顶点数据后在CPU侧计算切线空间，避免shader每帧计算
    void compute_tangents();

    ~Mesh();

    bool is_empty() const { return get_vertex_count() == 0; }
    size_t get_vertex_count() const;
    size_t get_triangle_count() const;
    size_t get_surface_count() const;

    Surface &get_surface(uint32_t idx) { return surfaces[idx]; }
    const Surface &get_surface(uint32_t idx) const { return surfaces[idx]; }
    /// @brief 拷贝一个 surface（保留给需要共享 CPU 数据的调用方）。
    void add_surface(const Surface &surface) { surfaces.push_back(surface); }
    /// @brief 移动一个 surface，避免加载器把整份顶点/索引再复制一遍。
    void add_surface(Surface &&surface) { surfaces.push_back(std::move(surface)); }
    void reserve_surfaces(size_t count) { surfaces.reserve(count); }
    const std::vector<Surface> &get_surfaces() const { return surfaces; }
};
