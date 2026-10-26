// Echo UI 库 - 输入与反馈类控件。
//
// 这一类都会"改值"。约定：控件的值来自 Source（只读地显示），
// 用户操作后通过 onChange 把新值交还给调用方——库自己不保存业务状态。
//
//     int interval = 500;
//     list.add<SliderInt>("采样间隔(ms)", &interval, 100, 2000,
//                         [&](int v) { interval = v; });
//
// 注意：如果你传的是快照（`list.add<SliderInt>("x", 5, ...)`），拖动会弹回去，
// 因为没有地方保存新值。要么传指针，要么在 onChange 里写回某个变量。
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_stdlib.h"
#include "source.h"
#include "theme.h"
#include "widget.h"

namespace echo {

/// 单行文本输入，直接编辑你给的那个 std::string。
///
/// capacity 只是"提交时把过长的字符串截断"的安全阀，ImGui 本身不限制长度。
/// 带 onSubmit 的构造版本会在回车时回调，并把焦点自动放回输入框（适合命令输入）。
class TextInput : public Widget {
public:
    TextInput(std::string label, std::string* buffer, size_t capacity = 256);
    TextInput(std::string label, std::string* buffer, std::function<void()> onSubmit, size_t capacity = 256);

    void render() override;
    const char* type() const override { return "TextInput"; }

    /// 控件宽度（像素），默认 240；负数表示占满剩余宽度。
    void setWidth(float width) { width_ = width; }

    /// 悬停提示，nullptr 表示没有。
    void setHint(const char* hint) { hint_ = hint; }

private:
    std::string label_;
    std::string* buffer_ = nullptr;
    size_t capacity_ = 256;
    std::function<void()> onSubmit_;
    bool submitOnEnter_ = false;
    bool refocusOnSubmit_ = true;
    bool focusNext_ = false;
    float width_ = 240.0f;
    const char* hint_ = nullptr;
};

/// 复选框。点击后把新状态交给 onChange。
class Checkbox : public Widget {
public:
    Checkbox(Source<std::string> label, Source<bool> state, std::function<void(bool)> onChange);

    void render() override;
    const char* type() const override { return "Checkbox"; }

private:
    Source<std::string> label_;
    Source<bool> state_;
    std::function<void(bool)> onChange_;
};

/// 单选组：options 里选一个，index 是当前选中的下标。
///
///     std::vector<std::string> modes{"批处理", "交互", "流式"};
///     list.add<RadioGroup>("模式", modes, &modeIndex, [&](int i) { modeIndex = i; });
///
/// horizontal = true 时排成一行，否则一行一个。
class RadioGroup : public Widget {
public:
    RadioGroup(Source<std::string> label, std::vector<std::string> options, Source<int> index,
               std::function<void(int)> onChange, bool horizontal = false);

    void render() override;
    const char* type() const override { return "RadioGroup"; }

private:
    Source<std::string> label_;
    std::vector<std::string> options_;
    Source<int> index_;
    std::function<void(int)> onChange_;
    bool horizontal_ = false;
};

/// 整数滑条，范围 [minimum, maximum]。
class SliderInt : public Widget {
public:
    SliderInt(Source<std::string> label, Source<int> value, int minimum, int maximum,
              std::function<void(int)> onChange = {});

    void render() override;
    const char* type() const override { return "SliderInt"; }
    void setWidth(float width) { width_ = width; }

private:
    Source<std::string> label_;
    Source<int> value_;
    int minimum_ = 0;
    int maximum_ = 100;
    std::function<void(int)> onChange_;
    float width_ = 200.0f;
};

/// 浮点滑条。
class SliderFloat : public Widget {
public:
    SliderFloat(Source<std::string> label, Source<float> value, float minimum, float maximum,
                std::function<void(float)> onChange = {}, const char* format = "%.2f");

    void render() override;
    const char* type() const override { return "SliderFloat"; }
    void setWidth(float width) { width_ = width; }

private:
    Source<std::string> label_;
    Source<float> value_;
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    std::function<void(float)> onChange_;
    const char* format_ = "%.2f";
    float width_ = 200.0f;
};

/// 整数输入框（可键入，也可用 +/- 按钮）。
class InputInt : public Widget {
public:
    InputInt(Source<std::string> label, Source<int> value, std::function<void(int)> onChange = {}, int step = 1,
             int stepFast = 100);

    void render() override;
    const char* type() const override { return "InputInt"; }
    void setWidth(float width) { width_ = width; }

private:
    Source<std::string> label_;
    Source<int> value_;
    std::function<void(int)> onChange_;
    int step_ = 1;
    int stepFast_ = 100;
    float width_ = 200.0f;
};

/// 浮点拖动条（按住左右拖，比滑条更省空间）。
class DragFloat : public Widget {
public:
    DragFloat(Source<std::string> label, Source<float> value, float speed = 1.0f, float minimum = 0.0f,
              float maximum = 0.0f, std::function<void(float)> onChange = {}, const char* format = "%.2f");

    void render() override;
    const char* type() const override { return "DragFloat"; }
    void setWidth(float width) { width_ = width; }

private:
    Source<std::string> label_;
    Source<float> value_;
    float speed_ = 1.0f;
    float minimum_ = 0.0f;
    float maximum_ = 0.0f;
    std::function<void(float)> onChange_;
    const char* format_ = "%.2f";
    float width_ = 200.0f;
};

/// 进度条。fraction 会被夹到 [0,1]；overlay 显示在条内（空串则不显示）。
class ProgressBar : public Widget {
public:
    ProgressBar(Source<float> fraction, Source<std::string> overlay = std::string{},
                ImVec2 size = ImVec2(-1, 0));

    void render() override;
    const char* type() const override { return "ProgressBar"; }
    void setSize(ImVec2 size) { size_ = size; }

private:
    Source<float> fraction_;
    Source<std::string> overlay_;
    ImVec2 size_ = ImVec2(-1, 0);
};

/// 转圈动画：没有确定进度时表示"正在忙"。纯绘制，不占状态。
class Spinner : public Widget {
public:
    explicit Spinner(float radius = 8.0f, const ImVec4& color = colors::info(), float speed = 4.0f);

    void render() override;
    const char* type() const override { return "Spinner"; }

private:
    float radius_ = 8.0f;
    ImVec4 color_;
    float speed_ = 4.0f;
};

/// 下拉选择。items 必须是"活到控件销毁"的字符串指针数组（通常写 static 数组）。
class Combo : public Widget {
public:
    Combo(Source<std::string> label, const char* const* items, int count, Source<int> index,
          std::function<void(int)> onChange = {});

    void render() override;
    const char* type() const override { return "Combo"; }
    void setWidth(float width) { width_ = width; }

private:
    Source<std::string> label_;
    const char* const* items_ = nullptr;
    int count_ = 0;
    Source<int> index_;
    std::function<void(int)> onChange_;
    float width_ = 200.0f;
};

/// 颜色选择器（RGBA）。点击色块弹出取色面板。
class ColorEdit : public Widget {
public:
    ColorEdit(Source<std::string> label, Source<ImVec4> color, std::function<void(ImVec4)> onChange);

    void render() override;
    const char* type() const override { return "ColorEdit"; }

private:
    Source<std::string> label_;
    Source<ImVec4> color_;
    std::function<void(ImVec4)> onChange_;
};

} // namespace echo
