// Echo UI 库 - 控件基类与控件列表。
//
// 【两种"容器"，别搞混】
//   Window / TextWindow   是一个"窗口"（有自己的标题栏、位置、折叠状态）；
//   Content               是窗口内部的"一串控件"，只管顺序，不管窗口。
//
// Content 里可以混装任意控件：文字、按钮、表格、树……顺序就是绘制顺序。
// 因为控件内部用的是 Source<T>（见 source.h），数据变了不需要重建列表：
//
//     auto& list = window.content();
//     auto& meter = list.add<MeterText>("CPU", &cpuPercent, 100);  // 建一次
//     // 之后只改 cpuPercent，界面自己跟着变
#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace echo {

/// 所有控件的基类。一个控件 = 一个"能在当前窗口里画点东西"的对象。
///
/// 约定：
///   - render() 会被每一帧调用一次，必须"画完就返回"，不要在里面阻塞；
///   - render() 不需要（也不应该）自己开窗口，窗口由 Window 负责；
///   - 控件不拥有数据，数据通过 Source<T> 或指针引用进来；
///   - 生命周期由 Content 管理（unique_ptr），析构顺序 = 添加顺序的逆序。
class Widget {
public:
    virtual ~Widget() = default;

    /// 画自己。每帧调用一次。
    virtual void render() = 0;

    /// 类型名，给调试/测试用（例如 Content 里塞了什么）。
    virtual const char* type() const { return "Widget"; }
};

using WidgetPtr = std::unique_ptr<Widget>;

/// 一串控件。负责持有、按顺序绘制、增删。
///
/// 用法：
///     Content content;
///     content.add<PlainText>("hello");
///     content.add<Button>("点击", [] { /* ... */ });
///     content.render();
class Content {
public:
    /// 追加一个控件，返回它的引用（后面还能改它，例如 setEnabled）。
    ///
    /// 参数原样转给 T 的构造函数：
    ///     auto& btn = content.add<Button>("确定", onClick);
    ///     btn.setEnabled(false);
    template <class T, class... Args>
    T& add(Args&&... args)
    {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *owned;
        items_.push_back(std::move(owned));
        return ref;
    }

    /// 清空（控件析构）。
    void clear() { items_.clear(); }

    bool empty() const { return items_.empty(); }
    size_t size() const { return items_.size(); }

    /// 按下标取控件，越界返回 nullptr。
    Widget* at(size_t index) { return index < items_.size() ? items_[index].get() : nullptr; }

    /// 删除第 index 个控件，返回是否删掉了。
    bool erase(size_t index)
    {
        if (index >= items_.size())
            return false;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    /// 按顺序绘制全部控件（由窗口调用，一般不需要你手动调）。
    void render()
    {
        for (auto& widget : items_)
            widget->render();
    }

    /// 遍历（用来批量改属性、统计、做测试）。
    template <class Fn>
    void forEach(Fn&& fn)
    {
        for (auto& widget : items_)
            fn(*widget);
    }

private:
    std::vector<WidgetPtr> items_;
};

/// 旧名字，保留以免已有代码报错。
using WidgetList = Content;

} // namespace echo
