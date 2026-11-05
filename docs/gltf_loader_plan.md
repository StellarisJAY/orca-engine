# glTF 加载方案（cgltf）

> 状态：**阶段 0–2 已实现**（前置修复、`PackedScene` 通用场景资源、`GLTFLoader` 解析 + 材质/贴图）；
> 阶段 3（`instantiate_scene` 实例化与演示）与阶段 4（手动 GPU 验证）尚未实现。
> 实现细节与偏差见 §5 各阶段的状态说明。
> 目标格式：`.gltf`（JSON + 外部 bin/贴图）与 `.glb`（二进制容器）
> 支持内容：模型、材质、贴图
> 明确不支持（本期）：相机、灯光、动画、skin、morph target、多 UV、透明混合
> 资源形态：**与格式无关的通用 `Scene` 资源**；glTF loader 只是该资源的一个生产者
> 结果形态：scene 层 `instantiate_scene(Ref<Scene>)` 生成 `Node3D` 根节点 + 树状子节点
> （模型节点实例化为 `MeshInstance3D`，其余节点为 `Node3D`）

---

## 1. 背景与目标

引擎目前只有 OBJ/MTL/PNG 加载能力，资源链路为：

```text
ResourceLoader::load(url)
  -> ObjMeshLoader -> Mesh（多个 Surface，每个 Surface 带 StandardMaterial）
  -> MeshInstance3D 进入场景树
  -> ensure_surfaces / ensure_material / ensure_texture
  -> RenderingServer 上传并绘制
```

glTF 是场景文件，不能像 OBJ 一样只返回一个 `Mesh`。本方案的目标是：

1. 新增 `.gltf/.glb` 的 `ResourceFormatLoader`；
2. 解析结果为**通用 `Scene` 资源**（纯 CPU 场景描述，`Resource` 子类，与文件格式无关）；
3. 在 scene 层用通用函数 `instantiate_scene(Ref<Scene>)` 把 `Scene` 实例化为 `Node3D` 树；
4. 复用现有 `Mesh / StandardMaterial / ImageTexture / RenderingServer`，不新增渲染 API；
5. 保持 `resource` 不依赖 `rendering/scene` 的分层约束。

**为什么统一成 `Scene` 而不是 `GltfDocument`：** `ResourceLoader::load()` 对所有扩展名都返回
`Ref<Resource>`，调用方不应该“知道这个 URL 是 glTF 才能 cast 成对应文档类”。场景资源统一为
`Scene` 后，以后新增 FBX / COLLADA / 自有 `.scn` 时只需要新增 loader，把结果填进同一个 `Scene`，
实例化、演示程序、后续场景切换/序列化都不需要改。glTF 的格式细节（cgltf、URI、UV、颜色空间、
多 scene）全部留在 `GltfLoader` 内部。

非目标：编辑器、场景序列化、动画播放、骨骼蒙皮、GLB 之外的打包格式。

---

## 2. 现状分析

### 2.1 可直接复用的能力

| 现有能力 | 位置 | 对 glTF 的意义 |
|---|---|---|
| `ResourceLoader` 扩展名注册 + URL 弱引用缓存 | `resource/resource_loader.cpp` | 注册 `.gltf/.glb`，同一 `Scene` 只解析一次 |
| `Mesh` 多 `Surface` | `resource/mesh/mesh.h` | 一个 glTF mesh 的多个 primitive 映射为多个 Surface |
| Surface 独立材质 | `resource/mesh/mesh.h` | 一个模型多材质无需改结构 |
| `StandardMaterial` 6 贴图槽 + 标量参数 | `resource/material/material.h` | 可映射 baseColor/normal/roughness/metallic/AO |
| `ImageTexture` CPU 像素 + sRGB 标记 | `resource/texture/image_texture.h` | 复用 stb_image 解码 PNG/JPEG |
| `MeshInstance3D` 进树时上传 | `scene/3d/mesh_instance_3d.cpp` | 实例化后加入场景树即可渲染 |
| `Node3D` 父子全局变换传播 | `scene/3d/node3d.cpp` | glTF 节点层级可直接表达 |

### 2.2 关键缺口

1. **资源层不能返回 Node3D**
   `Resource : RefCounted`，`Node3D : Object`，且 `resource` 不能依赖 `scene`。必须拆分为
   “解析成通用 `Scene` 资源（CPU 数据）”和“scene 层实例化”。`Scene` 不提供 `instantiate()`
   成员函数，否则 `orca-resource` 会反向依赖 `orca-scene`。

2. **没有 JSON / GLB / accessor 解析能力**
   `thirdparty` 中没有 JSON 库；`FileAccess` 缺少明确的二进制读取 API。GLB chunk、bufferView、accessor、byteStride 都需要解析库或自研。

3. **`Node3D` 无法表达任意局部变换**
   当前只有 `set_position/set_scale/rotate`，内部用欧拉角存储；glTF 使用 `matrix` 或 `T + quaternion + S`。需要 `set_transform(Transform3D)` 及 `Basis` 分解能力。

4. **glTF metallic-roughness 是打包贴图**
   引擎 shader 对 roughness 和 metallic 分别采样各自纹理的 `.r`；glTF 的 `metallicRoughnessTexture` 是 G=roughness、B=metallic 的一张图。必须拆分。

5. **共享 Mesh 会被重复上传**
   `MeshInstance3D` 每次进入场景树都无条件 `ensure_surfaces()`，而后者无条件 `add_mesh_surface()`。多个节点共享同一个 `Mesh` 时会重复追加 surface。glTF 中 mesh 复用非常常见，必须先修。

6. **`ImageTexture` 能力不足**
   缺少 sampler 的 setter；只能从文件加载，GLB 内嵌图片 / `data:` URI 需要“从内存解码”入口。

7. **URI 处理**
   `Path::join` 不处理 `%XX` 百分号编码，也不处理 `data:` URI。

8. **其他能力边界**
   - `StandardMaterial::emission` 未接入 shader，emissive texture 无槽位；
   - 渲染端未开启混合，alpha 材质无法正确显示；
   - 未开启背面剔除，`doubleSided` 暂时无影响；
   - 只支持一套 UV，`TEXCOORD_1+` 与 `KHR_texture_transform` 不支持。

### 2.3 现有测试注册问题

`AGENTS.md` 声称 `tests/resource/test_obj_loader.cpp` 已加入 `orca-tests`，但实际 `tests/CMakeLists.txt` 的源列表中没有它。新增 glTF 测试时必须手动加入，并顺便补回 OBJ 测试。

---

## 3. 总体设计

### 3.1 分层

```text
resource 层（不依赖 rendering / scene）
  GltfLoader : ResourceFormatLoader
      .gltf/.glb -> cgltf 解析 -> Scene : Resource       （通用场景资源）
          - 节点树：局部 Transform3D + 节点种类（NODE_3D / MESH_INSTANCE_3D）
          - 每个带 mesh 的节点 -> Ref<Mesh>（多个 Surface，可被多个节点共享）
          - 每个 Surface -> Ref<StandardMaterial>
          - 材质 -> Ref<ImageTexture>
          - 不包含 cgltf 类型、URI、多 scene、scene_index 等格式细节

scene 层（依赖 resource）
  instantiate_scene(Ref<Scene>) -> Node3D*（树）
      - 纯变换节点 -> Node3D
      - 带 mesh 的节点 -> MeshInstance3D(Ref<Mesh>)
      - mesh 为空的节点降级为 Node3D 并警告（MeshInstance3D 进树时必须非空）
      - 先 add_child、再 set_transform，保证全局变换正确
      - 返回合成根节点
  load_scene_instance(url)   // 便捷入口：load + instantiate

调用方：
  Ref<Scene> scene = ResourceLoader::get_singleton()->load("res://scene.glb")
                         .cast_checked<Scene>();
  Node3D *root = instantiate_scene(scene);
  world->add_child(root);   // 进树后自动上传并渲染
```

分层规则：

1. **格式相关的 loader 可以叫 `GltfLoader`，但输出类型必须是通用的 `Scene`。** 资源类型与文件
   格式解耦，这是本方案的核心约束。
2. **`Scene` 不提供 `instantiate()` 成员函数。** `Scene` 属于 `resource` 层，
   `Node3D`/`MeshInstance3D` 属于 `scene` 层；成员函数会导致 `resource` 反向依赖 `scene`。
   实例化以 scene 层自由函数的形式提供。
3. **`Scene` 只保存“节点树 + 每节点 payload”。** 材质、贴图已在 `Mesh::Surface::material`
   的引用图里，不需要再建一张资源表。
4. **多 scene 的取舍留在 loader。** `.gltf` 有多个 scene 时只构建默认 scene（与本方案
   “不支持多场景切换”一致）并打印警告；通用 `Scene` 不背 `scenes[]` 数组。

> 命名提示：`Scene` 与 `SceneTree::current_scene`（运行时根节点）、
> `RenderingServer::create_scene()`（`World3D` 背后的渲染 scene RID）是不同层的东西。
> 若团队更希望与 Godot 对齐，可改名为 `PackedScene` 或 `SceneResource`，结构不变；
> 关键是**只保留一个通用场景资源类型**。

### 3.2 为什么选 cgltf

- MIT，单头文件，仓库内只需 `cgltf.h` + 一个 `cgltf_impl.cpp`；
- 覆盖 `.gltf/.glb`、buffer/bufferView/accessor/byteStride、normalized、sparse；
- 直接暴露 glTF 规范字段（PBR、normal scale、occlusion strength、sampler），映射清晰；
- 不负责图片解码，正好复用现有 `stb_image`；
- 可以完全封装在 `GltfLoader` 内部，`Scene` 不暴露 cgltf 类型。

与 Assimp 的取舍见附录 A。

### 3.3 数据结构

通用场景资源（`resource/scene/scene.h`）：

```cpp
#pragma once

#include "resource.h"
#include "math/transform_3d.h"
#include "mesh/mesh.h"
#include "object/ref_counted.h"

#include <cstdint>
#include <string>
#include <vector>

/// @brief 与文件格式无关的场景资源：一棵（或一组根组成的）节点树的 CPU 描述。
///
/// 资源层只描述数据，不包含 Node3D / MeshInstance3D；实例化在 scene 层由
/// instantiate_scene() 完成。任何“场景型”格式（glTF、FBX、自有 .scn）都应产出本类型。
class Scene : public Resource
{
public:
    enum class NodeKind : uint8_t
    {
        NODE_3D = 0,
        MESH_INSTANCE_3D = 1,
        // 后续扩展：CAMERA_3D / LIGHT_3D / ...
    };

    struct Node
    {
        std::string name;
        NodeKind kind = NodeKind::NODE_3D;
        /// @brief 父节点索引，-1 表示根。与 children 互为冗余，由 loader 保持一致。
        int parent = -1;
        std::vector<int> children;
        /// @brief 局部变换；全局变换由 scene 层在父子挂接后计算。
        Transform3D local_transform;
        /// @brief 仅 MESH_INSTANCE_3D 使用；多个节点可以共享同一个 Mesh。
        Ref<Mesh> mesh;
    };

    RID get_rid() const override { return 0; }

    /// @brief 深拷贝节点数组；Mesh / Material / Texture 按 Ref 共享。
    Ref<Resource> duplicate() const override;

    const std::string &get_scene_name() const { return scene_name; }
    const std::vector<Node> &get_nodes() const { return nodes; }
    const std::vector<int> &get_roots() const { return roots; }

    /// @brief loader 构建用；返回新节点索引。
    int add_node(const Node &node);
    void add_root(int node_index);
    void set_scene_name(const std::string &name) { scene_name = name; }

    /// @brief 校验索引引用是否自洽；loader 的产物在实例化前应过一次。
    bool validate(std::string *error = nullptr) const;

private:
    std::string scene_name;
    std::vector<Node> nodes; // 索引即节点 id，与来源格式的节点顺序一致
    std::vector<int> roots;
};
```

> **实现命名说明**：实际代码中的类型名是 `PackedScene`（`resource/scene/scene.h`），
> 与 Godot 的命名对齐；结构与本节 `Scene` 完全一致。下文出现的 `Scene` 均指该 `PackedScene`。
> `PackedScene::validate()` 会校验 `children` / `parent_idx` / `roots` 三者的双向一致性。

设计要点：

- **扁平数组 + 索引**：节点 id 与 glTF `node` 下标一一对应，loader 不需要额外映射表；
  `duplicate()` 只是 `nodes` 的拷贝，不存在递归析构问题。
- **`roots` 是数组**：glTF 的一个 scene 允许挂多个根；通用资源直接支持“森林”，实例化时
  统一挂到合成根下。
- **节点 payload 用 `Ref<Mesh>` 而不是 `meshes` 索引表**：`Ref` 本身已经做共享，重复引用
  同一个 glTF mesh 时拿到同一个 `Ref<Mesh>`；材质/贴图通过 `Mesh::Surface::material`
  间接持有。少一张表、少一层索引换算。
- **`NodeKind` 枚举而不是字符串类型名**：当前引擎没有 `Variant` / `ClassDB`，硬编码枚举与
  现有风格一致；新增相机、灯光时在枚举和实例化函数里各加一个分支。
- **不包含**：`scenes[]`、`default_scene`、`scene_index`、`cgltf_*`、源文件 URI、sampler
  参数、UV 翻转标记——这些全部是 loader 的私有实现细节。
- **`get_rid()` 返回 0**：`Scene` 是纯 CPU 资源；`World3D` 的渲染 scene RID 是运行时对象，
  不是它。
- **未来扩展位**（不本期）：`Environment3D`/世界设置、相机/灯光节点字段、动画轨道、节点可见性。

加载器（`resource/loaders/gltf_loader.h`）——名字保持格式相关，输出类型通用：

```cpp
class GltfLoader : public ResourceFormatLoader
{
public:
    std::vector<std::string> format_extensions() const override
    {
        return {".gltf", ".glb"};
    }

    // 输出通用 Scene，调用方不需要知道这是 glTF。
    Ref<Resource> load(const std::string url) override;
};
```

实例化入口（`scene/3d/scene_instantiate.h`）：

```cpp
class Scene;

/// @brief 把通用场景资源实例化为 Node 树；失败返回 nullptr。
///        返回的根节点尚未进入场景树，调用方 add_child 后才会上传渲染资源。
Node3D *instantiate_scene(const Ref<Scene> &scene);

/// @brief 便捷入口：ResourceLoader::load + instantiate_scene。
Node3D *load_scene_instance(const std::string &url);
```

### 3.4 加载流程

```text
GltfLoader::load(url)
  1. FileAccess 读取整个文件（二进制）
  2. cgltf_parse 解析 JSON / GLB（自动识别 magic）
  3. cgltf_load_buffers 加载外部 .bin
        - file callback 内部走 FileAccess + Path，确保 res:// 生效
        - 对 uri 做百分号解码
  4. 校验 asset.version 为 2.x，检查 required_extensions
  5. 构建材质/贴图
  6. 构建 Mesh（每个 glTF mesh 一个，多个 primitive -> 多个 Surface）
  7. 构建通用 Scene
        - glTF node 下标直接作为 Scene::Node 下标
        - 局部变换：cgltf_node_transform_local -> Transform3D
        - 只有 node.mesh 非空时 kind = MESH_INSTANCE_3D，否则 NODE_3D
        - 只构建一个 scene：默认 scene（data->scene）> 第 0 个 scene > 无父节点集合
        - 多 scene 时打印警告，记录被忽略的 scene 名称
  8. cgltf_free，返回 Ref<Scene>
```

加载失败策略沿用现有风格：`printf` 明确错误，返回空 `Ref`；单个 primitive / 单张贴图失败只跳过该部分，不整体失败。

多 scene 策略：`Scene` 只装一棵树；同一 `.gltf` 的其它 scene 不会被保留。若后续确实要访问，
增加格式专用入口 `GltfLoader::load_scene(url, scene_index)`（不走 `ResourceLoader` 的 URL 缓存，
或用 `url#index` 作为缓存键），**不要**因此把 `scenes[]` 塞回通用资源。

loader 产物防御：`children` 越界、`parent` 不一致、mesh 下标越界在 loader 内过滤并警告；
`Scene::validate()` 作为实例化前的最后一道检查。

### 3.5 实例化流程

```cpp
// scene/3d/scene_instantiate.cpp
namespace
{
Node3D *build_node(const Scene &scene, int index, Node *parent)
{
    const Scene::Node &desc = scene.get_nodes()[index];

    Node3D *node = nullptr;
    if (desc.kind == Scene::NodeKind::MESH_INSTANCE_3D && desc.mesh.is_valid())
    {
        node = memnew(MeshInstance3D(desc.mesh));
    }
    else
    {
        // Mesh 缺失（加载失败或引用了空 mesh）时退回纯变换节点。
        // 注意：MeshInstance3D 的 ENTER_TREE 会直接解引用 mesh，不能传空 Ref。
        if (desc.kind == Scene::NodeKind::MESH_INSTANCE_3D)
            std::printf("[WARN] scene node \"%s\" has no mesh, falling back to Node3D\n",
                        desc.name.c_str());
        node = memnew(Node3D);
    }

    // 关键顺序：先挂父节点，再设置局部变换。
    // Node3D::transform() 用 get_parent_global_transform() 计算全局变换。
    parent->add_child(node);
    node->set_transform(desc.local_transform);
    if (!desc.name.empty())
        node->set_name(desc.name);

    for (int child : desc.children)
    {
        build_node(scene, child, node);
    }
    return node;
}
} // namespace

Node3D *instantiate_scene(const Ref<Scene> &scene)
{
    if (scene.is_null()) return nullptr;

    Node3D *root = memnew(Node3D); // 合成根，恒等变换
    root->set_name(scene->get_scene_name());

    for (int node_index : scene->get_roots())
    {
        build_node(*scene, node_index, root);
    }
    return root;
}

Node3D *load_scene_instance(const std::string &url)
{
    Ref<Resource> resource = ResourceLoader::get_singleton()->load(url);
    if (resource.is_null()) return nullptr;

    Ref<Scene> scene = resource.cast_checked<Scene>();
    if (scene.is_null())
    {
        std::printf("[ERROR] resource is not a Scene: %s\n", url.c_str());
        return nullptr;
    }
    return instantiate_scene(scene);
}
```

说明：

- 多根节点统一挂到合成根下，行为可预测；合成根使用场景名（没有名字时用空串）。
- `Scene` 的节点种类到 `Node` 类型的映射只在这里出现；以后新增相机/灯光节点，只改这个函数。
- 合成根不进入场景树时，`MeshInstance3D` 不会触发上传；加入场景树后由 `ENTER_TREE` 统一上传。
- 如果调用方在实例化后移动合成根，`Node3D` 的变换通知会同步到渲染实例。
- 返回裸 `Node3D *`，所有权与现有 `Node`/`add_child` 风格一致；调用方负责在销毁时递归释放。
- 为什么不放进 `SceneTree`：`SceneTree::destroy()`、`find_node()` 目前还是空实现，先做独立
  函数风险最低；等 `change_scene` 落地时让它内部调用 `instantiate_scene()` 即可。

---

## 4. glTF → 引擎映射

### 4.1 节点与变换

| glTF | 引擎 |
|---|---|
| `node.matrix` 或 `translation/rotation/scale` | `Scene::Node::local_transform`（实例化时写入 `Node3D::set_transform`） |
| `scene.nodes` | `Scene::roots` |
| `node.children` | `Scene::Node::children`，实例化时 `Node::add_child` |
| `node.mesh` | `Scene::NodeKind::MESH_INSTANCE_3D` + `Ref<Mesh>`，否则 `NODE_3D` |
| `node.camera`、`KHR_lights_punctual` | 忽略，但保留 `NODE_3D` 占位，保证子节点变换正确 |
| `node.skin` | 忽略，按 bind pose 渲染并警告 |
| `node.weights` / morph targets | 忽略，使用基础网格并警告 |

`Node3D` 新增：

```cpp
void set_transform(const Transform3D &p_transform);
Transform3D get_transform() const; // 局部变换
```

`Basis` 新增：

```cpp
Vector3 get_scale() const;
real_t determinant() const;
Quaternion get_rotation_quaternion() const;
```

`set_transform` 实现思路：

```cpp
void Node3D::set_transform(const Transform3D &t)
{
    position = t.origin;

    Basis basis = t.basis;
    scale = basis.get_scale();

    // 负缩放：把负号吸收到某一轴上，保证旋转部分的行列式为 +1。
    if (basis.determinant() < 0.0f)
    {
        scale.x = -scale.x;
        basis.columns[0] = -basis.columns[0];
    }

    // 去掉缩放，得到纯旋转。
    for (int i = 0; i < 3; ++i)
    {
        const real_t length = scale[i] != 0.0f ? scale[i] : 1.0f;
        basis.columns[i] = basis.columns[i] / length;
    }

    Quaternion q(basis);
    rotation = q.euler_angles(euler_rotation_order);
    transform();
}
```

注意：glTF 的 TRS 语义是 `T * R * S`，与 `Node3D::transform()` 的“先缩放、再旋转、再平移”一致。矩阵分解会丢失剪切（glTF 节点通常没有剪切），负缩放需要上面的行列式处理。

### 4.2 网格与 primitive

| glTF | 引擎 |
|---|---|
| `mesh.primitives[]` | `Mesh::Surface[]`，一个 glTF mesh 对应一个 `Mesh` |
| `POSITION` | `Surface.vertices` |
| `NORMAL` | `Surface.normals`；缺失时补平滑法线 |
| `TEXCOORD_0` | `Surface.uvs`；缺失时填 `Vector2()`，数量必须与顶点一致 |
| `indices` | `Surface.indices`，u8/u16/u32 统一转 u32；无索引时生成 `0..N-1` |
| `TANGENT` | 忽略，统一用 `Mesh::compute_tangents()` 从 UV 重算 |
| `mode` | 支持 TRIANGLES；TRIANGLE_STRIP/FAN 可转三角形；POINTS/LINES 跳过并警告 |
| `material` | `Surface.material` |
| `COLOR_0` | 忽略 |
| `JOINTS_0` / `WEIGHTS_0` | 忽略，警告一次 |

特别注意：`GLRenderingDevice::create_mesh_surface` 要求 `vertices/normals/uvs/tangents/bitangents` 数量完全一致。即使 glTF 没有 UV，也要填零向量占位，不能留空。

索引越界要过滤；无索引时顶点数不是 3 的倍数要截断并警告。

### 4.3 材质

| glTF | 引擎 |
|---|---|
| `pbrMetallicRoughness.baseColorFactor` | `set_albedo()`，需要 linear → sRGB 编码 |
| `baseColorTexture` | 槽 0 Albedo，`srgb = true` |
| `metallicFactor` / `roughnessFactor` | `set_metallic()` / `set_roughness()` |
| `metallicRoughnessTexture` | 拆成两张图：G → Roughness 槽，B → Metallic 槽，`srgb = false` |
| `normalTexture` | 槽 1 Normal，`normal_scale = normalTexture.scale` |
| `occlusionTexture` | 槽 4 AO，`ao_strength = occlusionTexture.strength` |
| `emissiveFactor` | `set_emission()`；当前 shader 未使用，先存值并警告 |
| `emissiveTexture` | 无槽位，本阶段忽略并警告 |
| `alphaMode` / `alphaCutoff` | 不支持透明/裁剪，警告 |
| `doubleSided` | 引擎未开背面剔除，暂时无影响 |
| `KHR_materials_unlit` | 忽略，按普通 PBR 处理并警告 |
| `KHR_materials_pbrSpecularGlossiness` | 不支持，警告 |

### 4.4 贴图与 sampler

贴图来源三种：

1. **外部文件**：`image.uri` 相对路径，拼 `.gltf` 目录，做百分号解码，走 `ResourceLoader`；
2. **GLB 内嵌**：`image.bufferView`，直接 `stbi_load_from_memory`；
3. **data URI**：`data:image/png;base64,...`，base64 解码后 `stbi_load_from_memory`。

`ImageTextureLoader` 增加：

```cpp
static Ref<ImageTexture> load_from_memory(
    const uint8_t *data, size_t size,
    bool srgb, const char *debug_name);
```

`ImageTexture` 增加：

```cpp
void set_repeat(bool);
void set_linear_filter(bool);
void set_gen_mipmap(bool);
```

sampler 映射：

| glTF | 引擎 |
|---|---|
| `wrapS/wrapT = REPEAT` | `repeat = true` |
| `wrapS/wrapT = CLAMP_TO_EDGE` | `repeat = false` |
| `wrapS/wrapT = MIRRORED_REPEAT` | 暂按 `repeat = true` 近似并警告 |
| `magFilter = NEAREST` | `linear_filter = false` |
| `magFilter = LINEAR` | `linear_filter = true` |
| `minFilter` 带 mipmap | `gen_mipmap = true` |
| `minFilter = NEAREST/LINEAR` | `gen_mipmap = false` |

同一 image 被不同槽位复用时，按 `(texture index, srgb)` 在 loader 内缓存；外部纹理从 `ResourceLoader` 拿到后如果 sRGB 不一致，沿用 MTL 的做法 `duplicate()` 后改标志，避免污染共享实例。

### 4.5 UV 方向

现有 `ImageTextureLoader` 固定 `stbi_set_flip_vertically_on_load(1)`，即纹理内存首行对应图片底部，符合 OBJ 的 v 向上约定。

glTF 的 UV 原点在左上角（v 向下）。统一策略：

**导入 glTF 时做 `v = 1 - v`，保留现有图片翻转。**

原因：

- 采样到的纹素与 glTF 原始语义一致；
- `compute_tangents()` 从翻转后的 UV 重算切线/副切线，bitangent 方向仍与 glTF 法线贴图的绿色通道约定一致；
- 不需要为 glTF 单独维护一套“不翻转”的贴图缓存。

必须用上下不对称的贴图做一次视觉验证，避免贴图上下颠倒或法线 Y 反向。

### 4.6 颜色空间

`RenderingServer` 上传材质时会调用 `material->albedo.linear(vec4)`，即把 `Color` 字节值当作 sRGB 再转线性。而 glTF 的 `baseColorFactor` 和 `emissiveFactor` 是**线性值**。

如果直接把 `0.5` 存成字节 128，shader 收到的是 `srgb_to_linear(0.5) ≈ 0.216`，颜色明显偏暗。正确做法是存之前做 **linear → sRGB 编码**：

```cpp
uint8_t to_srgb_byte(float linear)
{
    const float s = (linear <= 0.0031308f)
        ? linear * 12.92f
        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(s, 0.0f, 1.0f) * 255.0f + 0.5f);
}
```

贴图侧：

- baseColor / emissive：`srgb = true`，由 GPU 的 `GL_SRGB8` 自动解码；
- normal / metallicRoughness / occlusion：`srgb = false`。

### 4.7 不支持项处理

| 特性 | 策略 |
|---|---|
| 相机、灯光 | 忽略，保留 `Node3D` 变换占位 |
| 动画 | 忽略，使用节点静态 TRS |
| skin / morph target | 忽略，按 bind pose / 基础网格渲染，警告 |
| `TEXCOORD_1+`、`KHR_texture_transform` | 只支持 UV0，跳过该贴图并警告 |
| `KHR_draco_mesh_compression`、`EXT_meshopt_compression` | 检测到就报错/跳过该 primitive |
| KTX2 / basisu | stb 不支持，警告 |
| `COLOR_0` 顶点色 | 忽略 |
| 透明、alpha mask | 不支持，警告 |
| emissive texture | 本阶段忽略；emissiveFactor 先存入 `emission` |

---

## 5. 分阶段实施

### 阶段 0：前置修复（约 1.5–2 天）

> 状态：**已完成**。0.1–0.5 均已落地并有单元测试覆盖（见 `tests/core/io/test_file_access_and_path.cpp`、
> `tests/resource/test_image_texture_loader.cpp`、`tests/resource/test_mesh.cpp`、
> `tests/scene/test_node3d_transform.cpp`）。实施中发现 `Vector3::is_normalized()` 用精确比较，
> 会让 `Basis::set_axis_angle()` 静默忽略 `(1,1,1)/sqrt(3)` 这类浮点单位轴，已一并改为近似比较。

这些不改，glTF 即使解析出来也会出现变换错误、重复面、贴图方向错误。

#### 0.1 二进制文件读取

文件：`core/io/file_access.h/.cpp`

新增：

```cpp
size_t get_length() const;
std::vector<uint8_t> get_buffer() const;
static bool read_file_bytes(const std::string &path, std::vector<uint8_t> &out);
```

用于 `.glb`、外部 `.bin`、GLB 内嵌图片。

测试：写入一个含 `\0` 的临时文件，读回字节完全一致。

#### 0.2 URI 解码

文件：`core/io/path.h/.cpp`

新增：

```cpp
static std::string decode_uri(const std::string &uri);
```

处理 `%20` 等百分号编码；`data:` URI 在 loader 内单独处理。

测试：`a%20b.png` → `a b.png`。

#### 0.3 贴图采样参数与内存解码

文件：`resource/texture/image_texture.h`、`resource/loaders/image_texture_loader.h/.cpp`

新增：

```cpp
// ImageTexture
void set_repeat(bool);
void set_linear_filter(bool);
void set_gen_mipmap(bool);

// ImageTextureLoader
static Ref<ImageTexture> load_from_memory(
    const uint8_t *data, size_t size,
    bool srgb, const char *debug_name);
```

`load(url)` 改成“读文件 + 调用 `load_from_memory`”，保持现有翻转行为。

测试：用 1x1 PNG 字节数组解码，检查宽高、通道数、sRGB 标志。

#### 0.4 Node3D 任意变换

文件：`core/math/basis.h/.cpp`、`core/math/quaternion.h`、`scene/3d/node3d.h/.cpp`

新增：

```cpp
// Basis
Vector3 get_scale() const;
real_t determinant() const;
Quaternion get_rotation_quaternion() const;

// Quaternion
Quaternion(const Basis &basis); // 或静态构造

// Node3D
void set_transform(const Transform3D &p_transform);
Transform3D get_transform() const; // 局部变换
```

测试：

- TRS 分解后 `get_global_transform().is_equal_approx(original)`；
- 负缩放；
- 带旋转的父节点下设置子节点变换。

> 实现补充：`set_transform()` 直接保存局部矩阵，并把 `position/rotation/scale` 反解为缓存值；
> 负缩放无法唯一确定镜像轴，约定镜像归到 X 轴（`scale.x` 取负）。`Basis::scale()` 仍保持
> “忽略非正缩放因子”的既有语义，负缩放只通过 `set_transform()` 的矩阵路径支持。

#### 0.5 修复共享 Mesh 重复上传

文件：`scene/3d/mesh_instance_3d.cpp`、`resource/mesh/mesh.h`

现状：`ensure_surfaces()` 每次都无条件 `add_mesh_surface()`。

改法：

1. 把“材质/贴图上传”和“surface 上传”拆开；
2. 只有 `mesh->is_rid_dirty()` 时才 `add_mesh_surface()`；
3. 上传完成后清除 dirty。

```cpp
void MeshInstance3D::upload_if_needed()
{
    RenderingServer *server = RenderingServer::get_singleton();

    if (mesh->get_rid() == 0)
        mesh->set_rid(server->create_mesh());

    // 材质/贴图是幂等的，每次进树都可以确保一次。
    for (const Mesh::Surface &surface : mesh->get_surfaces())
        ensure_material(surface.material);

    if (mesh->is_rid_dirty())
    {
        ensure_surfaces();      // 只负责 add_mesh_surface
        mesh->mark_rid_clean(); // 新增方法，替代 set_rid(mesh->get_rid())
    }

    server->set_instance_mesh(get_instance(), mesh->get_rid());
}
```

`Mesh` 增加：

```cpp
void mark_rid_clean() { rid_dirty = false; }
```

验证：一个 Mesh 被多个 MeshInstance3D 引用时，RenderingServer 侧只产生一组 surface。

### 阶段 1：解析层骨架（约 3–4 天）

> 状态：**已完成**（1.1–1.5）。实现细节：
> - cgltf 只提供 `thirdparty/cgltf/cgltf.h`，以 INTERFACE target 引入；
>   `#define CGLTF_IMPLEMENTATION` 放在 `resource/loaders/gltf_loader.cpp`，
>   没有单独的 `cgltf_impl.cpp`（不要在其它翻译单元再定义一次，否则符号重复）。
> - 外部 `.bin` 的读取走 `FileAccess` + `Path::decode_uri`，通过 cgltf 的 file callback 接入；
>   `cgltf_load_buffers()` 的 `gltf_path` 传 `Path::replace_url(url)` 的绝对路径。
> - 单元测试：`tests/resource/test_scene.cpp`（`PackedScene`）、
>   `tests/resource/test_gltf_loader.cpp`（.gltf/.glb、节点层级、多 primitive、外部 bin、内嵌/data URI 图片）。

#### 1.1 引入 cgltf

文件：

```text
thirdparty/cgltf/cgltf.h          # 单头文件，保留 MIT 许可
thirdparty/cgltf/cgltf_impl.cpp   # #define CGLTF_IMPLEMENTATION + include
```

`thirdparty/CMakeLists.txt`：

```cmake
add_library(cgltf STATIC cgltf/cgltf_impl.cpp)
target_include_directories(cgltf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
```

`resource/CMakeLists.txt` 链接 `cgltf`。

注意：cgltf 是 C99 代码，作为 C++ 编译通常可行；若 MinGW 下报错，则在顶层 CMake 打开 C 语言并用 `.c` 编译，或在 include 外增加 `extern "C"`。

#### 1.2 通用 Scene 资源

文件：`resource/scene/scene.h/.cpp`

实现（结构见 3.3 节）：

- `get_rid()` 返回 0；
- `duplicate()` 复制 `nodes/roots/scene_name`，`Ref<Mesh>` 共享；
- `add_node` / `add_root` / `validate` 供 loader 构建与校验；
- 只读访问器供 scene 层实例化；
- 不包含任何 glTF 字段。

#### 1.3 GltfLoader 解析流程

文件：`resource/loaders/gltf_loader.h/.cpp`

```cpp
Ref<Resource> GltfLoader::load(const std::string url)
{
    std::vector<uint8_t> bytes;
    if (!FileAccess::read_file_bytes(url, bytes))
        return Ref<Resource>(nullptr);

    cgltf_options options = {};
    // 外部 buffer 的读取回调必须走 FileAccess/Path，不能直接 fopen，
    // 否则 res:// 与百分号编码会失效。
    options.file.read = &gltf_file_read;
    options.file.release = &gltf_file_release;
    options.file.user_data = &file_context;

    cgltf_data *data = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success)
    {
        std::printf("[ERROR] failed to parse glTF: %s\n", url.c_str());
        return Ref<Resource>(nullptr);
    }

    // 外部 .bin；GLB 的内嵌 BIN 由 cgltf 处理。
    if (cgltf_load_buffers(&options, data, Path::replace_url(url).c_str())
        != cgltf_result_success)
    {
        std::printf("[ERROR] failed to load glTF buffers: %s\n", url.c_str());
        cgltf_free(data);
        return Ref<Resource>(nullptr);
    }

    Scene *scene = memnew(Scene);
    // 1) 材质/贴图
    // 2) Mesh / Surface
    // 3) 节点 / 根集合 -> Scene（多 scene 只取默认 scene）
    cgltf_free(data);
    return Ref<Resource>(scene);
}
```

校验：

- `data->asset.version` 以 `"2."` 开头；
- `data->required_extensions` 中出现不支持项时报错；
- `data->meshes/nodes/scenes` 为空时给出警告，返回空 Ref。

#### 1.4 节点与场景构建

- 节点局部变换优先用 `cgltf_node_transform_local()` 生成列主序矩阵；
- 直接填 `Transform3D`：

```cpp
Transform3D transform_from_gltf(const cgltf_float m[16])
{
    Transform3D t;
    t.basis.columns[0] = Vector3(m[0], m[1], m[2]);
    t.basis.columns[1] = Vector3(m[4], m[5], m[6]);
    t.basis.columns[2] = Vector3(m[8], m[9], m[10]);
    t.origin = Vector3(m[12], m[13], m[14]);
    return t;
}
```

- `node->parent` 用于确定 `Scene::Node::parent` 索引；`node->children` 填 `children`；
- `node->mesh` 非空时 `kind = MESH_INSTANCE_3D` 并填入 `Ref<Mesh>`，否则 `NODE_3D`；
- 场景根：`scene->nodes[]` 填入 `Scene::roots`；无 scene 时使用无父节点的节点集合；
- 选择哪个 glTF scene（`data->scene` > 第 0 个 > 无父节点集合）是 **loader 内部逻辑**，
  `Scene` 不保存 `default_scene`/`scene_index`。

#### 1.5 注册与基础测试

`resource/resource_loader.cpp`：

```cpp
singleton->register_loader(memnew(GltfLoader));
```

测试：

- 最小 `.gltf` 三角形：`Scene` 节点 / 根 / `Mesh` 的顶点/索引/法线/UV；
- `ResourceLoader` 按 URL 缓存同一 `Scene`；
- 扩展名注册为大写 `.GLTF` 也能命中（`ResourceLoader` 会小写化）。

### 阶段 2：材质与贴图（约 2 天）

> 状态：**已完成**（2.1–2.4）。实现说明：
> - 外部图片不经过 `ResourceLoader`，而是直接读字节后调用
>   `ImageTextureLoader::load_from_memory`，保证同一张图在不同槽位（sRGB / 线性）与
>   sampler 设置下互不污染；缓存粒度为 `(texture index, srgb)`，只在单次加载内有效。
> - `metallicRoughnessTexture` 从已解码的 `ImageTexture` 像素中拆出 G/B，生成两张单通道
>   贴图（`channels = 1`、`srgb = false`），并复制源贴图的 repeat/linear/mipmap 设置。
> - sampler 缺省值按 glTF 规范：`wrap = REPEAT`、`mag = LINEAR`、`min = LINEAR`（不生成 mipmap）。
> - 必需扩展采用“白名单”策略：材质/灯光类扩展（含 `KHR_texture_transform`）只警告并降级，
>   `KHR_draco_mesh_compression`、`EXT_meshopt_compression`、`KHR_texture_basisu` 等直接报错返回空 Ref。

#### 2.1 图片加载

实现 `GltfLoader::load_image(const cgltf_image*)`：

```text
if image->buffer_view:
    数据 = cgltf_buffer_view_data(...)，大小 = view->size
    -> ImageTextureLoader::load_from_memory(..., srgb, debug_name)
else if image->uri 以 "data:" 开头:
    base64 解码 -> load_from_memory
else:
    texture_url = Path::join(Path::get_base_dir(gltf_url),
                             Path::decode_uri(image->uri))
    ResourceLoader::load(texture_url) -> ImageTexture
    若 srgb 不一致则 duplicate() 后 set_srgb
```

#### 2.2 材质构建

```text
material = memnew(StandardMaterial)
baseColorFactor -> Color（linear -> sRGB 编码）
metallicFactor / roughnessFactor -> set_metallic / set_roughness
baseColorTexture -> slot 0, srgb=true
normalTexture -> slot 1, srgb=false, normal_scale
metallicRoughnessTexture -> 拆分为 slot 2 (G) 与 slot 3 (B), srgb=false
occlusionTexture -> slot 4 (R), srgb=false, ao_strength
emissiveFactor -> set_emission（shader 暂未使用）
```

缺失字段使用 glTF 默认值：baseColor = 1,1,1,1；metallic = 1；roughness = 1；normal scale = 1；occlusion strength = 1。

#### 2.3 MR 贴图拆分

```cpp
// 输入：解码后的 RGBA/RGB 像素，宽高
// 输出：1 通道 roughness（G）与 1 通道 metallic（B）
std::vector<uint8_t> roughness(width * height);
std::vector<uint8_t> metallic(width * height);
for (size_t i = 0; i < width * height; ++i)
{
    roughness[i] = src[i * channels + 1]; // G
    metallic[i]  = src[i * channels + 2]; // B
}
```

AO 若引用同一张图，可复用一次解码结果，取 R 通道生成 1 通道 AO 图。

注意：如果源图是 3 通道，G/B 索引分别为 1/2；如果是 4 通道，同样。

#### 2.4 因子颜色转换

```cpp
Color color_from_linear(const cgltf_float rgba[4])
{
    return Color(
        to_srgb_byte(rgba[0]),
        to_srgb_byte(rgba[1]),
        to_srgb_byte(rgba[2]),
        static_cast<uint8_t>(std::clamp(rgba[3], 0.0f, 1.0f) * 255.0f + 0.5f));
}
```

测试：`baseColorFactor = [0.5, 0.5, 0.5, 1]` → `Color` 字节约 188，`linear()` 回来约 0.5。

#### 2.5 测试

- 外部 PNG + 相对路径 + `%20`；
- GLB 内嵌 PNG；
- data URI PNG；
- MR 拆分通道值；
- normal scale / occlusion strength；
- 缺失贴图回退标量。

### 阶段 3：场景实例化与演示（约 0.5–1 天）

#### 3.1 通用实例化函数

文件：`scene/3d/scene_instantiate.h/.cpp`

实现 `instantiate_scene()` 与 `load_scene_instance()`，核心递归见 3.5 节。

要点：

- 先 `add_child`，再 `set_transform`；
- 合成根节点使用恒等变换，名字取 `Scene::scene_name`；
- 场景选择已在 loader 内完成，实例化函数不再接收 `scene_index`；
- `mesh` 为空的节点降级为 `Node3D` 并警告（见 3.5 节和 9 节风险）；
- 实例化失败返回 `nullptr` 并打印错误；
- 重复实例化共享 `Ref<Mesh>`，依赖阶段 0.5 的修复避免重复 surface。

#### 3.2 Node 名称（实例化的前置条件）

`instantiate_scene()` 会把 `Scene::Node::name` 写入节点，所以 `Node` 必须先具备名称接口。
给 `Node` 增加：

```cpp
const std::string &get_name() const;
void set_name(const std::string &);
```

glTF `node.name` 写入，方便调试和后续 `find_node`。合成根使用 `Scene::scene_name`（loader 取
glTF scene 名称，没有时用文件名）。

#### 3.3 演示

新增 `tests/application/test_gltf_application.cpp`（可选）：

- 加载 `res://scene.glb`；
- 复用现有相机、方向光、`World3D`；
- 按 `R` 键重新加载或打印节点树，便于验证。

资源不在仓库中，需要手动放到 `build/resources/`。

### 阶段 4：测试、文档、收尾（约 1.5–2 天）

1. 新测试文件加入 `tests/CMakeLists.txt`，并补回 `resource/test_obj_loader.cpp`；
2. 更新 `AGENTS.md`：
   - 技术栈加 cgltf；
   - 目录结构加 `resource/scene/`（通用 `Scene`）、`resource/loaders/gltf_loader.*`、
     `scene/3d/scene_instantiate.*`；
   - 状态表加通用场景资源、glTF 能力与限制；
   - 已知陷阱加 UV 翻转、打包 MR、共享 Mesh 上传、glTF 因子颜色空间；
   - 新增“场景类型统一”说明：`.gltf/.glb` 产出 `Scene`，实例化入口 `instantiate_scene`；
3. 本文档与 `docs/resource_module_roadmap.md` 互相引用；
4. 手动跑 Khronos 样例验证。

---

## 6. 文件清单

### 新增

```text
thirdparty/cgltf/cgltf.h
thirdparty/cgltf/cgltf_impl.cpp   # 实际未单独建文件，CGLTF_IMPLEMENTATION 定义在 gltf_loader.cpp
resource/scene/scene.h            # 实现类型名为 PackedScene
resource/scene/scene.cpp
resource/loaders/gltf_loader.h
resource/loaders/gltf_loader.cpp
scene/3d/scene_instantiate.h
scene/3d/scene_instantiate.cpp
tests/resource/test_scene.cpp
tests/resource/test_gltf_loader.cpp
tests/scene/test_scene_instantiate.cpp
tests/scene/test_node3d_transform.cpp
```

### 修改

```text
thirdparty/CMakeLists.txt          # cgltf 目标
resource/CMakeLists.txt            # scene/scene.cpp、loaders/gltf_loader.cpp
scene/CMakeLists.txt               # 3d/scene_instantiate.cpp
tests/CMakeLists.txt               # 新测试 + 补回 test_obj_loader
resource/resource_loader.cpp       # 注册 GltfLoader
resource/loaders/image_texture_loader.h/.cpp  # load_from_memory
resource/texture/image_texture.h   # sampler setter
core/io/file_access.h/.cpp         # 二进制读取
core/io/path.h/.cpp                # decode_uri
core/math/basis.h/.cpp             # get_scale / determinant / 四元数
core/math/quaternion.h             # Basis 构造
core/object/ref_counted.h          # cast_checked（见第 9 节风险）
scene/node.h/.cpp                  # set_name / get_name
scene/3d/node3d.h/.cpp             # set_transform / get_transform
scene/3d/mesh_instance_3d.cpp      # 共享 Mesh 上传保护
resource/mesh/mesh.h               # mark_rid_clean
docs/gltf_loader_plan.md           # 本文档（已合并通用 Scene 方案）
AGENTS.md
```

与原 `GltfDocument`/`GltfScene` 方案相比：`resource/gltf/gltf_document.*`、
`scene/3d/gltf_scene.*` 不再新增，替换为 `resource/scene/scene.*`、
`scene/3d/scene_instantiate.*`。

---

## 7. 测试计划

### 7.1 单元测试（不依赖窗口/GPU）

`tests/resource/test_scene.cpp`（通用 `Scene` 资源）：

1. `add_node` / `add_root` 后索引与 `children` 自洽；
2. `duplicate()` 复制节点数组与 `roots`，共享同一个 `Ref<Mesh>`；
3. `validate()` 能识别越界 `children`、`parent` 不一致；
4. `get_rid() == 0`。

`tests/resource/test_gltf_loader.cpp`（原用例保留，断言对象改为 `Scene`）：

1. 最小 `.gltf` 三角形：`Scene` 节点/根数、`Mesh` 的顶点/索引/法线/UV 数量正确；
2. 多 primitive + 多材质：Surface 数、材质对应正确；
3. 节点层级：TRS 与 matrix 两种写法；
4. 索引类型：u8/u16/u32、无索引自动生成、越界索引过滤；
5. 交错 buffer（`byteStride`）与 normalized 属性；
6. 外部 `.bin` + 相对 URI + `%20` 文件名；
7. data URI 图片 / GLB 内嵌图片；
8. 材质：
   - `baseColorFactor` 线性 → sRGB 字节；
   - `metallicFactor/roughnessFactor`；
   - MR 拆分 G/B；
   - normal scale / occlusion strength；
9. UV 翻转：glTF `(0,0)` 导入后为 `(0,1)`；
10. glTF 2.0 版本校验、未知必需扩展报错；
11. 多 scene 文件只保留默认 scene 并告警；
12. 加载 `.obj` 得到的资源 `cast_checked<Scene>()` 为空（防止误 cast）。

`tests/scene/test_scene_instantiate.cpp`：

1. 根节点/子节点数量、名称正确；
2. `MESH_INSTANCE_3D` → `MeshInstance3D`，`NODE_3D` → `Node3D`；
3. 父子全局变换正确（含带旋转的父节点）；
4. `mesh` 为空的节点退回 `Node3D` 且不崩溃；
5. 同一 `Scene` 实例化两次共享 `Ref<Mesh>`，且（配合阶段 0.5）不重复追加 surface；
6. 递归 `memdelete` 清理，避免影响 `Memory::get_allocation_count()`。

`tests/scene/test_node3d_transform.cpp`：

1. `set_transform` 后全局变换与输入一致；
2. 负缩放；
3. 带旋转的父节点下的子节点变换。

### 7.2 手动 GPU 验证

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
cd build
# 样例 glTF/GLB 与贴图放到 build/resources/
.\tests\orca-gltf-application-test.exe
```

重点观察：

- 模型朝向；
- 贴图上下方向；
- 法线贴图凹凸方向；
- metallic/roughness 是否左右颠倒；
- 多节点共享 mesh 是否重复面/过绘。

推荐样例：Khronos `BoxTextured`、`DamagedHelmet`、`Sponza`。

---

## 8. 验收标准

- [ ] `.gltf` 与 `.glb` 均可加载，外部/内嵌贴图正常；
- [ ] loader 输出通用 `Scene` 资源，且 `.gltf/.glb` 的解析结果不暴露任何格式专用类型；
- [ ] `instantiate_scene(Ref<Scene>)` 返回 `Node3D` 根节点，层级与 glTF 节点一致；
- [ ] 同一 `Scene` 类型可由任意场景格式产出，实例化入口与格式无关；
- [ ] 一个 glTF mesh 的多个 primitive 正确变成多个 Surface；
- [ ] 材质 baseColor / normal / metallic / roughness / AO 映射正确；
- [ ] 贴图方向、法线方向、金属/粗糙度通道正确；
- [ ] 共享 Mesh 不重复上传 surface；
- [ ] 现有 OBJ/MTL 测试与 demo 不回归；
- [ ] `ctest --test-dir build --output-on-failure` 通过；
- [ ] 文档与 `AGENTS.md` 更新；
- [ ] 不支持项有明确警告，不崩溃。

---

## 9. 风险与限制

| 风险 | 影响 | 对策 |
|---|---|---|
| UV v 方向 | 贴图上下颠倒、法线 Y 反向 | 导入时 `v=1-v`，用非对称贴图测试 |
| 线性/sRGB 重复转换 | 颜色偏暗 | 因子 linear→sRGB 编码，补数值测试 |
| MR 打包贴图 | 金属/粗糙度错误 | CPU 拆分 G/B，补通道测试 |
| 共享 Mesh 重复上传 | 重复面、过绘 | 阶段 0.5 修复 |
| 矩阵分解 | 负缩放/万向锁偏差 | 行列式处理 + 分解测试 |
| 透明/自发光/多 UV | 部分模型效果不完整 | 明确警告，文档写清 |
| Draco/meshopt/KTX2 | 模型无法加载 | 检测必需扩展，明确报错 |
| 节点树无自动释放 | 内存泄漏 | 文档说明；测试中手动递归释放 |
| `Ref::cast` 是 `static_cast` | 把 `Mesh` 误 cast 成 `Scene` 是 UB | 新增 `Ref::cast_checked`（`dynamic_cast`），实例化入口只用它 |
| `Scene` 中 mesh 为空的节点 | `MeshInstance3D::ENTER_TREE` 直接解引用空 `Ref` 会崩溃 | 实例化时降级为 `Node3D` 并警告 |
| 多 scene 只保留默认 scene | 其它 scene 丢失 | loader 打印警告；需要时用 `GltfLoader::load_scene(url, index)` 专用入口 |
| `Scene` 命名歧义 | 与 `SceneTree::current_scene`、渲染 scene RID 混淆 | 类注释写清“CPU 资源”；如仍担心可改名 `PackedScene`/`SceneResource` |
| `Scene::duplicate()` 的共享语义 | 调用方误以为贴图也被复制 | 注释写明“节点数组深拷贝、Mesh/Material/Texture 共享”，并补测试 |

本期明确不支持：

- 相机、灯光、动画、skin、morph target；
- alpha blend / mask、双面控制、emissive 贴图（emissiveFactor 仅存值）；
- `TEXCOORD_1+`、`KHR_texture_transform`；
- Draco / meshopt / KTX2 / basisu；
- 多场景切换（只用默认 scene 或第一个 scene）。

---

## 10. 工作量估算

| 阶段 | 内容 | 预估 |
|---|---|---|
| 0 | 前置修复（Node3D、FileAccess、ImageTexture、共享 Mesh） | 1.5–2 天 |
| 1 | cgltf + 通用 `Scene` 资源 + `GltfLoader` 骨架 | 3–4 天 |
| 2 | 材质/贴图/sampler/MR 拆分 | 2 天 |
| 3 | `instantiate_scene` + 演示 | 0.5–1 天 |
| 4 | 测试、文档、AGENTS 更新 | 1.5–2 天 |
| **合计** | | **约 8.5–11 人天** |

阶段 1 相比 `GltfDocument` 方案少一个格式专用数据类，阶段 3 的通用函数比 `GltfScene` 工具类更简单；
测试量略有增加（多一组通用 `Scene` 用例，少一组 `GltfScene` 用例）。

---

## 附录 A：与 Assimp 的取舍

Assimp 的调用点更简单（一次 `ReadFile`），但对当前需求总体更重：

| 维度 | cgltf | Assimp |
|---|---|---|
| 依赖形态 | 单头文件 + 一个 impl | 完整 C++ 库 + zlib |
| 构建接入 | 一个 target | 裁剪 importer、处理 zlib、MinGW 编译较久 |
| glTF 材质保真 | 直接读规范字段 | 通用 `aiMaterial` + 版本相关 key，normal scale / occlusion strength 可能丢失 |
| sampler | 完整 | wrap 可以，filter/mipmap 不稳定 |
| 多 primitive | glTF mesh 直接映射 Surface | aiNode 引用多个 aiMesh，需要额外分组 |
| 节点树 | 直接映射 | 需注意行/列主序、骨骼节点 |
| 多格式/动画 | 不支持 | 支持 |

结论：

- 当前只做 glTF 模型/材质/贴图 → **cgltf 更简单、风险更低**；
- 若近期要统一 FBX/DAE/OBJ 或导入动画/skin → 再评估 Assimp；
- 无论选哪个，loader 的输出类型都是通用 `Scene`，实例化入口都是 `instantiate_scene()`，
  只替换 loader 内部实现即可。

---

## 附录 B：参考资料

- glTF 2.0 规范：https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html
- cgltf：https://github.com/jkuhlmann/cgltf
- Khronos glTF Sample Models：https://github.com/KhronosGroup/glTF-Sample-Models
- 现有资源路线图：`docs/resource_module_roadmap.md`
- 现有材质方案：`docs/material_texture_plan.md`

---

## 实施检查清单

按顺序勾选，避免漏改：

- [x] `FileAccess` 支持二进制读取
- [x] `Path::decode_uri`
- [x] `ImageTextureLoader::load_from_memory`
- [x] `ImageTexture` sampler setter
- [x] `Basis::get_scale/determinant/四元数`
- [x] `Node3D::set_transform/get_transform`
- [x] `Vector3::is_normalized` 改为近似比较（阶段 0 实施时发现的额外前置问题）
- [ ] `Node::set_name/get_name`
- [ ] `Ref::cast_checked`（可选，用于安全向下转换）
- [x] 共享 Mesh 上传修复 + `Mesh::mark_rid_clean`
- [x] 引入 cgltf 并编译通过
- [x] 通用 `Scene` 资源（`resource/scene/scene.*`，实现类型名为 `PackedScene`）
- [x] `GltfLoader` 节点/网格/材质/贴图 → `PackedScene`（多 scene 只取默认 scene）
- [x] 注册 `.gltf/.glb`
- [ ] `instantiate_scene` / `load_scene_instance`
- [x] 单元测试通过（`test_scene.cpp` / `test_gltf_loader.cpp`）
- [ ] 手动 GPU 验证（目前只用 Sponza 做过 CPU 加载冒烟）
- [x] 更新 `AGENTS.md`
- [x] 更新 CMake 源文件列表
