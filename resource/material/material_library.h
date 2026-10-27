#pragma once

#include "resource.h"
#include "material.h"
#include "os/memory.h"
#include <string>
#include <unordered_map>

/// @brief 一个 .mtl 文件解析结果：材质名 -> 材质资源。
///
/// MTL 文件天然可以包含多个材质，所以不能用一个 StandardMaterial 表示。
/// MaterialLibrary 只作为加载期容器，Mesh 的 surface 会持有具体的
/// Ref<StandardMaterial>，不依赖 library 延长生命周期。
class MaterialLibrary : public Resource
{
private:
    std::unordered_map<std::string, Ref<StandardMaterial>> materials;

public:
    RID get_rid() const override { return 0; }

    Ref<Resource> duplicate() const override
    {
        MaterialLibrary *copy = memnew(MaterialLibrary);
        copy->materials = materials;
        return Ref<Resource>(copy);
    }

    void add_material(const std::string &name, const Ref<StandardMaterial> &material)
    {
        materials[name] = material;
    }

    bool has_material(const std::string &name) const
    {
        return materials.find(name) != materials.end();
    }

    Ref<StandardMaterial> find(const std::string &name) const
    {
        auto it = materials.find(name);
        if (it == materials.end()) return Ref<StandardMaterial>(nullptr);
        return it->second;
    }

    const std::unordered_map<std::string, Ref<StandardMaterial>> &get_materials() const
    {
        return materials;
    }

    size_t get_material_count() const { return materials.size(); }
};
