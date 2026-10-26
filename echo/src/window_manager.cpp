#include "echo/window_manager.h"

#include <algorithm>

namespace echo {

Window* WindowManager::find(uint64_t id)
{
    for (auto& window : windows_) {
        if (window->id() == id)
            return window.get();
    }
    return nullptr;
}

Window* WindowManager::findByTitle(std::string_view title)
{
    for (auto& window : windows_) {
        if (window->title() == title)
            return window.get();
    }
    return nullptr;
}

bool WindowManager::remove(uint64_t id)
{
    if (rendering_) {
        if (find(id) == nullptr)
            return false;
        pendingRemoval_.push_back(id);
        return true;
    }

    const auto it = std::find_if(windows_.begin(), windows_.end(),
                                 [id](const std::unique_ptr<Window>& window) { return window->id() == id; });
    if (it == windows_.end())
        return false;

    windows_.erase(it);
    return true;
}

void WindowManager::clear()
{
    windows_.clear();
    pendingRemoval_.clear();
}

std::vector<Window*> WindowManager::windows()
{
    std::vector<Window*> out;
    out.reserve(windows_.size());
    for (auto& window : windows_)
        out.push_back(window.get());
    return out;
}

void WindowManager::renderAll()
{
    rendering_ = true;
    if (menuBar_)
        drawMenuBar();

    for (auto& window : windows_)
        window->render();

    rendering_ = false;

    if (!pendingRemoval_.empty()) {
        const std::vector<uint64_t> ids = pendingRemoval_;
        pendingRemoval_.clear();
        for (const uint64_t id : ids)
            remove(id);
    }

    drawModals();

    if (ImGui::BeginPopupContextVoid("##echo_window_menu")) {
        if (ImGui::BeginMenu("Windows")) {
            for (auto& window : windows_) {
                bool open = window->open;
                if (ImGui::MenuItem(window->title().c_str(), nullptr, &open))
                    window->open = open;
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
}

void WindowManager::drawMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("Windows")) {
        if (windows_.empty())
            ImGui::MenuItem("(none)", nullptr, false, false);
        for (auto& window : windows_) {
            bool open = window->open;
            if (ImGui::MenuItem(window->title().c_str(), nullptr, &open))
                window->open = open;
        }
        ImGui::EndMenu();
    }

    if (onQuit_ && ImGui::MenuItem("Quit"))
        onQuit_();

    ImGui::EndMainMenuBar();
}

void WindowManager::showMessage(std::string title, std::string text)
{
    messageTitle_ = std::move(title);
    messageText_ = std::move(text);
    messagePending_ = true;
}

void WindowManager::drawModals()
{
    if (messageTitle_.empty())
        return;

    if (messagePending_) {
        ImGui::OpenPopup(messageTitle_.c_str());
        messagePending_ = false;
    }

    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(messageTitle_.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", messageText_.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // 弹窗关掉后清掉标题/正文，否则下一帧还会走一次 OpenPopup
    if (!ImGui::IsPopupOpen(messageTitle_.c_str())) {
        messageTitle_.clear();
        messageText_.clear();
    }
}

} // namespace echo
