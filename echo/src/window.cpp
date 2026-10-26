// Echo UI 库 - 窗口的实现。
//
// 这里的重点是"Begin/End 配对"和"折叠时不画内容"。
// ImGui 要求：无论 Begin() 返回 true 还是 false，都必须调用 End()——
// 旧代码把返回值丢掉，等于把内容画到了零尺寸的裁剪区里，布局会错乱。
#include "echo/window.h"

#include <algorithm>
#include <cctype>
#include <cfloat>

#include "imgui_stdlib.h"

namespace echo {

// -------------------------------------------------------------------- Window

Window::Window(std::string title, ImVec2 size)
    : title_(std::move(title)), size_(size)
{
}

void Window::setPosition(ImVec2 position)
{
    position_ = position;
    hasPosition_ = true;
}

void Window::render()
{
    if (!open) {
        visible_ = false;
        return;
    }

    // 只在第一次出现时应用尺寸/位置；之后用户拖动的结果来自 imgui.ini。
    if (size_.x > 0.0f && size_.y > 0.0f)
        ImGui::SetNextWindowSize(size_, ImGuiCond_FirstUseEver);
    if (hasPosition_)
        ImGui::SetNextWindowPos(position_, ImGuiCond_FirstUseEver);

    const bool visible = ImGui::Begin(title_.c_str(), &open, flags_);
    if (visible) {
        if ((flags_ & ImGuiWindowFlags_MenuBar) != 0 && ImGui::BeginMenuBar()) {
            drawMenuBar();
            ImGui::EndMenuBar();
        }
        draw();
    }
    ImGui::End(); // 必须无条件调用
    visible_ = visible;
}

// --------------------------------------------------------------- PanelWindow

PanelWindow::PanelWindow(std::string title, ImVec2 size)
    : Window(std::move(title), size)
{
}

void PanelWindow::draw()
{
    content_.render();
}

// ------------------------------------------------------------- ConsoleWindow

ConsoleWindow::ConsoleWindow(std::string title, ImVec2 size)
    : Window(std::move(title), size)
{
    setFlags(ImGuiWindowFlags_MenuBar);
}

void ConsoleWindow::addLine(std::string line)
{
    lines_.push_back(std::move(line));

    // 上限 2000 行，超了丢最旧的 500 行：控制台不应该无限吃内存。
    if (lines_.size() > 2000)
        lines_.erase(lines_.begin(), lines_.begin() + 500);

    scrollToBottom_ = true;
}

void ConsoleWindow::registerCommand(std::string name, Command command)
{
    commands_[std::move(name)] = std::move(command);
}

bool ConsoleWindow::execute(const std::string& line)
{
    const Args args = split(line);
    if (args.empty())
        return false;

    const auto it = commands_.find(args.front());
    if (it == commands_.end()) {
        addLine("[error] unknown command: " + args.front());
        return false;
    }

    it->second(args);
    return true;
}

void ConsoleWindow::clearHistory()
{
    lines_.clear();
    scrollToBottom_ = true;
}

void ConsoleWindow::draw()
{
    // 底部留出输入框的高度，剩下的全部给历史区。
    const float footer = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();

    if (ImGui::BeginChild("##console_lines", ImVec2(0.0f, -footer), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto& line : lines_)
            ImGui::TextUnformatted(line.c_str());

        if (autoScroll_ && scrollToBottom_) {
            ImGui::SetScrollHereY(1.0f); // 1.0 = 贴底
            scrollToBottom_ = false;
        }
    }
    ImGui::EndChild();

    // 焦点必须在 InputText **之前**设置，它作用于下一个控件。
    if (focusInput_) {
        ImGui::SetKeyboardFocusHere();
        focusInput_ = false;
    }

    ImGui::SetNextItemWidth(-FLT_MIN); // 占满宽度
    if (ImGui::InputText("##console_input", &input_, ImGuiInputTextFlags_EnterReturnsTrue)) {
        const std::string line = input_;
        input_.clear();
        if (!line.empty()) {
            addLine(prompt_.empty() ? line : prompt_ + " " + line);
            execute(line);
        }
        focusInput_ = true; // 回车后把焦点抢回来，方便连续输入
    }
}

void ConsoleWindow::drawMenuBar()
{
    if (ImGui::BeginMenu("Console")) {
        if (ImGui::MenuItem("Clear"))
            clearHistory();

        bool autoScroll = autoScroll_;
        if (ImGui::MenuItem("Auto scroll", nullptr, &autoScroll))
            autoScroll_ = autoScroll;

        ImGui::EndMenu();
    }
}

ConsoleWindow::Args ConsoleWindow::split(const std::string& line)
{
    Args args;
    std::string current;
    bool inQuotes = false;

    for (const char ch : line) {
        if (ch == '"') {
            inQuotes = !inQuotes; // 引号本身不进入参数
            continue;
        }
        if (!inQuotes && std::isspace(static_cast<unsigned char>(ch))) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty())
        args.push_back(current);

    return args;
}

} // namespace echo
