#pragma once

#include "3d/node3d.h"
#include "3d/mesh_instance_3d.h"
#include "scene/scene.h"

class Instantiate
{
public:
    /// @brief 实例化一个3D场景资源
    /// @param scene 3D场景资源 PackedScene
    /// @return 一个包含场景节点树的Node3D
    static Node3D *instantiate_scene(const Ref<PackedScene> &scene);
};

inline Node3D *Instantiate::instantiate_scene(const Ref<PackedScene> &scene)
{
    Node3D *result = memnew(Node3D);
    result->set_position({0, 0, 0});

    const std::vector<PackedScene::Node> scene_nodes = scene->get_nodes();
    const std::vector<int> scene_roots = scene->get_roots();

    std::vector<Node3D *> nodes;

    // 创建场景树节点
    for (const PackedScene::Node &scene_node : scene_nodes)
    {
        switch (scene_node.kind)
        {
        case PackedScene::NodeKind::Node3D:
        {
            Node3D *node3d = memnew(Node3D);
            node3d->set_transform(scene_node.local_transform);
            nodes.push_back(node3d);
            break;
        }

        case PackedScene::NodeKind::MeshInstance:
        {
            if (!scene_node.mesh.is_valid())
            {
                std::printf("[WARN] invalid mesh instance node in packed scene, creating node3d\n");
                Node3D *fallback_node = memnew(Node3D);
                fallback_node->set_transform(scene_node.local_transform);
                nodes.push_back(fallback_node);
            }
            else
            {
                MeshInstance3D *mesh_node = memnew(MeshInstance3D(scene_node.mesh));
                mesh_node->set_transform(scene_node.local_transform);
                nodes.push_back(mesh_node);
            }
            break;
        }
        default:
        {
            // TODO 其他节点,现在只有Node3D和MeshInstance3D
            Node3D *unknown_fallback_node = memnew(Node3D);
            unknown_fallback_node->set_transform(scene_node.local_transform);
            nodes.push_back(unknown_fallback_node);
            break;
        }
        }
    }

    // 构建 父子关系
    for (int i = 0; i < scene_nodes.size(); i++)
    {
        const PackedScene::Node scene_node = scene_nodes[i];
        Node3D *node = nodes[i];
        for (int idx : scene_node.children)
        {
            node->add_child(nodes[idx]);
        }
    }

    for (int idx : scene_roots)
    {
        result->add_child(nodes[idx]);
    }
    return result;
}