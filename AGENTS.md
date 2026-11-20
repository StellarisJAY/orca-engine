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
| 图形 API（旧设备） | OpenGL 3.3 Core（默认、可用） | 仅旧 `RenderingDevice` 使用；新 RHI 已移除 OpenGL 适配层。通过 `thirdparty/glad` 加载现代入口点；GLSL `#version 330 core` |
| 图形 API（新 RHI） | Vulkan | V0/V1/V2/V4 已完成：构建、唯一句柄、异常处理、`Format ↔ vk::Format` 映射与共享 RHI 描述校验；instance/device/swapchain 初始化可跑通；V2 已完成纹理显存分配/绑定、内容上传（staging + layout transition）与 mipmap 生成，缓冲区显存分配/上传（`create_buffer`/`queue_write_buffer` + staging 池），以及纹理视图（2D/深度/cubemap + mip 子范围）与采样器（含 `compare_op`）；V4 完成 SPIR-V 工具链（glslc 编译 `shaders-vulkan/`）与 `create_shader_module` 的源类型/入口点/stage 校验；V3-1 在 `init_swapchain` 补齐交换链 image view、每图像深度、默认 render pass/framebuffer 与 acquire/present 同步对象；I-04 完成 `resize_swapchain`（oldSwapchain 重建、零尺寸推迟）；I-31 完成 `encoder_begin_render_pass` 的 render pass 创建（完整附件/subpass/dependency、尺寸推导、默认帧缓冲分支，pass 内部对象随 command buffer 释放）；V5-1/V5-2 完成 bind group layout（`VkDescriptorSetLayout`）、bind group（每个绑定组独占 descriptor pool + set + `vkUpdateDescriptorSets`）与 pipeline layout（`VkPipelineLayout`，`create_render_pipeline` 使用传入的 `pipeline_layout`）；V5-3 完成 `create_render_pipeline` 的完整状态（顶点输入、颜色混合与写掩码、深度、采样数，以及每管线一个用于兼容性检查的 render pass）；V6-2 完成 pass 级绑定（`pass_set_bind_group` 含动态偏移、`pass_set_vertex_buffer`、`pass_set_index_buffer`、`pass_set_viewport` 同时派生并裁剪 scissor，render pass 开始时设默认 viewport/scissor）；V6-2b/V6-3 完成 `pass_draw`/`pass_draw_indexed`、默认帧缓冲 acquire（`acquire_next_swapchain_image`）、`queue_submit`（帧 fence + semaphore 链）与 `present`（vkQueuePresentKHR + 帧推进），一帧端到端冒烟 validation 无 VUID。仍缺平台 resize 事件转发（V3-5）、按帧复用 encoder（V6-1）、延迟销毁（V6-4）与接入 `RenderingServer`（V7） |
| 窗口/输入 | GLFW（默认）或 SDL3 | 二选一，预编译库只提供 Windows/MinGW-w64 版本 |
| 图像解码 | `stb_image`（单头文件） | 由 `ImageTextureLoader` 使用 |
| glTF 解析 | `cgltf`（单头文件） | `.gltf/.glb` 的 JSON/GLB、bufferView/accessor；`#define CGLTF_IMPLEMENTATION` 在 `gltf_loader.cpp` 中定义 |
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
        ├── orca-resource    （依赖 object、math、core、stb_image、cgltf；禁止依赖 rendering）
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
```

可切换的后端/平台：

```powershell
# SDL3 窗口后端（需要同时关闭 GLFW）
cmake -S . -B build -DORCA_PLATFORM_GLFW=OFF -DORCA_PLATFORM_SDL3=ON
# Vulkan 后端（V0/V1/V2/V4 可构建、可初始化、可创建 SPIR-V shader 模块，V3-1 已补交换链
# 视图/深度/默认帧缓冲/同步对象，I-04 已补 resize 重建，I-31 已补 render pass 创建，
# V5-1/V5-2/V5-3 已补绑定组布局/绑定组/管线布局与完整管线状态；V6-2 已补 bind group/动态偏移、
# 顶点/索引缓冲区绑定与 viewport/scissor（pass 开始时设默认 viewport/scissor）；V6-2b/V6-3 已补
# pass_draw/pass_draw_indexed、默认帧缓冲 acquire、queue_submit（帧 fence + semaphore 链）与
# present，单帧端到端可绘制并呈现（validation 无 VUID），需要 Vulkan SDK 的 glslc 把
# shaders-vulkan/ 编译成 .spv，详见 docs/vulkan_rhi_checklist_v2.md）
cmake -S . -B build -DORCA_RENDERER_OPENGL=OFF -DORCA_RENDERER_VULKAN=ON
```

注意：

- GLFW 与 SDL3 **只能启用一个**，否则 CMake 直接报错。
- 至少启用一个图形后端，否则 CMake 直接报错。
- `ORCA_RENDERER_OPENGL` 只编译旧 `RenderingDevice` 的 OpenGL 实现（`RenderingServer` 仍在使用）；新 RHI（`RenderingDeviceRefactor`）只保留 Vulkan，原 Refactor 版 OpenGL 适配层已移除，因此不再提供全局 `ORCA_RENDERER_DEFAULT`：旧设备在编译了 GL 时默认 OpenGL，新 RHI 固定 Vulkan。
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

shader 是例外：`ShaderLoader` 会先找 `build/resources/shaders/`，找不到再回退到源码树 `servers/rendering/shaders/`，所以改 GLSL 后通常无需手动拷贝，但**仍需重启程序**才能重新编译 shader 程序。Vulkan 专用的 `shaders-vulkan/*.glsl` 由构建期 `glslc` 编译到 `build/resources/shaders-vulkan/*.spv`（目标 `orca-shader-spirv`），没有源码树回退。

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
| `orca-tests` | doctest 单元测试：math / object / os / io / resource（OBJ + glTF + 贴图解码 + PackedScene + Mesh 状态 + 天空盒校验）/ scene（含 Node3D 变换）/ servers（天空盒立方体、RHI 描述校验含 buffer desc/write 区间、view 范围解析、sampler/compare 约束、管线描述与 shader 模块 SPIR-V 校验；Vulkan 构建时另含 Format/ImageUsage/BufferUsage/CompareOp、顶点格式/blend/写掩码/采样数与图像布局映射） | 是，名称为 `orca-tests` |
| `orca-platform-window-test` | 创建窗口并初始化 OpenGL 设备，手动运行 | 否 |
| `orca-vulkan-init-test` | Vulkan `init → init_device → init_swapchain` 冒烟（含交换链 image view/每图像深度/默认 render pass+framebuffer/同步对象，重复 `init_swapchain` 的 oldSwapchain 重建，以及 `resize_swapchain` 的零尺寸推迟与 800x600 → 640x480 重建），并录制 color+depth 与 depth-only 两个 render pass（`desc.width/height=0` 时由附件推导、6 条非法 desc 拒绝路径），创建/销毁颜色/深度/cubemap 纹理验证显存分配与视图（含 cubemap mip 子范围、非法视图拒绝），上传纹理内容（含带行距的部分区域）与生成 cubemap mipmap，创建默认/compare 采样器，创建顶点/索引/uniform 缓冲并写入，创建绑定组布局（静态/动态 UBO + 2D/深度/cubemap + 独立 sampler）、绑定组与管线布局并验证 12 条拒绝路径，创建两条完整 graphics pipeline（vertex input / blend / write mask / depth / samples，含 3 条拒绝路径）并把其中一条绑定到附件格式一致的显式 render pass，绑定 set 0（静态 UBO + 动态 UBO）/顶点/索引缓冲并设置部分移出 render area 的 viewport（含 9 条 pass 绑定拒绝路径），再用颜色格式匹配交换链的管线录制一帧默认帧缓冲（begin 时 acquire 交换链图像）→ `pass_draw_indexed` + `pass_draw`（含 4 条 draw 拒绝路径、同帧重复提交拒绝）→ `queue_submit` → `present` → `queue_wait_idle` → 销毁，最后从 `.spv` 创建 triangle/builtin-skybox shader 模块并验证拒绝路径；设置 `ORCA_VULKAN_INIT_TEST_EXIT_AFTER_INIT=1` 可只初始化后退出 | 否 |
| `orca-application-test` | 演示程序（加载资源、渲染场景），手动运行 | 否 |
| `orca-app-test-sponza` | Sponza glTF 场景演示 | 否 |

> `tests/resource/test_obj_loader.cpp` 已重写并加入 `orca-tests`，覆盖四边形、负索引、缺失/混合法线、空面过滤、UV/兜底切线方向，以及 `ResourceLoader` 的 URL 缓存。修改 OBJ/资源加载时请同步更新这些用例。
> 另有 `tests/core/io/test_file_access_and_path.cpp`（二进制读取、`decode_uri`）、`tests/resource/test_image_texture_loader.cpp`（内存解码 PNG、sampler setter）、`tests/resource/test_mesh.cpp`（RID dirty 状态）、`tests/resource/test_scene.cpp`（`PackedScene` 的 `add_node/add_root/duplicate/validate`）、`tests/resource/test_gltf_loader.cpp`（.gltf/.glb、节点层级、多 primitive、外部 bin、内嵌/data URI 图片、材质与 MR 拆分）、`tests/resource/test_skybox.cpp`（六面齐全/尺寸/通道/正方形/像素长度校验、GL 面顺序、逐面行序翻转）、`tests/scene/test_node3d_transform.cpp`（`set_transform`/负缩放/父子变换），修改对应实现时同步维护。
> `test_gltf_loader.cpp` 在测试运行时自行生成 glTF/GLB 与 PNG 字节（base64、PNG、GLB 构造都在用例内），不依赖 `build/resources/` 中的样例资源。
> 新增 `tests/servers/test_rhi_validation.cpp`（后端无关的纹理/缓冲区/视图（含范围解析）/采样器（含 lod/compare 约束）/绑定组布局（唯一性/visibility/动态偏移约束）/绑定组解析（缺项、类型、区间、`min_binding_size`、view 维度/aspect/格式）/管线布局/管线描述校验（含纯深度 pass 仍要求 fragment 模块），以及 shader 模块的 source_kind/SPIR-V 头与 entry point/stage 校验）与 `tests/servers/test_vk_format.cpp`（仅 Vulkan 构建：Format↔vk::Format 双向映射、ImageUsage/BufferUsage 位映射、CompareFunction↔vk::CompareOp、顶点格式/blend 因子与操作/颜色写掩码/采样数映射）；`tests/servers/test_vk_image_layout.cpp`（仅 Vulkan 构建：aspect / resting layout / barrier 映射 / mip 尺寸）。修改 `rhi_validation.*`、`vk_format.*` 或 `vk_image_layout.*` 时同步维护。

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
│  │  ├─ file_access.h/.cpp        # 基于 FILE* 的文件读取，含二进制 get_buffer/read_file_bytes
│  │  └─ dir_access.h/.cpp
│  └─ os/
│     ├─ memory.h                  # memnew / memdelete / memnew_arr / 分配计数
│     └─ time.h/.cpp               # Time::now() 等
├─ thirdparty/
│  ├─ doctest/                     # 测试框架（头文件）
│  ├─ stbimage/stb_image.h         # 图像解码
│  ├─ cgltf/cgltf.h                # glTF 2.0 / GLB 解析（实现由 gltf_loader.cpp 提供）
│  ├─ glad/include/glad/gl.h       # OpenGL 入口点
│  ├─ glfw/                        # GLFW 头文件 + MinGW 预编译静态库
│  ├─ sdl3/                        # SDL3 头文件 + MinGW 预编译动态库
│  └─ CMakeLists.txt               # 以 INTERFACE / IMPORTED 目标暴露第三方库
├─ servers/rendering/              # 渲染服务器与图形后端
│  ├─ rendering_device.h/.cpp      # 旧后端抽象接口 + 工厂（只保留 OpenGL 实现）
│  ├─ rendering_device_refactor.h/.cpp # 新 RHI（WebGPU 风格）接口 + 工厂（只保留 Vulkan 实现）
│  ├─ rendering_server.h/.cpp      # 高层 API：scene/camera/light/mesh/material/texture
│  ├─ rendering_storage.h/.cpp     # 各资源的 CPU 端描述 + RIDStorage 实例
│  ├─ rhi_validation.h/.cpp        # 后端无关的 RHI 描述校验 + 视图范围解析（Vulkan 使用）
│  ├─ shader_loader.h/.cpp         # 读取 GLSL（res://shaders + 源码回退）与已编译 .spv（res://shaders-vulkan）
│  ├─ skybox_cube.h/.cpp           # 天空盒用的 8 顶点立方体几何（仅位置 + 索引）
│  ├─ opengl/
│  │  ├─ gl_rendering_device.h/.cpp# 旧 RenderingDevice 的 OpenGL 实现（VAO/VBO、shader、纹理）
│  │  └─ gl_loader.cpp             # glad 实现（GLAD_GL_IMPLEMENTATION）的唯一翻译单元
│  ├─ vulkan/
│  │  ├─ vk_rendering_device.h/.cpp# Vulkan 私有对象、present（vkQueuePresentKHR + 帧推进）、帧状态（frame_fence_reset/frame_submitted）、生命周期
│  │  ├─ vk_rendering_device_init.cpp # instance/surface/物理设备/逻辑设备
│  │  ├─ vk_swapchain.cpp          # swapchain + image view/每图像深度/默认 render pass/framebuffer/同步对象 + acquire_next_swapchain_image（V3-2）+ resize 重建（V3-1/I-04）
│  │  ├─ vk_internal.h             # Vulkan 资源对象数据结构（视图记录父纹理/布局/尺寸；pass 记录当前管线/active/索引缓冲/render area；command buffer 记录 submitted）；vk_render_pass_util 统一 dependency 模板
│  │  ├─ vk_format.h/.cpp          # Format ↔ vk::Format 双向映射 + ImageUsage/BufferUsage/CompareOp + 顶点格式/blend/写掩码/采样数映射
│  │  ├─ vk_memory.h/.cpp          # 显存类型选择 + 独占分配（G-06 自建分配器）
│  │  ├─ vk_image_layout.h/.cpp    # 布局/aspect/resting layout/barrier 映射（上传与 mipmap 共用）
│  │  ├─ vk_resource.cpp           # shader（SPIR-V 校验/创建）、image/view/sampler 创建与销毁
│  │  ├─ vk_buffer.cpp             # buffer 创建/写入/销毁 + 设备级 staging 池
│  │  ├─ vk_bind_group.cpp         # bind group layout / bind group（独占 descriptor pool + set）/ pipeline layout 创建与销毁（V5-1/V5-2）
│  │  ├─ vk_pipeline.cpp           # graphics pipeline 完整状态（顶点输入/混合/深度/采样数）；使用传入的 pipeline_layout；持有每管线兼容 render pass 并记录顶点缓冲数量（V5-3，dependency 与录制侧统一）
│  │  └─ vk_command.cpp            # 命令池/录制/render pass（完整附件/subpass/dependency/默认帧缓冲 + 默认 viewport/scissor）/resolve_recording_pass/pass 绑定/pass_draw(_indexed)/queue_submit（帧 fence + semaphore 链）；延迟销毁待补
│  ├─ shaders/
│     ├─ builtin/vert.glsl         # 内置顶点着色器
│     ├─ builtin/frag.glsl         # GGX/Smith/Schlick、法线贴图、视差贴图
│     ├─ builtin-skybox/            # 天空盒：用顶点方向直接采样 samplerCube，pos.xyww 贴远平面
│     └─ dummy/                    # 占位 shader
│  └─ shaders-vulkan/              # Vulkan 专用 GLSL 450 变体（显式 set/binding），构建期由 glslc 编成 .spv
│     ├─ triangle/                 # V4 工具链冒烟：无描述符的彩色三角形
│     └─ builtin-skybox/           # 天空盒 Vulkan 变体（set 0 UBO + set 1 samplerCube）
├─ platform/                       # 窗口与输入抽象
│  ├─ window.h/.cpp                # Window 抽象 + create() 工厂
│  ├─ glfw/glfw_window.h/.cpp      # GLFW 实现（默认）
│  └─ sdl3/sdl3_window.h/.cpp      # SDL3 实现
├─ resource/                       # CPU-first 资源层（不依赖渲染服务器）
│  ├─ resource.h/.cpp              # Resource 基类（url、get_rid、duplicate）
│  ├─ resource_cache.h/.cpp        # url -> Resource* 弱引用缓存（ResourceLoader 已接入）
│  ├─ resource_loader.h/.cpp       # 单例 + 扩展名 -> loader 注册表
│  ├─ loaders/
│  │  ├─ obj_loader.h/.cpp         # .obj -> Mesh，支持 mtllib/usemtl、负索引、切线
│  │  ├─ mtl_loader.h/.cpp         # .mtl -> MaterialLibrary，解析 Kd/Ns/Pr/Pm/贴图
│  │  ├─ gltf_loader.h/.cpp        # .gltf/.glb -> PackedScene（含 CGLTF_IMPLEMENTATION）
│  │  └─ image_texture_loader.*    # .png/.jpg 等 -> ImageTexture（stb_image）
│  ├─ scene/scene.h/.cpp           # PackedScene：与格式无关的场景节点树 CPU 描述
│  ├─ mesh/mesh.h/.cpp             # Mesh::Surface、CPU 顶点/索引、RID + dirty
│  ├─ material/material.h/.cpp     # StandardMaterial、6 个纹理槽、标量参数
│  ├─ material/material_library.*  # .mtl 中多材质的容器
│  ├─ texture/image_texture.h      # CPU 像素 + RID + srgb/mipmap/filter 标志
│  └─ environment/
│     ├─ environment_3d.*          # 环境资源壳（环境光 + 天空盒）
│     └─ skybox.h/.cpp             # CubeMapSkybox：六面 ImageTexture、校验与立方体贴图拼装
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
│  ├─ CMakeLists.txt               # 测试可执行目标（orca-tests / window / vulkan-init / application）
│  ├─ core/math/…                  # 数学单元测试
│  ├─ core/object/…                # RefCounted、RID 测试
│  ├─ core/os/…                    # Memory、Time 测试
│  ├─ scene/…                      # Node3D 全局变换、场景树
│  ├─ resource/test_obj_loader.cpp # OBJ 解析、按需切线、ResourceLoader 缓存测试
│  ├─ resource/test_image_texture_loader.cpp # 内存解码、sampler setter
│  ├─ resource/test_mesh.cpp       # Mesh RID dirty 状态
│  ├─ resource/test_scene.cpp      # PackedScene add_node/duplicate/validate
│  ├─ resource/test_gltf_loader.cpp # glTF/GLB 解析、材质、MR 拆分、节点层级
│  ├─ scene/test_node3d_transform.cpp # Node3D TRS/负缩放/父子变换
│  ├─ servers/test_skybox_cube.cpp # 天空盒立方体几何
│  ├─ servers/test_rhi_validation.cpp # 后端无关的 RHI 描述校验（无窗口/GPU）
│  ├─ servers/test_vk_format.cpp   # Vulkan Format/ImageUsage/BufferUsage/CompareOp/顶点格式/blend/写掩码/采样数映射纯逻辑（仅 Vulkan 构建）
│  ├─ servers/test_vulkan_init.cpp # Vulkan instance/device/swapchain（含 resize 重建）+ render pass 录制（color+depth/depth-only）+ 纹理/视图/采样器 + 上传/mipmap + buffer + 绑定组/管线布局 + 完整管线创建/绑定 + pass 绑定（bind group+动态偏移、顶点/索引缓冲、viewport+scissor，含拒绝路径）+ 默认帧缓冲 acquire + pass_draw(_indexed) + queue_submit + present 端到端（含拒绝路径）+ SPIR-V shader 模块冒烟（手动）
│  ├─ platform/test_window.cpp     # 窗口+设备初始化集成测试
│  ├─ application/test_application.cpp # 可交互演示程序
│  └─ application/test_scene_sponza.cpp # Sponza glTF 场景演示
├─ docs/
│  ├─ resource_module_roadmap.md   # 资源模块设计/路线图（部分已完成）
│  ├─ material_texture_plan.md     # 材质纹理方案（部分已完成）
│  ├─ vulkan_rhi_checklist_v2.md   # Vulkan 适配层清单（当前状态 + V0–V7 进度；V0/V1/V2/V4 与 V3-1/V3-3/I-04/I-31/V5-1/V5-2/V5-3/V6-2/V6-2b/V6-3 已完成，44 项接口全部 ✅）
│  ├─ vulkan_rhi_checklist.md      # 历史审计基线（2026-11-16，不再维护）
│  └─ webgpu-draw-diagram.svg      # RHI 绘制流程示意图
└─ build/                          # CMake 生成目录，已忽略；运行资源也在这里
```

### 4.1 关键入口速查

| 想改什么 | 先看这些文件 |
|---|---|
| 程序启动、初始化顺序、主循环 | `application/application.cpp` |
| 一帧的调度（物理/逻辑/渲染） | `scene/scene_tree.cpp`、`scene/viewport.cpp` |
| 资源格式加载 | `resource/resource_loader.cpp`、`resource/loaders/*` |
| 场景型资源（glTF/.glb） | `resource/loaders/gltf_loader.cpp`、`resource/scene/scene.*`（`PackedScene`） |
| 材质参数/纹理槽 | `resource/material/material.h`、`servers/rendering/rendering_storage.h` |
| 场景对象上传到渲染端 | `scene/3d/mesh_instance_3d.cpp`、`scene/3d/light_3d.h`、`scene/3d/world_3d.h` |
| 渲染 API 与绘制流程 | `servers/rendering/rendering_server.cpp` |
| 新 RHI（Vulkan）实现 / 描述校验 | `servers/rendering/rendering_device_refactor.*`、`servers/rendering/vulkan/*`、`servers/rendering/rhi_validation.*` |
| 旧 RenderingDevice 的 OpenGL 具体实现 | `servers/rendering/opengl/gl_rendering_device.cpp` |
| 着色算法 | `servers/rendering/shaders/builtin/{vert,frag}.glsl` |
| Vulkan shader / SPIR-V | `servers/rendering/shaders-vulkan/`、`servers/rendering/CMakeLists.txt`（`orca-shader-spirv`）、`shader_loader.cpp::load_spirv` |
| 天空盒 | `resource/environment/skybox.cpp`、`scene/3d/world_3d.cpp`、`servers/rendering/shaders/builtin-skybox/` |
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
                 -> 最后画天空盒（pos.xyww 贴远平面，默认深度函数为 GL_LEQUAL）
  -> Window::swap_buffers()
```

资源上传**不经过** `sync()`：`MeshInstance3D`、`Light3D`、`World3D`、`Camera3D` 在 `NOTIFICATION_ENTER_TREE` 时直接调用 `RenderingServer` 创建并上传。新增资源类型时，要么沿用这个“进树时上传”模式，要么先实现真正的 `sync()` 脏资源队列。

---

## 5. 当前实现状态（改代码前必读）

### 5.1 已完成、可正常工作

- `Object` / `RefCounted` / `Ref<T>` / `RID` / `RIDStorage`，含 generation 防悬空句柄。
- 数学库与单元测试（向量、矩阵/变换、投影等）。
- `Path`/`FileAccess`，`res://`、`user://` 解析；`FileAccess::get_buffer`/`read_file_bytes` 支持二进制读取（`\0` 安全），`Path::decode_uri` 处理百分号转义。
- `ImageTextureLoader::load_from_memory` 可从内存解码贴图（供 glTF 的 data URI / GLB 内嵌图片使用），`ImageTexture` 提供 `set_repeat/set_linear_filter/set_gen_mipmap`，`load()` 与内存路径共用同一解码函数。
- `Basis::get_scale/determinant/get_rotation_quaternion` 与 `Quaternion(const Basis&)` 支持从矩阵反解 TRS；`Node3D::set_transform/get_transform` 可直接设置任意局部变换（含负缩放，镜像约定归到 X 轴）。
- `Resource`、`ResourceLoader`、`ResourceFormatLoader` 注册表；`ObjMeshLoader`、`MtlLoader`、`ImageTextureLoader`、`GLTFLoader`。
- glTF 2.0 加载：`GLTFLoader` 把 `.gltf/.glb` 解析为**与格式无关的场景资源 `PackedScene`**（节点树 + 每节点 `Ref<Mesh>` 等 payload），支持节点 TRS/matrix、多 primitive → 多 Surface、u8/u16/u32 索引、TRIANGLE_STRIP/FAN、无索引自动生成、缺失法线补算、UV `v = 1 - v` 翻转。
- glTF 材质：baseColor/metallic/roughness 因子（linear → sRGB 编码）、baseColor/normal/occlusion 贴图、`metallicRoughnessTexture` 在 CPU 侧拆 G/B 为两张单通道贴图、sampler 映射；图片支持外部文件、GLB 内嵌 bufferView、`data:` URI 三种来源；材质/灯光类扩展降级警告，动画/skin/morph target 忽略。
- OBJ 解析：`v/vn/vt/f`、`v/vt/vn`、负索引、按 `usemtl` 拆分 surface、按 surface 补缺失法线、有 UV 时用 UV 解算切线（无 UV 时生成基于法线的兜底基）。切线不能按材质判断，因为材质可能在加载后由 `MeshInstance3D` 注入。
- MTL 解析：`newmtl`、`Kd/Ka/Ks/Ke`、`Ns/Pr/Pm/d/Tr`、`map_Kd/map_Ka/map_Ns/map_Pm/norm/bump`、`-s`/`-bm`。
- `Mesh`（CPU 数据 + dirty RID）、`StandardMaterial`（albedo/metal/roughness/6 纹理槽/uv_scale/parallax）。
- `RenderingServer` + `RenderingStorage`：scene/environment/camera/light/mesh/instance/material/texture 的创建与更新。
- OpenGL 3.3 后端：VAO/VBO/EBO、shader 编译与 uniform、2D/立方体纹理创建与绑定、清屏、绘制。
- 天空盒：`CubeMapSkybox` 六面 `ImageTexture` 在 `World3D` 进树时校验并拼成 cubemap 上传，`builtin-skybox` shader 用顶点方向直接采样、`pos.xyww` 贴远平面，在实例之后绘制。
- PBR：GGX 法线分布、Smith 几何项、Schlick 菲涅尔、金属/非金属 F0、环境光；法线贴图 TBN；32 步视差贴图。
- 颜色管线：sRGB 贴图由 `GL_SRGB8*` 硬件解码、颜色类 uniform 由 `Color::linear()` 转线性 → 光照在线性空间计算 → fragment shader 用 `linear_to_srgb()` 编码输出到默认帧缓冲（`builtin/frag.glsl` 与 `builtin-skybox/frag.glsl`）。
- 场景树：父子关系、enter/exit tree、process/physics_process 传播、固定物理步长与插值系数。
- 平台抽象：GLFW（默认）与 SDL3 两套窗口实现，OpenGL/Vulkan 驱动选择。

### 5.2 部分完成或占位

| 项目 | 现状 |
|---|---|
| Vulkan 后端 | V0/V1/V2/V4 已完成：`vk_command.cpp` 进入构建、唯一句柄、异常处理、Format↔vk::Format 与 ImageUsage 映射、共享 RHI 描述校验；`init/init_device/init_swapchain` 可跑通（实测交换链 800x600、BGRA8UnormSrgb、validation 无 error），V3-1 补齐交换链 image view/每图像深度（D32Sfloat）/默认 render pass+framebuffer/acquire-present 同步对象，重复 `init_swapchain` 走 oldSwapchain 重建；I-04 完成 `resize_swapchain`（复用 oldSwapchain 路径，零尺寸推迟，实测 800x600 → 640x480）。V2 完成纹理显存分配/绑定（`vk_memory.*`）、内容上传（`queue_write_texture` staging + layout transition）、mipmap 生成（`generate_mip_map`）、缓冲区分配/上传（`vk_buffer.cpp`：`create_buffer` + 复用 staging 池 + `vkCmdCopyBuffer`）、纹理视图（`create_texture_view`：2D/深度/cubemap + mip 子范围）与采样器（`create_sampler`，含 `compare_op`）。V4 完成 SPIR-V 工具链：`shaders-vulkan/` 由 glslc 编译到 `build/resources/shaders-vulkan/*.spv`，`ShaderLoader::load_spirv` 读取，`create_shader_module` 校验 source_kind/长度对齐/magic/版本/入口点/stage。I-31 完成 `encoder_begin_render_pass` 的 render pass 创建：显式附件填全 format/samples/load-store/initial+final layout，配 subpass 与两道 dependency，framebuffer 尺寸由附件推导、render area 取 `desc.width/height`；默认帧缓冲分支复用交换链缓存对象并在 begin 时惰性 acquire 交换链图像（V3-2）。render pass/framebuffer 归 command buffer 所有，`destroy_command_buffer` 统一释放。V5-1/V5-2 完成绑定组布局（`VkDescriptorSetLayout`）、绑定组（每个绑定组独占 descriptor pool + set + `vkUpdateDescriptorSets`）与管线布局（`VkPipelineLayout`）；V5-3 完成 `create_render_pipeline` 的完整状态（顶点输入/颜色混合与写掩码/深度/采样数）与每管线兼容 render pass，管线可在附件格式一致的显式 pass 中绑定。V6-2 完成 pass 级绑定（bind group + 动态偏移、顶点/索引缓冲区、viewport + 派生 scissor，pass 开始时设默认 viewport/scissor）；V6-2b/V6-3 完成 `pass_draw`/`pass_draw_indexed`（校验管线/索引缓冲/索引区间/pass 状态）、`queue_submit`（帧 fence + image_available/render_finished semaphore 链）与 `present`（vkQueuePresentKHR + 帧推进），单帧端到端冒烟 validation 无 VUID。仍缺平台 resize 事件转发（V3-5）、按帧复用 encoder（V6-1）、帧 fence 之后的延迟销毁（V6-4/D-15），且尚未接入 `RenderingServer`（V7） |
| `RenderingDeviceRefactor`（新 RHI） | 只保留 Vulkan 适配器；原 Refactor 版 OpenGL 适配层已清理（`gl_rendering_device_refactor.*`、`gl_internal.h`、`gl_resources.cpp`、`gl_command.cpp`、`gl_executor.*` 已删除）。Vulkan 适配器：V0/V1 完成构建与格式/校验，V2 完成纹理显存、内容上传、mipmap、缓冲区创建/上传、视图与采样器，V5-1/V5-2 完成绑定组布局/绑定组/管线布局（含共享校验与 descriptor pool），V5-3 完成完整管线状态与 render pass 兼容性（每管线一个兼容 render pass），V6-2 完成 pass 级绑定命令（bind group/动态偏移、顶点/索引缓冲区、viewport/scissor），V6-2b/V6-3 完成 draw、默认帧缓冲 acquire、queue_submit 与 present；资源、管线、录制、绘制、提交与呈现都已走到 driver 边界，单帧端到端冒烟通过（validation 无 VUID）。剩余工程化缺口：V3-5 平台接线、V6-1 按帧复用 encoder、V6-4/D-15 延迟销毁。共享 `rhi_validation.*` 只由 Vulkan 的 `create_*` 调用；`RenderingServer` 仍走旧 `RenderingDevice` |
| 资源缓存 | `ResourceLoader::load()` 已接入 `ResourceCache`（弱引用、URL 键、析构自动摘除）；同一 URL 的 Mesh/MTL/纹理返回同一实例。纹理 sRGB 不一致时 MTL 加载器会复制一份，避免共享实例被改写 |
| `RenderingServer::sync()` | 空实现；资源上传发生在节点进树时 |
| `RenderingServer::free()` | 只处理 `Camera/Mesh/Instance`；Material/Texture/Scene/Environment/Light 的 GPU 释放未实现 |
| 资源释放 | 资源析构为空；`ImageTexture`/`Material` 的 GPU RID 生命周期未与资源生命周期绑定 |
| 多光源 | 只取 `scene_data->lights[0]`；只有方向光，无阴影、无点光/聚光、无 IBL |
| 材质 | `StandardMaterial::duplicate()` 返回空 Ref；`get_roughenss()` 是历史拼写；emission/specular 未接入 shader |
| 天空盒 | 只有 cubemap 天空盒（无 HDR/等距柱状图、无 IBL/环境反射）；六面必须齐全、正方形、同尺寸同通道，否则 `World3D::ensure_skybox` 打印错误并整块跳过；天空盒不参与光照 |
| 纹理 | `ImageTextureLoader` 直接使用 stb 解码后的通道数；`srgb` 默认 false，由材质/MTL 加载器按槽位覆盖，直接 `load()` 得到的纹理需要调用方自己标记 sRGB |
| glTF / 场景资源 | `PackedScene` 与 `GLTFLoader` 已完成解析与材质/贴图；**尚未实现** `instantiate_scene()`（阶段 3），加载结果还不能直接变成 `Node3D` 树。不支持动画/skin/morph/透明/emissive 贴图/多 UV；`KHR_texture_transform` 的贴图会被跳过；多 scene 只加载默认 scene；glTF 图片直接解码，不复用 `ResourceLoader` 的纹理缓存 |
| 测试 | `orca-tests` 已包含 OBJ/glTF loader、PackedScene、贴图内存解码、Mesh RID 状态、二进制 IO、Node3D 变换、天空盒校验与拼装、RHI 描述校验（含 buffer 描述与写入区间、纹理视图范围解析、sampler lod/compare 约束、管线描述与 fragment 必需）用例；Vulkan 构建另含 Format/ImageUsage/BufferUsage/CompareOp、顶点格式/blend/写掩码/采样数与布局映射用例；无 CI、无格式化/静态检查配置 |
| 场景功能 | `SceneTree::find_node()` 返回 nullptr；`SceneTree::destroy()` 为空；`Viewport::find_world_3d()` 返回 nullptr；`SceneTree` 不支持切换场景的完整清理 |
| 序列化/编辑器 | 完全没有 |

### 5.3 已知陷阱

1. **`RefCounted::unref()` 内部用 `delete this`，而对象可能由 `memnew`（malloc + placement new）分配。** 当前代码能跑，但这是分配器不匹配的潜在 UB。修 `Ref`/`memnew` 关系时要补 `test_ref_counted.cpp`。
2. **`Ref<T>::cast<U>()` 使用 `static_cast`，不做运行时类型检查。** 加载结果 cast 错类型会静默产生非法指针；新增资源类型时注意用 `dynamic_cast` 或校验。
3. **`ResourceLoader::load()` 返回的失败形式是空 `Ref`。** 调用方必须检查 `is_null()/is_valid()`；不要解引用空 Ref。
4. **`res://` 依赖进程当前目录。** 从仓库根运行会找 `E:/code/orca-engine/resources`，必然失败；测试/演示统一从 `build/` 运行。
5. **纹理槽顺序是硬编码的契约。** `RenderingServer` 的 `TextureSlotAlbedo..TextureSlotHeight` 与 `MaterialTextureSlot` 枚举、mesh_instance 上传顺序、shader 的 texture unit 0..5 必须保持一致。新增槽位要同步改这四处。
6. **场景层目前只在 `NOTIFICATION_ENTER_TREE` 上传一次。** 运行时修改 mesh 顶点、材质参数或贴图不会自动同步到渲染端；`NOTIFICATION_MATERIAL_CHANGE` 仍是 TODO。
7. **共享 Mesh 的 surface 只会上传一次。** `MeshInstance3D::upload_if_needed()` 只在 `Mesh::is_rid_dirty()` 时 `add_mesh_surface()`，完成后 `mark_rid_clean()`。注意 `Mesh::set_rid()` 不再自动清除 dirty（创建 RID ≠ 上传完成）；`Mesh::mark_rid_dirty()` 才会让下次进树重新上传。退出场景树时 mesh/material/texture 的 RID 仍不释放，重复 enter 会重新创建 instance。
8. **`Vector3::is_normalized()` 使用近似比较。** 浮点误差下 `(1,1,1)/sqrt(3)` 这类单位向量的 `length_squared()` 不精确等于 1；改成精确比较会让 `Basis::set_axis_angle` 静默忽略合法轴。
9. **`RenderingServer::draw()` 对 `scene_data->env`、`scene_data->main_camera` 直接解引用。** 没有场景/相机时可能崩溃；新增测试或最小场景时要先创建 `World3D` 和 `Camera3D`。
10. **`StandardMaterial` 的默认值不是中性值**（`metallic = 1.0`、`roughness = 1.0`；`Color` 默认白色）。通过 `memnew(StandardMaterial)` 后仍应显式设置所有参数，避免依赖默认值。
11. **shader 只在启动时编译一次。** 修改 GLSL 后必须重启进程；`ShaderLoader` 的源码树回退只解决文件定位，不是热重载。`ShaderLoader::read_text_file()` 会剥掉 UTF-8 BOM，否则 GL 编译器会报 `illegal non-ASCII character`。
12. **`thirdparty/` 是预编译二进制。** 不要随意替换或升级 GLFW/SDL3/glad，除非同步验证 `Window` 与 `RenderingWindowInfo` 的原生句柄约定。
13. **glTF UV 方向由 loader 统一翻转。** 导入 glTF 时做 `v = 1 - v`，配合 `ImageTextureLoader` 固定的垂直翻转；不要再额外改其中任何一处，否则贴图会上下颠倒、法线 Y 反向。
14. **glTF 的因子颜色是线性值。** `baseColorFactor` / `emissiveFactor` 写入 `Color` 前必须做 linear → sRGB 编码（`GltfImporter::to_srgb_byte`）；直接存字节会让颜色偏暗。贴图侧靠 `srgb` 标志区分颜色图与数据图。
15. **glTF 的 metallicRoughness 是打包贴图。** shader 对 roughness/metallic 各自采样 `.r`，loader 必须把 G/B 拆成两张单通道（`channels = 1`）贴图；GL 纹理创建对 1 通道使用 `GL_RED`，`GL_UNPACK_ALIGNMENT` 已在渲染设备里设为 1。
16. **glTF 图片解码不经过 `ResourceLoader`。** `GLTFLoader` 直接读字节调用 `ImageTextureLoader::load_from_memory`，按 `(texture index, srgb)` 在单次加载内缓存，避免共享纹理实例被 sampler/sRGB 改写；代价是不同 glTF 文件之间不共享纹理实例。
17. **`CGLTF_IMPLEMENTATION` 只在 `gltf_loader.cpp` 定义一次。** 其它翻译单元只 include `cgltf.h`，重复定义会产生符号错误。
18. **`PackedScene::duplicate()` 是“节点数组深拷贝 + Mesh/Material/Texture 按 `Ref` 共享”。** 修改副本的节点列表不会影响原场景，但两者指向同一批 CPU 网格数据；需要独立网格时要显式复制 `Mesh`。
19. **cubemap 面的行序与 2D 纹理相反，拼接时必须翻回来。** `ImageTextureLoader` 对所有图片做垂直翻转（2D 约定：行 0 是图片底部），而 GL 的 cubemap 面以左上角为原点（行 0 是面顶部）。`CubeMapSkybox::build_cubemap_pixels()` 负责逐面恢复行序，`World3D::ensure_skybox()` 只调用它；不要在 shader 里用 `v_uv.y = -v_uv.y` 糊（会把 +Y/-Y 面互换）。同时 `validate_faces()` 会校验六面齐全、正方形、尺寸/通道/像素长度一致，失败时整块天空盒跳过并打印错误。
20. **颜色空间是端到端契约，新增 shader 必须自己编码输出。** 现状：sRGB 贴图由 `GL_SRGB8*` 解码、`u_albedo/u_ambient_color/u_light_color` 上传前已用 `Color::linear()` 转线性，所以 shader 里的光照全在线性空间；默认帧缓冲**不是** sRGB 格式，fragment shader 最后必须调用 `linear_to_srgb()`（`builtin/frag.glsl`、`builtin-skybox/frag.glsl` 各自有一份）再写输出，漏掉就会整幅偏暗。不要改成打开 `GL_FRAMEBUFFER_SRGB`（当前窗口帧缓冲不带 sRGB 属性，开在别处会双重编码）；`glClearColor` 的数值不经过 shader，按显示值给即可。
21. **Vulkan 交换链实际格式可能是 BGRA8UnormSrgb（`Format::BGRA8UnormSrgb`）。** `get_swapchain_format()` 返回实际格式，不再硬编码；新增格式必须同步：`Format` 枚举、`vulkan/vk_format.h` 双向映射。颜色空间约定已按后端固定（G-03）：Vulkan 变体走 sRGB 交换链 + 硬件编码，**不要**再手工 `linear_to_srgb()`；旧 GL 路径的默认帧缓冲不是 sRGB，GL 变体仍必须手工编码。两套 shader 变体不要互相拷贝。
22. **RHI 描述校验统一在 `servers/rendering/rhi_validation.*`。** 新增资源类型或格式约束时，先改共享校验，再让 Vulkan 的 `create_*` 走同一条路径；不要在 Vulkan 里单独加一套分叉规则。原 Refactor 版 GL 适配层已删除，校验不再需要为 GL 保留分支。
23. **Vulkan 纹理显存由 `vulkan/vk_memory.*` 独占分配，且 image 必须先于 memory 销毁。** `create_texture` 按 `vkGetImageMemoryRequirements2` 的结果分配 `eDeviceLocal` 内存（驱动声明 dedicated 需求时链入 `MemoryDedicatedAllocateInfo`）并 `vkBindImageMemory`；`VkTextureHandleData` 里 `memory` 必须声明在 `image` 之前，才能保证逆序析构正确。当前每张纹理一块 allocation，纹理数量大时再考虑在 `vk_memory` 内换成子分配/VMA。`create_texture` 本身仍不做内容上传：创建后内容未定义，必须随后调用 `queue_write_texture`（需要 mip 时再 `generate_mip_map`）才能采样到有效内容。
24. **Vulkan 的 `queue_write_texture` / `generate_mip_map` / `queue_write_buffer` 是同步一次性提交，不是 WebGPU 那种"排队到帧批次"。** 三者都通过 `vk_command.cpp` 的 `submit_immediate_commands` 新建临时命令池 + fence，提交后立即等待完成；因此调用返回时 GPU 已执行完，且天然先于之后提交的帧。代价是每次上传一次 submit + fence 等待（上传只发生在创建期，暂可接受），V6 做帧 fence 时可并入按帧复用的上传池。此外：纹理上传要求创建时带 `COPY_DST`，mipmap 要求 `COPY_SRC | COPY_DST`，缓冲区写入要求 `COPY_DST`（以上都是新 RHI 的 Vulkan 约定；旧 `RenderingDevice` 的 GL 路径不受此约束）；格式不支持 blit 时 `generate_mip_map` 只告警跳过；上传/mipmap 后 `VkTextureHandleData::current_layout` 记录整张图的布局（整图统一状态），V3 的 render pass 必须从这里取 oldLayout，不能假设初始布局。`generate_mip_map` 用 `vkCmdBlitImage`，`bytes_per_row` 由 CPU 侧重排成紧密打包，不依赖 `bufferRowLength` 的 texel 对齐规则。
25. **Vulkan 缓冲区是设备本地显存 + 复用 staging 池上传，且 buffer 必须先于 memory 销毁。** `create_buffer`（`vulkan/vk_buffer.cpp`）按 `vkGetBufferMemoryRequirements2` 分配 `eDeviceLocal` 内存并绑定，写入统一走 `queue_write_buffer`（设备级 `staging_buffer`，4 KiB 粒度扩容，map/memcpy/flush/unmap + `vkCmdCopyBuffer` + 两道 memory barrier）。因此：`VkBufferHandleData` 里 `memory` 必须声明在 `buffer` 之前；staging 池扩容时赋值顺序必须先替换 buffer 再替换 memory；`vkCmdCopyBuffer` 不要求 4 字节对齐（索引缓冲按 2 字节写入合法），只有 `vkCmdUpdateBuffer` 才要求 4 字节倍数，别把两条约束混用。uniform buffer 目前也是设备本地 + staging 拷贝：**不要**在每帧更新 UBO 的路径上直接调用 `queue_write_buffer`（会变成每帧 submit + fence 等待），V6 的按帧上传池/帧 fence 落地前，动态 UBO 需要在 `vk_memory` 里选 host-visible 内存或加专门通道。销毁仍是立即 `memdelete`（当前无帧提交队列），V6 后需改延迟销毁（D-15）。此外没有读回 API，冒烟只能验证"创建/写入/销毁 + validation 无错"。
26. **纹理视图的范围解析只有一处（`rhi_resolve_texture_view_range`），cubemap 有 6 层对齐约束。** `create_texture_view` 不再自行推算 `mip_count/layer_count == 0`，Vulkan 直接拿共享层解析后的范围；cubemap 视图要求 `base_layer` 与 `layer_count` 都是 6 的倍数（Vulkan `eCube` 约束），本项目 cubemap 固定 6 层，实际只允许 `base_layer=0 / layer_count=6`。视图的 `format` 固定取父纹理格式，**不支持** sRGB ↔ 非 sRGB 重解释；`subresourceRange.aspectMask` 由父纹理格式经 `vk_image_aspect` 推导，不要按 `desc.aspect` 直接传（共享校验已保证两者一致，按 desc 传会让深度分支依赖调用方）。新增视图维度（2DArray/3D）时先改共享解析与 Vulkan 的 viewType 映射。
27. **采样器的 `compare_op` 与 lod 校验是 Vulkan 的接口约定。** `SamplerDesc.compare_op`（`CompareFunction`，与管线 `depth_compare` 共用枚举）在 Vulkan 映射为 `vk::SamplerCreateInfo::compareOp`（`vk_format.cpp: vk_compare_op_from_function`）；即使 `compare_enable == false` 也必须给合法值。lod 校验在 `rhi_validate_sampler_desc`：拒绝 NaN/Inf、要求 `lod_max >= lod_min`、且 `lod_max <= 1000`（`VK_LOD_CLAMP_NONE`）。anisotropy 仍不在接口里，别只加后端字段而不进接口。
28. **SPIR-V 由构建期 glslc 生成，不是运行时编译。** `shaders-vulkan/**/*.glsl` 是 Vulkan 专用源（`#version 450`、显式 `set/binding`、NDC y 翻转），CMake 目标 `orca-shader-spirv` 用 `glslc --target-env=vulkan1.1` 编到 `<build>/resources/shaders-vulkan/**/*.spv`；源码树里没有 `.spv`，新增 shader 必须放进 `shaders-vulkan/` 并按 `vert.glsl`/`frag.glsl` 命名（stage 由文件名推断）。`create_shader_module` 只接受 `ShaderSourceKind::SPIRV`，GLSL 文本会被拒绝；`.spv` 必须 4 字节对齐长度、20 字节头、magic/版本合法，且 `OpEntryPoint` 里存在 `entry_point` 并与 `type` 对应；模块 stage 与管线槽位不一致会在 `create_render_pipeline` 被拒绝。
29. **交换链依赖资源必须随 swapchain 一起重建，且默认 render pass 的 load/store 是固定的。** `init_swapchain` 创建 image view / 每图像深度（`D32Sfloat`，回退 `D24S8`/`D32SfloatS8`）/ 默认 render pass + framebuffer / 2 组 semaphore+fence；成员必须声明在 `swapchain` 之后，重入时先 `device.waitIdle()`，再由 `destroy_swapchain_dependents()` 按 framebuffer → render pass → depth(view/image/memory) → color view 的顺序释放，再把旧 swapchain 作为 `oldSwapchain` 交回驱动。新增任何引用交换链 image 的对象都要挂进这个释放路径。`resize_swapchain` 只更新 `window_info` 并复用这条路径，**不要**硬把请求尺寸写进 `VkSwapchainCreateInfoKHR::imageExtent`：Windows 下 `currentExtent` 由窗口客户区决定，请求值只用于 `UINT32_MAX` 回退分支；零尺寸（最小化）必须推迟重建。默认帧缓冲的 render pass 固定 color `Clear+Store`（final `PresentSrcKHR`）、depth `Clear+DontCare`（final `DepthStencilAttachmentOptimal`）；load/store 不能像 clear value 那样在 begin 时覆盖，要支持 `Load` 必须按组合缓存 render pass+framebuffer（V3-3）。
30. **render pass 的附件模板、布局簿记与释放都集中在 `encoder_begin_render_pass`（I-31）。** 显式附件必须是单层 2D 视图且同尺寸；附件描述要填全 format/samples/load-store/initial+final layout，并配一道 subpass 与 external→subpass / subpass→external 两道统一 dependency。`initialLayout` 取父纹理的 `current_layout`（不要硬编码 Undefined），`finalLayout` 固定为 `ColorAttachmentOptimal` / `DepthStencilAttachmentOptimal`，并由 `pass_end` 写回父纹理——**录制顺序即布局簿记**，录制后丢弃命令缓冲会让簿记失真。framebuffer 尺寸由附件决定，`desc.width/height` 只是 render area（0 表示取附件尺寸）。render pass/framebuffer 归 command buffer 所有，`destroy_command_buffer` 统一释放（仍是立即销毁，D-15）；默认帧缓冲分支使用 `current_swapchain_image` 对应的缓存 framebuffer，进入前由 `acquire_next_swapchain_image` 惰性 acquire（V3-2），同一帧内多个默认帧缓冲 pass 复用同一张图像。begin 后立即录制覆盖整个 render area 的默认 viewport/scissor（G-04），因此不调用 `pass_set_viewport` 也能 draw；`pass_end` 之后所有 pass 级命令（绑定与 draw）都会被 `resolve_recording_pass` 拒绝。`VkTextureViewHandleData` 持有父纹理的非拥有指针，销毁顺序必须是视图先于父纹理。
31. **绑定组/管线布局/管线的所有权与生命周期（V5-1/V5-2/V5-3）。** `create_bind_group_layout`（`vk_bind_group.cpp`）缓存布局项；bind group 持有布局、buffer、view、sampler 的非拥有指针，pipeline layout 持有布局的非拥有指针，`VkGraphicsPipelineHandleData` 持有 pipeline layout 的非拥有指针。销毁顺序必须是 bind group / pipeline → pipeline layout → bind group layout → 资源，否则留下悬空指针（接口头文件已注明）。每个 bind group 独占一个 descriptor pool（`maxSets = 1` + `FREE_DESCRIPTOR_SET_BIT`）：`vk::raii::DescriptorSet` 析构会调用 `vkFreeDescriptorSets`，缺该 flag 会报 `VUID-vkFreeDescriptorSets-descriptorPool-00312`（冒烟实测过）。组合图像采样器要求 texture view + sampler 同时存在；启用 `compare_enable` 的采样器只能配深度格式视图（颜色/立方体视图会在 `create_bind_group` 被共享校验拒绝），深度纹理 + compare 采样器是合法组合。descriptor 的 imageLayout 取 `ShaderReadOnlyOptimal`（深度为 `DepthStencilReadOnlyOptimal`），但纹理仍可能被 render pass 切到附件布局，真正的采样 barrier 必须由 V6 在提交前按 `current_layout` 补齐。UniformBuffer 的 offset（含动态偏移基址）必须按 `minUniformBufferOffsetAlignment` 对齐；动态偏移已由 `pass_set_bind_group` 校验数量、顺序（binding 编号升序）、对齐与叠加静态 offset 后的区间。所有非法布局/绑定在创建 driver 对象之前被共享校验拒绝（`rhi_validation.*`），不要在 Vulkan 里另立分支。
32. **管线创建需要 render pass 兼容性，且 fragment shader 在光栅化开启时必需（V5-3/G-09）。** 录制侧走经典 `VkRenderPass`（I-31），`create_render_pipeline` 会为每条管线创建一个只用于兼容性检查的 render pass 并持有它（`VkGraphicsPipelineHandleData::compatibility_render_pass` 声明在 `pipeline` 之前，逆序析构时先销毁 pipeline）。兼容性比较附件格式/采样数、subpass 引用与附件索引，**并且要求 subpass dependency 的数量与内容完全一致**——只豁免 load/store、initial/final layout 与 attachment reference 的 layout。因此默认帧缓冲 pass、显式附件 pass、管线兼容 render pass 三处都必须使用 `vk_render_pass_util::make_render_pass_dependencies()` 生成的同一组两道 dependency，否则 draw 时 validation 报 `VUID-vkCmdDraw-renderPass-02684`（V6-2b 冒烟实测过 dependencyCount 不一致）。**实际 pass 的颜色附件顺序/数量/格式与深度格式必须与管线 desc 一致**（例如默认帧缓冲的颜色格式必须等于 `get_swapchain_format()`、深度格式等于交换链内部深度格式），否则 `vkCmdBindPipeline`/draw 时 validation 会报不兼容。`fragment.targets` 为空（纯深度）仍必须提供 fragment 模块——Vulkan 只有在 `rasterizerDiscardEnable = VK_TRUE` 时允许没有 fragment 状态，本接口没有该开关，共享校验会拒绝缺少 fragment 模块的 desc。另外管线布局必须声明 shader 用到的 descriptor set（例如 skybox 顶点 shader 的 `set 0/binding 0`），否则 `VUID-VkGraphicsPipelineCreateInfo-layout-07988`；冒烟已验证“空 layout 只适用于无描述符 shader”。
33. **pass 级绑定与绘制的顺序和状态（V6-2 / V6-2b）。** 所有 pass 级命令先经 `resolve_recording_pass` 校验（句柄有效、encoder 未 finish、pass 未 `pass_end`）。`pass_set_bind_group` 只能在 `pass_set_pipeline` 之后调用（`vkCmdBindDescriptorSets` 需要当前管线的 pipeline layout）；group 下标必须在 pipeline layout 的 set 范围内，bind group 布局必须与该 group 一致。动态偏移按 binding 编号升序消费，数量必须等于布局中 `has_dynamic_offset` 的项数，每个偏移按 `minUniformBufferOffsetAlignment` 对齐，且 descriptor 静态 offset + 动态偏移 + 区间不能越过缓冲区大小。`pass_set_vertex_buffer` 要求 `VERTEX` 用途且 slot 小于管线 `vertex_buffer_count`（未绑定管线时按 `maxVertexInputBindings` 校验）；`pass_set_index_buffer` 要求 `INDEX` 用途且 offset 按元素大小（2/4）对齐，并记录索引缓冲/offset/元素大小供 draw 校验。`pass_draw`/`pass_draw_indexed` 要求已经 `pass_set_pipeline`，索引绘制还要求已经 `pass_set_index_buffer` 且 `first_index + index_count` 不越过索引缓冲；数量为 0 时是合法空操作、不录制 draw 命令。`encoder_begin_render_pass` 先设置覆盖整个 render area 的默认 viewport/scissor；`pass_set_viewport` 同时设置 viewport 与由它派生、裁剪到 render area 的 scissor（G-04），管线把 viewport/scissor 设为 dynamic，缺少 scissor 会让 draw 读到未定义状态。命令只能在所属 encoder 还活着时录制（`encoder_finish` 后调用直接报错）。
34. **帧边界与提交同步（V6-3）。** 一帧的调用顺序固定为 `create_command_encoder → encoder_begin_render_pass(默认帧缓冲，内部 acquire) → 绑定/draw → pass_end → encoder_finish → queue_submit → present`；`queue_submit` 等待本帧 `image_available`、以 `in_flight` fence 提交、signal `render_finished`，`present` 等 `render_finished` 后 `vkQueuePresentKHR` 并轮换 `frame_syncs` 槽位。一次 present 之间**只接受一次** `queue_submit`（多命令缓冲需要额外 semaphore 链，见 V6-4/G-10），同一 command buffer 也不能重复提交；两者在入口被拒绝。纯 offscreen 帧（没有 acquire）只提交命令并用帧 fence 节流，`present` 只推进帧、不呈现。acquire 会先等待并 reset 本帧槽位的 in_flight fence（MAX_FRAMES_IN_FLIGHT 节流）；`waitForFences` 的返回值必须检查（D-38 已修）。
35. **提交后资源仍在途。** `destroy_*` 目前仍是立即 `memdelete`，而 `queue_submit` 之后 GPU 可能还在执行：**必须在 `queue_wait_idle()`（或帧 fence 已确认）之后再销毁 command buffer / pass / 管线 / 绑定的资源**。帧 fence 之后的延迟销毁（D-15/V6-4）尚未实现，接口头注释注明当前约定；不要在每帧循环里“submit 完立刻 destroy”。

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
5. 如需渲染，增加 `RenderingStorage` 数据结构 + `RenderingServer` API + `RenderingDevice` 接口（旧接口仍由 GL 实现；新 RHI 只保留 Vulkan）。
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
