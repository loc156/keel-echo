// Echo UI 库 - 数据展示类控件的实现。
//
// 这四个控件都有"子内容"：表格逐行填充、树/页签持有 Content、图只是画数组。
// 共同注意点：都用了 PushID(this)，这样同名的两个表格/两个树节点不会串 ID。
#include "echo/data.h"

#include <string>

namespace echo {

// --------------------------------------------------------------------- Table

Table::Table(std::vector<Column> columns, ImGuiTableFlags flags)
    : columns_(std::move(columns)), flags_(flags)
{
}

void Table::row(const std::vector<std::string>& cells)
{
    row(cells, RowOptions{});
}

void Table::row(std::initializer_list<std::string> cells)
{
    row(std::vector<std::string>(cells), RowOptions{});
}

void Table::row(const std::vector<std::string>& cells, const RowOptions& options)
{
    ImGui::TableNextRow();

    const bool interactive = !options.drag.empty() || !options.accept.empty();
    if (!interactive) {
        for (size_t i = 0; i < columns_.size(); ++i) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(i < cells.size() ? cells[i].c_str() : "");
        }
        return;
    }

    // 整行的命中区：一个横跨所有列的空 Selectable，拖拽/放置都挂在它上面。
    // PushID(rowCounter_) 保证每一行的命中区 ID 不同，否则只有第一行响应。
    ImGui::PushID(rowCounter_++);
    ImGui::TableNextColumn();
    ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns);

    if (!options.drag.empty())
        dnd::beginSource(options.drag, cells.empty() ? nullptr : cells[0].c_str());

    if (!options.accept.empty()) {
        if (auto dropped = dnd::accept(options.accept)) {
            if (options.onDrop)
                options.onDrop(*dropped);
        }
    }

    // 命中区不画文字，第一列的文本用 SameLine 叠回去。
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted(cells.empty() ? "" : cells[0].c_str());

    for (size_t i = 1; i < columns_.size(); ++i) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(i < cells.size() ? cells[i].c_str() : "");
    }

    ImGui::PopID();
}

void Table::render()
{
    if (columns_.empty())
        return;

    ImGui::PushID(this);
    rowCounter_ = 0; // 每帧重新编号，保证行内命中区 ID 稳定且唯一
    if (ImGui::BeginTable("##table", static_cast<int>(columns_.size()), flags_)) {
        for (const Column& column : columns_) {
            const ImGuiTableColumnFlags flags =
                column.width > 0.0f ? ImGuiTableColumnFlags_WidthFixed : ImGuiTableColumnFlags_WidthStretch;
            ImGui::TableSetupColumn(column.title.c_str(), flags, column.width);
        }
        ImGui::TableHeadersRow();

        if (rows_)
            rows_(*this);

        ImGui::EndTable();
    }
    ImGui::PopID();
}

// ------------------------------------------------------------------ TreeNode

TreeNode::TreeNode(Source<std::string> label, bool defaultOpen)
    : label_(std::move(label)), open_(defaultOpen)
{
}

void TreeNode::setOpen(bool open)
{
    requestedOpen_ = open;
    requestOpen_ = true;
}

void TreeNode::render()
{
    ImGui::PushID(this);

    if (requestOpen_) {
        // 程序控制的展开/折叠：强制下一帧生效。
        ImGui::SetNextItemOpen(requestedOpen_, ImGuiCond_Always);
        requestOpen_ = false;
    } else if (!initialized_) {
        // 第一帧用构造参数决定初始状态，之后交给用户点。
        ImGui::SetNextItemOpen(open_, ImGuiCond_Once);
        initialized_ = true;
    }

    const bool expanded =
        ImGui::TreeNodeEx("##node", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", label_.get().c_str());

    open_ = expanded; // 记录真实状态，供 open() 查询

    if (expanded) {
        children_.render();
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// -------------------------------------------------------------------- TabBar

TabBar::TabBar(std::vector<std::string> titles)
{
    tabs_.reserve(titles.size());
    for (auto& title : titles)
        tabs_.push_back(Tab{std::move(title), Content{}});
}

Content& TabBar::tab(size_t index)
{
    if (index >= tabs_.size()) {
        empty_.clear();
        return empty_;
    }
    return tabs_[index].content;
}

void TabBar::select(size_t index)
{
    selected_ = index;
    selectPending_ = true;
}

void TabBar::render()
{
    ImGui::PushID(this);
    if (ImGui::BeginTabBar("##tabs")) {
        for (size_t i = 0; i < tabs_.size(); ++i) {
            auto& tab = tabs_[i];
            // SetSelected 只在"程序要求切换"的那一帧加，之后交还给用户点击。
            const ImGuiTabItemFlags flags = (selectPending_ && i == selected_) ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem(tab.title.c_str(), nullptr, flags)) {
                tab.content.render();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    selectPending_ = false;
    ImGui::PopID();
}

// ---------------------------------------------------------------------- Plot

Plot::Plot(Source<std::string> caption, const float* values, int count, float minimum, float maximum,
           ImVec2 size)
    : caption_(std::move(caption)), values_(values), count_(count), minimum_(minimum), maximum_(maximum),
      size_(size)
{
}

void Plot::render()
{
    if (values_ == nullptr || count_ <= 0)
        return;

    const std::string caption = caption_.get();

    // 标签同时被当作 ID，用 PushID 隔离，避免同一窗口里两个 Plot 撞名。
    ImGui::PushID(this);
    ImGui::PlotLines("##plot", values_, count_, 0, caption.empty() ? nullptr : caption.c_str(), minimum_,
                     maximum_, size_);
    ImGui::PopID();
}

} // namespace echo
