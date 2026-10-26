// Echo UI 库 - 输入与反馈类控件的实现。
//
// 统一模式：先取当前值 -> 交给 ImGui 画控件 -> 值变了就调 onChange。
// 控件本身不保存值，所以"谁拥有数据"永远在调用方手里。
#include "echo/controls.h"

#include <algorithm>
#include <string>

namespace echo {

// ------------------------------------------------------------------ TextInput

TextInput::TextInput(std::string label, std::string* buffer, size_t capacity)
    : label_(std::move(label)), buffer_(buffer), capacity_(capacity)
{
}

TextInput::TextInput(std::string label, std::string* buffer, std::function<void()> onSubmit, size_t capacity)
    : label_(std::move(label)), buffer_(buffer), capacity_(capacity), onSubmit_(std::move(onSubmit)),
      submitOnEnter_(true)
{
}

void TextInput::render()
{
    if (buffer_ == nullptr)
        return;

    const ImGuiInputTextFlags flags = submitOnEnter_ ? ImGuiInputTextFlags_EnterReturnsTrue : 0;
    ImGui::SetNextItemWidth(width_);
    const bool submitted = ImGui::InputText(label_.c_str(), buffer_, flags);

    if (submitted) {
        if (buffer_->size() > capacity_)
            buffer_->resize(capacity_);
        if (onSubmit_)
            onSubmit_();
        if (refocusOnSubmit_)
            focusNext_ = true;
    }

    // 回车提交后把焦点放回输入框：SetKeyboardFocusHere() 作用于"下一个"控件，
    // 所以要在 InputText 之后再调一次，并只调一次。
    if (focusNext_) {
        ImGui::SetKeyboardFocusHere(-1);
        focusNext_ = false;
    }

    if (hint_ != nullptr && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", hint_);
}

// ------------------------------------------------------------------ Checkbox

Checkbox::Checkbox(Source<std::string> label, Source<bool> state, std::function<void(bool)> onChange)
    : label_(std::move(label)), state_(std::move(state)), onChange_(std::move(onChange))
{
}

void Checkbox::render()
{
    bool value = state_.get();
    if (ImGui::Checkbox(label_.get().c_str(), &value) && onChange_)
        onChange_(value);
}

// ---------------------------------------------------------------- RadioGroup

RadioGroup::RadioGroup(Source<std::string> label, std::vector<std::string> options, Source<int> index,
                       std::function<void(int)> onChange, bool horizontal)
    : label_(std::move(label)), options_(std::move(options)), index_(std::move(index)),
      onChange_(std::move(onChange)), horizontal_(horizontal)
{
}

void RadioGroup::render()
{
    if (options_.empty())
        return;

    // 同一窗口里多个单选组共用 ##radio_group，PushID 区分开。
    ImGui::PushID(this);

    if (ImGui::BeginTable("##radio_group", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label_.get().c_str());
        ImGui::TableNextColumn();

        const int current = index_.get();
        for (size_t i = 0; i < options_.size(); ++i) {
            if (horizontal_ && i > 0)
                ImGui::SameLine();
            if (ImGui::RadioButton(options_[i].c_str(), current == static_cast<int>(i)) && onChange_)
                onChange_(static_cast<int>(i));
        }
        ImGui::EndTable();
    }

    ImGui::PopID();
}

// ----------------------------------------------------------------- SliderInt

SliderInt::SliderInt(Source<std::string> label, Source<int> value, int minimum, int maximum,
                     std::function<void(int)> onChange)
    : label_(std::move(label)), value_(std::move(value)), minimum_(minimum), maximum_(maximum),
      onChange_(std::move(onChange))
{
}

void SliderInt::render()
{
    int value = value_.get();
    ImGui::SetNextItemWidth(width_);
    if (ImGui::SliderInt(label_.get().c_str(), &value, minimum_, maximum_) && onChange_)
        onChange_(value);
}

// --------------------------------------------------------------- SliderFloat

SliderFloat::SliderFloat(Source<std::string> label, Source<float> value, float minimum, float maximum,
                         std::function<void(float)> onChange, const char* format)
    : label_(std::move(label)), value_(std::move(value)), minimum_(minimum), maximum_(maximum),
      onChange_(std::move(onChange)), format_(format)
{
}

void SliderFloat::render()
{
    float value = value_.get();
    ImGui::SetNextItemWidth(width_);
    if (ImGui::SliderFloat(label_.get().c_str(), &value, minimum_, maximum_, format_) && onChange_)
        onChange_(value);
}

// ------------------------------------------------------------------ InputInt

InputInt::InputInt(Source<std::string> label, Source<int> value, std::function<void(int)> onChange, int step,
                   int stepFast)
    : label_(std::move(label)), value_(std::move(value)), onChange_(std::move(onChange)), step_(step),
      stepFast_(stepFast)
{
}

void InputInt::render()
{
    int value = value_.get();
    ImGui::SetNextItemWidth(width_);
    if (ImGui::InputInt(label_.get().c_str(), &value, step_, stepFast_) && onChange_)
        onChange_(value);
}

// ----------------------------------------------------------------- DragFloat

DragFloat::DragFloat(Source<std::string> label, Source<float> value, float speed, float minimum, float maximum,
                     std::function<void(float)> onChange, const char* format)
    : label_(std::move(label)), value_(std::move(value)), speed_(speed), minimum_(minimum), maximum_(maximum),
      onChange_(std::move(onChange)), format_(format)
{
}

void DragFloat::render()
{
    float value = value_.get();
    ImGui::SetNextItemWidth(width_);
    // ImGui 约定：minimum >= maximum 表示不限制范围。
    if (ImGui::DragFloat(label_.get().c_str(), &value, speed_, minimum_, maximum_, format_) && onChange_)
        onChange_(value);
}

// --------------------------------------------------------------- ProgressBar

ProgressBar::ProgressBar(Source<float> fraction, Source<std::string> overlay, ImVec2 size)
    : fraction_(std::move(fraction)), overlay_(std::move(overlay)), size_(size)
{
}

void ProgressBar::render()
{
    const float raw = fraction_.get();
    const float fraction = raw < 0.0f ? 0.0f : (raw > 1.0f ? 1.0f : raw);
    const std::string overlay = overlay_.get();
    ImGui::ProgressBar(fraction, size_, overlay.empty() ? nullptr : overlay.c_str());
}

// ------------------------------------------------------------------- Spinner

Spinner::Spinner(float radius, const ImVec4& color, float speed)
    : radius_(radius), color_(color), speed_(speed)
{
}

void Spinner::render()
{
    // 用一个 Dummy 占位（不占 ID、不响应输入），再把圆弧画在这个位置上。
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(radius_ * 2.0f, radius_ * 2.0f));

    const ImVec2 center(position.x + radius_, position.y + radius_);
    const float start = static_cast<float>(ImGui::GetTime()) * speed_;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PathClear();
    draw->PathArcTo(center, radius_, start, start + 4.2f, 24);
    draw->PathStroke(ImGui::GetColorU32(color_), 0, 2.5f);
}

// --------------------------------------------------------------------- Combo

Combo::Combo(Source<std::string> label, const char* const* items, int count, Source<int> index,
             std::function<void(int)> onChange)
    : label_(std::move(label)), items_(items), count_(count), index_(std::move(index)),
      onChange_(std::move(onChange))
{
}

void Combo::render()
{
    if (items_ == nullptr || count_ <= 0)
        return;

    int index = index_.get();
    if (index < 0 || index >= count_)
        index = 0;

    ImGui::SetNextItemWidth(width_);
    if (ImGui::BeginCombo(label_.get().c_str(), items_[index])) {
        for (int i = 0; i < count_; ++i) {
            const bool selected = (i == index);
            if (ImGui::Selectable(items_[i], selected) && i != index && onChange_) {
                onChange_(i);
                index = i;
            }
        }
        ImGui::EndCombo();
    }
}

// ----------------------------------------------------------------- ColorEdit

ColorEdit::ColorEdit(Source<std::string> label, Source<ImVec4> color, std::function<void(ImVec4)> onChange)
    : label_(std::move(label)), color_(std::move(color)), onChange_(std::move(onChange))
{
}

void ColorEdit::render()
{
    const ImVec4 color = color_.get();
    float channels[4] = {color.x, color.y, color.z, color.w};
    if (ImGui::ColorEdit4(label_.get().c_str(), channels) && onChange_)
        onChange_(ImVec4(channels[0], channels[1], channels[2], channels[3]));
}

} // namespace echo
