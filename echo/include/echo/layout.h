// Echo UI 库 - 布局小工具（直接作用于 ImGui 光标）。
//
// 这一层是给"自己继承 Window 写 draw()"的人用的便利函数；
// 如果你用的是 Content.add<T>() 那套控件列表，请用 containers.h 里的
// Row / Grid（它们本身就是控件，可以 add 进去、也可以嵌套）。
#pragma once

#include "imgui.h"

namespace echo {
namespace layout {

/// 不换行，继续在同一行往右排。
/// 作用于"下一个"控件；offsetFromStart 是横坐标，spacing 是额外间距（-1 用默认）。
inline void sameLine(float offsetFromStart = 0.0f, float spacing = -1.0f)
{
    ImGui::SameLine(offsetFromStart, spacing);
}

/// 一个空行（行高）。
inline void spacing() { ImGui::Spacing(); }

/// 一条分隔线。
inline void separator() { ImGui::Separator(); }

/// 临时缩进，记得配 unindent（或直接用 IndentText 控件）。
inline void indent(float width = 20.0f) { ImGui::Indent(width); }
inline void unindent(float width = 20.0f) { ImGui::Unindent(width); }

} // namespace layout
} // namespace echo
