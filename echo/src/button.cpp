// Echo UI 库 - 按钮的实现。
//
// 两个按钮的差别只体现在"值往哪去"：
//   Button       点击 -> 调用回调，不持有任何状态；
//   ToggleButton 点击 -> 把"取反后的状态"交给回调，状态由调用方保存。
#include "echo/button.h"

#include <string>

namespace echo {

// -------------------------------------------------------------------- Button

Button::Button(Source<std::string> label, std::function<void()> onClick, ImVec2 size, bool small,
               std::string tooltip)
    : label_(std::move(label)), onClick_(std::move(onClick)), size_(size), small_(small),
      tooltip_(std::move(tooltip))
{
}

void Button::setColor(const ImVec4& color)
{
    color_ = color;
    hasColor_ = true;
}

void Button::render()
{
    const std::string label = label_.get();

    if (hasColor_)
        ImGui::PushStyleColor(ImGuiCol_Button, color_);
    if (!enabled_)
        ImGui::BeginDisabled(); // 变灰，并且不响应输入

    bool pressed = false;
    if (small_)
        pressed = ImGui::SmallButton(label.c_str());
    else
        pressed = ImGui::Button(label.c_str(), size_);

    if (!enabled_)
        ImGui::EndDisabled();
    if (hasColor_)
        ImGui::PopStyleColor();

    // 禁用状态下即使拿到 pressed 也不触发回调（ImGui 会挡住，这只是双保险）。
    if (enabled_ && pressed && onClick_)
        onClick_();

    if (!tooltip_.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip_.c_str());
}

// -------------------------------------------------------------- ToggleButton

ToggleButton::ToggleButton(Source<std::string> label, Source<bool> state, std::function<void(bool)> onChange,
                           ImVec2 size)
    : label_(std::move(label)), state_(std::move(state)), onChange_(std::move(onChange)), size_(size)
{
}

void ToggleButton::render()
{
    const bool on = state_.get();
    const std::string label = label_.get();

    // 开着的时候用强调色填充，关着的时候用主题默认按钮色。
    const ImVec4 normal = ImGui::GetStyleColorVec4(ImGuiCol_Button);
    const ImVec4 hovered = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
    ImGui::PushStyleColor(ImGuiCol_Button, on ? theme::accent() : normal);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? theme::accent() : hovered);

    const bool pressed = ImGui::Button(label.c_str(), size_);

    ImGui::PopStyleColor(2);

    if (pressed && onChange_)
        onChange_(!on);
}

} // namespace echo
