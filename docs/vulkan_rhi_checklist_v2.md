# Vulkan 适配层清单（RenderingDeviceRefactor）

> 版本：**2026-11-20 复核版 + V3-1 / I-04 / I-31 / V5-1/V5-2/V5-3 / V6-2 / V6-2b / V6-3 完成**
> （代码基线 `57cbf58`（V6-2 pass 绑定），工作区另含 V6-2b draw 与 V6-3 submit/present 改动）
> 审计对象：`servers/rendering/rendering_device_refactor.h`（WebGPU 风格接口，44 个纯虚函数）
> 与 `servers/rendering/vulkan/*`（Vulkan 适配层）。
> 复核方式：全量重读 Vulkan 适配层源码并逐接口对照，配合三配置构建、`ctest`、
> `orca-vulkan-init-test` 冒烟（证据见 §2）。
> 当前阶段：**V0 / V1 / V2 / V4 已完成**（构建、句柄、格式映射、描述校验、纹理与缓冲区显存、
> 内容上传、mipmap、纹理视图、采样器、SPIR-V 工具链与 shader 模块校验）；**V3-1 已完成**
> （交换链 image view、每图像深度缓冲、默认 render pass/framebuffer、acquire/present 同步对象）；
> **I-04 `resize_swapchain` 已完成**（oldSwapchain 重建交换链与全部依赖资源，零尺寸推迟）；
> **I-31 `encoder_begin_render_pass` 的 render pass 创建已完成**（完整附件描述/subpass/
> dependency、尺寸推导、默认帧缓冲分支、pass 内部对象随 command buffer 释放）。
> **V5-1 / V5-2 已完成**：`create_bind_group_layout` → `VkDescriptorSetLayout`、
> `create_bind_group` → 每个 bind group 独占 descriptor pool + descriptor set +
> `vkUpdateDescriptorSets`、`create_pipeline_layout` → `VkPipelineLayout`（按 group 顺序），
> 三个 destroy 与失败清理一并补齐；共享校验覆盖布局项、资源匹配与 buffer 区间解析。
> **V5-3 已完成**：`create_render_pipeline` 按 `RenderPipelineDesc` 完整映射顶点输入、
> renderPass 兼容性（G-09 决策：经典 render pass + 每管线一个兼容 render pass）、颜色
> 混合/write mask、深度状态与多重采样；顶点/混合/写掩码/采样数的纯映射也已加入单测。
> **V6-2 的 pass 绑定已完成**：`pass_set_bind_group`（`vkCmdBindDescriptorSets` + dynamic offsets
> 的数量/顺序/对齐/区间校验）、`pass_set_vertex_buffer`、`pass_set_index_buffer`、
> `pass_set_viewport`（同时派生并裁剪到 render area 的 scissor，G-04）均可用；render pass
> 开始时还会设置覆盖整个 render area 的默认 viewport/scissor。
> **V6-2b / V6-3 已完成**：`pass_draw`/`pass_draw_indexed` 录制 `vkCmdDraw`/`vkCmdDrawIndexed`
> （校验管线、索引缓冲与索引区间、pass_end 状态），默认帧缓冲在 begin pass 时惰性 acquire
> 交换链图像，`queue_submit` 走帧 fence + image_available/render_finished semaphore 链，
> `present()` 做 `vkQueuePresentKHR` 并推进帧。端到端帧（默认帧缓冲 clear → 索引/非索引 draw
> → submit → present）冒烟通过，validation 无 VUID。
> **仍未完成**：V3-5（平台 `FramebufferSize` 事件转发）、V6-1（按帧复用 command pool）、
> V6-4（帧 fence 之后的延迟销毁，D-15/G-10）、V7（GPU 读回验收并把 `RenderingServer` 切到新 RHI）。
> 本次整理原则：已完成项直接标记完成；原先按提交批次记录的逐条修复细节不再保留
> （已体现在代码中）；只留下当前状态、未完成项、仍开放缺陷、待决策缺口与后续计划。
> 历史审计基线（2026-11-16）保留在原文件 `docs/vulkan_rhi_checklist.md`，不再维护。
> 注意：原 Refactor 版 OpenGL 适配层已于 2026-11-18 移除，本清单只针对 Vulkan。

---

## 1. 结论摘要

1. **初始化链路可用。** `VkRenderingDevice` 完整实现 44 个纯虚函数签名；
   `init → init_device → init_swapchain` 可在真实窗口上跑通（本机 Intel Graphics，
   交换链 800x600、3 张图像、实际格式 `BGRA8UnormSrgb`），异常被捕获、validation 无 VUID。
   `init_swapchain` 现已同时创建每图像 color image view、每图像深度 image/view
   （`D32Sfloat`）、默认 render pass + 每图像 framebuffer，以及 2 组 acquire/present
   同步对象（semaphore × 2 + signaled fence），重复调用走 `oldSwapchain` 重建路径；
   `resize_swapchain` 复用同一路径重建，零尺寸（最小化）时只记录尺寸并推迟。
2. **资源创建链路（V2）可用。** 纹理（颜色/深度/cubemap）可创建、分配并绑定独占显存，
   可上传内容（含带行距的部分区域、逐 cubemap 面）、生成 mip 链；缓冲区可创建并写入；
   纹理视图（2D/深度/cubemap + mip 子范围）与采样器（含 `compare_op`）可创建；
   所有描述先经过后端无关的 `rhi_validation.*` 校验，非法描述在进入 driver 前被拒绝。
3. **Shader 工具链（V4）可用。** `ShaderModuleDesc` 增加显式 `source_kind`
   （`GLSL_TEXT` / `SPIRV`）与 `entry_point`；CMake 的 `orca-shader-spirv` 目标用
   `glslc` 把 `servers/rendering/shaders-vulkan/` 下的 Vulkan GLSL 变体编译成
   `.spv`（输出到 `<build>/resources/shaders-vulkan/`）；`create_shader_module`
   先走共享校验（长度/对齐/magic/版本/入口点/stage 一致性），再复制到对齐缓冲创建模块。
4. **适配层已经能出画面。** `encoder_begin_render_pass` 创建完整 render pass（显式附件的
   format/samples/initial+final layout、subpass 与两道 dependency，framebuffer 尺寸由附件推导），
   默认帧缓冲分支使用 `init_swapchain` 缓存的 render pass/framebuffer，并在第一次进入默认帧缓冲
   时惰性 acquire 交换链图像（V3-2）；绑定组布局、绑定组、管线布局与完整 graphics pipeline
   均能创建并绑定；pass 级绑定（V6-2）与 `pass_draw`/`pass_draw_indexed`（V6-2b，校验管线、
   索引缓冲与索引区间、pass 状态）都能录制；`queue_submit` 走帧 fence + image_available/
   render_finished semaphore 链，`present()` 调用 `vkQueuePresentKHR` 并推进帧（V6-3）。
   冒烟已跑通一帧完整流程（默认帧缓冲 clear → 管线/绑定 → 索引 + 非索引 draw → submit →
   present → waitIdle → 销毁），validation 无 VUID，且不再有 D-38 的编译警告。
   仍然缺少的只是**平台接线与工程化**：平台 resize 事件仍未转发给设备（V3-5）、encoder 未按帧
   复用、没有帧 fence 之后的延迟销毁（D-15/G-10），新 RHI 也尚未接入 `RenderingServer`
   （V7，`RenderingServer` 与 `Application` 仍走旧 `RenderingDevice`）。
5. **接口实现数量统计**（判据见 §3）：

   | 状态 | 数量 | 占比 |
   |---|---|---|
   | ✅ 可用 | 44 | 100% |
   | ⚠️ 有代码但当前不可用 / 违反契约 | 0 | 0% |
   | ❌ 未实现 | 0 | 0% |

   > 变化：`create_shader_module`（I-09）经 V4 修复后转为 ✅（25/5/14 → 26/4/14）；
   > `init_swapchain`（I-03）经 V3-1 补齐交换链依赖资源后转为 ✅（26/4/14 → 27/3/14）；
   > `resize_swapchain`（I-04）经上一轮实现后转为 ✅（27/3/14 → 28/3/13）；
   > `encoder_begin_render_pass`（I-31）经上一轮补齐 render pass 创建后转为 ✅（28/3/13 → 29/2/13）；
   > 绑定组/管线布局 6 项（I-22..I-27）经 V5-1/V5-2 实现后转为 ✅（29/2/13 → 35/2/7）；
   > `create_render_pipeline`（I-28）经 V5-3 修复后转为 ✅（35/2/7 → 36/1/7）；
   > pass 绑定 4 项（I-33/I-34/I-35/I-36）经 V6-2 实现后转为 ✅（36/1/7 → 40/0/4）；
   > `present`（I-07）、`pass_draw_indexed`（I-37）、`pass_draw`（I-38）、`queue_submit`（I-42）
   > 经 V6-2b/V6-3 实现后转为 ✅（40/0/4 → 44/0/0）。

6. **推进顺序**：V3-5（平台 resize 事件转发）→ V6-1（按帧复用 command pool）→ V6-4/D-15/G-10
   （帧 fence 之后的延迟销毁与多帧在飞）→ V7（GPU 读回验收、像素对照并把 `RenderingServer`
   切到新 RHI）。新 RHI 的每一帧现在都能录制、提交并呈现，剩余工作不再阻塞 draw/present。

### 1.1 下一批任务（按依赖排序）

- [x] **V3-1** 交换链 image view、每图像深度 image/view、默认 render pass + 每图像
      framebuffer、acquire/present 同步对象（D-16/G-07；见 §7）。
- [x] **V3-2** acquire/present 接线：`encoder_begin_render_pass` 的默认帧缓冲分支惰性调用
      `vkAcquireNextImageKHR`（`vk_swapchain.cpp:432`，内部先等待并 reset 帧 fence），
      `queue_submit` 等待 image_available、signal render_finished（D-16/D-24）。
- [x] **V3-3** `encoder_begin_render_pass` 支持默认帧缓冲：无效 color view → 当前交换链图像，
      无效 depth view → 内部深度缓冲，`width/height == 0` 由附件推导（D-07/D-19）。
      代码路径已完成（I-31），acquire 由 V3-2 补齐；冒烟已录制默认帧缓冲 pass。
- [x] **V3-4** `present` → `vkQueuePresentKHR`（D-17/D-25）；`resize_swapchain` 的重建部分已完成（I-04）。
- [x] **V5-1** descriptor set layout / pipeline layout（D-06）：`create_bind_group_layout` →
      `VkDescriptorSetLayout`，`create_pipeline_layout` → `VkPipelineLayout`（按 group 顺序）。
- [x] **V5-2** descriptor pool / descriptor set（D-06）：`create_bind_group` 为每个 bind group
      创建独占 pool + set 并 `vkUpdateDescriptorSets`，校验布局/资源/区间。
- [x] **V5-3** 补全 `create_render_pipeline`：顶点输入 binding/attribute、renderPass 兼容性
      （G-09：经典 render pass + 每管线兼容 render pass）、颜色混合/write mask、深度状态
      （write/compare）、multisample；使用传入的 `pipeline_layout` 已在 V5-1 完成（D-10/D-20）。
- [x] **V5-4** 三个 `destroy_*` 与创建失败路径清理（D-15/D-28）。
- [ ] **V6-1** command encoder 生命周期：按帧复用 pool、`begin()`/`end()`、`encoder_finish` move、
      `destroy_command_buffer` 真正释放（D-07/D-15）。当前每次 `create_command_encoder` 新建
      pool/buffer，仍可工作但未复用。
- [x] **V6-2a** pass 绑定：`vkCmdBindDescriptorSets`（含 dynamic offsets）、
      `vkCmdBindVertexBuffers`、`vkCmdBindIndexBuffer`、`vkCmdSetViewport/SetScissor`
      （I-33/I-34/I-35/I-36；G-04/D-20）。
- [x] **V6-2b** `pass_draw`/`pass_draw_indexed`（I-37/I-38）：校验管线、索引缓冲、索引区间与
      pass_end 状态后录制 `vkCmdDraw`/`vkCmdDrawIndexed`；数量为 0 时是合法空操作。
- [x] **V6-3** `queue_submit`（帧 fence + acquire/present semaphore 链）与 `present`
      （`vkQueuePresentKHR` + 帧推进）；**延迟销毁仍未做**（D-15/G-10），
      `destroy_command_buffer` 仍要求调用方先 `queue_wait_idle`。
- [ ] **V6-4** 帧 fence 之后的延迟销毁（destroy 任意时刻可调），以及一次 present 之间多次
      `queue_submit` 的 semaphore 链（G-10）。
- [ ] **V7-1** 手工 GPU 测试目标：clear → 三角形 → 纹理四边形 → 深度 → cubemap（D-36）。
- [ ] **V7-2** 与旧 GL 路径同场景像素/视觉对照。
- [ ] **V7-3** 把 `RenderingServer` 切到 `RenderingDeviceRefactor`（D-12）。

---

## 2. 验证现状（可复现）

| 配置 | 构建 | 单元测试 | `orca-vulkan-init-test` 冒烟 |
|---|---|---|---|
| Vulkan + GL（`ORCA_RENDERER_OPENGL=ON`、`VULKAN=ON`） | ✅ `orca-tests` + `orca-vulkan-init-test` 构建通过（**无编译警告**，D-38 已修） | ✅ `ctest` 通过（`orca-tests` 208 用例 / 2120 断言） | ✅ 退出码 0，交换链依赖资源（view/深度/framebuffer/同步）、`resize_swapchain`（800x600 → 640x480）+ render pass 录制（color+depth / depth-only）+ 纹理/视图/采样器/缓冲区 + 绑定组布局/绑定组/管线布局 + 完整管线创建/绑定 + pass 绑定（bind group + 动态偏移、顶点/索引缓冲区、viewport + scissor）+ **默认帧缓冲 acquire、`pass_draw`/`pass_draw_indexed`、`queue_submit`、`present` 的端到端帧** + SPIR-V shader 模块全通过，validation **无 VUID** |
| Vulkan-only（`OPENGL=OFF`、`VULKAN=ON`） | ✅ `orca-tests` + `orca-vulkan-init-test` 构建通过 | ✅ `ctest` 通过 | ✅ 退出码 0，绑定组/管线布局/完整管线 + pass 绑定 + draw/submit/present 用例通过，validation **无 VUID** |
| GL-only（`OPENGL=ON`、`VULKAN=OFF`，默认） | ✅ `orca-tests` + `orca-vulkan-init-test` 构建通过 | ✅ `ctest` 通过 | ✅ 输出 SKIP（stderr 一行“后端不可用”提示）、退出码 0 |

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
[INFO] Vulkan created swapchain depth: format=126, 3 image(s), 7260.38 KiB
[INFO] Vulkan created swapchain: 800x600, 3 images, format=9
[INFO] Vulkan swapchain resources: 3 image view(s), 3 depth image(s), 3 framebuffer(s), 2 frame sync set(s)
[TEST] Vulkan initialized: swapchain format=9, size=800x600
[INFO] Vulkan created swapchain depth: format=126, 3 image(s), 7260.38 KiB
[INFO] Vulkan created swapchain: 800x600, 3 images, format=9
[INFO] Vulkan swapchain resources: 3 image view(s), 3 depth image(s), 3 framebuffer(s), 2 frame sync set(s)
[TEST] swapchain re-init OK (image view/depth/framebuffer/sync recreated).
[INFO] Vulkan created texture(smoke-cube): 16x16, 5 mip(s), 6 layer(s), 24.00 KiB
[INFO] Vulkan created texture view(smoke-cube): Cube, mip 0..4, layer 0..5
[INFO] Vulkan generated mip maps(smoke-cube-mips): 5 level(s)
[INFO] Vulkan created buffer(smoke-vertex): 4096 byte(s), usage 0x09, 4.00 KiB
[TEST] buffer create/write OK (vertex + index with offset + uniform).
[TEST] invalid descriptions rejected.
```

- `format=9` 即 `Format::BGRA8UnormSrgb`：交换链格式已能被接口表达。
- `format=126` 即 `VK_FORMAT_D32_SFLOAT`：内部深度格式由设备 format properties 选择。
- 日志中的 KiB 来自 `vkGetImageMemoryRequirements2` / `vkGetBufferMemoryRequirements2`，
  说明显存（含每图像深度缓冲）确实分配并绑定。
- validation 输出中**没有 VUID / validation error**（只有 loader/层的注册表与卸载日志）。
- 冒烟覆盖：instance/device/swapchain、**交换链 image view / 每图像深度 / 默认 render pass +
  framebuffer / acquire-present 同步对象（含重复 `init_swapchain` 的 oldSwapchain 重建）**、
  **`resize_swapchain` 的 oldSwapchain 重建（零尺寸推迟 + 800x600 → 640x480）**、
  命令编码器往返、颜色/深度/cubemap 纹理与视图（含 cubemap mip 子范围与 6 类非法视图拒绝）、
  纹理整图/带行距部分区域上传、cubemap 逐面 + mipmap、缓冲区创建/偏移写入、
  SPIR-V shader 模块（triangle + builtin-skybox）、**绑定组布局/绑定组/管线布局
  （6 项布局 + 静态/动态 UBO + 2D/深度/cubemap + 独立 sampler）**、非法描述拒绝。
- **V3-1 证据**：`init_swapchain` 创建 3 个 color image view、3 个 `D32Sfloat` 深度
  image/view（各带独占显存）、1 个默认 render pass（color `Undefined → PresentSrcKHR`、
  depth `Undefined → DepthStencilAttachmentOptimal`）与 3 个 framebuffer、2 组
  image-available/render-finished semaphore + signaled frame fence；随后再次调用
  `init_swapchain` 走 `oldSwapchain` 重建，尺寸不变、validation 无 VUID。
- **I-04 证据**：冒烟先调用 `resize_swapchain(0, 0)`（模拟最小化），只打印推迟日志、
  交换链尺寸不变；随后把窗口客户区从 800x600 改为 640x480 并调用
  `resize_swapchain(640, 480)`，日志重新打印 `3 image view(s), 3 depth image(s),
  3 framebuffer(s), 2 frame sync set(s)`（深度显存由 7260.38 KiB 变为 4104.38 KiB），
  并打印 `[INFO] Vulkan resized swapchain: 640x480`，`get_swapchain_size` 返回 640x480，
  validation 无 VUID。
- **V4 证据**：`orca-shader-spirv` 用 `glslc --target-env=vulkan1.1` 编译 4 个 `.spv`
  （`triangle/{vert,frag}`、`builtin-skybox/{vert,frag}`）；冒烟从 `.spv` 创建 4 个 shader
  模块并销毁，validation 无 VUID。错误路径也已覆盖：GLSL 文本被拒绝、入口点不存在被拒绝、
  fragment SPIR-V 装进 vertex 槽被拒绝。
- **I-31 证据**：冒烟用 128x96 的 `RGBA8UnormSrgb` 颜色纹理（`RENDER_ATTACHMENT|TEXTURE_BINDING`）
  与 `D32Sfloat` 深度纹理各一张录制 `color+depth` pass（`desc.width/height = 0`），日志显示
  `[INFO] Vulkan began render pass(smoke-pass): 1 color + 1 depth, framebuffer 128x96, area 128x96`
  与 `[INFO] ... (smoke-depth-only): 0 color + 1 depth, framebuffer 128x96, area 128x96`，随后
  `pass_end` + `encoder_finish` + `destroy_command_buffer`（render pass/framebuffer 随 command
  buffer 释放）；validation 无 VUID。拒绝路径覆盖空附件、附件尺寸不一致（64x64 vs 128x96）、
  render area 越界（200x96 > 128x96）、显式 color + 默认深度混用、cubemap 视图作附件，以及
  尚未 acquire 的默认帧缓冲（错误信息明确指向 V3-2）。
- **V5 证据**：冒烟创建一个 6 项布局（binding 0 静态 UBO、1 2D、2 独立 sampler、3 深度、
  4 cubemap、5 动态 UBO），随后创建 `pipeline layout`（引用该布局）与 `bind group`
  （一个 buffer 同时供静态/动态 UBO，颜色/深度/cubemap 视图共用一个 sampler）；日志显示
  `[INFO] Vulkan created bind group layout(smoke-bgl): 6 binding(s)`、
  `[INFO] Vulkan created pipeline layout(smoke-pipeline-layout): 1 bind group layout(s)`、
  `[INFO] Vulkan created bind group(smoke-bind-group): 6 binding(s), layout(smoke-bgl)`；
  销毁顺序为 bind group → pipeline layout → 布局 → 资源，validation 无 VUID。拒绝路径覆盖
  重复 binding、空 visibility、纹理项上的动态偏移、缺项、未声明 binding、维度和 aspect 不匹配、
  非 UNIFORM 缓冲区、越界/过小区间、比较采样器配颜色纹理、无效布局句柄共 12 条；另外单独验证
  了「深度纹理 + compare 采样器」这一合法组合。
- **V5-3 证据**：冒烟用 `builtin-skybox` 顶点 shader（消费 location 0 的 `vec3`）与 `triangle`
  片段 shader（不采样描述符）创建两条完整管线：static 状态（cull back/CCW、depth write +
  `LessEqual`、write mask ALL、triangle list）与 blend 状态（`SrcAlpha`/`OneMinusSrcAlpha` 加法、
  只写 RGB、depth `Always` 且不写深度、cull none/CW、triangle strip），顶点布局为 stride 12 +
  location 0 `Float32x3`；日志显示 `[INFO] Vulkan created render pipeline(smoke-pipeline-opaque):
  1 color target(s), depth on, 1 sample(s)` 与对应的 `smoke-pipeline-blend`。随后把 opaque 管线
  绑定到 128x96 `RGBA8Unorm` + `D32Sfloat` 的显式 pass（附件格式/采样数/subpass 引用与管线
  兼容 render pass 一致，validation 的 pipeline/render-pass 兼容性检查通过，无 VUID）。
  `pipeline_layout` 按 skybox 顶点 shader 的 `set 0 / binding 0` UBO 声明了对应布局（否则会触发
  `VUID-VkGraphicsPipelineCreateInfo-layout-07988`）。拒绝路径覆盖 `samples_count = 2`、
  顶点 `array_stride = 0`、纯深度声明但缺少 fragment 模块（现在共享校验统一要求 fragment shader）。
- **未覆盖**：GPU 读回比对（无法验证像素内容，D-36）、连续多帧/多帧在飞与 resize 后的长时间
  稳定性、平台 resize 事件转发（V3-5）、`RenderingServer` 接入（V7）。
- **V6-2b/V6-3 证据**：冒烟在默认帧缓冲 pass 中依次验证 4 条 draw 拒绝路径（未 `pass_set_pipeline`
  的 draw/draw_indexed、未 `pass_set_index_buffer` 的 draw_indexed、`first_index + index_count`
  超出索引缓冲、`pass_end` 之后的 draw），随后录制合法的 `vkCmdDrawIndexed(3)` 与
  `vkCmdDraw(3)`；`encoder_finish` 后 `queue_submit` 提交（等待 acquire 的 image_available、
  signal render_finished、signal 帧 fence），`present()` 等待 render_finished 后
  `vkQueuePresentKHR`，再 `queue_wait_idle` 并释放 command buffer；同一帧第二次 `queue_submit`
  被入口拒绝。日志显示 `[INFO] Vulkan began default framebuffer pass(smoke-frame): image 0`，
  输出 `[TEST] pass draw/draw_indexed recorded OK`、`[TEST] pass draw rejection paths OK`、
  `[TEST] queue_submit + present OK`，validation 无 VUID。默认帧缓冲已 acquire 的图像在同一帧
  的多个默认帧缓冲 pass 中复用（第一帧的 `default-fb-begin` 与后面的 `smoke-frame` 都是 image 0）。

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
| I-03 | `init_swapchain` | ✅ | `vulkan/vk_swapchain.cpp:545`（辅助函数 `:160/:201/:312/:395/:432/:522`） | 使用实际 surface 格式/extent；创建 swapchain 与 `vk::Image` 数组、每图像 color image view、每图像深度 image/view（`D32Sfloat`，独占显存）、默认 render pass（color `Undefined→PresentSrcKHR`、depth `Undefined→DepthStencilAttachmentOptimal`，含 subpass 与两道统一 dependency）与每图像 framebuffer、2 组 acquire/present semaphore + signaled fence；重复调用释放旧依赖并传 `oldSwapchain`。默认帧缓冲分支在 `encoder_begin_render_pass` 中惰性 acquire 图像（V3-2），帧末由 `present` 呈现（V3-4）。 |
| I-04 | `resize_swapchain` | ✅ | `vulkan/vk_swapchain.cpp:689` | 更新 `window_info`；零尺寸（最小化）只记录并推迟重建；以 surface 实际 extent 为目标，与当前一致且依赖资源完整时不重建；否则调用 `init_swapchain()` 的 oldSwapchain 路径重建 image view/深度/render pass/framebuffer/同步对象。平台 resize 事件转发仍缺（V3-5/D-25）。 |
| I-05 | `get_swapchain_format` | ✅ | `vk_swapchain.cpp:57` | 用 `vk_format_from_vulkan` 把实际 `swapchain_image_format` 翻译成接口 `Format`，不可表达时告警。 |
| I-06 | `get_swapchain_size` | ✅ | `vk_swapchain.cpp:68` | 返回 `swapchain_image_extent`；未初始化时回退 `window_info`。 |
| I-07 | `present` | ✅ | `vulkan/vk_rendering_device.cpp:25` | 等待 render_finished semaphore 后 `vkQueuePresentKHR`；`eSuboptimalKHR` 告警继续，`eErrorOutOfDateKHR` 走 `init_swapchain` 重建；本帧未 acquire 图像时只推进帧；present 后归还图像、重置 `frame_submitted` 并轮换 `frame_syncs` 槽位（V3-4/V6-3）。 |
| I-08 | `shutdown` | ✅ | `vk_rendering_device.cpp:121` | `waitIdle`（捕获异常）+ 依赖 RAII 成员析构。 |

### 4.2 Shader（2 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-09 | `create_shader_module` | ✅ | `vulkan/vk_resource.cpp:65`；`rhi_validation.cpp:341` | 显式 `source_kind`（Vulkan 拒绝 GLSL 文本并提示用 glslc）；共享校验检查 SPIR-V 长度 4 字节倍数、20 字节头、magic、版本 1.0–1.6、指令流不越界、`OpEntryPoint` 存在且 ExecutionModel 与 `type` 一致；`pCode` 指向对齐的 `uint32_t` 拷贝；入口名存入模块供管线回退。 |
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
| I-22 | `create_bind_group_layout` | ✅ | `vulkan/vk_bind_group.cpp:123`；`rhi_validation.cpp:384` | 共享校验（binding 唯一、visibility 合法、动态偏移/min_binding_size 仅限 UBO）→ 设备每 set 数量上限检查 → `vk::DescriptorSetLayoutBinding`/`VkDescriptorSetLayout`；布局项缓存供 bind group 复用。空布局合法。 |
| I-23 | `create_bind_group` | ✅ | `vk_bind_group.cpp:239` | 共享校验布局匹配、资源类型、buffer offset/size/`min_binding_size`、view 维度/aspect/格式、compare 采样器只能配深度视图；后端补 uniform offset 对齐；每个 bind group 独占 descriptor pool（`maxSets=1` + `FREE_DESCRIPTOR_SET_BIT`）并分配 1 个 descriptor set，`vkUpdateDescriptorSets` 写入 UBO/组合图像采样器/独立 sampler。 |
| I-24 | `create_pipeline_layout` | ✅ | `vk_bind_group.cpp:476` | 按 `PipelineLayoutDesc::bind_group_layouts` 顺序创建 `VkPipelineLayout`；校验句柄/后端 data 与 `maxBoundDescriptorSets`；空布局合法。`create_render_pipeline` 改为使用它（D-28）。 |
| I-25 | `destroy_bind_group` | ✅ | `vk_bind_group.cpp:541` | `memdelete` 释放 pool（descriptor set 析构走 `vkFreeDescriptorSets`）与绑定的非拥有指针记录。 |
| I-26 | `destroy_bind_group_layout` | ✅ | `vk_bind_group.cpp:549` | `memdelete`；调用方须先销毁引用它的 bind group/pipeline layout。 |
| I-27 | `destroy_pipeline_layout` | ✅ | `vk_bind_group.cpp:557` | `memdelete`；调用方须先销毁使用它的 pipeline。 |

### 4.6 渲染管线（2 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-28 | `create_render_pipeline` | ✅ | `vulkan/vk_pipeline.cpp:127`（兼容 render pass `:65`、统一 dependency `:121`，销毁 `:396`） | 共享校验后按 `RenderPipelineDesc` 完整映射：管线布局使用调用方传入的 `pipeline_layout`；vertex input 按 `buffers[i]` → binding i、`VertexFormat` → `vk::Format`、stride/offset 原样映射；颜色目标逐项生成 `PipelineColorBlendAttachmentState`（blend 因子/操作 + write mask）；深度状态由 `depth_stencil.format/write/compare` 驱动（无深度格式时关闭 test/write）；`samples_count` → `SampleCountFlagBits`。G-09 决策为经典 render pass：每个管线创建一个仅用于兼容性检查的 render pass（附件格式/采样数/subpass 引用与录制侧一致，load/store/layout 不参与兼容性），并复用 `vk_render_pass_util::make_render_pass_dependencies()` 的同一组 subpass dependency（兼容性**包含** dependency 数量与内容）。颜色格式/深度格式映射失败、超过 `maxColorAttachments`、阶段模块类型不匹配均在上驱动前拒绝。仍无 pipeline cache（D-28 剩余项）。 |
| I-29 | `destroy_render_pipeline` | ✅ | `vk_pipeline.cpp:396` | `memdelete` 释放 `VkGraphicsPipelineHandleData`（pipeline 与其兼容 render pass 由 RAII 释放，pipeline layout 归调用方）。 |

### 4.7 命令录制（12 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-30 | `create_command_encoder` | ✅ | `vulkan/vk_command.cpp:301` | queue-family 命令池 + 单个 primary command buffer，并已 `begin()`；唯一 handle；失败路径释放 data。按帧复用 pool 属 V6-1（D-24 剩余）。 |
| I-31 | `encoder_begin_render_pass` | ✅ | `vulkan/vk_command.cpp:344`（`resolve_recording_pass:278`、`pass_end:976`、`destroy_command_buffer:1036`） | 显式附件：从视图解析格式/尺寸/父纹理实际布局，填全 `VkAttachmentDescription`（format/samples/load-store/initial+final layout），创建 subpass（color refs + depth ref）与两道统一 dependency；framebuffer 尺寸由附件推导（要求同尺寸），render area 取 `desc.width/height`（0→附件尺寸）并校验不越界；空附件/多深度/尺寸不一致/越界/非单层 2D 视图/混用默认帧缓冲均在创建 driver 对象前拒绝。默认帧缓冲分支使用 `init_swapchain` 缓存的 render pass/framebuffer，并惰性 acquire 交换链图像（V3-2）；同帧多个默认帧缓冲 pass 复用同一张图像。render pass/framebuffer 归 command buffer 所有，`destroy_command_buffer` 统一释放（D-15 仅剩延迟销毁）。begin 后立即录制覆盖整个 render area 的默认 viewport/scissor（G-04）。 |
| I-32 | `pass_set_pipeline` | ✅ | `vk_command.cpp:648` | 统一经 `resolve_recording_pass` 校验 pass 可录制；正确调用 `vkCmdBindPipeline` 并记录当前管线（`VkRenderPassHandleData::pipeline`），供后续 bind group/vertex buffer/draw 校验取 pipeline layout、顶点布局并要求管线已绑定。 |
| I-33 | `pass_set_bind_group` | ✅ | `vk_command.cpp:675` | 校验 group 下标、bind group 布局与 pipeline layout 对应 set 一致、dynamic offsets 数量/顺序（binding 编号升序）/`minUniformBufferOffsetAlignment` 对齐/叠加静态 offset 后的区间；随后 `vkCmdBindDescriptorSets`（单 set，含动态偏移）。 |
| I-34 | `pass_set_vertex_buffer` | ✅ | `vk_command.cpp:793` | 校验句柄、`VERTEX` 用途、offset 不越过缓冲区、slot 小于当前管线 `vertex_buffer_count`（未绑定管线时按 `maxVertexInputBindings` 校验），随后 `vkCmdBindVertexBuffers`（单 binding）。 |
| I-35 | `pass_set_index_buffer` | ✅ | `vk_command.cpp:846` | 校验句柄、`INDEX` 用途、`IndexFormat` 映射（Uint16/Uint32）、offset 按元素大小（2/4）对齐且不越过缓冲区，随后 `vkCmdBindIndexBuffer`；同时记录索引缓冲/offset/元素大小供 draw 校验区间。 |
| I-36 | `pass_set_viewport` | ✅ | `vk_command.cpp:896` | 校验有限值、宽高 > 0、深度范围 0..1 且 min <= max、`maxViewportDimensions`/`viewportBoundsRange`；同时录制 `vkCmdSetViewport` 与由 viewport 派生、裁剪到 render area 的 `vkCmdSetScissor`（G-04/D-20 关闭）。 |
| I-37 | `pass_draw_indexed` | ✅ | `vk_command.cpp:916` | 要求 pass 仍在录制、已 `pass_set_pipeline`、已 `pass_set_index_buffer`；校验 `index_buffer_offset + (first_index + index_count) * element_size` 不越过索引缓冲区后录制 `vkCmdDrawIndexed`；`index_count/instance_count == 0` 是合法空操作。 |
| I-38 | `pass_draw` | ✅ | `vk_command.cpp:959` | 要求 pass 仍在录制且已 `pass_set_pipeline`（顶点数据来源由管线/`gl_VertexIndex` 决定，不强制顶点缓冲），随后录制 `vkCmdDraw`；`vertex_count/instance_count == 0` 是合法空操作。 |
| I-39 | `pass_end` | ✅ | `vk_command.cpp:976` | `resolve_recording_pass` 校验后调用 `vkCmdEndRenderPass`，置 `VkRenderPassHandleData::active = false`（此后所有 pass 级命令被拒绝），并把附件 `finalLayout` 写回父纹理的 `current_layout`；render pass/framebuffer 由 `destroy_command_buffer` 统一释放。延迟销毁（帧 fence 后）仍见 D-15。 |
| I-40 | `encoder_finish` | ✅ | `vk_command.cpp:997` | `end()` 后把命令池 + command buffer 一起 move 到 `VkCommandBufferHandleData`，删除 encoder data；`submitted` 状态初始为 false。 |
| I-41 | `destroy_command_buffer` | ✅ | `vk_command.cpp:1036` | `memdelete` 释放命令池与 command buffer；当前是立即销毁，调用方必须先 `queue_wait_idle`（D-15）。 |

### 4.8 队列 / 帧（3 项）

| ID | 接口 | 状态 | 证据 | 现状 |
|---|---|---|---|---|
| I-42 | `queue_submit` | ✅ | `vk_command.cpp:1127` | 一次 present 之间只接受一次提交（重复提交/重复使用同一 command buffer 在入口被拒绝）；等待帧 fence（acquire 已等待/重置时跳过）、等待本帧 image_available、以 in_flight fence 提交、signal render_finished；纯 offscreen 帧（未 acquire）只提交命令并用帧 fence 节流。多提交的 semaphore 链与延迟销毁仍属 V6-4/G-10。 |
| I-43 | `queue_wait_idle` | ✅ | `vk_rendering_device.cpp:134` | `device.waitIdle()`，异常被捕获。 |
| I-44 | `submit_frame` | ✅ | `vk_rendering_device.cpp:147` | 返回递增计数；帧 fence 与帧槽位由 `queue_submit`/`present` 内部使用，`submit_frame` 本身仍未被调度层调用（D-24 剩余）。 |

---

## 5. 缺陷清单（仍开放 / 部分开放）

> 已关闭缺陷只保留 ID 与结论（§5.4），不再保留修复细节。P0 = 阻塞渲染，P1 = 功能/契约，
> P2 = 质量/可维护性。

### 5.1 P0：阻塞级

| ID | 位置 | 问题 | 影响 / 修复方向 |
|---|---|---|---|
| D-12 | `rendering_server.cpp`（全文走旧 `RenderingDevice::get_singleton()`）；`application.cpp:17-30` | 引擎渲染链路没有调用 `RenderingDeviceRefactor`；`Application` 用旧设备、按旧默认驱动建窗口。 | 即使 Vulkan 适配层已经能绘制/呈现，也“没有调用方”。先手工测试，最后接 `RenderingServer`（V7，独立大任务）。 |
| D-25 | `platform/glfw/glfw_window.cpp:282`；`application.cpp:116` | 平台已有 `FramebufferSize` 事件（`glfw_window.cpp:427`），但没有转发给设备；`resize_swapchain` 已实现（I-04）但仍无平台调用方；`present` 已实现（V3-4），Vulkan 下 `swap_buffers` 仍不负责 present。 | 窗口 resize 后新 RHI 的画面不会自动跟着重建、应用层也还不会调用 present；需要 V3-5 平台接线。 |

### 5.2 P1：功能 / 契约级

| ID | 位置 | 问题 | 影响 / 修复方向 |
|---|---|---|---|
| D-15（剩余） | `vk_command.cpp:1036`；`vk_internal.h:280` | render pass/framebuffer 已随 `destroy_command_buffer` 释放，`queue_submit` 也使用帧 fence，但 `destroy_*` 仍是立即 `memdelete`，没有帧序号/延迟销毁队列（`submit_frame` 未被调度层使用）；`destroy_command_buffer` 要求调用方保证 `queue_wait_idle` 之后才调用。 | 提交后销毁在途资源是 UB；需要 fence + 延迟销毁表（V6-4/G-10）。 |
| D-24（剩余） | `vk_rendering_device.cpp:147`；`vk_command.cpp:1127` | 帧 fence/槽位轮换与 acquire/present semaphore 已接线，但每次 `create_command_encoder` 仍新建命令池、`submit_frame` 只自增、一次 present 只接受一次 `queue_submit`。 | 无法安全地多命令缓冲在飞、也无法按帧复用 command pool；多提交的 semaphore 链与延迟销毁见 V6-1/V6-4/G-10。 |
| D-26（剩余） | `rendering_device_refactor.h:260-275` | 采样器 `compare_op` 与 lod 校验已完成；仍无 anisotropy 字段/设置（Vulkan 需 `samplerAnisotropy` 特性，GL 需 `EXT_texture_filter_anisotropic`）。 | 各向异性过滤不可用；接口扩展前不要只在后端加字段。 |
| D-28（剩余） | `vk_pipeline.cpp:374` | 已使用传入的 `desc.pipeline_layout` 并校验后端 data；完整管线状态已补齐（V5-3）；仍没有 pipeline cache，每次创建都会重复编译。 | 管线创建性能问题，后续单独实现（pipeline cache 的二进制缓存/磁盘缓存不在本期）。 |

### 5.3 P2：代码质量 / 可维护性

| ID | 位置 | 问题 |
|---|---|---|
| D-33 | `vk_rendering_device_init.cpp:200-256` | `rateDeviceSuitability` 是死代码；物理设备只取“第一个合适的”，没有按评分选；`device_features` 为空且从不开特性链（后续 anisotropy 等也依赖它）。 |
| D-36 | `tests/CMakeLists.txt` | Vulkan 侧已有格式/布局/校验单测与初始化冒烟（含 draw/submit/present 的一帧端到端录制），但仍缺：validation error 即失败、资源/绘制 GPU 目标、泄漏计数、GPU 读回比对（无法验证写入内容）。 |

### 5.4 已关闭缺陷（保留 ID 便于追溯）

D-01（CMake 漏 `vk_command.cpp`）、D-02（`vk_command.cpp` 编译错误）、D-03（纯 Vulkan 构建/旧工厂）、
D-04（`VkImageCreateInfo` 非法字段）、D-05（`VkImageViewCreateInfo` 未填）、
D-06（绑定组/管线布局空桩、`vk_internal.h` 缺 descriptor 结构；V5-1/V5-2 补齐 layout/pool/set 与
pipeline layout）、
D-07（render pass 附件缺 format/samples/layout/subpass/dependency、无效 view 被跳过、尺寸不推导、
pass/framebuffer 每 pass 新建即泄漏；I-31 补齐创建与释放）、
D-10（`create_render_pipeline` 只创建了 stage，顶点输入/renderPass/混合/深度/写掩码/采样数全部
缺失；V5-3 按 `RenderPipelineDesc` 完整映射并新增每管线兼容 render pass，冒烟验证管线可绑定到
格式一致的显式 pass）、D-11（GLSL 文本无法交 Vulkan、缺源类型与工具链；V4 补 `source_kind` + glslc `.spv` + 入口/stage 校验）、
D-13（句柄恒为 1）、D-14（`destroy_shader_module` 错误 cast）、D-16（交换链缺 image view/深度/
framebuffer/同步对象与 `oldSwapchain`；V3-1 在 `init_swapchain` 补齐，重复调用走 oldSwapchain 重建）、
D-17（交换链格式硬编码/无 BGRA 表示）、
D-18（格式映射与 usage 位运算错误）、D-19（深度附件缺 format/samples/layout、默认深度未 transition；
I-31 补齐附件模板，默认帧缓冲的深度 transition 由缓存的默认 render pass 承担）、
D-21（缺少描述校验）、D-22（异常未捕获）、
D-23（没有显存分配器/staging）、D-27（shader 模块 stage 与管线槽位不匹配；V4 增加类型校验与 pName）、
D-29（枚举 switch 无 `default`）、D-30（类注释与实现脱节；V3-1/I-04 更新注释后关闭）、
D-31（`NODEBUG` 拼写）、D-32（debug messenger 无条件创建/未链 pNext）、
D-34（swapchain 缺 `eTransferSrc`）、D-35（`create_texture` 字段/校验缺失）、
D-37（`vk_internal.h` 缺 bind group layout/bind group/pipeline layout/descriptor pool 结构；
V5-1/V5-2 已补 `Vk...HandleData`）、
D-20（`pass_set_viewport` 只设 `VkViewport`、动态 scissor 未录制；V6-2 让 viewport 同时派生并
裁剪 scissor 到 render area，pass 开始时另设默认 viewport/scissor，G-04 关闭）、
D-08（`pass_draw`/`pass_draw_indexed` 空实现；V6-2b 补上管线/索引缓冲/索引区间/pass 状态校验
并录制 `vkCmdDraw`/`vkCmdDrawIndexed`，冒烟验证已执行、validation 无 VUID）、
D-09（`queue_submit`/`present` 空实现；V6-3 补上帧 fence + acquire/present semaphore 链与
`vkQueuePresentKHR`，端到端帧冒烟通过）、
D-38（一次性提交路径忽略 `waitForFences` 的 nodiscard 返回值；已改为检查 `eSuccess` 并打印
`[ERROR]`，编译警告消失）。

---

## 6. 接口 / 契约缺口（待决策）

| ID | 缺口 | 现状 | 建议 |
|---|---|---|---|
| G-10 | 多帧在飞与延迟销毁 | 帧 fence/槽位轮换已存在（`queue_submit`/`present`），但一次 present 只接受一次 `queue_submit`、`destroy_*` 仍立即释放、`submit_frame` 未被调度层使用。 | 设计 `frame_index + max_frames_in_flight(2~3) + per-frame fence/command buffer/descriptor pool + deferred destroy list`；多命令缓冲提交需要额外的 semaphore 链（最后一个提交才 signal render_finished）。 |

已关闭的缺口：**G-01**（`ShaderModuleDesc` 增加显式 `source_kind` 与 `entry_point`）、
**G-02**（CMake `orca-shader-spirv` 用 glslc 把 `shaders-vulkan/` 变体编译成 `.spv`，
`ShaderLoader::load_spirv` 读取）、**G-03**（Vulkan 变体按 sRGB 交换链 + 硬件编码，
shader 不再手工 `linear_to_srgb()`；GL 变体保持原约定，见 `shaders-vulkan/*/frag.glsl` 注释）、
**G-05**（`SamplerDesc.compare_op` 已落地并映射两端）、**G-06**（显存策略定为
“自建、每资源独占 allocation”，实现于 `vulkan/vk_memory.*`，纹理与缓冲区均已接入）、
**G-07**（默认帧缓冲深度：`RenderingDeviceRefactor::DepthAttachmentDesc` 的无效 view 表示内部深度，
I-31 在默认帧缓冲分支使用缓存的深度 view；实际录制依赖 V3-2）、
**G-11**（`alloc_handle_id()` 统一分配唯一句柄）、**G-12**（校验抽到 `rhi_validation.*`，Vulkan 统一调用）、
**G-09**（render pass 方案定为“经典 `VkRenderPass` 对象”：录制侧沿用 I-31 的 render pass；
`create_render_pipeline` 为每条管线创建并持有一个仅用于兼容性检查的 render pass，其附件格式、采样数
与 subpass 引用必须与录制侧一致；load/store 与 initial/final layout 不参与 Vulkan 的兼容性判定；
**subpass dependency 的数量与内容参与兼容性**，因此三处 render pass 都通过
`vk_render_pass_util::make_render_pass_dependencies()` 生成同一组两道 dependency——这一点在 V6-2b
冒烟时由 `VUID-vkCmdDraw-renderPass-02684` 暴露，见 §9.2 陷阱 11）、
**G-04**（接口不新增 scissor 命令：`encoder_begin_render_pass` 先设覆盖整个 render area 的默认
viewport/scissor，`pass_set_viewport` 再同时录制 viewport 与派生的 scissor，并按 render area
裁剪；V6-2 已落地）、
**G-08**（图像 acquire / present 归属：接口没有独立的 acquire 入口与当前图像索引，
后端在第一次进入默认帧缓冲的 `encoder_begin_render_pass` 中惰性 acquire（V3-2），
`present()` 负责 `vkQueuePresentKHR` 并推进帧（V3-4）；帧边界由后端维护，已写入接口头注释）。

---

## 7. 实施阶段

> 依赖关系：V0 → V1 → V2 →（V3、V4 可并行）→ V5 → V6 → V7。V3 依赖 V2 的纹理/视图；
> V5 依赖 V2 的 buffer/texture 与 V3 的 render pass 方案；不要在 V3 之前碰 draw。
> **V4 已完成**（SPIR-V 工具链与 shader 模块校验不依赖交换链）。

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

### V3 交换链 / 默认帧缓冲 / 呈现（V3-1 / V3-3 已完成，其余待做）

- [x] **V3-1** `init_swapchain` 补 image view、深度 image/view（G-07）、每 swapchain image 的 framebuffer/render pass 缓存，并创建 acquire/present 同步对象。（D-16）
  - 实现：`vk_swapchain.cpp` 的 `create_swapchain_image_views` / `create_swapchain_depth_buffers` /
    `create_swapchain_render_pass_and_framebuffers` / `create_frame_sync_objects` /
    `destroy_swapchain_dependents`；深度格式按设备 format properties 选 `D32Sfloat`（回退
    `D24S8`/`D32SfloatS8`），每图像独占一份深度显存。
  - 默认 render pass 的 load/store 是固有能力：统一 color `Clear+Store`、depth `Clear+DontCare`；
    将来支持 `Load` 需按组合缓存 render pass + framebuffer（V3-3）。
  - 验收：冒烟中 3 image view / 3 depth image / 3 framebuffer / 2 frame sync 创建成功，
    重复 `init_swapchain` 走 oldSwapchain 重建且尺寸不变，validation 无 VUID。
- [x] **V3-2** acquire/present：`vkAcquireNextImageKHR`、image-available/render-finished semaphore、每帧 fence 的**接线**（同步对象已由 V3-1 创建）。（D-16/D-24）
  - 实现：`vk_swapchain.cpp` 的 `acquire_next_swapchain_image`（默认帧缓冲 begin 时惰性 acquire，
    先等待并 reset 本帧槽位的 `in_flight` fence，`eSuboptimalKHR` 继续、`eErrorOutOfDateKHR` 重建
    交换链后重试）；`queue_submit` 消费 `image_available` 并 signal `render_finished`。
- [x] **V3-3** `encoder_begin_render_pass` 支持默认帧缓冲：无效 color view → 当前交换链 image；无效 depth view → 内部深度；`width/height=0` → 由附件推导；完整 attachment（format/samples/initial/final layout）+ subpass/dependency。（D-07/D-19）
  - 实现：显式附件路径在 `vk_command.cpp` 解析视图格式/尺寸/父纹理实际布局，填全附件描述与
    subpass/dependency；framebuffer 由附件尺寸决定、render area 由 `desc.width/height` 决定；
    无效 color/depth view 走默认帧缓冲分支，复用 `init_swapchain` 缓存的 render pass/framebuffer。
  - 默认帧缓冲分支在 V3-2 接线后不再拒绝，而是 acquire 当前帧图像；同帧多个默认帧缓冲 pass
    复用同一张图像；请求 `Load` 仍被拒绝（默认 render pass 固定 Clear/Store）。
  - 验收：冒烟录制 color+depth 与 depth-only 两个 pass（128x96，尺寸由附件推导）并随 command
    buffer 释放，validation 无 VUID；6 条拒绝路径（空附件/尺寸不一致/越界/混用/cubemap）+ 默认帧缓冲
    acquire/begin 用例。
- [x] **V3-4** `present()` → `vkQueuePresentKHR`；`resize_swapchain` 已实现（I-04）；`get_swapchain_format/size` 保持真实值。（D-17/D-25）
  - 实现：`vk_rendering_device.cpp` 的 `present()` 等待 `render_finished`、调用
    `vkQueuePresentKHR`（`eSuboptimalKHR` 告警、`eErrorOutOfDateKHR` 重建交换链），随后归还
    图像、重置 `frame_submitted` 并轮换帧槽位；未 acquire 图像的帧只推进帧。
  - `resize_swapchain` 复用 `init_swapchain` 的 oldSwapchain 路径，零尺寸推迟、extent 未变时不重建；
    平台 resize 事件转发仍未接（V3-5）。
- [ ] **V3-5** 平台接线：GLFW/SDL3 的 `FramebufferSize` 转发给设备；Vulkan 下 `swap_buffers` 不再依赖平台 swap。（D-25）

**验收**：录一个 `BeginPass(clear) + EndPass` 并提交，窗口显示稳定单色；连续两帧、resize 后仍正确；
validation 全绿。**当前状态**：单帧的 acquire → clear → draw → submit → present 已通过冒烟；
连续多帧与 resize 后的稳定性待 V3-5 平台接线后验证。

### V4 Shader 工具链（已完成）

- [x] **V4-1** G-01/G-02：`ShaderModuleDesc` 增加 `source_kind`（`GLSL_TEXT` / `SPIRV`）与
  `entry_point`；新增 `servers/rendering/shaders-vulkan/`（triangle、builtin-skybox 的
  `#version 450` 变体，显式 set/binding），CMake `orca-shader-spirv` 目标用 `glslc`
  （`--target-env=vulkan1.1`）编译为 `<build>/resources/shaders-vulkan/<name>/{vert,frag}.spv`。
- [x] **V4-2** `create_shader_module` 只接受 SPIR-V：共享校验检查 4 字节长度、20 字节头、
  magic、版本 1.0–1.6、指令流边界、`OpEntryPoint` 入口名与 ExecutionModel/`ShaderType` 一致性；
  `pCode` 使用对齐的 `uint32_t` 拷贝；入口名记录在模块上；管线阶段补 `stage`/`pName` 并校验
  模块类型与槽位匹配（D-11/D-27）。
- [x] **V4-3** `ShaderLoader::load_spirv()` 从 `res://shaders-vulkan/<name>/{vert,frag}.spv` 读二进制；
  `orca-vulkan-init-test` 增加 triangle/builtin-skybox 模块创建与非法输入拒绝用例。

**验收（已完成）**：从 `.spv` 创建 shader 模块，validation 无 VUID；GLSL 文本、
错误入口名、stage 不匹配均在入口被拒绝。整条管线（shader 模块 + 管线布局 + render pass
兼容性）已在 V5-3 验收。

### V5 管线与绑定（V5-1 / V5-2 / V5-3 / V5-4 已完成）

- [x] **V5-1** `create_bind_group_layout` → `VkDescriptorSetLayout`；`create_pipeline_layout` → `VkPipelineLayout`（按 group 顺序）。（D-06）
  - 实现：`vulkan/vk_bind_group.cpp`；共享校验在 `rhi_validation.cpp`（布局项唯一性/visibility/
    动态偏移约束、管线布局句柄），后端补每 set 描述符数量上限与 `maxBoundDescriptorSets`。
- [x] **V5-2** `create_bind_group` → descriptor pool（每个 bind group 独占一个 maxSets=1 的 pool）+
  `vkUpdateDescriptorSets`；校验 layout 匹配、buffer offset/size、view 维度/aspect/格式。（D-06）
  - 实现：`rhi_resolve_bind_group_desc` 统一缺项/多项/类型/区间/`min_binding_size` 校验，
    后端补 uniform buffer offset 对齐；image layout 按颜色/深度取 ShaderReadOnly/DepthStencilReadOnly。
- [x] **V5-3** `create_render_pipeline` 完整状态：vertex input binding/attribute
  （`VertexFormat` → `vk::Format`）；renderPass 兼容性（G-09：每管线创建一个兼容 render pass，
  与录制侧共享附件格式/采样数/subpass 结构）；color blend state（targets/blend/write mask）；
  depth state（format/write/compare）；multisample（`samples_count` → `SampleCountFlagBits`）。
  另把顶点格式、blend 因子/操作、写掩码、采样数映射抽到 `vk_format.*` 并补单测。（D-10/D-20）
- [x] **V5-4** destroy bind group/layout/pipeline layout；创建失败路径清理。（D-15/D-28）

**验收（已完成）**：创建 `bind group layout + bind group + pipeline layout` 成功并销毁；
两条完整 `RenderPipelineDesc`（static 与 blend/write-mask/depth 状态）创建成功，并把管线绑定到
附件格式一致的显式 render pass（validation 的管线/pass 兼容性检查通过，无 VUID）；拒绝路径覆盖
samples=2、顶点 stride=0、缺少 fragment 模块，另有 12 条非法布局/绑定在入口被拒绝。
pass 级绑定命令已由 V6-2 完成，draw/submit/present 由 V6-2b/V6-3 完成（见下）。

### V6 命令录制 / 提交 / 同步（V6-2 / V6-2b / V6-3 已完成，V6-1 / V6-4 待做）

- [ ] **V6-1** command encoder 生命周期：按帧复用 pool、`begin()`/`end()`、`encoder_finish` move、`destroy_command_buffer` 真正释放。（D-07/D-15）
  - 现状：每条 encoder 新建命令池/命令缓冲，`encoder_finish` move、`destroy_command_buffer` 释放都已正确；
    按帧复用 pool 与多帧在飞尚未实现。
- [x] **V6-2** pass 绑定：`vkCmdBindDescriptorSets`（含 dynamic offsets，校验数量/顺序/对齐/区间）、
  `vkCmdBindVertexBuffers`、`vkCmdBindIndexBuffer`、`vkCmdSetViewport` + 派生并裁剪到 render area 的
  `vkCmdSetScissor`；pass 开始时设默认 viewport/scissor。（I-33/I-34/I-35/I-36；G-04/D-20）
  - 验收：冒烟把 set 0（静态 UBO + 动态 UBO）绑定到管线，随后绑定顶点/索引缓冲并设置部分移出
    render area 的 viewport；validation 接受录制出的全部命令，无 VUID。9 条拒绝路径（group 越界、
    动态偏移缺失、vertex slot 越界、VERTEX/INDEX 用途错误、索引 offset 未对齐、viewport 尺寸/深度
    范围非法）均在入口被拒绝。
- [x] **V6-2b** `vkCmdDraw`/`vkCmdDrawIndexed`。（D-08 关闭）
  - 实现：`pass_draw_indexed`/`pass_draw` 经 `resolve_recording_pass`（句柄/encoder/pass 活跃状态）
    校验后要求已 `pass_set_pipeline`；索引绘制还要求已 `pass_set_index_buffer`，并校验
    `offset + (first_index + index_count) * element_size` 不越过索引缓冲区；数量为 0 的空操作不录制
    命令。`pass_end` 置 `active=false`，之后的绑定/draw 全部被拒绝。
  - 验收：默认帧缓冲 pass 中录制 `vkCmdDrawIndexed(3)` + `vkCmdDraw(3)` 并提交执行（V6-3），
    validation 无 VUID；4 条拒绝路径（管线未绑定×2、索引缓冲未绑定、索引区间越界、pass_end 之后）
    在入口被拒绝。
- [x] **V6-3** `queue_submit` → `vkQueueSubmit`（semaphore/fence 链）；`queue_wait_idle` → `vkDeviceWaitIdle`；`present` 与帧 fence 配合。（D-09 关闭）
  - 实现：`queue_submit` 等待帧 fence（acquire 已等待/重置则跳过）、等待本帧 `image_available`、
    以 `in_flight` fence 提交、signal `render_finished`；纯 offscreen 帧只提交并用帧 fence 节流；
    `present()` 等 `render_finished` 后 `vkQueuePresentKHR` 并推进帧槽位；`eErrorOutOfDateKHR`
    重建交换链，`eSuboptimalKHR` 只告警。
  - 限制：一次 present 之间只接受一次 `queue_submit`（多提交 semaphore 链未做）；`destroy_*`
    仍是立即销毁，调用方必须先 `queue_wait_idle`（D-15）。
  - 验收：一帧 acquire → draw → submit → present → waitIdle → destroy 通过，validation 无 VUID；
    同帧第二次 `queue_submit` 与同一 command buffer 重复提交均在入口被拒绝。
- [ ] **V6-4** 帧 fence 之后的延迟销毁（destroy 任意时刻可调）、多提交 semaphore 链、render pass/framebuffer
  缓存/复用（不再每 pass 新建）。（D-15/G-10）

**验收**：默认帧缓冲里画出彩色三角形；连续 60 帧无 validation 错误、无泄漏上涨。
（单帧端到端链路已通过冒烟；连续 60 帧与泄漏检查属 V6-4/V7-1。）

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
   - `test_rhi_validation.cpp`：纹理/视图（含范围解析）/采样器（含 lod/compare）/缓冲区（含写入区间）/
     绑定组布局（唯一性/visibility/动态偏移约束）/绑定组解析（缺项、多项、类型、区间、
     `min_binding_size`、view 维度/aspect/格式）/管线布局句柄校验/管线描述校验（含
     “纯深度 pass 仍必须有 fragment 模块”），以及 shader 模块（source_kind、SPIR-V 头/magic/版本、
     入口点与 stage 一致性、截断指令流）。
   - `test_vk_format.cpp`（Vulkan 构建）：Format↔vk::Format、ImageUsage/BufferUsage、CompareOp、
     顶点格式（`VertexFormat` → `vk::Format`）、blend 因子/操作、颜色写掩码、采样数映射。
   - `test_vk_image_layout.cpp`（Vulkan 构建）：aspect/resting layout/barrier/mip 尺寸映射。
3. **设备测试**（需要 Vulkan runtime）
   - `orca-vulkan-init-test`（已有）：instance/device/swapchain + 交换链依赖资源 +
     `resize_swapchain`（含零尺寸推迟）+ render pass 录制（color+depth / depth-only，含 6 条
     拒绝路径）+ 纹理/视图/采样器/缓冲区 + SPIR-V shader 模块（triangle + builtin-skybox）+
     绑定组布局/绑定组/管线布局（含 12 条拒绝路径）+ 完整管线创建与绑定（vertex input /
     blend / write mask / depth，含 samples/stride/no-fragment 3 条拒绝路径）+
     pass 绑定（bind group + 动态偏移、顶点/索引缓冲区、viewport + 派生 scissor，含 9 条
     拒绝路径）+ **默认帧缓冲 acquire + `pass_draw`/`pass_draw_indexed` + `queue_submit` +
     `present` 的一帧端到端**（含管线顺序/索引缓冲/索引区间/pass_end 4 条 draw 拒绝路径与
     同帧重复提交拒绝）冒烟。
   - 待新增 `orca-vulkan-draw-test`：清屏 → 三角形 → 纹理四边形 → 深度 → 多帧循环 + resize，
     并做 GPU 读回比对（V7-1）。
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
| `vk_rendering_device.h` | `VkRenderingDevice` 类声明、`QueueFamilyIndices`、`SwapChainSupportDetails`、staging 池与一次性提交助手、交换链依赖资源、`frame_fence_reset`/`frame_submitted` 帧状态 | ✅ 声明完整；bind group 的 descriptor pool 由每个 bind group 独占持有（`vk_internal.h`） |
| `vk_rendering_device.cpp` | `present()`（vkQueuePresentKHR + 帧推进）、`shutdown`、`queue_wait_idle`、`submit_frame` | ✅ present 可用（V3-4/V6-3） |
| `vk_rendering_device_init.cpp` | instance/debug/surface/物理设备/逻辑设备 | ✅ 初始化可用 |
| `vk_swapchain.cpp` | 交换链创建、image view/每图像深度/默认 render pass+framebuffer/同步对象、`acquire_next_swapchain_image`（V3-2）、格式/尺寸查询、surface format/present mode 选择、`resize_swapchain` 重建 | ✅ 交换链、默认帧缓冲资源、acquire 与 resize 重建可用 |
| `vk_resource.cpp` | shader（SPIR-V 校验/对齐拷贝）、texture（显存/上传/mipmap）、image view、sampler 与对应 destroy | ✅ 均可用 |
| `vk_buffer.cpp` | buffer 创建/写入/销毁 + 设备级 staging 池 | ✅ 可用 |
| `vk_bind_group.cpp` | bind group layout / bind group（独占 descriptor pool + set + `vkUpdateDescriptorSets`）/ pipeline layout 与对应 destroy | ✅ 可用（V5-1/V5-2/V5-4） |
| `vk_pipeline.cpp` | graphics pipeline 创建/销毁；使用调用方传入的 `pipeline_layout`；按 `RenderPipelineDesc` 生成完整状态；持有每管线兼容 render pass（G-09，dependency 与录制侧统一） | ✅ 可用（V5-3；pipeline cache 仍缺，D-28） |
| `vk_command.cpp` | encoder/render pass（完整附件/subpass/dependency/尺寸推导/默认帧缓冲、默认 viewport/scissor）、`resolve_recording_pass` 统一入口校验、pass 绑定、`pass_draw`/`pass_draw_indexed`、`queue_submit`（帧 fence + semaphore 链）、一次性提交 | ✅ 录制/绘制/提交可用（V6-2/V6-2b/V6-3）；延迟销毁待 V6-4 |
| `vk_internal.h` | 资源对象数据结构（含 bind group/layout/pipeline layout/管线结构；render pass 记录当前管线、`active`、索引缓冲状态与 render area；command buffer 记录 `submitted`）；`vk_render_pass_util::make_render_pass_dependencies()` 统一 dependency 模板 | ✅ 结构完整；统一 dependency 模板保证管线兼容 render pass 与录制侧一致 |
| `vk_format.h/.cpp` | Format↔vk::Format、image/buffer usage、CompareOp、顶点格式、blend 因子/操作、颜色写掩码、采样数映射 | ✅ 纯逻辑，已单测 |
| `vk_memory.h/.cpp` | 显存类型选择 + 独占 `vkAllocateMemory` | ✅ 可用 |
| `vk_image_layout.h/.cpp` | aspect/resting layout/transition/mip 尺寸/layout barrier | ✅ 纯逻辑，已单测 |

`servers/rendering/shaders-vulkan/` 是 Vulkan 专用 GLSL 变体源码（`#version 450`、显式 set/binding、
sRGB 交换链约定）；构建时由 `orca-shader-spirv` 目标用 `glslc` 编到
`<build>/resources/shaders-vulkan/`，源码树里没有 `.spv`。GL 变体仍在 `shaders/`，两套文件不共用。

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
8. **比较采样器只能用于深度纹理。** 采样器本身可以在没有纹理时提前创建（`compare_enable`
   由 `SamplerDesc` 决定）；`create_bind_group` 的共享校验（`rhi_resolve_bind_group_desc`）
   会在绑定到颜色/立方体视图时拒绝（对应 Vulkan 的 VUID-VkDescriptorImageInfo-sampler-01082），
   深度纹理 + compare 采样器（阴影 PCF）是合法组合，冒烟已覆盖。
9. **`rhi_validation.*` 是唯一校验入口。** 新增资源类型或格式约束时先改共享层，再让 Vulkan 的
   `create_*` 走同一路径；不要在 Vulkan 里另立分支。
10. **交换链格式与颜色空间是端到端契约（G-03 已定）。** 本机实际格式是 `BGRA8UnormSrgb`；
    Vulkan 变体（`shaders-vulkan/`）按“sRGB 交换链 + 硬件编码”写，不要再手工 `linear_to_srgb()`；
    旧 GL 路径的默认帧缓冲不是 sRGB，GL 变体仍必须手工编码。两套变体不要互相拷贝代码。
11. **管线与 render pass 的兼容性靠“每管线兼容 render pass”（G-09 已定）。** 录制侧走经典
    `VkRenderPass`（I-31），`create_render_pipeline` 不能传 `nullptr` renderPass，也未启用
    dynamic rendering。因此每条管线创建并持有一个只用于兼容性检查的 render pass：颜色附件按
    `targets` 顺序排 0..n-1、深度附件排 n，格式/采样数与录制侧一致；只豁免 load/store 与
    initial/final layout。**subpass dependency 的数量与内容也参与兼容性**：默认帧缓冲 pass、
    显式附件 pass、管线兼容 render pass 都必须使用 `vk_render_pass_util::make_render_pass_dependencies()`
    生成的同一组两道 dependency，否则 draw 时 validation 会报
    `VUID-vkCmdDraw-renderPass-02684`（V6-2b 冒烟实测过 dependencyCount 不一致）。
    **实际 pass 的附件数量/顺序/格式/采样数必须与管线 desc 一致**（例如默认帧缓冲的颜色格式
    必须等于 `get_swapchain_format()`、深度格式等于交换链内部深度格式），否则绑定/绘制时
    validation 会报不兼容；`VkGraphicsPipelineHandleData` 中兼容 render pass 声明在 pipeline
    之前，保证逆序析构时先销毁 pipeline。
12. **fragment shader 在光栅化开启时是必需的。** 只声明深度目标（`fragment.targets` 为空）
    也必须提供 fragment 模块；共享校验（`rhi_validate_render_pipeline_desc`）不再允许“有深度
    格式但无 fragment 模块”。Vulkan 只在 `rasterizerDiscardEnable = VK_TRUE` 时才允许省略
    fragment 状态，而本接口没有 discard 开关。另外管线布局必须满足 shader 声明的 descriptor
    set（冒烟里 skybox 顶点 shader 的 `set 0/binding 0` 必须出现在 `pipeline_layout` 中，否则
    `VUID-VkGraphicsPipelineCreateInfo-layout-07988`）。
13. **句柄与销毁。** 所有 handle 由 `alloc_handle_id()` 分配且唯一；`destroy_*` 目前立即 `memdelete`，
    而 `queue_submit` 之后命令仍在途：**必须先 `queue_wait_idle()`（或帧 fence 已确认）再销毁
    command buffer / pass / 管线 / 绑定的资源**。帧 fence 之后的延迟销毁（D-15/V6-4）尚未实现，
    调用方不能假设 destroy 是“任意时刻安全”的（接口头注释已注明当前约定）。
14. **render pass 的附件、布局与释放都有固定约定（I-31）。** 显式附件必须是单层 2D 视图且同尺寸；
    附件描述必须填全 format/samples/load-store/initial+final layout，并配一道 subpass 与
    external→subpass / subpass→external 两道统一 dependency。`initialLayout` 取父纹理的
    `current_layout`（不要硬编码 Undefined），`finalLayout` 固定为 `ColorAttachmentOptimal` /
    `DepthStencilAttachmentOptimal`，由 `pass_end` 写回父纹理——因此**录制顺序决定布局簿记**，
    录制后丢弃命令缓冲会让簿记失真。framebuffer 尺寸由附件决定，`desc.width/height` 只是
    render area（0 表示取附件尺寸）。render pass/framebuffer 归 command buffer 所有，
    `destroy_command_buffer` 统一释放（立即销毁，D-15）。begin 后还会立即录制覆盖整个 render
    area 的默认 viewport/scissor（G-04），因此不调用 `pass_set_viewport` 也能 draw；`pass_end`
    之后所有 pass 级命令（绑定和 draw）都会被 `resolve_recording_pass` 拒绝。
15. **帧边界由 `present()` 维护。** 默认帧缓冲在第一次 `encoder_begin_render_pass` 时惰性
    acquire 当前帧图像（同一帧内复用）；`queue_submit` 一次 present 之间只接受一次（多提交需要
    额外 semaphore 链，见 V6-4/G-10），并等待 image_available、signal render_finished；
    `present()` 等 render_finished 后 `vkQueuePresentKHR`，把图像交还 presentation engine 并
    轮换 `frame_syncs` 槽位（`current_frame`）。因此**一帧的调用顺序是**：
    `create_command_encoder → encoder_begin_render_pass(默认帧缓冲) → ... → pass_end →
    encoder_finish → queue_submit → present →（需要销毁时先 queue_wait_idle）→ destroy_*`。
    没有 acquire 图像的帧（纯 offscreen）present 只推进帧、不做呈现。
16. **`init_swapchain` 前必须先有 surface。** 离屏（`create_vulkan_surface` 为空）会直接返回 false；
    `findQueueFamilies` 依赖 surface 做 present 支持查询。
17. **SPIR-V 由构建期 glslc 生成，不是运行时编译。** `ShaderModuleDesc.source` 必须配
    `source_kind = ShaderSourceKind::SPIRV`；GLSL 文本会被 Vulkan 拒绝。`.spv` 位于
    `<build>/resources/shaders-vulkan/`（`res://` 依赖当前工作目录），修改
    `shaders-vulkan/*.glsl` 后重新构建即可；入口名由 `OpEntryPoint` 校验，模块 stage 与管线
    槽位不一致会被 `create_render_pipeline` 拒绝。
18. **交换链依赖资源有销毁顺序约束，且重复 `init_swapchain` 会重建它们。**
    `framebuffer`/`image view`/深度资源必须声明在 `swapchain` 之后（逆序析构）；
    重建时先 `device.waitIdle()`，再按 framebuffer → render pass → depth（view/image/memory）
    → color view 的顺序释放，然后才把旧 `swapchain` 作为 `oldSwapchain` 交回驱动。
    任何新加"依赖交换链 image"的对象都要挂进 `destroy_swapchain_dependents()`，
    否则 resize 后会留下悬空 view/framebuffer。`resize_swapchain` 复用这条路径，
    不要在它里面再次手动释放/创建依赖资源。
19. **默认帧缓冲 render pass 的 load/store 是固定的。** 目前统一 color `Clear+Store`、
    depth `Clear+DontCare`（`finalLayout` 分别为 `PresentSrcKHR` 与
    `DepthStencilAttachmentOptimal`）。load/store 不能像 clear value 那样在 begin 时覆盖；
    `encoder_begin_render_pass` 的默认帧缓冲分支只接受 color `Clear`/`DontCare`，请求 `Load`
    会被拒绝。要支持 `Load` 必须按 load/store 组合缓存 render pass + framebuffer，
    不能直接复用这一个。
20. **`resize_swapchain` 的尺寸以 surface 为准，零尺寸必须推迟。** 更新 `window_info`
    只是为 `choose_swap_extent` 的 `UINT32_MAX` 回退分支提供输入；Windows 下
    `currentExtent` 由窗口客户区决定，不能用请求值硬覆盖 `VkSwapchainCreateInfoKHR::imageExtent`
    （会违反 `VUID-VkSwapchainCreateInfoKHR-imageExtent-01274`）。窗口最小化时目标 extent 为 0，
    调用只记录尺寸并返回，等下一次非零 resize 再重建。重建成功前不要消费
    `get_swapchain_size` 作为 viewport（它返回的仍是旧 extent）。
21. **默认帧缓冲的 render pass/framebuffer 归交换链所有，acquire 由后端惰性完成。**
    `encoder_begin_render_pass` 默认帧缓冲分支使用 `current_swapchain_image` 对应的缓存
    framebuffer；进入前若尚未 acquire 则调用 `acquire_next_swapchain_image()`（V3-2），
    它先等待并 reset本帧槽位的 in_flight fence，再 `vkAcquireNextImageKHR`。重建交换链
    （`init_swapchain`/`resize_swapchain`）会销毁这些缓存对象，而 `device.waitIdle()` 只等待
    已提交的命令：引用它们的命令缓冲必须在重建前提交并执行完，否则留下悬空的
    render pass/framebuffer 引用。`TextureViewHandleData` 还持有父纹理的非拥有
    指针（读 `current_layout`），销毁顺序必须是视图先于父纹理。
22. **绑定组/管线布局/管线的所有权与生命周期。** `create_bind_group_layout` 缓存布局项；
    bind group 持有布局、buffer、view、sampler 的非拥有指针，pipeline layout 持有布局的
    非拥有指针，`VkGraphicsPipelineHandleData` 持有 pipeline layout 的非拥有指针。销毁顺序必须是
    bind group / pipeline → pipeline layout → bind group layout → 资源，否则留下悬空指针。
    每个 bind group 独占一个 descriptor pool（`maxSets = 1` + `FREE_DESCRIPTOR_SET_BIT`）：
    `vk::raii::DescriptorSet` 析构会调用 `vkFreeDescriptorSets`，没有该 flag 会报
    `VUID-vkFreeDescriptorSets-descriptorPool-00312`（冒烟已实测）。组合图像采样器要求
    texture view + sampler 同时存在；descriptor 的 imageLayout 取 `ShaderReadOnlyOptimal`
    （深度为 `DepthStencilReadOnlyOptimal`），但纹理仍可能被 render pass 切到附件布局，
    真正的采样 barrier 必须由 V6 在提交前按 `current_layout` 补齐。UniformBuffer 的
    `offset`（含动态偏移的基址）必须按 `minUniformBufferOffsetAlignment` 对齐；动态偏移已由
    `pass_set_bind_group` 校验数量、顺序（binding 编号升序）、对齐与叠加静态 offset 后的区间。
23. **pass 级绑定的顺序与状态（V6-2 / V6-2b）。** 所有 pass 级命令先经 `resolve_recording_pass`
    统一校验（句柄有效、encoder 未 finish、pass 未 `pass_end`）。`pass_set_bind_group` 依赖当前管线：
    必须先 `pass_set_pipeline`（`vkCmdBindDescriptorSets` 需要 pipeline layout），group 下标必须在
    pipeline layout 的 set 范围内，bind group 的布局必须与该 group 的布局一致。动态偏移按
    binding 编号升序消费，数量必须等于布局中 `has_dynamic_offset` 的项数；每个偏移按
    `minUniformBufferOffsetAlignment` 对齐，且 `descriptor offset + 动态偏移 + 区间` 不能越过
    缓冲区大小。`pass_set_vertex_buffer` 要求 `VERTEX` 用途且 slot 小于管线声明的顶点缓冲数量
    （未绑定管线时按设备 `maxVertexInputBindings` 校验）；`pass_set_index_buffer` 要求 `INDEX`
    用途且 offset 按元素大小（Uint16=2 / Uint32=4）对齐，并记录索引缓冲/offset/元素大小供
    `pass_draw_indexed` 校验区间。`pass_draw`/`pass_draw_indexed` 还要求已经 `pass_set_pipeline`，
    索引绘制要求已经 `pass_set_index_buffer` 且 `first_index + index_count` 不越过索引缓冲；
    数量为 0 时是合法空操作、不录制 draw。render pass 开始时本后端设置覆盖整个 render area
    的默认 viewport/scissor；`pass_set_viewport` 会同时设置 viewport 与由它派生、裁剪到 render
    area 的 scissor（G-04）——管线把这两项都设为 dynamic，缺少 scissor 会让 draw 读到未定义状态。
    命令只能在 pass 所属 encoder 还活着时录制（`encoder_finish` 后调用直接报错）。
24. **`queue_submit` 与 `present` 的同步链。** `queue_submit` 等待 `image_available`（仅本帧
    acquire 过时）、以 `in_flight` fence 提交、signal `render_finished`；`present` 等
    `render_finished` 后 `vkQueuePresentKHR`。`VkCommandBufferHandleData::submitted` 防止同一
    command buffer 重复提交，`frame_submitted` 防止同一帧多次提交——两者在入口被拒绝。纯 offscreen
    帧（没有 acquire）只提交命令并用帧 fence 节流，`present` 只推进帧。`waitForFences` 的返回值
    现在会检查（D-38 已修），超时/设备丢失时走失败路径而不是当成成功。
