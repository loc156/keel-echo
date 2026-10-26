# Keel × Echo

> **Keel** 是一个 Forth 式的小型嵌入式脚本语言（C++17，header-only）；
> **Echo** 是一个基于 Dear ImGui 的 C++ 即时模式 UI 封装库（C++20）；
> **KeelUI** 把两者接起来——用脚本画界面。
>
> English: **Keel** is a small Forth-like scripting language in one C++17 header.
> **Echo** wraps Dear ImGui in an object-style immediate-mode API.
> **KeelUI** lets a Keel script drive an Echo-hosted window through `ui.*` commands.

## Keel的定位
特别的一点，Keel 不单纯是一个给人用的脚本语言， 更是一个**默认不信任脚本、错误可聚合成一段可直接回灌给外部的嵌入式栈式脚本引擎**。

- **默认不信任**是结构性的，不只是限额：引擎只执行宿主注册过的指令，脚本递不进原生代码；
  没有闭包和宿主回调，状态只有一条数据栈；再叠上默认限额（20 万步 / 深度 200 / 栈 1 万）、
  硬天花板、文件 IO 沙箱、可关的 `include`、装载期的字节数 / token 数上限。
- **错误可聚合**：错误按位置收集，同一处同一描述压成 `(xN)`，上限 100 条，`(块, ip)` 能翻回
  `文件:行`。引擎不负责回灌，但回灌需要的那段文本是它直接给出的（代码里就叫
  `format_errors_for_ai`）。
- **块表不归宿主管**：`{` 在运行时从栈顶取块名，名字可以是算出来的；装载器只做非权威的结构扫描
  （给诊断和行号用），从不定义块。

如果使用者是模型 / agent：生成一段脚本、在受限环境里跑、把结构化错误喂回去再改。
如果使用者是人，优先考虑表面语法那层（`let` / `if` / `while` / `fn`）。
Echo 和 KeelUI 不受这个定位影响：Echo 是普通的 C++ UI 库。

## 三个部分

| 目录 | 是什么 | 语言 | 依赖 |
| --- | --- | --- | --- |
| [`keel/`](keel/) | 栈式脚本语言与 VM：token → 块 → 数据栈，18 个头文件，header-only | C++17 | 无（只用标准库） |
| [`echo/`](echo/) | 即时模式 UI 封装：`App` + 窗口 + 30 多个控件，`libecho.a` | C++20 | GLFW 3.3+、Dear ImGui 1.92+ |
| [`keelui/`](keelui/) | 把 Echo 底座上的 ImGui 绑成 `ui.*` 指令，脚本每帧跑一个 `frame` 块 | C++17 | Keel + Dear ImGui |

```
 .keel 脚本 ──keel::Keel──▶ token / 块 / 数据栈 ──▶ 内置库（base·str·cont·math·io + 三个 IO 扩展）
                                   │
              keelld::装载器 ──────┤  文件 → token → Program → 环境（限额/include/沙箱）
                                   │
              keelui::register_ui ─┘  ui.* 指令
                                   │
                        Echo::App（GLFW + OpenGL3 + ImGui 帧循环）
```

三者的依赖方向只有一条：**KeelUI → Keel + Echo 的 App**。Keel 不知道 UI 的存在，Echo 不知道脚本语言的存在。

| Echo：`echo/examples/calculator`         | KeelUI：`keelui/examples/counter.keel`       |
| --------------------------------------- | ------------------------------------------- |
| ![Echo 计算器示例](echo/docs/screenshot.png) | ![KeelUI 计数器示例](keelui/docs/screenshot.png) |

## 快速体验

**Echo**（Windows + MinGW-w64，不需要 CMake）：

```powershell
# 把第三方源码放到 echo/libs/（见 echo/build.ps1 开头的说明）
cd echo
.\build.ps1 -Test     # 编库 + 跑单元测试
.\build.ps1           # 编出 build\libecho.a 与两个示例
```

**Keel**（纯头文件，拿 `-I keel/include` 就能编）：

```cpp
#include "KeelStd.hpp"
#include "KeelLoader.hpp"

int main() {
    keel::Keel vm;
    keelstd::register_all(vm);                       // 装内置指令

    const char* src = R"(
        "sq" { dup * }                               // 运行时定义块
        5 "sq" call println                          // 25
    )";
    auto program = keelld::load_text(src);           // 文件 → token → 入口块
    keelld::Environment env;
    if (!keelld::install(vm, program, env).ok) return 1;
    keelld::execute(vm);
}
```

这两段代码都在仓库里，能直接编。Keel 那段就是 [`keel/examples/hello.cpp`](keel/examples/hello.cpp)，完整步骤见 [`docs/BUILD.md`](docs/BUILD.md)。

## 文档地图

| 我想… | 读 |
| --- | --- |
| 先搞清三个项目什么关系 | [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| 把代码编起来 / 排查编译错误 | [`docs/BUILD.md`](docs/BUILD.md) |
| 用 Echo 做第一个界面 | [`echo/docs/GETTING-STARTED.md`](echo/docs/GETTING-STARTED.md) |
| 查 Echo 的控件和函数签名 | [`echo/docs/API.md`](echo/docs/API.md) |
| 了解 Keel 的语言模型（先读这篇） | [`keel/docs/LANGUAGE.md`](keel/docs/LANGUAGE.md) |
| 知道 Keel 为什么"默认不信任" | [`keel/docs/SURFACE.md`](keel/docs/SURFACE.md) 第 6 节、[`docs/ROADMAP.md`](docs/ROADMAP.md) 设计取舍 |
| 查 Keel 有哪些指令 | [`keel/docs/REFERENCE.md`](keel/docs/REFERENCE.md) |
| 把 Keel 装进自己的程序 | [`keel/docs/LOADER.md`](keel/docs/LOADER.md)、[`EMBEDDING.md`](keel/docs/EMBEDDING.md) |
| 写 `let/if/while/fn`（表面语法） | [`keel/docs/SURFACE.md`](keel/docs/SURFACE.md) |
| 用脚本画界面 | [`keelui/README.md`](keelui/README.md) |
| 知道哪些没做完 | [`docs/ROADMAP.md`](docs/ROADMAP.md) |

## 现状与已知限制

- 只在 Windows + MinGW-w64 g++ 11.5.0 上验证过。`echo/build.ps1` 是 Windows 专用；其他平台要自己写构建，源码本身没有平台绑定代码，但没跑过。
- 第三方库不在仓库里。GLFW 和 Dear ImGui 要自己下载到 `echo/libs/`，这个目录被 `.gitignore` 排除了。
- Keel 的表面语法还是 v1，`fn` 不支持形参，调用方按栈约定传值，细节见 [`keel/docs/SURFACE.md`](keel/docs/SURFACE.md)。
- `echo/docs/API.md` 和头文件注释是两份人工维护的文本，可能有轻微不同步，以头文件为准。
- 没有 CMake，也没有 CI。

## 关于 AI 参与

事先说明：本项目后期代码大量依赖 AI 修改和完善，核心设计与最初框架由本人独立搭建。
本项目采用 MIT 许可证；如你发现任何可能的许可或归属问题，欢迎提 issue，我会尽快处理。

## 关于名字

这个项目最早叫 Aspark，`Aspark.hpp`、`spk::Aspark`、脚本后缀 `.asp` 都是那时候留下的。
取名时没意识到它和已有的公司、产品重名比较严重，整理成开源仓库时就把整套名字换成了
Keel：`Aspark.hpp` → `Keel.hpp`，`spk` → `keel`，`spk::Aspark` → `keel::Keel`，
`.asp` → `.keel`，连内部结构体也从 `AparkData` 改成了 `KeelData`。

改名只动了名字，功能和设计没有变化。在别处看到 Aspark，指的就是它改名前的样子；
本仓库里已经没有旧名字了。

## 许可证

[MIT](LICENSE)，版权归作者所有。

`keel/`、`echo/`、`keelui/` 三部分都是作者独立编写的代码，统一使用这一份 MIT 许可证，
没有各自单独的条款。仓库里唯一的第三方软件是 GLFW 和 Dear ImGui：它们以依赖的形式使用，
源码不在本仓库中，许可证原文收在 [`LICENSES/`](LICENSES/)（GLFW 为 zlib/libpng，
Dear ImGui 为 MIT）。
