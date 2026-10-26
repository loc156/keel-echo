// Echo UI 库 - 文本类控件。
//
// 这里全部是"只显示、不交互"的控件，风格接近一篇文章的排版：
// 标题、正文、强调、列表、键值对、状态条……
//
// 所有文本参数都是 Source<std::string>（见 source.h）：
//   PlainText("固定文字")                      // 快照
//   PlainText(&someString)                     // 指针，值变则显示变
//   PlainText([&] { return std::to_string(n); }) // 每帧算
#pragma once

#include <string>
#include <utility>

#include "imgui.h"
#include "source.h"
#include "theme.h"
#include "widget.h"

namespace echo {

/// 普通正文：原样显示，不做格式化（`%` 不会被当占位符）。
/// 需要换行就自己塞 '\n'，需要自动折行用 WrappedText。
class PlainText : public Widget {
public:
    explicit PlainText(Source<std::string> text) : text_(std::move(text)) {}
    void render() override;
    const char* type() const override { return "PlainText"; }

private:
    Source<std::string> text_;
};

/// 指定颜色的正文。
class ColoredText : public Widget {
public:
    ColoredText(Source<std::string> text, const ImVec4& color);
    void render() override;
    const char* type() const override { return "ColoredText"; }

private:
    Source<std::string> text_;
    ImVec4 color_;
};

/// 小节标题：彩色文字 + 一条分隔线（等价于 ImGui::SeparatorText）。
/// 默认用主题强调色；适合给窗口内容分块。
class HeadingText : public Widget {
public:
    explicit HeadingText(Source<std::string> text);
    HeadingText(Source<std::string> text, const ImVec4& color);
    void render() override;
    const char* type() const override { return "HeadingText"; }

private:
    Source<std::string> text_;
    ImVec4 color_;
};

/// 一条水平分隔线。
class Separator : public Widget {
public:
    void render() override;
    const char* type() const override { return "Separator"; }
};

/// 缩进一行的正文，用来表现层级（配合 ListItemText 做树状列表）。
/// 第二个参数是缩进像素，默认 20。
class IndentText : public Widget {
public:
    explicit IndentText(Source<std::string> text);
    IndentText(Source<std::string> text, float indentPixels);
    void render() override;
    const char* type() const override { return "IndentText"; }

private:
    Source<std::string> text_;
    float indent_ = 20.0f;
};

/// 指标条：「标签: 当前值 / 上限」+ 一条细进度条，颜色按比例自动变。
///
///     比例 > 60%  绿     (正常)
///     30% ~ 60%   黄     (注意)
///     < 30%       红     (告警，取决于你怎么定义好坏)
///     max <= 0    灰     (没有上限的数据，例如"连接数")
///
/// 值每帧重新读取，所以直接传指针/回调就能做实时监控面板：
///     list.add<MeterText>("CPU", &cpuPercent, 100.0f, "%");
class MeterText : public Widget {
public:
    /// label 标签，value 当前值，maximum 上限，suffix 单位（"%"/"MB"...）。
    /// showBar = false 时只画文字，不画那条细进度条。
    MeterText(Source<std::string> label, Source<float> value, Source<float> maximum = 100.0f,
              Source<std::string> suffix = std::string{}, bool showBar = true);

    /// 整数版本：一次性的字面量（例如 "队列", 12, 50）。
    MeterText(Source<std::string> label, int value, int maximum, Source<std::string> suffix = std::string{});

    /// 整数版本：传指针就能实时更新（例如 "队列", &queueLength, 50）。
    MeterText(Source<std::string> label, const int* value, int maximum,
              Source<std::string> suffix = std::string{});

    void render() override;
    const char* type() const override { return "MeterText"; }

private:
    Source<std::string> label_;
    Source<float> value_;
    Source<float> maximum_;
    Source<std::string> suffix_;
    bool bar_ = true;
    bool integer_ = false;
};

/// 发言/来源式的一行：「说话者: 内容」，内容自动折行。
/// 非对话场景也常用：日志行、告警行、提交记录都能这么排。
class SpeakerText : public Widget {
public:
    SpeakerText(Source<std::string> speaker, Source<std::string> message);
    SpeakerText(Source<std::string> speaker, Source<std::string> message, const ImVec4& speakerColor);
    void render() override;
    const char* type() const override { return "SpeakerText"; }

private:
    Source<std::string> speaker_;
    Source<std::string> message_;
    ImVec4 color_ = ImVec4(0.60f, 0.80f, 1.00f, 1.00f);
};

/// 列表项：「前缀 + 文字」，默认前缀是圆点 "• "。
/// 前缀可以换成 "OK " / "FAIL " 之类，也可以用颜色区分。
class ListItemText : public Widget {
public:
    explicit ListItemText(Source<std::string> text);
    ListItemText(Source<std::string> text, std::string prefix);
    ListItemText(Source<std::string> text, std::string prefix, const ImVec4& color);
    void render() override;
    const char* type() const override { return "ListItemText"; }

private:
    Source<std::string> text_;
    std::string prefix_ = "\xE2\x80\xA2 "; // U+2022 圆点
    ImVec4 color_ = colors::white();
};

/// 空白：默认是 ImGui 的行间距，传 height > 0 则是固定像素的空行。
class Spacer : public Widget {
public:
    Spacer() = default;
    explicit Spacer(float height);
    void render() override;
    const char* type() const override { return "Spacer"; }

private:
    float height_ = 0.0f;
};

/// 带背景色的文字（标签、提示、警告条）。
///
/// 实现说明：背景是直接画在当前窗口的 draw list 上的矩形，再画文字，
/// 因为 ImGui 的文本绘制不读 FrameBg 颜色——只压样式是画不出背景的。
class HighlightText : public Widget {
public:
    HighlightText(Source<std::string> text, const ImVec4& background);
    HighlightText(Source<std::string> text, const ImVec4& background, const ImVec4& foreground);
    void render() override;
    const char* type() const override { return "HighlightText"; }

private:
    Source<std::string> text_;
    ImVec4 background_;
    ImVec4 foreground_ = colors::white();
};

/// 自动折行的正文（适合长说明、帮助文字）。
class WrappedText : public Widget {
public:
    explicit WrappedText(Source<std::string> text);
    void render() override;
    const char* type() const override { return "WrappedText"; }

private:
    Source<std::string> text_;
};

/// 「键        值」两列对齐的一行。keyWidth 是值开始的横坐标偏移。
class KeyValueText : public Widget {
public:
    KeyValueText(Source<std::string> key, Source<std::string> value, float keyWidth = 120.0f);
    void render() override;
    const char* type() const override { return "KeyValueText"; }

private:
    Source<std::string> key_;
    Source<std::string> value_;
    float keyWidth_ = 120.0f;
};

/// 灰色小字：说明、单位、补充信息。用 TextDisabled 的样式。
class MutedText : public Widget {
public:
    explicit MutedText(Source<std::string> text);
    void render() override;
    const char* type() const override { return "MutedText"; }

private:
    Source<std::string> text_;
};

/// 可点击的链接，点了用系统默认浏览器打开。
/// 实现走 ImGui::TextLinkOpenURL（跨平台：由后端的 platform IO 代为打开）。
class LinkText : public Widget {
public:
    LinkText(Source<std::string> label, std::string url);
    void render() override;
    const char* type() const override { return "LinkText"; }

private:
    Source<std::string> label_;
    std::string url_;
};

/// 小圆角标签（badge / chip），用于状态、分类、计数。
class Badge : public Widget {
public:
    Badge(Source<std::string> text, const ImVec4& background);
    void render() override;
    const char* type() const override { return "Badge"; }

private:
    Source<std::string> text_;
    ImVec4 background_;
};

} // namespace echo
