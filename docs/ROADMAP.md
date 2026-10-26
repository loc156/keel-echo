# 现状与路线

这份文档记录三件事：哪些东西真的跑过、哪些没有、以及已知的问题和有意的限制。
最后附一份"可以做但还没做"的清单，不代表承诺。

## 验证过的部分

| 项目 | 验证到什么程度 |
| --- | --- |
| Keel | 18 个头文件在 `-std=c++17 -Wall -Wextra` 下零警告编译；核心语法、表面语法、装载器、单步执行都实际跑通 |
| Echo | `build.ps1 -Test` 全流程通过：7 个 ImGui 目标 + 11 个库源文件 + 单元测试；单元测试 74 项 0 失败 |
| Echo 示例 | `examples/hello` 与 `examples/calculator` 编译链接通过并启动过窗口（calculator 用 `--shot` 截图验证） |
| KeelUI | `examples/counter.keel` 由 `examples/host.cpp` 装载，连续渲染 5 帧无错误，截屏见 [keelui/docs/screenshot.png](../keelui/docs/screenshot.png) |
| 文档示例 | `LANGUAGE.md` 里 14 段 keel 示例逐块实跑（12 段正常输出，2 段本来就是演示报错和越限）；`LOADER.md` 的最小宿主与 `EMBEDDING.md` 的自定义指令示例都编译并跑出了文档写的输出 |

环境：Windows 10/11 + MinGW-w64 g++ 11.5.0，Dear ImGui 1.93.0 WIP，GLFW 预编译包。

## 没有验证的部分

- 非 Windows 平台。源码里没有平台专属代码（Echo 只通过 GLFW 后端碰平台），但没有 macOS / Linux 的构建脚本，也没跑过。
- g++ 11.5.0 以外的编译器。Echo 用到 C++20 指定初始化器；Keel 的 `fast_atof` 在 `__cpp_lib_to_chars` 不足时会回退到 `strtod`，这条回退路径没有实测。
- GUI 的像素级回归。界面能开、能截屏，但没有"跑 30 帧再比对像素"的自动化（`captureFramebufferBmp` 已经可用，缺的是对比逻辑）。
- Dear ImGui 1.92 之前的版本。`theme::setFontScale` 用了 1.92 才加入的 `ImGuiStyle::FontScaleDpi`，更早的版本编不过。

## 设计取舍

这几条常被当成"没做完"，其实是方向选择。

- **块在运行时定义**（`{` 从栈顶取名字）。换来了块名可以算出来、文件顶层就是入口；
  代价是装载器无法静态列出全部块，工具只能在运行期观察。
- **没有闭包、没有局部作用域**，变量按 VM 实例存放。换来了状态完全可观察、可单步；
  代价是大脚本要自己管命名。
- **一条数据栈，没有具名参数**。换来了指令模型极简、每步代价可测；代价是人读栈式代码费劲。
- **限额是内建的**（步数 / 深度 / 栈 / 单次分配，外加文件沙箱）。换来了"脚本卡死或越权不会
  拖垮宿主"；代价是长的计算要显式 `limit`。

## 已知缺陷与限制

Keel：

- 表面语法 `fn` 不接受形参（设计取舍，理由见上）；其余条目见
  [SURFACE.md](../keel/docs/SURFACE.md) 第 6 节。
- `KeelLoader.hpp` 保留了一个遗留重载 `install(KeelData&, entry, src)`：只切词、不装环境（不限额度、不装 include、不走表面语法）。新代码用 `load_text + install(Keel&, ...)`。
- `Value` 的列表和映射是 `shared_ptr` 共享的，拷贝是浅拷贝；要独立容器得手工深拷。
- 指令表是两份人工维护的文本（[REFERENCE.md](../keel/docs/REFERENCE.md) 与各头文件注释），没有从代码自动生成的脚本。

Echo：

- `API.md` 与头文件注释也是两份人工维护的文本，可能轻微不同步，以头文件为准。
- 没有 CMake，也没有 CI。
- 第三方库要自己放进 `echo/libs/`，仓库不收录。

KeelUI：

- 26 条 `ui.*` 指令，覆盖面是工具面板级别，不是完整的 ImGui 绑定。
- 脚本里的 `f.*` 输出被丢弃（`stdout_sink` 是空函数）。
- `ui.input` 的输入缓冲只能由用户输入改变，脚本写不进初值。
- 宿主程序只有 `examples/host.cpp` 一个，没有做拖放、热重载之类的功能。

## 可以做但还没做的事

1. CMake。Echo 需要给 GLFW 和 ImGui 写 `find_package` 或 `FetchContent`，Keel 是纯头文件。前提是能在一台装了 cmake 的机器上跑通再合进来。
2. CI。GitHub Actions 的 Windows runner 跑 `echo/build.ps1 -Test`，Keel 侧加一个纯头文件的编译检查。
3. Linux / macOS 构建脚本。`build.sh`，用 `pkg-config glfw3` 取编译和链接参数。
4. 指令表自动生成。从 `xxx_register` 函数体里抽 `register_func` 和 `alias`，直接生成 `REFERENCE.md`，省掉两份文本不同步的麻烦。
5. 截图回归。示例已经有 `--shot`，接上"存 BMP 再比对"就能做。
6. 更细的教程。现在的文档偏参考手册，面向"能读代码的人"；面向"只想照着抄一份能跑的东西"的步骤化教程还没写。
