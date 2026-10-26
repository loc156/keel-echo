// Echo UI 库 - 布局容器：把一个 Content 里的控件排成一行 / 排成网格。
//
// 【为什么需要】
// Content 天生是"纵向流"：add 一个控件就换一行。这是绝大多数面板的排版方式，
// 但键盘、工具栏、3×3 计算器这种"同一行放多个按钮"的场景就没法直接写。
//
// 这一层提供两个容器控件（它们自己也是 Widget，所以可以嵌套）：
//
//   Row    横向排列：子控件依次画在同一行
//   Grid   网格排列：N 列，子控件按行优先填格子（列宽自动对齐）
//
// 用法（3×3 数字键盘）：
//     auto& pad = panel.add<Grid>(3);
//     for (const char* label : {"7", "8", "9", "4", "5", "6", "1", "2", "3"})
//         pad.add<Button>(label, [&] { input(label[0]); }, ImVec2(-1, 36));
//
// 说明：
//   - `ImVec2(-1, h)` 是 ImGui 的约定，表示"宽度占满可用空间"，在网格里就是占满单元格；
//   - Grid 用 ImGui 的表格实现，所以列宽天然对齐（Row 只是简单 SameLine，不会对齐）；
//   - 容器可以嵌套：Row 里放 Grid、Grid 里放 Row 都行。
#pragma once

#include <utility>

#include "imgui.h"
#include "widget.h"

namespace echo {

/// 横向排列容器：子控件依次画在同一行。
///
///     auto& bar = panel.add<Row>();
///     bar.add<Button>("保存", [&] { save(); });
///     bar.add<Button>("取消", [&] { cancel(); });
///
/// spacing 是控件之间的间距（像素），传负值表示用主题默认间距。
class Row : public Widget {
public:
    explicit Row(float spacing = -1.0f);

    /// 追加子控件，返回引用。
    template <class T, class... Args>
    T& add(Args&&... args)
    {
        return children_.add<T>(std::forward<Args>(args)...);
    }

    void render() override;
    const char* type() const override { return "Row"; }

    Content& children() { return children_; }

private:
    Content children_;
    float spacing_ = -1.0f;
};

/// 网格容器：N 列，子控件按"行优先"填格子。
///
///     auto& grid = panel.add<Grid>(2);
///     grid.add<TextInput>("账号", &user);
///     grid.add<TextInput>("密码", &pass, 64);
///
/// columns 最小按 1 处理；size 传 (0,0) 表示宽度自适应、高度按内容。
/// borders = true 会画出单元格边框（调试排版时有用）。
class Grid : public Widget {
public:
    explicit Grid(int columns, ImVec2 size = ImVec2(0, 0), bool borders = false);

    /// 追加子控件（按添加顺序填格子），返回引用。
    template <class T, class... Args>
    T& add(Args&&... args)
    {
        return children_.add<T>(std::forward<Args>(args)...);
    }

    void render() override;
    const char* type() const override { return "Grid"; }

    Content& children() { return children_; }
    int columns() const { return columns_; }
    void setColumns(int columns) { columns_ = columns < 1 ? 1 : columns; }

private:
    int columns_ = 1;
    ImVec2 size_{0.0f, 0.0f};
    bool borders_ = false;
    Content children_;
};

} // namespace echo
