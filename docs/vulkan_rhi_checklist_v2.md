# Vulkan 适配层清单（RenderingDeviceRefactor）

> 版本：**2026-11-18 复核版**（代码基线 `c49f58e`，工作区干净）
> 审计对象：`servers/rendering/rendering_device_refactor.h`（WebGPU 风格接口，44 个纯虚函数）
> 与 `servers/rendering/vulkan/*`（Vulkan 适配层）。
> 复核方式：全量重读 Vulkan 适配层源码并逐接口对照，配合三配置构建、`ctest`、
> `orca-vulkan-init-test` 冒烟（证据见 §2）。
> 当前阶段：**V0 / V1 / V2 已完成**（构建、句柄、格式映射、描述校验、纹理与缓冲区显存、
> 内容上传、mipmap、纹理视图、采样器）。**V3 起尚未开始**，适配层仍不能渲染任何内容。
> 本次整理原则：已完成项直接标记完成；原先按提交批次记录的逐条修复细节不再保留
> （已体现在代码中）；只留下当前状态、未完成项、仍开放缺陷、待决策缺口与后续计划。
> 历史审计基线（2026-11-16）保留在原文件 `docs/vulkan_rhi_checklist.md`，不再维护。
> 注意：原 Refactor 版 OpenGL 适配层已于 2026-11-18 移除，本清单只针对 Vulkan。

---

## 1. 结论摘要

1. **初始化链路可用。** `VkRenderingDevice` 完整实现 44 个纯虚函数签名；
   `init → init_device → init_swapchain` 可在真实窗口上跑通（本机 Intel Graphics，
   交换链 800x600、3 张图像、实际格式 `BGRA8UnormSrgb`），异常被捕获、validation 无 VUID。
2. **资源创建链路（V2）可用。** 纹理（颜色/深度/cubemap）可创建、分配并绑定独占显存，
   可上传内容（含带行距的部分区域、逐 cubemap 面）、生成 mip 链；缓冲区可创建并写入；
   纹理视图（2D/深度/cubemap + mip 子范围）与采样器（含 `compare_op`）可创建；
   所有描述先经过后端无关的 `rhi_validation.*` 校验，非法描述在进入 driver 前被拒绝。
3. **仍然不能出画面。** 绑定组/描述符集、管线布局、完整管线状态、`pass_set_bind_group`、
   顶点/索引绑定、draw、`queue_submit`、`present`、交换链图像视图/深度缓冲/帧缓冲全部缺失；
   新 RHI 也不在任何一帧的渲染路径中（`RenderingServer` 与 `Application` 仍走旧 `RenderingDevice`）。
   直接补 `pass_draw` 依然无效，必须先完成 V3/V4/V5。
4. **接口实现数量统计**（判据见 §3）：

   | 状态 | 数量 | 占比 |
   |---|---|---|
   | ✅ 可用 | 25 | 57% |
   | ⚠️ 有代码但当前不可用 / 违反契约 | 5 | 11% |
   | ❌ 未实现 | 14 | 32% |

   > 变化：`destroy_render_pipeline`（I-29）已实现销毁，统计由上轮 24/5/15 变为 25/5/14。

5. **推进顺序**：V3（交换链图像视图/深度/帧缓冲/acquire-present）→ V4（SPIR-V 工具链）→
   V5（descriptor set/pipeline layout/完整管线）→ V6（draw/submit/同步/延迟销毁）→
   V7（GPU 验收与接入 `RenderingServer`）。每个阶段都有前置依赖，不要跳步。

### 1.1 下一批任务（按依赖排序）

- [ ] **V3-1** 交换链图像视图、深度缓冲、每图像 render pass/framebuffer 缓存（D-16/D-07/G-07/G-09）。
- [ ] **V3-2** acquire/present 同步：`vkAcquireNextImageKHR` + semaphore + 每帧 fence（D-16/D-24）。
- [ ] **V3-3** `encoder_begin_render_pass` 支持默认帧缓冲：无效 color view → 当前交换链图像，
      无效 depth view → 内部深度缓冲，`width/height == 0` 由附件推导（D-07）。
- [ ] **V3-4** `present` / `resize_swapchain` / 平台 resize 事件转发（D-25）。
- [ ] **V4-1** 确定并落地 Shader 工具链（SPIR-V 来源类型 + Vulkan 专用 GLSL 变体 + `glslc`）（D-11/G-01/G-02）。
- [ ] **V4-2** `create_shader_module` 按源类型处理 SPIR-V（长度/对齐/入口名/stage 一致性）（D-11/D-27）。
- [ ] **V5-1** descriptor set layout / pool / set 与 pipeline layout（D-06）。
- [ ] **V5-2** 补全 `create_render_pipeline`（stages、顶点输入、renderPass 兼容、颜色混合、深度、MSAA、
      使用传入的 `pipeline_layout`）（D-10/G-09/D-20/D-28）。
- [ ] **V6-1** pass 绑定与 draw（`vkCmdBindDescriptorSets`/顶点/索引/`vkCmdSetScissor`/draw）（D-08/G-04）。
- [ ] **V6-2** `queue_submit`、`present`、帧 fence 与延迟销毁（D-09/D-15/G-10）。
- [ ] **V7-1** 手工 GPU 测试目标：clear → 三角形 → 纹理四边形 → 深度 → cubemap（D-36）。
- [ ] **V7-2** 与旧 GL 路径同场景像素/视觉对照。
- [ ] **V7-3** 把 `RenderingServer` 切到 `RenderingDeviceRefactor`（D-12）。

---

## 2. 验证现状（可复现）

| 配置 | 构建 | 单元测试 | `orca-vulkan-init-test` 冒烟 |
|---|---|---|---|
| Vulkan + GL（`ORCA_RENDERER_OPENGL=ON`、`VULKAN=ON`） | ✅ `orca-tests` + `orca-vulkan-init-test` 构建通过（仅一编译警告，见 D-38） | ✅ `ctest` 通过 | ✅ 退出码 0，纹理/视图/采样器/缓冲区全通过，validation 无 VUID |
| Vulkan-only（`OPENGL=OFF`、`VULKAN=ON`） | ✅ `orca-tests` + `orca-vulkan-init-test` 构建通过 | ✅ `ctest` 通过 | ✅ 退出码 0 |
| GL-only（`OPENGL=ON`、`VULKAN=OFF`，默认） | ✅ `orca-tests` 构建通过 | ✅ `ctest` 通过 | ✅ 输出 SKIP（stderr 一行“后端不可用”提示）、退出码 0 |

> 表中是本轮实际重跑的结果（目标级构建 + `orca-tests`/冒烟）；上一轮审计另做过全目标构建，
> 结论一致（三配置均可配置并构建）。

复现命令：

```powershell
# 双后端
cmake -S . -B build-gl-vk -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug `
  -DORCA_RENDERER_OPENGL=ON -DORCA_RENDERER_VULKAN=ON
cmake --build build-gl-vk --parallel
ctest --test-dir build-gl-vk --output-on-failure

# 纯 Vulkan
cmake -S . -B build-vk-only -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug `
  -DORCA_RENDERER_OPENGL=OFF -DORCA_RENDERER_VULKAN=ON
cmake --build build-vk-only --parallel

# 只初始化不进入窗口循环
$env:ORCA_VULKAN_INIT_TEST_EXIT_AFTER_INIT = '1'
.\build-gl-vk\tests\orca-vulkan-init-test.exe
```

本轮冒烟关键输出（Intel Graphics，Debug + validation layer）：

```text
[INFO] created window surface
[INFO] Using Physical Device: Intel(R) Graphics
[INFO] Created Vulkan Logical Device
[INFO] Vulkan created swapchain: 800x600, 3 images, format=9
[TEST] Vulkan initialized: swapchain format=9, size=800x600
[INFO] Vulkan created texture(smoke-cube): 16x16, 5 mip(s), 6 layer(s), 24.00 KiB
[INFO] Vulkan created texture view(smoke-cube): Cube, mip 0..4, layer 0..5
[INFO] Vulkan generated mip maps(smoke-cube-mips): 5 level(s)
[INFO] Vulkan created buffer(smoke-vertex): 4096 byte(s), usage 0x09, 4.00 KiB
[TEST] buffer create/write OK (vertex + index with offset + uniform).
[TEST] invalid descriptions rejected.
```

- `format=9` 即 `Format::BGRA8UnormSrgb`：交换链格式已能被接口表达。
- 日志中的 KiB 来自 `vkGetImageMemoryRequirements2` / `vkGetBufferMemoryRequirements2`，
  说明显存确实分配并绑定。
- validation 输出中**没有 VUID / validation error**（只有 loader/层的注册表与卸载日志）。
- 冒烟覆盖：instance/device/swapchain、命令编码器往返、颜色/深度/cubemap 纹理与视图（含
  cubemap mip 子范围与 6 类非法视图拒绝）、纹理整图/带行距部分区域上传、cubemap 逐面 + mipmap、
  缓冲区创建/偏移写入、非法描述拒绝。
- **未覆盖**：任何 draw/submit/present、render pass 录制、绑定组、管线状态、validation
  失败即红、GPU 读回比对（见 D-36）。

---

## 3. 状态判据

| 标记 | 含义 |
|---|---|
| ✅ 可用 | 该接口的语义基本被正确实现，当前没有已知阻塞缺陷（剩余小问题在备注中列出）。 |
| ⚠️ 有代码但当前不可用 | 已写实现，但存在 P0/P1 缺陷：调用会失败、泄漏、违反接口契约，或只能完成一半流程。 |
| ❌ 未实现 | 空桩、无条件返回空句柄，或只更新了窗口尺寸字段，没有后端行为。 |

---

## 4. 接口逐项清单（44 项）

### 4.1 初始化 / 交换链（8 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-01 | `init` | ✅ | `vulkan/vk_rendering_device_init.cpp:291` | instance + debug messenger（仅 validation 开启时创建并链入实例 `pNext`）、surface 主流程；异常被捕获并打印。 |
| I-02 | `init_device` | ✅ | `vk_rendering_device_init.cpp:310` | 物理设备选择 + 逻辑设备 + 双队列；缓存 `memory_properties` 供显存分配；异常被捕获。评分函数仍是死代码（D-33）。 |
| I-03 | `init_swapchain` | ⚠️ | `vulkan/vk_swapchain.cpp:109` | 创建 swapchain 与 `vk::Image` 数组，使用实际 surface 格式/extent（本机 BGRA8UnormSrgb）；**没有 image view、没有深度缓冲、没有 per-image framebuffer、没有 acquire/present 同步**（D-16）。 |
| I-04 | `resize_swapchain` | ❌ | `vk_swapchain.cpp:195` | 只改 `window_info.width/height`，重建仍是 TODO；且无调用方（D-25）。 |
| I-05 | `get_swapchain_format` | ✅ | `vk_swapchain.cpp:5` | 用 `vk_format_from_vulkan` 把实际 `swapchain_image_format` 翻译成接口 `Format`，不可表达时告警。 |
| I-06 | `get_swapchain_size` | ✅ | `vk_swapchain.cpp:16` | 返回 `swapchain_image_extent`；未初始化时回退 `window_info`。 |
| I-07 | `present` | ❌ | `vulkan/vk_rendering_device.cpp:6` | TODO(V3) 空实现；没有 `vkQueuePresentKHR`，窗口不会显示渲染结果。 |
| I-08 | `shutdown` | ✅ | `vk_rendering_device.cpp:11` | `waitIdle`（捕获异常）+ 依赖 RAII 成员析构。 |

### 4.2 Shader（2 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-09 | `create_shader_module` | ⚠️ | `vulkan/vk_resource.cpp:60` | 把 `desc.source` 直接当 SPIR-V（`codeSize = source.size()`、`reinterpret_cast<uint32_t*>`）；接口实际传的仍是 GLSL 文本，也没有 4 字节长度/对齐检查、没有入口名处理。属 D-11/G-01/G-02。 |
| I-10 | `destroy_shader_module` | ✅ | `vk_resource.cpp:92` | 正确析构 `VkShaderModuleData`。 |

### 4.3 缓冲区（3 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-11 | `create_buffer` | ✅ | `vulkan/vk_buffer.cpp:29` | 共享描述校验 + usage→`vk::BufferUsageFlags` + `vkGetBufferMemoryRequirements2`（含 dedicated 需求）+ 设备本地独占显存 + `vkBindBufferMemory`；uniform/storage 尺寸按设备 limits 校验。 |
| I-12 | `queue_write_buffer` | ✅ | `vulkan/vk_buffer.cpp:192`（staging 池在 `:140`） | `COPY_DST`/区间/空指针校验 + 设备级复用 host-visible staging（map/memcpy/flush/unmap）+ `vkCmdCopyBuffer` + 两道 memory barrier，同步提交并等待 fence。 |
| I-13 | `destroy_buffer` | ✅ | `vk_buffer.cpp:275` | `memdelete` 触发 RAII 析构；buffer 先于其绑定内存释放。延迟销毁见 D-15。 |

### 4.4 纹理 / 视图 / 采样器（8 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-14 | `create_texture` | ✅ | `vulkan/vk_resource.cpp:108` | 共享校验 + 格式映射 + 合法 `VkImageCreateInfo`（arrayLayers、extent.depth=1、cubemap 用 `e2D + eCubeCompatible`、深度格式用深度附件用途）+ 独占显存分配/绑定；颜色/深度/cubemap 已实测可创建。内容上传见 I-17。 |
| I-15 | `create_texture_view` | ✅ | `vk_resource.cpp:522`；`rhi_validation.cpp:139` | 共享层 `rhi_resolve_texture_view_range` 解析 mip/层默认范围与 cubemap 6 层对齐；`viewType`/父纹理 `format`/完整 `subresourceRange`（aspect 由父格式推导）均正确填充；已记录解析后范围。 |
| I-16 | `create_sampler` | ✅ | `vk_resource.cpp:604` | filter/address/mipmap/lod/compareEnable/`compareOp` 映射正确；lod 经共享校验（NaN/Inf、顺序、`maxLod <= 1000`）。anisotropy 仍不在接口（D-26 剩余项）。 |
| I-17 | `queue_write_texture` | ✅ | `vk_resource.cpp:245` | usage/mip/层/区域/步长/`data_size` 校验 + 临时 host-visible staging（CPU 按行重排为紧密打包）+ 整图 layout barrier + `vkCmdCopyBufferToImage`，同步提交。 |
| I-18 | `generate_mip_map` | ✅ | `vk_resource.cpp:414` | 校验用法（`COPY_SRC\|COPY_DST`）与格式 blit 能力后，用 `vkCmdBlitImage` 逐级下采样，每层前后做 layout barrier；同步提交。 |
| I-19 | `destroy_texture` | ✅ | `vk_resource.cpp:657` | `memdelete`；成员声明顺序保证 image 先于内存释放。延迟销毁见 D-15。 |
| I-20 | `destroy_texture_view` | ✅ | `vk_resource.cpp:664` | 同上。 |
| I-21 | `destroy_sampler` | ✅ | `vk_resource.cpp:671` | 同上。 |

### 4.5 绑定组 / 管线布局（6 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-22 | `create_bind_group_layout` | ❌ | `vulkan/vk_rendering_device.cpp:24` | 返回空句柄；`vk_internal.h` 也没有对应 `Vk...Data` 结构。 |
| I-23 | `create_bind_group` | ❌ | `vk_rendering_device.cpp:30` | 返回空句柄。 |
| I-24 | `create_pipeline_layout` | ❌ | `vk_rendering_device.cpp:36` | 返回空句柄。 |
| I-25 | `destroy_bind_group` | ❌ | `vk_rendering_device.cpp:42` | 空实现。 |
| I-26 | `destroy_bind_group_layout` | ❌ | `vk_rendering_device.cpp:47` | 空实现。 |
| I-27 | `destroy_pipeline_layout` | ❌ | `vk_rendering_device.cpp:52` | 空实现。 |

### 4.6 渲染管线（2 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-28 | `create_render_pipeline` | ⚠️ | `vulkan/vk_pipeline.cpp:55` | 有骨架且入口走共享校验、失败路径清理正确，但关键字段仍缺失/写错：两个 shader stage 都没设 `stage` 与 `pName`；顶点输入为空；未设 `renderPass` 也未启用 dynamic rendering；颜色混合状态没挂到 `vk::PipelineColorBlendStateCreateInfo`（`color_blend_attachment` 是死变量）；忽略 `desc.pipeline_layout`；深度状态硬编码（depth test 开、write 关、compare `eNever`）；MSAA 硬编码 `e1`。 |
| I-29 | `destroy_render_pipeline` | ✅ | `vk_pipeline.cpp:217` | `memdelete` 释放 `VkGraphicsPipelineHandleData`（pipeline + 内部 layout 均由 RAII 释放）。 |

### 4.7 命令录制（12 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-30 | `create_command_encoder` | ✅ | `vulkan/vk_command.cpp:46` | queue-family 命令池 + 单个 primary command buffer，并已 `begin()`；唯一 handle；失败路径释放 data。 |
| I-31 | `encoder_begin_render_pass` | ⚠️ | `vk_command.cpp:89` | 能创建 render pass + framebuffer 并 `beginRenderPass`，但：attachment 没设 format/samples/initialLayout/finalLayout；没设 subpass/dependency；不支持默认帧缓冲（无效 view 被 `continue` 跳过，附件为空时会创建 0 subpass 的非法 render pass）；`desc.width/height == 0` 未由附件推导；render pass/framebuffer 每个 pass 新建且无销毁路径（D-07/D-15/D-19）。 |
| I-32 | `pass_set_pipeline` | ✅ | `vk_command.cpp:182` | 正确调用 `vkCmdBindPipeline`（前提是管线本身可用）。 |
| I-33 | `pass_set_bind_group` | ❌ | `vk_command.cpp:194` | 空实现；无 `vkCmdBindDescriptorSets`/动态偏移。 |
| I-34 | `pass_set_vertex_buffer` | ❌ | `vk_command.cpp:204` | 空实现。 |
| I-35 | `pass_set_index_buffer` | ❌ | `vk_command.cpp:213` | 空实现。 |
| I-36 | `pass_set_viewport` | ⚠️ | `vk_command.cpp:222` | 设了 `VkViewport`；但管线把 scissor 设为 dynamic，却没有任何 `vkCmdSetScissor` → 绘制时 scissor 状态未定义（D-20/G-04）。 |
| I-37 | `pass_draw_indexed` | ❌ | `vk_command.cpp:232` | 空实现。 |
| I-38 | `pass_draw` | ❌ | `vk_command.cpp:242` | 空实现。 |
| I-39 | `pass_end` | ✅ | `vk_command.cpp:251` | 调用 `vkCmdEndRenderPass`。资源释放不在本接口职责内（泄漏见 D-15）。 |
| I-40 | `encoder_finish` | ✅ | `vk_command.cpp:260` | `end()` 后把命令池 + command buffer 一起 move 到 `VkCommandBufferHandleData`，删除 encoder data。 |
| I-41 | `destroy_command_buffer` | ✅ | `vk_command.cpp:291` | `memdelete` 释放命令池与 command buffer。 |

### 4.8 队列 / 帧（3 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-42 | `queue_submit` | ❌ | `vk_command.cpp:357` | TODO(V6) 空实现；无 `vkQueueSubmit`、无 acquire/present semaphore、无 fence。 |
| I-43 | `queue_wait_idle` | ✅ | `vk_rendering_device.cpp:57` | `device.waitIdle()`，异常被捕获。 |
| I-44 | `submit_frame` | ✅ | `vk_rendering_device.cpp:70` | 返回递增计数；但没有任何 fence/帧资源使用它（D-24）。 |

---

## 5. 缺陷清单（仍开放 / 部分开放）

> 已关闭缺陷只保留 ID 与结论（§5.4），不再保留修复细节。P0 = 阻塞渲染，P1 = 功能/契约，
> P2 = 质量/可维护性。

### 5.1 P0：阻塞级

| ID | 位置 | 问题 | 影响 / 修复方向 |
|---|---|---|---|
| D-06（剩余） | `vulkan/vk_rendering_device.cpp:24-55`；`vk_internal.h` | 仅剩绑定组/管线布局部分：`create_bind_group_layout`、`create_bind_group`、`create_pipeline_layout` 及三个 destroy 仍是空桩；`vk_internal.h` 没有 descriptor set layout/pool/set、pipeline layout 结构。（缓冲区与纹理上传已完成。） | 引擎 shader 依赖 UBO + 6 张纹理（纹理槽契约见 `AGENTS.md` 5.3-5），没有绑定组就传不了参数。需先实现 descriptor layout/pool/set 与 pipeline layout（V5）。 |
| D-07 | `vk_command.cpp:89` | render pass 附件没有 format/samples/initial/final layout，create info 没有 subpass 与 dependency；无效 color view（默认帧缓冲）被跳过；`width/height=0` 不推导；render pass/framebuffer 每 pass 新建且不销毁。 | 无法录制合法 pass，更无法渲染到窗口。需要完整 render pass（含 subpass/dependency）、交换链分支、pass 缓存与销毁（V3）。 |
| D-08 | `vk_command.cpp:194-249` | `pass_set_bind_group`、`pass_set_vertex_buffer`、`pass_set_index_buffer`、`pass_draw`、`pass_draw_indexed` 全为空。 | 没有任何几何/参数被提交，画面一定为空（V6）。 |
| D-09 | `vk_command.cpp:357`；`vk_rendering_device.cpp:6` | `queue_submit` 与 `present` 为空。 | 不提交、不 acquire、不 present；窗口只会显示未定义内容（V3/V6）。 |
| D-10 | `vk_pipeline.cpp:55-215` | 管线描述大面积缺失：stage 的 `stage`/`pName` 未设（fragment 会被当作 vertex、入口名为 null）；顶点输入为空；`renderPass` 为 null（未启用 dynamic rendering）；颜色混合状态未挂上；忽略 `desc.pipeline_layout` 与 `desc.depth_stencil`；MSAA 硬编码 1。 | 管线创建会被 validation 拒绝，或创建后发现不了颜色/顶点状态。需按 `RenderPipelineDesc` 完整映射（V5）。 |
| D-11 | `vk_resource.cpp:60`；`servers/rendering/shaders/*` | Vulkan 要求 SPIR-V，但 `ShaderModuleDesc.source` 传的是 GLSL 文本；现有 GLSL 是 `#version 330 core`，不能直接给 Vulkan；入口名被忽略；`pCode` 取自 `std::string::data()`，无 4 字节对齐保证。 | Shader 是整条链路的前置。必须决定：离屏 `glslc` 生成 `.spv`，或运行时 `glslang`；并补 Vulkan 专用 shader 变体（V4）。 |
| D-12 | `rendering_server.cpp`（全文走旧 `RenderingDevice::get_singleton()`）；`application.cpp:17-30` | 引擎渲染链路没有调用 `RenderingDeviceRefactor`；`Application` 用旧设备、按旧默认驱动建窗口。 | 即使 Vulkan 适配层修好也“没有调用方”。先手工测试，最后接 `RenderingServer`（V7，独立大任务）。 |
| D-16 | `vk_swapchain.cpp:109-190` | swapchain 只有 `vk::Image` 数组：没有 image view、没有深度缓冲、没有 per-image framebuffer、没有 `oldSwapchain`、没有 acquire/present 同步对象。 | 无法作为默认帧缓冲使用；窗口 resize 无法恢复；`present()` 无对象可呈现（V3）。 |
| D-25 | `platform/glfw/glfw_window.cpp:282`；`application.cpp:116` | 平台已有 `FramebufferSize` 事件（`glfw_window.cpp:427`），但没有转发给设备；`resize_swapchain` 无调用方；Vulkan 下 `swap_buffers` 不负责 present。 | Vulkan 下交换链永远不会被 present；resize 后画面拉伸/崩溃（V3）。 |

### 5.2 P1：功能 / 契约级

| ID | 位置 | 问题 | 影响 / 修复方向 |
|---|---|---|---|
| D-15（剩余） | `vk_command.cpp:144`；`vk_internal.h:105` | `VkRenderPassHandleData`（render pass + framebuffer）没有销毁入口，每个 pass 新建即泄漏；`destroy_*` 仍是立即 `memdelete`，没有帧序号/延迟销毁队列（`submit_frame` 未被使用）。 | 长时间运行耗尽显存/句柄；提交后销毁在途资源是 UB。需要 fence + 延迟销毁表（V6/G-10）。 |
| D-19（剩余） | `vk_command.cpp:128-141` | 深度纹理的创建/usage/aspect 已正确（已关闭部分），但 render pass 的深度附件仍缺 format/samples/layout，交换链也没有深度缓冲；图像从未 transition 到附件布局。 | 深度测试、阴影贴图、默认帧缓冲深度都不可用（V3）。 |
| D-20 | `vk_pipeline.cpp:166-189`；`vk_command.cpp:222` | 管线状态忽略 desc：MSAA 硬编码 `e1`；depth test 恒开、depth write 恒关、compare 默认 `eNever`；颜色目标/blend/write mask 全部忽略；动态 scissor 从未设置。 | 深度恒不通过 → 即便 draw 也全被裁掉；混合/写掩码失效。`pass_set_viewport` 应同时设 scissor（G-04）。 |
| D-24（剩余） | `vk_rendering_device.cpp:70`；`vk_command.cpp:305` | 没有帧 fence / 多帧在飞；`submit_frame` 只自增；一次性上传用局部 fence 同步等待。 | 无法安全复用 command buffer/descriptor/上传内存，也无法延迟销毁（V6）。 |
| D-26（剩余） | `rendering_device_refactor.h:260-275` | 采样器 `compare_op` 与 lod 校验已完成；仍无 anisotropy 字段/设置（Vulkan 需 `samplerAnisotropy` 特性，GL 需 `EXT_texture_filter_anisotropic`）。 | 各向异性过滤不可用；接口扩展前不要只在后端加字段。 |
| D-27 | `vk_pipeline.cpp:97-116` | 不校验 `ShaderModuleDesc.type` 与 `VertexStage/FragmentStage` 是否匹配；`VkShaderModuleData::type` 存了但没用。 | 顶点/片段模块可能装反，validation 报 stage 不匹配（V4）。 |
| D-28（剩余） | `vk_pipeline.cpp:191-204` | 失败清理与 destroy 已修；仍自建 `pipeline_layout` 并忽略 `desc.pipeline_layout`（两者是不同的 `VkPipelineLayout`），且没有 pipeline cache。 | 绑定组无法与管线匹配；每次创建重复编译（V5）。 |

### 5.3 P2：代码质量 / 可维护性

| ID | 位置 | 问题 |
|---|---|---|
| D-30 | `vk_rendering_device.h:27-31` | 类注释过期：仍写“资源显存、绑定组、完整管线与命令提交仍在 V2+ 实施中”，实际 V2 已完成、只剩 V3+。`vk_rendering_device.cpp` 的旧注释已清理。 |
| D-33 | `vk_rendering_device_init.cpp:200-256` | `rateDeviceSuitability` 是死代码；物理设备只取“第一个合适的”，没有按评分选；`device_features` 为空且从不开特性链（后续 anisotropy 等也依赖它）。 |
| D-36 | `tests/CMakeLists.txt` | Vulkan 侧已有格式/布局/校验单测与初始化冒烟，但仍缺：validation error 即失败、资源/绘制 GPU 目标、泄漏计数、GPU 读回比对（无法验证写入内容）。 |
| D-37（剩余） | `vulkan/vk_internal.h` | 数据结构已补 `VkBufferHandleData`，仍缺 bind group layout、bind group、pipeline layout、descriptor pool；显存分配器独立在 `vk_memory`（不再需要放这里）。 |
| D-38 | `vk_command.cpp:347`（编译警告） | `device.waitForFences(...)` 标了 `nodiscard` 但返回值被忽略：超时/设备丢失时会被当成“上传成功”。应检查返回值并走失败路径。 |

### 5.4 已关闭缺陷（保留 ID 便于追溯）

D-01（CMake 漏 `vk_command.cpp`）、D-02（`vk_command.cpp` 编译错误）、D-03（纯 Vulkan 构建/旧工厂）、
D-04（`VkImageCreateInfo` 非法字段）、D-05（`VkImageViewCreateInfo` 未填）、D-13（句柄恒为 1）、
D-14（`destroy_shader_module` 错误 cast）、D-17（交换链格式硬编码/无 BGRA 表示）、
D-18（格式映射与 usage 位运算错误）、D-21（缺少描述校验）、D-22（异常未捕获）、
D-23（没有显存分配器/staging）、D-29（枚举 switch 无 `default`）、D-31（`NODEBUG` 拼写）、
D-32（debug messenger 无条件创建/未链 pNext）、D-34（swapchain 缺 `eTransferSrc`）、
D-35（`create_texture` 字段/校验缺失）。

---

## 6. 接口 / 契约缺口（待决策）

| ID | 缺口 | 现状 | 建议 |
|---|---|---|---|
| G-01 | Shader 源类型 | `ShaderModuleDesc.source` 是 `std::string`；Vulkan 需要 SPIR-V，却没有源类型字段。 | 增加明确的 `source_kind`（GLSL 文本 / SPIR-V）或独立 `spirv` 字段，不要靠后端猜。 |
| G-02 | Vulkan shader 变体与工具链 | 现有 shader 是 `#version 330 core`；缺少显式 binding/独立 sampler/`gl_VertexIndex` 等 Vulkan 约定；CMake 未接入 `glslc`。 | 优先离屏方案：CMake 自定义目标把 Vulkan 变体编译成 `.spv`（如 `resources/shaders-vk/`），加载器按后端选目录；运行时 `glslang` 作为备选。 |
| G-03 | 交换链格式与颜色空间 | 已改为使用实际格式（sRGB 优先，BGRA 已可表达）；但颜色空间约定未统一：Vulkan sRGB 交换链由硬件编码，shader 不应再手工 `linear_to_srgb()`，而现有 GL 路径相反。 | 接入绘制前必须定死一条：UNORM 交换链 + shader 手工编码（与 GL 一致），或 sRGB 交换链 + 关闭手工编码（需 shader 变体/分支）。 |
| G-04 | 裁剪矩形 | 接口只有 `pass_set_viewport`，没有 scissor 命令；管线把 scissor 设为 dynamic。 | 约定 `pass_set_viewport` 同时设置 scissor，或在 `encoder_begin_render_pass` 按 pass 尺寸设默认 scissor。 |
| G-07 | 默认帧缓冲深度 | 接口注释说“深度附件由后端提供的交换链深度缓冲承担”，但没有暴露“默认深度视图”的途径。 | `init_swapchain` 创建每图像深度 image + view；`encoder_begin_render_pass` 在 depth view 无效时自动使用它。 |
| G-08 | 图像 acquire / present 归属 | `present()` 无返回值，也没有“当前图像索引”入口。 | 后端内部在 `queue_submit` 前或 begin pass 时 acquire；`present()` 负责 present 并推进帧；在头文件注释写清帧边界由后端维护。 |
| G-09 | render pass 对象 vs dynamic rendering | `encoder_begin_render_pass` 创建 `VkRenderPass` 对象，但 `create_render_pipeline` 既没传 renderPass、也没启用 dynamic rendering，两者矛盾。 | 二选一并写清：经典 render pass 对象（与现有代码一致，推荐先落地），或 `VK_KHR_dynamic_rendering` + `VkPipelineRenderingCreateInfo`。 |
| G-10 | 多帧在飞与延迟销毁 | 接口约定“destroy 任意时刻可调、Vulkan 延迟到 fence 之后”，但帧同步对象不存在；`submit_frame` 未使用。 | 设计 `frame_index + max_frames_in_flight(2~3) + per-frame fence/command buffer/descriptor pool + deferred destroy list`。 |

已关闭的缺口：**G-05**（`SamplerDesc.compare_op` 已落地并映射两端）、**G-06**（显存策略定为
“自建、每资源独占 allocation”，实现于 `vulkan/vk_memory.*`，纹理与缓冲区均已接入）、
**G-11**（`alloc_handle_id()` 统一分配唯一句柄）、**G-12**（校验抽到 `rhi_validation.*`，Vulkan 统一调用）。

---

## 7. 实施阶段

> 依赖关系：V0 → V1 → V2 → V3 → V4 → V5 → V6 → V7。V3 依赖 V2 的纹理/视图；V5 依赖 V2 的
> buffer/texture 与 V3 的 render pass 方案；不要在 V3 之前碰 draw。

### V0 构建与骨架修复（已完成）

- [x] **V0-1** `vk_command.cpp`/`vk_format.cpp` 加入 `servers/rendering/CMakeLists.txt`。
- [x] **V0-2** 修 `vk_command.cpp` 编译错误（`vk::ClearValue` 花括号、command buffer move、录制前 `begin()`）。
- [x] **V0-3** 纯 Vulkan 构建：旧 `RenderingDevice` 工厂按后端条件编译，驱动查询不再返回错误后端。
- [x] **V0-4** 统一唯一句柄分配器；修正 `destroy_shader_module` cast。
- [x] **V0-5** 所有 create/init 入口捕获异常；`NDEBUG`；debug messenger 跟随 validation 开关并链 pNext。
- [x] **V0-6** 构建矩阵 + 只初始化冒烟（`ORCA_VULKAN_INIT_TEST_EXIT_AFTER_INIT`）。

**验收（已通过）**：三配置全绿；`orca-vulkan-init-test` 输出物理设备与交换链尺寸；validation 无 error。

### V1 格式映射与描述校验（已完成）

- [x] **V1-1** `Format` 补 BGRA8Unorm/BGRA8UnormSrgb；新增 `vulkan/vk_format.*` 双向映射。
- [x] **V1-2** 修 usage 位运算；深度格式映射 `eDepthStencilAttachment`；swapchain 支持 `eTransferSrc`。
- [x] **V1-3** 新增后端无关 `rhi_validation.*`：格式谓词、纹理/视图/采样器/管线描述校验。
- [x] **V1-4** 纯逻辑单测 `test_rhi_validation.cpp`（始终编译）与 `test_vk_format.cpp`（Vulkan 开启时编译）。

**验收（已通过）**：无需 Vulkan 设备即可跑单测；非法 desc 在 create 入口被拒绝，不再进入 driver。

### V2 显存、缓冲、纹理、视图、采样器（已完成）

- [x] **V2-1** 自建显存分配器 `vulkan/vk_memory.*`（独占 allocation，处理 dedicated 需求）。
- [x] **V2-2** `create_texture` 合法字段 + 显存分配/绑定；`create_buffer` + 设备级 staging 池。
- [x] **V2-3** `queue_write_buffer`（`vkCmdCopyBuffer`）与 `queue_write_texture`（staging + layout transition）。
- [x] **V2-4** `generate_mip_map`（`vkCmdBlitImage` 逐级下采样，按格式特性选择过滤/blit）。
- [x] **V2-5** `create_texture_view` 完整映射（共享范围解析、cubemap 6 层对齐）。
- [x] **V2-6** sampler 补 `compare_op`（G-05）与 lod 校验；anisotropy 仍未进接口（D-26）。
- [x] **V2-7** 新增 `vk_image_layout.*`（aspect/resting layout/barrier/mip 尺寸）与布局映射单测。

**验收（已通过）**：颜色/深度/cubemap 纹理与视图、上传、mipmap、缓冲区创建/写入、sampler 全部
冒烟通过；validation 无 VUID；销毁走 RAII。

### V3 交换链 / 默认帧缓冲 / 呈现（约 1.5–2 天）

- [ ] **V3-1** `init_swapchain` 补 image view、深度 image/view（G-07）、每 swapchain image 的 framebuffer/render pass 缓存。（D-16）
- [ ] **V3-2** acquire/present：`vkAcquireNextImageKHR`、image-available/render-finished semaphore、每帧 fence。（D-16/D-24）
- [ ] **V3-3** `encoder_begin_render_pass` 支持默认帧缓冲：无效 color view → 当前交换链 image；无效 depth view → 内部深度；`width/height=0` → 由附件推导；完整 attachment（format/samples/initial/final layout）+ subpass/dependency。（D-07/D-19/G-09）
- [ ] **V3-4** `present()` → `vkQueuePresentKHR`；`resize_swapchain` 用 `oldSwapchain` 重建并重建依赖资源；`get_swapchain_format/size` 保持真实值。（D-17/D-25）
- [ ] **V3-5** 平台接线：GLFW/SDL3 的 `FramebufferSize` 转发给设备；Vulkan 下 `swap_buffers` 不再依赖平台 swap。（D-25）

**验收**：录一个 `BeginPass(clear) + EndPass` 并提交，窗口显示稳定单色；连续两帧、resize 后仍正确；
validation 全绿。

### V4 Shader 工具链（约 1–2 天，含决策）

- [ ] **V4-1** 实现 G-01/G-02：`ShaderModuleDesc` 增加源类型；`builtin` 的 Vulkan GLSL 变体（450 + 显式 binding + 独立 sampler）编译为 `.spv` 并随资源拷贝。
- [ ] **V4-2** `create_shader_module` 按源类型处理 SPIR-V：长度/对齐校验、`entry_point` 使用、`ShaderType` 与 stage 一致性校验。（D-11/D-27）
- [ ] **V4-3** 同步更新 shader 加载路径（或先只给手工测试用）。

**验收**：从 SPIR-V 创建 shader module，validation 无错；为一个简单 shader 生成管线通过。

### V5 管线与绑定（约 2–3 天）

- [ ] **V5-1** `create_bind_group_layout` → `VkDescriptorSetLayout`；`create_pipeline_layout` → `VkPipelineLayout`（按 group 顺序）。（D-06）
- [ ] **V5-2** `create_bind_group` → descriptor pool（按帧/按组策略）+ `vkUpdateDescriptorSets`；校验 layout 匹配、buffer offset/size、view 类型。（D-06）
- [ ] **V5-3** 补全 `create_render_pipeline`：stage/`pName`；vertex input binding/attribute（`VertexFormat` → `vk::Format`）；renderPass 对象（G-09）；color blend state（targets/blend/write mask）；depth state（format/write/compare）；multisample；使用传入的 `pipeline_layout`。（D-10/D-20/D-28）
- [ ] **V5-4** destroy pipeline/layout/set layout；创建失败路径清理。（D-15/D-28）

**验收**：创建 `pipeline layout + bind group layout + bind group + pipeline` 成功；validation 无错；
descriptor 内容与 shader 声明匹配。

### V6 命令录制 / 提交 / 同步（约 1.5–2 天）

- [ ] **V6-1** command encoder 生命周期：按帧复用 pool、`begin()`/`end()`、`encoder_finish` move、`destroy_command_buffer` 真正释放。（D-07/D-15）
- [ ] **V6-2** pass 绑定：`vkCmdBindDescriptorSets`（含 dynamic offsets）、`vkCmdBindVertexBuffers`、`vkCmdBindIndexBuffer`、`vkCmdSetViewport/SetScissor`、`vkCmdDraw/DrawIndexed`。（D-08/G-04）
- [ ] **V6-3** `queue_submit` → `vkQueueSubmit`（semaphore/fence 链）；`queue_wait_idle` → `vkDeviceWaitIdle`；`present` 与帧 fence 配合；延迟销毁（G-10）。（D-09/D-24）
- [ ] **V6-4** pass 结束/帧结束释放或缓存 render pass/framebuffer，不再每 pass 泄漏。（D-15）

**验收**：默认帧缓冲里画出彩色三角形；连续 60 帧无 validation 错误、无泄漏上涨。

### V7 验收场景与接入

- [ ] **V7-1** 手工 GPU 测试目标（隐藏窗口）：clear、三角形、带纹理四边形、深度测试、cubemap 天空盒。（D-36）
- [ ] **V7-2** 与旧 GL 路径同一场景对照（截图或至少视觉对照）。
- [ ] **V7-3**（可选，独立大任务）把 `RenderingServer` 切到 `RenderingDeviceRefactor`。（D-12）

---

## 8. 测试与验收建议

1. **构建矩阵**（现状见 §2）
   - `GL-only`（默认）：保持 `ctest` 全绿。
   - `Vulkan+GL`：`orca-vulkan-init-test` 必须能链接、能跑。
   - `Vulkan-only`：必须能配置+构建。
2. **纯逻辑单测**（已在 `orca-tests`）
   - `test_rhi_validation.cpp`：纹理/视图（含范围解析）/采样器（含 lod/compare）/缓冲区（含写入区间）/管线描述校验。
   - `test_vk_format.cpp`（Vulkan 构建）：Format↔vk::Format、ImageUsage/BufferUsage、CompareOp 映射。
   - `test_vk_image_layout.cpp`（Vulkan 构建）：aspect/resting layout/barrier/mip 尺寸映射。
3. **设备测试**（需要 Vulkan runtime）
   - `orca-vulkan-init-test`（已有）：instance/device/swapchain + 纹理/视图/采样器/缓冲区冒烟。
   - 待新增 `orca-vulkan-resources-test`：创建 bind group/pipeline 并销毁，validation 全绿。
   - 待新增 `orca-vulkan-draw-test`：清屏 → 三角形 → 纹理四边形 → 深度 → present。
4. **Validation layer**
   - Debug 默认开启 `VK_LAYER_KHRONOS_validation`；建议同时打开 sync validation 捕捉 barrier 缺失；
   - 测试退出码应以 validation error 为红（当前 callback 只打印，D-36）。
5. **泄漏检查**
   - 在 `VkRenderingDevice` 的 create/destroy 维护 `live_object_count`（Debug 断言归零）；
   - 或用 Debug Utils object naming / `VK_LAYER_LUNARG_api_dump` 辅助定位。
6. **回归**
   - `ctest --test-dir build --output-on-failure` 保持通过；新增 Vulkan 测试建议先作为手工目标，不阻塞 CTest。

---

## 9. 附录

### 9.1 `servers/rendering/vulkan/` 文件职责（当前）

| 文件 | 内容 | 状态 |
|---|---|---|
| `vk_rendering_device.h` | `VkRenderingDevice` 类声明、`QueueFamilyIndices`、`SwapChainSupportDetails`、staging 池与一次性提交助手 | ✅ 声明完整；成员仍缺交换链 image view/深度/fence/semaphore/descriptor pool |
| `vk_rendering_device.cpp` | present/shutdown、绑定组与管线布局桩、队列查询、`submit_frame` | ⚠️ present 与绑定组/layout 仍为桩 |
| `vk_rendering_device_init.cpp` | instance/debug/surface/物理设备/逻辑设备 | ✅ 初始化可用 |
| `vk_swapchain.cpp` | 交换链创建、格式/尺寸查询、surface format/present mode 选择、resize 桩 | ⚠️ 缺视图/深度/同步；resize 未实现 |
| `vk_resource.cpp` | shader、texture（显存/上传/mipmap）、image view、sampler 与对应 destroy | ✅ 除 shader 源类型外均可用 |
| `vk_buffer.cpp` | buffer 创建/写入/销毁 + 设备级 staging 池 | ✅ 可用 |
| `vk_pipeline.cpp` | graphics pipeline 创建/销毁 | ⚠️ 骨架缺关键状态 |
| `vk_command.cpp` | encoder/render pass/命令绑定/一次性提交/队列桩 | ⚠️ 录制骨架可跑，绑定与 draw 仍空 |
| `vk_internal.h` | 资源对象数据结构 | ⚠️ 缺 bind group/layout、pipeline layout、descriptor pool |
| `vk_format.h/.cpp` | Format↔vk::Format、image/buffer usage、CompareOp 映射 | ✅ 纯逻辑，已单测 |
| `vk_memory.h/.cpp` | 显存类型选择 + 独占 `vkAllocateMemory` | ✅ 可用 |
| `vk_image_layout.h/.cpp` | aspect/resting layout/transition/mip 尺寸/layout barrier | ✅ 纯逻辑，已单测 |

### 9.2 当前实现的边界与陷阱（后续阶段必须遵守）

1. **创建 ≠ 有内容。** `create_texture`/`create_buffer` 只完成创建与显存绑定，内容未定义；
   纹理必须随后 `queue_write_texture`（需要 mip 时再 `generate_mip_map`）才能采样到有效数据。
2. **写入是同步一次性提交，不是帧批次。** `queue_write_texture`/`generate_mip_map`/`queue_write_buffer`
   每次都新建临时命令池 + fence 并等待完成；返回时 GPU 已执行完，且天然先于之后提交的帧。
   代价是每次写入一次 submit + fence 等待（创建期可接受）。**不要**在每帧更新 UBO 的路径上直接
   调用 `queue_write_buffer`；V6 落地前，动态 UBO 需要在 `vk_memory` 里选 host-visible 内存或加专门通道。
3. **用法位是 Vulkan 的硬约束。** 上传要求创建时带 `COPY_DST`；mipmap 要求 `COPY_SRC | COPY_DST`
   （GL 后端无此限制，但新 RHI 的 Vulkan 约定如此）。
4. **显存策略：每资源独占 allocation。** 由 `vk_memory::allocate_device_memory` 选择内存类型，
   `image`/`buffer` 与 `memory` 的成员声明顺序保证资源先于内存销毁（逆序析构）。
   当前无 VMA、无子分配；要改只需改 `vk_memory`。
5. **布局按“整张图统一”记录。** `VkTextureHandleData::current_layout` 是整图的显式状态：
   创建后 `eUndefined`，上传/mipmap 后回到 `resting_layout`。V3 的 render pass 必须从这里取
   `oldLayout`，不能假设初始布局，也不能对单个 mip/层单独记账。
6. **`generate_mip_map` 依赖格式的 blit 能力。** 不支持 `eBlitSrc/eBlitDst` 时只告警并跳过，
   不录制非法命令；过滤模式按 `eSampledImageFilterLinear` 选择。
7. **视图不重解释格式。** `format` 固定取父纹理格式；aspect 由父格式推导（深度只能 DepthOnly），
   不按 desc 直接传；cubemap 的 `base_layer`/`layer_count` 必须是 6 的倍数（本项目固定 `0/6`）。
8. **`compare_enable` 不校验父纹理是深度纹理。** 该约束只有到 descriptor 绑定（V5）才能判定，
   采样器本身可以提前创建。
9. **`rhi_validation.*` 是唯一校验入口。** 新增资源类型或格式约束时先改共享层，再让 Vulkan 的
   `create_*` 走同一路径；不要在 Vulkan 里另立分支。
10. **交换链格式与颜色空间是端到端契约（G-03 未定）。** 本机实际格式是 `BGRA8UnormSrgb`；
    Vulkan 会由硬件做 sRGB 编码，接入绘制时 shader 不能再手工 `linear_to_srgb()`（与旧 GL 路径相反）。
11. **管线与 render pass 尚未对齐（G-09）。** `create_render_pipeline` 现在没有 renderPass（也未启用
    dynamic rendering），但录制走 `VkRenderPass` 对象；V5 必须二选一并写清。
12. **句柄与销毁。** 所有 handle 由 `alloc_handle_id()` 分配且唯一；`destroy_*` 目前立即 `memdelete`，
    在 V6 引入帧 fence 前是安全的，之后必须改为延迟销毁（D-15）。
13. **render pass/framebuffer 每 pass 新建且无销毁入口。** 当前只出现在未完成的录制路径上，
    V3/V6 必须补缓存或释放（D-15/D-07）。
14. **`waitForFences` 返回值被忽略（D-38）。** 一次性提交的失败（超时/设备丢失）会被当成成功。
15. **`init_swapchain` 前必须先有 surface。** 离屏（`create_vulkan_surface` 为空）会直接返回 false；
    `findQueueFamilies` 依赖 surface 做 present 支持查询。
