# AGENTS.md

> 本文件面向在本仓库中工作的编码智能体。它描述的是**当前代码的真实状态**，不是未来路线图。
> 改动前先读第 5、6 节；涉及资源/渲染分层时，先读 `docs/` 下的两份设计文档，但注意其中的“现状”章节可能已经过时。

---

## 1. 项目简介

`orca-engine` 是一个用现代 C++ 从零实现的迷你 3D 游戏引擎，整体分层参考 Godot：

- `Object -> RefCounted -> Resource` 的对象与引用计数体系；
- `RID + RIDStorage` 的服务器侧资源句柄；
- `RenderingServer -> RenderingDevice` 的渲染服务/图形后端分层；
- `Node / Node3D / SceneTree` 的场景树与固定物理步长；
- `ResourceLoader + ResourceFormatLoader` 的资源加载抽象。

当前已经能跑通一条最小但完整的渲染链路：

```text
.obj/.mtl/.png 资源  ->  Mesh / StandardMaterial / ImageTexture
        -> 场景节点进入场景树时上传
        -> RenderingServer 保存 RID 数据
        -> OpenGL 3.3 Core 绘制
        -> 单方向光的金属度/粗糙度 PBR + 法线贴图 + 视差贴图
```

`application` 的 `test_application` 就是当前的演示程序：初始化窗口和设备、加载 OBJ 模型、创建 `World3D`/相机/方向光、进入主循环。项目版本为 `0.1.0`，没有编辑器、序列化场景、音频、物理引擎或脚本系统。

---

## 2. 项目技术栈

| 类别 | 选型 | 说明 |
|---|---|---|
| 语言标准 | C++17 | 顶层 CMake 强制 `CXX_STANDARD 17`，且关闭编译器扩展 |
| 构建系统 | CMake >= 3.16 | 当前构建目录使用 `MinGW Makefiles` 生成器 |
| 编译器（现有环境） | MinGW-w64 g++ | `build/CMakeCache.txt` 中为 `E:/tools/mingw64/bin/g++.exe` |
| 图形 API | OpenGL 3.3 Core（默认、可用） | 通过 `thirdparty/glad` 加载现代入口点；GLSL `#version 330 core` |
| 图形 API（可选） | Vulkan | 只完成了 instance / 物理设备 / 逻辑设备 / surface / swapchain 骨架，绘制与资源接口仍是空实现 |
| 窗口/输入 | GLFW（默认）或 SDL3 | 二选一，预编译库只提供 Windows/MinGW-w64 版本 |
| 图像解码 | `stb_image`（单头文件） | 由 `ImageTextureLoader` 使用 |
| 测试 | doctest（单头文件） | 每个测试目标自带 `main()` |
| 数学库 | 自研 | `Vector2/3/4`、`Basis`、`Transform2D/3D`、`Projection`、`Color`、`Quaternion` 等 |
| 内存 | 自研薄封装 | `memnew` / `memdelete` / `memnew_arr`，底层是 malloc/free + placement new |
| 平台 | 当前仅 Windows/MinGW-w64 | `thirdparty/CMakeLists.txt` 对非 Windows 直接 `FATAL_ERROR` |

第三方库均在 `thirdparty/` 下，以源码或预编译静态/动态库形式随仓库提供，不使用 `FetchContent`。

### 2.1 分层与依赖方向

CMake 目标：`orca-object`、`orca-math`、`orca-core`、`orca-rendering`、`orca-platform`、`orca-resource`、`orca-scene`、`orca-application`。

```text
orca-object / orca-math / orca-core      基础层（互不反向依赖）
        ↑
        ├── orca-rendering   （依赖 math、core）
        │         ↑
        │   orca-platform    （依赖 rendering，提供窗口/上下文）
        │
        ├── orca-resource    （依赖 object、math、core、stb_image；禁止依赖 rendering）
        │
        ├── orca-scene       （依赖 object、core、rendering、resource）
        │
        └── orca-application （组装以上全部，拥有 main 循环）
```

必须保持的铁律：

1. **`orca-resource` 不能链接 `orca-rendering`。** 资源层只保存 CPU 数据和可回填的 `RID`。
2. **只有 `scene` 层负责把资源转换为 `RenderingServer` 调用**（典型入口：`MeshInstance3D::ensure_surfaces/ensure_material/ensure_texture`）。
3. `platform` 不包含 GL/Vulkan 具体类型，只通过 `RenderingWindowInfo` 传递原生句柄与函数指针。
4. 修改模块依赖前先看 `*/CMakeLists.txt`，不要为了图方便新增反向链接。

---

## 3. 启动方式与测试规范

### 3.1 环境准备

- Windows + MinGW-w64（当前唯一验证过的平台组合）。
- CMake 3.16 及以上。
- 可用的 OpenGL 3.3 驱动。
- 仓库自带的预编译 GLFW / SDL3 仅支持 MinGW-w64；换 MSVC 或 Linux 需要自行替换 `thirdparty/` 中的库。
- 若机器上 `g++/mingw32-make` 不在 `PATH`，先加入 `E:/tools/mingw64/bin` 或使用完整路径。

### 3.2 配置与构建

在仓库根目录执行（已有 `build/` 时可直接跳到构建）：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

默认配置（与当前 `build/CMakeCache.txt` 一致）：

```text
ORCA_BUILD_TESTS      = ON
ORCA_PLATFORM_GLFW    = ON
ORCA_PLATFORM_SDL3    = OFF
ORCA_RENDERER_OPENGL  = ON
ORCA_RENDERER_VULKAN  = OFF
ORCA_RENDERER_DEFAULT = opengl
```

可切换的后端/平台：

```powershell
# SDL3 窗口后端（需要同时关闭 GLFW）
cmake -S . -B build -DORCA_PLATFORM_GLFW=OFF -DORCA_PLATFORM_SDL3=ON
# Vulkan 后端（当前只能验证窗口/设备初始化，不能正常绘制）
cmake -S . -B build -DORCA_RENDERER_OPENGL=OFF -DORCA_RENDERER_VULKAN=ON -DORCA_RENDERER_DEFAULT=vulkan
```

注意：

- GLFW 与 SDL3 **只能启用一个**，否则 CMake 直接报错。
- 至少启用一个图形后端，否则 CMake 直接报错。
- `build/` 是生成目录且被 `.gitignore` 忽略，不要手改里面的源码副本或把生成物当源码。

### 3.3 运行演示程序

`res://` 的解析基准是**当前工作目录**，不是可执行文件目录（见 `core/io/path.cpp`）：

```text
res://xxx  ->  <当前工作目录>/resources/xxx
user://xxx ->  HOME / USERPROFILE / 当前工作目录
```

因此必须从 `build/` 启动，且 `build/resources/` 中要有模型/贴图：

```powershell
cd build
.\tests\orca-application-test.exe
```

shader 是例外：`ShaderLoader` 会先找 `build/resources/shaders/`，找不到再回退到源码树 `servers/rendering/shaders/`，所以改 GLSL 后通常无需手动拷贝，但**仍需重启程序**才能重新编译 shader 程序。

> 重要：仓库源码中**没有** `assets/` 或 `resources/` 目录；演示用模型/贴图目前只存在于构建目录 `build/resources/`（被 `.gitignore` 忽略，另有 `build/resources.zip` 但未被 CMake 引用）。在干净克隆里，除 shader 外的演示资源需要自行准备到 `build/resources/`，否则 `test_application` 会在加载 `res://gggg/obj.obj` 等资源时失败。

### 3.4 测试命令

```powershell
# 运行已注册到 CTest 的测试（当前只有 orca-tests 注册）
ctest --test-dir build --output-on-failure

# 或直接运行
cd build
.\tests\orca-tests.exe
```

当前测试目标（定义在 `tests/CMakeLists.txt`）：

| 目标 | 内容 | 是否注册到 CTest |
|---|---|---|
| `orca-tests` | doctest 单元测试：math / object / os / scene | 是，名称为 `orca-tests` |
| `orca-platform-window-test` | 创建窗口并初始化 OpenGL 设备，手动运行 | 否 |
| `orca-application-test` | 演示程序（加载资源、渲染场景），手动运行 | 否 |

> 已知测试缺口：`tests/resource/test_obj_loader.cpp` **没有**加入 `tests/CMakeLists.txt`，且其中使用的 `Mesh::Surface::get_vertex_count()` 等 API 与当前 `mesh.h` 不一致，属于过期测试。修改 OBJ/资源加载时不要指望它兜底，应补新的、可编译的测试。

### 3.5 测试编写规范

1. 使用 doctest，头文件写法固定为 `#include "doctest/doctest.h"`。
2. 新测试文件命名为 `test_<被测对象>.cpp`，放在对应模块目录，例如 `tests/resource/test_resource_loader.cpp`。
3. `TEST_CASE` 标题使用 `"模块/类: 行为"` 形式，例如 `"RIDStorage: freed handle is rejected"`。
4. 用 `CHECK` 做可继续执行的断言，前置条件用 `REQUIRE`，浮点比较用 `doctest::Approx`。
5. **新增测试文件必须手动加入 `tests/CMakeLists.txt` 的 `orca-tests` 源列表**，本项目没有自动 glob 测试。
6. 单元测试应尽量不依赖窗口、GPU 和外部资源；需要窗口/GL 上下文的放到独立可执行目标（如 `test_window.cpp`），不要塞进 `orca-tests`。
7. 需要文件的测试应自行写入临时文件并在用例内删除，避免污染 `build/resources/`；需要 `res://` 资源的测试应显式说明运行目录要求。
8. 测试结束前确认没有泄漏：项目自带 `Memory::get_allocation_count()`，已有 `test_memory.cpp` 作为参考。

---

## 4. 项目地图

```text
orca-engine/
├─ CMakeLists.txt                  # 顶层配置：C++17、平台/渲染后端选项、子目录
├─ AGENTS.md                       # 本文件
├─ core/                           # 与渲染无关的基础设施
│  ├─ object/
│  │  ├─ object.h                  # Object 基类、_notification 通知
│  │  ├─ object.cpp
│  │  ├─ ref_counted.h             # RefCounted + Ref<T>（引用计数、cast、移动语义）
│  │  └─ rid.h                     # RID 位布局、RIDType、RIDUtil、RIDStorage<T>
│  ├─ math/                        # Vector2/3/4、Basis、Transform2D/3D、
│  │                              # Projection、Color、Quaternion、Plane、Face3、
│  │                              # TriangleMesh、math_defs（real_t = float）、math_funcs
│  ├─ io/
│  │  ├─ path.h/.cpp               # 路径拼接、扩展名、res:// 与 user:// 解析
│  │  ├─ file_access.h/.cpp        # 基于 FILE* 的最小文件读取
│  │  └─ dir_access.h/.cpp
│  └─ os/
│     ├─ memory.h                  # memnew / memdelete / memnew_arr / 分配计数
│     └─ time.h/.cpp               # Time::now() 等
├─ thirdparty/
│  ├─ doctest/                     # 测试框架（头文件）
│  ├─ stbimage/stb_image.h         # 图像解码
│  ├─ glad/include/glad/gl.h       # OpenGL 入口点
│  ├─ glfw/                        # GLFW 头文件 + MinGW 预编译静态库
│  ├─ sdl3/                        # SDL3 头文件 + MinGW 预编译动态库
│  └─ CMakeLists.txt               # 以 INTERFACE / IMPORTED 目标暴露第三方库
├─ servers/rendering/              # 渲染服务器与图形后端
│  ├─ rendering_device.h/.cpp      # 后端抽象接口 + 工厂（按编译宏选 GL/VK）
│  ├─ rendering_server.h/.cpp      # 高层 API：scene/camera/light/mesh/material/texture
│  ├─ rendering_storage.h/.cpp     # 各资源的 CPU 端描述 + RIDStorage 实例
│  ├─ shader_loader.h/.cpp         # 从 res://shaders 或源码目录读取 GLSL
│  ├─ opengl/
│  │  ├─ gl_rendering_device.h/.cpp# 可用的 OpenGL 实现（VAO/VBO、shader、纹理）
│  ├─ vulkan/
│  │  ├─ vk_rendering_device.h/.cpp# instance/device/swapchain 骨架；绘制接口为空
│  └─ shaders/
│     ├─ builtin/vert.glsl         # 内置顶点着色器
│     ├─ builtin/frag.glsl         # GGX/Smith/Schlick、法线贴图、视差贴图
│     └─ dummy/                    # 占位 shader
├─ platform/                       # 窗口与输入抽象
│  ├─ window.h/.cpp                # Window 抽象 + create() 工厂
│  ├─ glfw/glfw_window.h/.cpp      # GLFW 实现（默认）
│  └─ sdl3/sdl3_window.h/.cpp      # SDL3 实现
├─ resource/                       # CPU-first 资源层（不依赖渲染服务器）
│  ├─ resource.h/.cpp              # Resource 基类（url、get_rid、duplicate）
│  ├─ resource_cache.h/.cpp        # url -> Resource* 弱引用缓存（当前尚未接入加载流程）
│  ├─ resource_loader.h/.cpp       # 单例 + 扩展名 -> loader 注册表
│  ├─ loaders/
│  │  ├─ obj_loader.h/.cpp         # .obj -> Mesh，支持 mtllib/usemtl、负索引、切线
│  │  ├─ mtl_loader.h/.cpp         # .mtl -> MaterialLibrary，解析 Kd/Ns/Pr/Pm/贴图
│  │  └─ image_texture_loader.*    # .png/.jpg 等 -> ImageTexture（stb_image）
│  ├─ mesh/mesh.h/.cpp             # Mesh::Surface、CPU 顶点/索引、RID + dirty
│  ├─ material/material.h/.cpp     # StandardMaterial、6 个纹理槽、标量参数
│  ├─ material/material_library.*  # .mtl 中多材质的容器
│  ├─ texture/image_texture.h      # CPU 像素 + RID + srgb/mipmap/filter 标志
│  └─ environment/environment_3d.* # 环境资源壳
├─ scene/                          # 场景树（消费资源，调用渲染服务器）
│  ├─ node.h/.cpp                  # 父子关系、生命周期、process/physics_process
│  ├─ scene_tree.h/.cpp            # 固定 60Hz 物理步长 + 可变渲染帧
│  ├─ viewport.h/.cpp              # 根节点；render() 调 RenderingServer::sync/draw
│  └─ 3d/
│     ├─ node3d.h/.cpp             # transform、旋转、全局变换传播
│     ├─ world_3d.h                # scene/environment 的 RID 封装
│     ├─ visual_instance_3d.h      # instance RID、可见性/transform 通知
│     ├─ mesh_instance_3d.h/.cpp   # 进入场景树时上传 mesh/material/texture
│     ├─ camera_3d.h/.cpp          # 透视/正交相机、设为主相机
│     └─ light_3d.h                # 方向光（目前只有 DIRECTIONAL）
├─ application/
│  ├─ application.h/.cpp           # 单例初始化顺序、窗口、主循环、shutdown
│  └─ CMakeLists.txt
├─ tests/
│  ├─ main.cpp                     # DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
│  ├─ CMakeLists.txt               # 三个测试可执行目标
│  ├─ core/math/…                  # 数学单元测试
│  ├─ core/object/…                # RefCounted、RID 测试
│  ├─ core/os/…                    # Memory、Time 测试
│  ├─ scene/…                      # Node3D 全局变换、场景树
│  ├─ resource/test_obj_loader.cpp # 过期且未加入构建，勿直接依赖
│  ├─ platform/test_window.cpp     # 窗口+设备初始化集成测试
│  └─ application/test_application.cpp # 可交互演示程序
├─ docs/
│  ├─ resource_module_roadmap.md   # 资源模块设计/路线图（部分已完成）
│  └─ material_texture_plan.md     # 材质纹理方案（部分已完成）
└─ build/                          # CMake 生成目录，已忽略；运行资源也在这里
```

### 4.1 关键入口速查

| 想改什么 | 先看这些文件 |
|---|---|
| 程序启动、初始化顺序、主循环 | `application/application.cpp` |
| 一帧的调度（物理/逻辑/渲染） | `scene/scene_tree.cpp`、`scene/viewport.cpp` |
| 资源格式加载 | `resource/resource_loader.cpp`、`resource/loaders/*` |
| 材质参数/纹理槽 | `resource/material/material.h`、`servers/rendering/rendering_storage.h` |
| 场景对象上传到渲染端 | `scene/3d/mesh_instance_3d.cpp`、`scene/3d/light_3d.h`、`scene/3d/world_3d.h` |
| 渲染 API 与绘制流程 | `servers/rendering/rendering_server.cpp` |
| OpenGL 具体实现 | `servers/rendering/opengl/gl_rendering_device.cpp` |
| 着色算法 | `servers/rendering/shaders/builtin/{vert,frag}.glsl` |
| 窗口创建与原生句柄 | `platform/glfw/glfw_window.cpp`、`platform/window.h` |
| 路径与 `res://` 规则 | `core/io/path.cpp` |

### 4.2 一帧的数据流

```text
Application::run()
  -> Window::poll_event()
  -> SceneTree::tick(time)
       -> physics_process()   固定步长，最多补 8 步
       -> process()           可变 delta，上限 0.25s
       -> Viewport::render()
            -> RenderingServer::sync()   当前为空实现
            -> RenderingServer::draw()
                 -> 遍历 instances，构造 DrawItem
                 -> 绑定 builtin shader、相机/环境/灯光 uniform
                 -> 绑定 6 张纹理并提交 draw_call
  -> Window::swap_buffers()
```

资源上传**不经过** `sync()`：`MeshInstance3D`、`Light3D`、`World3D`、`Camera3D` 在 `NOTIFICATION_ENTER_TREE` 时直接调用 `RenderingServer` 创建并上传。新增资源类型时，要么沿用这个“进树时上传”模式，要么先实现真正的 `sync()` 脏资源队列。

---

## 5. 当前实现状态（改代码前必读）

### 5.1 已完成、可正常工作

- `Object` / `RefCounted` / `Ref<T>` / `RID` / `RIDStorage`，含 generation 防悬空句柄。
- 数学库与单元测试（向量、矩阵/变换、投影等）。
- `Path`/`FileAccess`，`res://`、`user://` 解析。
- `Resource`、`ResourceLoader`、`ResourceFormatLoader` 注册表；`ObjMeshLoader`、`MtlLoader`、`ImageTextureLoader`。
- OBJ 解析：`v/vn/vt/f`、`v/vt/vn`、负索引、按 `usemtl` 拆分 surface、缺法线自动计算、`compute_tangents()`。
- MTL 解析：`newmtl`、`Kd/Ka/Ks/Ke`、`Ns/Pr/Pm/d/Tr`、`map_Kd/map_Ka/map_Ns/map_Pm/norm/bump`、`-s`/`-bm`。
- `Mesh`（CPU 数据 + dirty RID）、`StandardMaterial`（albedo/metal/roughness/6 纹理槽/uv_scale/parallax）。
- `RenderingServer` + `RenderingStorage`：scene/environment/camera/light/mesh/instance/material/texture 的创建与更新。
- OpenGL 3.3 后端：VAO/VBO/EBO、shader 编译与 uniform、2D 纹理创建/绑定、清屏、绘制。
- PBR：GGX 法线分布、Smith 几何项、Schlick 菲涅尔、金属/非金属 F0、环境光；法线贴图 TBN；32 步视差贴图。
- 场景树：父子关系、enter/exit tree、process/physics_process 传播、固定物理步长与插值系数。
- 平台抽象：GLFW（默认）与 SDL3 两套窗口实现，OpenGL/Vulkan 驱动选择。

### 5.2 部分完成或占位

| 项目 | 现状 |
|---|---|
| Vulkan 后端 | `init/init_device/init_swapchain` 有较完整骨架；`create_mesh_surface`、`draw_call`、shader、纹理接口全部返回 0/空实现，不能渲染 |
| 资源缓存 | `ResourceCache` 已存在，但 `ResourceLoader::load()` **既不查缓存也不写入缓存**；`Resource` 没有 `set_url()`，`get_resource()` 用 `cache.at()` 未命中会抛异常 |
| `RenderingServer::sync()` | 空实现；资源上传发生在节点进树时 |
| `RenderingServer::free()` | 只处理 `Camera/Mesh/Instance`；Material/Texture/Scene/Environment/Light 的 GPU 释放未实现 |
| 资源释放 | 资源析构为空；`ImageTexture`/`Material` 的 GPU RID 生命周期未与资源生命周期绑定 |
| 多光源 | 只取 `scene_data->lights[0]`；只有方向光，无阴影、无点光/聚光、无 IBL |
| 材质 | `StandardMaterial::duplicate()` 返回空 Ref；`get_roughenss()` 是历史拼写；emission/specular 未接入 shader |
| 纹理 | `ImageTextureLoader` 直接使用 stb 解码后的通道数；`srgb` 仅按 `channels == 4` 猜测，MTL 加载器会按槽位覆盖，但直接 `load()` 的纹理可能 sRGB 标记错误 |
| 测试 | 仅 `orca-tests` 注册到 CTest；OBJ/resource 测试过期未编译；无 CI、无格式化/静态检查配置 |
| 场景功能 | `SceneTree::find_node()` 返回 nullptr；`SceneTree::destroy()` 为空；`Viewport::find_world_3d()` 返回 nullptr；`SceneTree` 不支持切换场景的完整清理 |
| 序列化/编辑器 | 完全没有 |

### 5.3 已知陷阱

1. **`RefCounted::unref()` 内部用 `delete this`，而对象可能由 `memnew`（malloc + placement new）分配。** 当前代码能跑，但这是分配器不匹配的潜在 UB。修 `Ref`/`memnew` 关系时要补 `test_ref_counted.cpp`。
2. **`Ref<T>::cast<U>()` 使用 `static_cast`，不做运行时类型检查。** 加载结果 cast 错类型会静默产生非法指针；新增资源类型时注意用 `dynamic_cast` 或校验。
3. **`ResourceLoader::load()` 返回的失败形式是空 `Ref`。** 调用方必须检查 `is_null()/is_valid()`；不要解引用空 Ref。
4. **`res://` 依赖进程当前目录。** 从仓库根运行会找 `E:/code/orca-engine/resources`，必然失败；测试/演示统一从 `build/` 运行。
5. **纹理槽顺序是硬编码的契约。** `RenderingServer` 的 `TextureSlotAlbedo..TextureSlotHeight` 与 `MaterialTextureSlot` 枚举、mesh_instance 上传顺序、shader 的 texture unit 0..5 必须保持一致。新增槽位要同步改这四处。
6. **场景层目前只在 `NOTIFICATION_ENTER_TREE` 上传一次。** 运行时修改 mesh 顶点、材质参数或贴图不会自动同步到渲染端；`NOTIFICATION_MATERIAL_CHANGE` 仍是 TODO。
7. **`MeshInstance3D` 只处理一次进树。** 退出时 `VisualInstance3D` 会释放 `instance`，但 mesh/material/texture 的 RID 不释放；重复 enter 会重新创建 instance，且 `ensure_surfaces()` 会向同一个 mesh RID 再次追加 surface。
8. **`RenderingServer::draw()` 对 `scene_data->env`、`scene_data->main_camera` 直接解引用。** 没有场景/相机时可能崩溃；新增测试或最小场景时要先创建 `World3D` 和 `Camera3D`。
9. **`StandardMaterial` 的默认值不是中性值**（`metallic = 1.0`、`roughness = 1.0`；`Color` 默认白色）。通过 `memnew(StandardMaterial)` 后仍应显式设置所有参数，避免依赖默认值。
10. **shader 只在启动时编译一次。** 修改 GLSL 后必须重启进程；`ShaderLoader` 的源码树回退只解决文件定位，不是热重载。
11. **`thirdparty/` 是预编译二进制。** 不要随意替换或升级 GLFW/SDL3/glad，除非同步验证 `Window` 与 `RenderingWindowInfo` 的原生句柄约定。

---

## 6. 编码约定

- **语言**：代码注释以中文为主，标识符用英文；错误信息多为英文，保持现状即可。
- **命名**：类型 `PascalCase`，函数/变量 `snake_case`，成员变量不加 `m_` 前缀，常量宏全大写；与 Godot 风格保持一致。
- **头文件**：统一 `#pragma once`；include 路径相对于模块根目录（如 `#include "math/vector3.h"`、`#include "3d/node3d.h"`、`#include "resource_loader.h"`），因为各模块把自身目录设为 PUBLIC include dir。
- **格式**：4 空格缩进，Allman 大括号（左括号独占一行），`if (...)` 带空格。
- **内存**：引擎对象用 `memnew`/`memdelete`；`Ref<T>` 管理 `RefCounted` 生命周期；不要混用 `new/delete` 与 `memnew/memdelete`。
- **RID**：0 表示无效句柄；新增服务器资源类型时在 `RIDType` 加枚举、在 `RenderingStorage` 加 `RIDStorage`、在 `RenderingServer::free()` 加释放分支。
- **错误处理**：资源加载失败返回空 `Ref` 并 `printf` 明确错误；渲染 API 对无效 RID 做空指针检查后静默返回（保持现有风格），不要抛异常（测试代码除外）。
- **添加源文件**：必须同步修改对应模块的 `CMakeLists.txt`；添加测试必须修改 `tests/CMakeLists.txt`。
- **不要提交生成物**：`build/`、`.vscode/`、`.idea/` 已被忽略；`build/resources` 不是版本化资源目录。

### 6.1 新增一种资源/格式的最小清单

1. 在 `resource/` 下定义资源类（继承 `Resource`，实现 `get_rid()`、`duplicate()`）。
2. 在 `resource/loaders/` 实现 `ResourceFormatLoader::format_extensions()`。当前约定是**带点的小写扩展名**（如 `.obj`、`.png`），因为 `Path::get_extension()` 返回带点后缀，而 `ResourceLoader` 只做小写化、不补点；新增格式必须与这个约定一致。
3. 在 `ResourceLoader::create_singleton()` 注册 loader。
4. 在对应 `CMakeLists.txt` 增加源文件。
5. 如需渲染，增加 `RenderingStorage` 数据结构 + `RenderingServer` API + `RenderingDevice` 接口（GL 实现）。
6. 在场景节点进树路径或 `sync()` 中上传。
7. 补 CPU 侧单元测试；GPU 侧用手动集成目标验证并在 PR/说明中写清运行方式。

---

## 7. 改动前检查清单

- [ ] 已确认改动属于哪个模块，没有破坏第 2.1 节的依赖方向。
- [ ] 新增的 `.cpp/.h` 已加入对应 `CMakeLists.txt`；新增测试已加入 `tests/CMakeLists.txt`。
- [ ] 从干净的 `build/` 能完成 `cmake -S . -B build -G "MinGW Makefiles"` 与 `cmake --build build --parallel`。
- [ ] `ctest --test-dir build --output-on-failure` 通过；需要窗口/GPU 的验证已手动运行并记录结果。
- [ ] 若改了纹理槽、shader uniform、RID 类型或资源生命周期，已检查第 5.3 节的关联点。
- [ ] 若改了运行方式、目录结构、资源约定，已同步更新本文件。
