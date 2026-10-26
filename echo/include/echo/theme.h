// Echo UI 库 - 颜色与样式（主题）。
//
// 【两层概念，别混】
//   colors::xxx()  只是"一组好看的颜色常量"，不改变任何全局状态；
//   theme::xxx()   真正动 ImGui 的样式表（圆角、间距、强调色、字号缩放）。
//
// 典型用法：
//     theme::dark();                  // 深色主题（默认）
//     theme::setAccent(colors::cyan()); // 换强调色，按钮/标题/进度条跟着变
//
// theme:: 的函数几乎都会**重新构建整张样式表**（不是增量修改），
// 所以你可以随时来回切、反复调用，不会出现样式叠加。
// 唯一例外是 rounding()：它就地改当前样式表，之后任何一次 dark()/light()/
// setAccent()/setFontScale() 都会把它冲掉——要固定圆角，请在那之后调它。
#pragma once

#include "imgui.h"

namespace echo {

/// 一组常用颜色（0~1 的 RGBA），不涉及全局状态。
namespace colors {

constexpr ImVec4 white() { return ImVec4(1.00f, 1.00f, 1.00f, 1.00f); }
constexpr ImVec4 black() { return ImVec4(0.00f, 0.00f, 0.00f, 1.00f); }
constexpr ImVec4 text() { return ImVec4(0.90f, 0.91f, 0.92f, 1.00f); }
constexpr ImVec4 muted() { return ImVec4(0.55f, 0.57f, 0.60f, 1.00f); }
constexpr ImVec4 gold() { return ImVec4(1.00f, 0.80f, 0.20f, 1.00f); }
constexpr ImVec4 red() { return ImVec4(1.00f, 0.28f, 0.28f, 1.00f); }
constexpr ImVec4 green() { return ImVec4(0.35f, 0.90f, 0.40f, 1.00f); }
constexpr ImVec4 blue() { return ImVec4(0.28f, 0.55f, 1.00f, 1.00f); }
constexpr ImVec4 yellow() { return ImVec4(1.00f, 0.90f, 0.10f, 1.00f); }
constexpr ImVec4 orange() { return ImVec4(1.00f, 0.60f, 0.15f, 1.00f); }
constexpr ImVec4 purple() { return ImVec4(0.62f, 0.35f, 0.95f, 1.00f); }
constexpr ImVec4 gray() { return ImVec4(0.50f, 0.50f, 0.50f, 1.00f); }
constexpr ImVec4 cyan() { return ImVec4(0.20f, 0.85f, 0.90f, 1.00f); }
constexpr ImVec4 pink() { return ImVec4(1.00f, 0.45f, 0.70f, 1.00f); }
constexpr ImVec4 danger() { return ImVec4(0.90f, 0.30f, 0.30f, 1.00f); }
constexpr ImVec4 success() { return ImVec4(0.35f, 0.85f, 0.45f, 1.00f); }
constexpr ImVec4 warning() { return ImVec4(0.95f, 0.72f, 0.20f, 1.00f); }
constexpr ImVec4 info() { return ImVec4(0.45f, 0.70f, 1.00f, 1.00f); }

} // namespace colors

namespace theme {

/// 深色主题（默认）。会把样式表整体替换成 Echo 调过的版本。
void dark();

/// 浅色主题。
void light();

/// 强调色：按钮、标题分隔线、进度条、选中项都用它。
void setAccent(const ImVec4& color);

/// 当前强调色。
ImVec4 accent();

/// 全局字号缩放（1.0 = 原样）。
/// 注意：DPI 缩放不需要你调这个——后端会按显示器密度栅格化字形；
/// 这里的 1.5 是给"用户放大界面"用的，会同时放大字号与所有尺寸。
void setFontScale(float scale);

/// 当前字号缩放。
float fontScale();

/// 只改圆角（窗口/子窗口/边框/弹出层/抓取块），就地修改当前样式表；
/// 注意 TabRounding / ScrollbarRounding 不在其内（它们只由 dark()/light() 设定）。
void rounding(float radius);

} // namespace theme
} // namespace echo
