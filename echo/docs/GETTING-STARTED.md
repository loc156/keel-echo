# 入门：从零做一个小工具

## 0. 先把它编起来

见 [docs/BUILD.md](../../docs/BUILD.md) 的三步：把 GLFW / Dear ImGui 放进 `echo/libs/`，然后

```powershell
cd echo
.\build.ps1           # 编出 build\libecho.a 和示例
```

下面每一节都可以直接替换 `examples/hello/main.cpp` 试。

## 1. 最小程序：一个窗口，一行字

```cpp
#include "echo/echo.h"

using namespace echo;

int main() {
    App app;                          // 唯一知道 GLFW / OpenGL 的东西
    AppConfig config;                 // 全部有默认值
    config.title = "Echo";
    if (!app.init(config)) return 1;  // 失败会写日志 + 弹框

    WindowManager windows;
    auto& panel = windows.add<PanelWindow>("TestWindow");
    panel.add<PlainText>("Hello");

    while (app.running() && app.beginFrame()) {
        windows.renderAll();          // 每帧重画整棵树
        app.endFrame();
    }
    return 0;
}
```

这段就是 `examples/hello/main.cpp`。三个要点：

- `app.running() && app.beginFrame()` 为真时，**必须**在同一轮里调 `app.endFrame()`（否则 ImGui 帧状态失衡）。
- `windows.add<PanelWindow>(...)` 返回 `T&`，这个引用在 `remove()` / `clear()` 之前一直有效。
- 界面的"值"不是存起来的：每帧 `renderAll()` 会把树走一遍重新调用 ImGui。

## 2. 加控件：按钮、输入框、勾选框

```cpp
int clicks = 0;
std::string name;
bool loud = false;

auto& panel = windows.add<PanelWindow>("主面板", ImVec2(420.0f, 320.0f));
panel.add<HeadingText>("小工具");
panel.add<PlainText>([&] { return "点了 " + std::to_string(clicks) + " 次"; });
panel.add<Button>("点我", [&] { ++clicks; }, ImVec2(-1.0f, 32.0f));  // -1 = 占满宽度
panel.add<TextInput>("名字", &name);                                 // 绑到 std::string
panel.add<Checkbox>("大声", &loud, [](bool) {});                      // 绑到 bool
```

控件都是 `add<T>(构造参数...)`，参数**原样转发给 `T` 的构造函数**。所以查某个控件的参数，直接看它头文件里的构造函数即可（或看 [API.md](API.md)）。

控件有两种取值方式。`Button`、`Checkbox` 走回调，动作发生时你在回调里改自己的变量；`PlainText`、`MeterText`、`ProgressBar` 不回调，每帧从 `Source<T>` 取值重画。

## 3. 数据绑定：`Source<T>`

每个控件的"值/标签"字段都是一个 `Source<T>`，可以装三种东西，语法一致：

```cpp
Source<int> a = 42;          // 快照：永远是 42
Source<int> b = &counter;    // 指针：每帧解引用，counter 变了就显示变了
Source<int> c = [&] { return tasks.size(); };  // lambda：每帧调用
```

值留在你自己的变量里，控件每帧去读，所以数据变了不需要重建界面。

有几点需要注意：

- lambda 会被拷进 `std::function`，捕获用 `[&]`，并且要保证被捕获的变量活得比控件长。
- 每帧都会调用一次，别在里面做读文件、加锁、遍历大容器这类活。
- `get()` 返回副本，`std::string` 每帧会构造一次。对一般界面来说这点开销可以忽略。

## 4. 布局：同一行、网格

布局也是控件，所以是树结构问题，不是坐标问题：

```cpp
auto& footer = panel.add<Row>(6.0f);          // 横向排列，间距 6
footer.add<Button>("复制", [&] { /* ... */ }, ImVec2(90.0f, 0.0f));
footer.add<Button>("清空", [&] { clicks = 0; }, ImVec2(90.0f, 0.0f));

auto& pad = panel.add<Grid>(4);               // 4 列网格，行优先填
for (const char* label : {"7", "8", "9", "/"})
    pad.add<Button>(label, [&] { /* ... */ }, ImVec2(-1.0f, 40.0f));
```

`examples/calculator/main.cpp` 就是完整的网格 + 事件例子。

## 5. 退出、主题、截图

```cpp
windows.setOnQuit([&] { app.requestQuit(); });   // 菜单里的 Quit
theme::dark();                                   // 默认深色主题
theme::setAccent(colors::cyan());

// 冒烟测试：跑到第 N 帧存一张 BMP 再退出
if (app.frameIndex() >= 30) { captureFramebufferBmp("shot.bmp"); app.requestQuit(); }
```

`requestQuit()` 之后 `running()` 下一帧就为 false；`running()` 同时也看窗口的 X 按钮。

## 6. 常见问题

| 现象 | 原因 |
| --- | --- |
| 双击没反应 / 立刻退出 | `init()` 失败。GUI 子系统下 stderr 是黑洞，看 `AppConfig::logFile` 或打开 `showErrorDialog` |
| 中文是方块 | 字体没加载成功：`AppConfig::fontPath` 指定字体路径，或让自动探测找到 `C:/Windows/Fonts/msyh.ttc` |
| 控制台没有输出 | `-mwindows` 是 GUI 子系统。用 `build.ps1 -Console`，或看日志文件 |
| 调试版 ImGui 断言 | `Begin` 返回 false 仍然调了对应的 `End`。用 Echo 的控件不会踩到；自己写裸 ImGui 要检查配对 |
| 界面糊 / 太大 | `AppConfig::scaleToMonitor` 是 DPI 自动缩放，关掉它就是按物理像素画 |

更细的 API 见 [API.md](API.md)；三个项目的关系见 [docs/ARCHITECTURE.md](../../docs/ARCHITECTURE.md)。
