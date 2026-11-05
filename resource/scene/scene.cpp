#include "scene.h"
#include "os/memory.h"

#include <algorithm>
#include <string>

int PackedScene::add_node(const Node &node)
{
    nodes.push_back(node);
    return static_cast<int>(nodes.size()) - 1;
}

void PackedScene::add_root(int idx)
{
    roots.push_back(idx);
}

Ref<Resource> PackedScene::duplicate() const
{
    // 节点数组是深拷贝，但 Node::mesh 是 Ref：Mesh / Material / Texture 仍然共享。
    PackedScene *copy = memnew(PackedScene);
    copy->scene_name = scene_name;
    copy->nodes = nodes;
    copy->roots = roots;
    return Ref<Resource>(copy);
}

namespace
{
    bool fail(std::string *error, const std::string &message)
    {
        if (error != nullptr) *error = message;
        return false;
    }
} // namespace

bool PackedScene::validate(std::string *error) const
{
    const int node_count = static_cast<int>(nodes.size());

    for (int i = 0; i < node_count; ++i)
    {
        const Node &node = nodes[i];

        if (node.parent_idx < -1 || node.parent_idx >= node_count)
        {
            return fail(error, "node " + std::to_string(i) + " has invalid parent index " +
                                   std::to_string(node.parent_idx));
        }

        for (int child : node.children)
        {
            if (child < 0 || child >= node_count)
            {
                return fail(error, "node " + std::to_string(i) + " has invalid child index " +
                                       std::to_string(child));
            }

            // children 与 parent 互为冗余，两边必须一致。
            if (nodes[child].parent_idx != i)
            {
                return fail(error, "node " + std::to_string(i) + " lists child " + std::to_string(child) +
                                       ", but the child's parent is " +
                                       std::to_string(nodes[child].parent_idx));
            }
        }

        if (node.parent_idx >= 0)
        {
            const std::vector<int> &siblings = nodes[node.parent_idx].children;
            if (std::find(siblings.begin(), siblings.end(), i) == siblings.end())
            {
                return fail(error, "node " + std::to_string(i) + " points to parent " +
                                       std::to_string(node.parent_idx) +
                                       ", but is missing from the parent's children");
            }
        }
    }

    for (size_t i = 0; i < roots.size(); ++i)
    {
        const int root = roots[i];
        if (root < 0 || root >= node_count)
        {
            return fail(error, "root entry " + std::to_string(i) + " references invalid node index " +
                                   std::to_string(root));
        }
        if (nodes[root].parent_idx != -1)
        {
            return fail(error, "root entry " + std::to_string(root) + " is not a root node (parent=" +
                                   std::to_string(nodes[root].parent_idx) + ")");
        }
    }

    return true;
}
