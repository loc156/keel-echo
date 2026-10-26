// Echo UI 库 - 按钮类控件。
//
// 两种语义，别混：
//   Button        点一下就执行一次回调（"动作"）
//   ToggleButton  维护一个开/关状态，点一下翻转（"开关"）
//
// 旧版本有个 Button 会在每帧把绑定的 bool 写成 false，只能做"按住"，
// 而且会把同一变量写乱；现在这两种需求各有各的控件。
#pragma once

#include <functional>
#include <string>
#include <utility>

#include "imgui.h"
#include "source.h"
#include "theme.h"
#include "widget.h"

namespace echo {

/// 普通按钮：松开鼠标时触发一次 onClick。
///
///     auto& btn = list.add<Button>("重新扫描", [&] { rescanner.restart(); });
///     btn.setEnabled(false);                       // 变灰且不响应
///     btn.setTooltip("扫描所有磁盘");               // 悬停提示
///     list.add<Button>("小号", cb, ImVec2(0, 0), true);   // small = true
class Button : public Widget {
public:
    /// label 显示文字（可以是 Source，做动态文案）
    /// onClick 点击回调，可以为空（只显示不响应）
    /// size 按钮尺寸，ImVec2(0,0) 表示按内容自适应
    /// small 用 ImGui 的小号按钮样式
    /// tooltip 悬停提示，空串表示没有
    Button(Source<std::string> label, std::function<void()> onClick = {}, ImVec2 size = ImVec2(0, 0),
           bool small = false, std::string tooltip = {});

    void render() override;
    const char* type() const override { return "Button"; }

    /// 禁用后按钮变灰、不触发回调。常用于"当前不能点"的场景。
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    /// 自定义底色（不设置则跟随主题强调色）。
    void setColor(const ImVec4& color);
    void setTooltip(std::string tooltip) { tooltip_ = std::move(tooltip); }

private:
    Source<std::string> label_;
    std::function<void()> onClick_;
    ImVec2 size_ = ImVec2(0, 0);
    bool small_ = false;
    bool enabled_ = true;
    std::string tooltip_;
    ImVec4 color_{};
    bool hasColor_ = false;
};

/// 开关按钮：显示当前状态（开=强调色），点击后把新状态交给 onChange。
///
///     bool streaming = false;
///     list.add<ToggleButton>("实时流", &streaming, [&](bool on) { streaming = on; });
///
/// 状态来源可以是快照/指针/lambda；onChange 里负责把值写回去。
class ToggleButton : public Widget {
public:
    ToggleButton(Source<std::string> label, Source<bool> state, std::function<void(bool)> onChange,
                 ImVec2 size = ImVec2(0, 0));

    void render() override;
    const char* type() const override { return "ToggleButton"; }

private:
    Source<std::string> label_;
    Source<bool> state_;
    std::function<void(bool)> onChange_;
    ImVec2 size_ = ImVec2(0, 0);
};

} // namespace echo
