# RenderingServer 渲染管线重构方案（切换到 RenderingDeviceRefactor / Vulkan）

> 版本：2026-11-20
> 代码基线：`refactor/RHI` 分支（`57cbf58` 之后，工作区含 V6-2b/V6-3 的 draw/submit/present 改动；
> `AGENTS.md` 已更新为 44 项接口全部可用，单帧端到端冒烟通过）。下一步是把这套 RHI 接进
> `RenderingServer`（清单里的 V7-3）。
> 目标：把 `RenderingServer` 从旧 `RenderingDevice`（OpenGL 立即状态机）迁到
> `RenderingDeviceRefactor`（WebGPU 风格显式 RHI），重写引擎的渲染管线。
> 范围：`servers/rendering/rendering_server.*`、`rendering_storage.*`、`shaders-vulkan/*`、
> `application/application.cpp`、必要的 CMake/测试；**不修改** `resource/`、`scene/` 的公共 API
> 与“节点进树时上传”的约定。
> 非目标：适配层内部实现正确性（假定 `rendering_device_refactor.h` 的契约成立）、多视口/多窗口、
> 多光源、阴影级联、IBL、透明排序、MSAA、GPU Driven。

---

## 1. 结论摘要

1. **保留 `RenderingServer` 的公共 API、RID 体系和 scene 层调用方式**，只替换服务器内部表示：
   旧服务器的 `uint32/uint64` GL 句柄全部换成 RHI 的强类型句柄，`bind_* / set_uniform_*`
   立即状态调用全部换成“资源创建 + 绑定组 + 管线 + 命令录制”。
2. **资源映射**：Mesh surface → 顶点/索引 `GPUBuffer`；Texture → `Texture + TextureView + Sampler`；
   Material → 惰性缓存的 `BindGroup`；Cubemap/Skybox → cube 纹理 + cube 视图 + sampler；
   Shadow map → `Depth32Float` 纹理 + 深度视图 + sampler；Shader → SPIR-V 模块；
   Framebuffer → 每次 pass 的 `RenderPassDesc`。
3. **渲染管线拆成三个 graphics pipeline**：主 PBR（颜色 + 深度）、阴影深度（纯深度）、天空盒
   （颜色 + 深度、depth write 关闭）。主 PBR 和阴影共用一套“对象”绑定组布局，用动态偏移完成
   per-draw 数据。
4. **描述符分三组**：`set 0` 每帧数据（相机/环境/方向光 + 阴影贴图）、`set 1` 每对象动态 UBO、
   `set 2` 材质 6 张纹理。阴影管线用 `shadow-frame + object`，天空盒用 `skybox-frame + cubemap`。
5. **一帧只录制一个 command encoder、只提交一次、只 present 一次**：阴影 pass 与主 pass 都录进
   同一个 command buffer，由默认帧缓冲 pass 惰性触发 acquire；`present()` 推进帧边界。
6. **MVP 先保证正确性，不追求零同步开销**：若 R0 已补 host-visible UBO 通道，则每帧只做一次
   廉价写入；否则暂时每帧 `queue_write_buffer` 一次。帧尾固定
   `queue_submit → present → queue_wait_idle → destroy_command_buffer`。等后端 V6-1/V6-4（按帧
   复用 encoder / 延迟销毁）落地后再去掉每帧 wait。
7. 有 **9 项后端/接口事项必须先确认**（见 §2.4、§10），其中“默认帧缓冲深度格式查询”、
   “采样纹理的初始布局 barrier”和“每帧 UBO 更新通道”是硬缺口：分别决定主管线能否创建、
   阴影贴图能否被采样、以及能否避免每帧一次 submit+fence。

---

## 2. 现状分析

### 2.1 当前一帧的数据流（旧 GL 路径）

```text
Application::run
  -> Window::poll_event
  -> SceneTree::tick
       -> physics_process / process        // 节点 process 中调用 RenderingServer::set_* 更新 CPU 数据
       -> Viewport::render
            -> RenderingServer::sync()     // 空实现
            -> RenderingServer::draw()
                 -> RenderingDevice::clear()
                 -> 遍历 storage.instances 生成 DrawItem（surface / material / transform / index_count）
                 -> render_pass_depth_map()   // 绑定 depth FBO + depth shader，逐 DrawItem draw
                 -> render_pass_scene()       // 绑定 builtin program，逐 DrawItem 设 uniform + 6 纹理 + draw
                 -> render_pass_skybox()      // 绑定 skybox program + cubemap，draw 内置立方体
  -> Window::swap_buffers()                   // GL: glfwSwapBuffers；Vulkan: 空操作
```

资源上传**不在 draw 里**：`MeshInstance3D` / `Light3D` / `World3D` / `Camera3D` 在
`NOTIFICATION_ENTER_TREE` 直接调用 `RenderingServer`，服务器在对应的 `set_* / add_mesh_surface`
里同步创建 GL 资源（VAO/VBO、着色器程序、纹理、FBO、深度贴图）。重构后必须保留这个时机，
只是把“创建 GL 对象”换成“创建 RHI 对象 + 上传”。

### 2.2 旧 `RenderingServer` 的职责清单

| 职责 | 当前实现 | 重构后 |
|---|---|---|
| 资源存储 | `RenderingStorage` 的 10 个 `RIDStorage` | 保留，元素内部字段改为 RHI 句柄 |
| 初始化 | 编译 3 个 GLSL program、建深度 FBO、默认 1x1 纹理、默认材质、天空盒立方体 | 建 SPIR-V 模块、描述符布局、管线布局、3 条管线、UBO arena、默认纹理/材质、共享立方体 |
| 资源上传 | `set_texture_image` / `set_skybox_cubemap` / `add_mesh_surface` / `set_light_cast_shadow` 里同步建 GL 对象 | 同样的 API 调用点里创建 RHI 对象并 `queue_write_*` 上传 |
| 状态设置 | `bind_shader_program`、`set_uniform_*`（按名字查 location）、`bind_texture_2d(unit)` | 绑定组 + UBO + 动态偏移；没有按名字的 uniform |
| 绘制 | `draw_call(DrawItem)`（绑定 VAO + `glDrawElements`） | `pass_set_vertex_buffer` / `pass_set_index_buffer` / `pass_draw_indexed` |
| 帧边界 | `clear()` + GL 自动交换 | `create_command_encoder` → pass 录制 → `encoder_finish` → `queue_submit` → `present` |
| 释放 | `free()` 只覆盖部分 RID 类型；资源析构为空 | `free()` 覆盖全部 GPU 资源；引入延迟销毁/失效表 |

### 2.3 旧 `RenderingDevice` 与新 RHI 的模型差异

| 概念 | 旧 GL 后端 | 新 RHI（Vulkan） | 对 server 的影响 |
|---|---|---|---|
| 网格 | `create_mesh_surface` 一次建 VAO+VBO+EBO | 自己建 vertex/index buffer，并声明 `BufferLayout` | 顶点数据必须交错打包；一个 surface = 2 个 buffer |
| 纹理 | 纹理对象内含 sampler 参数 | `Texture` 与 `Sampler` 分离 | 每个纹理额外持有 sampler；换 flags 要重建 sampler |
| uniform | 按名字设置，驱动维护 program 状态 | 显式 UBO + bind group | 必须定义 std140 结构、绑定组布局、动态偏移 |
| 管线 | program 内部状态 | 创建时固定全部状态（目标格式/深度/混合/顶点布局） | 管线与附件格式绑定；默认帧缓冲格式必须查询 |
| 绑定 | `bind_texture_2d(unit)` | `pass_set_bind_group` | 材质需要 bind group 缓存与失效 |
| 绘制 | `glDrawElements` | 录制到 command buffer | draw 必须发生在 pass 内，且顺序固定 |
| 帧 | 隐式 swap | acquire 惰性发生在默认 pass，`present()` 定义帧边界 | server 的 `draw()` 要负责 submit + present |
| 生命周期 | 立即销毁，GL 驱动兜底 | 后端当前仍是立即销毁，需要调用方等 GPU 完成 | MVP 每帧 `queue_wait_idle`，后续延迟销毁 |

### 2.4 阻塞项与后端契约（开工前必须确认）

以下均来自 `docs/vulkan_rhi_checklist_v2.md` 与当前工作区改动（`rendering_device_refactor.h` 已补充帧边界注释）：

| ID | 事项 | 现状 | 对本方案的影响 / 决策 |
|---|---|---|---|
| B-1 | `pass_draw` / `pass_draw_indexed` | 已完成（工作区实现 + `AGENTS.md` §5.3 #33），校验管线/索引缓冲/索引区间/pass 状态 | 可直接使用；server 每帧仍要遵守“先 set_pipeline 再绑定，再 draw”的顺序 |
| B-2 | `queue_submit` / `present` | 已完成（`AGENTS.md` §5.3 #34）；一帧 = encoder → 默认 pass（内部 acquire）→ finish → `queue_submit` → `present`，一次 present 之间只接受一次 `queue_submit` | server 必须把所有 pass 录进一个 command buffer，帧尾只提交一次 |
| B-3 | 默认帧缓冲 acquire | 由第一个默认 pass 的 `encoder_begin_render_pass` 惰性 acquire | `draw()` 必须先录默认 pass 再 finish/submit/present；不能在 acquire 前 present |
| B-4 | 默认帧缓冲格式（color/depth） | 后端选 `D32Sfloat`，失败回退 `D24S8`/`D32SfloatS8`；接口只有 `Format::Depth32Float`，**没有深度格式查询入口**；color 若是 `*_Unorm`（非 sRGB），Vulkan 变体的硬件编码假设也不成立 | 主/天空盒管线的 `depth_stencil.format` 必须等于默认深度格式，否则管线与 pass 不兼容。**必须补 `get_swapchain_depth_format()`，或保证只接受 D32Sfloat**。`init()` 还要校验 `get_swapchain_format()` 为 sRGB 格式，否则要么加非 sRGB shader 变体，要么明确拒绝启动 |
| B-5 | 采样纹理的布局 | render pass 结束把纹理 `current_layout` 置为附件布局；bind group 的 descriptor 期望 `ShaderReadOnlyOptimal` / `DepthStencilReadOnlyOptimal`；draw 路径的 barrier 仍未实现 | 阴影贴图在本帧 pass 结束后立刻作为 `sampler2D` 采样，**必须由后端在 bind/draw 时按 `current_layout` 补 barrier**；也要覆盖无阴影时 1x1 回退深度纹理的 `Undefined → DepthStencilReadOnlyOptimal` 首次转换。未实现则不能开阴影，且回退深度绑定也可能触发 validation |
| B-6 | UBO 每帧更新 | `queue_write_buffer` 是“submit + fence 等待”的同步一次性提交，没有 host-visible / map / 帧批上传入口；`AGENTS.md` §5.3 #25 明确要求不要在每帧更新 UBO 的路径上直接用它 | **推荐在 R0 给 RHI 补一个最小通道**：`GPUBufferDesc` 增加 `host_visible`（或专门的 `map_buffer`/上传接口），后端对 host-visible buffer 的 `queue_write_buffer` 走 map/memcpy/flush、不再 submit+fence；server 的 frame arena 用 host-visible 创建。若暂不扩展接口，MVP 只能每帧同步写一次（功能正确但 CPU/GPU 串行，记录为临时风险） |
| B-7 | 资源延迟销毁 | `destroy_*` 仍立即 `memdelete`；`destroy_command_buffer` 必须等提交执行完 | MVP 帧尾 `present → queue_wait_idle` 后再销毁 command buffer；server 侧也以“帧后失效”处理资源替换 |
| B-8 | 默认帧缓冲 load/store | 默认 render pass 固定 color `Clear+Store`、depth `Clear+DontCare`；`Load` 会被拒绝 | 主 pass 只用 Clear；天空盒必须画在同一个主 pass 内（不能另开 Load 颜色的 pass） |
| B-9 | render pass 兼容性中的 subpass dependency | `AGENTS.md` §5.3 #32：管线兼容 render pass 与录制侧 pass 的 **subpass dependency 数量/内容必须完全一致**（否则 draw 报 `VUID-vkCmdDraw-renderPass-02684`），load/store/layout 只豁免部分字段 | server 不创建 render pass，由后端模板保证；但验收时必须跑真实 draw 并检查 validation。若默认帧缓冲 pass 与 explicit pass 模板尚未统一，需要后端在 R0 对齐 `vk_render_pass_util::make_render_pass_dependencies()` |

> 建议把 B-4、B-5 作为 R0 的硬前置：B-4 决定主管线能否创建成功；B-5 决定阴影能否启用。
> 如果暂时不补 B-5，MVP 可以先关闭阴影（`cast_shadow` 强制 false），主 pass + 天空盒仍可工作。

---

## 3. 总体设计

### 3.1 边界与不变契约

1. `RenderingServer` 公共头文件的函数签名、RID 类型、`TextureSlot` 位序保持不变；scene 层无需改动。
   只新增一个 `shutdown()`（由 `Application` 在销毁单例前显式调用；`destroy_singletone()`
   作为兜底，若未调用 shutdown 也在析构前释放 GPU 资源）。
2. 资源上传时机保持“节点进树时上传”；`sync()` 仍可以为空实现，但服务器内部把所有惰性工作
   （材质 bind group 重建、UBO arena 扩容）放在 `draw()` 开始处完成。
3. `res://`、颜色空间、纹理槽顺序、UV 方向、glTF 材质映射等既有约定不变。
4. 唯一后端：新 RHI 固定 Vulkan。`RenderingServer` 不再包含任何旧 `RenderingDevice` 调用；
   未启用 Vulkan 的构建中 `init()` 明确失败并打印错误（不是静默降级）。
5. 一帧只有一个 scene / 一个主相机 / 一个方向光（保持现状）；暂不做多光源。

### 3.2 服务器内部结构

```text
RenderingServer
├─ std::shared_ptr<RenderingDeviceRefactor> device       // 缓存的设备单例
├─ RenderingStorage storage                             // RID -> CPU 描述 + GPU 句柄
├─ 全局 GPU 对象
│   ├─ Shader modules: main_vs/fs, depth_vs/fs, skybox_vs/fs
│   ├─ BindGroupLayout: frame / object / material / shadow_frame / skybox_frame / skybox_texture
│   ├─ PipelineLayout: main / shadow / skybox
│   ├─ RenderPipeline: main / shadow / skybox（缓存创建时的 color/depth format）
│   ├─ FrameUniformArena: 1 个 UNIFORM|COPY_DST buffer + CPU 影子缓冲
│   ├─ BindGroup: frame_bg / shadow_frame_bg / object_bg / skybox_frame_bg
│   ├─ 默认 1x1 白/法线纹理，默认材质及其 bind group
│   ├─ 天空盒共享立方体几何（1 个 vertex buffer + 1 个 index buffer）
│   └─ 1x1 回退深度纹理/视图/sampler（无阴影时占用 frame_bg 的深度绑定）
├─ std::vector<DrawItem> draw_items                     // 每帧重建
└─ 帧生命周期状态（上一帧 command buffer、延迟销毁队列）
```

**新增文件建议**

| 文件 | 内容 |
|---|---|
| `servers/rendering/rendering_uniforms.h` | std140 UBO 结构、arena 偏移常量、`static_assert`、矩阵拷贝辅助函数 |
| `servers/rendering/rendering_pipeline.h/.cpp` | shader 模块加载、BGL/pipeline layout 创建、3 条管线创建与销毁、`PipelineKey` |
| `servers/rendering/rendering_server.cpp` | 公共 API、资源上传、draw item 收集、UBO 填充、pass 录制、帧生命周期 |
| `servers/rendering/rendering_storage.h` | 只改 GPU 字段类型，不新增 RID 类型 |

如果不想新增 `.cpp`，可先把 `rendering_pipeline` 的逻辑放在 `rendering_server.cpp` 的匿名命名空间，
但 `rendering_uniforms.h` 建议独立，便于单测校验布局。

### 3.3 `RenderingStorage` 字段迁移

| 结构 | 旧字段 | 新字段 |
|---|---|---|
| `Surface` | `uint32_t surface`（VAO）、`index_count`、`material` | `GPUBufferHandle vertex_buffer`、`GPUBufferHandle index_buffer`、`uint32_t index_count`、`uint32_t vertex_stride`、`RID material` |
| `TextureData` | `uint32_t texture_2d`、宽高、srgb/repeat/linear/mipmap | `TextureHandle texture`、`TextureViewHandle view`、`SamplerHandle sampler`、`Format format`、`uint32_t mip_levels`、`uint64_t generation`、原有的 4 个 flag |
| `MaterialData` | 6 个纹理 RID、标量、`texture_mask` | 标量/纹理 RID **不变**；新增 `BindGroupHandle bind_group`、`uint64_t bind_group_key[6]`、`bool bind_group_dirty` |
| `SkyboxData` | `uint32_t cube_map_texture`、`cube_surface`、`cube_index_count` | `TextureHandle cube_texture`、`TextureViewHandle cube_view`、`SamplerHandle cube_sampler`、`BindGroupHandle cube_bind_group`；立方体几何改为 server 全局共享，不在 SkyboxData 里重复 |
| `LightData` | `uint32_t depth_map` | `TextureHandle shadow_texture`、`TextureViewHandle shadow_view`、`SamplerHandle shadow_sampler` |
| `ShaderData` | `uint64_t shader_program` | `ShaderHandle vertex_module`、`ShaderHandle fragment_module` |
| `MeshData` / `InstanceData` / `SceneData` / `EnvironmentData` / `CameraData` | — | 不变 |

RID 类型和 `RIDStorage` 不变；`free()` 的分支保留并补全。

---

## 4. 资源映射与上传

### 4.1 Mesh surface

`add_mesh_surface` 改为：

1. 校验输入：`vertices` 非空；`normals/uvs/tangents/bitangents` 要么全空，要么全部与 `vertices`
   等长；`indices` 非空。**`add_mesh_surface` 一律打包成 14 个 `real_t` / stride 56**：
   缺失的属性填默认值（法线 `(0,0,1)`、UV `(0,0)`、切线 `(1,0,0)`、副切线 `(0,0,1)`），
   保证所有 mesh surface 都能绑定到固定的主 PBR 管线。stride 12 的“仅位置”几何只保留给
   server 内部共享的天空盒立方体。
2. 把顶点交错打包成 `p(3) + n(3) + uv(2) + t(3) + bt(3) = 14` 个 `real_t`，stride = 56 字节。
3. 创建 `GPUBufferDesc{ size=顶点字节数, usage=VERTEX|COPY_DST }`，`queue_write_buffer` 上传。
4. 创建索引 buffer：`usage=INDEX|COPY_DST`，内容为 `uint32_t`（保持现状），`IndexFormat::Uint32`。
5. 把两个句柄与 `index_count` 写进 `Surface`；失败时打印错误并返回（保持“无效输入静默跳过”的风格）。

**顶点布局常量**（server 的 `BufferLayout` 与 shader location 必须一致）：

| location | 属性 | 格式 | offset | stride |
|---|---|---|---|---|
| 0 | position | `Float32x3` | 0 | 56 |
| 1 | normal | `Float32x3` | 12 | 56 |
| 2 | uv | `Float32x2` | 24 | 56 |
| 3 | tangent | `Float32x3` | 32 | 56 |
| 4 | bitangent | `Float32x3` | 44 | 56 |

阴影管线只声明 location 0、stride 仍为 56；天空盒管线声明 location 0、stride 12。
`add_mesh_surface` 不允许产生 stride 12 的 surface（否则与主 PBR 管线不兼容），
缺少的属性按 §4.1 的默认值补齐。

### 4.2 2D 纹理

`set_texture_image` 的映射规则：

| channels | srgb=false | srgb=true |
|---|---|---|
| 1 | `R8Unorm` | CPU 展开成 RGBA8 → `RGBA8UnormSrgb` |
| 2 | `RG8Unorm` | CPU 展开成 RGBA8 → `RGBA8UnormSrgb` |
| 3 | CPU 展开成 RGBA8（alpha=255）→ `RGBA8Unorm` | 同左 → `RGBA8UnormSrgb` |
| 4 | `RGBA8Unorm` | `RGBA8UnormSrgb` |

说明：

- 接口的 `Format` 没有 `RGB8`/`R8Srgb`，3 通道必须 CPU 展开；展开只发生在 `set_texture_image`，
  不影响 `ImageTexture` 的 CPU 数据与现有加载器（glTF 的 MR 拆分已经是单通道）。
- `usage = TEXTURE_BINDING | COPY_DST`；`gen_mipmap` 时再或上 `COPY_SRC`，
  `mip_levels = floor(log2(max(w,h))) + 1`，否则 `mip_levels = 1`。
- 先 `queue_write_texture(mip 0, bytes_per_row=0, data_size=按实际展开后的字节数填写)`，
  再 `generate_mip_map`。
- 创建全范围 2D 视图（`base_mip=0, mip_count=0, layer_count=1, aspect=Color`）。
- sampler 由 `repeat / linear_filter / mipmap` 生成：`address_u/v/w = Repeat|ClampToEdge`，
  `mag/min = Linear|Nearest`，`mipmap_filter = Linear`，有 mip 时 `lod_max=1000`，否则 `lod_max=0`。
- 重新上传同一个 `RID` 时：先重建新的一组 `texture/view/sampler`，再销毁旧的一组；
  `generation++`，并把引用该纹理的材质标记为 bind group dirty（见 §7.2）。

### 4.3 Cubemap 天空盒

`set_skybox_cubemap`：

- `TextureDescRefactor{ dim=Cube, format=RGBA8UnormSrgb|RGBA8Unorm, width=height, mip_levels=1,
  usage=TEXTURE_BINDING|COPY_DST }`。
- 6 个面逐个 `queue_write_texture`：`array_layer = 0..5`，`bytes_per_row = width*4`，
  `data_size = width * height * 4`，`data` 指向 `pixels + face * w*h*4`。CPU 侧
  `CubeMapSkybox::build_cubemap_pixels()` 已经按 `+X,-X,+Y,-Y,+Z,-Z` 拼好并恢复行序，无需再翻转。
  如果天空盒图片不是 4 通道，先用 §4.2 的规则在 CPU 展开成 RGBA8（接口没有 RGB8 格式），
  再按每面 `w*h*4` 字节上传。
- 视图：`TextureViewDesc{ dim=Cube, aspect=Color, layer_count=6 }`。
- sampler：`address=ClampToEdge`、线性过滤、`lod_max=0`（只有 mip 0）。
- `SkyboxData` 里保存 `cube_texture/cube_view/cube_sampler` 与它的 `cube_bind_group`
  （布局固定为 skybox 的 `set 1`，绑定 cube 视图 + sampler）。
- 天空盒立方体几何由 server 全局创建一次：8 个顶点（stride 12）+ 36 个 `uint32` 索引；
  `SkyboxData` 只引用全局句柄。
- 重复调用时替换旧资源，旧 `cube_bind_group` 与新纹理视图一起销毁/重建。
- 方向约定：2D 纹理**不需要**在 Vulkan 下再翻转数据。`ImageTextureLoader` 已把原图底部放在
  数据行 0，GL 把行 0 当 v=0（底部），Vulkan 把行 0 当 v=0（顶部），而引擎 UV 的 v=0 约定是
  图片底部——数据行序与坐标原点同时翻转，最终“v=0 采样原图底部”在两边一致。天空盒 cubemap
  的面朝向在 GL/Vulkan 间可能不同（尤其 ±Y 面），必须靠 R7 视觉对照确认；如果上下颠倒，
  只在 Vulkan skybox shader 的采样方向翻转，**不要改 `CubeMapSkybox::build_cubemap_pixels()`**
  （那会破坏 GL 路径，见 AGENTS §5.3 #19）。

### 4.4 阴影贴图

`set_light_cast_shadow`：

- `enable == true`：
  - `TextureDescRefactor{ format=Depth32Float, width=height=shadow_size, mip_levels=1,
    usage=TEXTURE_BINDING|RENDER_ATTACHMENT }`；
  - 深度视图：`TextureViewDesc{ dim=Texture2D, aspect=DepthOnly, mip_count=1, layer_count=1 }`；
  - sampler：`ClampToEdge`、`Nearest`、`compare_enable=false`、`lod_max=0`
    （shader 侧保持和 GL 一致的 `sampler2D` + `.r` 手动 PCF，不切换到 `sampler2DShadow`）；
  - 保留 `light_space_transform` / `light_space_projection` 的现有计算。
- `enable == false`：先销毁 `frame_bind_group`（它持有阴影视图的非拥有指针），再销毁阴影
  视图/纹理/sampler，并把 `frame_bind_group` 标记为需要重建（下次 draw 用回退深度视图重建）。
- 启用/更换阴影资源时：先创建新的纹理/视图/sampler，再销毁旧的 `frame_bind_group` 与旧资源，
  最后用新视图重建 `frame_bind_group`，避免任何 bind group 持有已销毁视图的指针。
- 初始化时额外创建一个 **1x1 的 `Depth32Float` 回退纹理/视图/sampler**，保证 `frame` 绑定组
  永远有合法的 `DepthTexture` 项（无阴影时 shader 由 uniform 分支提前返回）。
- 依赖 B-5：阴影 pass 结束后纹理处于 `DepthStencilAttachmentOptimal`，主 pass 采样前必须
  由后端把布局切到 `DepthStencilReadOnlyOptimal`。

### 4.5 默认纹理、默认材质与共享资源

`create_default_textures` 与旧逻辑一致：1x1 白色（albedo/roughness/metallic/ao/height）、
1x1 法线 `(128,128,255,255)`。它们走与普通纹理相同的创建/上传路径，`srgb=false`、`gen_mipmap=false`、
`repeat=true`、`linear=true`。默认材质（`texture_mask=0`）在 `init()` 中创建，并在首次需要时
惰性创建 bind group（6 个槽全部绑默认纹理）。

天空盒立方体几何、回退深度纹理、frame/object bind group 都属于“server 全局资源”，
由 `init()` 创建、`shutdown()` 销毁，不通过 RID 暴露。

---

## 5. Shader、UBO 与描述符布局

### 5.1 Vulkan shader 变体

需要在 `servers/rendering/shaders-vulkan/` 下新增/补齐（CMake 的 `orca-shader-spirv` 会自动编译）：

```text
shaders-vulkan/builtin/vert.glsl        # PBR 顶点（替换旧 GL 变体）
shaders-vulkan/builtin/frag.glsl        # PBR 片元（GGX/Smith/Schlick/法线/视差/阴影）
shaders-vulkan/builtin-depth/vert.glsl  # 阴影 pass 顶点
shaders-vulkan/builtin-depth/frag.glsl  # 空片元（管线要求 fragment 模块）
shaders-vulkan/builtin-skybox/vert.glsl # 已有，按新 set/binding 复核
shaders-vulkan/builtin-skybox/frag.glsl # 已有，按新 set/binding 复核
```

移植规则（**必须逐条遵守**）：

1. `#version 450`，所有 uniform 改为显式 `layout(set=..., binding=...)` 的 UBO / `sampler2D` /
   `samplerCube`；不再有按名字设置的 uniform。
2. **颜色空间**：交换链是 `*_UnormSrgb`，写入由硬件编码，Vulkan 变体**删除**
   `linear_to_srgb()`，直接输出线性颜色（G-03）。清屏颜色同样按线性值给（见 §6.2），
   不要把旧 GL 的显示值直接填进 `ColorAttachmentDesc::clear_color`。
3. **屏幕 Y 轴**：Vulkan NDC y 向下；主 PBR 和天空盒顶点 shader 在最后输出
   `gl_Position.y = -gl_Position.y`（现有 skybox 变体已经这样做）。必须**先算完所有 varying**
   （尤其主 PBR 的 `v_frag_light_space_pos`，它必须保持未翻转的 GL 裁剪坐标），最后再翻转
   `gl_Position`，否则阴影采样会镜像。
4. **深度 Z 重映射**：引擎 `Projection` 仍是 GL 约定（NDC z ∈ [-1,1]），Vulkan 裁剪空间要求
   z ∈ [0,1]。主 PBR 与阴影顶点 shader 都做 `gl_Position.z = 0.5 * gl_Position.z + 0.5 * gl_Position.w;`
   （天空盒 `pos.xyww` 的 z=w，映射后仍为 1，可不做或做同理处理）。
5. **阴影 pass 的 Y 轴不能翻转**：主着色器里的 `v_frag_light_space_pos` 仍使用未翻转的
   GL 空间裁剪坐标，再按 `pos*0.5+0.5` 采样。阴影 shader 不翻 y，这样 Vulkan 光栅化后
   “+y_gl 对应纹理 v 大的一侧”，与主 shader 的采样公式一致；如果阴影 shader 也翻 y，
   采样会上下镜像。z 重映射两边一致，保证存进去的深度和 `pos.z` 可直接比较。
6. 法线/切线/副切线、视差、`textureGrad`、`textureLod`、`textureSize` 等逻辑原样保留；
   `u_texture_mask` 改为 UBO 里的 `ivec4`（或 float 位掩码），含义和位序不变。
7. 阴影采样保持 `sampler2D u_light_depth_map` + `.r`，PCF/bias 逻辑不变；不要改成
   `sampler2DShadow`（那只是后续可选优化，本期保持与 GL 语义一致）。
8. GL 变体（`shaders/builtin*`）继续保留给旧设备/对照测试，**不要互相拷贝修改**；
   两份变体语义必须同步维护。

### 5.2 UBO 结构与 std140 布局

建议直接在 `rendering_uniforms.h` 里定义，并配合 `static_assert`：

```cpp
struct alignas(16) FrameUniforms
{
    real_t view[16];                  // offset 0,   size 64
    real_t projection[16];            // offset 64,  size 64
    real_t camera_position[4];        // offset 128, xyz + 0
    real_t ambient_color_strength[4]; // offset 144, linear rgb + ambient_strength
    real_t light_color_intensity[4];  // offset 160, linear rgb + intensity
    real_t light_direction_type[4];   // offset 176, direction xyz + (float)type
    real_t light_space_matrix[16];    // offset 192
    real_t light_space_projection[16];// offset 256
    int32_t flags[4];                 // offset 320, flags[0]=cast_shadow，其余 0
};                                    // size 336
static_assert(sizeof(FrameUniforms) == 336);

struct alignas(16) ShadowUniforms
{
    real_t light_space_matrix[16];    // offset 0
    real_t projection[16];            // offset 64
};                                    // size 128
static_assert(sizeof(ShadowUniforms) == 128);

struct alignas(16) ObjectUniforms
{
    real_t model[16];                 // offset 0
    real_t albedo[4];                 // offset 64, linear rgba
    real_t params0[4];                // offset 80, metallic, roughness, parallax_scale, normal_scale
    real_t params1[4];                // offset 96, uv_scale.x, uv_scale.y, ao_strength, parallax_flip
    int32_t texture_mask[4];          // offset 112, texture_mask + 0/0/0
};                                    // size 128
static_assert(sizeof(ObjectUniforms) == 128);

struct alignas(16) SkyboxFrameUniforms
{
    real_t view[16];                  // offset 0
    real_t projection[16];            // offset 64
};                                    // size 128
```

**Frame uniform arena**（一个 buffer 承载三块数据，减少每帧 `queue_write_buffer` 调用）：

| 区域 | 偏移 | 大小 | 绑定 |
|---|---|---|---|
| `FrameUniforms` | 0 | 336 | main 的 set 0 / binding 0 |
| `ShadowUniforms` | 512 | 128 | shadow 的 set 0 / binding 0 |
| `ObjectUniforms` | 768 | `max_draws * 256` | main 与 shadow 的 set 1 / binding 0（动态偏移） |

- 偏移 512、768 都按 256 对齐；`OBJECT_STRIDE = 256`（大于 `sizeof(ObjectUniforms)==128`，
  保证常见设备的 `minUniformBufferOffsetAlignment <= 256`）。接口不暴露对齐值，server 固定
  用 256 是 Vulkan 规范允许的上限，安全。
- buffer：`GPUBufferDesc{ usage = UNIFORM | COPY_DST, size = 768 + max_draws*256 }`；
  推荐按 B-6 增加 `host_visible`（若 RHI 尚未扩展则先用设备本地 + 每帧 `queue_write_buffer`）。
  初始 `max_draws = 2048`，不足时按 2 倍扩容（销毁旧 buffer 与引用它的四个 bind group
  ——`frame_bg` / `shadow_frame_bg` / `object_bg` / `skybox_frame_bg`——后重建）。
- 每帧把 `FrameUniforms`、`ShadowUniforms`、所有 `ObjectUniforms` 填进 CPU 影子缓冲，
  一次写入 `[0, 768 + used*256)`。host-visible 时这条写入不产生 submit+fence；设备本地路径
  则退化为每帧一次同步提交（仅作临时方案）。
- 四个 bind group 的 UBO 绑定都要**显式给 `BindingResource::size`**：
  `Frame=336`、`Shadow=128`、`Object=128`；不要用 `size=0`，否则共享 arena 的“到缓冲区末尾”
  会让动态偏移校验区间过大而失败。

### 5.3 描述符集合与管线布局

**主 PBR pipeline layout：`[frame_set_layout, object_set_layout, material_set_layout]`**

| set | binding | 类型 | visibility | 说明 |
|---|---|---|---|---|
| 0 | 0 | UniformBuffer | VERTEX + FRAGMENT | `FrameUniforms`，`min_binding_size=336`，静态 |
| 0 | 1 | DepthTexture | FRAGMENT | 阴影贴图（或 1x1 回退），`name="u_light_depth_map"` |
| 1 | 0 | UniformBuffer | VERTEX + FRAGMENT | `ObjectUniforms`，`min_binding_size=128`，**has_dynamic_offset=true** |
| 2 | 0 | Texture2D | FRAGMENT | `u_albedo_texture` |
| 2 | 1 | Texture2D | FRAGMENT | `u_normal_texture` |
| 2 | 2 | Texture2D | FRAGMENT | `u_roughness_texture` |
| 2 | 3 | Texture2D | FRAGMENT | `u_metallic_texture` |
| 2 | 4 | Texture2D | FRAGMENT | `u_ao_texture` |
| 2 | 5 | Texture2D | FRAGMENT | `u_height_texture` |

**阴影 pipeline layout：`[shadow_frame_set_layout, object_set_layout]`**
（`object_set_layout` **复用同一个句柄**，这样同一份对象 bind group 可以在两条管线里绑定）

| set | binding | 类型 | visibility | 说明 |
|---|---|---|---|---|
| 0 | 0 | UniformBuffer | VERTEX | `ShadowUniforms`，`min_binding_size=128` |
| 1 | 0 | UniformBuffer | VERTEX + FRAGMENT | 同 `object_set_layout`，动态偏移 |

**天空盒 pipeline layout：`[skybox_frame_set_layout, skybox_texture_set_layout]`**

| set | binding | 类型 | visibility | 说明 |
|---|---|---|---|---|
| 0 | 0 | UniformBuffer | VERTEX | `SkyboxFrameUniforms`，`min_binding_size=128` |
| 1 | 0 | TextureCube | FRAGMENT | `u_skybox_texture`，组合图像采样器 |

**bind group 实例**

| bind group | 绑定的 set | 内容 | 生命周期 |
|---|---|---|---|
| `frame_bg` | main set 0 | frame UBO 区域 + 当前阴影/回退深度视图 | arena 扩容或阴影资源变化时重建 |
| `shadow_frame_bg` | shadow set 0 | shadow UBO 区域 | arena 扩容时重建 |
| `object_bg` | main/shadow set 1 | object UBO 区域（不含动态偏移） | arena 扩容时重建 |
| `skybox_frame_bg` | skybox set 0 | skybox UBO 区域 | arena 扩容时重建 |
| 每个材质的 `bind_group` | main set 2 | 6 组 view+sampler | 纹理/槽位变化时重建 |
| 每个天空盒的 `cube_bind_group` | skybox set 1 | cube view + sampler | 天空盒贴图替换时重建 |

> 注意后端 `pass_set_bind_group` 要求 `bind_group.layout == 当前管线 layout[group_index]`
> （指针比较），所以 `object_set_layout` 必须由 main/shadow 两条 pipeline layout 引用同一个
> 句柄；材质/天空盒的布局各自独立即可。

### 5.4 数据填充约定

- 矩阵：`Transform3D::get_mat4` / `Projection::data` 都是“列主序、按列排列”的内存布局，
  GLSL `mat4` 默认列主序，**直接 memcpy 到 UBO 的 float[16] 即可**，不要转置。
- 颜色保持现有约定：`Color::linear(out)` 后写入 UBO（sRGB→linear）；sRGB 贴图由硬件解码；
  输出交给 sRGB 交换链编码。
- 注意 `Color::linear()` **只写 rgb，不写 alpha**。填 UBO 时必须显式把 alpha 置为确定值
  （albedo 用 1.0，强度/环境色用 0.0），不要依赖数组的未初始化内容——旧 GL 路径里
  `real_t vec4[4]` 只传 `.rgb` 掩盖了这个问题，新路径建议统一写成
  `Color::linear(dst)` + `dst[3] = 1.0f` 的辅助函数。
- `cast_shadow` 无阴影时为 0；`light_intensity <= 0` 时 shader 的提前返回逻辑与旧版一致。
- 每帧从 `DrawItem` 生成 `ObjectUniforms` 时同时决定动态偏移槽位：
  `slot = item_index`，偏移 = `slot * 256`（传给 `pass_set_bind_group` 的 `dynamic_offsets` 是
  **相对 descriptor 的偏移**，后端会再加上 bind group 的静态 offset 768）。

---

## 6. 管线状态与 pass 编排

### 6.1 三条 graphics pipeline 的固定状态

| 状态 | main PBR | shadow | skybox |
|---|---|---|---|
| vertex module | `builtin` vert | `builtin-depth` vert | `builtin-skybox` vert |
| fragment module | `builtin` frag | `builtin-depth` frag（空） | `builtin-skybox` frag |
| 顶点布局 | stride 56，loc0..4 | stride 56，仅 loc0 | stride 12，仅 loc0 |
| topology | TriangleList | TriangleList | TriangleList |
| cull | None（与 GL 现状一致） | None | None |
| depth test / write | LessEqual / true | LessEqual / true | LessEqual / **false** |
| 颜色目标 | `{ get_swapchain_format(), blend off, mask ALL }` | 空 | `{ get_swapchain_format(), blend off, mask ALL }` |
| 深度格式 | `get_swapchain_depth_format()`（见 B-4） | `Depth32Float` | 同 main |
| samples | 1 | 1 | 1 |
| pipeline layout | main | shadow | skybox |

底层管线是动态 viewport/scissor；pass 开始后端已设覆盖 render area 的默认值，server 不需要
调用 `pass_set_viewport`，除非以后做分屏（本期不做）。

> 兼容性注意（B-9）：Vulkan 的 render pass 兼容性除了颜色/深度格式、采样数、subpass 引用与
> 附件索引，还要求 **subpass dependency 的数量与内容一致**（AGENTS §5.3 #32）。server 不创建
> render pass，但必须保证实际录制用的是后端同一套模板（默认帧缓冲 pass、显式阴影 pass 都是
> 后端 `encoder_begin_render_pass` 生成的），并以真实 draw 的 validation 结果验收。

### 6.2 一帧的 pass 结构

```text
帧开始（draw()）
  ├─ 若上一帧未确认完成：queue_wait_idle；刷新延迟销毁队列
  ├─ 收集 DrawItem / 更新相机、环境、灯光数据
  ├─ 按需重建材质 bind group、按需扩容 arena、按需重建 frame_bg
  ├─ CPU 填充 FrameUniforms / ShadowUniforms / ObjectUniforms[max]
  ├─ queue_write_buffer(frame_uniform_buffer, 0, data, used)
  ├─ create_command_encoder
  │    ├─ [可选] 阴影 pass（显式深度附件，0 个颜色附件）
  │    │     pass_set_pipeline(shadow)
  │    │     pass_set_bind_group(0, shadow_frame_bg)
  │    │     每个 DrawItem：
  │    │       pass_set_bind_group(1, object_bg, {slot*256})
  │    │       pass_set_vertex_buffer(0, surface.vb) / pass_set_index_buffer(surface.ib, Uint32)
  │    │       pass_draw_indexed(index_count)
  │    │     pass_end
  │    └─ 主 pass（默认帧缓冲：color view 无效 + depth view 无效，Clear）
  │          pass_set_pipeline(main)
  │          pass_set_bind_group(0, frame_bg)
  │          每个 DrawItem：
  │            pass_set_bind_group(1, object_bg, {slot*256})
  │            pass_set_bind_group(2, material.bind_group)
  │            pass_set_vertex_buffer(0, surface.vb)
  │            pass_set_index_buffer(surface.ib, Uint32)
  │            pass_draw_indexed(index_count)
  │          [天空盒] pass_set_pipeline(skybox)
  │            pass_set_bind_group(0, skybox_frame_bg)
  │            pass_set_bind_group(1, skybox.cube_bind_group)
  │            pass_set_vertex_buffer(0, 全局立方体 vb)
  │            pass_set_index_buffer(全局立方体 ib, Uint32)
  │            pass_draw_indexed(36)
  │          pass_end
  ├─ encoder_finish → queue_submit(cmd) → present()
  ├─ queue_wait_idle
  ├─ destroy_command_buffer(cmd)
  └─ 帧结束（保留 arena CPU 数据；不销毁 GPU 全局对象）
```

**关键点**

- 阴影 pass 必须排在主 pass 之前；两段都在同一个 command buffer 里，只有一次 `queue_submit`。
- 默认帧缓冲 pass 是“惰性 acquire”的触发点；`present()` 必须在本帧 `queue_submit` 之后调用。
- 主 pass 的颜色/深度清屏值来自 `ColorAttachmentDesc`。**注意 sRGB 交换链的 clear 值按线性解释**
  （Vulkan 规范：sRGB 附件的 float clear 值被视为 linear）：旧 GL 路径的显示值
  `(0.10, 0.11, 0.13)` 不能直接照抄，要写成等价的线性值，例如
  `Color(26, 28, 33, 255).linear(clear_color); clear_color[3] = 1.0f`；深度 clear 仍为 1.0。
- 天空盒必须在**同一个主 pass**里画（默认 render pass 固定 Clear，不能另开 Load 颜色的 pass）。
- 阴影 map 尺寸用 `RenderPassDesc.width/height = 0`，由附件视图推导；主 pass 同样为 0，
  由交换链尺寸推导。这样 resize 后 server 不需要手工更新 render area。
- `DrawItem` 结构可以沿用 `rendering_device.h` 里的旧结构，也可以改成 server 内部结构；
  建议新增：

```cpp
struct DrawItem
{
    // 已上传的 surface（server 内部指针或 RID）
    RID mesh;
    uint32_t surface_index;
    RID material;      // 解析后的材质（surface.material / override / default）
    Transform3D transform;
};
```

  旧 `Surface::surface` 句柄不再有意义，改为按 `(mesh, surface_index)` 索引 storage。
  如果继续直接持有 `Surface*`，要保证 mesh 不被并发释放（当前单线程、draw 期间无释放，安全）。

### 6.3 空场景 / 缺资源行为

- 没有 `current_scene`、主相机或环境：不再像旧代码那样直接 return（那会让 Vulkan 窗口停留在
  上一帧）。仍然录制一个只有默认帧缓冲 Clear、没有 draw 的 pass，然后 submit + present。
- 没有天空盒：跳过 skybox 管线与 draw。
- 没有阴影（light 不存在、visible=false、cast_shadow=false）：跳过阴影 pass；`frame_bg` 仍绑定
  1x1 回退深度纹理，`cast_shadow=0`。
- 没有材质：保持旧的回退链 `surface.material → instance.override_material → default_material`；
  所有 6 个纹理槽缺失时绑默认纹理，靠 `texture_mask` 跳过采样。

---

## 7. 帧生命周期、资源失效与销毁

### 7.1 MVP 的同步策略

当前后端契约（见 `rendering_device_refactor.h` 工作区注释）：

- 一次 `present()` 之间只接受一次 `queue_submit`；
- 默认 pass 惰性 acquire；`present()` 推进帧；
- `destroy_command_buffer` 必须等命令执行完（帧 fence 或 `queue_wait_idle`）。

因此 server 的 MVP 流程固定为（UBO 写入在录制之前：优先 host-visible 直接写；没有该通道时
退化为每帧一次 `queue_write_buffer`）：

```cpp
encoder = device->create_command_encoder();
... // 录制
cmd = device->encoder_finish(encoder);
device->queue_submit(cmd);
device->present();
device->queue_wait_idle();
device->destroy_command_buffer(cmd);
```

`queue_wait_idle()` 是暂时的正确性保险：它保证下一帧开始时：
- 上一帧命令已经结束，可以安全销毁被替换的 GPU 资源（纹理/视图/sampler/bind group/buffer）；
- `command buffer` 的 render pass/framebuffer 内部对象可以立即释放（D-15 尚未实现延迟销毁）。

后续优化（后端 V6-1/V6-4）：
- 按帧复用 command pool/encoder，避免每帧新建；
- `destroy_*` 交给后端延迟销毁表，server 不再等待 `queue_wait_idle`；
- UBO 若仍走设备本地路径，改为 host-visible ring 或帧批上传，消除每帧同步。

### 7.2 资源替换 / 失效

| 事件 | server 动作 |
|---|---|
| `set_texture_image` 重传同一 RID | 新建 texture/view/sampler → 上传 → 替换字段；`generation++`；把引用该 RID 的所有材质标 dirty，**并立即销毁这些材质持有旧 view 指针的 bind group**；确认没有其它 bind group（如 frame_bg）引用旧 view 后，再销毁旧 view → texture → sampler |
| `set_material_texture` | 更新槽位 RID / `texture_mask`；`bind_group_dirty=true`（旧的 bind group 等到下次 draw 重建时再销毁，期间不得绑定） |
| `set_material_*` 标量参数 | 只影响 `ObjectUniforms`，不重建 bind group |
| `free(texture)` | 先把引用该 RID 的材质标 dirty 并销毁其 bind group（若 frame_bg 也引用该视图则先销毁 frame_bg 并置为待重建）；再销毁 view → texture → sampler |
| `free(material)` | 销毁 `bind_group` 后释放 storage |
| `free(skybox)` | 销毁 cube bind group → cube view → cube texture → sampler |
| `free(light)` | 从 scene.lights 移除（已有逻辑）；销毁 `frame_bg`（如果引用该阴影视图）→ 阴影 view → texture → sampler；置 `frame_bg` 待重建 |
| `free(mesh)` | 销毁每个 surface 的 vertex/index buffer |
| `free(shader)` | 销毁 vertex/fragment modules；默认 shader RID 置 0 |
| 帧内禁止 | draw() 录制期间不要销毁任何被本帧引用的 GPU 对象 |

**bind group 缓存键**：材质 bind group 的 key 用 6 个槽位的
`(texture RID, texture.generation)` 与 sampler 参数。draw 前比较 key：
不一致就销毁旧 bind group（此时上一帧已 wait idle，安全）并重建。这样不需要在每次
`set_material_texture` 里立即建 bind group，也能处理“纹理先于材质上传/反过来”的顺序。

**延迟销毁队列**（建议同时引入，即使 MVP 每帧 wait idle）：`free()`/重传时把待销毁句柄
push 到 `pending_gpu_destroy`，在“已确认 GPU idle”的安全点统一 flush；这样将来去掉
每帧 wait idle 时不需要改所有调用点。

### 7.3 `free()` / `shutdown()` 的销毁顺序

server 必须在 device 之前 shutdown（Application 调用顺序）。顺序：

1. `device->queue_wait_idle()`（确保没有任何在途命令）；
2. 销毁 3 条 pipeline；
3. 销毁所有材质 bind group、天空盒 bind group、frame/shadow/object/skybox frame bind group；
4. 销毁 3 个 pipeline layout；
5. 销毁 6 个 bind group layout；
6. 销毁所有纹理视图（材质纹理、天空盒、阴影、回退深度、默认纹理）→ 纹理 → sampler；
7. 销毁所有 mesh surface 的 vertex/index buffer、frame arena buffer、天空盒几何 buffer；
8. 销毁 shader modules；
9. 清空 storage（RIDStorage 只存 CPU 数据）。

注意依赖方向：pipeline → pipeline layout → bind group layout；bind group → bind group layout；
texture view → texture。先销毁引用者，再销毁被引用者，避免后端悬空指针。

### 7.4 `init()` 的初始化顺序

```text
Application:
  RenderingDeviceRefactor::create_singleton(Vulkan)
  device->init(window_info) → init_device() → init_swapchain()
  RenderingServer::init()
       ├─ 检查 device 非空、`get_swapchain_format()` 是 sRGB 格式（否则报错/切换非 sRGB 变体）、
       │   `get_swapchain_depth_format()` 可用（见 B-4）
       ├─ 加载 3 组 .spv（ShaderLoader::load_spirv）
       ├─ 创建 shader modules
       ├─ 创建 6 个 bind group layout + 3 个 pipeline layout
       ├─ 创建 3 条 pipeline（main/skybox 用 swapchain color + swapchain depth format；
       │   shadow 用 Depth32Float）
       ├─ 创建 frame arena
       ├─ 创建 1x1 回退深度纹理/视图/sampler
       ├─ 创建 frame/shadow_frame/object/skybox_frame bind group
       ├─ 创建默认纹理、默认材质
       └─ 创建共享天空盒立方体几何
```

若任何一步失败：打印明确错误并返回 false；`Application::init` 必须检查返回值并清理已建资源。
注意 `frame_bg` 同时引用 frame UBO 区域与回退/阴影深度视图，所以回退深度资源必须先于
`frame_bg` 创建；阴影启用后重建 `frame_bg` 的流程见 §4.4、§7.2。

---

## 8. Application / Window / CMake 接入

1. `Application::init` 改为创建 `RenderingDeviceRefactor`（Vulkan），不再创建旧 `RenderingDevice`；
   `WindowDesc::driver` 使用 `RenderingDeviceRefactor::get_default_driver()`（Vulkan）。
2. 顺序保持：窗口 → `device->init` → `init_device` → `init_swapchain` → `RenderingServer::init`。
3. `Application::window_event_callback` 收到 `WindowEventType::FramebufferSize` 时，
   在 Vulkan 路径调用 `device->resize_swapchain(event.w, event.h)`；零尺寸（最小化）由后端推迟。
   resize 与 `draw()` 不在同一时刻并发，后端 `init_swapchain` 会先 wait idle，因此安全。
   `draw()` 每帧比较缓存的 `get_swapchain_format()`，若 resize 后格式发生变化则重建
   main/skybox 管线（阴影管线只依赖 Depth32Float，不需要重建）。
4. `Window::swap_buffers()` 在 Vulkan 下继续是空操作；present 由 `RenderingServer::draw()` 负责。
   **不要**在 Application 里再调用一次 present。
5. `Application::shutdown` 顺序：`SceneTree::destroy` → `RenderingServer` shutdown/销毁 →
   device shutdown → device 单例销毁 → resource loader → window。
6. CMake：
   - `RenderingServer` 的源码不依赖 Vulkan 类型，不需要 `#ifdef`；但运行需要
     `ORCA_RENDERER_VULKAN=ON`。未启用 Vulkan 时 `RenderingDeviceRefactor::create_singleton`
     返回空单例，`RenderingServer::init` 打印错误并失败。
   - 旧 `RenderingDevice` / `opengl` 源码暂时保留，供 `orca-platform-window-test` 等旧目标使用；
     `RenderingServer` 不再引用它们。待 V7 对照结束后再删除旧设备与 GL shader 路径。
   - Vulkan 构建仍需要 `glslc` 编译 `shaders-vulkan/`；新增的 3 组 shader 目录会被
     `orca-shader-spirv` 自动发现（`CONFIGURE_DEPENDS`）。
7. 相机宽高比：现有 `Camera3D` 的 aspect 在构造时固定，resize 后不会自动更新；
   本期沿用现状（不在 server 重构范围内），后续可以给 `Viewport` 加尺寸变化通知。

---

## 9. 分阶段实施计划

> 建议按依赖顺序推进，每一阶段都可构建、可手动验证；不要一次性重写后再调。

### R0：确认后端契约与缺口（0.5~1 天，可与适配层收尾并行）

- [ ] 确认 `pass_draw` / `queue_submit` / `present` 的最终语义与 header 注释一致
      （一次 present 一次 submit；默认 pass 惰性 acquire；destroy 前需 idle/fence）。
- [ ] 决策 B-4：补 `RenderingDeviceRefactor::get_swapchain_depth_format()`，或者
      Vulkan 后端在 `init_swapchain` 只接受 `D32Sfloat`、回退时明确失败。
- [ ] 确认 B-5：draw 路径在采样纹理 `current_layout` 与 descriptor 布局不一致时插入
      image barrier（至少覆盖 `DepthStencilAttachmentOptimal → DepthStencilReadOnlyOptimal`）。
      未完成则 MVP 关闭阴影。
- [ ] 决策 B-6：优先给 RHI 增加 host-visible buffer / 直接写通道（`GPUBufferDesc.host_visible`
      或 `map_buffer`），让 frame arena 每帧写入不产生 submit+fence；若暂不扩展，明确接受
      “每帧一次同步 `queue_write_buffer`”的临时性能代价并记入风险。
- [ ] 确认 B-9：默认帧缓冲 pass、阴影 explicit pass 与管线兼容 pass 使用同一组 subpass
      dependency 模板（AGENTS §5.3 #32）；用真实 draw 的 validation 验证。
- [ ] 确认 `create_bind_group` 的 UBO `size` 显式值、动态偏移对齐（256）与 arena 方案可行。
- 验收：能在 `orca-vulkan-init-test` / 临时 draw 冒烟里画出主 pass 与采样阴影贴图（或明确记录阴影暂缓）。

### R1：数据模型与 uniform 骨架（1~2 天）

- [ ] 改 `rendering_storage.h` 的 GPU 字段（按 §3.3）；保持所有 RID 类型不变。
- [ ] 新增 `rendering_uniforms.h`：4 个 UBO 结构、arena 偏移/步长常量、`static_assert`、
      矩阵/颜色填充辅助。
- [ ] 新增纯函数（可单测）：通道格式映射、mip 数计算、sampler key、bind group key 比较、
      DrawItem 过滤。
- [ ] 补单测 `tests/servers/test_rendering_uniforms.cpp`（布局偏移/size/arena 对齐）与
      `tests/servers/test_rendering_server_helpers.cpp`（纯逻辑），加入 `tests/CMakeLists.txt`。
- 验收：`orca-tests` 通过；新结构在 Vulkan 与非 Vulkan 构建中都能编译。

### R2：shader / 布局 / 管线（1~2 天）

- [ ] 新增 `shaders-vulkan/builtin/{vert,frag}.glsl` 与 `builtin-depth/{vert,frag}.glsl`；
      按 §5.1 的规则完成 set/binding、Y 翻转、Z 重映射、颜色空间。
- [ ] 新增 `rendering_pipeline.h/.cpp`：加载 `.spv`、创建 modules、6 个 BGL、3 个 pipeline layout、
      3 条 pipeline；缓存 color/depth format，格式变化时重建。
- [ ] `RenderingServer::init` 调用它；失败时打印并返回 false。
- [ ] 用 `orca-vulkan-init-test` 的管线创建路径或新增轻量目标，验证：
      main/shadow/skybox 三条管线都能创建，且能绑定到对应附件格式的 pass。
- 验收：无 validation VUID；管线创建失败有明确错误。

### R3：资源上传（2~3 天）

- [ ] `add_mesh_surface` 改为交错顶点 + 索引 buffer（§4.1）。
- [ ] `set_texture_image` 改为 Texture/View/Sampler + 上传 + mipmap（§4.2）。
- [ ] `set_skybox_cubemap` 改为 cube 纹理 6 面上传 + cube 视图 + bind group（§4.3）。
- [ ] `set_light_cast_shadow` 改为深度纹理/视图/sampler（§4.4）；无阴影回退深度纹理。
- [ ] 默认纹理/材质、天空盒共享立方体（§4.5）。
- 验收：不渲染的状态下创建/销毁/重传所有资源，validation 无错误；`free()` 后无句柄泄漏。

### R4：主 pass 打通（1~2 天）

- [ ] `draw()` 实现帧生命周期（§7.1）：收集 DrawItem、写 UBO、主 pass 录制、submit、present、
      wait、destroy。
- [ ] 材质 bind group 惰性创建/缓存/失效（§7.2）。
- [ ] 空场景 clear-only 帧、缺材质/缺纹理回退。
- 验收：Vulkan 构建运行 `test_application`，4 个球体以 PBR 显示，窗口连续帧稳定；
      validation 全绿；resize 后继续渲染。

### R5：阴影 + 天空盒（1~2 天）

- [ ] 阴影 pass（显式深度附件）+ shadow pipeline + `ShadowUniforms`。
- [ ] 主 pass `frame_bg` 绑定阴影贴图；确认 B-5 barrier；否则本阶段暂时保持关闭并记录。
- [ ] 天空盒 pipeline + 全局立方体 + cube bind group。
- 验收：带 shadow 的平行光场景有阴影；天空盒方向正确、无上下颠倒；z-fighting/深度关系与 GL 对照一致。

### R6：生命周期、resize、清理（1 天）

- [ ] `free()` 全类型、shutdown 顺序、延迟销毁队列。
- [ ] Application 的 resize 转发、init 失败清理、shutdown 顺序。
- [ ] 旧 `RenderingDevice` 从 server 彻底移除；更新 `AGENTS.md` 与
      `docs/vulkan_rhi_checklist_v2.md`（D-12 / V7-3 状态）。
- 验收：反复 enter/exit tree、重传纹理、resize、最小化/恢复、退出，validation 无错误、
      无 Memory 分配增长。

### R7：对照与验收（1 天）

- [ ] 同一场景分别用旧 GL 路径（保留的旧设备/旧 server 分支或临时对照程序）与 Vulkan 新路径
      截图对照（V7-2）。
- [ ] 记录已知差异（阴影 PCF 边界、采样精度、天空盒 clamp/repeat）。
- 验收：主画面颜色/明暗/几何关系一致；差异有明确解释。

---

## 10. 风险与待决策

| 风险 | 影响 | 应对 |
|---|---|---|
| 默认深度格式查询缺失（B-4） | 主/天空盒管线与默认 pass 不兼容，`vkCmdBindPipeline`/draw validation 报错 | R0 补查询或强制 D32Sfloat |
| 采样纹理 barrier 缺失（B-5） | 阴影贴图采样布局非法；1x1 回退深度纹理也可能触发 | R0 补 barrier；否则 MVP 关闭阴影 |
| `queue_write_buffer` 每帧同步（若 R0 未扩展 host-visible） | 每帧一次 submit+fence，CPU/GPU 串行，帧率下降 | 优先 R0 扩展 host-visible UBO；否则作为 MVP 临时妥协并在 R4 记录 |
| render pass 兼容性 dependency 不一致（B-9） | 真实 draw 时 validation 报 `VUID-vkCmdDraw-renderPass-02684` | R0 让后端模板统一；R4/R5 真实 draw 验收 |
| 每帧 `queue_wait_idle` | GPU 无法流水，CPU 等待 | MVP 接受；后端帧 fence + 延迟销毁后移除 |
| arena 扩容 / 动态偏移对齐 | bind group 重建、对齐错误会导致绑定失败 | 固定 256 步长、扩容时重建四个 bind group、单测校验 |
| 材质 bind group 频繁重建 | 纹理/材质 load 顺序导致 draw 前重建，池分配开销 | 惰性 + key 缓存；同材质连续 draw 复用 |
| 3 通道纹理展开 | CPU 内存/上传增大，加载路径不变 | 只在上传时展开，记录约定 |
| shader 两份变体语义漂移 | Vulkan 画面与 GL 不一致 | §5.1 规则 + R7 视觉对照 + 注释互相引用 |
| 默认 pass 固定 Clear | 天空盒/后处理必须同 pass，不能拆 | 本期天空盒同 pass；后续做后处理时再扩展 RHI 的 load/store 缓存 |
| resize 后相机 aspect 不更新 | 画面拉伸 | 已知现状，后续单独处理；本期 resize 交换链正确即可 |
| 单帧单 submit 限制 | 上传与帧提交不能并行，阴影不能单独 submit | 所有 pass 录进同一 command buffer；上传仍走一次性提交 |

**明确不在本期做**：多光源/点光阴影、IBL/环境反射、透明/混合排序、MSAA、后处理、HDR、
多视口、pipeline cache 磁盘缓存、bindless/纹理数组、多线程录制。

---

## 11. 测试与验收

### 单元测试（无窗口/GPU）

| 测试 | 内容 |
|---|---|
| `tests/servers/test_rendering_uniforms.cpp` | 4 个 UBO 的 `sizeof`/offset 与 std140 预期一致；arena 偏移（0/512/768）与 256 对齐；`ObjectUniforms` stride ≥ size |
| `tests/servers/test_rendering_server_helpers.cpp` | channels+srgb → `Format` 映射；mip 数计算；sampler key 比较；材质 bind group key 失效；DrawItem 过滤/回退材质链 |
| 已有 `tests/servers/test_rhi_validation.cpp` | 继续覆盖 bind group/pipeline 描述约束；新增 helper 不应绕过共享校验 |

> 所有新增测试文件都要加入 `tests/CMakeLists.txt` 的 `orca-tests` 源列表。

### GPU 集成测试（手动）

1. Vulkan-only 构建（`-DORCA_RENDERER_OPENGL=OFF -DORCA_RENDERER_VULKAN=ON`）；
   从 `build/` 运行 `tests\orca-application-test.exe`。
2. Validation 全绿：无 VUID、无 image layout 报错、无 descriptor 未写入。
3. 主画面：4 个 PBR 球体、材质贴图、法线/高度视差、方向光；与旧 GL 路径对照（含清屏色：
   Vulkan sRGB 交换链的 clear 值要按线性给，见 §6.2）。
4. 阴影：打开 `Light3D::set_shadow(true)`，确认阴影方向/深度关系正确（依赖 B-5）。
5. 天空盒：`World3D::ensure_skybox` 场景，确认六面方向正确、无上下颠倒/接缝。
6. resize：拖拽窗口、最小化/恢复，交换链重建后继续渲染。
7. 生命周期：反复 enter/exit scene tree、重传纹理、退出程序，validation 无错误、
   `Memory::get_allocation_count()` 无异常增长。
8. 空场景/缺相机/缺环境：不崩溃，窗口显示 clear 色。

### 完成判据

- `ctest --test-dir build --output-on-failure` 通过；
- Vulkan-only 与“GL+Vulkan 双后端”配置都能构建；
- 演示场景在 Vulkan 下画面正确、validation 全绿、resize/退出稳定；
- `RenderingServer` 源码不再出现 `RenderingDevice::get_singleton()` / `set_uniform_*` / `bind_texture_*`；
- `AGENTS.md`、`docs/vulkan_rhi_checklist_v2.md` 的状态同步更新。

---

## 12. 改动文件清单（预估）

| 文件 | 改动 |
|---|---|
| `servers/rendering/rendering_storage.h` | GPU 字段类型迁移；新增材质 bind group 缓存字段、纹理 generation、阴影/天空盒句柄 |
| `servers/rendering/rendering_uniforms.h` | **新增**：UBO 结构、arena 常量、填充辅助、static_assert |
| `servers/rendering/rendering_pipeline.h/.cpp` | **新增**：SPIR-V 模块、BGL/pipeline layout、3 条管线、管线缓存 |
| `servers/rendering/rendering_server.h` | 私有成员替换（旧 shader program / framebuffer / surface 句柄 → RHI 句柄）；新增 shutdown/帧状态 |
| `servers/rendering/rendering_server.cpp` | 全面重写：init、资源上传、draw、free、shutdown |
| `servers/rendering/shaders-vulkan/builtin/{vert,frag}.glsl` | **新增** PBR Vulkan 变体 |
| `servers/rendering/shaders-vulkan/builtin-depth/{vert,frag}.glsl` | **新增** 阴影 Vulkan 变体 |
| `servers/rendering/shaders-vulkan/builtin-skybox/*` | 复核/调整 set/binding 与深度约定 |
| `application/application.cpp` | 改新 RHI 初始化、resize 转发、init 失败处理、shutdown 顺序 |
| `servers/rendering/CMakeLists.txt` | 新源文件加入；shader 目录自动发现（已可用） |
| `tests/CMakeLists.txt` + `tests/servers/test_rendering_*.cpp` | 新增纯逻辑测试 |
| `AGENTS.md`、`docs/vulkan_rhi_checklist_v2.md` | 状态与约定同步 |

不改：`resource/**`、`scene/**`（除非后续要补 aspect/resize 通知）、`core/**`。

若 R0 采纳 B-4 / B-6 / B-9 的后端扩展，还需额外改：`rendering_device_refactor.h`
（`get_swapchain_depth_format()`、`GPUBufferDesc.host_visible`）、`vulkan/vk_swapchain.cpp`
（深度格式查询/保证）、`vulkan/vk_buffer.cpp` + `vulkan/vk_memory.*`（host-visible 分配与直接写）、
`rhi_validation.*`（新增字段校验）、以及 `vulkan/vk_command.cpp` / `vk_pipeline.cpp`
（unified subpass dependency 模板与采样 barrier）。这些改动应在 R0 独立提交、先于 server 重写，
避免 server 开发期间反复调整接口。

---

## 附录 A：旧 API → 新 RHI 调用映射

| 旧 `RenderingServer` 调用点 | 旧 `RenderingDevice` 调用 | 新 RHI 调用 |
|---|---|---|
| `create_builtin_skybox_cube` | `create_mesh_surface` | 建全局 vertex/index `GPUBuffer` + `queue_write_buffer` |
| `create_shader_program(source)` | `create_shader_program(vertex, fragment)` | `create_shader_module(SPIRV×2)` |
| `create_depth_map_pass` | `create_framebuffer` + `create_depth_map` | 不再需要 FBO；阴影用 `create_texture(Depth32Float)` + view + sampler |
| `set_texture_image` | `create_texture_2d` | `create_texture` + `create_texture_view` + `create_sampler` + `queue_write_texture` + `generate_mip_map` |
| `set_skybox_cubemap` | `create_cube_map_texture` | cube `create_texture` + 6×`queue_write_texture` + cube view + sampler |
| `add_mesh_surface` | `create_mesh_surface` | 2 个 `GPUBuffer` + 2 次 `queue_write_buffer` |
| `render_pass_depth_map` | `bind_framebuffer_depth_map` / `bind_shader_program` / uniforms / `draw_call` | `encoder_begin_render_pass(depth)` + `pass_set_pipeline` + bind groups + `pass_draw_indexed` |
| `render_pass_scene` | `bind_shader_program` + `set_uniform_*` + `bind_texture_2d` + `draw_call` | 主 pass + frame/object/material bind group + `pass_draw_indexed` |
| `render_pass_skybox` | skybox program + cubemap + `draw_call` | skybox pipeline + skybox bind groups + `pass_draw_indexed` |
| `clear()` | `glClear` | `RenderPassDesc` 的 Clear load op（默认帧缓冲） |
| `set_viewport/reset_viewport` | `glViewport` | pass 默认 viewport；阴影 pass render area 由附件推导 |
| `draw()` 帧结束 | `swap_buffers`（GL） | `encoder_finish` + `queue_submit` + `present` |
| `free()` | `destroy_texture_2d` / `destroy_cube_map_texture` / `destroy_depth_map` / `destroy_mesh_surface` / `destroy_shader_program` | `destroy_texture_view` → `destroy_texture` / `destroy_sampler` / `destroy_buffer` / `destroy_shader_module` / `destroy_bind_group` |

---

## 附录 B：建议的 RHI 最小扩展（R0，需先于 server 重写落地）

这些扩展只补 server 无法用现有接口表达的必需信息/通道，不改变既有语义：

```cpp
// rendering_device_refactor.h
class RenderingDeviceRefactor
{
    ...
    /// @brief 默认帧缓冲（交换链）深度附件的格式。
    /// 主 PBR / 天空盒管线的 depth_stencil.format 必须使用该值；
    /// 后端只支持能映射到 Format 的深度格式（当前应保证 Depth32Float）。
    virtual Format get_swapchain_depth_format() const = 0;
};

struct GPUBufferDesc
{
    std::string label;
    uint64_t size = 0;
    uint8_t usage = 0;
    /// @brief true 表示该 buffer 需要 CPU 每帧更新：后端应分配 host-visible/coherent 内存，
    ///        queue_write_buffer 直接 map/memcpy/flush，不产生命令提交与 fence 等待。
    ///        仅建议用于 UNIFORM + COPY_DST 的小缓冲区（frame arena），不支持读回。
    bool host_visible = false;
};
```

Vulkan 侧落地要点：
- `create_buffer`：`host_visible` 时内存类型选择 `eHostVisible | eHostCoherent`，不要求 `eDeviceLocal`
  （可按 vk_memory 现有分配器加 `prefer_host_visible` 参数）；仍保留 `COPY_DST` usage 校验。
- `queue_write_buffer`：host-visible 分支直接 map/memcpy/flush（整段或子区间），**不**调用
  `submit_immediate_commands`；设备本地分支保持现有 staging + submit + fence 语义。
- 两种分支都必须校验 offset/size 区间；`GPUBufferHandleData` 需要记录 `host_visible` 与映射指针
  （或每次 map/unmap），并保持“buffer 先于 memory 销毁”的声明顺序。
- 新字段必须经过 `rhi_validation` 校验（例如 host_visible 只允许配合 UNIFORM/COPY_DST，
  不允许同时声明 VERTEX/INDEX/STORAGE 等主用途，具体规则在 R0 决策）。

如果团队不希望扩展描述符，替代方案是新增 `map_buffer`/`unmap_buffer` 或专门的
`queue_write_buffer_host_visible`；但不要继续在每帧路径上使用现有的同步
`queue_write_buffer`（AGENTS §5.3 #25）。