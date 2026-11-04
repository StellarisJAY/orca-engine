# OBJ 加载器：现状分析、性能瓶颈与第三方导入器移植可行性

> 分析对象：`resource/loaders/obj_loader.cpp`（343 行）、`resource/loaders/mtl_loader.cpp`（340 行）、
> `resource/mesh/mesh.cpp`（110 行），以及它们下游的 `MeshInstance3D` → `RenderingServer` → `GLRenderingDevice` 上传链路。
>
> 第三方候选：`D:\dev\other\blender\source\blender\io\wavefront_obj\importer`（Blender 5.x，13 个文件 / ~3372 行）。
>
> 本报告只做分析，没有修改任何引擎源码。

---

## 0. 摘要

### 0.1 一句话结论

**当前加载器的问题重心是"语义不完整 + 结构不合理"，而不是"词法解析慢"。** 性能确实有约 **6 倍**的
可挖空间（实测吞吐仅 10 MB/s，见 §2.2），但即使把解析速度做到 Blender 的水平，也不能解决 `o`/`g` 丢失、
平滑组丢失、凹多边形穿帮、法线全零这些会直接体现在画面上的缺陷。因此建议：

1. **不建议全量移植 Blender 导入器**——GPL-2.0-or-later 会污染整个引擎，且 Blender 的 Mesh 表达（per-corner
   属性 + n-gon + sharp_face 延迟求法线）与引擎的"单一索引数组 + per-vertex 属性 + 必须三角化"存在 5 处硬冲突，
   移植等价于重写而不是搬运（保守 25~40 人日）。
2. **建议自研重构，但按 Blender 的算法选择来做**：零拷贝行扫描 + `std::from_chars` + 扁平 corner 数组 +
   打包 key 的开放寻址去重 + 耳切三角化。（约 13 人日到完整形态，其中 1 人日就能拿到主要性能收益。）
3. **如果只想"别再那么慢"**：只改读取与数字解析两处（< 150 行），收益最大、风险最低。
4. **长期正解**是引入引擎自有二进制网格格式，把 OBJ 解析降级为"离线导入工具"的事情。

### 0.2 关键发现速览

| 维度 | 现状 | 严重度 |
|---|---|---|
| 面/对象切分 | `o` / `g` 被完全忽略，整个文件塌缩成一个 `Mesh`，只能按材质拆 surface | **高**（场景级 OBJ 无法使用） |
| 平滑组 | `s` 被忽略；只要文件里出现任何一个 `vn`，全模型就不算法线 | **高**（画面错误） |
| 法线回退 | `has_normals` 是**全局**标志；出现部分 `vn` 的文件会得到零法线顶点 | **高**（黑面/光照错乱） |
| 三角化 | 扇形三角化，凹多边形穿帮 | 中（多数资产是三角/四边形） |
| 切线 | 先归一化再加权（权重错误），未做施密特正交化 | 中（法线贴图细节异常） |
| 解析性能 | 吞吐仅 **10 MB/s**；`>> float` 独占 78%~91% 的时间；全文件被拷贝 2 次 | **高**（实测见 §2.2） |
| 去重数据结构 | `unordered_map<tuple<int,int,int>>`，节点式、无 reserve | **高** |
| 资源缓存 | `ResourceCache` 存在但未接入，重复 `load()` 会重复解析 | 中 |
| 上传链路 | 每个 `MeshInstance3D` 都重新上传一次同一个 mesh（重复 VRAM） | **高**（多实例场景） |
| 测试 | `tests/resource/test_obj_loader.cpp` 已过期且未加入 CMake | 中 |

---

## 1. 现状解剖

### 1.1 调用链

```text
ResourceLoader::load(url)                       resource/resource_loader.cpp:33
  └─ ObjMeshLoader::load(url)                   resource/loaders/obj_loader.cpp:141
       ├─ FileAccess::get_file_as_text(url)     core/io/file_access.cpp:32  （整文件读入 std::string）
       ├─ std::istringstream file(text)         obj_loader.cpp:200          （第二次整文件拷贝）
       ├─ 逐行 std::istringstream stream(line)   obj_loader.cpp:207          （每行一次拷贝 + locale 初始化）
       ├─ mtllib → ResourceLoader::load(.mtl)   obj_loader.cpp:177          （→ MtlLoader，同样是 istringstream）
       ├─ f 行 → SurfaceBuilder::vertex_map     obj_loader.cpp:295          （tuple key 的 unordered_map 去重）
       ├─ 扇形三角化                            obj_loader.cpp:290
       ├─ compute_vertex_normals()              obj_loader.cpp:113          （仅当全局 has_normals == false）
       └─ mesh->compute_tangents()              resource/mesh/mesh.cpp:74  （无条件执行）
```

资源侧到 GPU 侧：

```text
MeshInstance3D::_notification(NOTIFICATION_ENTER_TREE)   scene/3d/mesh_instance_3d.h:32
  └─ ensure_surfaces()                                   scene/3d/mesh_instance_3d.cpp:3
       └─ RenderingServer::add_mesh_surface(...)         servers/rendering/rendering_server.cpp:142
            └─ GLRenderingDevice::create_mesh_surface()  servers/rendering/opengl/gl_rendering_device.cpp:76
                 └─ 打包成 14 float/vertex 的交错缓冲 → glBufferData
```

### 1.2 数据模型（重构必须保持的契约）

`Mesh::Surface`（`resource/mesh/mesh.h:20-36`）是 SoA：

```cpp
std::vector<Vector3> vertices, normals, tangents, bitangents;
std::vector<Vector2> uvs;
std::vector<uint32_t> indices;
Ref<StandardMaterial> material;   // 一个 surface 一个材质
```

四个硬契约：

1. **`orca-resource` 不能依赖 `orca-rendering`**（AGENTS.md §2.1）。加载器只能产出 CPU 数据 + 可回填 `RID`。
2. **上传发生在节点进树时**，不在 `RenderingServer::sync()` 里（`sync()` 是空实现）。
3. **GL 顶点格式固定为 14 个 float**：position/normal/uv/tangent/bitangent，且
   `GLRenderingDevice::create_mesh_surface` **会校验这 5 个数组长度必须完全相等**，否则直接报错返回 0
   （`gl_rendering_device.cpp:85-93`）。所以"没有切线就别传"是行不通的，加载器必须始终产出 tangents/bitangents。
4. **纹理槽顺序是跨层硬编码契约**（`MaterialTextureSlot` ↔ `RenderingServer::TextureSlot*` ↔ shader 的
   texture unit 0..5），改动槽位要同步 4 处。

### 1.3 代码地图（谁在消费 OBJ）

- 唯一真实消费者：`tests/application/test_application.cpp:74`（演示程序加载 `res://sphere.obj`）。
- `tests/resource/test_obj_loader.cpp` 调用的是 `surface.get_vertex_count()` / `get_index_count()` /
  `get_triangle_count()`，这些 API 在当前 `mesh.h` 中**已不存在**（现在是 `Mesh::get_vertex_count()` 等），
  且该文件没有加入 `tests/CMakeLists.txt`——**即整个 OBJ 路径目前没有任何可编译的测试兜底**。

这是一次难得的重构窗口：调用面小、无历史包袱、也没有测试要迁就。

---

## 2. 性能分析

### 2.1 瓶颈清单（按实测影响排序，数据见 2.2）

#### ① `operator>>` 解析浮点 —— **占总加载时间的 78%~91%**

```cpp
real_t parse_real(std::istringstream &stream) { real_t value = 0; stream >> value; return value; }  // obj_loader.cpp:65
```

这是本次测量最大的发现，也是与"直觉判断"最容易分歧的一点：

- **不是** `std::istringstream` 的**构造**慢（构造只占 0.2%~0.3%，因为它每行只做一次，而每行要解析多个数字）；
- **而是** 每次 `stream >> float` 慢：`num_get` facet 走通用路径——构造 sentry（检查流状态、跳过空白）、
  locale 检查、内部 `strtod`、错误掩码判断、结果写回。本机微基准中 `istringstream >> float` 约
  **6.0 µs/次**，而 `std::strtof` 是 **63.8 ns/次**、`sscanf("%f")` 是 **243.6 ns/次**。
  也就是说：**换掉 `>>` 就有约 90 倍的单点提升空间**（本机绝对值偏大，比值是可靠的）。
- 它还会把非法 token **静默解析成 0**（不检查 `stream.fail()`），是"错误数据静默通过"的来源之一。

对照：`v/vt/vn` 一共只占文件里一小部分字节，却吃掉了 80% 以上的时间——**典型的"用错工具"型瓶颈，
不是算法复杂度问题**。

#### ② 面 token 解析：`strtol` + 每 token 一个 `std::string`（obj_loader.cpp:270-287）

占 2%~7%。`while (stream >> token)` 每个 corner 构造一个 `std::string`（短串走 SSO 不分配，但仍有构造开销），
再用 `strtol` 解析 1~3 个整数。换成"指针扫描 + `std::from_chars`"可以省掉 token 字符串这一层。

#### ③ 每行一个 `std::istringstream`（obj_loader.cpp:207）

```cpp
while (std::getline(file, line)) {
    std::istringstream stream(line);   // ← 12.6 万行 = 12.6 万次构造/析构
```

仅占 0.2%~0.3%（因为它被每行的多个数字摊薄了），但它同时是**内存翻倍的元凶**（见 ④ 与 2.3）：
每行一份 stringbuf 副本，加上整文件本身已被拷贝两次。去掉它主要是为了内存和代码简洁，
单独看 CPU 收益不大。

#### ④ 整文件被拷贝两次（file_access.cpp:109-134 → obj_loader.cpp:200）

`get_file_as_text()` 里 `text.resize(length)` + `fread`（1 份），随后 `std::istringstream file(text)`
内部又拷贝一份（第 2 份）。一个 200 MB 的 OBJ 在"还没开始解析"时就已经常驻 400 MB。

#### ⑤ 面顶点去重：节点式 `unordered_map` + `std::tuple` key（obj_loader.cpp:25-42、295-309）

- key 是 `std::tuple<int,int,int>`，哈希要算 3 次 `std::hash<int>` 再混合，比较走 tuple 的逐元素比较；
- `std::unordered_map` 是**链地址法 + 每个元素单独 `new` 一个节点**，百万级顶点就是百万次小分配，
  缓存局部性极差；
- 整个 loader 里**没有任何 `reserve`**（`obj_loader.cpp:150-159`、`400`），桶数组会经历多次 rehash；
- `SurfaceBuilder` 里还额外存了一个 `std::string material_name`。

对照 Blender 的做法：把 `(v, vt, vn)` 打包进一个 `uint64_t`（各 21 bit 足够覆盖 OBJ 单文件规模），
用开放寻址表、按预期顶点数一次性开好桶数组——同一数量级的查找可以快 2–4 倍，且内存减半。

#### ⑥ 每个面一次 `std::vector<VertexKey> face` 分配（obj_loader.cpp:268）

每读到一个 `f` 行就构造一个临时 vector（典型 3~4 元素 → 1 次 malloc + 1 次 free）。
10 万个面就是 20 万次分配/释放。Blender 的做法是**扁平 `face_corners_` 数组 + `FaceElem{start_index, corner_count}`**，
全程零 per-face 分配。

#### ⑦ 其他次要项

| 位置 | 问题 | 量级 |
|---|---|---|
| obj_loader.cpp:337 | `add_surface(Surface&)` 按值 `push_back`，把 6 个 vector 整体再拷一遍 | 一次全量拷贝 |
| mesh.cpp:23-62 | 每个三角形算 2 次 `normalized()`（各含 sqrt），再做顶点级 normalize | 3N 次 sqrt |
| mesh.cpp:65-71 | 没有 UV 的模型也跑完整切线循环（`det == 0` 全部 continue） | 纯浪费 |
| mesh.cpp:74-80 | `compute_tangents()` 无条件对所有 surface 执行 | — |
| obj_loader.cpp:44-62 | `trim`/`unquote` 每 `usemtl`/`mtllib` 都产生新 string | 小 |
| mtl_loader.cpp:188-196 | MTL 同样每行一个 istringstream | 小（MTL 通常很短） |

#### ⑧ 加载链路之外，真正的放大器（scene/rendering 侧）

- **没有任何缓存**：`ResourceLoader::load()` 既不查也不写 `ResourceCache`（AGENTS.md §5.2 已记录）。
  同一个模型加载两次就解析两次。
- **每个实例重复上传**：`MeshInstance3D::_notification` 每次 `ENTER_TREE` 都调用 `ensure_surfaces()`；
  它只检查 `mesh->get_rid() == 0`，不检查 surfaces 是否已上传。**100 个共享同一 Mesh 的实例 = 100 份 VAO/VBO**，
  显存和上传时间都 ×100。这条比解析器本身的性能更值得优先修。
- **上传时再打包一次**：`GLRenderingDevice::create_mesh_surface` 把 5 个 SoA 数组重排成 14 float 交错缓冲
  （`gl_rendering_device.cpp:96-120`），又是一份全量临时拷贝。SoA 布局本身也让"每顶点 56 字节"变成
  5 个独立数组（两次 cache 往返）。

### 2.2 实测数据

**环境**：Windows + MinGW-w64 g++（`E:/tools/mingw64`），`CMAKE_BUILD_TYPE=Release`，每项 5 次重复取中位数。
测试数据由 `build/bench_obj/gen_obj.py` 合成（脚本、插桩源码、基准驱动都留在 `build/` 下，未进版本库）。
注意：工程默认构建类型是 **Debug**（AGENTS.md §3.2），所以另附 Debug 对照——实际体验比 Release 数字更差。

**表 2-1 加载性能（Release）**

| 文件 | 大小 | 三角形 | 唯一顶点 | surface | 加载 ms | 吞吐 MB/s | 面/秒 | 纯读取 ms | 峰值工作集 MB |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `flat_10k`（无 vt/vn/材质） | 0.30 MB | 10,082 | 5,184 | 1 | 32.6 | 9.1 | 309 k | 0.27 | 6.0 |
| `flat_100k` | 3.26 MB | 100,352 | 50,625 | 1 | **326.2** | **10.0** | 308 k | 2.18 | 19.3 |
| `flat_500k` | 17.4 MB | 500,000 | 251,001 | 1 | **1665** | **10.5** | 300 k | 12.1 | 88.3 |
| `smooth_100k`（带 vt/vn） | 7.78 MB | 100,352 | 50,625 | 1 | **778.7** | 10.0 | 129 k | 5.8 | 31.6 |
| `many_mtl_100k`（8 材质 + o/g/s 行） | 7.83 MB | 100,352 | 56,226 | 8 | 786.9 | 10.0 | 128 k | 5.1 | — |
| `flat_100k_crlf`（CRLF 换行） | 3.39 MB | 100,352 | 50,625 | 1 | 330.7 | 10.3 | 303 k | 2.07 | — |
| `flat_100k_comments`（含注释/空行） | 3.26 MB | 100,352 | 50,625 | 1 | 311.8 | 10.5 | 322 k | 1.80 | — |

**表 2-2 阶段占比**（插桩版 loader 副本，扣除了计时器自身开销）

| 阶段 | `flat_100k` | `flat_500k` | `smooth_100k` |
|---|---:|---:|---:|
| **`v/vt/vn` 数值解析（`>> float`）** | **80.9%** | **77.9%** | **90.5%** |
| 面 token 解析（`strtol`） | 6.4% | 6.5% | 2.7% |
| 去重表 + surface `push_back` | 3.5% | 6.5% | 1.1% |
| 未计入（循环/分派开销） | 7.2% | 7.1% | 4.4% |
| 文件读取 | 0.6% | 0.7% | 0.7% |
| 每行 `istringstream` 构造 | 0.2% | 0.3% | 0.3% |
| `add_surface` 整表拷贝 | 0.6% | 0.5% | 0.1% |
| `compute_vertex_normals` | 0.4% | 0.3% | 0% |
| `Mesh::compute_tangents` | 0.2% | 0.2% | 0.2% |

调用计数（`flat_500k`）：62.6 万行 → 62.6 万个 `istringstream`、150 万次去重表查找、25.1 万次插入、
75.3 万次浮点解析。

**表 2-3 微基准（Release，ns/次）**

| 操作 | ns/次 | 比值 |
|---|---:|---|
| `std::istringstream` 构造 + `>> float` | 6028 | 基准 |
| `std::istringstream` 复用 + `str()` + `>> float` | 5806 | — |
| `std::strtof` | 63.8 | **94× 快于 `>>`** |
| `sscanf("%f")` | 243.6 | 25× 快于 `>>` |
| `unordered_map<tuple<int,int,int>>` 查找+插入 | 33.3 | 基准 |
| `unordered_map<uint64_t>`（打包 key）查找+插入 | 15.3 | **2.2× 快** |
| 去重：`unordered_map`（不预留） | 11.2 | 基准 |
| 去重：`unordered_map`（预留桶） | 12.2 | 无改善 |
| 去重：`sort + unique`（扁平数组） | 8.5 | 1.3× 快 |

> 表中绝对值受本机环境影响（`istream` 开销明显高于常见水平），**但比值在多轮测量中稳定**；
> 与表 2-2 的插桩占比（78%~91%）互相印证。

**表 2-4 Debug vs Release（`smooth_100k`）**

| 构建 | 加载 ms |
|---|---:|
| Release | 778.7 |
| Debug | **3543.6** |

**读法**：

1. **吞吐恒定在 ~10 MB/s，且与文件大小无关**——说明没有超线性退化（哈希碰撞、vector 抖动都没失控），
   纯粹是每字节成本太高。100 MB 的 OBJ 要 10 秒，这在引擎启动路径上不可接受。
2. **`smooth_100k` 比 `flat_100k` 慢 2.4 倍，而两者三角形数完全相同**，唯一差别是多了 `vt`/`vn`
   （浮点数从 15.2 万涨到 40.5 万）。这直接证明"瓶颈是数字解析的数量"，而不是面数或去重。
3. **`many_mtl_100k` 只比 `smooth_100k` 慢 1%**：`usemtl` / `mtllib` / 8 个 surface 的拆分几乎免费，
   说明材质路径不是性能问题（但仍是功能问题，见 §3.2）。
4. **CRLF 与注释文件差异 < 6%**：行处理本身不是问题。
5. **Debug 下 3.5 秒**：而工程默认就是 Debug 构建——实际加载体验比表 2-1 更糟。

**对 P0 改造的收益外推**：把 `>>` 换成 `strtof`/`from_chars`（占总时间 78%~91%，单点约快 28~94×），
再叠加面 token 与去重表的改造，`flat_100k` 预计从 **326 ms → 约 50~70 ms（≈5~6×）**，
吞吐从 10 MB/s 提升到 **60 MB/s 量级**；`smooth_100k` 从 779 ms → **约 90~120 ms**。
这是"零算法改动的纯 IO/解析层替换"就能拿到的收益。

### 2.3 内存放大

**实测**（峰值工作集，含进程基线约 4~5 MB）：

| 文件 | 大小 | 峰值内存 | 倍数 |
|---|---:|---:|---:|
| `flat_10k` | 0.30 MB | 6.0 MB | ~20×（基线主导） |
| `flat_100k` | 3.26 MB | 19.3 MB | **5.9×** |
| `smooth_100k` | 7.78 MB | 31.6 MB | **4.1×** |
| `flat_500k` | 17.4 MB | 88.3 MB | **5.1×** |

经验值：**峰值内存 ≈ 4~6 × OBJ 文件大小**。一个 100 MB 的模型要占 0.5 GB。

**构成拆解**（`flat_500k`，17.4 MB 文件 / 25.1 万唯一顶点 / 50 万三角形）：

| 项目 | 估算 |
|---|---:|
| 文件文本（`std::string`） | 17.4 MB |
| `istringstream` 内部副本 | 17.4 MB |
| SoA 顶点数据（56 B/顶点） | 14.1 MB |
| 索引（12 B/三角形） | 6.0 MB |
| 去重表节点（~40 B/顶点 + 桶） | ~11 MB |
| 进程基线 | ~5 MB |
| **合计** | **≈ 71 MB**（实测 88 MB，差值来自分配器碎片与 vector 容量冗余） |

两个可以立即拿掉的常数项：**两份完整文件副本（35 MB）**、**去重表（11 MB）**。
按 Blender 的思路（mmap + 扁平 corner 数组 + 打包 key 开放寻址），同一份数据可以做到 **~2× 文件大小**。

> 附：GL 上传前还会再造一份 14 float/vertex 的交错缓冲（`gl_rendering_device.cpp:96-120`），
> 那是另一条链路的瞬时峰值，不在上面的加载峰值里。

---

## 3. 不完善问题清单

### 3.1 P0：会直接导致渲染错误的缺陷

| # | 问题 | 证据 | 后果 |
|---|---|---|---|
| 1 | **法线回退判定是全局的** | `has_normals` 在 `obj_loader.cpp:153` 定义、在 224 处只要出现任意 `vn` 就置 true；319 行 `if (!has_normals && ...)` 才算法线 | 文件里只要**有一个** `vn`，所有没带法线的面就得到 `Vector3()`，shader 里法线全零 → 该区域完全黑或闪烁 |
| 2 | **`s` 平滑组被忽略** | 关键字分派里没有 `s`（`obj_loader.cpp:313` 注释"其余关键字 (o / g / s ...) 暂不处理"） | 导出时没写 `vn` 但写了 `s off` 的硬边模型（常见于建筑/机械资产）会被整体平滑，丢失所有棱角 |
| 3 | **切线加权错误** | `mesh.cpp:52-53` 先 `normalized()` 再累加，`mesh.cpp:67` 的 `TODO 施密特正交化` | 标准算法应"累加未归一化的 T/B，最后一次归一化"。当前实现让每个三角形等权，即使三角形面积/UV 密度不同；且 T 不保证垂直于 N，法线贴图会出现方向性偏差 |
| 4 | **凹多边形扇形三角化穿帮** | `obj_loader.cpp:290` 的 `(0, i, i+1)` | 五边形以上的凹面（圆环端面、星形、字母轮廓）会生成覆盖到面外的三角形 |
| 5 | **退化面不做处理** | 无面积/重复索引检查 | 零面积三角形的顶点法线为 0，且切线 `det == 0` 被跳过，留下零切线 |

### 3.2 P1：语义缺失（对照 OBJ 规范与业界实现）

| 特性 | 现状 | 影响 |
|---|---|---|
| `o` 对象 | 忽略 | 场景级 OBJ（一个文件多个零件）整文件塌缩成一个 Mesh，无法分别摆放 |
| `g` 组 | 忽略 | 同上，且丢失 vertex group 语义 |
| `s` 平滑组 / 硬边 | 忽略（降维成"全平滑"） | 见 P0-2 |
| `mtllib` 多文件 / 带空格的文件名 | 空格分隔的多文件名支持；未加引号且含空格的文件名会被拆错 | 部分工具导出的资产材质丢失 |
| `usemtl` 同名跨库 | `materials[name]` 后者覆盖前者 | 材质串味 |
| `vt` 第三分量 `w` | 忽略 | 非标准扩展，影响极小 |
| `v x y z w` | 忽略 `w` | 同上 |
| `l` 折线 / `p` 点 | 忽略 | 线框资产导入为空 |
| `curv` / `surf` / NURBS | 忽略 | 曲线资产不支持（可接受） |
| 顶点色（`v x y z r g b` / `#MRGB`） | 忽略 | 顶点色资产丢失（引擎目前也没有顶点色管线） |
| 轴向 / 缩放 | 无 | OBJ 没有统一的上轴约定（Blender 导出默认 Y-up/-Z-forward，3ds Max 是 Z-up）。没有显式转换参数时，同一批资产朝向可能不一致 |
| MTL `illum` / `Ni` / `Tf` / `Ke` 之外的 `map_d` / `map_Ke` / `map_Ks` / `-o` / `-mm` | 部分忽略或告警 | 透明度贴图、自发光贴图、UV 偏移丢失 |
| `ns → roughness` 公式 | `sqrt(2/(Ns+2))` | Blender 用的是 `1 - sqrt(clamp(Ns,0,1000)/1000)`，两者在 Ns 中等区间差异明显；建议对齐 |

### 3.3 P2：健壮性与诊断

- **失败不可区分**：`FileAccess::get_file_as_text` 对"打开失败"和"空文件"都返回空串，加载器因此只能打印同一句错误。
- **静默丢数据**：越界索引、非法浮点、解析失败全部 `continue`，用户拿不到任何计数或行号。
- **无规模防护**：没有 int32 溢出检查（Blender 有 `validate::size_fits_in_int`），超大文件的行为未定义。
- **路径**：`Path::join`/`replace_url` 依赖 `std::filesystem`，非 ASCII 路径在 MinGW 下的行为未验证。
- **无 `#` 行内注释的显式处理**：靠"解析失败就跳过 token"偶然正确。

### 3.4 P3：工程质量

- 没有可编译的 OBJ 测试（现有的那个是过期文件）。
- 解析与建网格耦合在一个 200 行的函数里，无法复用给 glTF 等后续格式。
- 没有二进制缓存格式，启动即解析。
- 错误处理是 `std::printf`，没有统一的 logger/diagnostic 结构。

---

## 4. 方案 A：自研重构

### 4.1 目标架构（三层，保持 `orca-resource` 不依赖 `orca-rendering`）

```text
core/io/file_access       一次读全文件（可选 mmap），暴露 (data, size)
        ↓
resource/loaders/obj/obj_tokenizer.h    零拷贝：const char* p/end 扫描行与 token
        ↓                               关键字手写匹配 + std::from_chars
resource/loaders/obj/obj_parser.h       语义层：v/vt/vn/f/l/o/g/s/usemtl/mtllib
        ↓                               产出 ObjGeometry（全局顶点池 + 扁平 corner 数组 + FaceElem）
resource/loaders/obj/obj_mesh_builder.h 装配层：corner→vertex 去重、三角化、法线、切线、per-surface
        ↓
Mesh（沿用现有 public API，新增 upload 幂等性由 scene 层修）
```

改动落在 `resource/loaders/` 内，`obj_loader.h` 保留 `ObjMeshLoader` 门面，`ResourceLoader` 注册不变。

### 4.2 关键算法决策

1. **零拷贝行扫描**（照搬 Blender `read_next_obj_line` 的思路）
   ```cpp
   // 普通行：直接返回 string_view 指向文件缓冲，零分配
   // 仅当遇到反斜杠续行时才落到 line_buffer_
   std::string_view read_line(std::string_view &buffer);
   ```
   行尾判定用 `c <= ' '` 而不是 `isspace`，天然兼容 `\r\n`。

2. **数字解析用 `std::from_chars`**（C++17，GCC 11+ / MSVC 2019+ 均支持浮点）
   ```cpp
   const char* parse_float(const char* p, const char* end, float& out);
   ```
   注意 `from_chars` 不跳过前导空白，需自己 `drop_whitespace`。

3. **keyword 手写匹配**：`p[0]=='v' && (p[1]==' ' || p[1]=='n' || p[1]=='t')`，不用 `std::string` 构造。

4. **corner → vertex 去重：打包 key + 开放寻址**
   ```cpp
   // v/vt/vn 各 21 bit（缺失值用 0 表示，实际索引 +1 存储）
   uint64_t key = (uint64_t(v+1) << 42) | (uint64_t(vt+1) << 21) | uint64_t(vn+1);
   ```
   按 `triangle_count` 预估顶点数一次开表（负载因子 0.7 左右），避免 rehash。
   *备选*：先写**不去重的三角形汤**，最后按 3 个 key 做一次 `radix sort`，顺序扫描合并——
   在百万级数据上通常比哈希更快、内存更可控，且可以多线程分块。

5. **三角化：耳切（ear clipping）+ Newell 法向选投影轴**
   - 三角形直接通过；
   - 四边形用对角线选择（选短对角线 / 更接近直角的那个）；
   - 5 边以上投影到主平面做耳切；退化（自交）时退化为扇形并计数告警。
   Blender 本身**不三角化**（保留 n-gon 交给你自己的 tessellator），所以这里没有可抄的现成实现，需要自写。

6. **法线**：
   - 有 `vn` → 用 `vn`（读入时 normalize，Blender 也这么做，因为很多文件小数位不足）；
   - 无 `vn` → 按 `s` 组 + 角度阈值（默认 30°~60°）做平滑分组，组内面积加权累加；
   - 退化面（面积 < 1e-12）强制 flat，避免污染相邻顶点（Blender 的对应做法在 `obj_import_mesh.cc:286-290`）。
   - **不要再使用全局 `has_normals`**，判定要下沉到面。

7. **切线**：按标准流程——累加未归一化的 T/B，最后归一化 + 施密特正交化（`T -= N * dot(N,T)`）。
   无 UV 时直接用 `tangent = 任意与 N 垂直的向量`，避免 GL 上传校验失败。

8. **`o` / `g` 切分**：把"材质"和"对象"都作为 surface 的划分键 `(object_index, material_index)`。
   这样不改变 `Mesh` 结构就能保留对象边界；等 scene 层有了多 Mesh 导入能力再升级成
   `ObjImportResult { std::vector<Ref<Mesh>>, std::vector<std::string> names }`。

9. **显式导入选项**（避免把策略硬编码）：
   ```cpp
   struct ObjImportOptions {
       bool split_objects = true;
       bool generate_tangents = true;
       int smooth_angle_degrees = 60;
       enum class Axis { Y_UP_NEG_Z_FWD, Z_UP_Y_FWD, KEEP } axis = Axis::KEEP;
       float global_scale = 1.0f;
   };
   ```

### 4.3 分期计划

| 阶段 | 内容 | 工作量 | 预期收益 |
|---|---|---|---|
| **P0 热点替换** | 去掉 `istringstream`/`getline`，改指针扫描 + `from_chars`/`strtof`；`tuple` key 换 `uint64_t`；加 `reserve` | 1 人日 | 加载时间 **↓ 5~6×**（外推 326 ms → 50~70 ms），峰值内存 **↓ 30~40%** |
| **P1 正确性** | 法线判定下沉到面、`s` 平滑组、切线与正交化、退化面处理、凹多边形耳切 | 3 人日 | 画面正确性；资产兼容面变宽 |
| **P2 语义补全** | `o`/`g` 切分、轴转换/缩放参数、MTL 补 `map_d`/`map_Ke`/`-o`/`-mm`、`Ns` 公式对齐 | 3 人日 | 场景级 OBJ 可用 |
| **P3 工程化** | 可编译的单元测试（`tests/CMakeLists.txt` 注册）、诊断计数、错误码 | 2 人日 | 后续改动可回归 |
| **P4 缓存** | 接入 `ResourceCache` + 引擎自有二进制网格格式（`.omesh`） | 4 人日 | 启动时间 ↓ 一个量级 |

合计 **≈13 人日**，与 Blender 导入器的"部分移植"估算一致——因为需要写的东西本质相同。

### 4.4 风险

| 风险 | 缓解 |
|---|---|
| `std::from_chars` 浮点在旧 MinGW 上缺失 | 用 `E:/tools/mingw64` 的 GCC 版本实测；不行就引入 header-only 的 `fast_float`（MIT/Apache-2.0，可安全随仓库分发） |
| 凹多边形耳切引入 bug | 用"面积和 == 多边形面积"做自检，失败退回扇形并计数 |
| 切分规则改变影响下游 | `Mesh::Surface` 结构不变；只是 surface 数量和顺序变化 |
| 平滑法线与既有资产观感不一致 | 用 `smooth_angle_degrees` 参数化，默认对齐常见 DCC |

---

## 5. 方案 B：移植 Blender 导入器

### 5.1 它是什么

Blender 的 OBJ 导入器（`source/blender/io/wavefront_obj/importer`）是一条**解析 → 中间表示 → bpy 对象**的管线：

```text
OBJParser (mmap + 零拷贝行扫描)
  → GlobalVertices{ vertices, uv_vertices, vert_normals, vertex_colors, vertex_weights }
  → 每个 Geometry{ face_corners_, face_elements_, global_to_local_vertices_, material_order_, ... }
  → MeshFromGeometry::create_mesh()  → bke::Mesh（per-corner 属性 + n-gon）
  → create_mesh_object()            → bpy Object + Material 节点树
```

值得单独指出的两个设计：

- `importer_geometry()`（`obj_importer.cc:209`）是一条**不创建 bpy Object、只产出 `GeometrySet`** 的入口，
  说明"解析层"和"bpy 层"在 Blender 内部本来就是分开的——这是它可移植性的最好证据。
- 面数据是 **SOA 扁平数组**：`Vector<FaceCorner> face_corners_` + `FaceElem{start_index_, corner_count_}`，
  全程没有 `vector<vector<>>`。

### 5.2 依赖面（按可移植性）

| 文件 | 行数 | Blender 依赖 | 可移植性 |
|---|---|---|---|
| `obj_import_file_reader.cc` | 980 | 只有 2 处 `BKE_reportf` + `CLOG_*`；其余是 BLI 容器与 `fast_float` | **Easy** |
| `obj_import_objects.hh` | 178 | 仅用 `DNA_object_types.h` 的两个枚举 | **Easy** |
| `io/common/IO_string_utils.*` | 153 | 纯函数 + `fast_float` | **Easy** |
| `io/common/IO_validate.hh` | 小 | 2 个 inline 函数 | **Easy** |
| `obj_import_mtl.cc` | 440 | **全部**是 `bke::node_add_static_node` / `SH_NODE_BSDF_PRINCIPLED` 等节点树 API | **Hard** |
| `obj_import_mesh.cc` | 506 | `BKE_mesh_new_nomain`、`MutableAttributeAccessor`、`mesh_calc_edges`、`mesh_set_custom_normals`、`BKE_object_*` | **Hard** |
| `obj_importer.cc` | 268 | Collection / ViewLayer / Depsgraph / `bke::GeometrySet` | **Hard** |
| `obj_import_nurbs.cc` | 392 | `Nurb` / `BPoint` / `CurvesGeometry` | **Hard（可丢弃）** |
| `importer_mesh_utils.cc` | 117 | `BLI_delaunay_2d`（约束 Delaunay，仅用于修复带洞面） | **Hard（可丢弃）** |

统计：`BLI_*` 头约 26 个（其中真正不可替代的是 `BLI_mmap.hh` 与 `BLI_delaunay_2d.hh`），
`BKE_*` 头 16 个，集中在上面 4 个 Hard 文件里。**唯一可直接复用的第三方库是 `fast_float`（MIT/Apache-2.0 双许可）。**

### 5.3 与引擎的 5 个硬冲突

这是"移植"这个词最容易低估的地方——不是改改 include 就能编过：

1. **corner vs vertex**。Blender 的 Mesh 允许"顶点索引 + per-corner UV + per-corner 法线"三套独立数组；
   引擎的 GL 格式是**单一 `indices` + per-vertex 属性**。移植后**必须新增一整趟 `(v,vt,vn)` 去重**，
   而 Blender 恰恰是"因为不需要所以没写"这段代码。也就是说：最难、最热的那部分仍然要自己写。
2. **n-gon vs 三角形**。Blender 导入器**完全不三角化**（推迟到 tessellate 阶段）。引擎需要三角形，
   所以三角化也要自己写——Blender 只提供了"保留 n-gon"这条路，帮不上忙。
3. **延迟求法线 vs 立即求法线**。Blender 没有 `vn` 时不算法线，只写 `sharp_face` 属性，靠后续
   `mesh_normals_calc` 处理。引擎没有这套属性系统，必须自己实现"平滑组/角度阈值 → 顶点法线"。
4. **Object/Collection vs 单 Mesh**。Blender 的一个 `Geometry` 映射到一个 Object，带 material slot 数组、
   vertex group、deform layer。引擎没有这些概念，需要重新设计切分策略。
5. **材质节点树**。`obj_import_mtl.cc` 的 440 行里约 300 行是在搭
   Principled BSDF / Image Texture / Normal Map / Mapping 节点。引擎的 `StandardMaterial` 是扁平的
   6 槽结构，这部分**完全不可移植**，只能抄公式（例如 `Ns → roughness`）。

### 5.4 工作量估算

按"照抄结构、改写成引擎风格"估：

| 组件 | 人日 |
|---|---|
| 行读取 + 关键字分发 + 数字解析 | 1.5 |
| `v/vt/vn/f/l` 解析 + 负索引/越界策略 | 1.5 |
| corner 去重 + 索引生成（Blender 没有，必须新写） | 1.5 |
| 多边形三角化（Blender 没有，必须新写） | 2.0 |
| 法线（有/无 vn、`s` 边界） | 1.5 |
| MTL 解析 + 材质映射 | 1.5 |
| `o`/`g`/`usemtl` 切分 | 0.5 |
| 轴转换 / 缩放 / 文件读取后端 | 1.0 |
| 测试 | 2.0 |
| **合计** | **≈13 人日** |

注意这个数字和方案 A 的 P0–P3 **几乎相同**——因为"要写的东西"是同一批东西，差别只在于是否参考了
Blender 的代码组织方式。

### 5.5 许可证：这是决定性因素

所有文件头都是：

```
SPDX-FileCopyrightText: 2023 Blender Authors
SPDX-License-Identifier: GPL-2.0-or-later
```

- **"只依赖 BLI 容器、不依赖 Blender 运行时" ≠ 没有 GPL**。`obj_import_file_reader.cc` 依旧是 GPL 代码，
  贴进引擎即构成衍生作品，整个引擎需以 GPL-2.0-or-later 分发（提供完整对应源码）。
- **可以做的**：① 学算法与数据结构选择（思想不受版权保护），用自己的命名与类型**独立重写**；
  ② 复制 `fast_float`（MIT/Apache-2.0）；③ 在**离线工具链**里调用 `blender --background --python`
  把 `.obj` 烘成自有格式——约束的是 Blender 程序本身，不约束它的输出数据。
- **不能做的**：把 importer 的 `.cc` 或 `bpy_extras/io_utils.py` 直接放进仓库。

如果这个引擎将来要闭源分发，这一条就足以否决"直接移植"。

### 5.6 结论

**整体移植：不推荐。** 理由是三个独立的、任何一个都成立的否决项：

1. 许可证（GPL → 全引擎传染）；
2. 语义不匹配（corner/n-gon/延迟法线/材质节点树，5 处硬冲突，移植 = 重写）；
3. 收益不匹配（花 25~40 人日搬来的代码，最关键的"去重 + 三角化 + 立即求法线"仍然是 Blender 没写、
   必须自己写的部分）。

**算法级借鉴：强烈推荐。** Blender 的解析层（mmap 零拷贝扫描、`(p,end)` 双指针接口、`fast_float`、
扁平 corner 数组、`-1` 哨兵 + 惰性兜底、退化面强制 sharp）是可以直接落地的工程经验，
而且完全绕开许可证问题。

---

## 6. 方案 C：现成第三方库（补充对比）

用户提到"接入第三方 OBJ 加载器"，除 Blender 外还有几个更常规的选项：

| 库 | 许可 | 规模 | 优点 | 缺点 |
|---|---|---|---|---|
| **tinyobjloader** | MIT | 约 6~7k 行，header-only | 成熟、支持 group/material/负索引/n-gon、有 shape 划分 | n-gon 只做扇形三角化（凹面同样穿帮）；返回的是自己的一套顶点结构，仍需转换到 `Mesh`；与"从零实现迷你引擎"的定位冲突 |
| **fast_obj** | MIT | 约 1.5k 行，单文件 | 以解析速度为主要卖点（作者宣称比 tinyobjloader 快数倍），零分配设计 | 材质/group 支持弱，仍需自己做三角化与法线 |
| **assimp** | BSD-3 | 巨大 | 支持一切格式 | 体量远超整个引擎，构建负担重 |
| **Blender importer** | **GPL-2.0-or-later** | ~3.4k 行但强依赖 BKE | 行为最贴近 DCC 工具 | 见 §5：不可直接使用 |

评估：

- 如果只追求"解析速度"，`fast_obj` 是性价比最高的外援，但它把"ObjMesh → Mesh"的转换成本留给了你，
  而这正是当前实现最大的问题所在（去重结构、三角化、法线）。
- `tinyobjloader` 能顺手解决材质与 group，但引入 7k 行第三方代码，与仓库"`thirdparty/` 只放预编译库、
  且是自研引擎"的定位不符；同时它也没有解决凹多边形和切线质量。
- **三者都不能解决最痛的问题**：`MeshInstance3D` 每实例重复上传、无资源缓存、无二进制格式。

---

## 7. 推荐路线

### 7.1 结论

```text
短期（1 天）   → 方案 A 的 P0：换掉 istringstream/getline，换 from_chars，换去重表，加 reserve
中期（1~2 周） → 方案 A 的 P1+P2：正确性 + 语义，算法细节参考 Blender（不抄代码）
长期（可并行） → 修 scene/rendering 侧的重复上传，接入 ResourceCache，引入 .omesh 二进制格式
不做           → 全量移植 Blender importer（GPL + 语义冲突 + 工作量）
可选           → 若某天需要"和 Blender 导入结果逐字节一致"，再做离线烘焙管线
```

理由一句话：**当前实现的性能瓶颈是"用了错误的工具"（istringstream / tuple map / 无 reserve），
不是"算法不够高级"；而功能瓶颈是"根本没解析那几个关键字"。这两类问题都可以在 2 周内自研解决，
不需要背 GPL 或 7k 行第三方代码。**

### 7.2 里程碑与验收标准

| 里程碑 | 验收标准 |
|---|---|
| M1 热点替换 | 同一模型加载时间相对当前 ↓ ≥ 5×；峰值内存 ↓ ≥ 25%；`ctest` 全绿 |
| M2 正确性 | 新增用例覆盖：部分 `vn` 文件、`s 0` 硬边、凹五边形、零面积面、负索引、CRLF、行内注释 |
| M3 语义 | 多 `o` 文件能正确切分 surface；轴向/缩放参数生效；MTL 透明度贴图接入 |
| M4 缓存 | 同一资源 `load()` 两次只解析一次；`.omesh` 二次加载快于 OBJ 解析 ≥ 10× |
| M5 上传幂等 | 同一 `Mesh` 的 N 个实例只产生 1 份 GPU 缓冲；VRAM 占用不随实例数线性增长 |

### 7.3 明确不做的事

- NURBS 曲线/曲面、`p` 点、`l` 折线（引擎渲染管线不支持）。
- 顶点色（引擎没有顶点色 attribute）。
- `#MRGB`、vertex weight、vertex group。
- 与 Blender 逐字节一致的错误容忍行为。

---

## 附录 A：基准测试复现

基准工程的产物在 `build/bench_obj/`（`build/` 已被 `.gitignore` 忽略，属于一次性产物，未进版本库）：

| 文件 | 作用 |
|---|---|
| `gen_obj.py` | 生成合成 OBJ / MTL（flat / smooth / 多材质 / CRLF / 注释 五种变体） |
| `obj_loader_bench.cpp` | **插桩版 loader 副本**（保持逻辑不变，只加计时与调用计数） |
| `bench_obj_driver.cpp` | 端到端驱动：对每个文件重复 3~5 次，输出中位数、计数与阶段占比 |
| `micro_bench.cpp` | 微基准：`istringstream` / `strtof` / `sscanf`；tuple vs 打包 key；去重策略 |
| `build_bench.ps1` | 编译脚本（Debug 与 Release 两套，链接 `build/` 与 `build/bench_obj/build_release/` 的静态库） |

复现（Release）：

```powershell
# 1) 生成数据与编译（脚本内部会用 cmake 单独构建一份 Release 的 orca-resource）
powershell -ExecutionPolicy Bypass -File build/bench_obj/build_bench.ps1
python build/bench_obj/gen_obj.py

# 2) 端到端加载（每次输出 RESULT / PHASE / COUNT 行）
build/bench_obj/bench_obj_driver_release.exe `
    build/bench_obj/flat_100k.obj build/bench_obj/smooth_100k.obj

# 3) 微基准
build/bench_obj/micro_bench_o2.exe

# 4) 峰值内存（在进程运行期间采样 WorkingSet）
```

峰值内存的采样方式（PowerShell，在 driver 运行期间轮询）：

```powershell
$p = Start-Process build/bench_obj/bench_obj_driver_release.exe `
     -ArgumentList build/bench_obj/flat_500k.obj -PassThru -NoNewWindow
$peak = 0
while (-not $p.HasExited) {
    $w = (Get-Process -Id $p.Id).WorkingSet64
    if ($w -gt $peak) { $peak = $w }
    Start-Sleep -Milliseconds 40
}
"peak = {0:N1} MB" -f ($peak / 1MB)
```

> 说明：`Memory::get_allocation_count()` 只统计 `memnew`，统计不到 `std::vector` / `std::unordered_map` /
> `std::string` 的分配，所以分配次数改用"峰值工作集 + 结构估算"来给结论。

## 附录 B：相关位置索引

| 主题 | 位置 |
|---|---|
| 加载入口 | `resource/loaders/obj_loader.cpp:141` |
| 逐行解析 | `resource/loaders/obj_loader.cpp:200-314` |
| 面索引解析 | `resource/loaders/obj_loader.cpp:75-110` |
| 去重表 | `resource/loaders/obj_loader.cpp:23-42`、`295-309` |
| 三角形装配 | `resource/loaders/obj_loader.cpp:290-311` |
| 法线回退 | `resource/loaders/obj_loader.cpp:113-138`、`319-322` |
| 切线 | `resource/mesh/mesh.cpp:12-80` |
| MTL | `resource/loaders/mtl_loader.cpp:166-340` |
| 上传 | `scene/3d/mesh_instance_3d.cpp:3-26`、`servers/rendering/opengl/gl_rendering_device.cpp:76-166` |
| 资源缓存（未接入） | `resource/resource_cache.h` |
| Blender 解析器 | `D:\dev\other\blender\source\blender\io\wavefront_obj\importer\obj_import_file_reader.cc` |
| Blender 网格装配 | `D:\dev\other\blender\source\blender\io\wavefront_obj\importer\obj_import_mesh.cc` |
| 基准工程 | `build/bench_obj/`（gitignore） |

## 附录 C：重构后的解析骨架（草图）

只表达结构与数据流，不是最终代码。

```cpp
// ---------- 1) 中间表示：与 Blender 的 Geometry 同构，扁平、无 per-face 分配 ----------
struct ObjCorner { int32_t vert = -1, uv = -1, normal = -1; };   // 0 基，-1 表示缺省

struct ObjFace
{
    uint32_t material   = 0;      // ObjGeometry::material_names 的下标
    uint32_t first_corner = 0;    // corners 数组起点
    uint32_t corner_count = 0;
    bool     smooth     = false;  // 由 `s` 行决定
};

struct ObjGeometry
{
    std::string name;
    std::vector<Vector3> positions, normals;   // 全局池
    std::vector<Vector2> uvs;
    std::vector<ObjCorner> corners;            // 扁平 corner 数组
    std::vector<ObjFace> faces;
    std::vector<std::string> material_names;
};

// ---------- 2) 词法层：零拷贝、手写关键字、from_chars ----------
std::string_view read_line(std::string_view &buffer);          // 不分配
const char *parse_float(const char *p, const char *end, float &out);   // std::from_chars
const char *parse_int  (const char *p, const char *end, int &out);

void parse(std::string_view buffer, std::vector<ObjGeometry> &out)
{
    ObjGeometry *geom = &out.emplace_back();
    bool smooth = false;
    uint32_t material = 0;

    while (!buffer.empty())
    {
        const std::string_view line = read_line(buffer);
        const char *p = line.data(), *end = p + line.size();
        p = drop_whitespace(p, end);
        if (p == end || *p == '#') continue;

        switch (*p)
        {
        case 'v':
            if (p[1] == ' ')      { Vector3 v; p = parse_floats(p + 1, end, &v.x, 3); geom->positions.push_back(v); }
            else if (p[1] == 'n') { Vector3 n; p = parse_floats(p + 1, end, &n.x, 3); n.normalize(); geom->normals.push_back(n); }
            else if (p[1] == 't') { Vector2 t; p = parse_floats(p + 1, end, &t.x, 2); geom->uvs.push_back(t); }
            break;
        case 'f':
        {
            ObjFace face{material, (uint32_t)geom->corners.size(), 0, smooth};
            while (p < end)                                  // 逐 corner 解析（负索引就地转换）
            {
                ObjCorner corner;
                if (!parse_corner(p, end, corner, *geom)) break;
                geom->corners.push_back(corner);
                ++face.corner_count;
            }
            geom->faces.push_back(face);
            break;
        }
        case 'o':
            geom = &out.emplace_back();                  // 切分对象
            geom->name = trim(line);
            break;
        case 's':
            smooth = !(trim(line) == "s 0" || trim(line) == "s off");
            break;
        case 'u':  // usemtl
            material = intern_material(geom, trim(line));
            break;
        default:
            break;                                       // 未知关键字静默忽略并计数
        }
    }
}

// ---------- 3) 装配层：corner → 唯一顶点，然后三角化 ----------
uint64_t key = (uint64_t(vert + 1) << 42) | (uint64_t(uv + 1) << 21) | uint64_t(normal + 1);
uint32_t index = table.find_or_insert(key);       // 开放寻址，预留 0.7 负载
if (index == newly_inserted)
{
    surface.vertices.push_back(geom.positions[vert]);            // 位置
    surface.normals .push_back(normal >= 0 ? geom.normals[normal] : Vector3());  // 稍后按 s/角度补
    surface.uvs     .push_back(uv >= 0 ? geom.uvs[uv] : Vector2());
}
surface.indices.push_back(index);
// 面按 (object, material) 落到不同 surface；n-gon 走耳切；最后统一算法线/切线
```

关键点：**解析层与装配层分离**。解析层只做字节点扫描（可单独测、可复用到 glTF），
装配层才关心去重/三角化/材质——这也是 Blender 的 `OBJParser` 与 `MeshFromGeometry` 的划分方式。
