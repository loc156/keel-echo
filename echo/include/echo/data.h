// Echo UI 库 - 数据展示类控件：表格、树、页签、曲线图。
//
// 这几个控件会"递归"或"逐行"绘制别的东西，所以它们既是控件也是容器：
//   - Table   每一帧调用你给的 row 回调，你在回调里一行行填；
//   - TreeNode 自带 children()，可以往里 add 任意控件，形成树；
//   - TabBar  每个页签各有一个 Content；
//   - Plot    只是把一块 float 数组画成折线，不拥有数据。
//
// 约定：Table 的行回调里**只能在表格内部**调用 row()，且每帧都要重新填一遍
// （这是 ImGui 的立即模式特性，不是 Echo 的限制）。
#pragma once

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "dragdrop.h"
#include "imgui.h"
#include "source.h"
#include "widget.h"

namespace echo {

/// 多列表格：表头 + 若干行文本。
///
///     auto& table = list.add<Table>(std::vector<Table::Column>{
///         {"任务", 0.0f}, {"状态", 0.0f}, {"进度", 0.0f}});
///     table.setRows([&](Table& t) {
///         for (const auto& task : tasks)
///             t.row({task.name, task.state, std::to_string(task.progress) + "%"});
///     });
///
/// 单元格目前只能放文本；要放按钮等控件，用 TreeNode/Content 自己拼，或者在
/// 回调里直接调 ImGui（Table 只包了 BeginTable/EndTable，不限制你在里面做什么）。
class Table : public Widget {
public:
    struct Column {
        std::string title;
        float width = 0.0f; ///< 0 = 均分可用宽度（WidthStretch）；>0 为固定像素宽
    };

    /// 单行的可选行为（拖拽/放置）。默认构造 = 普通只读行。
    struct RowOptions {
        DragPayload drag;                                  ///< 非空 => 整行可以拖
        std::vector<std::string> accept;                   ///< 非空 => 整行可以接
        std::function<void(const DragPayload&)> onDrop;    ///< 放下时回调
    };

    explicit Table(std::vector<Column> columns,
                   ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp);

    /// 设置"如何填行"。每帧在表格内部调用一次。
    void setRows(std::function<void(Table&)> rows) { rows_ = std::move(rows); }

    /// 追加一行（必须在 rows 回调里调用）。
    void row(const std::vector<std::string>& cells);
    void row(std::initializer_list<std::string> cells);

    /// 带拖拽/放置能力的行：整行可拖、可接，松开触发 options.onDrop。
    ///
    ///     table.row({name, state}, Table::RowOptions{
    ///         .drag = DragPayload::of<ItemRef>("demo/item", ref),
    ///         .accept = {"demo/item"},
    ///         .onDrop = [&](const DragPayload& p) { /* 移动到这里 */ }});
    void row(const std::vector<std::string>& cells, const RowOptions& options);

    /// 当前列数。
    size_t columnCount() const { return columns_.size(); }

    void render() override;
    const char* type() const override { return "Table"; }

private:
    std::vector<Column> columns_;
    std::function<void(Table&)> rows_;
    ImGuiTableFlags flags_;
    int rowCounter_ = 0; ///< 每帧归零，用来给行内的命中区生成唯一 ID
};

/// 可折叠的树节点，自带子控件列表。
///
///     auto& node = list.add<TreeNode>("高级选项");
///     node.add<Checkbox>("启用缓存", &cacheEnabled, [&](bool v) { cacheEnabled = v; });
///     node.add<Spacer>(4.0f);
///     node.add<MutedText>("改动会立即生效");
class TreeNode : public Widget {
public:
    /// label 显示的标题；defaultOpen 为 true 时第一帧就是展开的。
    explicit TreeNode(Source<std::string> label, bool defaultOpen = false);

    /// 子控件列表（和 Content 一样用）。
    Content& children() { return children_; }

    /// 便捷写法：node.add<PlainText>("...")。
    template <class T, class... Args>
    T& add(Args&&... args)
    {
        return children_.add<T>(std::forward<Args>(args)...);
    }

    /// 程序控制展开/折叠（下一帧生效）。
    void setOpen(bool open);
    bool open() const { return open_; }

    void render() override;
    const char* type() const override { return "TreeNode"; }

private:
    Source<std::string> label_;
    Content children_;
    bool open_ = false;
    bool initialized_ = false;
    bool requestOpen_ = false;
    bool requestedOpen_ = false;
};

/// 页签容器：每个页签有自己的 Content。
///
///     auto& tabs = list.add<TabBar>(std::vector<std::string>{"概览", "设置"});
///     tabs.tab(0).add<PlainText>("第一页的内容");
///     tabs.tab(1).add<Checkbox>("开机自启", &autostart, ...);
class TabBar : public Widget {
public:
    explicit TabBar(std::vector<std::string> titles);

    /// 第 index 个页签的内容，越界返回一个空的占位 Content。
    Content& tab(size_t index);
    size_t count() const { return tabs_.size(); }

    /// 程序选择某一页（下一帧生效）。用于"打开时默认停在第二页"这类需求。
    void select(size_t index);

    void render() override;
    const char* type() const override { return "TabBar"; }

private:
    struct Tab {
        std::string title;
        Content content;
    };
    std::vector<Tab> tabs_;
    Content empty_;
    size_t selected_ = 0;
    bool selectPending_ = false;
};

/// 折线图。数据是外部数组（通常是一个固定长度的环形缓冲）。
///
///     float history[120] = {};
///     list.add<Plot>("吞吐", history, 120, 0.0f, 100.0f, ImVec2(0, 60));
///     // 之后只要改 history 里的数，图就跟着动
class Plot : public Widget {
public:
    /// caption 显示在图左上角，同时也是空字符串时的占位；
    /// values 必须活到控件销毁；count 是点数；minimum/maximum 是纵轴范围。
    Plot(Source<std::string> caption, const float* values, int count, float minimum = 0.0f, float maximum = 1.0f,
         ImVec2 size = ImVec2(0, 60));

    void setRange(float minimum, float maximum)
    {
        minimum_ = minimum;
        maximum_ = maximum;
    }
    void setSize(ImVec2 size) { size_ = size; }

    void render() override;
    const char* type() const override { return "Plot"; }

private:
    Source<std::string> caption_;
    const float* values_ = nullptr;
    int count_ = 0;
    float minimum_ = 0.0f;
    float maximum_ = 1.0f;
    ImVec2 size_ = ImVec2(0, 60);
};

} // namespace echo
