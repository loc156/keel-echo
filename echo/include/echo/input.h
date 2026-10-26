// Echo UI 库 - 输入查询。
//
// 全部是对 ImGui 输入状态的薄包装。这样做的好处是**焦点自动正确**：
// 当某个文本框正在输入时，ImGui 会先把按键吃掉，这里再查就查不到——
// 不会出现"在输入框里打字，WASD 又触发了一次全局快捷键"这类经典 bug。
//
//     if (input::keyPressed(ImGuiKey_F5)) reload();
//     if (input::keyDown(ImGuiKey_LeftCtrl) && input::keyPressed(ImGuiKey_S)) save();
//     const ImVec2 mouse = input::mousePosition();
#pragma once

#include "imgui.h"

namespace echo {
namespace input {

/// 此刻是否按住（持续为真）。
inline bool keyDown(ImGuiKey key) { return ImGui::IsKeyDown(key); }

/// 这一帧是否刚按下（只真一帧）。repeat = true 时按住会自动重复。
inline bool keyPressed(ImGuiKey key, bool repeat = false) { return ImGui::IsKeyPressed(key, repeat); }

/// 这一帧是否刚松开。
inline bool keyReleased(ImGuiKey key) { return ImGui::IsKeyReleased(key); }

/// 鼠标键是否按住。0 = 左键，1 = 右键，2 = 中键。
inline bool mouseDown(int button = 0) { return ImGui::IsMouseDown(button); }
inline bool mouseClicked(int button = 0) { return ImGui::IsMouseClicked(button); }

/// 鼠标位置（屏幕坐标）与本帧位移。
inline ImVec2 mousePosition() { return ImGui::GetIO().MousePos; }
inline ImVec2 mouseDelta() { return ImGui::GetIO().MouseDelta; }

/// 滚轮增量（向上为正）。
inline float mouseWheel() { return ImGui::GetIO().MouseWheel; }

/// ImGui 时钟（秒），从第一帧开始计。做动画/防抖用它比 std::chrono 方便。
inline double time() { return ImGui::GetTime(); }

} // namespace input
} // namespace echo
