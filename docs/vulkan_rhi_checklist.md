# Vulkan 适配层审计清单（RenderingDeviceRefactor）

> 审计对象：`servers/rendering/rendering_device_refactor.h`（WebGPU 风格接口）与
> `servers/rendering/vulkan/*`（Vulkan 适配层）。
> 参考实现：`servers/rendering/opengl/*`（同一接口的 OpenGL 适配器，含描述校验与资源对象）。
> 审计日期：2026-11-16。
> 审计方式：全量通读 + 三组真实构建/编译验证（见 §2）。
> 审计基线：**骨架阶段；既不能完整构建，也不能绘制任何内容。**
> 实施进度：V0（构建/句柄/异常）与 V1（格式映射/描述校验）已完成，见 §10；§4/§5 保留审计基线。

---

## 1. 结论摘要

1. **分层位置正确，但没有接通。** `VkRenderingDevice` 完整实现了 `RenderingDeviceRefactor` 的 44 个纯虚函数签名，
   `init/init_device/init_swapchain` 有一套能走通的 Vulkan 初始化代码；但引擎实际渲染链路
   （`Application`、`RenderingServer`、`Viewport`）仍走旧 `RenderingDevice`，新 RHI 目前只被
   `tests/servers/test_vulkan_init.cpp` 调用。因此 Vulkan 后端**不在任何一帧的渲染路径中**。
2. **构建当前是坏的（已实测）。**
   - 纯 Vulkan 配置编译失败：`rendering_device.cpp:36` 无条件 `make_shared<GLRenderingDevice>()`。
   - Vulkan + OpenGL 双后端配置链接失败：`vk_command.cpp` 没被加入
     `servers/rendering/CMakeLists.txt`，13 个命令录制虚函数未定义。
   - `vk_command.cpp` 单独做语法检查有 4 个编译错误（vexing parse、`vk::raii::CommandBuffer` 拷贝），
     说明**该文件从未进入过编译**。
3. **运行期能力仅到“设备/交换链初始化骨架”。** 缓冲区、绑定组、管线布局、纹理上传、mipmap、
   draw、submit、present 全部是空桩或非法描述；纹理/视图对象用默认字段创建，validation layer
   必然报错；渲染管线缺少 shader stage、入口名、renderPass、顶点输入、颜色混合状态，并忽略传入的
   pipeline layout；命令缓冲没有 `begin()`，render pass 没有 subpass，默认帧缓冲分支不存在。
4. **接口实现数量统计**（判据见 §3；下行为审计基线，V0/V1 后的最新统计见 §10）：

   | 状态 | 数量 | 占比 |
   |---|---|---|
   | ✅ 可用 | 7 | 16% |
   | ⚠️ 有代码但当前不可用 / 违反契约 | 15 | 34% |
   | ❌ 未实现 | 22 | 50% |

   > V0/V1 完成后：✅ 17 / ⚠️ 7 / ❌ 20（§10.2 列出变更项）。

5. **推进顺序建议**：先修 P0-1～P0-9（构建、句柄、内存、纹理描述、默认帧缓冲、命令/提交链路），
   再做 P0-10～P0-12（管线/绑定/Shader 工具链），最后才谈“画一个三角形”和接入 `RenderingServer`。
   直接补 `pass_draw` 是无效的——前面每一层都会先失败。

### 1.1 下一步 12 项（按依赖排序）

- [x] **V0-1** 把 `vk_command.cpp` 加入 CMake 并修复其 4 个编译错误（D-01/D-02）。
- [x] **V0-2** 修复纯 Vulkan 构建：旧 `RenderingDevice` 工厂加 `ORCA_RENDERER_OPENGL` 保护，
      修正 `is_driver_supported/get_default_driver`（D-03）。
- [x] **V0-3** 引入统一 unique handle 分配器（所有对象不再恒为 1），修正 `destroy_shader_module`
      的错误 `static_cast`（D-13/D-14）。
- [x] **V1-1** 补 `Format ↔ vk::Format` 双向映射，修 `get_vk_image_usage` 的运算符优先级 bug（D-18）。
- [ ] **V2-1** 选型并接入显存分配器（VMA 或自建，G-06），实现 `create_buffer + 暂存上传`（D-06）。
- [ ] **V2-2** 修 `create_texture` 的合法字段（arrayLayers、extent.depth、cubemap、深度附件用法）并绑定显存（D-04/D-19）。
- [ ] **V2-3** 补全 `create_texture_view` 的 viewType/format/subresourceRange（D-05）。
- [ ] **V2-4** 实现 `queue_write_texture`（staging + layout transition）与 `generate_mip_map`（D-06）。
- [ ] **V3-1** 交换链图像视图、深度缓冲、每图像 render pass/framebuffer、acquire/present 同步、resize 重建（D-16/D-25）。
- [ ] **V4-1** 确定 Shader 工具链（离屏 `glslc` 或运行时 `glslang`），补 Vulkan 专用 GLSL 变体与 SPIR-V 入口（D-11/G-01/G-02）。
- [ ] **V5-1** 实现 descriptor set layout/pool/set 与 `pipeline layout`，再补全 `create_render_pipeline`（D-06/D-10）。
- [ ] **V6-1** 实现 command buffer 生命周期、bind/draw、`queue_submit`、`present`、帧 fence 与延迟销毁（D-07/D-08/D-09/D-15）。

---

## 2. 验证方法与可复现证据

以下记录的是 **V0 修复前** 的审计基线（保留下来便于对照）。V0 完成后这三项都已通过，
当前状态与复现结果见 §10.5。

### 2.1 配置纯 Vulkan 构建（OpenGL 关闭）

```powershell
cmake -S . -B <tmp> -G "MinGW Makefiles" `
  -DCMAKE_BUILD_TYPE=Debug `
  -DORCA_RENDERER_OPENGL=OFF -DORCA_RENDERER_VULKAN=ON -DORCA_RENDERER_DEFAULT=vulkan
cmake --build <tmp> --target orca-vulkan-init-test
```

结果：**编译失败**。

```text
servers/rendering/rendering_device.cpp:36:34:
  error: 'GLRenderingDevice' was not declared in this scope
  singleton = std::make_shared<GLRenderingDevice>();
```

原因：旧 `RenderingDevice` 工厂没有按 `ORCA_RENDERER_OPENGL` 条件编译（`rendering_device.cpp:33-38`），
且 `create_singleton(driver)` 完全忽略 `driver` 参数。

### 2.2 配置 Vulkan + OpenGL 双后端

把 OpenGL 打开后，库可以编译，但 `orca-vulkan-init-test` **链接失败**，缺少 13 个命令录制/队列符号：

```text
undefined reference to `VkRenderingDevice::create_command_encoder()'
undefined reference to `VkRenderingDevice::encoder_begin_render_pass(...)'
undefined reference to `VkRenderingDevice::pass_set_pipeline(...)'
undefined reference to `VkRenderingDevice::pass_set_bind_group(...)'
undefined reference to `VkRenderingDevice::pass_set_vertex_buffer(...)'
undefined reference to `VkRenderingDevice::pass_set_index_buffer(...)'
undefined reference to `VkRenderingDevice::pass_set_viewport(...)'
undefined reference to `VkRenderingDevice::pass_draw_indexed(...)'
undefined reference to `VkRenderingDevice::pass_draw(...)'
undefined reference to `VkRenderingDevice::pass_end(...)'
undefined reference to `VkRenderingDevice::encoder_finish(...)'
undefined reference to `VkRenderingDevice::destroy_command_buffer(...)'
undefined reference to `VkRenderingDevice::queue_submit(...)'
```

原因：`servers/rendering/CMakeLists.txt:46-51` 的 Vulkan 源文件列表只包含
`vk_rendering_device.cpp / vk_rendering_device_init.cpp / vk_resource.cpp / vk_pipeline.cpp`，
漏掉了 `vulkan/vk_command.cpp`。

### 2.3 单独编译 `vk_command.cpp`

```powershell
g++ -std=c++17 -fsyntax-only -I servers/rendering -I core `
  -isystem D:/tools/vk/sdk/Include servers/rendering/vulkan/vk_command.cpp
```

结果：**4 个编译错误**。

```text
vk_command.cpp:80  vk::ClearValue clear_color(vk::ClearColorValue(color));
                   -> 被解析成函数声明（most vexing parse），emplace_back 失败
vk_command.cpp:231 cmd_buffer_data->command_buffers = cmd_encoder_data->command_buffers;
                   -> vk::raii::CommandBuffer 的拷贝构造/赋值是 deleted（2 处）
```

这与 2.2 的链接错误互相印证：该文件既没被编译，也确实编不过。

### 2.4 测试覆盖

- 仅有一个手工目标 `orca-vulkan-init-test`（`tests/CMakeLists.txt:39-40`），只验证
  `create_singleton(Vulkan) → Window → init → init_device → init_swapchain` 不崩。
- 没有任何 Vulkan 资源/管线/命令/格式映射的测试；也没有把 validation layer 的错误当作失败。
- GL 一侧至少还有 `test_gl_command_recording.cpp`、`test_gl_resources.cpp`、
  `orca-gl-resources-test`（真实上下文），Vulkan 侧完全空白。

---

## 3. 状态判据

| 标记 | 含义 |
|---|---|
| ✅ 可用 | 该接口的语义基本被正确实现，当前没有已知阻塞缺陷（可能存在小问题，已在备注中列出）。 |
| ⚠️ 有代码但当前不可用 | 已写实现，但存在 P0/P1 缺陷：调用会失败、泄漏、违反接口契约，或只能完成一半流程。 |
| ❌ 未实现 | 空桩、无条件返回空句柄，或只更新了窗口尺寸字段，没有后端行为。 |

---

## 4. 接口逐项清单（44 项）

### 4.1 初始化 / 交换链（8 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-01 | `init` | ⚠️ | `vk_rendering_device_init.cpp:73-117,275-283` | instance + debug messenger + surface 主流程存在；`create_instance` 抛异常（不返回 bool），`create_debug_messenger` 无条件执行；surface 失败返回 false 但无清理说明。 |
| I-02 | `init_device` | ⚠️ | `vk_rendering_device_init.cpp:212-294` | 物理设备选择 + 逻辑设备 + 双队列可用；评分函数 `rateDeviceSuitability` 是死代码，无特性/扩展需求链，失败抛异常。 |
| I-03 | `init_swapchain` | ⚠️ | `vk_rendering_device_init.cpp:359-407` | 创建了 swapchain 和 `vk::Image` 数组；**没有 image view、没有深度缓冲、没有 per-image framebuffer、没有 acquire/present 同步**。 |
| I-04 | `resize_swapchain` | ❌ | `vk_rendering_device.cpp:7-11` | 只改 `window_info.width/height`，不重建 swapchain，不更新 extent。 |
| I-05 | `get_swapchain_format` | ⚠️ | `vk_rendering_device.cpp:13-16` | 硬编码 `Format::RGBA8UnormSrgb`，不使用 `swapchain_image_format`；与真实 surface 格式可能不一致。 |
| I-06 | `get_swapchain_size` | ⚠️ | `vk_rendering_device.cpp:18-22` | 返回 `window_info` 尺寸，而非 `swapchain_image_extent`；`choose_swap_extent` 被 capabilities 钳制时两者可能不一致。 |
| I-07 | `present` | ❌ | `vk_rendering_device.cpp:24-26` | 空实现；没有 `vkQueuePresentKHR`，窗口不会显示任何渲染结果。 |
| I-08 | `shutdown` | ⚠️ | `vk_rendering_device.cpp:28-30` | 空实现，依赖 RAII 成员析构；没有 `waitIdle`，没有显式销毁顺序保证。 |

### 4.2 Shader（2 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-09 | `create_shader_module` | ⚠️ | `vk_resource.cpp:8-29` | 把 `desc.source` 直接当作 SPIR-V 二进制：`codeSize = source.size()`、`reinterpret_cast<const uint32_t*>`。但接口传的是 GLSL 文本；也没有长度 4 对齐检查。 |
| I-10 | `destroy_shader_module` | ⚠️ | `vk_resource.cpp:31-42` | `static_cast<VkShaderModule *>(handle.data)` 类型错误（实际对象是 `VkShaderModuleData`），`memdelete` 会导致堆损坏并泄漏内部 RAII module。 |

### 4.3 缓冲区（3 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-11 | `create_buffer` | ❌ | `vk_rendering_device.cpp:32-36` | 返回空句柄；无 `VkBuffer`、无显存、无 usage 映射。 |
| I-12 | `queue_write_buffer` | ❌ | `vk_rendering_device.cpp:38-44` | 空实现；没有暂存缓冲/拷贝命令，也没有“先写后提交可见”的时序实现。 |
| I-13 | `destroy_buffer` | ❌ | `vk_rendering_device.cpp:46-49` | 空实现。 |

### 4.4 纹理 / 视图 / 采样器（8 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-14 | `create_texture` | ⚠️ | `vk_resource.cpp:99-132` | 创建了 `VkImage`，但描述非法：`arrayLayers` 未设（默认 0）、`extent.depth` 未设（默认 0）、cubemap 映射成 `e3D`、未分配/绑定显存、无 layout/transition。 |
| I-15 | `create_texture_view` | ⚠️ | `vk_resource.cpp:142-159` | 创建了 `VkImageView`，但只设置了 image：`viewType` 默认 `e1D`、`format` 默认 `Undefined`、`subresourceRange` 的 aspect/levelCount/layerCount 全为 0；desc 字段完全没使用。必被 validation 拒绝。 |
| I-16 | `create_sampler` | ✅ | `vk_resource.cpp:192-217` | filter/address/mipmap/lod/compareEnable 的映射基本正确。小缺口见 D-26。 |
| I-17 | `queue_write_texture` | ❌ | `vk_rendering_device.cpp:51-55` | 空实现；无 staging buffer、无 layout transition、无 `vkCmdCopyBufferToImage`。 |
| I-18 | `generate_mip_map` | ❌ | `vk_resource.cpp:134-136` | 空实现（GL 侧有 `glGenerateMipmap`）。 |
| I-19 | `destroy_texture` | ✅ | `vk_resource.cpp:219-229` | `memdelete` 触发 RAII image 析构。当前无内存/视图引用，先可用；延迟销毁见 D-15。 |
| I-20 | `destroy_texture_view` | ✅ | `vk_resource.cpp:231-241` | 同上。 |
| I-21 | `destroy_sampler` | ✅ | `vk_resource.cpp:243-253` | 同上。 |

### 4.5 绑定组 / 管线布局（6 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-22 | `create_bind_group_layout` | ❌ | `vk_rendering_device.cpp:57-61` | 返回空句柄；`vk_internal.h` 里也没有对应的 `Vk...Data` 结构。 |
| I-23 | `create_bind_group` | ❌ | `vk_rendering_device.cpp:63-67` | 返回空句柄。 |
| I-24 | `create_pipeline_layout` | ❌ | `vk_rendering_device.cpp:69-73` | 返回空句柄。 |
| I-25 | `destroy_bind_group` | ❌ | `vk_rendering_device.cpp:75-78` | 空实现。 |
| I-26 | `destroy_bind_group_layout` | ❌ | `vk_rendering_device.cpp:80-83` | 空实现。 |
| I-27 | `destroy_pipeline_layout` | ❌ | `vk_rendering_device.cpp:85-88` | 空实现。 |

### 4.6 渲染管线（2 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-28 | `create_render_pipeline` | ⚠️ | `vk_pipeline.cpp:42-171` | 有 130 行骨架，但关键字段缺失/写错：shader stage 没设 `stage` 和 `pName`；顶点输入为空；`renderPass` 为 null 且未启用 dynamic rendering；颜色混合状态未挂上；忽略 `desc.pipeline_layout`；深度状态硬编码。 |
| I-29 | `destroy_render_pipeline` | ❌ | `vk_pipeline.cpp:173-176` | 空实现；`VkGraphicsPipelineHandleData` 由 `memnew` 分配，永不析构 → `VkPipeline`/`VkPipelineLayout` 泄漏。 |

### 4.7 命令录制（12 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-30 | `create_command_encoder` | ⚠️ | `vk_command.cpp:7-28` | 创建 queue-family 的 `VkCommandPool` + 1 个 primary command buffer；**未 `begin()`**；`handle` 恒为 1；数据由 `memnew` 分配且无释放路径。 |
| I-31 | `encoder_begin_render_pass` | ⚠️ | `vk_command.cpp:54-136` | 创建 render pass + framebuffer 并 `beginRenderPass`，但：attachment 没设 format/samples/initialLayout/finalLayout；没设 subpass；不支持默认帧缓冲（无效 view 被 `continue` 跳过）；`desc.width/height == 0` 时未由附件推导；render pass/framebuffer 每帧泄漏。 |
| I-32 | `pass_set_pipeline` | ✅ | `vk_command.cpp:141-154` | 正确调用 `vkCmdBindPipeline`。前提是管线本身可用（当前不满足）。 |
| I-33 | `pass_set_bind_group` | ❌ | `vk_command.cpp:156-164` | 空实现；无 `vkCmdBindDescriptorSets`/动态偏移。 |
| I-34 | `pass_set_vertex_buffer` | ❌ | `vk_command.cpp:166-172` | 空实现。 |
| I-35 | `pass_set_index_buffer` | ❌ | `vk_command.cpp:174-180` | 空实现。 |
| I-36 | `pass_set_viewport` | ⚠️ | `vk_command.cpp:182-192` | 设了 `VkViewport`；但管线启用了动态 scissor，却没有任何 `vkCmdSetScissor` → 绘制时状态未定义。 |
| I-37 | `pass_draw_indexed` | ❌ | `vk_command.cpp:194-202` | 空实现。 |
| I-38 | `pass_draw` | ❌ | `vk_command.cpp:204-211` | 空实现。 |
| I-39 | `pass_end` | ✅ | `vk_command.cpp:213-221` | 调用 `vkCmdEndRenderPass`。资源释放不在此接口职责内，但泄漏问题见 D-15。 |
| I-40 | `encoder_finish` | ⚠️ | `vk_command.cpp:223-235` | 调用 `end()`（但从未 `begin()`），把 `std::vector<vk::raii::CommandBuffer>` 按值拷贝（编译错误），未释放 encoder/pool。 |
| I-41 | `destroy_command_buffer` | ❌ | `vk_command.cpp:237-242` | 空实现；`VkCommandBufferHandleData` 永不释放。 |

### 4.8 队列 / 帧（3 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-42 | `queue_submit` | ❌ | `vk_command.cpp:244-247` | 空实现；无 `vkQueueSubmit`、无 acquire/present semaphore、无 fence。 |
| I-43 | `queue_wait_idle` | ❌ | `vk_rendering_device.cpp:91-93` | 空实现；应为 `vkDeviceWaitIdle`。 |
| I-44 | `submit_frame` | ✅ | `vk_rendering_device.cpp:95-98` | 返回递增计数；但没有任何 fence/帧资源使用它（见 D-24）。 |

---

## 5. 缺陷清单

### 5.1 P0：阻塞级（不修就无法构建/无法出画面）

| ID | 位置 | 问题 | 影响 / 修复方向 |
|---|---|---|---|
| D-01 | `servers/rendering/CMakeLists.txt:46-51` | Vulkan 源文件漏了 `vulkan/vk_command.cpp`。 | 链接期 13 个虚函数未定义，`orca-vulkan-init-test` 无法生成。加入该文件是第一步。 |
| D-02 | `vk_command.cpp:80,231` | `vk::ClearValue clear_color(vk::ClearColorValue(color));` 被解析成函数声明；`std::vector<vk::raii::CommandBuffer>` 按值赋值使用 deleted 拷贝构造。 | 该文件从未编译过。改成 `{...}` 初始化；command buffer 用 `std::move` 或改用非 RAII 句柄容器。 |
| D-03 | `rendering_device.cpp:12-38` | 旧 `RenderingDevice` 工厂忽略 `driver`，无条件 `make_shared<GLRenderingDevice>()`；`is_driver_supported(Vulkan)` 恒为 false；`get_default_driver()` 恒 OpenGL。 | 纯 Vulkan 构建直接编译失败；应用即便 Vulkan 构建也会创建 OpenGL 窗口。需要条件编译或引入旧设备→新 RHI 的桥接。 |
| D-04 | `vk_resource.cpp:99-132` | `VkImageCreateInfo` 关键字段缺失/错误：`arrayLayers=0`、`extent.depth=0`、cubemap 用 `e3D`、未分配并绑定 `VkDeviceMemory`。 | `vkCreateImage` 必然 validation 报错；即使创建成功也无法采样（未绑内存）。需补齐字段并引入显存分配器。 |
| D-05 | `vk_resource.cpp:142-159` | `VkImageViewCreateInfo` 只设了 image：`viewType` 默认 `e1D`、`format` 默认 `Undefined`、`subresourceRange` 默认全 0；`TextureViewDesc` 未使用。 | 视图创建必然失败；后续绑定/附件都无法使用。 |
| D-06 | `vk_rendering_device.cpp:32-88`；`vk_internal.h` | `create_buffer/queue_write_buffer/create_bind_group_layout/create_bind_group/create_pipeline_layout` 全为空或返回空句柄；`vk_internal.h` 没有 buffer/bind group/pipeline layout 的数据结构。 | 引擎 shader 依赖 UBO + 6 张纹理（纹理槽契约见 `AGENTS.md` 5.3-5），没有绑定组就根本传不了参数。需先做 descriptor set/pool/layout 与 buffer 上传。 |
| D-07 | `vk_command.cpp:54-136` | command buffer 从未 `begin()`；render pass 无 subpass、attachment 无 format/samples/initial/final layout；默认帧缓冲（无效 view）被跳过；`desc.width/height=0` 未推导；render pass/framebuffer 每帧新建且不销毁。 | 无法录制合法 pass，更无法渲染到窗口。需要：`begin()`、完整 render pass（含 subpass/dependency）、交换链分支、pass 缓存与销毁。 |
| D-08 | `vk_command.cpp:156-211` | `pass_set_bind_group`、`pass_set_vertex_buffer`、`pass_set_index_buffer`、`pass_draw`、`pass_draw_indexed` 全为空。 | 没有任何几何/参数被提交，画面一定为空。 |
| D-09 | `vk_command.cpp:244-247`；`vk_rendering_device.cpp:24-26,91-93` | `queue_submit`、`present`、`queue_wait_idle` 全为空。 | 不提交、不 acquire、不 present、不等待；窗口只会显示未定义内容。 |
| D-10 | `vk_pipeline.cpp:42-171` | 管线描述大面积缺失：两 stage 都没 `setStage/setPName`（fragment 会被当成 vertex、入口名为 null）；vertex input state 为空；`renderPass` 为 null（未启用 dynamic rendering）；`colorBlendAttachment` 未挂到 `VkPipelineColorBlendStateCreateInfo`；忽略 `desc.pipeline_layout` 与 `desc.depth_stencil`；multisample 硬编码 1。 | 管线创建会被 validation 拒绝，或创建后发现不了任何颜色/顶点状态。需要按 `RenderPipelineDesc` 完整映射。 |
| D-11 | `vk_resource.cpp:8-29`；`servers/rendering/shaders/*` | Vulkan 要求 SPIR-V，但 `ShaderModuleDesc.source` 传的是 GLSL 文本；现有 GLSL 是 `#version 330 core`，不能直接给 Vulkan；入口名被忽略；`pCode` 取自 `std::string::data()`，无 4 字节对齐保证。 | Shader 是整条链路的前置。必须决定：离屏 `glslc` 生成 `.spv`（CMake 已有 glslc 组件）或运行时 `glslang`，并补 Vulkan 专用 shader 变体。 |
| D-12 | `rendering_server.cpp`（全文使用 `RenderingDevice::get_singleton()`）；`application.cpp:17-25` | 引擎渲染链路没有调用 `RenderingDeviceRefactor`；`Application` 用旧设备、旧默认驱动（OpenGL），窗口也按 OpenGL 创建。 | 即使 Vulkan 适配层修好，也“没有调用方”。需要阶段的接入计划（先手工测试，最后接 `RenderingServer`）。 |

### 5.2 P1：功能/契约级（能编译后仍会导致错误行为）

| ID | 位置 | 问题 | 影响 / 修复方向 |
|---|---|---|---|
| D-13 | `vk_resource.cpp:128-131,155-156,214-216`；`vk_command.cpp:24-27,108-109,232-233`；`vk_pipeline.cpp:168-169` | 所有句柄 `.handle = 1`。 | 违反接口约定“handle 是后端唯一标识、0 无效”；两个纹理/编码器无法区分，任何按 handle 建表/比较的逻辑都会错。需统一 `alloc_handle_id()`（可参考 GL `gl_rendering_device_refactor.h:23`）。 |
| D-14 | `vk_resource.cpp:36-38` | destroy 时 `static_cast<VkShaderModule *>(handle.data)`。 | 对错误类型做 `memdelete`：堆损坏 + 内部 `vk::raii::ShaderModule` 永不析构。应 cast 到 `VkShaderModuleData`。 |
| D-15 | `vk_pipeline.cpp:173-176`；`vk_command.cpp:105-129,223-242`；接口约定第 2 条 | 大量资源无销毁：pipeline、pipeline layout、render pass、framebuffer、encoder data、command buffer data 都泄漏；`destroy_*` 也没有延迟销毁队列。 | 长时间运行必然耗尽显存/句柄；`destroy` 在提交后调用会直接释放仍在 GPU 使用的对象。需 fence/帧序号 + 延迟销毁表（`frame_index` 目前无用）。 |
| D-16 | `vk_rendering_device_init.cpp:359-407` | swapchain 只有 `vk::Image` 数组：没有 image view、没有深度缓冲、没有 per-image framebuffer、没有 `oldSwapchain`、没有 acquire/present 同步对象。 | 无法作为默认帧缓冲使用；窗口 resize 无法恢复；`present()` 无对象可呈现。 |
| D-17 | `vk_rendering_device.cpp:13-16`；`vk_rendering_device_init.cpp:312-323,400-405` | `get_swapchain_format()` 硬编码 `RGBA8UnormSrgb`；实际 `swapchain_image_format` 存了但不用；`choose_swap_surface_format` 优先 `eB8G8R8A8Srgb`（BGRA，`Format` 枚举无法表达）；没有 `vk::Format → Format` 反向映射。 | 管线目标格式与 render pass/交换链格式不匹配；BGRA 无法表示；还要统一 sRGB 约定（见 G-03）。 |
| D-18 | `vk_resource.cpp:44-94` | `Format` 映射错误：`RGBA8UnormSrgb → eR8G8B8A8Unorm`（丢 sRGB）、`Undefined → eR8G8B8A8Srgb`（应为非法）；`get_vk_image_usage` 里 `flags & TextureUsage::X != 0` 受运算符优先级影响，实际等价于 `flags & 1`，导致**所有 usage 位都被设置**。 | 纹理颜色空间错误；所有图像都被标成 color attachment/sampled/transfer/storage，可能触发格式能力校验失败。需改成 `(flags & X) != 0` 并补深度格式分支。 |
| D-19 | `vk_resource.cpp:69-94,99-132`；`vk_command.cpp:61-106` | 深度纹理/深度附件路径完全缺失：深度格式仍加 `eColorAttachment`；view aspect 未处理；render pass 无初始/最终 layout，图像从未 transition。 | 深度测试、阴影贴图、默认帧缓冲深度都不可用。 |
| D-20 | `vk_pipeline.cpp:128-150`；`vk_command.cpp:61-102,182-192` | 管线状态忽略 desc：multisample 硬编码 e1；depth test 恒开、depth write 恒关、depth compare 恒 `eNever`；颜色目标/blend/write mask 全部忽略；`samples_count` 不校验；动态 scissor 从未设置。 | 深度恒不通过 → 即便 draw 也全被裁掉；混合/写掩码失效。`pass_set_viewport` 应同时设 scissor（或在 begin pass 设默认）。 |
| D-21 | `vk_resource.cpp`（全文）；`vk_pipeline.cpp:42-171` | 没有 GL 那样的 `gl_validate_*` 对应物：`Format` 未定义、宽高 0、mip 超限、cubemap 非正方形、usage 未知位、view 范围越界、sampler lod 顺序、管线 target/depth 格式不匹配等都没有校验。 | 非法 desc 被直接送进 driver，只能靠 validation layer 兜底；错误发生在很难定位的 GPU 调用点。应照抄/移植 `gl_resources.cpp:259-405` 的校验逻辑。 |
| D-22 | `vk_rendering_device_init.cpp:73-100,275-283`；`vk_resource.cpp` 等 | `vk::raii` 抛出的 `vk::SystemError`/`std::runtime_error` 没有捕获；接口约定返回 bool/空句柄。 | 一次设备丢失/扩展缺失就会 terminate（`test_vulkan_init` 也没 catch）。需在每个 create 入口 try/catch，返回失败并打印上下文。 |
| D-23 | `servers/rendering/vulkan/*`（全局） | 没有显存分配器：所有 image/buffer 都没有 `vkAllocateMemory`/`vkBindImageMemory`；也没有 staging buffer 池。 | 资源无法真正创建；这是 V2 的前置决策（VMA vs 自建，见 G-06）。 |
| D-24 | `vk_rendering_device.cpp:95-98`；`vk_command.cpp:244-247` | `submit_frame` 只自增计数，没有 fence；`queue_submit` 也没有同步对象。 | 无法安全复用 command buffer/descriptor/上传内存，也无法实现延迟销毁。 |
| D-25 | `platform/glfw/glfw_window.cpp:282-286`；`application.cpp:35-94` | GLFW `swap_buffers()` 只对 OpenGL 生效；窗口 resize 事件（`FramebufferSize`）没有转发给设备；`resize_swapchain` 无调用方。 | Vulkan 下交换链永远不会被 present；resize 后画面拉伸/崩溃。 |
| D-26 | `vk_resource.cpp:192-217`；`rendering_device_refactor.h:243-255` | 采样器缺口：`compare_enable` 为 true 时没有 `compareOp`（接口 `SamplerDesc` 也没有该字段）；`lod_min_clamp/lod_max_clamp` 没做顺序校验；无 anisotropy 字段/设置。 | 阴影 PCF 的 compare sampler 不可用；非法 lod 会触发 validation。 |
| D-27 | `vk_pipeline.cpp:61-68` | 不校验 `ShaderModuleDesc.type` 与 `VertexStage/FragmentStage` 是否匹配；`VkShaderModuleData::type` 存了但没用。 | 顶点/片段模块可能装反，validation 报 stage 不匹配。 |
| D-28 | `vk_pipeline.cpp:152-171` | 先 `memnew` 再调用 `createGraphicsPipeline`，若调用抛异常则 data 泄漏；没有 pipeline cache；`data->pipeline_layout` 与 `desc.pipeline_layout` 是两个不同对象。 | 创建失败路径泄漏；绑定组无法与管线匹配。 |

### 5.3 P2：代码质量/可维护性

| ID | 位置 | 问题 |
|---|---|---|
| D-29 | `vk_pipeline.cpp:18-40`；`vk_command.cpp:30-52` | 枚举 switch 没有 `default` 分支返回值（cull/front-face 已产生 `-Wreturn-type` 警告）；遇到越界枚举值是 UB。 |
| D-30 | `vk_rendering_device.cpp:3-5`；`vk_rendering_device.h:26-27` | 注释过期：`vk_rendering_device.cpp` 说“真正初始化在本文件”，实际在 `vk_rendering_device_init.cpp`；头文件说“只保留接口签名”，实际已有实现。 |
| D-31 | `vk_rendering_device_init.cpp:5-9` | 用 `NODEBUG` 而不是 `NDEBUG` 控制 validation layer；Release 构建仍会启用调试层（若运行环境没有 layer 则抛异常）。 |
| D-32 | `vk_rendering_device_init.cpp:50-67` | debug messenger 无条件创建：validation 关闭时 `VK_EXT_debug_utils` 未启用，却调用 `createDebugUtilsMessengerEXT`；也没链入 instance 的 `pNext`，实例创建/销毁期的消息收不到。 |
| D-33 | `vk_rendering_device_init.cpp:187-208,212-240` | `rateDeviceSuitability` 死代码；物理设备只看第一个合适的，没有按评分选；无特性/扩展需求查询。 |
| D-34 | `vk_rendering_device_init.cpp:381` | swapchain `imageUsage` 只有 `eColorAttachment`，没有 `eTransferSrc`（截图/离屏合成会用到）。 |
| D-35 | `vk_resource.cpp:99-132` | `create_texture` 忽略 `label`、`samples_count`；`usage == 0` 不校验；`mip_levels` 不校验上限。 |
| D-36 | `tests/CMakeLists.txt:39-40` | 没有 Vulkan 资源/命令/格式映射测试，也没有 validation layer 失败即红的手工目标。 |
| D-37 | `vk_internal.h:5-49` | 数据结构只覆盖 shader/texture/view/sampler/pipeline/encoder/renderpass/commandbuffer；缺 buffer、bind group、bind group layout、pipeline layout、描述符池、内存分配器等对象。 |

---

## 6. 接口 / 契约缺口（必须先做设计决策）

这些问题不是“写漏了”，而是接口或工具链层面需要先定方案，否则实现会反复返工。

| ID | 缺口 | 现状 | 建议 |
|---|---|---|---|
| G-01 | Shader 源类型 | `ShaderModuleDesc.source` 是 `std::string`；GL 解释为 GLSL 文本，Vulkan 需要 SPIR-V 二进制。 | 在 `ShaderModuleDesc` 增加明确的 `source_kind`（GLSL 文本 / SPIR-V 二进制）或独立的 `spirv` 字段；不要靠后端猜。 |
| G-02 | Vulkan shader 变体与编译工具链 | 现有 shader 是 `#version 330 core`，使用 GLSL 330 的 uniform/sampler 写法；Vulkan 需要 450+、显式 binding、`gl_VertexIndex` 等。CMake 已能发现 `glslc`/`glslangValidator`。 | 优先离屏方案：CMake 自定义目标把 Vulkan 变体编译成 `.spv` 放入 `resources/shaders-vk/`；加载器按后端选择目录。运行时 `glslang` 作为备选。 |
| G-03 | 交换链格式与颜色空间 | GL 默认帧缓冲非 sRGB，shader 手工 `linear_to_srgb()`（`AGENTS.md` 5.3-20）；Vulkan 优先选 `B8G8R8A8Srgb`，硬件会做 sRGB 编码，若 shader 再手工编码就是双重编码。 | 明确 Vulkan 约定：要么选 UNORM 交换链 + shader 手工编码（与 GL 一致），要么选 sRGB 交换链 + 关闭手工编码（需要 shader 变体/分支）。同时给 `Format` 补 BGRA 表示或强制选 RGBA 格式。 |
| G-04 | 裁剪矩形 | 接口只有 `pass_set_viewport`，没有 scissor 命令；Vulkan 管线把 scissor 设为 dynamic。 | 约定 `pass_set_viewport` 同时设置 scissor（最简单），或在 `encoder_begin_render_pass` 按 pass 尺寸设默认 scissor。 |
| G-05 | 采样器比较函数 | `SamplerDesc.compare_enable` 只有 bool，没有 `compare_op`。 | 增加 `CompareFunction compare_op` 字段；Vulkan 需要 `compareOp`，GL 需要 `GL_TEXTURE_COMPARE_FUNC`。 |
| G-06 | 显存分配策略 | 当前完全没有 `vkAllocateMemory`；`thirdparty/` 没有 VMA。 | 决策：引入 VMA（推荐，但需要把源码/预编译库加入 `thirdparty`）或自建“每资源独立 allocation + 上传池”。这决定 D-04/D-06/D-23 的实现形态。 |
| G-07 | 默认帧缓冲深度 | 接口注释说“深度附件由后端提供的交换链深度缓冲承担”，但接口没有暴露“默认深度视图”的途径。 | Vulkan 在 `init_swapchain` 创建深度 image + view，并让 `encoder_begin_render_pass` 在 depth view 无效时自动使用它；同时明确每 swapchain image 对应一份。 |
| G-08 | 图像 acquire / present 归属 | `present()` 没有返回值，也没有“当前图像索引”入口。 | 后端内部在 `queue_submit` 前或 begin pass 时 acquire；`present()` 负责 present 并推进帧。需要在头文件注释中写清“帧边界由后端 maintainer”。 |
| G-09 | render pass 对象 vs dynamic rendering | 当前 Vulkan 代码试图用 `VkRenderPass` 对象（`encoder_begin_render_pass` 里创建），但管线创建又传 null renderPass，两者矛盾。 | 明确选一条：经典 render pass 对象（与现有代码一致，Windows/GL 对照更稳），或启用 `VK_KHR_dynamic_rendering` + `VkPipelineRenderingCreateInfo`。推荐前者，先落地。 |
| G-10 | 多帧在飞与延迟销毁 | 接口约定“destroy 任意时刻可调、Vulkan 延迟到 fence 之后”，但帧同步对象完全不存在；`submit_frame` 未使用。 | 设计 `frame_index + max_frames_in_flight(2~3) + per-frame fence/command buffer/descriptor pool + deferred destroy list`。 |
| G-11 | 句柄唯一性 | 接口要求 handle 由后端分配且唯一；Vulkan 侧全为 1。 | 把一个 `next_handle_id` 放进设备（GL 已有 `alloc_handle_id`），所有 `create_*` 用它；`destroy_*` 不再试图修改入参副本。 |
| G-12 | GL/Vulkan 校验逻辑复用 | GL 有 `gl_format_info/gl_validate_texture_desc/gl_validate_texture_view_desc/gl_validate_sampler_desc/gl_validate_render_pipeline_desc`（纯逻辑，可单测）。 | 抽成后端无关的 `rhi_validation.h/.cpp`，两个后端共用；避免 Vulkan 再缺一遍校验（D-21）。 |

---

## 7. 建议实施阶段（可勾选）

> 依赖关系：V0 → V1 → V2 → V3 → V4 → V5 → V6 → V7。V3 的交换链重建依赖 V2 的 image view；V5 的绑定组依赖 V2 的 buffer/texture；不要在 V2 之前碰 draw。

### V0 构建与骨架修复（约 0.5–1 天）

- [ ] **V0-1** `vk_command.cpp` 加入 `servers/rendering/CMakeLists.txt` 的 `if(ORCA_RENDERER_VULKAN)` 块。（D-01）
- [ ] **V0-2** 修 `vk_command.cpp` 两个编译错误：`vk::ClearValue` 用花括号；command buffer 容器改 move 或换非 RAII 句柄。（D-02）
- [ ] **V0-3** 修纯 Vulkan 构建：`rendering_device.cpp` 的旧工厂按后端条件编译；`is_driver_supported`/`get_default_driver` 按宏返回；或在旧设备退役前把纯 Vulkan 配置标为“不提供旧设备”。（D-03）
- [ ] **V0-4** 统一句柄分配器 + `destroy_*` 正确 cast/释放；先修 `destroy_shader_module`。（D-13/D-14）
- [ ] **V0-5** 所有 create/init 入口 try/catch `vk::SystemError`，返回失败并打印 `label`；`NODEBUG` → `NDEBUG`，debug messenger 跟随 validation 开关。（D-22/D-31/D-32）
- [ ] **V0-6** 建立构建矩阵：`Vulkan-only`、`Vulkan+GL`、`GL-only` 都要能配置+构建；`orca-vulkan-init-test` 在两种 Vulkan 配置下可链接并跑通 init。

**验收**：三种配置全绿；`orca-vulkan-init-test` 输出 physical device、swapchain extent；validation layer 开至 fatal。

### V1 格式映射与描述校验（约 1 天）

- [ ] **V1-1** 抽出 `Format ↔ vk::Format` 双向映射（`Format` 含 BGRA 决策或选格式策略）。（D-17/D-18/G-03）
- [ ] **V1-2** 修 `get_vk_image_usage` 运算符优先级；补深度格式 → `eDepthStencilAttachment`；usage==0 拒绝。（D-18/D-19）
- [ ] **V1-3** 移植 GL 的描述校验函数（纹理/视图/采样器/管线），放在后端无关文件，GL/VK 共用。（D-21/G-12）
- [ ] **V1-4** 纯逻辑单测加入 `orca-tests`：格式映射、usage 位、view 范围、pipeline 校验、非法描述拒绝。

**验收**：无需 Vulkan 设备即可跑单测；非法 desc 在 create 入口被拒绝，不再进入 driver。

### V2 显存、缓冲、纹理（约 2–3 天）

- [ ] **V2-1** 接入显存分配器（G-06 决策），实现 `create_buffer` + usage→`vk::BufferUsageFlags` 映射 + staging 上传池。（D-06/D-23）
- [ ] **V2-2** `queue_write_buffer` 按接口时序实现：记录待写数据，`queue_submit` 前 flush（或直接写 staging 并在提交命令中 copy）。（D-06）
- [ ] **V2-3** 修 `create_texture`：`arrayLayers`（2D=1/Cube=6）、`extent.depth=1`、Cube 用 `e2D + eCubeCompatible`、depth 格式用 depth attachment、绑定显存；加校验复用 V1-3。（D-04/D-19）
- [ ] **V2-4** 补全 `create_texture_view`：viewType/format/subresourceRange/aspect，解析 `mip_count/layer_count=0` 的默认范围。（D-05）
- [ ] **V2-5** 实现 `queue_write_texture`：staging buffer + layout transition + `vkCmdCopyBufferToImage`；cubemap 逐面；`bytes_per_row` 校验。（D-06）
- [ ] **V2-6** 实现 `generate_mip_map`：`vkCmdBlitImage` 逐级下采样并处理格式过滤支持。（D-06）
- [ ] **V2-7** sampler 补 `compareOp`（G-05）与 lod 校验；顺手补 anisotropy（若接口扩展）。（D-26）

**验收**：创建 2D/cubemap/depth 纹理、上传已知像素、生成 mip、创建 sampler，validation 全绿；销毁无泄漏（Debug 层对象统计或 `vkDeviceWaitIdle` 后退出）。

### V3 交换链 / 默认帧缓冲 / 呈现（约 1.5–2 天）

- [ ] **V3-1** `init_swapchain` 补 image view、深度 image/view（G-07）、每 swapchain image 的 framebuffer/render pass 缓存。（D-16）
- [ ] **V3-2** 实现 acquire/present：`vkAcquireNextImageKHR`、`VkSemaphore`（image available / render finished）、每帧 `VkFence`。（D-16/D-24）
- [ ] **V3-3** `encoder_begin_render_pass` 支持默认帧缓冲：无效 color view → 当前 swapchain image；无效 depth view → 内部深度；`desc.width/height=0` → 由附件推导。（D-07）
- [ ] **V3-4** render pass 用 `initialLayout/finalLayout` 处理 `ePresentSrcKHR`；首帧用 `eUndefined`，后续按图像状态切换。（D-19）
- [ ] **V3-5** `present()` → `vkQueuePresentKHR`；`resize_swapchain` 重建 swapchain（带 `oldSwapchain`）并重新创建依赖资源；`get_swapchain_format/size` 用真实格式与 extent。（D-17/D-25）
- [ ] **V3-6** 平台接线：GLFW/SDL3 的 resize 事件转发给设备；Vulkan 下 `swap_buffers` 不再依赖平台 swap。（D-25）

**验收**：录一个 `BeginPass(clear) + EndPass` 并提交，窗口显示稳定单色；连续两帧、resize 后仍正确；validation 全绿。

### V4 Shader 工具链（约 1–2 天，含决策）

- [ ] **V4-1** 决策并实现 G-01/G-02：`ShaderModuleDesc` 增加源类型；`builtin` 的 Vulkan GLSL 变体（450 + 显式 binding + 独立 sampler）编译为 `.spv` 并随资源拷贝。
- [ ] **V4-2** `create_shader_module` 按源类型处理 SPIR-V：长度/对齐校验、`entry_point` 使用、`ShaderType` 与 stage 一致性校验；失败走异常捕获。（D-11/D-27）
- [ ] **V4-3** 同步更新 `RenderingServer` 的 shader 加载路径（或先只给手工测试用）。

**验收**：从 SPIR-V 创建 shader module，validation 无错；为一个简单 shader 生成管线通过。

### V5 管线与绑定（约 2–3 天）

- [ ] **V5-1** `create_bind_group_layout` → `VkDescriptorSetLayout`；`create_pipeline_layout` → `VkPipelineLayout`（按 group 顺序）。（D-06）
- [ ] **V5-2** `create_bind_group` → descriptor pool（按帧/按组策略，G-12）+ `vkUpdateDescriptorSets`；校验 layout 匹配、buffer offset/size、view 类型。（D-06）
- [ ] **V5-3** 补全 `create_render_pipeline`：stages 的 stage/pName；vertex input binding/attribute（`VertexFormat` 映射 `vk::Format`）；renderPass 对象（G-09）；color blend state（targets/blend/write mask）；depth state（format/write/compare）；multisample；samples_count 校验；使用传入的 `pipeline_layout`。（D-10/D-20/D-28）
- [ ] **V5-4** destroy pipeline/layout/set layout；创建失败路径清理。（D-15/D-28）

**验收**：创建 `pipeline layout + bind group layout + bind group + pipeline` 成功；validation 无错；descriptor 内容与 shader 声明匹配。

### V6 命令录制 / 提交 / 同步（约 1.5–2 天）

- [ ] **V6-1** command encoder 生命周期：pool per frame、`begin()`/`end()`、`encoder_finish` 转移所有权（move 而非 copy）、`destroy_command_buffer` 真正释放。（D-07/D-15）
- [ ] **V6-2** pass 绑定：`vkCmdBindDescriptorSets`（含 dynamic offsets）、`vkCmdBindVertexBuffers`、`vkCmdBindIndexBuffer`、`vkCmdSetViewport/SetScissor`、`vkCmdDraw/DrawIndexed`。（D-08）
- [ ] **V6-3** `queue_submit` → `vkQueueSubmit`（semaphore/fence 链）；`queue_wait_idle` → `vkDeviceWaitIdle`；`present` 与帧 fence 配合；延迟销毁（G-10）。（D-09/D-24）
- [ ] **V6-4** pass 结束/帧结束释放 render pass/framebuffer（或缓存复用），不再每帧泄漏。（D-15）

**验收**：默认帧缓冲里画出一个彩色三角形；连续 60 帧无 validation 错误、无泄漏上涨。

### V7 验收场景与接入

- [ ] **V7-1** 手工 GPU 测试目标（隐藏窗口）：clear、三角形、带纹理的四边形、深度测试、cubemap 天空盒。（D-36）
- [ ] **V7-2** 与 GL 适配器同一场景对照（像素/截图或至少视觉对照）。
- [ ] **V7-3**（可选，独立大任务）把 `RenderingServer` 切到 `RenderingDeviceRefactor`：要么给新 RHI 写适配层，要么重写服务器绘制路径；在此之前 Vulkan 只作为验证后端。（D-12）

---

## 8. 测试与验收建议

1. **构建矩阵**
   - `GL-only`（当前默认）：保持 `ctest` 全绿。
   - `Vulkan+GL`：`orca-vulkan-init-test` 必须能链接、能跑。
   - `Vulkan-only`：修 D-03 后必须能配置+构建。
2. **纯逻辑单测**（无设备，加入 `orca-tests`）
   - `Format ↔ vk::Format` 双向映射、`TextureUsage` 位映射、`VertexFormat` 映射、`IndexFormat` 映射；
   - 纹理/视图/采样器/管线描述校验（移植 GL 校验后共享）。
3. **设备测试**（需要 Vulkan runtime）
   - `orca-vulkan-init-test` 扩展为：创建 instance/device/swapchain，打印格式/尺寸，clean shutdown；
   - 新增 `orca-vulkan-resources-test`（隐藏窗口）：创建 buffer/texture/view/sampler/bind group/pipeline，销毁，validation 全绿；
   - 新增 `orca-vulkan-draw-test`：清屏→三角形→带纹理四边形→深度→present。
4. **Validation layer 开关**
   - Debug 默认 `VK_LAYER_KHRONOS_validation`；把 `VK_LAYER_ENABLES` 中的 sync validation 也打开以捕捉 barrier 缺失；
   - 测试退出码以 validation error 为红（当前 callback 只打印）。
5. **泄漏检查**
   - Vulkan 对象计数：在 `VkRenderingDevice` 的 create/destroy 里维护 `live_object_count`（Debug 断言归零）；
   - 或开启 `VK_LAYER_LUNARG_api_dump`/Debug Utils object naming 辅助定位。
6. **回归**
   - `ctest --test-dir build --output-on-failure` 保持通过；新增 Vulkan 测试建议先作为手工目标，不阻塞 CTest。

---

## 9. 附录

### 9.1 `servers/rendering/vulkan/` 文件职责

| 文件 | 内容 | 状态 |
|---|---|---|
| `vk_rendering_device.h` | `VkRenderingDevice` 类声明、`QueueFamilyIndices`、`SwapChainSupportDetails` | 完整声明；成员缺 image view/depth/fence/semaphore/descriptor pool |
| `vk_rendering_device.cpp` | 生命周期查询/呈现/缓冲区/绑定组/队列的桩 | 大部分 ❌ |
| `vk_rendering_device_init.cpp` | instance/debug/surface/物理设备/逻辑设备/swapchain | ⚠️ 初始化骨架可跑，资源不完整 |
| `vk_internal.h` | 资源对象数据结构 | ⚠️ 缺 buffer/bind group/pipeline layout/descriptor/memory |
| `vk_resource.cpp` | shader/texture/image view/sampler + destroy | ⚠️ shader 契约、纹理/视图描述非法 |
| `vk_pipeline.cpp` | graphics pipeline | ⚠️ 骨架缺关键状态 |
| `vk_command.cpp` | encoder/render pass/draw/queue | ⚠️ 未加入构建、编不过、大面积空桩 |

### 9.2 与 GL 适配器的对齐说明

- GL 侧已实现、可直接参考的部分：
  - `gl_resources.cpp:221-405` 格式映射与四类描述校验（纯逻辑、无 GL 调用）；
  - `gl_resources.cpp:411-708` 纹理/视图/采样器创建与上传（含 cubemap、mip、`bytes_per_row`）；
  - `gl_resources.cpp:725-930` bind group layout / bind group / pipeline layout 的对象模型与校验；
  - `gl_command.cpp:53-349` 命令录制顺序校验与命令快照。
- GL 侧**同样未完成**的部分（不要把“GL 已实现”当既定事实）：
  - `gl_resources.cpp:936-940` 的 `create_render_pipeline` 仍是 `// TODO 重构` 空桩；
  - `gl_executor.cpp:5-19` 的 replay/flush 仍是占位实现；
  - GL RHI 也没有接到 `RenderingServer`（`AGENTS.md` 5.2 已注明）。
- 因此 Vulkan 的管线/回放实现不能“照抄 GL 成品”，只能复用其校验与对象布局思路。

### 9.3 与工程现状相关的非本层问题

- `RenderingDevice`（旧接口）本身不支持 Vulkan：`rendering_device.cpp:14-31` 写死 OpenGL；
  `Application`（`application.cpp:17-25`）也用它选驱动。Vulkan 适配层若要真正上屏，
  必须先解决“旧设备/新 RHI 二选一或桥接”。
- `platform` 层的 `RenderingWindowInfo` 已具备 `create_vulkan_surface` 与
  `vulkan_instance_extensions`（`glfw_window.cpp:260-277`、`sdl3_window.cpp:429-437` 在
  `ORCA_RENDERER_VULKAN` 宏下），这部分可用；缺的只是 resize 事件转发与 Vulkan present。
- `thirdparty/` 没有 VMA；显存分配需要新增依赖或自建（G-06）。

### 9.4 审计时的临时构建命令（V0 修复前基线）

> 下列命令在审计时分别暴露 D-01/D-02/D-03；V0 修复后应全部通过（实际结果见 §10.5）。

```powershell
# 纯 Vulkan（审计时编译失败于 D-03；V0 后应通过）
cmake -S . -B build-vk-only -G "MinGW Makefiles" `
  -DORCA_RENDERER_OPENGL=OFF -DORCA_RENDERER_VULKAN=ON -DORCA_RENDERER_DEFAULT=vulkan
cmake --build build-vk-only --target orca-vulkan-init-test

# 双后端（审计时链接失败于 D-01；V0 后应通过）
cmake -S . -B build-vk-gl -G "MinGW Makefiles" `
  -DORCA_RENDERER_OPENGL=ON -DORCA_RENDERER_VULKAN=ON -DORCA_RENDERER_DEFAULT=vulkan
cmake --build build-vk-gl --target orca-vulkan-init-test

# 单文件语法检查（审计时 4 个错误于 D-02；V0 后应通过）
g++ -std=c++17 -fsyntax-only -I servers/rendering -I core `
  -isystem "$env:VULKAN_SDK/Include" servers/rendering/vulkan/vk_command.cpp
```

> 注：临时的 `build-vk-only`/`build-vk-gl` 只用于审计验证，未写入仓库；复现时请使用自己的
> 临时目录，避免污染 `build/`。

---

## 10. 实施进度（V0 / V1，2026-11-16 更新）

> §4 的接口表与 §5 的缺陷表保留审计基线；本节记录实际修复与验证结果。

### 10.1 V0：构建/句柄/错误处理（已完成）

| 任务 | 内容 | 结果 |
|---|---|---|
| V0-1 | `vk_command.cpp`、`vk_format.cpp` 加入 `servers/rendering/CMakeLists.txt` | 完成 |
| V0-2 | 修 `vk_command.cpp` 编译错误：`vk::ClearValue` 改用花括号；command buffer 改为单 buffer + `std::move` 转移；录制前调用 `begin()` | 完成（该文件首次进入编译） |
| V0-3 | 纯 Vulkan 构建：旧 `RenderingDevice` 工厂按 `ORCA_RENDERER_OPENGL` 条件编译；`is_driver_supported/get_default_driver` 不再返回错误后端；`Application` 对空设备做保护 | 完成 |
| V0-4 | 所有 Vulkan 对象改用 `alloc_handle_id()` 生成唯一 handle；删除 `destroy_*` 里无效的本地句柄置零；修正 `destroy_shader_module` 的错误 cast | 完成 |
| V0-5 | `init`/`init_device`/`init_swapchain`/各 `create_*` 捕获 `vk::SystemError` 与 `std::exception`，返回 bool/空句柄；`NODEBUG`→`NDEBUG`；debug messenger 仅在 validation 开启时创建并链入实例 `pNext`；实现 `queue_wait_idle`/`shutdown` 的 `waitIdle` | 完成 |
| V0-6 | 构建矩阵 + init 冒烟测试：新增 `ORCA_VULKAN_INIT_TEST_EXIT_AFTER_INIT` 环境变量，支持 CI 只初始化不进入窗口循环；测试内增加命令池往返、采样器创建/销毁、非法描述拒绝检查 | 完成 |

### 10.2 V1：格式映射与描述校验（已完成）

| 任务 | 内容 | 结果 |
|---|---|---|
| V1-1 | `Format` 增加 `BGRA8Unorm`/`BGRA8UnormSrgb`；新增 `vulkan/vk_format.{h,cpp}`，提供 `Format ↔ vk::Format` 双向映射；交换链优先选择"接口能表达"的 sRGB 格式，`get_swapchain_format` 使用实际格式 | 完成 |
| V1-2 | `get_vk_image_usage` 改为 `(flags & X) != 0`；深度格式的 RENDER_ATTACHMENT 映射 `eDepthStencilAttachment`；`queue_wait_idle`、swapchain `imageUsage` 支持 `eTransferSrc` | 完成 |
| V1-3 | 新增后端无关 `rhi_validation.{h,cpp}`：格式谓词、纹理/视图/采样器/管线描述校验；GL 的 `gl_validate_*` 改为复用；Vulkan 的 `create_texture`/`create_texture_view`/`create_sampler`/`create_render_pipeline` 在入口调用 | 完成 |
| V1-4 | 新增 `tests/servers/test_rhi_validation.cpp`（始终编译）与 `tests/servers/test_vk_format.cpp`（Vulkan 开启时编译）；`tests/CMakeLists.txt` 条件加入 Vulkan 依赖 | 完成（7 个新用例、95 条断言通过） |
| 附带 | `create_texture` 描述字段合法化（`arrayLayers`、`extent.depth=1`、cubemap 用 2D+eCubeCompatible、深度 usage）；`create_texture_view` 补 viewType/format/subresourceRange；`create_shader_module`/texture/view/sampler/pipeline 创建失败路径释放已分配 data；GL 映射补 BGRA | 完成 |

### 10.3 接口状态变更（相对 §4）

| 接口 | 基线 | 现在 | 说明 |
|---|---|---|---|
| I-01 `init` | ⚠️ | ✅ | 异常被捕获；validation/debug 条件正确 |
| I-02 `init_device` | ⚠️ | ✅ | 异常被捕获；设备/队列创建可用 |
| I-05 `get_swapchain_format` | ⚠️ | ✅ | 返回实际交换链格式（本机实测 BGRA8UnormSrgb） |
| I-06 `get_swapchain_size` | ⚠️ | ✅ | 返回 swapchain extent |
| I-08 `shutdown` | ⚠️ | ✅ | `waitIdle` 后由 RAII 释放 |
| I-10 `destroy_shader_module` | ⚠️ | ✅ | 正确析构 `VkShaderModuleData` |
| I-30 `create_command_encoder` | ⚠️ | ✅ | 唯一 handle、录制前 begin、失败清理 |
| I-40 `encoder_finish` | ⚠️ | ✅ | 编译通过，池+缓冲 move 转移 |
| I-41 `destroy_command_buffer` | ❌ | ✅ | 释放命令池与 command buffer |
| I-43 `queue_wait_idle` | ❌ | ✅ | 调用 `device.waitIdle()` |
| 其余 | — | 不变 | I-03/I-09/I-14/I-15/I-28/I-31/I-36 仍为 ⚠️；缓冲区/绑定组/draw/submit/present 仍为 ❌ |

最新统计：**✅ 17 / ⚠️ 7 / ❌ 20**（总 44）。

### 10.4 缺陷关闭情况（相对 §5）

- **已关闭**：D-01、D-02、D-03、D-13、D-14、D-17、D-18、D-21、D-22、D-29、D-31、D-32、D-34。
- **部分关闭**：
  - D-04：`create_texture` 的描述字段已合法化，但仍缺显存分配/绑定（留待 V2）；
  - D-05：视图描述字段已补全，但仍依赖纹理显存（留待 V2）；
  - D-15：pipeline destroy、encoder/command buffer 所有权与销毁已修；render pass/framebuffer 每 pass 泄漏与延迟销毁仍开放；
  - D-19：深度格式的 usage/格式映射已修；render pass 的 depth attachment/layout 仍开放；
  - D-24：`queue_wait_idle` 已实现；fence/多帧在飞仍开放；
  - D-26：采样器 lod 校验已接入；`compare_op`（G-05）仍开放；
  - D-28：pipeline destroy 与失败清理已修；pipeline layout 仍与 `desc.pipeline_layout` 不一致；
  - D-35：`samples_count`/`usage`/`mip_levels` 校验与 label 日志已接入；其余限制未变。
- **仍开放（按优先级）**：D-04（显存）、D-06（buffer/bind group/pipeline layout）、D-07（默认帧缓冲 render pass）、D-08（draw/bind）、D-09（submit/present）、D-10（完整管线状态）、D-11（SPIR-V 工具链）、D-12（接入 RenderingServer）、D-16/D-25（swapchain 视图/深度/同步/resize）、D-23（分配器）、D-20/D-27/D-33/D-36/D-37。

### 10.5 验证结果

| 配置 | 构建 | 单元测试 | init 冒烟 |
|---|---|---|---|
| GL-only（`ORCA_RENDERER_OPENGL=ON`） | ✅ 全部目标 | ✅ `ctest` 通过（含新增 RHI 校验用例） | ✅ Vulkan 未编入时输出 SKIP、退出码 0 |
| Vulkan + GL | ✅ 全部目标 | ✅ `ctest` 通过（7 个新用例 / 95 断言） | ✅ 退出码 0 |
| Vulkan-only | ✅ 全部目标 | ✅ `ctest` 通过（含 Vulkan 格式映射用例） | ✅ 退出码 0 |

`init` 冒烟实测（Intel Graphics，Debug + validation layer）：

```text
[TEST] Window initialized.
[INFO] created window surface
[INFO] Using Physical Device: Intel(R) Graphics
[INFO] Created Vulkan Logical Device
[INFO] Vulkan created swapchain: 800x600, 3 images, format=9
[TEST] Vulkan initialized: swapchain format=9, size=800x600
[TEST] command encoder round-trip OK.
[TEST] sampler create/destroy OK.
[TEST] invalid descriptions rejected.
```

- `format=9` 即 `Format::BGRA8UnormSrgb`，说明交换链格式已能被接口表达。
- validation layer 输出中**没有 VUID/validation error**（只有 loader 的注册表/层加载日志）。
- 复现：设置 `$env:ORCA_VULKAN_INIT_TEST_EXIT_AFTER_INIT=1` 后运行
  `tests/orca-vulkan-init-test.exe`。

### 10.6 V0/V1 变更文件

新增：

- `servers/rendering/rhi_validation.h` / `.cpp`
- `servers/rendering/vulkan/vk_format.h` / `.cpp`
- `tests/servers/test_rhi_validation.cpp`
- `tests/servers/test_vk_format.cpp`

修改：

- `servers/rendering/CMakeLists.txt`、`tests/CMakeLists.txt`
- `servers/rendering/rendering_device_refactor.h`（Format 增加 BGRA）
- `servers/rendering/rendering_device.cpp`
- `servers/rendering/vulkan/vk_rendering_device.h` / `.cpp`
- `servers/rendering/vulkan/vk_rendering_device_init.cpp`
- `servers/rendering/vulkan/vk_internal.h`
- `servers/rendering/vulkan/vk_resource.cpp`
- `servers/rendering/vulkan/vk_pipeline.cpp`
- `servers/rendering/vulkan/vk_command.cpp`
- `servers/rendering/opengl/gl_resources.cpp`（复用共享校验 + BGRA 映射）
- `tests/servers/test_vulkan_init.cpp`
- `application/application.cpp`（空设备保护）

### 10.7 下一步（V2 前置决策）

1. 先定 G-06：VMA vs 自建显存分配器（本机 `thirdparty/` 无 VMA）。
2. 实现 `create_buffer` / `queue_write_buffer` / staging 上传。
3. 让 `create_texture` 完成 `vkAllocateMemory` + `vkBindImageMemory`；实现 `queue_write_texture` 与 `generate_mip_map`。
4. 之后进入 V3（swapchain image view/深度/acquire-present）。在 V2 完成前，`create_texture` 仍会因未绑显存而在 validation layer 报错，这是已知的中间状态。

