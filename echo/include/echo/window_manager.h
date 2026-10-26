// Echo UI 库 - 窗口管理器。
//
// 负责：创建窗口、分配 id、每帧统一渲染、删除、菜单栏、右键菜单、模态框。
//
//     WindowManager windows;
//     auto& panel = windows.add<PanelWindow>("设置");
//     ...
//     while (app.running() && app.beginFrame()) {
//         windows.renderAll();
//         app.endFrame();
//     }
//
// 【id 与生命周期】
//   - add<T>() 返回 T&，这个引用在 remove()/clear() 之前一直有效；
//   - 窗口由 unique_ptr 持有，顺序 = 添加顺序 = 绘制顺序（后加的画在上面）；
//   - 在某个窗口的 draw() 里删除窗口是安全的：删除会延迟到本帧渲染结束。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui.h"
#include "window.h"

namespace echo {

class WindowManager {
public:
    /// 创建一个窗口并返回稳定引用。
    ///     auto& w = windows.add<PanelWindow>("标题", ImVec2(400, 300));
    template <class T, class... Args>
    T& add(Args&&... args)
    {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *owned;
        Window* base = owned.get();
        base->assignId(nextId_++);
        windows_.push_back(std::move(owned));
        return ref;
    }

    /// 按 id 找窗口，没有返回 nullptr。
    Window* find(uint64_t id);

    /// 按标题找窗口（标题重复时返回第一个），没有返回 nullptr。
    Window* findByTitle(std::string_view title);

    /// 删除窗口。可以在窗口的 draw() 里调用（会延迟到本帧末尾）。
    bool remove(uint64_t id);

    /// 删除全部窗口（退出前调用，保证控件按顺序析构）。
    void clear();

    size_t size() const { return windows_.size(); }

    /// 全部窗口的裸指针（只读快照，顺序 = 绘制顺序）。
    std::vector<Window*> windows();

    /// 画菜单栏（可选）、全部窗口、模态框、右键菜单。每帧调用一次。
    void renderAll();

    /// 顶部菜单栏（"Windows" / "Quit"）开关，默认开。
    void setMenuBar(bool enabled) { menuBar_ = enabled; }
    bool menuBar() const { return menuBar_; }

    /// 菜单里点 Quit 时做什么（一般是 app.requestQuit()）。
    void setOnQuit(std::function<void()> onQuit) { onQuit_ = std::move(onQuit); }

    /// 弹一个信息框（下一次 renderAll 时出现），按 OK/Enter/Esc 关闭。
    void showMessage(std::string title, std::string text);

private:
    void drawMenuBar();
    void drawModals();

    std::vector<std::unique_ptr<Window>> windows_;
    std::vector<uint64_t> pendingRemoval_;
    uint64_t nextId_ = 1;
    bool menuBar_ = true;
    bool rendering_ = false;
    std::function<void()> onQuit_;

    std::string messageTitle_;
    std::string messageText_;
    bool messagePending_ = false;
};

} // namespace echo
