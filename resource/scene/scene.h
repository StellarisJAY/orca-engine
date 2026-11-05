#pragma once

#include "object/ref_counted.h"
#include "resource.h"
#include "mesh/mesh.h"
#include "material/material.h"
#include "texture/image_texture.h"
#include "math/transform_3d.h"
#include <cstdint>
#include <string>
#include <vector>

/// @brief 与文件格式无关的场景资源：一棵（或一组根节点组成的）节点树的 CPU 描述。
///
/// 资源层只描述数据，不包含 Node3D / MeshInstance3D；实例化由 scene 层的
/// instantiate_scene() 完成。任何“场景型”格式（glTF、FBX、自有 .scn）都应产出本类型。
///
/// 注意与两个同名概念区分：
/// - SceneTree::current_scene 是运行时场景树的根节点（scene 层）；
/// - RenderingServer 的 scene RID 属于 World3D（渲染层）。
class PackedScene: public Resource
{
public:
    /// @brief 节点种类；决定实例化时创建哪种 Node。
    /// 新增相机/灯光等类型时，在这里加枚举，并在 instantiate_scene() 里加对应分支。
    enum class NodeKind: uint8_t
    {
        None,
        Node3D,
        MeshInstance,
        // TODO 相机、灯光、动画等等类型
        Max
    };

    /// @brief 场景节点
    struct Node
    {
        std::string name;
        NodeKind kind = NodeKind::Node3D;
        /// @brief 父节点idx，根节点为-1；与 children 互为冗余，由 loader 保持一致
        int parent_idx = -1;
        /// @brief 子节点idx集合
        std::vector<int> children;
        /// @brief local transform；全局变换由 scene 层在父子挂接后计算
        Transform3D local_transform;
        /// @brief 仅MeshInstance类型节点拥有；多个节点可以共享同一个 Mesh
        Ref<Mesh> mesh;
    };

    RID get_rid() const override { return 0; }
    /// @brief 深拷贝节点数组与 roots；Mesh / Material / Texture 按 Ref 共享。
    Ref<Resource> duplicate() const override;

public:
    /// @brief 追加一个节点，返回其索引（索引即节点 id）。
    int add_node(const Node &node);
    void add_root(int idx);
    void set_scene_name(const std::string &name) { scene_name = name; }
    const std::string get_scene_name() const { return scene_name; }

    const std::vector<Node> &get_nodes() const { return nodes; }
    const std::vector<int> &get_roots() const { return roots; }

    /// @brief 校验索引引用是否自洽（children / parent / roots），实例化前应调用。
    /// @param error 非空时写入第一条错误描述
    /// @return 结构自洽返回 true
    bool validate(std::string *error = nullptr) const;

private:
    /// @brief 场景名称
    std::string scene_name;
    /// @brief 节点集合，索引即节点 id
    std::vector<Node> nodes;
    /// @brief 根节点idx集合
    std::vector<int> roots;
};
