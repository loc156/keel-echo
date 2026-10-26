// Echo UI 库 - 总入口头文件。
//
// 引一个头文件就能用全部功能：
//
//     #include "echo/echo.h"
//     using namespace echo;
//
// 最小可运行例子（约 20 行）：
//
//     int main() {
//         App app;
//         AppConfig config;
//         config.title = "工具";
//         if (!app.init(config)) return 1;
//
//         WindowManager windows;
//         windows.setOnQuit([&] { app.requestQuit(); });
//         auto& panel = windows.add<PanelWindow>("主面板", ImVec2(480, 320));
//         panel.add<HeadingText>("你好");
//         panel.add<PlainText>("这是一个非游戏的界面示例。");
//         panel.add<Spinner>();
//
//         while (app.running() && app.beginFrame()) {
//             windows.renderAll();
//             app.endFrame();
//         }
//         return 0;
//     }
//
// 各头文件职责：
//   app.h            启动/帧循环/配置
//   window.h         窗口基类、PanelWindow、ConsoleWindow
//   window_manager.h 窗口集合与菜单
//   widget.h         控件基类与 Content
//   source.h         Source<T>（快照/指针/lambda）
//   text.h           文本类控件
//   button.h         按钮
//   containers.h     Row / Grid 布局容器
//   controls.h       输入类控件
//   data.h           表格/树/页签/曲线
//   dragdrop.h       跨窗口拖拽（DragPayload / DragItem / DropZone）
//   theme.h          颜色与样式
//   log.h            日志
//   screenshot.h     读后缓冲存 BMP（界面冒烟测试）
//   input.h          输入查询
//   time.h           固定步长
//   layout.h         排版小工具
#pragma once

#include "app.h"
#include "button.h"
#include "containers.h"
#include "controls.h"
#include "data.h"
#include "dragdrop.h"
#include "input.h"
#include "layout.h"
#include "log.h"
#include "screenshot.h"
#include "source.h"
#include "text.h"
#include "theme.h"
#include "time.h"
#include "widget.h"
#include "window.h"
#include "window_manager.h"
