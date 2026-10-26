// Echo UI 库 - 拖拽与放置的实现。
//
// ImGui 的原语是四对函数：
//   BeginDragDropSource / SetDragDropPayload / [画预览] / EndDragDropSource
//   BeginDragDropTarget / AcceptDragDropPayload / EndDragDropTarget
// 这里把它们收成 beginSource / accept 两个调用，并补上"悬停判断"与两个控件。
#include "echo/dragdrop.h"

#include <string>

namespace echo {

namespace dnd {

namespace {

// 把 initializer_list 和 vector 统一成"逐个试"的形式。
template <class Fn>
bool tryTypes(std::initializer_list<const char*> types, Fn&& fn)
{
    for (const char* type : types) {
        if (type != nullptr && fn(type))
            return true;
    }
    return false;
}

} // namespace

bool beginSource(const DragPayload& payload, const char* previewText, ImGuiDragDropFlags flags)
{
    if (payload.empty())
        return false;
    if (!ImGui::BeginDragDropSource(flags))
        return false;

    // SetDragDropPayload 会自己拷贝一份数据，所以 payload 活多久都不影响拖拽。
    // 这里传的是"含内部头部"的原始缓冲，接收端用 fromImGui 原样拷回来。
    ImGui::SetDragDropPayload(payload.type().c_str(), payload.raw(), payload.rawSize());

    if (previewText != nullptr && *previewText != '\0')
        ImGui::TextUnformatted(previewText);
    else
        ImGui::TextUnformatted(payload.type().c_str());

    ImGui::EndDragDropSource();
    return true;
}

std::optional<DragPayload> accept(std::initializer_list<const char*> types, ImGuiDragDropFlags flags)
{
    if (!ImGui::BeginDragDropTarget())
        return std::nullopt;

    std::optional<DragPayload> result;
    tryTypes(types, [&](const char* type) {
        if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(type, flags)) {
            result = DragPayload::fromImGui(*accepted);
            return true;
        }
        return false;
    });

    ImGui::EndDragDropTarget();
    return result;
}

std::optional<DragPayload> accept(const std::vector<std::string>& types, ImGuiDragDropFlags flags)
{
    if (!ImGui::BeginDragDropTarget())
        return std::nullopt;

    std::optional<DragPayload> result;
    for (const std::string& type : types) {
        if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload(type.c_str(), flags)) {
            result = DragPayload::fromImGui(*accepted);
            break;
        }
    }

    ImGui::EndDragDropTarget();
    return result;
}

bool canAccept(std::initializer_list<const char*> types)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload == nullptr)
        return false;
    return tryTypes(types, [&](const char* type) { return payload->IsDataType(type); });
}

bool canAccept(const std::vector<std::string>& types)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload == nullptr)
        return false;
    for (const std::string& type : types) {
        if (payload->IsDataType(type.c_str()))
            return true;
    }
    return false;
}

} // namespace dnd

// ----------------------------------------------------------------- DragItem

DragItem::DragItem(Source<std::string> label, DragPayload payload, bool selected)
    : label_(std::move(label)), payload_(std::move(payload)), selected_(selected)
{
}

void DragItem::render()
{
    ImGui::PushID(this);

    const std::string label = label_.get();
    ImGui::Selectable(label.c_str(), selected_);

    // 拖起来之后用这一行的文字当预览（ImGui 会画在鼠标旁边）。
    if (!payload_.empty())
        dnd::beginSource(payload_, label.c_str());

    ImGui::PopID();
}

// ----------------------------------------------------------------- DropZone

DropZone::DropZone(std::vector<std::string> types, Handler onDrop, Source<std::string> placeholder, ImVec2 size)
    : types_(std::move(types)), onDrop_(std::move(onDrop)), placeholder_(std::move(placeholder)), size_(size)
{
}

void DropZone::render()
{
    // 同一个窗口里可能放多个投放区，PushID 保证各自的 ##echo_dropzone 不撞。
    ImGui::PushID(this);

    // 宽度传负数 = 占满剩余宽度。
    const float width = size_.x < 0.0f ? ImGui::GetContentRegionAvail().x : size_.x;
    const ImVec2 size(width, size_.y);
    const ImVec2 position = ImGui::GetCursorScreenPos();

    // 用一个不可见的按钮当命中区，这样它才能接收悬停、拖拽命中。
    ImGui::InvisibleButton("##echo_dropzone", size);
    const bool hoveredNow = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    hoveredAcceptable_ = hoveredNow && dnd::canAccept(types_);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(position.x + size.x, position.y + size.y);
    const ImVec4 accent = theme::accent();
    const ImVec4 border = hoveredAcceptable_ ? accent : colors::muted();

    draw->AddRectFilled(position, max,
                        ImGui::GetColorU32(hoveredAcceptable_ ? ImVec4(accent.x, accent.y, accent.z, 0.22f)
                                                              : ImVec4(0.0f, 0.0f, 0.0f, 0.18f)),
                        4.0f);
    draw->AddRect(position, max, ImGui::GetColorU32(border), 4.0f, 0, hoveredAcceptable_ ? 2.0f : 1.0f);

    const std::string text = placeholder_.get();
    if (!text.empty()) {
        const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        draw->AddText(ImVec2(position.x + (size.x - textSize.x) * 0.5f, position.y + (size.y - textSize.y) * 0.5f),
                      ImGui::GetColorU32(hoveredAcceptable_ ? accent : colors::muted()), text.c_str());
    }

    // 松手时取数据；accept 内部会处理"类型不匹配"和"还没松手"。
    if (auto dropped = dnd::accept(types_)) {
        if (onDrop_)
            onDrop_(*dropped);
    }

    ImGui::PopID();
}

} // namespace echo
