// Echo UI 库 - 窗口。
//
// 一个 Window 就是屏幕上一块可以拖动/折叠/关闭的矩形区域。
// 它负责 ImGui 的 Begin/End 生命周期，子类只负责"里面画什么"。
//
// 三个层次：
//   Window        基类：处理标题栏、尺寸、位置、折叠、关闭、菜单栏
//   PanelWindow   内容是一串控件（Content），90% 的场景用它
//   ConsoleWindow 命令行式界面：历史 + 输入框 + 命令注册
//
// 自己写窗口时只要继承 Window 并实现 draw()：
//     class ClockWindow : public Window {
//     public:
//         ClockWindow() : Window("时钟", ImVec2(240, 120)) {}
//         void draw() override { ImGui::TextUnformatted(timeText.c_str()); }
//     };
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "widget.h"

namespace echo {

/// 顶层窗口基类。
///
/// 它替你做三件容易出错的事：
///   1. Begin() 一定配一个 End()（哪怕窗口被折叠）；
///   2. 折叠/隐藏时不执行你的 draw()，避免在零尺寸区域里塞控件；
///   3. 记住上一帧是否真的显示（visible()），供外部判断。
///
/// 位置和大小只在**第一次**生效（ImGuiCond_FirstUseEver），之后用户拖动的结果
/// 会存进 imgui.ini，下次启动自动恢复。
class Window {
public:
    explicit Window(std::string title, ImVec2 size = ImVec2(0, 0));
    virtual ~Window() = default;

    /// 画内容。调用时已经处于 ImGui::Begin() 内部，直接画即可。
    virtual void draw() = 0;

    /// 画窗口菜单栏（只在 flags 含 ImGuiWindowFlags_MenuBar 时被调用）。
    virtual void drawMenuBar() {}

    /// 完整的一帧：Begin -> [菜单栏] -> draw -> End。由 WindowManager 调用。
    void render();

    /// 类型名（调试/测试用）。
    virtual const char* type() const { return "Window"; }

    void setTitle(std::string title) { title_ = std::move(title); }
    const std::string& title() const { return title_; }

    /// 首次显示的尺寸；ImVec2(0,0) 表示交给 ImGui 自己决定。
    void setSize(ImVec2 size) { size_ = size; }
    ImVec2 size() const { return size_; }

    /// 首次显示的位置（屏幕坐标）。
    void setPosition(ImVec2 position);

    /// ImGui 窗口标志，例如 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar。
    void setFlags(ImGuiWindowFlags flags) { flags_ = flags; }
    ImGuiWindowFlags flags() const { return flags_; }

    /// 窗口 id，由 WindowManager 分配，全局唯一。
    uint64_t id() const { return id_; }

    /// 上一帧是否真的画出来了（没被关掉、也没折叠）。
    bool visible() const { return visible_; }

    bool open = true; ///< X 按钮 / 菜单勾选的状态

protected:
    void assignId(uint64_t id) { id_ = id; }

private:
    friend class WindowManager;

    std::string title_;
    ImVec2 size_ = ImVec2(0, 0);
    ImVec2 position_ = ImVec2(0, 0);
    bool hasPosition_ = false;
    ImGuiWindowFlags flags_ = ImGuiWindowFlags_None;
    uint64_t id_ = 0;
    bool visible_ = false;
};

/// 内容是一串控件（Content）的窗口。绝大多数界面用它就够了。
///
///     auto& panel = windows.add<PanelWindow>("设置", ImVec2(420, 300));
///     panel.add<HeadingText>("外观");
///     panel.add<Checkbox>("深色主题", &dark, [](bool on) { ... });
class PanelWindow : public Window {
public:
    explicit PanelWindow(std::string title, ImVec2 size = ImVec2(600, 400));

    /// 追加控件，返回引用。
    template <class T, class... Args>
    T& add(Args&&... args)
    {
        return content_.add<T>(std::forward<Args>(args)...);
    }

    /// 清空全部控件。
    void clear() { content_.clear(); }

    /// 直接拿到底层列表（批量操作/测试用）。
    Content& content() { return content_; }

    void draw() override;
    const char* type() const override { return "PanelWindow"; }

private:
    Content content_;
};

/// 旧名字，等价于 PanelWindow。
using TextWindow = PanelWindow;

/// 命令台窗口：滚动历史 + 一行输入 + 命令表。
///
///     auto& console = windows.add<ConsoleWindow>("控制台");
///     console.registerCommand("help", [&console](const ConsoleWindow::Args&) {
///         console.addLine("可用命令: help / quit");
///     });
///     console.registerCommand("quit", [&app](const ConsoleWindow::Args&) { app.requestQuit(); });
///
/// 输入框回车后自动清空并重新获得焦点；新行出现时自动滚到底部。
/// 参数解析支持双引号：`load "C:\a b.txt"` 会把路径当成一个参数。
class ConsoleWindow : public Window {
public:
    using Args = std::vector<std::string>;
    using Command = std::function<void(const Args&)>;

    explicit ConsoleWindow(std::string title = "Console", ImVec2 size = ImVec2(600, 400));

    /// 追加一行历史。
    void addLine(std::string line);

    /// 注册命令（同名覆盖）。命令名区分大小写。
    void registerCommand(std::string name, Command command);

    /// 解析并执行一行；命令不存在时自动写一条错误行并返回 false。
    bool execute(const std::string& line);

    void clearHistory();
    const std::vector<std::string>& history() const { return lines_; }

    /// 新内容是否自动滚到底部，默认开。
    void setAutoScroll(bool autoScroll) { autoScroll_ = autoScroll; }

    /// 输入行的提示前缀（默认 ">"），只影响回显。
    void setPrompt(std::string prompt) { prompt_ = std::move(prompt); }

    void draw() override;
    void drawMenuBar() override;
    const char* type() const override { return "ConsoleWindow"; }

    /// 按空白切分，支持双引号包裹的空格。公开出来方便单测和复用。
    static Args split(const std::string& line);

private:
    std::string input_;
    std::string prompt_ = ">";
    std::vector<std::string> lines_;
    std::map<std::string, Command> commands_;
    bool autoScroll_ = true;
    bool scrollToBottom_ = false;
    bool focusInput_ = false;
};

} // namespace echo
