// Echo UI 库 - 应用对象（窗口 + OpenGL 上下文 + ImGui + 帧循环 + 日志 + DPI）。
//
// 它是唯一"知道 GLFW 和 OpenGL"的地方。你的业务代码通常只碰这几句：
//
//     App app;
//     AppConfig config;
//     config.title = "我的工具";
//     if (!app.init(config)) return 1;
//
//     WindowManager windows;
//     windows.add<PanelWindow>("主面板");
//
//     while (app.running() && app.beginFrame()) {
//         windows.renderAll();
//         app.endFrame();
//     }
//     app.shutdown();
//
// 【帧循环契约】
//   beginFrame() 返回 true 时，**必须**在同一个循环体里调用 endFrame()，
//   否则 ImGui 的帧状态会失衡（调试版会断言）。
//   返回 false 表示"该退出了"（用户点了 X / Alt+F4 / requestQuit()）。
//
// 【init 失败时怎么办】
//   返回 false，lastError() 里有原因，同时写了一条 fatal 日志；
//   如果 AppConfig::showErrorDialog 为 true，还会弹一个系统错误框——
//   因为 GUI 子系统下 std::cerr 是黑洞，否则你会看到"双击没反应"。
#pragma once

#include <cstdint>
#include <string>

#include "imgui.h"

struct GLFWwindow; // 前置声明，避免把 GLFW 头文件泄给使用者

namespace echo {

/// App 的启动参数。全部有默认值，通常只改 title / width / height。
struct AppConfig {
    std::string title = "Echo";
    int width = 1400;
    int height = 900;

    /// 垂直同步。开：省电、不空转；关：跑满（测帧率时用）。
    bool vsync = true;

    /// 让 GLFW 按显示器缩放系数放大窗口（150% 屏上 1400x900 -> 物理 2100x1350）。
    bool scaleToMonitor = true;

    /// 请求的 OpenGL 版本。默认 3.3 core，找不到就初始化失败（不会静默黑屏）。
    int glMajor = 3;
    int glMinor = 3;

    /// 传给 ImGui OpenGL3 后端的 GLSL 版本头。
    std::string glslVersion = "#version 330";

    /// 字体文件。留空则自动探测：可执行文件旁的 assets/ 优先，其次系统字体
    /// （msyh.ttc / simhei.ttf / simsun.ttc，保证中文能显示）。
    std::string fontPath;
    float fontSize = 18.0f;

    /// ImGui 布局文件（窗口位置/大小/折叠状态）。空串表示不用磁盘布局。
    std::string iniPath = "imgui.ini";

    /// 每帧清屏颜色。
    ImVec4 clearColor = ImVec4(0.10f, 0.10f, 0.11f, 1.0f);

    /// 键盘导航（Tab 切换焦点）。
    bool enableKeyboardNav = true;

    /// 初始化失败时弹系统错误框（GUI 子系统下强烈建议打开）。
    bool showErrorDialog = true;

    /// 是否套用 Echo 的默认深色主题。
    bool applyDefaultTheme = true;

    /// 日志文件。空串表示不写文件（仍然有内存缓冲和回调）。
    std::string logPath = "echo.log";
};

/// 应用对象。一个进程一个，不可拷贝。
///
/// 生命周期顺序（析构函数里会自动做反向清理）：
///   init -> [beginFrame/endFrame 若干次] -> shutdown
class App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    /// 初始化：创建窗口、GL 上下文、ImGui、字体、日志文件。失败返回 false。
    bool init(const AppConfig& config);

    /// 关闭并释放全部资源（可重复调用）。
    void shutdown();

    /// 窗口还开着吗（用户点 X 后变 false）。
    bool running() const;

    /// 开始一帧：收事件、算 dt、清屏、ImGui::NewFrame()。
    /// 返回 false 表示应该退出循环（此时不要调 endFrame）。
    bool beginFrame();

    /// 结束一帧：ImGui::Render() + 提交绘制 + 交换缓冲。
    void endFrame();

    /// 请求退出（下一帧 running() 返回 false）。
    void requestQuit();

    /// 上一帧耗时（秒），已经夹到 0.25 秒以内，避免断点/卡顿造成巨大跳变。
    double deltaTime() const { return deltaTime_; }
    /// 从 init 到现在的总秒数（真实时间，不做夹取）。
    double elapsed() const { return elapsed_; }
    /// 已经渲染了多少帧。
    uint64_t frameIndex() const { return frameIndex_; }

    /// 底层 GLFW 窗口（很少需要；比如自己调 glfwGetWindowSize）。
    GLFWwindow* window() const { return window_; }

    /// 显示器内容缩放（100% = 1.0，150% = 1.5）。只读，缩放由后端处理。
    float contentScale() const { return contentScale_; }

    const AppConfig& config() const { return config_; }
    const std::string& lastError() const { return lastError_; }

    /// 改窗口标题（运行中也可以）。
    void setTitle(const std::string& title);

private:
    bool fail(const std::string& message);
    bool createWindow();
    bool createImGui();
    void loadFont();
    void applyScale(float scale);
    static void onContentScaleChanged(GLFWwindow* window, float xscale, float yscale);

    AppConfig config_;
    GLFWwindow* window_ = nullptr;
    std::string iniPath_;
    std::string lastError_;
    double lastTime_ = 0.0;
    double deltaTime_ = 0.0;
    double elapsed_ = 0.0;
    uint64_t frameIndex_ = 0;
    float contentScale_ = 1.0f;
    bool initialized_ = false;
    bool imguiReady_ = false;
    bool glfwReady_ = false;
    bool quit_ = false;
};

} // namespace echo
