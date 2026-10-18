# 资源模块（Resource）实现路线图

> 目标：在渲染服务器（RenderingServer）之前，先实现一套可独立工作的资源加载模块，
> 使后续调试可以直接使用 Mesh / Material / Environment 等资源。
>
> 核心原则：**资源模块不依赖渲染服务器**。资源先持有 CPU 数据，RID 由服务器在后续阶段回填。
> 链接方向始终是 `server -> resource`，而不是 `resource -> server`。

---

## 0. 设计原则

- `orca-resource` 只依赖 `orca-object` / `orca-core` / `orca-math`，**绝不链接 `orca-rendering`**。
- `Resource` 基类不绑定任何图形 API；只有需要 GPU 支持的子类才持有 `RID`。
- 前期不做通用反射（`Variant` + `ClassDB`），改用**格式专用 loader** 直接构造具体类。
- 缓存使用**弱引用**：资源引用计数归零被析构时，自动从缓存摘除，缓存不延长资源寿命。

---

## 1. 目录组织

```
core/io/
    file_access.h/.cpp          # 读文件抽象（含 res:// 解析）
    path.h/.cpp                 # 路径拼接 / 扩展名 / res:// -> 绝对路径
core/string/...                 # 可选；前期直接用 std::string

resource/
    resource.h/.cpp             # Resource 基类（path / name / emit_changed / duplicate）
    resource_registry.h/.cpp    # 迷你 ClassDB：类名 -> 工厂（可选，后置）
    resource_cache.h/.cpp       # path -> Resource*（弱引用）
    resource_loader.h/.cpp      # 单例：load() / exists() + 已注册 loader 列表
    resource_saver.h/.cpp       # 后做
    format/
        resource_format_loader.h    # 抽象基类
        resource_format_saver.h     # 后做
        obj_mesh_loader.h/.cpp      # .obj -> ArrayMesh（最实用的第一个）
        text_resource_loader.h/.cpp # 自定义 .tres 风格（材料 / 环境）
        binary_resource_loader ...  # 后做
    mesh/
        mesh.h/.cpp                 # 数据 + 可选 RID
        array_mesh.h/.cpp           # 来自顶点数组
        primitive_mesh.h/.cpp       # Box / Sphere / Plane 程序化生成（调试必备）
    material/
        material.h/.cpp
        standard_material_3d.h/.cpp
    environment/
        environment_3d.h/.cpp
```

---

## 2. 分阶段实现步骤

### 步骤 1：`Resource` 基类 + `Ref<T>` 补丁

**大致要做的事**

- 让 `RefCounted` 继承 `Object`，与 Godot 对齐（`Object -> RefCounted -> Resource`）。
- 实现 `Resource`：
  - `path` / `name`（前期用 `std::string`）。
  - `emit_changed()` + `connect_changed()`：数据变更通知回调列表。
  - `virtual RID get_rid()`：默认无效，服务器型子类覆写。
  - `virtual Ref<Resource> duplicate()`：深拷贝。
  - 析构函数调用 `ResourceCache::remove(path)`，防止缓存悬空。
- 给 `Ref<T>` 增加**派生类 → 基类**的转换构造函数：
  ```cpp
  template <typename U, typename = std::enable_if_t<std::is_base_of_v<T, U>>>
  Ref(const Ref<U> &other) : reference(other.ptr()) {
      if (reference) static_cast<RefCounted*>(reference)->ref();
  }
  ```
- 提供一个安全向下转换 helper（`Object::cast_to<T>()` 或 `Ref::cast_to<T>()`）。

**交付物 / 验证**

- `resource/resource.h/.cpp` 编译通过。
- 单元测试：`Ref<Derived>` 能赋给 `Ref<Base>`；`emit_changed` 回调被触发；析构后 `Ref` 计数正确。

---

### 步骤 2：`Path` / `FileAccess`

**大致要做的事**

- `Path`：
  - `get_extension(path)`、`get_basename(path)`、`join(a, b)`、`is_absolute(path)`。
  - `res://` 解析：映射到可执行文件旁的 `resources/` 目录。
  - （后置）`user://` 映射到用户数据目录。
- `FileAccess`：
  - 最小接口：`open(path, flags)`、`is_open()`、`get_length()`、`get_as_utf8_string()`、`get_buffer()`、`close()`。
  - 前期可用 `std::ifstream` 实现，但保持抽象，方便以后加打包 / 加密 / 内存文件。

**交付物 / 验证**

- `core/io/` 下新文件编译通过。
- 单元测试：路径拼接 / 扩展名提取；`res://` 相对路径能映射为绝对路径；读取一个临时文件内容正确。

---

### 步骤 3：`ResourceCache` + `ResourceLoader`

**大致要做的事**

- `ResourceCache`：
  - `std::unordered_map<std::string, Resource*>`（**非拥有**）。
  - `get_ref(path)` 返回 `Ref<Resource>`（会 `ref()`）；`add` / `remove`。
  - 单例生命周期：`create_singleton` / `destroy_singleton` / `get_singleton`。
- `ResourceLoader`：
  - 单例；持有已注册的 `ResourceFormatLoader*` 列表。
  - `add_format_loader()`、`load(path, type_hint, cache_mode)`、`exists(path)`。
  - `load` 流程：查缓存 → 按扩展名匹配 loader → `loader->load()` → `set_path` → 写缓存 → 返回。
  - `CacheMode`：`CACHE_MODE_REUSE`（默认）/ `CACHE_MODE_IGNORE`。
- `ResourceFormatLoader` 抽象基类：
  - `get_recognized_extensions()`、`handles_type(type)`、`load(path, type_hint)`。

**交付物 / 验证**

- 单元测试：
  - 同路径 `load` 两次返回同一实例（缓存命中）。
  - `CACHE_MODE_IGNORE` 返回新实例且不写缓存。
  - 资源被释放后缓存自动清除（弱引用语义）。
  - 找不到 loader 时返回无效 `Ref`。

---

### 步骤 4：`ObjMeshLoader`（`.obj -> ArrayMesh`）

**大致要做的事**

- 解析 OBJ 的 `v` / `vn` / `vt` / `f` 行，处理 `f v/vt/vn` 形式的索引。
- 处理负索引（相对索引）与三角面。
- 生成 `ArrayMesh`：顶点、法线、UV、索引。
- 注册到 `ResourceLoader`。

**交付物 / 验证**

- 加载一个已知的 `.obj`（如立方体）：
  - 顶点数 / 面数符合预期。
  - 第一个三角形坐标正确。
- 这是一个**可直接用于后续渲染调试**的资源。

---

### 步骤 5：`Mesh` 数据类 + `PrimitiveMesh`

**大致要做的事**

- `Mesh : Resource`：
  - `struct Surface { vertices, normals, uvs, indices }`。
  - `std::vector<Surface> surfaces`；`add_surface()`。
  - `RID rid`（服务器可用前保持无效）+ `rid_dirty` 标记。
  - `get_rid()`、`is_rid_dirty()`、`set_rid()`。
- `ArrayMesh`：从 `Surface` 构造。
- `PrimitiveMesh`：`BoxMesh`、`SphereMesh`、`PlaneMesh` 等程序化生成。
- `~Mesh()`：有 `rid` 时由服务器释放（现阶段为空实现或断言）。

**交付物 / 验证**

- 不依赖文件也能造出 mesh（`PrimitiveMesh`），方便测试与后续渲染。
- `add_surface` 后 `rid_dirty == true`，`set_rid` 后为 `false`。

---

### 步骤 6：`TextResourceLoader`（材料 / 环境）

**大致要做的事**

- 定义简单文本格式（类似 `.tres`，但无需 Variant）：
  ```
  [resource type="StandardMaterial3D"]
  albedo=1,0,0,1
  metallic=0.2
  roughness=0.8
  ```
- loader 读取 `type` 后**直接构造具体类**并逐字段赋值（写死映射）。
- 先支持 `StandardMaterial3D` 与 `Environment3D` 的常用字段。

**交付物 / 验证**

- 加载材料文件后属性值正确。
- 未知 `type` / 未知字段有明确错误信息。

---

### 步骤 7：接渲染服务器（后续）

**大致要做的事**

- `RenderingServer::sync()` 遍历脏资源：
  - `Mesh`：无 RID 则 `mesh_create()`，上传 surface，`set_rid()`。
  - `Material` / `Environment3D`：同理。
- `MeshInstance3D`：
  - `_notification(ENTER_TREE)` 时 `set_instance_mesh(instance, mesh->get_rid())`。
  - `mesh->connect_changed(...)`，资源热更新时重新上传。

**交付物 / 验证**

- 资源 RID 能正确创建 / 上传 / 更新 / 释放。
- 修改 mesh 数据后，下一帧渲染结果更新。

---

### 步骤 8：可选增强

- `ResourceSaver` + `ResourceFormatSaver`：保存资源到 `.tres` / binary。
- 二进制资源格式（`.res` 风格）。
- `Variant` + `ResourceRegistry`（迷你 ClassDB）+ 通用 `.tres` 反序列化。
- `res://` 接入 `ProjectSettings`。
- `ResourceUID`：路径无关的稳定 ID。

---

## 3. 当前代码的待改点（对照）

| 位置 | 现状 | 建议 |
|---|---|---|
| `core/object/ref_counted.h` | `RefCounted` 不继承 `Object` | 改为 `RefCounted : public Object` |
| `core/object/ref_counted.h` | `Ref<T>` 无派生→基类转换 | 增加 `enable_if` 转换构造函数 |
| `resource/resource.h` | 空壳 `Resource : RefCounted` | 补 `path` / `name` / `emit_changed` / `get_rid` / `duplicate` |
| `resource/mesh/mesh.h` | 只有 `RID`，无人创建 | 改为 CPU-first：先存数据，RID 后填 |
| `resource/material/material.h` | 同上 | 先存属性，RID 后填 |
| `resource/environment/environment_3d.h` | 同上 | 先存属性，RID 后填 |
| `scene/3d/mesh_instance_3d.h` | 已存 `Ref<Mesh>` | 进树时 `set_instance_mesh` + `connect_changed` |
| `resource/CMakeLists.txt` | 仅少量文件 | 补 `format/`、`io/` 新文件；**不要链接 `orca-rendering`** |

---

## 4. 测试策略

- 路径 / 文件读取：临时文件读写、`res://` 映射。
- 缓存语义：命中、忽略、析构自动移除。
- loader：OBJ 解析正确性；文本格式字段映射。
- 内存：结合 `core/os/memory.h` 的 `memnew` / `memdelete` 验证无泄漏。
- 每个步骤都应能在**没有窗口 / 没有渲染服务器**的情况下跑通单元测试。

---

## 5. 一句话总结

`Resource` 基类 + `Path/FileAccess` + `ResourceCache/ResourceLoader` + `ResourceFormatLoader`
构成骨架；`Mesh` 等资源在渲染服务器出现前只持有 CPU 数据；渲染服务器落地后反过来消费资源并回填 `RID`。
