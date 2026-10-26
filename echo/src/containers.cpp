// Echo UI 库 - 布局容器（Row / Grid）的实现。
//
// 两个容器都不保存业务状态，只是"决定子控件画在哪"。
// 它们自己也继承 Widget，所以可以互相嵌套、也可以放进 Table 的单元格里。
#include "echo/containers.h"

#include "echo/theme.h"

namespace echo {

// ---------------------------------------------------------------------- Row

Row::Row(float spacing) : spacing_(spacing) {}

void Row::render()
{
    bool first = true;
    children_.forEach([&](Widget& child) {
        // SameLine 作用于"下一个"控件，所以除了第一个之外都要先调一次。
        if (!first)
            ImGui::SameLine(0.0f, spacing_);
        first = false;
        child.render();
    });
}

// --------------------------------------------------------------------- Grid

Grid::Grid(int columns, ImVec2 size, bool borders)
    : columns_(columns < 1 ? 1 : columns), size_(size), borders_(borders)
{
}

void Grid::render()
{
    if (children_.empty())
        return;

    ImGui::PushID(this);

    // SizingStretchSame：所有列等宽，这样键盘/计算器的按钮才对得齐。
    ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame;
    if (borders_)
        flags |= ImGuiTableFlags_Borders;

    if (ImGui::BeginTable("##grid", columns_, flags, size_)) {
        children_.forEach([](Widget& child) {
            ImGui::TableNextColumn(); // 自动换行：到列尾就起新行
            child.render();
        });
        ImGui::EndTable();
    }

    ImGui::PopID();
}

} // namespace echo
