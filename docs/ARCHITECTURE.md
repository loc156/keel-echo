# 架构

三个子项目，一条依赖方向。下面先看整体，再看各自的边界。

```
┌──────────────────────────────────────────────────────────────────┐
│  .keel 脚本（文本）                                               │
│     核心语法（body 模型）  或  表面语法（let/if/while/fn）         │
└───────────────┬──────────────────────────────────────────────────┘
                │ keelld::load_text / load_file
                ▼
┌──────────────────────────────────────────────────────────────────┐
│  KeelLoader（keel/include/KeelLoader.hpp）                        │
│  源码 → token → Program{ blocks, diags } → Environment            │
│  （限额 · include 路径 · 文件沙箱 · stdout 汇 · prelude）          │
│  错误 → "文件:行:列"                                             │
└───────────────┬──────────────────────────────────────────────────┘
                │ install / prepare / execute
                ▼
┌──────────────────────────────────────────────────────────────────┐
│  Keel VM（keel/include/Keel.hpp，keel::Keel / keel::KeelData）     │
│  token 流 + 数据栈 + 块表 + 编译后的指令缓存                       │
│  内置库：base · str · cont · math · io（核心 5 组）                │
│          f. · big. · src.（三个 IO 扩展库）                        │
└───────────────┬──────────────────────────────────────────────────┘
                │ keelui::register_ui(vm)  登记 ui.* 指令
                │ keelui::Host 每帧跑 frame 块
                ▼
┌──────────────────────────────────────────────────────────────────┐
│  KeelUI 宿主：ImGui 立即模式调用                                    │
│  （KeelUI.hpp 直接用 imgui.h / imgui_stdlib.h）                    │
└───────────────┬──────────────────────────────────────────────────┘
                │ Echo::App 提供 GLFW 窗口 + GL 3.3 上下文 + 帧循环
                │ + ImGui 初始化 + 字体 + DPI + 主题 + 日志
                ▼
┌──────────────────────────────────────────────────────────────────┐
│  Echo（echo/include/echo/ · echo/src/）                           │
│  App · WindowManager · Window/PanelWindow · Widget/Content        │
│  30 多个控件 · Source<T> · theme/log/input/layout/time            │
└──────────────────────────────────────────────────────────────────┘
```

## 边界：谁可以依赖谁

| 项目 | 依赖 | 明确**不**依赖 |
| --- | --- | --- |
| `keel/` | 只有 C++ 标准库 | UI、图形、平台 API、任何第三方库 |
| `echo/` | GLFW、Dear ImGui、OpenGL3 | 脚本语言、Keel |
| `keelui/` | Keel（VM + 装载器）、Dear ImGui | **Echo 的窗口树**（只用 Echo 的 `App` 当宿主；`KeelUI.hpp` 本身不 include 任何 Echo 头） |

三边互不认识：Keel 不知道自己被谁用，Echo 不知道自己上面有脚本。所以三个项目都可以单独拿出去用。

## Keel：为什么"源码 = 入口块的块体"

Keel 是 Forth 式的 token 栈机。`{` 是一条**指令**，不是语法结构：

- 执行到 `{` 时，它从数据栈顶取块名，把配对的 `}` 之前的 token 存成一个块；
- 所以块名可以是算出来的（`"na" "me" concat` 拼出 `name`），装载器**不可能**预先知道所有块名；
- 装载器因此只做四件事：源码 → token；非权威的结构扫描（给诊断/断点/行号用，只报告不定义）；准备环境；把运行时错误 (块, ip) 翻回文件行号。

由此有两个直接影响：

- 文件顶层就是入口块的块体，不写 `main { }`；要"程序"就是顶层那一串 token。
- 块在**执行到** `{` 时才存在，所以调用定义在后面的块也没问题（同一次运行里顺序执行过就行）。

## Echo：对象树 + 每帧重画

Echo 把 ImGui 的立即模式包成"对象"：`WindowManager` 持有窗口，窗口持有控件（`Widget` / `Content`）。但**渲染仍然是每帧一遍**：`wm.renderAll()` 每帧把树走一次，逐个调用对应的 ImGui 函数。

- 控件不保存"上一帧画到哪"，只保存状态（文本、开关、回调）。
- 控件的每个字段可以是"快照 / 指针 / lambda"（`Source<T>`），所以数据变了**不用重建界面**，下一帧读到的新值就生效。
- 布局容器（`Row` / `Grid`）也是控件，所以"同一行放几个按钮"是树结构问题，不是坐标问题。

`App` 是唯一知道 GLFW / OpenGL 的地方：开窗口、建上下文、初始化 ImGui、按显示器 DPI 缩放、提供 `beginFrame()/endFrame()` 配对。业务代码通常只碰 `App`、`WindowManager`、以及一堆 `add<控件>()`。

## KeelUI：宿主契约

`keelui::Host` 把「脚本」和「每帧的 ImGui 调用」接起来：

1. **装载**：`Host::load(path)` → 用 `keelld` 把文件变成 `Program` → `install`（限额放宽到 UI 级别：步数 1000 万、深度 1000、栈 10 万）→ 执行顶层（序章），序章负责定义 `startup` 和 `frame` 两个块。
2. **启动**：有 `startup` 就跑一次。
3. **每帧**：`Host::frame()` → `run_block("frame")`：
   - 清掉 `ui.*` 的配对计数（窗口/子窗口/表格/页签）；
   - `clear_errors()`、`vm->init("frame")`、清空数据栈、重置步数预算；
   - 最多执行 `budget`（默认 10 万）条指令；
   - `force_close()`：脚本没配对的 `Begin/End` 由宿主替它收尾，否则 ImGui 断言会把整个 App 带走；
   - 出错 → 停脚本、把错误记在 `Host::error` 里，**不崩 App**；下一帧画一个只有错误信息的窗口。

脚本里的 `f.*`（快速 IO）输出被丢弃（`stdout_sink` 是空函数），`println` 走宿主控制台。

## 一次 frame 的完整时序

```
App::beginFrame()          // 收事件、算 dt、清屏、ImGui::NewFrame()
  └─ Host::frame()         // 若已装载
       ├─ run_block("frame")
       │    ├─ 清 ui.* 计数
       │    ├─ clear_errors / init("frame") / 清栈 / 步数归零
       │    ├─ while (i < budget && !end) vm->step()
       │    ├─ keelfast::flush_all()
       │    └─ force_close()           // 补上没配对的 End
       └─ 若出错：记 error、loaded = false（下一帧显示错误）
  └─ 你的其他窗口/控件（Echo 的 WindowManager）
App::endFrame()            // ImGui::Render() + 提交绘制 + 交换缓冲
```

`beginFrame()` 返回 true 时**必须**在同一个循环体里调 `endFrame()`，否则 ImGui 帧状态失衡（调试版会断言）。
