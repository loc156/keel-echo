#include "echo/app.h"

#include <algorithm>
#include <fstream>
#include <string>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "echo/log.h"
#include "echo/theme.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace echo {

namespace {

void onGlfwError(int code, const char* description)
{
    std::string message = "GLFW error " + std::to_string(code) + ": " + (description ? description : "");
    log::error(message);
}

std::string executableDirectory()
{
#ifdef _WIN32
    char buffer[MAX_PATH] = {0};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    std::string path(buffer, length);
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
#else
    return ".";
#endif
}

bool fileExists(const std::string& path)
{
    std::ifstream stream(path.c_str());
    return stream.good();
}

std::string findFont()
{
    const std::string dir = executableDirectory();

    const char* relative[] = {
        "assets/fonts/simhei.ttf",
        "assets/simhei.ttf",
        "assets/fonts/msyh.ttc",
        "assets/msyh.ttc",
    };
    for (const char* name : relative) {
        const std::string path = dir + "/" + name;
        if (fileExists(path))
            return path;
    }

    const char* systemFonts[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (const char* path : systemFonts) {
        if (fileExists(path))
            return path;
    }
    return {};
}

void showErrorDialog(const std::string& message)
{
#ifdef _WIN32
    MessageBoxA(nullptr, message.c_str(), "Echo", MB_OK | MB_ICONERROR);
#else
    (void)message;
#endif
}

} // namespace

App::App() = default;

App::~App()
{
    shutdown();
}

bool App::fail(const std::string& message)
{
    lastError_ = message;
    log::fatal(message);
    if (config_.showErrorDialog)
        showErrorDialog(message);
    return false;
}

bool App::init(const AppConfig& config)
{
    if (initialized_)
        return true;

    config_ = config;
    iniPath_ = config.iniPath;

    if (!config_.logPath.empty() && log::file().empty())
        log::setFile(config_.logPath);

    glfwSetErrorCallback(&onGlfwError);
    if (!glfwInit())
        return fail("glfwInit() failed");
    glfwReady_ = true;

    if (!createWindow()) {
        glfwTerminate();
        glfwReady_ = false;
        window_ = nullptr;
        return false;
    }

    if (!createImGui()) {
        shutdown();
        return false;
    }

    if (config_.applyDefaultTheme)
        theme::dark();

    // No manual font scaling here: the backend reports a 1.5x framebuffer scale
    // on a 150% monitor, and ImGui rasterises glyphs at that density.  Scaling
    // fonts as well would render everything 2.25x too large.
    lastTime_ = glfwGetTime();
    elapsed_ = 0.0;
    frameIndex_ = 0;
    initialized_ = true;

    log::info("Echo initialized: OpenGL " + std::to_string(config_.glMajor) + "." +
              std::to_string(config_.glMinor) + ", content scale " + std::to_string(contentScale_));
    return true;
}

bool App::createWindow()
{
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, config_.glMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, config_.glMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
#ifdef GLFW_SCALE_TO_MONITOR
    if (config_.scaleToMonitor)
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(config_.width, config_.height, config_.title.c_str(), nullptr, nullptr);
    if (window_ == nullptr)
        return fail("cannot create an OpenGL " + std::to_string(config_.glMajor) + "." +
                    std::to_string(config_.glMinor) + " window; update your GPU driver");

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(config_.vsync ? 1 : 0);

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(window_, &scaleX, &scaleY);
    contentScale_ = std::max(1.0f, std::max(scaleX, scaleY));

    glfwSetWindowUserPointer(window_, this);
    glfwSetWindowContentScaleCallback(window_, &App::onContentScaleChanged);
    return true;
}

bool App::createImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    if (config_.enableKeyboardNav)
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = iniPath_.empty() ? nullptr : iniPath_.c_str();

    loadFont();

    if (!ImGui_ImplGlfw_InitForOpenGL(window_, true))
        return fail("ImGui_ImplGlfw_InitForOpenGL failed");
    if (!ImGui_ImplOpenGL3_Init(config_.glslVersion.c_str())) {
        ImGui_ImplGlfw_Shutdown();
        return fail("ImGui_ImplOpenGL3_Init(" + config_.glslVersion + ") failed");
    }
    imguiReady_ = true;
    return true;
}

void App::loadFont()
{
    ImGuiIO& io = ImGui::GetIO();

    std::string path = config_.fontPath.empty() ? findFont() : config_.fontPath;
    if (!path.empty()) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), config_.fontSize, nullptr,
                                                    io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        if (font != nullptr) {
            log::info("font loaded: " + path);
            return;
        }
        log::warn("failed to load font: " + path);
    }

    io.Fonts->AddFontDefault();
    log::warn("using the default ImGui font; CJK glyphs will be missing");
}

void App::applyScale(float scale)
{
    contentScale_ = scale > 0.0f ? scale : 1.0f;
    log::info("content scale changed to " + std::to_string(contentScale_));
}

void App::onContentScaleChanged(GLFWwindow* window, float xscale, float yscale)
{
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (app != nullptr)
        app->applyScale(std::max(xscale, yscale));
}

bool App::running() const
{
    return window_ != nullptr && !quit_ && !glfwWindowShouldClose(window_);
}

bool App::beginFrame()
{
    if (window_ == nullptr)
        return false;

    glfwPollEvents();
    if (glfwWindowShouldClose(window_))
        return false;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width <= 0 || height <= 0) {
        // Minimised: sleep instead of spinning, and still run a frame so the
        // NewFrame/Render pair stays balanced.
        glfwWaitEventsTimeout(0.1);
        width = 1;
        height = 1;
    }

    const double now = glfwGetTime();
    const double rawDelta = now - lastTime_;
    lastTime_ = now;
    deltaTime_ = std::min(rawDelta, 0.25);
    elapsed_ += rawDelta;
    ++frameIndex_;

    glViewport(0, 0, width, height);
    const ImVec4 clear = config_.clearColor;
    glClearColor(clear.x, clear.y, clear.z, clear.w);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    return true;
}

void App::endFrame()
{
    if (window_ == nullptr)
        return;

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

void App::requestQuit()
{
    quit_ = true;
    if (window_ != nullptr)
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

void App::shutdown()
{
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfwReady_) {
        glfwTerminate();
        glfwReady_ = false;
    }
    initialized_ = false;
}

void App::setTitle(const std::string& title)
{
    config_.title = title;
    if (window_ != nullptr)
        glfwSetWindowTitle(window_, title.c_str());
}

} // namespace echo
