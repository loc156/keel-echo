// Echo UI 库 - 界面截图（冒烟测试用）。
//
// 【为什么放在库里】
// 用系统截屏工具验证界面很不可靠：窗口没置顶、桌面有别的窗口、DPI 缩放，都会让截图失真。
// 直接从 OpenGL 后缓冲读像素则与桌面无关，适合放进 CI 做"界面没崩"的检查。
//
// 用法（必须在 endFrame() 之后调用，此时画面已经交换到前台）：
//
//     while (app.running() && app.beginFrame()) {
//         windows.renderAll();
//         app.endFrame();
//         if (frameIndex == 60) { echo::captureFramebufferBmp("shot.bmp"); app.requestQuit(); }
//     }
//
// 产物是 24 位无压缩 BMP（格式简单、零依赖；需要 PNG 自己转换）。
#pragma once

#include <string>

namespace echo {

/// 把当前窗口的显示缓冲保存为 BMP。成功返回 true。
/// 需要当前线程有可用的 OpenGL 上下文（即 App 已 init 且未 shutdown）。
bool captureFramebufferBmp(const std::string& path);

} // namespace echo
