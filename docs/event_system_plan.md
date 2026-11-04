# GUI / Input 事件系统实现方案

> 目标：为 orca-engine 补上「输入事件 → 场景树 → 节点 → GUI」的完整链路，
> 让节点系统（Node / Control）能够真正消费平台层的鼠标、键盘、文本、窗口事件，
> 从而支撑 GUI 控件交互与「用输入操作场景物体」。
>
> 约束：
> - 保持现有分层铁律（`resource` 不依赖 `rendering`、`platform` 不含图形 API 类型、`scene` 负责把资源变成渲染调用）。
> - 事件数据类型不能依赖渲染/资源层；节点分发不能依赖平台后端。
> - 本文只做**方案设计**，不含代码改动；实施时按第 8 节分阶段落地。

---

## 1. 结论先行

1. **当前的事件链路是断的，而且是从数据到消费端全断**：平台回调只 `printf` 级别的信息（`WindowEvent` 连鼠标坐标、按键码、按下/抬起都没有），`Window::emit()` 在应用/节点层没有任何订阅者（唯一订阅者是 `tests/platform/test_window.cpp:57`，只 printf），`SceneTree::tick()` 里也没有输入阶段。现状不是「事件没接到节点」这一处漏接，而是**平台 → 归一化输入数据 → 分发入口 → 节点接口**四段全部缺失。
2. **`Node` 现在没有可以接收事件的接口**：没有 `_input` / `_gui_input` / `_unhandled_input`，没有 `set_process_input`，没有 `is_inside_tree()` / `get_viewport()`，没有焦点、命中、`handled` 语义。GUI 需要的「谁在上面、谁拿到键盘焦点、谁消费了这次点击」在节点系统里一个都没有。
3. **`WindowEvent` 是一个占位结构，必须换成一套归一化的 `InputEvent`**，并且键位/鼠标键/修饰符要定义引擎自己的枚举，由平台后端做映射（GLFW / SDL3 各一张映射表），否则 GUI 与游戏逻辑都要直接吃 GLFW 常量，平台抽象就白做了。
4. **推荐架构**：新增独立基础目标 `orca-input`（`core/input/`），提供 `InputEvent`、键位枚举、`InputMap`、`Input` 单例；平台后端把原生事件直接翻译成 `InputEvent` 推给 `Input`；`SceneTree` 每帧开头排空事件缓冲并向 **根 `Viewport`** 分发；`Viewport` 负责 GUI 命中/焦点与 `handled` 标志，未消费的事件继续走 `_input` / `_unhandled_input`，最终可用于 3D 拾取。
5. **必须先补齐一批节点系统缺陷**（`ready()` 从不调用、分发起点错误、`queued_free` 立即移除、`find_node()` 空实现、无 `ObjectID`/消息队列），否则事件分发会在节点增删、延迟删除、信号连接上立刻踩内存问题。核验时又发现 8 条会直接踩内存的既有缺陷（`Node` 无析构脱钩、传播循环全是 range-for、`tick()` 无条件渲染导致无法无窗口测试、根 `Viewport` 收不到 ENTER_TREE、`queued_free` 双发 EXIT_TREE、SDL3 未 `SDL_Init` 等），已并入 P0，见 §2.5 与第 8 节（P0 共 13 项）。
6. **GUI 可见还需要 2D 渲染，这不在事件系统内但必须并行规划**：`RenderingServer` 目前只有 3D 接口，没有 2D canvas / 矩形 / 图元 / 字体。事件系统可以先独立完成并用无窗口单测验证，但 GUI 真正能看之前必须补一层 `CanvasItem` + 2D 绘制（建议单独一份 `docs/gui_rendering_plan.md`）。

**推荐路径**：P0 节点系统修补 → P1 `InputEvent` 数据层 → P2 平台适配 → P3 节点分发 → P4 GUI 命中与焦点 → P5 信号与 3D 拾取 → P6（独立文档）2D 渲染。

---

## 2. 现状分析

### 2.1 当前事件链路全貌（已按代码核实）

```text
GLFW callbacks (glfw_window.cpp:57-85)
  key / mouse_button / cursor_pos / window_close / window_size / framebuffer_size
        │
        ▼
WindowEvent {type, keyCode, w, h}          ← 只有 4 个字段，keyCode 是空 struct
        │
        ▼
Window::emit(event)                        ← window.cpp:8-14，遍历 callbacks
        │
        ▼
（callbacks 只有注册接口，没有取消接口）
        │
        ▼
唯一的订阅者：tests/platform/test_window.cpp:57-61，只 printf
```

主循环侧（`application/application.cpp:50-65`）：

```text
Application::run()
  window->poll_event()        // 事件在这里产生，同步 emit，但无人消费
  scene_tree->tick(t)         // scene_tree.cpp:22-29：physics → process → render
  window->swap_buffers()
```

**结论**：事件在 `poll_event()` 里产生并立刻被丢弃；`tick()` 完全没有输入阶段；
`Input` 这种「当前按键状态/鼠标位置」的查询口也不存在。GUI 和游戏逻辑目前既拿不到事件，也拿不到状态。

### 2.2 逐层缺陷

#### 平台层（`platform/`）

| 位置 | 现状 | 后果 |
|---|---|---|
| `platform/window.h:35-37` | `struct KeyCode {}` 空实现 | 按键完全不可识别 |
| `platform/window.h:39-45` | `WindowEvent` 只有 `type/keyCode/w/h` | 没有按键码、按下/抬起、鼠标坐标、按钮、滚轮、修饰键、文本 |
| `platform/window.h:26-33` | `WindowEventType` 只有 Key/MouseButton/WindowSize/FramebufferSize/Close | 缺 MouseMotion / MouseWheel / TextInput / Focus |
| `platform/window.h:70-71` | 只有 `register_event_callback`，无退订、无句柄 | **潜在** UAF：一旦节点/控件注册窗口回调，析构后即悬空；GUI 高频增删下必现 |
| `platform/window.cpp:8-14` | `emit()` 里 `for(auto cb : callbacks)` 按值拷贝 `std::function` | 每事件一次拷贝；语义上也无法在遍历中安全退订 |
| `platform/glfw/glfw_window.cpp:182-194` | key/mouse 回调除 `event.type` 外不填任何载荷 | 事件信息量几乎为零 |
| `platform/glfw/glfw_window.cpp:196-201` | `cursor_pos_callaback`（拼写错误）发出的是 `WindowEventType::Key`，且丢弃 x/y | 鼠标移动被当成按键事件 |
| `platform/glfw/glfw_window.cpp:57-85` | 未注册 char / scroll / cursor_enter / window_focus 回调 | 文本输入、滚轮、鼠标进出、失焦全缺 |
| `platform/sdl3/sdl3_window.cpp:68-110` | `poll_event()` 只处理 Close/Resize/MouseButton/Key；无 MouseMotion/Wheel/Text/Focus | 与 GLFW 行为不一致 |
| `platform/glfw/glfw_window.cpp:119-127` | `get_size` 用 `glfwGetWindowSize`（逻辑坐标） | 两个后端坐标系语义不同 |
| `platform/sdl3/sdl3_window.cpp:112-120` | `get_size`/`get_frame_buffer_size` 都用 `SDL_GetWindowSizeInPixels`（物理像素） | 高 DPI 下 GUI 坐标会错位；且 SDL3 从不发 `FramebufferSize` |

#### 基础层（`core/`）

| 位置 | 现状 | 后果 |
|---|---|---|
| `core/object/object.h:3-10` | 只有 6 个通知，无输入相关 | 无法用通知机制分发事件 |
| `core/object/object.h:12-18` | 没有 `ObjectID` / 存活校验 / 信号 | 事件与信号连接无法安全持有「目标对象」 |
| `core/object/ref_counted.h:48-56` | `cast<U>()` 用 `static_cast`，无运行时检查 | 多态 `InputEvent` 基类方案在这里天然不安全 |
| `core/object/ref_counted.h:18-28` | `unref()` 用 `delete this`，而对象可能由 `memnew` 分配 | 每事件一个堆对象的方案会放大这个 UB（见 AGENTS 5.3 第 1 条） |
| `core/`（无目录） | 没有任何输入相关代码 | 没有 `InputEvent` / `InputMap` / `Input` |
| `core/math/projection.h` | 只有 `create_*` 与几个 `get_*`，无逆矩阵 | 无法做屏幕坐标 → 射线，3D 拾取做不了 |

#### 场景层（`scene/`）——这是「事件无法被节点消费」的核心

| 位置 | 现状 | 后果 |
|---|---|---|
| `scene/node.h` | 无 `_input` / `_gui_input` / `_unhandled_input`，无 `set_process_input` | 节点无处接收事件 |
| `scene/node.h` | 无 `is_inside_tree()`、`get_viewport()` | 分发时无法判断节点是否真的在树里、无法取窗口尺寸/焦点 |
| `scene/node.cpp:20-35` | `add_child` 只给**直接子节点**赋 `scene_tree`（29 行）、只在父已在树上时才发 ENTER_TREE，不递归、不校验 nullptr/自身/祖先环 | 后挂子树的后代 `scene_tree` 仍为 null（`is_inside_tree()` 误判）；误把自己加进自己会爆栈 |
| `scene/node.cpp:66-72` | `queued_free()` 立刻 `parent->remove_child(this)`；`parent` 为空直接崩溃；释放内存是 TODO | 在按钮自己的点击回调里删自己是必崩路径 |
| `scene/node.cpp:74-76` | `ready()` 是**死代码**，全仓库没有任何调用点（已核实） | GUI 控件无法在「子节点就绪后」建布局 |
| `scene/scene_tree.cpp:22-29` | `tick()` 顺序是 physics → process → render，没有输入阶段 | 事件没有入口 |
| `scene/scene_tree.cpp:31-45` | `process/physics_process` 的分发起点是 `current_scene` | GUI 通常挂在根 `Viewport` 下与主场景平级，**永远不会被遍历到** |
| `scene/scene_tree.cpp:112-119` | `destroy()` 空、`find_node()` 恒 `nullptr` | 无路径查找，焦点/测试/工具链无法定位节点 |
| `scene/viewport.h/.cpp` | 功能上只有空 `init` / 恒 `nullptr` 的 `find_world_3d` / `render`（另有未使用的 `RID rid` 与空的 ENTER/EXIT_TREE override） | 没有尺寸、没有 GUI 根、没有焦点、没有 `handled` 标志、没有输入入口 |
| `scene/3d/camera_3d.h:19` | `aspect = 800.0/600.0` 硬编码；`set_perspective` 由调用方传入 | Viewport 尺寸不参与相机，窗口 resize 后射线计算必错 |
| `scene/3d/camera_3d.cpp:12-26` | `get_viewport()` 在向上找父节点时 `dynamic_cast`，假定 `scene_tree` 非空 | 为 GUI 提供 `get_viewport()` 时不够健壮 |

#### 渲染层（`servers/rendering/`）

| 位置 | 现状 | 后果 |
|---|---|---|
| `servers/rendering/rendering_server.h` | 只有 scene/camera/light/mesh/material/texture 的 3D API | 没有 2D 图元/canvas/font，GUI 画不出来 |
| `rendering_server.cpp` | `sync()` 空实现，绘制只遍历 3D `instances` | GUI 无法插入绘制队列 |

### 2.3 「事件为什么消费不了」的五个根因

- **R1 事件数据不足**：平台只传事件类型，没有键码/坐标/按钮/文本，节点即使收到也无法做任何判断。
- **R2 没有归一化输入层**：缺失引擎自己的键位枚举与 `Input` 单例，游戏代码只能吃 GLFW 常量，且拿不到「当前是否按住」这种状态。
- **R3 没有分发入口与时序**：`tick()` 无输入阶段，事件产生于 `poll_event()` 且瞬时丢弃；即使补一个回调，也会在场景树状态不一致时触发（例如刚 enter tree 的节点就被轮询）。
- **R4 节点接口缺失**：`Node` 没有接收事件的虚函数与开关，没有 `Control` 的命中/焦点概念。
- **R5 消费语义缺失**：没有 `handled` 传播控制，GUI 点击后 3D 拾取仍会触发（或反之），无法表达「事件被上层吃掉」。

### 2.4 节点/场景树中必须一起修的前置缺陷

按对事件系统的阻塞程度排序：

1. `ready()` 从不调用 —— GUI 控件初始化（构子节点、测量尺寸）依赖它。
2. 分发起点是 `current_scene` —— 必须改为根 `Viewport`，否则挂 Viewport 下的 GUI 收不到任何事件。
3. `queued_free()` 立即移除、未释放内存，且会重复发 EXIT_TREE（`remove_child` 已经发过一次，`node.cpp:45-46` vs `69-70`）—— 事件回调内删节点是 GUI 高频路径，必须统一成延迟队列并改名 `queue_free()`。
4. 无 `is_inside_tree()` —— 分发时要在回调前后判活，避免节点在 `_input` 中被移除后继续被访问。
5. `find_node()` 只是恒 `nullptr` 的空桩（`scene_tree.h:16` 有声明、`scene_tree.cpp:116-119` 是空实现），且无子节点索引/顺序概念 —— GUI 命中顺序、焦点、层级都依赖子节点顺序。
6. 无 `ObjectID` / 消息队列 —— 信号连接与 `call_deferred` 的安全前提。
7. `Viewport` 无尺寸、相机 aspect 硬编码 —— 鼠标坐标 → GUI 坐标 → 世界射线的前提。
8. `Projection` 无逆变换、`Mesh` 无 AABB —— 3D 拾取前提。

### 2.5 另外八个会阻塞事件系统的既有缺陷（核验补充）

这八条不在最初的「节点系统缺陷」清单里，但任何一条不修，第 5 节的分发与判活方案都会踩到：

| 编号 | 缺陷 | 证据 | 对事件系统的影响 | 归属阶段 |
|---|---|---|---|---|
| A | `Node` 没有析构/脱钩逻辑：直接 `memdelete` 或栈对象析构后，仍留在 `parent->children` 里成为悬空指针 | `node.h` 无 `~Node`；`node.cpp:12/27` 只维护指针 | 高频增删的 GUI 控件必然踩悬空 | P0-10 |
| B | 现有传播循环全部是 range-for（`process` / `physics_process` / `enter_tree` / `exit_tree` / `Node3D` 变换传播） | `node.cpp:100-104`、`109-113`、`118-123`、`127-131`；`node3d.cpp:95-103` | 回调里增删子节点⇒迭代器失效；事件传播要复用同一套循环，必须先改 | P0-11 |
| C | `tick()` 无条件渲染：`Viewport::render()` → `RenderingServer::draw()` 直接解引用 `scene_data->env` / `main_camera` | `scene_tree.cpp:27`；`viewport.cpp:13-18`；`rendering_server.cpp:510-512`（无判空） | 没有窗口/渲染单例/World3D 时 `tick()` 必崩 ⇒ 分发逻辑**无法无窗口单测** | P0-12 |
| D | 根 `Viewport` 永远收不到 `NOTIFICATION_ENTER_TREE`：`SceneTree::init()` 只 `memnew` + `set_root()`，`set_root` 只赋指针 | `scene_tree.cpp:9-20`、`126-130`；`viewport.h:16-26` 是空 override | 若把 GUI 根/尺寸初始化写进根 Viewport 的 `enter_tree()`，会静默不执行 | P0-13 |
| E | `queued_free()` 双重退树通知（`remove_child` 已发 + 自己再发一次），且重复调用会因 `parent == nullptr` 崩溃 | `node.cpp:45-46` vs `69-70` | 在按钮自己的点击回调里删自己会触发两次 `exit_tree` | P0-10 |
| F | SDL3 后端**没有** `SDL_Init` / `SDL_Quit`，也不处理 `SDL_EVENT_QUIT` | `sdl3_window.cpp:7-47`、`49-61`、`68-110` | SDL3 下事件循环本身就不成立，P2 验收无法进行 | P2-6 |
| G | `add_child` 不校验 `nullptr` / 自己 / 祖先环 | `node.cpp:20-35` | 事件回调里误操作会递归爆栈而不是报错 | P0-10 |
| H | `tick()` 把 `root` 强转 `Viewport`，而 `set_root()` 是 public、只有 `init()` 保证类型 | `scene_tree.cpp:27`、`scene_tree.h:19` | 测试里替换/未初始化 root 会 UB；无窗口单测必须能构造 Viewport 根 | P0-13 |

> 结论：第 8 节的 P0 已据此扩到 13 项。**P0 不是「顺手修一下」，它是事件系统能否安全落地的前提**。

---

## 3. 设计目标与边界

**目标**

- 平台无关：同一套 `InputEvent` 在 GLFW / SDL3 下语义一致；新增后端只需写一张映射表。
- 时序确定：事件在一帧内的派发顺序固定，可测试。
- 可消费：GUI 吃掉的事件不再进入 3D，未消费的才进入游戏逻辑。
- 可交互：支持焦点、命中、hover、按键状态、动作映射（WASD/按键绑定）。
- 可测试：不依赖窗口和 GPU 就能单测分发逻辑。

**不做（本方案范围外）**

- IME / 输入法候选框；触摸/多点触控；手柄；剪贴板；拖放；多窗口；无障碍。
- GUI 布局系统（anchors/container/theme）与 2D 渲染（另开文档）。
- 编辑器序列化（信号按名字持久化）——本方案只做运行时类型安全的信号。

---

## 4. 总体架构

```text
                ┌────────────────────────── core/input（新目标 orca-input）────────────────────────┐
                │ input_event.h  InputEvent / InputEventType / Key / MouseButton / KeyModifierMask │
                │ input_map.h    InputMap（动作名 → 事件列表）                                      │
                │ input.h        Input 单例（状态 + 事件缓冲）                                      │
                └───────────────▲──────────────────────────────────────────▲─────────────────────┘
                                │ 产生 InputEvent                            │ 播放/查询
   ┌────────────────────────────┴───────────────┐          ┌─────────────────┴──────────────────┐
   │ platform（GLFW / SDL3 后端）                │          │ application（装配）                 │
   │ 原生回调 → 键位映射 → InputEvent            │          │ window 回调 → Input::parse_input_event│
   │ Window::register_input_callback             │          │ WindowEvent  → SceneTree::set_window_size│
   └─────────────────────────────────────────────┘          └─────────────────┬──────────────────┘
                                                                            │ tick()
   ┌──────────────────────────────────────────────────────────────────────────▼──────────────────┐
   │ scene                                                                                        │
   │ SceneTree::tick(): 排空输入缓冲 → root(Viewport)::push_input                                  │
   │   Viewport: GUI 命中/焦点/handled → _input 传播 → _shortcut_input → _unhandled_input          │
   │   Node: _input / _unhandled_input；Control: _gui_input / mouse_enter/exit / focus             │
   └──────────────────────────────────────────────────────────────────────────────────────────────┘
```

**依赖方向（新增）**

```text
orca-input  （只依赖 orca-math；不依赖 core/scene/rendering）
    ↑
orca-platform（新增 PUBLIC 链接 orca-input）   orca-scene（新增 PUBLIC 链接 orca-input）
    ↑                                              ↑
orca-application（装配：window 回调 → Input → SceneTree）
```

> 为什么把输入数据放进独立的目标而不是塞进 `orca-core`：`InputEvent` 需要 `Vector2`，
> 而 AGENTS 第 2.1 节规定 `orca-object / orca-math / orca-core` 是互不反向依赖的基础层。
> 新建 `orca-input` 只依赖 `orca-math`，可以保持这条铁律；`core/input/` 仍放在 `core/` 目录下，
> 头文件路径保持 `input/input_event.h` 风格，与现有 `math/`、`os/`、`io/` 一致。

---

## 5. 数据结构与 API 设计

### 5.1 InputEvent：扁平标签化结构（推荐）

```cpp
// core/input/input_event.h
#pragma once
#include "math/vector2.h"
#include "input/keyboard.h"   // Key / KeyModifierMask
#include "input/mouse_button.h" // MouseButton
#include <cstdint>

enum class InputEventType : uint8_t
{
    None = 0,
    Key,            // 键盘按下/抬起
    MouseButton,    // 鼠标键按下/抬起
    MouseMotion,    // 鼠标移动（绝对位置 + 相对位移）
    MouseWheel,     // 滚轮
    TextInput,      // Unicode 文本（GUI 输入框）
    WindowFocus,    // 窗口获得/失去焦点
};

struct InputEvent
{
    InputEventType type = InputEventType::None;

    // ---- 通用 ----
    bool pressed = false;          // 按下为 true，抬起为 false
    bool echo = false;             // 键盘自动重复
    int modifiers = 0;             // KeyModifierMask 位组合

    // ---- Key ----
    Key key = Key::NONE;

    // ---- MouseButton / MouseWheel ----
    MouseButton button = MouseButton::NONE;
    int wheel_delta = 0;           // 滚轮格数，正负表示方向
    int button_mask = 0;           // 当前按住的鼠标键位掩码

    // ---- 位置（窗口坐标，左上角原点，y 向下，单位：逻辑像素）----
    Vector2 position = Vector2::ZERO;
    Vector2 relative = Vector2::ZERO;   // 相对上一帧的位移（捕获鼠标时使用）
    Vector2 scroll = Vector2::ZERO;     // 双轴滚动（触摸板）

    // ---- TextInput ----
    char32_t unicode = 0;

    // ---- WindowFocus ----
    bool has_focus = true;

    InputEventType get_type() const { return type; }
};
```

**为什么不做 `class InputEvent` 多态层级（Godot 风格）**

| 方案 | 优点 | 缺点（在本仓库的代价） |
|---|---|---|
| 多态 `Ref<InputEvent>` 子类 | 扩展干净、类型判断清晰 | `Ref::cast()` 是 `static_cast` 无校验；`RefCounted::unref()` 用 `delete this` 与 `memnew` 不匹配（AGENTS 5.3 #1）；每个事件一次堆分配。**这套地基现在不安全** |
| 扁平标签化 struct | 零分配（进 `std::vector` 缓冲）、可拷贝、可跨模块传值、无堆生命周期问题 | 字段冗余；新增事件类型要加字段 |

本方案选**扁平结构**，并在 `InputEvent` 上加一个 `get_type()` 便于将来若引入多态时平滑迁移。
如果未来一定要多态，先修 `ObjectID` + `Ref` + 分配器一致性（P0 之后）。

### 5.2 键位与鼠标枚举

```cpp
// core/input/keyboard.h
enum class Key : uint16_t
{
    NONE = 0,
    // 字母/数字
    A, B, C, ... Z,
    NUM_0, NUM_1, ... NUM_9,
    // 控制
    ESCAPE, ENTER, TAB, BACKSPACE, DELETE, SPACE,
    LEFT, RIGHT, UP, DOWN, HOME, END, PAGE_UP, PAGE_DOWN, INSERT,
    // 修饰
    SHIFT, CTRL, ALT, SUPER, CAPS_LOCK,
    // 功能键
    F1, ... F12,
    MAX
};

enum KeyModifierMask : int
{
    KEY_MOD_NONE  = 0,
    KEY_MOD_SHIFT = 1 << 0,
    KEY_MOD_CTRL  = 1 << 1,
    KEY_MOD_ALT   = 1 << 2,
    KEY_MOD_SUPER = 1 << 3,
};

// core/input/mouse_button.h
enum class MouseButton : uint8_t
{
    NONE = 0, LEFT, RIGHT, MIDDLE, XBUTTON1, XBUTTON2,
    WHEEL_UP, WHEEL_DOWN, WHEEL_LEFT, WHEEL_RIGHT,
};
```

映射表放在各后端内部（`platform/glfw/glfw_keymap.cpp` 或直接静态函数），
**不要**让 GLFW/SDL 常量泄漏到 `scene` 层。未知键统一映射为 `Key::NONE` 并丢弃。

### 5.3 Input 单例

```cpp
// core/input/input.h
class Input
{
    static Input *singleton;
public:
    static void create_singleton();
    static void destroy_singleton();
    static Input *get_singleton() { return singleton; }

    // 唯一入口：由 Application 注册给 Window 的 input 回调调用。
    // 平台后端只负责 emit InputEvent，不直接持有 Input，避免 platform 反向驱动 scene。
    // 作用：更新内部状态 + 入缓冲，**不派发**。
    void parse_input_event(const InputEvent &event);

    // SceneTree 每帧开头调用一次：取走本帧事件（取走即清空）。
    // 全流程只保留这一个取事件接口，不要同时暴露 get_buffered_events()/flush 两套名字。
    std::vector<InputEvent> take_buffered_events();

    // ---- 状态查询（对游戏逻辑/GUI）----
    bool is_key_pressed(Key key) const;
    bool is_mouse_button_pressed(MouseButton button) const;
    Vector2 get_mouse_position() const { return mouse_position; }
    Vector2 get_last_mouse_velocity() const { return mouse_velocity; }
    void set_mouse_position(const Vector2 &pos) { mouse_position = pos; }

    // ---- 动作（InputMap）----
    bool is_action_pressed(const char *action, bool exact_match = false) const;
    bool is_action_just_pressed(const char *action) const;
    bool is_action_just_released(const char *action) const;
    void action_press(const char *action, float strength = 1.0f);   // 模拟输入，测试用
    void action_release(const char *action);

    // ---- 鼠标模式（鼠标锁定 / 隐藏，做第一人称相机时必需）----
    enum MouseMode { MOUSE_MODE_VISIBLE, MOUSE_MODE_HIDDEN, MOUSE_MODE_CAPTURED };
    void set_mouse_mode(MouseMode mode);

    // 每帧末尾由 SceneTree 调用，用于 just_pressed/just_released 的帧对齐
    void end_frame();
private:
    std::vector<InputEvent> buffered_events;
    std::vector<bool> key_pressed;        // 以 Key 为下标
    std::vector<bool> mouse_button_pressed;
    Vector2 mouse_position;
    Vector2 mouse_velocity;
    uint64_t frame = 0;
    // just_pressed 依赖「按下时的 frame 号」
    std::unordered_map<std::string, uint64_t> action_pressed_frame;
    ...
};
```

要点：

- `parse_input_event` 必须**先更新状态再入缓冲**：这样 `is_key_pressed()` 在事件产生的同一帧内即为正确值。
- 窗口失焦（`InputEventType::WindowFocus` 且 `has_focus == false`）时清空按键/鼠标状态，避免 alt-tab 后按键卡住。
- `set_mouse_mode()` 需要平台层新增虚函数 `Window::set_mouse_mode(...)`（GLFW: `glfwSetInputMode` + `glfwSetCursorPos`；SDL3: `SDL_SetWindowRelativeMouseMode`）。捕获模式下后端应发绝对+相对位移，或直接发 `relative`。

### 5.4 InputMap

```cpp
// core/input/input_map.h
class InputMap
{
public:
    static void create_singleton();
    static InputMap *get_singleton();

    void add_action(const char *action);
    void erase_action(const char *action);
    bool has_action(const char *action) const;
    // 绑定：键 or 鼠标键，可带修饰键要求
    void action_add_key(const char *action, Key key);
    void action_add_mouse_button(const char *action, MouseButton button);

    // 绑定表的文件解析**不放在这里**：orca-input 只依赖 orca-math，
    // 一旦在 InputMap 里实现 load_from_file 就会被迫引入 core/io。
    // 解析放到更高层（application 装配，或新增 core/input/input_map_loader.* 并显式给
    // orca-input 增加 orca-core 依赖），届时再定。
};
```

动作判定顺序沿用 Godot 语义：`is_action_pressed` 只要有一项绑定处于按下状态即为真；
`exact_match` 控制修饰键是否必须完全一致。**动作状态由 `Input` 在 `parse_input_event` 里增量维护**，
`InputMap` 只存绑定表，二者不循环依赖。

### 5.5 Node 扩展

```cpp
// scene/node.h （新增部分）
class Node : public Object
{
    // ...
    bool process_input = true;            // 与现有 process_on 默认值风格一致
    bool process_unhandled_input = true;
public:
    // 不能直接用 `scene_tree != nullptr` 判断：现有 add_child/remove_child 只维护直接子节点
    // （node.cpp:29/43/60），后挂子树的后代为 null、移除后后代仍保留旧指针。
    // P0-9 必须先把 scene_tree 改为**递归维护**（或让本函数沿 parent 链上溯），再实现本函数。
    bool is_inside_tree() const;
    Viewport *get_viewport() const;       // 向上找最近的 Viewport，找不到返回 nullptr

    void set_process_input(bool on) { process_input = on; }
    void set_process_unhandled_input(bool on) { process_unhandled_input = on; }

    virtual void ready();                                   // P0 让它在进树后真正被调用（子先父后）
    virtual void _input(const InputEvent &event);           // 没有被 GUI 消费的事件
    virtual void _unhandled_input(const InputEvent &event); // _input 也没消费的事件
    // 延迟删除（P0）。注意现有 API 叫 queued_free()（node.h:24），P0-10 统一改名 queue_free()
    // 并删除旧名，不要两套并存。
    virtual void queue_free();

    // 传播（内部使用；顺序见第 6 节）
    void _propagate_input(const InputEvent &event);
    void _propagate_unhandled_input(const InputEvent &event);
    void _propagate_ready();
};
```

- **遍历安全（现有代码必须先改）**：现存 `_propagate_process` / `_propagate_physics_process` /
  `_propagate_enter_tree` / `_propagate_exit_tree` 以及 `Node3D` 的变换传播**全部是 range-for**
  （`node.cpp:100-104`、`109-113`、`118-123`、`127-131`；`node3d.cpp:95-103`），
  任何回调里增删子节点都会立刻令迭代器失效。P0-11 必须先把它们统一改成
  「下标遍历 + 每次回调后校验 `children[i]->get_parent() == this`」，再在此基础上做事件传播；
  **不要**持有迭代器、引用或 `children` 的拷贝指针。
- `ready()` 的调用顺序定为**子先父后**（与 Godot 一致，GUI 布局依赖此语义）。
  `NOTIFICATION_ENTER_TREE` 是父先子后，因此在 `add_child` 递归结束后，对「本次新进树的子树」
  再做一次自底向上的 `_propagate_ready()`；不要每帧全树扫，也不要对已 ready 的子树重复调用。

### 5.6 SceneTree 扩展

```cpp
class SceneTree : public Object
{
public:
    bool tick(double time);
    // ...
    Viewport *get_viewport() const;      // root 即 Viewport
    void set_window_size(int w, int h);  // 由 application 在窗口事件/初始化时写入
    void queue_delete(Node *node);       // 延迟删除队列，tick 末尾统一处理

    // 新增通知
    // NOTIFICATION_INPUT 不需要：分发在 tick 内同步完成
private:
    bool input_stage();
    void flush_delete_queue();
    Input *input = nullptr;              // 非拥有指针，指向 Input 单例
};
```

`tick()` 的新顺序：

```text
flush_delete_queue()          // 上一帧遗留的删除
input_stage()                 // 排空 Input 缓冲，逐事件 push_input
flush_delete_queue()          // 输入回调里 queue_free() 的节点必须在这里立刻摘除
physics_process()             // 固定步长（可多步）
process()                     // 可变 delta
flush_delete_queue()          // 逻辑回调里产生的删除
render()
```

> 顺序理由：输入先于物理/逻辑，保证「本帧按下的键」在 `physics_process` 里已可见（与主流引擎一致）。
> **必须**在 `input_stage()` 之后立刻 flush 一次：否则在按钮自己的点击回调里 `queue_free()`
> 的节点仍会进入同帧的 `physics_process()` / `process()`，等于「删了还继续被回调」。

### 5.7 Viewport：分发、焦点、命中

```cpp
// scene/viewport.h
// 必须前向声明：camera_3d.h / control.h 已经 #include "viewport.h"，反向 include 会成环。
class Camera3D;
class Control;

class Viewport : public Node
{
public:
    void init();
    World3D *find_world_3d() const;                  // P0 修好（沿子树找 World3D）
    void render() const;

    // ---- 输入 ----
    void push_input(const InputEvent &event);        // SceneTree 的唯一入口
    void set_input_as_handled() { input_handled = true; }
    bool is_input_handled() const { return input_handled; }

    // ---- 尺寸 ----
    void set_size(const Vector2 &size) { this->size = size; }
    Vector2 get_size() const { return size; }

    // ---- GUI 焦点 ----
    Control *get_gui_focus_owner() const { return gui_focus_owner; }
    void gui_grab_focus(Control *control);
    void gui_release_focus();
    Control *get_control_at_position(const Vector2 &pos) const;  // 反向深度优先命中

    // ---- 2D → 3D ----
    Camera3D *get_camera_3d() const;
    bool get_screen_to_world_ray(const Vector2 &screen_pos,
                                 Vector3 &out_origin, Vector3 &out_dir) const;
private:
    enum
    {
        // 分发阶段
        INPUT_STAGE_GUI,
        INPUT_STAGE_INPUT,
        INPUT_STAGE_UNHANDLED,
    };
    void _gui_dispatch(const InputEvent &event);
    void _call_gui_input(const InputEvent &event, Control *control);

    bool input_handled = false;
    Vector2 size = {800, 600};
    Control *gui_focus_owner = nullptr;
    Control *gui_mouse_over = nullptr;
    Control *gui_mouse_focus = nullptr;
};
```

`push_input` 的伪代码：

```cpp
void Viewport::push_input(const InputEvent &event)
{
    input_handled = false;

    // 1. GUI 优先：焦点控件 → 命中控件
    _gui_dispatch(event);

    // 2. 未被 GUI 消费，进入常规 _input
    if (!input_handled)
    {
        _input(event);
        if (!input_handled) _propagate_input(event);
    }

    // 3. 仍未消费，进入 _unhandled_input（游戏逻辑 / 3D 拾取）
    if (!input_handled)
    {
        _unhandled_input(event);
        if (!input_handled) _propagate_unhandled_input(event);
    }
}
```

`_gui_dispatch` 的规则（按事件类型分支）：

| 事件 | 处理 |
|---|---|
| `MouseMotion` | 命中测试取最上层 `Control`；与 `gui_mouse_over` 不同时发 `mouse_exit` / `mouse_enter`；给命中控件（及其 `MOUSE_FILTER_PASS` 祖先链）发 `_gui_input` |
| `MouseButton` 按下 | 命中控件若 `focus_mode != FOCUS_NONE` → `gui_grab_focus(命中控件)`；否则 `gui_release_focus()`；设置 `gui_mouse_focus` 捕获后续按下/抬起；发 `_gui_input` |
| `MouseButton` 抬起 | 发给 `gui_mouse_focus`（若还存活），清空捕获；发 `_gui_input` |
| `MouseWheel` | 发给命中控件 |
| `Key` | 只发给 `gui_focus_owner`（沿其控件父链向上，直到有人 `accept_event()`） |
| `TextInput` | 只发给 `gui_focus_owner` |
| `WindowFocus`（失焦） | 清空 `gui_mouse_focus` / `gui_mouse_over`，焦点保留 |

`Control::accept_event()` 的实现是 `if (Viewport *vp = get_viewport()) vp->set_input_as_handled();`
（必须判空：脱离树的控件调用它是合法但无意义的操作，不能崩）。
焦点不是焦点控件的事件在进入 `_input` 阶段前 `input_handled` 仍为 false，所以不会误吞游戏输入。

### 5.8 Control：命中、焦点、鼠标过滤

```cpp
// scene/gui/control.h
class Control : public Node
{
public:
    enum MouseFilter
    {
        MOUSE_FILTER_STOP,     // 命中即消费（按钮、面板）
        MOUSE_FILTER_PASS,     // 命中但不消费，继续给下层/祖先（标签）
        MOUSE_FILTER_IGNORE,   // 不参与命中（纯装饰）
    };
    enum FocusMode
    {
        FOCUS_NONE,            // 不可获得焦点
        FOCUS_CLICK,           // 点击时获得焦点（输入框）
        FOCUS_ALL,             // 点击 + Tab 遍历
    };
private:
    Vector2 position = Vector2::ZERO;
    Vector2 size = Vector2::ZERO;
    MouseFilter mouse_filter = MOUSE_FILTER_STOP;
    FocusMode focus_mode = FOCUS_NONE;
    bool visible = true;
public:
    Vector2 get_global_position() const;      // 累加父 Control 的 position
    Rect2 get_global_rect() const;
    virtual bool has_point(const Vector2 &point) const;   // 默认矩形测试，可覆写为圆形等

    virtual void _gui_input(const InputEvent &event);
    virtual void mouse_enter();
    virtual void mouse_exit();

    void accept_event();
    void grab_focus();
    void release_focus();
    bool has_focus() const;

    void set_position(const Vector2 &p);
    void set_size(const Vector2 &s);
    void set_mouse_filter(MouseFilter f);
    void set_focus_mode(FocusMode m);
    void set_visible(bool v);
};
```

`Control` 命中测试只依赖 `position/size/visible/mouse_filter`，**不依赖渲染**，
因此 P4 可以先于 2D 渲染完成并用纯单测验证。

> GUI 顺序约定：`children` 数组顺序即绘制顺序（后者在上），命中测试**逆序**遍历。
> 这与第 6 节 `_input` 的逆序传播一致（后添加的兄弟优先）。

### 5.9 信号与 ObjectID（运行时可用的最小实现）

GUI 控件必须能「通知外部」，否则交互只能靠继承覆写。建议最小实现：

```cpp
// core/object/object_id.h
struct ObjectID
{
    uint64_t id = 0;
    bool is_valid() const;           // 通过 ObjectDB 查存活
    static ObjectID from_object(Object *obj);
};

// core/object/object.h（新增）
class Object
{
    const uint64_t instance_id;
public:
    Object();
    uint64_t get_instance_id() const { return instance_id; }
    ObjectID get_instance_id_as_object_id() const;
    static Object *get_object_by_id(ObjectID id);
};
```

`Object` 构造时从全局原子计数器取 id 并登记到 `ObjectDB`（`std::unordered_set<uint64_t>`），
析构时注销。这是信号自动断开、`call_deferred` 目标存活校验、GUI 焦点指针存活校验的统一基础。

```cpp
// core/object/signal.h
template <typename... Args>
class Signal
{
public:
    // owner 必须是**回调 lambda 实际捕获的宿主对象**的 ObjectID（通常就是调用 connect 的 this）。
    // emit 前统一剔除已析构的 owner，从而既不悬空也不泄漏。
    // 反例：传「另一个对象」的 id，断开时机与回调宿主不一致，宿主先死仍会悬空。
    void connect(ObjectID owner, std::function<void(Args...)> callback);
    void disconnect(ObjectID owner);
    void disconnect_all();
    void emit(Args... args) const;
private:
    std::vector<Slot> slots;   // { ObjectID owner; std::function<void(Args...)> callback; }
};
```

用法（`Button`）：

```cpp
class Button : public Control
{
    Signal<> pressed;
public:
    void _gui_input(const InputEvent &event) override
    {
        if (event.type == InputEventType::MouseButton
            && event.pressed && event.button == MouseButton::LEFT)
        {
            accept_event();
            pressed.emit();
        }
    }
};

// 调用方：owner 传「lambda 的宿主 this」，不是别的对象
button->pressed.connect(get_instance_id_as_object_id(),
                        [this]() { this->do_something(); });
```

**关键点**：连接以 `ObjectID` 为键，`emit` 时先剔除失效目标，避免「控件先析构、连接方还持有回调」的悬空。
按名字 `connect("pressed", ...)` 这种字符串方案留给未来的编辑器/序列化，本期不做。

### 5.10 3D 拾取（「用输入操作物体」）

事件系统打通后，3D 交互只是消费端的应用：

```cpp
// scene/3d/camera_3d.cpp（新增）
bool Camera3D::project_ray_origin(const Vector2 &screen_pos, Vector3 &out) const;
bool Camera3D::project_ray_normal(const Vector2 &screen_pos, Vector3 &out) const;
// 依赖新增 Projection::inverse()，且 aspect 应来自 Viewport 尺寸而非构造参数

// scene/3d/mesh_instance_3d.h（新增）
bool intersect_ray(const Vector3 &from, const Vector3 &dir, real_t &out_distance) const;
// 用 Mesh 的 AABB（需给 Mesh 增加 get_aabb()）→ 转世界空间 OBB → 射线求交
```

用户侧典型写法：

```cpp
void Player::_unhandled_input(const InputEvent &event)
{
    if (event.type == InputEventType::MouseButton
        && event.pressed && event.button == MouseButton::LEFT)
    {
        Vector3 from, dir;
        get_viewport()->get_screen_to_world_ray(event.position, from, dir);
        // 由场景树/自定义管理器做射线检测
    }
}
```

> 没有物理引擎，第一期用 **AABB/OBB 暴力遍历** 即可；三角形级精确求交后置。
> 注意：GUI 点击会先被 `_gui_dispatch` 消费，因此「点按钮时不穿透到 3D」是自动成立的。

### 5.11 CMake 与文件清单

```cmake
# core/CMakeLists.txt（新增目标；include dir 与 orca-core 相同，都是 core/，因此头文件路径仍是 input/xxx.h）
add_library(orca-input STATIC
    input/input.cpp
    input/input_map.cpp
)
target_include_directories(orca-input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(orca-input PUBLIC orca-math)

# platform/CMakeLists.txt：后端要产出 InputEvent，必须能链接到 Input 符号
target_link_libraries(orca-platform PUBLIC orca-rendering orca-input)

# scene/CMakeLists.txt：viewport.h / control.h 的公共头会暴露 InputEvent，必须 PUBLIC 传递
target_link_libraries(orca-scene PUBLIC orca-input)
target_link_libraries(orca-scene PRIVATE orca-object orca-rendering orca-resource orca-core)

# application/CMakeLists.txt：装配 Input 单例，显式加上
target_link_libraries(orca-application PUBLIC ... orca-input)

# tests/CMakeLists.txt：orca-tests 增加 orca-input（input / input_map 单测）
```

> 说明：`orca-platform` 目前 PUBLIC 链接 `orca-rendering`，而 `orca-rendering` PUBLIC 链接 `orca-core`，
> 所以 `core/` 这个 include 根本来就会传递给平台层；新增 `orca-input` 主要是给「产出 InputEvent 的后端」
> 和「消费 Input 的场景层」一个明确的符号来源，头文件路径风格与现有 `math/`、`os/` 一致。
> `orca-scene` 用 **PUBLIC** 的理由是 usage requirements：静态库的 PRIVATE 依赖只以 `LINK_ONLY`
> 形式传给最终可执行文件、**不会**把 include dir 透给下游，而 `viewport.h` / `control.h` 会把
> `InputEvent` 暴露给 `application`。顺手把 scene 的公共依赖理清，避免「靠调用方自己补链」的隐性约定。

新增/修改文件总览：

| 模块 | 文件 | 类型 |
|---|---|---|
| core/input | `input_event.h`、`keyboard.h`、`mouse_button.h`、`input.h/.cpp`、`input_map.h/.cpp` | 新增 |
| core/object | `object_id.h/.cpp`、`signal.h`、`message_queue.h/.cpp`；`object.h` 加 id 与通知 | 新增 + 修改 |
| core/math | `projection.cpp` 加 `inverse()` | 修改 |
| platform | `window.h/.cpp`（input 回调、mouse mode）、`glfw/glfw_window.*`、`sdl3/sdl3_window.*`、键位映射 | 修改 |
| scene | `node.h/.cpp`、`scene_tree.h/.cpp`、`viewport.h/.cpp` | 修改 |
| scene/gui | `control.h/.cpp`、`button.h/.cpp`（后续 label/panel/input 等） | 新增 |
| scene/3d | `camera_3d.*`（射线）、`mesh_instance_3d.*`（拾取）、`mesh` 加 AABB | 修改 |
| application | `application.cpp` 装配两个回调 + 每帧尺寸同步 | 修改 |
| tests | `core/input/test_input.cpp`、`scene/test_input_dispatch.cpp`、`scene/gui/test_control_hit.cpp` | 新增（必须加进 `tests/CMakeLists.txt`） |

---

## 6. 分发时序

### 6.1 一帧内的时序

```text
Application::run()
  window->poll_event()
      └─ 后端回调 → InputEvent → Window::register_input_callback → Input::parse_input_event(e)
                            （更新状态 + 入缓冲，不派发）
  Application 同步窗口尺寸 → SceneTree::set_window_size / Viewport::set_size
  SceneTree::tick(t)
      flush_delete_queue()
      for (e : Input::take_buffered_events())      // 本帧所有事件按产生顺序
          Viewport::push_input(e)
      physics_process()  ×  0..8 固定步
      process()
      flush_delete_queue()
      Viewport::render()
  window->swap_buffers()
```

### 6.2 单个事件内的派发顺序（固定）

```text
1. GUI 阶段（Viewport::_gui_dispatch）
   1.1 失焦/焦点事件 → 焦点控件
   1.2 键盘/文本 → 焦点控件（沿控件父链向上）
   1.3 鼠标移动 → 命中控件（mouse_enter/exit + _gui_input）
   1.4 鼠标键 → 命中控件（必要时转移焦点）
   1.5 MouseFilter::PASS 时不设置 handled，继续向外层祖先控件冒泡
2. _input 阶段（根 Viewport → 子节点，逆序）
3. _unhandled_input 阶段（同一遍历顺序）
4. 未被消费 → 丢弃（进入下一事件）
```

**逆序理由**：子节点数组顺序 = 添加顺序 = 绘制顺序（后者在上），命中与事件优先级都应是「上层先」。
`_unhandled_input` 采用同样的逆序，保证上层节点先有机会处理。

**可重入保护**：`Viewport::push_input` 期间若节点调用了会被 `queue_free()` 的节点，
其标记在 `flush_delete_queue()` 统一处理；分发中任何回调后都要校验 `is_inside_tree()`，
失效则终止该子树遍历。

---

## 7. 关键决策与备选方案

| 编号 | 决策 | 备选与放弃理由 |
|---|---|---|
| D1 | `InputEvent` 用扁平 struct | 多态 `Ref<InputEvent>`：受 `Ref::cast` 与 `delete this`/`memnew` 不一致拖累（AGENTS 5.3 #1），且每事件堆分配 |
| D2 | 事件先入缓冲、在 `tick()` 开头统一派发 | 在 `poll_event()` 里立即派发：会在场景树状态未同步时触发回调（例如窗口刚创建、节点刚进树），且无法无窗口单测 |
| D3 | 分发从**根 Viewport** 开始，而不是 `current_scene` | 保持 `current_scene` 为起点：GUI 通常挂在 Viewport 下，会永远收不到事件（现有 `process()` 就是这个缺陷） |
| D4 | `handled` 标志放在 `Viewport`（`set_input_as_handled`），不放 `InputEvent` | 放事件里需要非 const 传递或在结构里加可变位，污染数据；放 Viewport 与 Godot 一致，且天然按事件重置 |
| D5 | 坐标统一用**逻辑窗口坐标**（左上原点、y 向下） | 用物理帧缓冲坐标：高 DPI 下 GUI 缩放不可控；用物理坐标还需额外除 DPI 比例。渲染仍用 framebuffer 尺寸算 aspect |
| D6 | 平台层直接产出 `InputEvent`，不再扩展 `WindowEvent` 表达输入 | 扩展 `WindowEvent` 后再在别处翻译：翻译层没有位置放（`core` 不能依赖 `platform`），必然要么循环依赖要么重复实现两遍 |
| D7 | 信号用 `Signal<Args...>` + `ObjectID` 自动断开；不做字符串连接 | 字符串 + `Variant` 连接：本仓库无 `Variant`/`ClassDB`，为编辑器能力提前付出过大成本 |
| D8 | `WindowEvent` 保留（窗口生命周期），新增 `register_input_callback` | 合并成一个事件流：`WindowEvent` 已被 `test_window.cpp` 使用，且窗口事件与输入事件语义不同，合并会破坏现有代码 |
| D9 | 删除、信号、延迟调用依赖 P0 的 `ObjectID` + 消息队列 | 直接裸指针连接：GUI 控件高频增删，必然悬空 |

---

## 8. 分阶段实施计划

每个阶段都可独立编译、独立测试；P1/P2 之间、P5 的 3D 拾取与 P4 之间可以并行。

### P0 节点系统地基修复（前置，无窗口可测）

| 项 | 内容 | 文件 |
|---|---|---|
| P0-1 | `ready()` 真正被调用：进树后对新增子树调 `_propagate_ready()` | `scene/node.h/.cpp` |
| P0-2 | `is_inside_tree()`、`get_viewport()`、`get_index()` | `scene/node.h/.cpp` |
| P0-3 | `queue_free()` + `SceneTree::queue_delete()` + 帧末 flush；修 `parent == nullptr` | `scene/node.*`、`scene/scene_tree.*` |
| P0-4 | `SceneTree::find_node()` 实现（`/` 分隔路径） | `scene/scene_tree.cpp` |
| P0-5 | `SceneTree::destroy()` 实现（递归释放并清空树） | `scene/scene_tree.cpp` |
| P0-6 | `Object` 加 `instance_id` + `ObjectDB` 存活查询 | `core/object/object.h/.cpp`、`object_id.h/.cpp` |
| P0-7 | `MessageQueue`（`std::function` 队列）+ `Object::call_deferred` | `core/object/message_queue.h/.cpp` |
| P0-8 | `Viewport::find_world_3d()` 实现；`Viewport` 持有 `size` | `scene/viewport.*` |
| P0-9 | `scene_tree` **递归维护**（进/出树时整棵子树同步），`is_inside_tree()` 才可用 | `scene/node.cpp` |
| P0-10 | `~Node` 统一脱钩（从 `parent->children` 摘除）；`add_child` 加 nullptr/自身/祖先环校验；`queued_free` 改名为 `queue_free()` 并去掉重复的 EXIT_TREE | `scene/node.h/.cpp` |
| P0-11 | 所有传播循环 range-for → 下标 + 回调后校验（`node.cpp:100-131`、`node3d.cpp:95-103`） | `scene/node.cpp`、`scene/3d/node3d.cpp` |
| P0-12 | `tick()` 与渲染解耦：加 `set_render_enabled(bool)`，无渲染单例时跳过 `render()`；`draw()` 对 `scene_data/env/main_camera` 判空 | `scene/scene_tree.*`、`scene/viewport.*`、`servers/rendering/rendering_server.cpp` |
| P0-13 | 根 Viewport 补发 ENTER_TREE；`set_root()` 校验 `dynamic_cast<Viewport*>` | `scene/scene_tree.cpp` |

**验收**：`orca-tests` 新增 `test_node_lifecycle.cpp`（`ready()` 子先父后且只调一次、
`queue_free()` 延迟生效且只发一次 `exit_tree`、`find_node()` 路径命中、`destroy()` 后无泄漏、
子树移除后整棵子树 `is_inside_tree()` 均为 false），全部无窗口运行；`Memory::get_allocation_count()` 归零。

> 注意：P0-12 完成前，「无窗口测 `tick()`」不可行——`tick()` 会无条件走到
> `Viewport::render()` → `RenderingServer::draw()`，而 `draw()` 直接解引用
> `scene_data->env` / `scene_data->main_camera`（`rendering_server.cpp:510-512`）。
> 因此 P0-12 必须先于任何 tick 时序测试落地。

### P1 InputEvent 数据层

| 项 | 内容 |
|---|---|
| P1-1 | `core/input/{input_event.h, keyboard.h, mouse_button.h}` |
| P1-2 | `Input` 单例：状态表 + 事件缓冲 + `parse_input_event` |
| P1-3 | `InputMap`：动作注册与绑定 |
| P1-4 | `core/CMakeLists.txt` 新增 `orca-input` |
| P1-5 | 单测 `tests/core/input/test_input.cpp`：按键状态、修饰键、鼠标位置、动作 just_pressed 的帧语义、失焦清空状态 |

**验收**：不建窗口、不初始化渲染即可运行；覆盖 `is_action_pressed` 的 `exact_match` 行为。

### P2 平台适配（GLFW + SDL3）

| 项 | 内容 |
|---|---|
| P2-1 | `Window` 基类：`register_input_callback` / `unregister`、`set_mouse_mode`、`set_cursor_shape`（后置） |
| P2-2 | GLFW：补齐 char / scroll / cursor_enter / window_focus 回调；修正 `cursor_pos_callaback` 的类型与坐标；key/mouse 回调填满字段 |
| P2-3 | SDL3：补 MouseMotion / Wheel / TextInput / Focus；统一 `get_size` 与 `get_frame_buffer_size` 的语义 |
| P2-4 | 两个后端各一张「原生键码 → `Key`」映射表，未知键丢弃 |
| P2-5 | 事件回调的退订接口（返回 handle 或 id），消除悬空回调 |
| P2-6 | SDL3 后端补 `SDL_Init(SDL_INIT_VIDEO)` / `SDL_Quit` 并处理 `SDL_EVENT_QUIT`（现在完全没有，见 §2.5 F） |

**验收**：`tests/platform/test_window.cpp` 改成打印归一化事件（键名、坐标、按钮），
手动运行确认鼠标移动/按键/滚轮/文本/失焦都正确；`FramebufferSize` 在 SDL3 下也能发出。

### P3 节点分发

| 项 | 内容 |
|---|---|
| P3-1 | `Node::_input` / `_unhandled_input` / `_propagate_*`（逆序、遍历安全） |
| P3-2 | `SceneTree::input_stage()` + 新 `tick()` 顺序 |
| P3-3 | `Viewport::push_input` + `input_handled` |
| P3-4 | `Application` 装配：窗口 input 回调 → `Input::parse_input_event`；`Input::create_singleton` |
| P3-5 | 单测 `tests/scene/test_input_dispatch.cpp`：直接构造 `InputEvent` 推进 `push_input`，断言回调顺序、`set_input_as_handled` 截止传播、回调内增删子节点不崩 |

**验收**：单测全绿；`test_application` 里用 `_input` 让相机 WASD 平移（不依赖 GUI）验证真实链路。

### P4 GUI 命中与焦点

| 项 | 内容 |
|---|---|
| P4-1 | `Control` 基类：rect、`has_point`、`_gui_input`、`mouse_enter/exit`、`accept_event` |
| P4-2 | `Viewport::_gui_dispatch`：命中测试、hover、焦点转移、键盘/文本到焦点控件 |
| P4-3 | `Viewport::get_control_at_position`（逆序深度优先） |
| P4-4 | `Button` 最小控件（含 `pressed` 信号） |
| P4-5 | 单测 `tests/scene/test_control_hit.cpp`：嵌套控件命中优先级、`MOUSE_FILTER_PASS` 冒泡、点击转移焦点、点击空白释放焦点 |

**验收**：单测全绿（无窗口）；`test_application` 里挂一个不可见的按钮层，点击日志正确。

### P5 信号与 3D 拾取

| 项 | 内容 |
|---|---|
| P5-1 | `Signal<Args...>` + `ObjectID` 自动断开；`Button::pressed` 接入 |
| P5-2 | `Projection::inverse()`、`Camera3D::project_ray_*`、aspect 取自 Viewport |
| P5-3 | `Mesh::get_aabb()`、`MeshInstance3D::intersect_ray` |
| P5-4 | 演示：点球体变色 / 拖动旋转（在 `_unhandled_input` 里做，验证 GUI 不穿透） |

**验收**：手动运行演示；单测覆盖 `Projection::inverse()` 与射线-盒子求交。

### P6 2D 渲染（独立文档）

`CanvasItem` + `RenderingServer` 2D API（矩形/纹理/文字）+ 字体图集。
事件系统不依赖它，但 GUI 可可见地交互必须等它完成。建议下一步单独出 `docs/gui_rendering_plan.md`。

---

## 9. 测试方案

**单元测试（无窗口、无 GPU，进 `orca-tests`）**

| 文件 | 覆盖 |
|---|---|
| `tests/core/input/test_input.cpp` | 按键/鼠标状态、修饰键、`just_pressed` 帧语义、失焦清空 |
| `tests/core/input/test_input_map.cpp` | 动作绑定、多绑定、`exact_match` |
| `tests/scene/test_node_lifecycle.cpp` | `ready` 调用一次、`queue_free` 延迟、`find_node`、`destroy` 无泄漏 |
| `tests/scene/test_input_dispatch.cpp` | 派发顺序、GUI/`_input`/`_unhandled_input` 三级消费、回调内改树、`handled` 截止 |
| `tests/scene/test_control_hit.cpp` | 命中优先级、`MOUSE_FILTER_PASS`、焦点转移/释放 |
| `tests/core/math/test_projection_inverse.cpp` | 逆投影与 `Approx` 校验 |

**集成测试（手动，需窗口）**

- `tests/platform/test_window.cpp`：打印归一化事件流。
- `tests/application/test_application.cpp`：WASD 移动相机 + 鼠标点击 3D 物体。

**测试规范**：新文件必须加入 `tests/CMakeLists.txt`；`TEST_CASE` 用 `"模块/类: 行为"`；
前置条件用 `REQUIRE`；用例结束检查 `Memory::get_allocation_count()`。

---

## 10. 风险与已知坑

1. **坐标系不一致**：GLFW 的 `get_size` 是逻辑坐标、SDL3 现在是物理像素。P2 必须统一，
   否则 GUI 在 150% 缩放显示器上必然错位。建议：`get_size` 统一为逻辑坐标，
   `get_frame_buffer_size` 统一为物理像素，GUI 用前者、渲染用后者算 aspect。
2. **按键映射表遗漏**：未知键映射为 `Key::NONE` 并直接丢弃，避免 GUI 收到无效键。
   映射表需要单测（至少覆盖 A-Z、方向键、修饰键、鼠标键）。
3. **事件回调的生命周期**：`register_event_callback` 无法退订是**潜在** UAF（目前只有
   `test_window` 一个订阅者、生命周期安全）。P2-5 必须解决；在解决前不要给节点/控件注册窗口回调。
4. **遍历中改树**：风险来自现有遍历循环本身（`node.cpp:100-131` 全是 range-for），
   不是 `add_child` 被调用。P0-11 必须先统一改成「下标 + 回调后校验」；
   切勿给遍历加引用或迭代器缓存。
5. **`ready()` 触发时机**：只对「本次进入树的子树」调用，不要每帧全树扫，否则性能与语义都不对。
6. **`queue_free()` 与信号**：删除队列 flush 后要通知 `ObjectDB` 注销 id，否则失效 `ObjectID`
   永远不会被清理（内存与误判风险）。
7. **`Input` 的帧语义**：`is_action_just_pressed` 依赖帧号。若事件在 `parse_input_event` 时
   立即更新状态而 `end_frame()` 在 `tick` 末尾调用，需保证「本帧写入的 just_pressed 在本帧可读、
   下一帧自动失效」；建议用 `pressed_frame == current_frame` 判定。
8. **`Ref::cast` 与 `memnew` 不一致**（AGENTS 5.3 #1）：本方案不引入新的 `RefCounted` 输入对象，
   规避该问题；若未来改成多态事件，先修这条。
9. **不要给 GUI 增加「每帧全树命中测试」**：hover 只在鼠标移动事件里计算，
   控件增删时把 `gui_mouse_over` 置空即可，避免逐帧开销。
10. **暂停语义后置**：`process_mode` / 暂停时是否派发输入，本方案暂不支持，文档需注明。

---

## 11. 实施时需要同步更新的文档

- `AGENTS.md`：新增 `orca-input` 目标与依赖方向（2.1 节）、`core/input/` 目录（第 4 节）、
  `test_window` / 分发入口的说明（4.1 节）、第 5 节「部分完成」里补上事件系统状态。
- `docs/` 新增 `docs/gui_rendering_plan.md`（2D 渲染），并在本文第 1 节交叉引用。
- 本文件在 P0–P5 完成后应把「方案」改为「已实现」并记录实际差异。

---

## 12. 待确认的问题（影响实现细节，不阻塞开工）

1. **GUI 坐标是否需要 DPI 缩放策略**：先按「逻辑窗口坐标、不做缩放」实现，还是现在就引入
   `content_scale` 因子（Godot 的 stretch 模式）？
2. **文本输入是否第一期就要做**：`TextInput` 事件与 IME 强相关，若近期没有输入框需求，
   `TextInput` 可以先只支持 ASCII 直传、IME 后置。
3. **3D 拾取是否需要拖拽/持续按住语义**：这会影响是否要额外维护 `Input` 的拖拽状态
   （`gui_mouse_focus` 已在 GUI 层预留，3D 侧需要时再加）。
4. **是否需要手柄/触摸**：本方案的事件类型预留了扩展位，但映射表与测试规模差别较大，
   建议一期不做。

---

## 13. 实施进度记录

### 已完成：platform 窗口层事件补全（2026-11-03）

| 改动 | 内容 |
|---|---|
| `platform/window.h` | `WindowEventType` 扩到 10 种（Key/MouseButton/MouseMotion/MouseWheel/TextInput/CursorEnter/Focus/WindowSize/FramebufferSize/Close）；`WindowEvent` 补齐键码、按下/重复、修饰键、鼠标坐标/相对位移/滚轮/按键掩码、Unicode、光标进出、焦点、尺寸；新增 `WindowKeyModifier` 与 `WindowMouseButtonMask` |
| `platform/glfw/glfw_window.*` | 补齐 char / scroll / cursor_enter / window_focus 回调；修正原先「鼠标移动发成 Key 事件且丢弃坐标」的 bug；key/mouse 回调回填全部字段；回调内自行维护鼠标按键掩码与相对位移；失焦时清零掩码 |
| `platform/sdl3/sdl3_window.*` | 补齐 `SDL_Init`/`SDL_Quit`、`SDL_EVENT_QUIT`、MouseMotion/Wheel/TextInput/Focus/CursorEnter；UTF-8 文本解码为码点逐个发出；`get_size` 改为窗口坐标、`get_frame_buffer_size` 保留像素坐标（高 DPI 下两者不同） |
| `platform/CMakeLists.txt` | `orca-platform` 增加 PUBLIC 链接 `orca-input`（复用键位枚举） |
| `tests/platform/test_window.cpp` | 改为打印归一化后的事件流，便于手动验证 |

**与计划（§7 D6）的一处偏差**：D6 原本设想「平台后端直接产出 `InputEvent`」。实际按需求分两步走：
本期先把平台层内部事件补全为 `WindowEvent`，`WindowEvent → InputEvent` 的转换放在
`Application::window_event_callback`（application 同时链接 platform 与 input，是唯一能合法做转换的位置）。
这样平台层不依赖 `core/input` 的事件结构、`core/input` 也不依赖平台层，依赖方向仍然干净；
代价是转换代码集中在 application，而不是分散在各后端。

**复用已有枚举**：`core/input/keyboard.h` 的 `KeyboardKeyCode` 与 `core/input/mouse_button.h` 的
`MouseButton` 已经存在，平台层直接复用，避免出现两套键位枚举（否则 application 同时包含
`window.h` 与 `input.h` 时会因 `MouseButton` 重定义而编译失败）。

**已知缺口**：`KeyboardKeyCode` 目前没有 `CapsLock` / `Super`，`MouseButton` 没有 X1/X2，
因此这些键的*按键事件*会被丢弃，只保留在 `WindowEvent.modifiers` 中。需要时在 `core/input` 补齐枚举，
平台层映射表同步加 case 即可。

**验证**：`cmake --build build --parallel` 通过（含 `orca-platform-window-test`）。
运行时验证需手动执行 `build/tests/orca-platform-window-test.exe`，观察鼠标移动/按键/滚轮/文本/焦点事件输出。

### 下一步

1. `Application::window_event_callback`：`WindowEvent` → `InputEvent`，并调用 `Input::parse_input_event`。
2. 实现 `Input::parse_input_event` 的状态维护（按键表、鼠标位置、动作帧号、失焦清空）。
3. 回到 P0：节点系统地基修复（§8），然后 P3 分发。

