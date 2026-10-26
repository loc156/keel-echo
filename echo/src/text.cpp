// Echo UI 库 - 文本类控件的实现。
//
// 这一层只做"排版 + 调用 ImGui 的绘制函数"，不持有任何业务数据：
// 需要显示的值都通过 Source 取（见 source.h），所以这里的代码都不长。
#include "echo/text.h"

#include <cfloat>
#include <string>

#include "echo/theme.h"

namespace echo {

// ---------------------------------------------------------------- PlainText

void PlainText::render()
{
    ImGui::TextUnformatted(text_.get().c_str());
}

// -------------------------------------------------------------- ColoredText

ColoredText::ColoredText(Source<std::string> text, const ImVec4& color)
    : text_(std::move(text)), color_(color)
{
}

void ColoredText::render()
{
    ImGui::TextColored(color_, "%s", text_.get().c_str());
}

// -------------------------------------------------------------- HeadingText

HeadingText::HeadingText(Source<std::string> text)
    : text_(std::move(text)), color_(theme::accent())
{
}

HeadingText::HeadingText(Source<std::string> text, const ImVec4& color)
    : text_(std::move(text)), color_(color)
{
}

void HeadingText::render()
{
    // SeparatorText 会把文字和右侧的分隔线一起画出来；颜色靠 Text 样式控制。
    ImGui::PushStyleColor(ImGuiCol_Text, color_);
    ImGui::SeparatorText(text_.get().c_str());
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- Separator

void Separator::render()
{
    ImGui::Separator();
}

// --------------------------------------------------------------- IndentText

IndentText::IndentText(Source<std::string> text) : text_(std::move(text)) {}

IndentText::IndentText(Source<std::string> text, float indentPixels)
    : text_(std::move(text)), indent_(indentPixels)
{
}

void IndentText::render()
{
    ImGui::Indent(indent_);
    ImGui::TextUnformatted(text_.get().c_str());
    ImGui::Unindent(indent_);
}

// ---------------------------------------------------------------- MeterText

MeterText::MeterText(Source<std::string> label, Source<float> value, Source<float> maximum,
                     Source<std::string> suffix, bool showBar)
    : label_(std::move(label)), value_(std::move(value)), maximum_(std::move(maximum)),
      suffix_(std::move(suffix)), bar_(showBar), integer_(false)
{
}

MeterText::MeterText(Source<std::string> label, int value, int maximum, Source<std::string> suffix)
    : label_(std::move(label)), value_(static_cast<float>(value)), maximum_(static_cast<float>(maximum)),
      suffix_(std::move(suffix)), bar_(true), integer_(true)
{
}

MeterText::MeterText(Source<std::string> label, const int* value, int maximum, Source<std::string> suffix)
    : label_(std::move(label)),
      // 指针版本：每次取值时解引用，再转成内部统一使用的 float。
      value_([value]() { return value != nullptr ? static_cast<float>(*value) : 0.0f; }),
      maximum_(static_cast<float>(maximum)), suffix_(std::move(suffix)), bar_(true), integer_(true)
{
}

void MeterText::render()
{
    const float value = value_.get();
    const float maximum = maximum_.get();
    const std::string suffix = suffix_.get();

    // 比例决定颜色：越接近上限越"绿"，越低越"红"。
    float ratio = 0.0f;
    ImVec4 color = colors::muted();
    if (maximum > 0.0f) {
        ratio = value / maximum;
        ratio = ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio);
        color = ratio > 0.6f ? colors::green() : (ratio > 0.3f ? colors::warning() : colors::red());
    }

    if (integer_)
        ImGui::TextColored(color, "%s: %.0f%s / %.0f%s", label_.get().c_str(), value, suffix.c_str(), maximum,
                           suffix.c_str());
    else
        ImGui::TextColored(color, "%s: %.1f%s / %.1f%s", label_.get().c_str(), value, suffix.c_str(), maximum,
                           suffix.c_str());

    // 细条只是视觉辅助；宽度 -FLT_MIN 表示"占满可用宽度"，高度 4 像素。
    if (bar_)
        ImGui::ProgressBar(ratio, ImVec2(-FLT_MIN, 4.0f), "");
}

// -------------------------------------------------------------- SpeakerText

SpeakerText::SpeakerText(Source<std::string> speaker, Source<std::string> message)
    : speaker_(std::move(speaker)), message_(std::move(message))
{
}

SpeakerText::SpeakerText(Source<std::string> speaker, Source<std::string> message, const ImVec4& speakerColor)
    : speaker_(std::move(speaker)), message_(std::move(message)), color_(speakerColor)
{
}

void SpeakerText::render()
{
    ImGui::TextColored(color_, "%s:", speaker_.get().c_str());
    ImGui::SameLine();
    ImGui::TextWrapped("%s", message_.get().c_str());
}

// ------------------------------------------------------------- ListItemText

ListItemText::ListItemText(Source<std::string> text) : text_(std::move(text)) {}

ListItemText::ListItemText(Source<std::string> text, std::string prefix)
    : text_(std::move(text)), prefix_(std::move(prefix))
{
}

ListItemText::ListItemText(Source<std::string> text, std::string prefix, const ImVec4& color)
    : text_(std::move(text)), prefix_(std::move(prefix)), color_(color)
{
}

void ListItemText::render()
{
    ImGui::TextColored(color_, "%s%s", prefix_.c_str(), text_.get().c_str());
}

// -------------------------------------------------------------------- Spacer

Spacer::Spacer(float height) : height_(height) {}

void Spacer::render()
{
    if (height_ > 0.0f)
        ImGui::Dummy(ImVec2(0.0f, height_));
    else
        ImGui::Spacing();
}

// ------------------------------------------------------------ HighlightText

HighlightText::HighlightText(Source<std::string> text, const ImVec4& background)
    : text_(std::move(text)), background_(background)
{
}

HighlightText::HighlightText(Source<std::string> text, const ImVec4& background, const ImVec4& foreground)
    : text_(std::move(text)), background_(background), foreground_(foreground)
{
}

void HighlightText::render()
{
    const std::string text = text_.get();

    // 关键点：ImGui 的文字绘制不读 FrameBg，所以背景必须自己画成矩形。
    // 先算好文字尺寸，画矩形，再把光标对准矩形内部写文字。
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 padding(4.0f, 2.0f);

    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(position.x - padding.x, position.y - padding.y),
                                              ImVec2(position.x + textSize.x + padding.x, position.y + textSize.y + padding.y),
                                              ImGui::GetColorU32(background_), 3.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, foreground_);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();
}

// --------------------------------------------------------------- WrappedText

WrappedText::WrappedText(Source<std::string> text) : text_(std::move(text)) {}

void WrappedText::render()
{
    ImGui::TextWrapped("%s", text_.get().c_str());
}

// ------------------------------------------------------------- KeyValueText

KeyValueText::KeyValueText(Source<std::string> key, Source<std::string> value, float keyWidth)
    : key_(std::move(key)), value_(std::move(value)), keyWidth_(keyWidth)
{
}

void KeyValueText::render()
{
    // 键用弱化色，值正常色；SameLine(offset) 让值从固定横坐标开始，形成两列。
    ImGui::TextColored(colors::muted(), "%s", key_.get().c_str());
    ImGui::SameLine(keyWidth_);
    ImGui::TextUnformatted(value_.get().c_str());
}

// ----------------------------------------------------------------- MutedText

MutedText::MutedText(Source<std::string> text) : text_(std::move(text)) {}

void MutedText::render()
{
    ImGui::TextDisabled("%s", text_.get().c_str());
}

// ------------------------------------------------------------------ LinkText

LinkText::LinkText(Source<std::string> label, std::string url)
    : label_(std::move(label)), url_(std::move(url))
{
}

void LinkText::render()
{
    // ImGui 自带 TextLinkOpenURL：Windows 上走 ShellExecute，
    // 其他平台由后端处理；点击后立刻返回，不会阻塞。
    ImGui::TextLinkOpenURL(label_.get().c_str(), url_.c_str());
}

// --------------------------------------------------------------------- Badge

Badge::Badge(Source<std::string> text, const ImVec4& background)
    : text_(std::move(text)), background_(background)
{
}

void Badge::render()
{
    const std::string text = text_.get();
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 padding(6.0f, 2.0f);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 size(textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(position, ImVec2(position.x + size.x, position.y + size.y),
                        ImGui::GetColorU32(background_), size.y * 0.5f);

    // 文字画在矩形内部，然后把光标移回矩形下方，保证后续控件不会叠上来。
    ImGui::SetCursorScreenPos(ImVec2(position.x + padding.x, position.y + padding.y));
    ImGui::PushStyleColor(ImGuiCol_Text, colors::white());
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopStyleColor();

    ImGui::SetCursorScreenPos(ImVec2(position.x, position.y + size.y + ImGui::GetStyle().ItemSpacing.y));
}

} // namespace echo
