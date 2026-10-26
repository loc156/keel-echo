// Echo UI 库 - 拖拽与放置（含跨窗口）。
//
// 【底层是什么】
// ImGui 自带一套拖拽原语：拖拽源（BeginDragDropSource）把一段字节放进一个"全局当前载荷"，
// 拖拽目标（BeginDragDropTarget）在鼠标松开时取回这段字节。因为载荷是上下文级的、
// 不是窗口级的，所以**天然支持跨窗口**：A 窗口里开始拖，B 窗口里松手就行。
//
// 【这一层补了什么】
//   1. DragPayload：带类型名 + 类型标记的字节包，装/取都做校验（大小相同但类型不同的结构体不会互相误读）；
//   2. dnd::beginSource / accept / canAccept：把 ImGui 那四对 Begin/End 收成三个函数；
//   3. DragItem：一条可拖动的行（列表、看板卡片、清单条目都用它）；
//   4. DropZone：一块虚线放置区，悬停高亮、松手回调；
//   5. Table::row(..., RowOptions)：整行可拖、整行可接（表格行拖拽的常用形态）。
//
// 【重要约定：库只搬运，不搬运业务】
//   拖拽传递的是"你是谁"（例如第几列第几项、任务 id），不是"怎么改数据"。
//   移动语义（从 A 删除、往 B 插入）必须由你的 onDrop 回调完成——库不碰你的容器。
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "imgui.h"
#include "source.h"
#include "theme.h"
#include "widget.h"

namespace echo {

namespace detail {

// 每个类型一个唯一标记（地址即 id）。用来防止"大小相同但类型不同"的载荷被误读。
template <class T>
struct PayloadTag {
    static inline char value = 0;
};

} // namespace detail

/// 一次拖拽携带的数据：一个类型名 + 一段原始字节。
///
/// 类型名用于"谁能接"：只有请求了同一类型的目标才会收到；
/// 字节内容由你决定，通常是结构体/下标/id。必须可平凡拷贝（POD），不要塞 std::string。
///
///     struct CardRef { int column; int index; };
///     auto payload = DragPayload::of<CardRef>("demo/card", CardRef{1, 3});
///     ...
///     if (auto ref = dropped.get<CardRef>()) move(*ref);
class DragPayload {
public:
    DragPayload() = default;

    /// 原始字节构造（一般不用，用 of<T>() 更方便）。
    DragPayload(std::string type, std::vector<unsigned char> bytes)
        : type_(std::move(type)), bytes_(std::move(bytes))
    {
    }

    /// 把任意可平凡拷贝的值打包。
    template <class T>
    static DragPayload of(std::string type, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "DragPayload 只装可平凡拷贝的数据；复杂对象请放你自己的容器里，拖 id");

        DragPayload payload;
        payload.type_ = std::move(type);
        payload.bytes_.resize(kHeaderSize + sizeof(T));

        Header header;
        header.magic = kMagic;
        header.token = &detail::PayloadTag<T>::value;
        std::memcpy(payload.bytes_.data(), &header, sizeof(Header));
        std::memcpy(payload.bytes_.data() + kHeaderSize, &value, sizeof(T));
        return payload;
    }

    /// 取出数据。大小或类型标记不匹配时返回空，不会越界读。
    template <class T>
    std::optional<T> get() const
    {
        static_assert(std::is_trivially_copyable_v<T>, "DragPayload 只装可平凡拷贝的数据");
        if (bytes_.size() != kHeaderSize + sizeof(T))
            return std::nullopt;

        Header header;
        std::memcpy(&header, bytes_.data(), sizeof(Header));
        if (header.magic != kMagic || header.token != &detail::PayloadTag<T>::value)
            return std::nullopt;

        T value{};
        std::memcpy(&value, bytes_.data() + kHeaderSize, sizeof(T));
        return value;
    }

    const std::string& type() const { return type_; }
    bool empty() const { return type_.empty(); }

    /// 数据部分字节数（不含内部头部）。
    size_t size() const { return bytes_.size() >= kHeaderSize ? bytes_.size() - kHeaderSize : 0; }

    /// 数据指针（不含内部头部）。空载荷返回 nullptr。
    const unsigned char* data() const { return bytes_.size() >= kHeaderSize ? bytes_.data() + kHeaderSize : nullptr; }

    /// 含头部的原始缓冲，交给 ImGui 用。
    const unsigned char* raw() const { return bytes_.data(); }
    size_t rawSize() const { return bytes_.size(); }

    /// 把 ImGui 交付的载荷复制出来（只在 Accept 的那一帧有效，所以必须立刻拷贝）。
    static DragPayload fromImGui(const ImGuiPayload& payload)
    {
        DragPayload result;
        result.type_ = std::string(payload.DataType);
        const auto* bytes = static_cast<const unsigned char*>(payload.Data);
        result.bytes_.assign(bytes, bytes + payload.DataSize);
        return result;
    }

private:
    struct Header {
        std::uint32_t magic = 0;
        std::uint32_t reserved = 0;
        const void* token = nullptr;
    };

    static constexpr std::uint32_t kMagic = 0x4543484fu; // "ECHO"
    static constexpr size_t kHeaderSize = sizeof(Header);

    std::string type_;
    std::vector<unsigned char> bytes_;
};

namespace dnd {

/// 把"上一个 ImGui 控件"变成拖拽源。
///
/// 必须在某个控件提交之后立刻调用（BeginDragDropSource 依赖 last item）：
///     ImGui::Selectable("条目");
///     dnd::beginSource(payload, "拖动中");
///
/// 返回 true 表示正在拖。预览文字为空时显示类型名。
bool beginSource(const DragPayload& payload, const char* previewText = nullptr,
                 ImGuiDragDropFlags flags = ImGuiDragDropFlags_None);

/// 在当前光标处接受拖拽，返回"松手那一刻"的数据；没松手/类型不匹配返回空。
///
///     if (auto dropped = dnd::accept({"demo/item"}))
///         handle(*dropped);
std::optional<DragPayload> accept(std::initializer_list<const char*> types,
                                  ImGuiDragDropFlags flags = ImGuiDragDropFlags_None);

/// 同上，但类型用 vector 给（控件内部用这个重载）。
std::optional<DragPayload> accept(const std::vector<std::string>& types,
                                  ImGuiDragDropFlags flags = ImGuiDragDropFlags_None);

/// 只查询"正被拖动且类型可接受"的载荷，用于悬停高亮，不消费。
bool canAccept(std::initializer_list<const char*> types);
bool canAccept(const std::vector<std::string>& types);

} // namespace dnd

/// 可拖动的条目行（列表项、看板卡片、清单条目）。
///
///     panel.add<DragItem>("写周报", DragPayload::of<Item>("todo/item", Item{3}));
///
/// 它只负责"能被拖起来"；拖到哪、放下后发生什么，由对方的 DropZone / Table 行 / 你的回调决定。
class DragItem : public Widget {
public:
    DragItem(Source<std::string> label, DragPayload payload, bool selected = false);

    void render() override;
    const char* type() const override { return "DragItem"; }

    /// 换载荷（例如列表顺序变化后重建时）。
    void setPayload(DragPayload payload) { payload_ = std::move(payload); }

private:
    Source<std::string> label_;
    DragPayload payload_;
    bool selected_ = false;
};

/// 放置区：一块虚线区域，拖着可接受的数据悬停时高亮，松手触发回调。
///
///     panel.add<DropZone>(std::vector<std::string>{"todo/item"},
///                         [&](const DragPayload& p) { moveTo(p); },
///                         "拖到这里", ImVec2(-1, 72));
///
/// 宽度传负数表示"占满剩余宽度"。
class DropZone : public Widget {
public:
    using Handler = std::function<void(const DragPayload&)>;

    DropZone(std::vector<std::string> types, Handler onDrop, Source<std::string> placeholder = std::string{"拖到这里"},
             ImVec2 size = ImVec2(-1, 72));

    void render() override;
    const char* type() const override { return "DropZone"; }

    /// 本帧是否正被"可接受的载荷"悬停。
    bool hovered() const { return hoveredAcceptable_; }

private:
    std::vector<std::string> types_;
    Handler onDrop_;
    Source<std::string> placeholder_;
    ImVec2 size_;
    bool hoveredAcceptable_ = false;
};

} // namespace echo
