# 构建

这个仓库**没有 CMake**，也没有 make。所有构建都是明文的 `g++` 命令行，Windows 上由 PowerShell 脚本代劳。

## 0. 你需要什么

| 东西 | 要求 | 用途 |
| --- | --- | --- |
| 编译器 | C++17 起步；**Echo 需要 C++20**（用了指定初始化器） | 全部 |
| GLFW | 3.3+（含 `include/GLFW/` 与 `lib-mingw-w64/libglfw3.a`） | 只有 Echo / KeelUI 需要 |
| Dear ImGui | **1.92+**（在 1.93.0 上验证；含 `backends/` 与 `misc/cpp/imgui_stdlib.cpp`） | 只有 Echo / KeelUI 需要 |

验证过的环境：**Windows 10/11 + MinGW-w64 g++ 11.5.0**（以下命令都在这套环境上跑过）。
代码本身没有平台绑定，理论上 macOS / Linux 用 clang + 同名库也能编，但**未验证**（见 [ROADMAP](ROADMAP.md)）。

## 1. 只编 Keel（最快，纯头文件）

```bash
g++ -std=c++17 -O1 -Wall -Wextra -I keel/include your_app.cpp -o your_app
```

`keel/include/` 是唯一的头文件目录，没有 `.cpp` 要编，也没有第三方依赖。

## 2. Echo 的第三方库放哪

仓库**不收录**第三方源码（体积大、且要尊重上游发布方式）。约定放在 `echo/libs/`：

```
echo/
└─ libs/                     ← 已被 .gitignore 排除
   ├─ imgui/                 imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
   │                         imgui_stdlib.cpp  backends/  misc/cpp/
   └─ glfw/                  include/GLFW/*.h   lib-mingw-w64/libglfw3.a
```

- Dear ImGui：<https://github.com/ocornut/imgui/releases>（下载源码 zip 解到 `libs/imgui`）
- GLFW：<https://www.glfw.org/download.html> 的 **Windows 预编译包**（解到 `libs/glfw`，它自带上面那个目录结构）

`imgui_stdlib.cpp` 在 ImGui 源码的 `misc/cpp/` 下；本仓库的构建脚本默认按 `libs/imgui/imgui_stdlib.cpp` 找，所以把这两个文件拷到 `libs/imgui/` 根目录即可（或改脚本里的路径）。

## 3. 编 Echo

```powershell
cd echo
.\build.ps1                # 库 + 两个示例
.\build.ps1 -Test          # 编库 + 编并跑单元测试
.\build.ps1 -Clean -Warn   # 先清、并打开 -Wall -Wextra
.\build.ps1 -Run           # 编完直接启动 hello 示例
```

做的活（顺序固定）：

1. 把 `libs/imgui` 编成 `build/libimgui.a`（7 个 `.cpp`，含两个后端与 `imgui_stdlib`）；
2. 把 `src/*.cpp` 编成 `build/libecho.a`；
3. 把 `examples/*/main.cpp` 链接成 exe（`-mwindows`，静态链接 libgcc/libstdc++，产物只依赖系统 DLL）；
4. `-Test` 时再编 `tests/test_main.cpp` 并直接跑。

**手动版**（不想要脚本时，任选一份 IDE 工程照着填）：

```bash
g++ -std=c++20 -O2 -DNDEBUG -Iecho/include -Iecho/libs/imgui -Iecho/libs/imgui/backends -Iecho/libs/glfw/include \
    -c echo/src/*.cpp
g++ -std=c++20 -O2 -DNDEBUG -Iecho/include -Iecho/libs/imgui -Iecho/libs/imgui/backends -Iecho/libs/glfw/include \
    echo/examples/hello/main.cpp *.o \
    echo/libs/imgui/imgui.cpp echo/libs/imgui/imgui_draw.cpp echo/libs/imgui/imgui_tables.cpp \
    echo/libs/imgui/imgui_widgets.cpp echo/libs/imgui/imgui_stdlib.cpp \
    echo/libs/imgui/backends/imgui_impl_glfw.cpp echo/libs/imgui/backends/imgui_impl_opengl3.cpp \
    -Lecho/libs/glfw/lib-mingw-w64 -lglfw3 -lopengl32 -lgdi32 -static -o hello.exe
```

## 4. 编 KeelUI

`KeelUI.hpp` 是单头文件，**只依赖 Keel 和 imgui**（不需要 Echo 头文件）。它要编进一个有 imgui 上下文的宿主程序——仓库里就有一个：`keelui/examples/host.cpp`（用 Echo 的 `App` 当宿主）。先按 §3 编出 `echo/build/libecho.a` 与 `libimgui.a`，然后：

```bash
g++ -std=c++17 -O1 \
    -I keel/include -I keelui/include -I echo/include \
    -I echo/libs/imgui -I echo/libs/imgui/backends -I echo/libs/glfw/include \
    keelui/examples/host.cpp \
    echo/build/libecho.a echo/build/libimgui.a echo/libs/glfw/lib-mingw-w64/libglfw3.a \
    -lopengl32 -lgdi32 -static -mwindows -o host.exe
```

（上面这条命令在本仓库验证过：编出的 `host.exe examples/counter.keel` 能正常开窗、每帧跑 `frame` 块。）

只做语法检查（不链接）时：

```bash
g++ -std=c++17 -fsyntax-only -I keel/include -I keelui/include -I echo/libs/imgui -I echo/libs/imgui/backends keelui/include/KeelUI.hpp
```

宿主怎么写见 [`keelui/README.md`](../keelui/README.md) 与 [`docs/ARCHITECTURE.md`](ARCHITECTURE.md)。

## 5. 常见报错

| 现象 | 原因 | 解法 |
| --- | --- | --- |
| 编译**退出码 1，一行错误都没有** | MinGW 的 `cc1plus.exe` 找不到它旁边的 DLL（g++ 驱动把失败吞了） | 把 `g++` 所在目录加进 `PATH`，或给脚本传 `-Mingw <目录>`。`build.ps1` 已内置这一行 |
| `fatal error: imgui.h: No such file` | 没加 `-I libs/imgui` | 见 §3 的 include 列表 |
| `undefined reference to 'glfwInit'` 等 | 没链接 GLFW | 加 `-L libs/glfw/lib-mingw-w64 -lglfw3`；静态包是 `libglfw3.a` |
| `designated initializers are a C++20 feature` | 用 `-std=c++17` 编 Echo | Echo 必须 `-std=c++20` |
| 运行时 ImGui 断言 `BeginXxx/EndXxx` 不配对 | 代码里 `Begin` 返回 false 还调了 `End`，或漏了 `End` | Echo 的控件已经处理；自己写裸 ImGui 时检查配对 |
| 程序起来了但**看不到控制台输出** | `-mwindows` 是 GUI 子系统，stdout 是黑洞 | 用 `build.ps1 -Console`，或看 `log` 写的日志文件 |
| 中文全是方块 | 字体没加载到 | `AppConfig::fontPath` 指定字体，或让自动探测走到 `C:/Windows/Fonts/msyh.ttc` |
