# Echo API 参考

Echo 是一个基于 Dear ImGui + GLFW 的 C++ 立即模式 UI 封装库：把 ImGui 的逐帧调用包装成 `Window` / `Widget` / `Content` 的对象式接口，但保留"每帧重画"的语义。全部公开接口位于命名空间 `echo`（通常 `using namespace echo;`）。

- 包含方式：把库的 `include` 目录加进头文件搜索路径（`-I echo/include`），然后 `#include "echo/echo.h"` 一次拉进全部功能；也可以只包含需要的那一个，例如 `#include "echo/text.h"`。
- 依赖：GLFW 3.3+、Dear ImGui **1.92+**（在 1.93.0 上验证；1.92 之前 `ImGuiStyle::FontScaleDpi` 还不存在，编不过）、`imgui_stdlib`（输入框直接编辑 `std::string` 需要它）。
- 编译单元：`src/` 下全部 `.cpp`，加上 ImGui 核心 `imgui.cpp`、`imgui_draw.cpp`、`imgui_tables.cpp`、`imgui_widgets.cpp`，后端 `imgui_impl_glfw.cpp`、`imgui_impl_opengl3.cpp`，以及 `imgui_stdlib.cpp`。
- 这份文档按头文件分节，与源码同名对应：`app.h` 对应「app.h」节，依此类推；每节先列公开类型，再列成员与自由函数。

---

## 一、公开控件总表

所有控件都继承 `Widget`，都可以通过 `add<T>(...)` 加进一个 `Content`（窗口内容、`Row`、`Grid`、`TreeNode`、`TabBar` 的页签都提供 `add`）。`add` 的参数会原样转发给 `T` 的构造函数，返回 `T&`。

| 类别 | 控件 | 头文件 | 一句话用途 |
| --- | --- | --- | --- |
| 文本 | PlainText | text.h | 原样显示一段正文，不做格式化（`%` 不被当占位符） |
| 文本 | ColoredText | text.h | 指定颜色的正文 |
| 文本 | HeadingText | text.h | 小节标题 + 右侧分隔线（等价 `ImGui::SeparatorText`） |
| 文本 | WrappedText | text.h | 按可用宽度自动折行的长正文 |
| 文本 | MutedText | text.h | 灰色小字（说明、单位、补充信息） |
| 文本 | HighlightText | text.h | 带背景色块的文字（标签、提示、警告条） |
| 文本 | Badge | text.h | 圆角小标签（状态、分类、计数） |
| 文本 | SpeakerText | text.h | 「说话者: 内容」，内容自动折行 |
| 文本 | ListItemText | text.h | 「前缀 + 文字」列表项，默认前缀是圆点 |
| 文本 | KeyValueText | text.h | 「键 …… 值」两列对齐的一行 |
| 布局 | Row | containers.h | 横向排列：子控件依次画在同一行 |
| 布局 | Grid | containers.h | N 列网格，子控件按行优先填格子 |
| 布局 | Spacer | text.h | 空行：默认行间距，或固定像素高度 |
| 布局 | Separator | text.h | 一条水平分隔线 |
| 布局 | IndentText | text.h | 缩进一行的正文，用来表现层级 |
| 数据展示 | MeterText | text.h | 「标签: 值 / 上限」+ 细进度条，颜色按比例自动变 |
| 数据展示 | Table | data.h | 多列表格，每帧回调填行，行可拖/可接 |
| 数据展示 | TreeNode | data.h | 可折叠树节点，自带子控件列表 |
| 数据展示 | TabBar | data.h | 页签容器，每个页签一个 `Content` |
| 数据展示 | Plot | data.h | 折线图，画一块外部 `float` 数组 |
| 数据展示 | ProgressBar | controls.h | 进度条，`fraction` 会被夹到 [0,1] |
| 数据展示 | Spinner | controls.h | 转圈动画，纯绘制、不占状态 |
| 按钮 | Button | button.h | 点一下执行一次回调，可禁用/提示/改色 |
| 按钮 | ToggleButton | button.h | 开关按钮，点击把"取反后的状态"交给回调 |
| 输入 | TextInput | controls.h | 单行文本输入，直接编辑 `std::string` |
| 输入 | Checkbox | controls.h | 复选框，点击把新状态交给回调 |
| 输入 | RadioGroup | controls.h | 单选组：一串选项里选一个下标 |
| 输入 | SliderInt | controls.h | 整数滑条，范围 `[minimum, maximum]` |
| 输入 | SliderFloat | controls.h | 浮点滑条，可给显示格式 |
| 输入 | InputInt | controls.h | 整数输入框，可键入也可点 +/- |
| 输入 | DragFloat | controls.h | 浮点拖动条（按住左右拖） |
| 输入 | Combo | controls.h | 下拉选择，选项为静态字符串数组 |
| 输入 | ColorEdit | controls.h | RGBA 取色器，点色块弹面板 |
| 交互 | LinkText | text.h | 可点击链接，点了用系统默认浏览器打开 |
| 交互 | DragItem | dragdrop.h | 一条可拖动的条目行 |
| 交互 | DropZone | dragdrop.h | 虚线放置区，悬停高亮、松手回调 |

不属于 `Widget`、但同样要每帧驱动的类型：

| 类型 | 头文件 | 用途 |
| --- | --- | --- |
| App | app.h | 窗口 + GL 上下文 + ImGui + 帧循环 + 日志 + DPI，一个进程一个 |
| AppConfig | app.h | `App::init` 的启动参数，全部有默认值 |
| WindowManager | window_manager.h | 创建/查找/删除窗口，每帧统一渲染，菜单栏、模态框 |
| Window | window.h | 顶层窗口基类，管 ImGui 的 Begin/End 生命周期 |
| PanelWindow | window.h | 内容是一串控件（`Content`）的窗口，绝大多数场景用它 |
| ConsoleWindow | window.h | 命令台：滚动历史 + 一行输入 + 命令表 |

---

## 二、app.h

`AppConfig` 的字段全部有默认值，通常只需改 `title` / `width` / `height`。

| 字段 | 默认值 | 说明 |
| --- | --- | --- |
| `title` | `"Echo"` | GLFW 窗口标题 |
| `width` | `1400` | 初始窗口宽（逻辑像素，见 `scaleToMonitor`） |
| `height` | `900` | 初始窗口高 |
| `vsync` | `true` | 开垂直同步（`glfwSwapInterval(1)`）；关则跑满帧率 |
| `scaleToMonitor` | `true` | 请求 GLFW 按显示器缩放系数放大窗口（150% 屏上 1400x900 → 物理 2100x1350）；仅在 GLFW 提供 `GLFW_SCALE_TO_MONITOR` 宏时生效 |
| `glMajor` | `3` | 请求的 OpenGL 主版本 |
| `glMinor` | `3` | 请求的 OpenGL 次版本；固定使用 core profile |
| `glslVersion` | `"#version 330"` | 传给 ImGui OpenGL3 后端的 GLSL 版本头 |
| `fontPath` | `""`（空） | 字体文件；空则自动探测（见「常见问题」） |
| `fontSize` | `18.0f` | 字体像素高度，仅 `init` 时用一次 |
| `iniPath` | `"imgui.ini"` | ImGui 布局文件（窗口位置/大小/折叠状态）；空串表示不读写磁盘布局 |
| `clearColor` | `ImVec4(0.10f, 0.10f, 0.11f, 1.0f)` | 每帧清屏颜色 |
| `enableKeyboardNav` | `true` | 打开 ImGui 键盘导航（Tab 切焦点） |
| `showErrorDialog` | `true` | 初始化失败时弹系统错误框 |
| `applyDefaultTheme` | `true` | `init` 成功后套用 Echo 默认深色主题（`theme::dark()`） |
| `logPath` | `"echo.log"` | 日志文件；空串表示不写文件（内存缓冲与回调仍然可用） |

| 签名 | 说明 |
| --- | --- |
| `App()` | 构造，不做任何初始化 |
| `~App()` | 析构，内部自动 `shutdown()` |
| `bool init(const AppConfig& config)` | 创建窗口、GL 上下文、ImGui、字体、日志文件；失败返回 `false`；已初始化时直接返回 `true` 并忽略传入配置 |
| `void shutdown()` | 关闭并释放全部资源，可重复调用 |
| `bool running() const` | 窗口还开着吗（点 X 或调用 `requestQuit()` 后变 `false`） |
| `bool beginFrame()` | 收事件、算 dt、清屏、`ImGui::NewFrame()`；返回 `false` 表示该退出循环，此时不要调 `endFrame()` |
| `void endFrame()` | `ImGui::Render()` + 提交绘制 + 交换缓冲 |
| `void requestQuit()` | 请求退出：置 GLFW 关闭标志并让 `running()` 立即变 `false`，下一帧 `beginFrame()` 返回 `false` |
| `double deltaTime() const` | 上一帧耗时（秒），已夹到 0.25 秒以内 |
| `double elapsed() const` | 从 `init` 到现在的总秒数（真实时间，不做夹取） |
| `uint64_t frameIndex() const` | 已经开始的帧数；`init` 后为 0，第一次 `beginFrame()` 之后为 1 |
| `GLFWwindow* window() const` | 底层 GLFW 窗口，未初始化时为 `nullptr` |
| `float contentScale() const` | 显示器内容缩放（100% = 1.0），初始化为 `max(1.0, xscale, yscale)` |
| `const AppConfig& config() const` | 当前配置的只读引用 |
| `const std::string& lastError() const` | 最近一次初始化失败的原因，见下方注意 |
| `void setTitle(const std::string& title)` | 改窗口标题（运行中也可以），同时写回 `config().title` |

注意：

- `App` 不可拷贝（拷贝构造与赋值被删除），一个进程一个。
- `init` 失败时不一定都有 `lastError()`：`glfwInit()` 或建窗失败会写 `lastError()` + 一条 `fatal` 日志并按 `showErrorDialog` 弹框；而 ImGui 后端（`ImGui_ImplGlfw_InitForOpenGL` / `ImGui_ImplOpenGL3_Init`）初始化失败只写一条 `error` 日志，`lastError()` 保持为空。
- `init` 里设置日志文件的规则是"`logPath` 非空且当前还没有日志文件"，所以先用 `log::setFile()` 指定过路径时，`AppConfig::logPath` 会被忽略。

---

## 三、window.h

`Window` 负责 ImGui 的 Begin/End 生命周期：`Begin()` 一定配一个 `End()`（包括被折叠时），折叠/隐藏时不执行 `draw()`，并记录上一帧是否真的画出来（`visible()`）。尺寸与位置只在**第一次**生效（`ImGuiCond_FirstUseEver`），之后来自 `imgui.ini`。

| 签名 | 说明 |
| --- | --- |
| `explicit Window(std::string title, ImVec2 size = ImVec2(0, 0))` | `size` 为 `(0,0)` 表示交给 ImGui 自行决定 |
| `virtual void draw() = 0` | 画内容；调用时已处于 `ImGui::Begin()` 内部，纯虚，必须实现 |
| `virtual void drawMenuBar()` | 画窗口菜单栏，默认空实现；只在 `flags` 含 `ImGuiWindowFlags_MenuBar` 时被调用 |
| `void render()` | 完整的一帧：Begin → [菜单栏] → draw → End；由 `WindowManager` 调用 |
| `virtual const char* type() const` | 类型名，默认 `"Window"` |
| `void setTitle(std::string title)` | 改标题，下一帧生效 |
| `const std::string& title() const` | 当前标题 |
| `void setSize(ImVec2 size)` | 首次显示的尺寸；只有 `x`、`y` 都大于 0 才会提交给 ImGui |
| `ImVec2 size() const` | 记录的首次尺寸 |
| `void setPosition(ImVec2 position)` | 首次显示的位置（屏幕坐标），只在第一次生效 |
| `void setFlags(ImGuiWindowFlags flags)` | ImGui 窗口标志，例如 `ImGuiWindowFlags_NoCollapse \| ImGuiWindowFlags_MenuBar` |
| `ImGuiWindowFlags flags() const` | 当前标志 |
| `uint64_t id() const` | 窗口 id，由 `WindowManager::add` 分配，全管理器内唯一 |
| `bool visible() const` | 上一帧是否真的画出来了（没被关掉、也没折叠） |
| `bool open` | 公开字段，默认 `true`；X 按钮与菜单勾选都写它。为 `false` 时 `render()` 直接返回，连 `Begin()` 都不调 |

`PanelWindow`：内容是一串控件，90% 的场景用它。

| 签名 | 说明 |
| --- | --- |
| `explicit PanelWindow(std::string title, ImVec2 size = ImVec2(600, 400))` | |
| `template <class T, class... Args> T& add(Args&&... args)` | 追加控件，参数原样转发给 `T` 的构造函数，返回 `T&` |
| `void clear()` | 清空全部控件（析构） |
| `Content& content()` | 拿到底层控件列表（批量操作/测试用） |
| `void draw() override` | 逐帧渲染 `content()` |
| `const char* type() const override` | 返回 `"PanelWindow"` |

| 签名 | 说明 |
| --- | --- |
| `using TextWindow = PanelWindow` | 旧名字，完全等价于 `PanelWindow` |

`ConsoleWindow`：滚动历史 + 一行输入 + 命令表。构造时自动设置 `ImGuiWindowFlags_MenuBar`。输入框回车后自动清空并重新获得焦点；新行出现且 `setAutoScroll(true)` 时自动滚到底部；历史超过 2000 行时丢弃最旧的 500 行。

| 签名 | 说明 |
| --- | --- |
| `using Args = std::vector<std::string>` | 命令参数表 |
| `using Command = std::function<void(const Args&)>` | 命令回调 |
| `explicit ConsoleWindow(std::string title = "Console", ImVec2 size = ImVec2(600, 400))` | |
| `void addLine(std::string line)` | 追加一行历史（自动滚到底部） |
| `void registerCommand(std::string name, Command command)` | 注册命令，同名覆盖；命令名区分大小写 |
| `bool execute(const std::string& line)` | 解析并执行一行；命令不存在时自动写一条错误行并返回 `false`；空行直接返回 `false` |
| `void clearHistory()` | 清空历史 |
| `const std::vector<std::string>& history() const` | 历史行的只读引用 |
| `void setAutoScroll(bool autoScroll)` | 新内容是否自动滚到底部，默认开 |
| `void setPrompt(std::string prompt)` | 输入行的提示前缀，默认 `">"`，只影响回显 |
| `void draw() override` | 历史区（子窗口）+ 底部输入框 |
| `void drawMenuBar() override` | 提供 Console → Clear / Auto scroll 菜单 |
| `const char* type() const override` | 返回 `"ConsoleWindow"` |
| `static Args split(const std::string& line)` | 按空白切分，双引号包裹的空格保留；引号本身不进入参数，不支持转义 |

注意：`execute()` 只负责解析与执行，不会为你回显命令行；回显是输入框提交路径做的（先 `addLine(prompt + " " + line)` 再 `execute(line)`）。手动调用 `execute()` 时，只有"未知命令"这一种情况会自己写错误行。

---

## 四、window_manager.h

持有全部窗口（`unique_ptr`），顺序 = 添加顺序 = 绘制顺序（后加的画在上面）。

| 签名 | 说明 |
| --- | --- |
| `template <class T, class... Args> T& add(Args&&... args)` | 创建窗口并返回稳定引用；参数原样转发给 `T` 的构造函数，同时分配 id；`T` 必须是 `Window` 的派生类 |
| `Window* find(uint64_t id)` | 按 id 找窗口，没有返回 `nullptr` |
| `Window* findByTitle(std::string_view title)` | 按标题找窗口（重复时返回第一个），没有返回 `nullptr` |
| `bool remove(uint64_t id)` | 删除窗口；在某个窗口的 `draw()` 里调用会延迟到本帧渲染结束 |
| `void clear()` | 删除全部窗口 |
| `size_t size() const` | 当前窗口数 |
| `std::vector<Window*> windows()` | 全部窗口裸指针的只读快照，顺序 = 绘制顺序 |
| `void renderAll()` | 画菜单栏（可选）、全部窗口、模态框、右键菜单；每帧调一次 |
| `void setMenuBar(bool enabled)` | 顶部菜单栏（Windows / Quit）开关，默认开 |
| `bool menuBar() const` | 菜单栏当前开关 |
| `void setOnQuit(std::function<void()> onQuit)` | 菜单里点 Quit 时做什么（一般是 `app.requestQuit()`）；**不设置就没有 Quit 菜单项** |
| `void showMessage(std::string title, std::string text)` | 弹一个模态信息框，下一次 `renderAll()` 时出现，按 OK / Enter / Esc 关闭；关闭后实现会清空标题与正文 |

注意：`add<T>()` 返回的引用在对应窗口被 `remove()` / `clear()` 之前一直有效；在窗口的 `draw()` 里删窗口是安全的（延迟删除）。

---

## 五、widget.h

两种"容器"别混：`Window` 是有标题栏/位置/折叠状态的窗口，`Content` 是窗口内部"一串控件"，只管顺序。

| 签名 | 说明 |
| --- | --- |
| `virtual ~Widget() = default` | 虚析构 |
| `virtual void render() = 0` | 画自己，每帧一次；必须"画完就返回"，不要阻塞，也不要自己开窗口 |
| `virtual const char* type() const` | 类型名，默认 `"Widget"`，给调试/测试用 |
| `using WidgetPtr = std::unique_ptr<Widget>` | 控件所有权类型 |
| `template <class T, class... Args> T& add(Args&&... args)` | 追加控件，参数原样转发给 `T` 的构造函数（`std::make_unique<T>(...)`），返回 `T&`；`T` 必须是 `Widget` 派生类 |
| `void clear()` | 清空（控件析构，顺序是添加顺序的逆序） |
| `bool empty() const` | 是否没有控件 |
| `size_t size() const` | 控件数 |
| `Widget* at(size_t index)` | 按下标取控件，越界返回 `nullptr` |
| `bool erase(size_t index)` | 删除第 `index` 个控件，返回是否删掉了 |
| `void render()` | 按顺序绘制全部控件（由窗口调用，一般不用手动调） |
| `template <class Fn> void forEach(Fn&& fn)` | 按顺序遍历，`fn` 收到 `Widget&` |
| `using WidgetList = Content` | 旧名字，避免已有代码报错 |

控件的生命周期由 `Content` 管理，不要自己 `delete`；`add` 返回的引用在对应控件被 `erase()` / `clear()` 之前有效。

---

## 六、source.h

`Source<T>` 是"一个能取出 `T` 的对象"，同一套语法支持快照 / 指针 / 可调用对象三种来源。控件每帧 `get()` 一次，所以数据变了不需要重建控件。

| 签名 | 说明 |
| --- | --- |
| `Source()` | 空来源，`get()` 返回 `T{}`（数字 0、字符串空串） |
| `template <class U> Source(U&& v)` | 快照：把值存起来，每次返回同一份拷贝；接受任何能转换成 `T` 的类型（所以 `const char*` 可以直接当 `std::string`），但排除"指向 `T` 的指针"和"返回 `T` 的可调用对象" |
| `Source(const T* ptr)` | 指针：每次 `get()` 解引用；指针为空时返回 `T{}`，不会崩 |
| `template <class F> Source(F fn)` | 可调用对象：每次 `get()` 调用一次；会被拷贝进 `std::function` |
| `Source(std::function<T()> fn)` | 显式接收一个 `std::function`（例如从别处接过来的回调） |
| `T get() const` | 取当前值，控件每帧调用一次；返回的是副本 |
| `bool valid() const` | 是否装了东西（空 `Source` 的 `get()` 返回默认值） |
| `template <class T> Source<T> value(T v)` | 显式构造快照来源，用于消除重载歧义 |
| `template <class T> Source<T> ref(const T* ptr)` | 显式构造指针来源：`Source<int> s = ref(&counter);` |

注意：

- 捕获请用引用捕获 `[&]` 或拷贝小对象；控件必须活得比被捕获的变量短（通常"同一个作用域内"）。
- 每帧都会调用，别在 lambda 里读文件、加锁、遍历大容器。
- `get()` 返回 `T` 的副本；`std::string` 会因此每帧构造一次。每秒上万行文本时请自己缓存成字符串再传快照。

---

## 七、text.h

全部是"只显示、不交互"的控件（`LinkText` 例外）。所有文本参数都是 `Source<std::string>`。

| 类型 | 签名 | 说明 |
| --- | --- | --- |
| PlainText | `explicit PlainText(Source<std::string> text)` | 原样显示，不做格式化；换行自己塞 `'\n'` |
| ColoredText | `ColoredText(Source<std::string> text, const ImVec4& color)` | 指定颜色的正文 |
| HeadingText | `explicit HeadingText(Source<std::string> text)` | 小节标题 + 分隔线，颜色默认用 `theme::accent()` |
| HeadingText | `HeadingText(Source<std::string> text, const ImVec4& color)` | 指定标题颜色 |
| Separator | `Separator()` | 一条水平分隔线（`ImGui::Separator()`） |
| IndentText | `explicit IndentText(Source<std::string> text)` | 缩进 20 像素的正文 |
| IndentText | `IndentText(Source<std::string> text, float indentPixels)` | 指定缩进像素 |
| MeterText | `MeterText(Source<std::string> label, Source<float> value, Source<float> maximum = 100.0f, Source<std::string> suffix = std::string{}, bool showBar = true)` | 浮点指标条：`showBar = false` 时只画文字 |
| MeterText | `MeterText(Source<std::string> label, int value, int maximum, Source<std::string> suffix = std::string{})` | 整数版，一次性字面量 |
| MeterText | `MeterText(Source<std::string> label, const int* value, int maximum, Source<std::string> suffix = std::string{})` | 整数版，传指针即可实时更新 |
| SpeakerText | `SpeakerText(Source<std::string> speaker, Source<std::string> message)` | 「说话者: 内容」，说话者颜色默认 `ImVec4(0.60f, 0.80f, 1.00f, 1.00f)` |
| SpeakerText | `SpeakerText(Source<std::string> speaker, Source<std::string> message, const ImVec4& speakerColor)` | 指定说话者颜色 |
| ListItemText | `explicit ListItemText(Source<std::string> text)` | 默认前缀 `"• "`（U+2022 + 空格） |
| ListItemText | `ListItemText(Source<std::string> text, std::string prefix)` | 换前缀，例如 `"OK "` / `"FAIL "` |
| ListItemText | `ListItemText(Source<std::string> text, std::string prefix, const ImVec4& color)` | 再指定颜色 |
| Spacer | `Spacer()` | 一个行间距（`ImGui::Spacing()`） |
| Spacer | `explicit Spacer(float height)` | `height > 0` 时是固定像素的空行，否则等同默认行间距 |
| HighlightText | `HighlightText(Source<std::string> text, const ImVec4& background)` | 背景色块 + 白字 |
| HighlightText | `HighlightText(Source<std::string> text, const ImVec4& background, const ImVec4& foreground)` | 再指定前景色 |
| WrappedText | `explicit WrappedText(Source<std::string> text)` | 自动折行正文 |
| KeyValueText | `KeyValueText(Source<std::string> key, Source<std::string> value, float keyWidth = 120.0f)` | 键用弱化色，值从 `keyWidth` 这个**横坐标偏移**开始（相对窗口内容起点，不是键宽） |
| MutedText | `explicit MutedText(Source<std::string> text)` | 灰色小字（`ImGui::TextDisabled`） |
| LinkText | `LinkText(Source<std::string> label, std::string url)` | 可点击链接，实现是 `ImGui::TextLinkOpenURL` |
| Badge | `Badge(Source<std::string> text, const ImVec4& background)` | 圆角小标签，圆角半径 = 高度的一半，文字固定白色 |

`MeterText` 的颜色规则（`ratio = value / maximum`，先夹到 [0,1]）：

| 条件 | 颜色 |
| --- | --- |
| `ratio > 0.6` | `colors::green()` 绿 |
| `0.3 < ratio <= 0.6` | `colors::warning()` 黄 |
| `ratio <= 0.3` | `colors::red()` 红 |
| `maximum <= 0` | `colors::muted()` 灰，且比例按 0 处理（进度条画成 0） |

`MeterText` 的文本格式是 `标签: 值+后缀 / 上限+后缀`（后缀会出现在两处）：整数版 `%.0f`，浮点版 `%.1f`；`showBar` 为真的细条高度固定 4 像素、宽度占满可用宽度。

---

## 八、button.h

两种语义别混：`Button` 是"动作"（点一次执行一次回调），`ToggleButton` 是"开关"（维护一个开/关状态，点一下翻转）。

| 签名 | 说明 |
| --- | --- |
| `Button(Source<std::string> label, std::function<void()> onClick = {}, ImVec2 size = ImVec2(0, 0), bool small = false, std::string tooltip = {})` | `onClick` 可为空（只显示不响应）；`size` 为 `(0,0)` 表示按内容自适应；`small = true` 用 `ImGui::SmallButton`（此时 `size` 不生效）；`tooltip` 空串表示没有 |
| `void setEnabled(bool enabled)` | 禁用后按钮变灰、不触发回调 |
| `bool enabled() const` | 当前是否可用，默认 `true` |
| `void setColor(const ImVec4& color)` | 自定义底色；只覆盖 `ImGuiCol_Button`，悬停/按下仍是主题色 |
| `void setTooltip(std::string tooltip)` | 悬停提示；空串表示没有 |
| `ToggleButton(Source<std::string> label, Source<bool> state, std::function<void(bool)> onChange, ImVec2 size = ImVec2(0, 0))` | 开着时用 `theme::accent()` 填充，点击把 `!state` 交给 `onChange`（回调必填，状态由调用方保存） |

`ToggleButton` 的状态来源可以是快照 / 指针 / lambda，`onChange` 里负责把值写回去；只传快照的话界面上不会留下状态。

---

## 九、containers.h

`Content` 天生是纵向流（`add` 一个就换一行），这两个容器把子控件排成一行 / 网格。它们自己也是 `Widget`，可以互相嵌套。

| 类型 | 签名 | 说明 |
| --- | --- | --- |
| Row | `explicit Row(float spacing = -1.0f)` | 横向排列；`spacing` 是控件间距（像素），负值表示用主题默认 `ItemSpacing` |
| Row | `template <class T, class... Args> T& add(Args&&... args)` | 转发给 `Content::add`，参数原样给 `T` 的构造函数 |
| Row | `void render() override` | 对除第一个以外的子控件先调 `ImGui::SameLine(0, spacing)` |
| Row | `Content& children()` | 底层子控件列表 |
| Grid | `explicit Grid(int columns, ImVec2 size = ImVec2(0, 0), bool borders = false)` | 网格；`columns` 最小按 1 处理；`size` 为 `(0,0)` 表示宽度自适应、高度按内容；`borders = true` 画单元格边框 |
| Grid | `template <class T, class... Args> T& add(Args&&... args)` | 转发给 `Content::add` |
| Grid | `Content& children()` | 底层子控件列表 |
| Grid | `int columns() const` | 当前列数 |
| Grid | `void setColumns(int columns)` | 改列数，小于 1 按 1 处理 |

用 ImGui 表格实现，所以列宽天然对齐：所有列等宽（`ImGuiTableFlags_SizingStretchSame`，键盘/计算器这类按钮才对得齐）。`ImVec2(-1, h)` 这类负宽度是 ImGui 的约定，表示"占满可用空间"，在网格里就是占满单元格。没有子控件时 `Grid::render()` 什么都不画。

---

## 十、controls.h

这一类的共同约定：控件的值来自 `Source`（只读地显示），用户操作后通过 `onChange` 把新值交还给调用方——库自己不保存业务状态。传快照时拖动会弹回去，因为没有地方保存新值。

| 类型 | 签名 | 说明 |
| --- | --- | --- |
| TextInput | `TextInput(std::string label, std::string* buffer, size_t capacity = 256)` | 直接编辑 `buffer` 指向的字符串；`buffer` 为空指针时整个控件不画 |
| TextInput | `TextInput(std::string label, std::string* buffer, std::function<void()> onSubmit, size_t capacity = 256)` | 回车时调用 `onSubmit` 并把焦点自动放回输入框（适合命令输入） |
| TextInput | `void setWidth(float width)` | 控件宽度（像素），默认 240；负数表示占满剩余宽度 |
| TextInput | `void setHint(const char* hint)` | 悬停提示；`nullptr` 表示没有。**只存指针不拷贝**，传字符串字面量最安全 |
| Checkbox | `Checkbox(Source<std::string> label, Source<bool> state, std::function<void(bool)> onChange)` | 点击后把新状态交给 `onChange` |
| RadioGroup | `RadioGroup(Source<std::string> label, std::vector<std::string> options, Source<int> index, std::function<void(int)> onChange, bool horizontal = false)` | `options` 里选一个，`index` 是当前选中下标；`horizontal = true` 时排成一行，否则一行一个；`options` 为空时整个控件不画 |
| SliderInt | `SliderInt(Source<std::string> label, Source<int> value, int minimum, int maximum, std::function<void(int)> onChange = {})` | 整数滑条 |
| SliderInt | `void setWidth(float width)` | 默认 200 |
| SliderFloat | `SliderFloat(Source<std::string> label, Source<float> value, float minimum, float maximum, std::function<void(float)> onChange = {}, const char* format = "%.2f")` | 浮点滑条；`format` 只存指针 |
| SliderFloat | `void setWidth(float width)` | 默认 200 |
| InputInt | `InputInt(Source<std::string> label, Source<int> value, std::function<void(int)> onChange = {}, int step = 1, int stepFast = 100)` | 整数输入框，可键入也可点 +/- |
| InputInt | `void setWidth(float width)` | 默认 200 |
| DragFloat | `DragFloat(Source<std::string> label, Source<float> value, float speed = 1.0f, float minimum = 0.0f, float maximum = 0.0f, std::function<void(float)> onChange = {}, const char* format = "%.2f")` | 浮点拖动条；`minimum >= maximum`（含默认的 0 与 0）表示不限制范围 |
| DragFloat | `void setWidth(float width)` | 默认 200 |
| ProgressBar | `ProgressBar(Source<float> fraction, Source<std::string> overlay = std::string{}, ImVec2 size = ImVec2(-1, 0))` | `fraction` 夹到 [0,1]；`overlay` 显示在条内（空串则不显示）；宽度 `-1` 表示占满可用宽度 |
| ProgressBar | `void setSize(ImVec2 size)` | 改尺寸 |
| Spinner | `explicit Spinner(float radius = 8.0f, const ImVec4& color = colors::info(), float speed = 4.0f)` | 转圈动画，用 `Dummy` 占 `2r x 2r` 的位置再画圆弧 |
| Combo | `Combo(Source<std::string> label, const char* const* items, int count, Source<int> index, std::function<void(int)> onChange = {})` | 下拉选择；`items` 必须是活到控件销毁的字符串指针数组（通常写 `static` 数组）；`index` 越界时按 0 显示；选中同一项不触发回调 |
| Combo | `void setWidth(float width)` | 默认 200 |
| ColorEdit | `ColorEdit(Source<std::string> label, Source<ImVec4> color, std::function<void(ImVec4)> onChange)` | RGBA 取色器，内部用 `ImGui::ColorEdit4` |

`TextInput` 的 `capacity` 语义：只有在 `ImGui::InputText` 报告"值变化/回车"时才把过长字符串截断。带 `onSubmit` 的构造使用 `ImGuiInputTextFlags_EnterReturnsTrue`，因此只在回车时截断；不带 `onSubmit` 的构造会在编辑过程中截断。ImGui 本身不限制输入长度。

注意：`RadioGroup` 内部的表格使用固定 id `##radio_group`，同一窗口里放多个 `RadioGroup` 会共用这个 id，相互干扰。

---

## 十一、data.h

| 类型 | 签名 | 说明 |
| --- | --- | --- |
| Table::Column | `struct Column { std::string title; float width = 0.0f; }` | `width = 0` 表示该列使用 `ImGuiTableColumnFlags_WidthStretch`（均分可用宽度）；`width > 0` 表示固定像素宽 |
| Table::RowOptions | `struct RowOptions { DragPayload drag; std::vector<std::string> accept; std::function<void(const DragPayload&)> onDrop; }` | 单行的拖拽/放置行为；默认构造 = 普通只读行 |
| Table | `explicit Table(std::vector<Column> columns, ImGuiTableFlags flags = ImGuiTableFlags_Borders \| ImGuiTableFlags_RowBg \| ImGuiTableFlags_Resizable \| ImGuiTableFlags_SizingStretchProp)` | 多列表格；`columns` 为空时不画任何东西 |
| Table | `void setRows(std::function<void(Table&)> rows)` | 设置"如何填行"，每帧在表格内部调用一次 |
| Table | `void row(const std::vector<std::string>& cells)` | 追加一行文本，必须在 `rows` 回调里调用 |
| Table | `void row(std::initializer_list<std::string> cells)` | 同上，方便写 `t.row({"a", "b"})` |
| Table | `void row(const std::vector<std::string>& cells, const RowOptions& options)` | 带拖拽/放置能力的行 |
| Table | `size_t columnCount() const` | 当前列数 |
| TreeNode | `explicit TreeNode(Source<std::string> label, bool defaultOpen = false)` | 可折叠节点；`defaultOpen` 只决定第一帧 |
| TreeNode | `Content& children()` | 子控件列表（和 `Content` 一样用） |
| TreeNode | `template <class T, class... Args> T& add(Args&&... args)` | 转发给 `Content::add` |
| TreeNode | `void setOpen(bool open)` | 程序控制展开/折叠，下一次 `render()` 生效 |
| TreeNode | `bool open() const` | 上一次渲染时的真实展开状态；首次渲染前等于 `defaultOpen` |
| TabBar | `explicit TabBar(std::vector<std::string> titles)` | 页签容器，每个页签一个 `Content` |
| TabBar | `Content& tab(size_t index)` | 第 `index` 个页签的内容；越界返回一个共享的空占位 `Content`（每次调用先清空，往里面 add 的东西不会保留） |
| TabBar | `size_t count() const` | 页签数 |
| TabBar | `void select(size_t index)` | 程序选中某一页，下一次 `render()` 时该页签带 `ImGuiTabItemFlags_SetSelected` |
| Plot | `Plot(Source<std::string> caption, const float* values, int count, float minimum = 0.0f, float maximum = 1.0f, ImVec2 size = ImVec2(0, 60))` | 折线图；`values` 必须活到控件销毁；`count <= 0` 或指针为空时不画；`caption` 同时是左上角标签和 `PlotLines` 的 overlay |
| Plot | `void setRange(float minimum, float maximum)` | 改纵轴范围 |
| Plot | `void setSize(ImVec2 size)` | 改图表尺寸 |

约定：`Table` 的行回调里只能在表格内部调 `row()`，并且每帧都要重新填一遍（这是 ImGui 的立即模式特性）。`RowOptions` 里 `drag` 非空表示整行可拖（拖动预览是第一列文本），`accept` 非空表示整行可接、松手时回调 `onDrop`。`RowOptions` 的示例写法用了指定初始化器（`.drag = ...`），需要 C++20。

---

## 十二、dragdrop.h

ImGui 的拖拽载荷是上下文级的、不是窗口级的，所以天然支持跨窗口：A 窗口里开始拖，B 窗口里松手即可。Echo 只搬运"你是谁"（类型名 + 字节），不碰你的容器——移动语义必须由 `onDrop` 完成。

| 类型 | 签名 | 说明 |
| --- | --- | --- |
| DragPayload | `DragPayload()` | 空载荷（`empty()` 为真，`type()` 为空串） |
| DragPayload | `DragPayload(std::string type, std::vector<unsigned char> bytes)` | 原始字节构造，一般不用，改用 `of<T>()` |
| DragPayload | `template <class T> static DragPayload of(std::string type, const T& value)` | 把可平凡拷贝的值打包；`T` 必须 trivially copyable（`static_assert` 拦住 `std::string` 之类） |
| DragPayload | `template <class T> std::optional<T> get() const` | 取出数据；大小或类型标记不匹配时返回 `std::nullopt`，不会越界读 |
| DragPayload | `const std::string& type() const` | 类型名，决定"谁能接" |
| DragPayload | `bool empty() const` | 是否空载荷（空载荷不能当拖拽源） |
| DragPayload | `size_t size() const` | 数据部分字节数（不含内部头部） |
| DragPayload | `const unsigned char* data() const` | 数据指针（不含内部头部），空载荷返回 `nullptr` |
| DragPayload | `const unsigned char* raw() const` | 含内部头部的原始缓冲，交给 ImGui 用 |
| DragPayload | `size_t rawSize() const` | 含头部的总字节数 |
| DragPayload | `static DragPayload fromImGui(const ImGuiPayload& payload)` | 把 ImGui 交付的载荷复制出来；只在 Accept 的那一帧有效，所以必须立刻拷贝 |
| dnd | `bool beginSource(const DragPayload& payload, const char* previewText = nullptr, ImGuiDragDropFlags flags = ImGuiDragDropFlags_None)` | 把"上一个 ImGui 控件"变成拖拽源，返回 true 表示正在拖；`previewText` 为空时预览显示类型名；必须在某个控件提交之后立刻调用 |
| dnd | `std::optional<DragPayload> accept(std::initializer_list<const char*> types, ImGuiDragDropFlags flags = ImGuiDragDropFlags_None)` | 在当前光标处接受拖拽，返回"松手那一刻"的数据；没松手/类型不匹配返回空 |
| dnd | `std::optional<DragPayload> accept(const std::vector<std::string>& types, ImGuiDragDropFlags flags = ImGuiDragDropFlags_None)` | 同上，类型用 `vector` 给（控件内部用这个重载） |
| dnd | `bool canAccept(std::initializer_list<const char*> types)` | 只查询"正被拖动且类型可接受"，用于悬停高亮，不消费 |
| dnd | `bool canAccept(const std::vector<std::string>& types)` | 同上，`vector` 版本 |
| DragItem | `DragItem(Source<std::string> label, DragPayload payload, bool selected = false)` | 一条可拖动的 `Selectable` 行，拖动预览就是该行文字 |
| DragItem | `void setPayload(DragPayload payload)` | 换载荷（例如列表顺序变化后重建时） |
| DropZone | `DropZone(std::vector<std::string> types, Handler onDrop, Source<std::string> placeholder = std::string{"拖到这里"}, ImVec2 size = ImVec2(-1, 72))` | 虚线放置区；`Handler = std::function<void(const DragPayload&)>`；宽度负数表示占满剩余宽度 |
| DropZone | `bool hovered() const` | 本帧是否正被"可接受的载荷"悬停（需要 `render()` 跑过才会更新） |

注意：`DropZone` 的内部命中区使用固定 id `##echo_dropzone`，同一窗口里放多个 `DropZone` 会共用这个 id；把它们放进不同窗口，或外面套一层 `TreeNode`（它会 `PushID`）即可隔离。

---

## 十三、theme.h

两层概念别混：`colors::xxx()` 只是一组颜色常量，不改任何全局状态；`theme::xxx()` 真正动 ImGui 的样式表。

| 签名 | 说明 |
| --- | --- |
| `colors::white()` | `ImVec4(1.00f, 1.00f, 1.00f, 1.00f)` |
| `colors::black()` | `ImVec4(0.00f, 0.00f, 0.00f, 1.00f)` |
| `colors::text()` | `ImVec4(0.90f, 0.91f, 0.92f, 1.00f)` |
| `colors::muted()` | `ImVec4(0.55f, 0.57f, 0.60f, 1.00f)` |
| `colors::gold()` | `ImVec4(1.00f, 0.80f, 0.20f, 1.00f)` |
| `colors::red()` | `ImVec4(1.00f, 0.28f, 0.28f, 1.00f)` |
| `colors::green()` | `ImVec4(0.35f, 0.90f, 0.40f, 1.00f)` |
| `colors::blue()` | `ImVec4(0.28f, 0.55f, 1.00f, 1.00f)` |
| `colors::yellow()` | `ImVec4(1.00f, 0.90f, 0.10f, 1.00f)` |
| `colors::orange()` | `ImVec4(1.00f, 0.60f, 0.15f, 1.00f)` |
| `colors::purple()` | `ImVec4(0.62f, 0.35f, 0.95f, 1.00f)` |
| `colors::gray()` | `ImVec4(0.50f, 0.50f, 0.50f, 1.00f)` |
| `colors::cyan()` | `ImVec4(0.20f, 0.85f, 0.90f, 1.00f)` |
| `colors::pink()` | `ImVec4(1.00f, 0.45f, 0.70f, 1.00f)` |
| `colors::danger()` | `ImVec4(0.90f, 0.30f, 0.30f, 1.00f)` |
| `colors::success()` | `ImVec4(0.35f, 0.85f, 0.45f, 1.00f)` |
| `colors::warning()` | `ImVec4(0.95f, 0.72f, 0.20f, 1.00f)` |
| `colors::info()` | `ImVec4(0.45f, 0.70f, 1.00f, 1.00f)` |
| `void theme::dark()` | 深色主题（默认），把样式表整体替换成 Echo 调过的版本 |
| `void theme::light()` | 浅色主题 |
| `void theme::setAccent(const ImVec4& color)` | 强调色：按钮、标题分隔线、进度条、选中项、标题栏都用它；默认 `ImVec4(0.30f, 0.56f, 0.95f, 1.00f)` |
| `ImVec4 theme::accent()` | 当前强调色（纯 getter，不改样式表） |
| `void theme::setFontScale(float scale)` | 全局字号缩放，1.0 = 原样；`scale <= 0` 按 1.0 处理 |
| `float theme::fontScale()` | 当前字号缩放（纯 getter） |
| `void theme::rounding(float radius)` | 只改圆角：窗口 / 子窗口 / 边框 / 弹出层 / 抓取块五个 |

注意：

- `dark()` / `light()` / `setAccent()` / `setFontScale()` 每次都从一张全新的 `ImGuiStyle` 重建整张样式表，所以可以反复切、样式不会叠加；反过来说，`rounding()` 是直接改当前样式表，之后任何一次重建（例如再调 `setAccent`）都会把它冲掉。
- 字号缩放同时设置 `ImGuiStyle::FontScaleDpi`，并在 `scale != 1.0` 时再 `ScaleAllSizes(scale)`（尺寸一起放大）。DPI 缩放不需要调这个，后端会按显示器密度栅格化字形。

---

## 十四、log.h

默认以 GUI 子系统（`-mwindows`）链接时 stdout/stderr 是黑洞，所以 Echo 自带三条出路：内存环形缓冲、文件、回调。

| 类型/签名 | 说明 |
| --- | --- |
| `enum class LogLevel { Debug = 0, Info, Warn, Error, Fatal }` | 级别：`Debug < Info < Warn < Error < Fatal` |
| `const char* toString(LogLevel level)` | 级别转可读字符串：`"DEBUG"` / `"INFO"` / `"WARN"` / `"ERROR"` / `"FATAL"`，未知返回 `"?"` |
| `struct LogEntry { LogLevel level = LogLevel::Info; double time = 0.0; std::string text; }` | 一条日志；`time` 是"进程内日志系统启动后经过的秒数"，不是墙上时间 |
| `void log::write(LogLevel level, const std::string& text)` | 写一条：进环形缓冲、写文件（如果开了）、调回调（如果设了）；线程安全 |
| `void log::debug(const std::string& text)` | `write(Debug, ...)` |
| `void log::info(const std::string& text)` | `write(Info, ...)` |
| `void log::warn(const std::string& text)` | `write(Warn, ...)` |
| `void log::error(const std::string& text)` | `write(Error, ...)` |
| `void log::fatal(const std::string& text)` | `write(Fatal, ...)` |
| `std::vector<LogEntry> log::entries()` | 当前缓冲里的全部日志（旧 → 新），返回拷贝 |
| `size_t log::size()` | 当前缓冲条数 |
| `void log::clear()` | 清空缓冲（不影响文件） |
| `void log::setCapacity(size_t capacity)` | 环形缓冲上限，默认 512；传 0 按 1 处理；超出丢最旧的 |
| `void log::setFile(const std::string& path)` | 设置文件输出；传空串关闭；打开失败会静默关掉（不影响程序） |
| `const std::string& log::file()` | 当前文件路径，未开启时为空串 |
| `void log::setCallback(std::function<void(const LogEntry&)> callback)` | 每条日志的回调，用于把日志实时喂给界面；回调在锁外执行 |
| `std::string log::lastText()` | 最近一条日志的正文，没有则为空串 |

注意：文件以追加模式（`std::ios::app`）打开，每次启动不会清空旧日志；每行格式是 `[秒.毫秒] LEVEL: 正文`。在 `setCallback` 的回调里调用 `entries()` / `size()` 是安全的。

---

## 十五、input.h

全部是对 ImGui 输入状态的薄包装，好处是焦点自动正确：文本框正在输入时 ImGui 会先吃掉按键，这里再查就查不到，不会出现"在输入框里打字又触发全局快捷键"。

| 签名 | 说明 |
| --- | --- |
| `bool input::keyDown(ImGuiKey key)` | 此刻是否按住（持续为真） |
| `bool input::keyPressed(ImGuiKey key, bool repeat = false)` | 这一帧是否刚按下（只真一帧）；`repeat = true` 时按住会自动重复 |
| `bool input::keyReleased(ImGuiKey key)` | 这一帧是否刚松开 |
| `bool input::mouseDown(int button = 0)` | 鼠标键是否按住；0 = 左键，1 = 右键，2 = 中键 |
| `bool input::mouseClicked(int button = 0)` | 这一帧是否刚点下 |
| `ImVec2 input::mousePosition()` | 鼠标位置（屏幕坐标） |
| `ImVec2 input::mouseDelta()` | 本帧鼠标位移 |
| `float input::mouseWheel()` | 滚轮增量（向上为正） |
| `double input::time()` | ImGui 时钟（秒），从第一帧开始计，做动画/防抖比 `std::chrono` 方便 |

这些函数读的是 ImGui 当前帧的输入状态——`beginFrame()` 里先 `glfwPollEvents()` 再 `ImGui_ImplGlfw_NewFrame()`，所以只有落在 `beginFrame()` / `endFrame()` 之间查询才是本帧的按键。

---

## 十六、layout.h

给"自己继承 `Window` 写 `draw()`"的人用的便利函数，直接作用于 ImGui 光标。用 `Content.add<T>()` 那套控件列表时请用 `containers.h` 的 `Row` / `Grid`。

| 签名 | 说明 |
| --- | --- |
| `void layout::sameLine(float offsetFromStart = 0.0f, float spacing = -1.0f)` | 不换行，继续在同一行往右排；`offsetFromStart` 是横坐标，`spacing = -1` 用默认间距 |
| `void layout::spacing()` | 一个空行（行高） |
| `void layout::separator()` | 一条分隔线 |
| `void layout::indent(float width = 20.0f)` | 临时缩进，记得配 `unindent`（或直接用 `IndentText` 控件） |
| `void layout::unindent(float width = 20.0f)` | 取消缩进，宽度要与 `indent` 一致 |

---

## 十七、time.h

界面每帧画一次，但模拟/动画/轮询往往要求"每 1/60 秒算一步"，与帧率无关。`FixedStep` 负责把 `deltaTime` 攒成固定步长。

| 签名 | 说明 |
| --- | --- |
| `explicit FixedStep(double step = 1.0 / 60.0, size_t maxTicksPerFrame = 5)` | `step` 是每次回调的时间间隔；`maxTicksPerFrame` 是单帧最多补几步 |
| `template <class Fn> void advance(double deltaTime, Fn&& tick)` | 累积 `deltaTime` 并按需回调 `tick(step)`，回调次数在 `0..maxTicksPerFrame` 之间 |
| `double alpha() const` | `accumulator / step`，0..1，表示距离下一个模拟步还差多少（做渲染插值用） |
| `double accumulator() const` | 当前累积的未消费时间 |
| `double step() const` | 当前步长 |
| `void setStep(double step)` | 改步长；`step <= 0` 按 `1.0/60.0` 处理 |
| `void reset()` | 把累积量清零 |
| `using FixedTimestep = FixedStep` | 旧名字，完全等价 |

死亡螺旋保护：某帧特别长（断点、拖窗口）时最多只补 `maxTicksPerFrame` 步，剩下的直接丢掉（宁可慢动作，不可卡死）。典型用法见 `time.h` 顶部注释：`step.advance(app.deltaTime(), [&](double dt) { simulate(dt); });` 然后用 `step.alpha()` 做插值。

---

## 十八、screenshot.h

从 OpenGL 后缓冲读像素，与桌面无关，适合放进 CI 做"界面没崩"的冒烟检查。

| 签名 | 说明 |
| --- | --- |
| `bool captureFramebufferBmp(const std::string& path)` | 把当前窗口的显示缓冲保存为 24 位无压缩 BMP，成功返回 `true` |

必须在 `endFrame()` **之后**调用（此时画面已经交换，实现读的是前台缓冲），并且当前线程要有可用的 OpenGL 上下文（`App` 已 `init` 且未 `shutdown`）。没有当前上下文或窗口尺寸为 0 时返回 `false`。需要 PNG 请自己转换。

---

## 十九、echo.h

总入口，不定义任何类型或函数，只是把下面 17 个头文件依次包含，所以引一个 `#include "echo/echo.h"` 就能用全部功能：

`app.h`、`button.h`、`containers.h`、`controls.h`、`data.h`、`dragdrop.h`、`input.h`、`layout.h`、`log.h`、`screenshot.h`、`source.h`、`text.h`、`theme.h`、`time.h`、`widget.h`、`window.h`、`window_manager.h`。

---

## 二十、数据绑定 `Source<T>`

控件保存的是"怎么取值"，不是"值本身"，所以数据变了不需要重建控件。三种来源语法一致，`get()` 每帧取一次。

快照（值被拷进 `Source`，之后不再变）：

```cpp
int percent = 42;
PlainText text(percent);                            // 存的是 42 的拷贝
```

指针（每次 `get()` 解引用，值变则显示变；指针为空时返回默认值）：

```cpp
int percent = 42;
PlainText text(&percent);                           // 改 percent，界面下一帧就变
```

lambda（每次 `get()` 调用，想怎么算就怎么算）：

```cpp
std::vector<Task> tasks;
PlainText text([&] { return "任务数: " + std::to_string(tasks.size()); });
```

三者都可以直接传给任何 `Source<T>` 参数，例如 `list.add<MeterText>("CPU", &cpuPercent, 100.0f, "%")`。需要消除重载歧义时用显式构造：`Source<int> s = value(42);` 或 `Source<int> s = ref(&counter);`。

---

## 二十一、帧循环契约

来自 `app.h` 的契约注释，以及 `app.cpp` 的实现：

1. `beginFrame()` 返回 `true` 时，**必须**在同一个循环体里调用一次 `endFrame()`，否则 ImGui 的帧状态失衡（调试版会断言）。
2. `beginFrame()` 返回 `false` 表示"该退出了"（用户点 X / Alt+F4 / `requestQuit()`）。此时**不要**调用 `endFrame()`——因为 `NewFrame()` 也没有被调用，这一帧不存在。
3. 标准写法（顺序不能换，`beginFrame` 有副作用：收事件、算 dt、清屏、`NewFrame`）：

```cpp
while (app.running() && app.beginFrame()) {
    windows.renderAll();   // 或你自己的 ImGui 调用
    app.endFrame();
}
```

4. 所有 ImGui 调用都必须落在这两者之间，不要自己再调 `ImGui::NewFrame()` / `ImGui::Render()`。
5. 每帧只调一次 `renderAll()`（或只调一次你自己的绘制函数）；它内部会处理延迟删除、模态框与右键菜单。
6. 窗口最小化时 `beginFrame()` 仍然返回 `true` 并照常跑一帧（视口临时按 1x1 处理，并用 `glfwWaitEventsTimeout(0.1)` 等事件），所以帧内不要假设宽高非零。
7. `beginFrame()` 返回 `false` 的原因只有两种：`App` 没有可用窗口（未 `init` 或已 `shutdown`）、GLFW 关闭标志被置起。
8. 截图等"读缓冲"操作放在 `endFrame()` 之后（见 screenshot.h）。

---

## 二十二、常见问题

### 中文显示成方块或缺字

`App` 只加载一次字体，探测顺序是：可执行文件目录下的 `assets/fonts/simhei.ttf` → `assets/simhei.ttf` → `assets/fonts/msyh.ttc` → `assets/msyh.ttc`，然后是系统字体 `C:/Windows/Fonts/msyh.ttc`、`C:/Windows/Fonts/simhei.ttf`、`C:/Windows/Fonts/simsun.ttc`。全都找不到时退回 ImGui 内置字体，并写一条警告 `using the default ImGui font; CJK glyphs will be missing`——这就是看到方块/空字的原因。

解决办法：把字体放到 exe 旁的 `assets/` 下，或显式设置 `AppConfig::fontPath`。字形范围固定是 `GetGlyphRangesChineseSimplifiedCommon()`，只覆盖常用简体字，生僻字仍会缺。字体只在 `init` 里加载，运行中换字体需要重开 `App`。

### `-mwindows` 下看不到任何输出

GUI 子系统下 `std::cerr` 是黑洞，初始化失败会表现为"双击没反应"。`App::init` 失败时会按 `AppConfig::showErrorDialog` 弹系统错误框（默认开），并把原因写进 `lastError()`；日志侧有三条出路可查：`AppConfig::logPath` 指定的文件（默认 `echo.log`，追加模式）、`log::entries()` 的内存缓冲、`log::setCallback()` 的回调。GLFW 自身的错误也会以 `GLFW error <code>: <desc>` 写进日志。

### 高 DPI 下发虚或字号翻倍

DPI 不需要手动处理：`AppConfig::scaleToMonitor` 让 GLFW 按显示器缩放系数放大窗口，后端会按 framebuffer scale 栅格化字形，`App::contentScale()` 可读当前值（100% = 1.0）。

`theme::setFontScale()` 是给"用户主动放大界面"用的，不是 DPI 开关：它除了改字号还会 `ScaleAllSizes`，在 150% 屏上再手动乘 1.5 会让所有东西变成 2.25 倍大。运行中切换显示器时，`contentScale()` 会更新并写一条日志，但字体与样式不会自动重建。

### `ImGui` 断言 / 界面错乱

最常见来源是 Begin/End 或 NewFrame/Render 不配对：

- 自己写 `ImGui::Begin()` 时，无论返回值是 `true` 还是 `false` 都必须调 `ImGui::End()`——返回 `false` 只表示内容被裁剪，`End()` 不能省。`Window::render()` 已经替你保证这一点（折叠时也配对），所以继承 `Window` 时不要在 `draw()` 里再调 `Begin`/`End` 同一窗口。
- 从 `beginFrame()` 到 `endFrame()` 之间提前 `return`（例如在绘制中途 `requestQuit()` 后直接返回）会漏掉 `endFrame()`，导致下一帧断言。正确做法是置标志位，让循环条件退出。
- `beginFrame()` 返回 `false` 时不要补一次 `endFrame()`。

### 滑条/复选框拖了又弹回去

控件不保存业务状态：值来自 `Source`（只读），新值通过 `onChange` 交还。传快照（`list.add<SliderInt>("x", 5, ...)`）就没有地方保存新值，拖完自然弹回。要么传指针（`&interval`），要么在 `onChange` 里写回变量：

```cpp
int interval = 500;
list.add<SliderInt>("采样间隔(ms)", &interval, 100, 2000, [&](int v) { interval = v; });
```

### 编译失败：`ImGuiStyle` 没有 `FontScaleDpi` 成员

`theme::setFontScale` 会写 `ImGuiStyle::FontScaleDpi`，这个字段是 Dear ImGui 1.92.0 才加入的（更早的版本只有 `io.FontGlobalScale`）。所以本库要求 **1.92+**（在 1.93.0 上验证过）；用 1.90.x 会在这一行报 `no member named 'FontScaleDpi'`。要在老版本 ImGui 上用，把 `theme.cpp::apply()` 里的这一行换成旧版的字体缩放写法。
