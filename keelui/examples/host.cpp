// KeelUI 的最小宿主：Echo 的 App 开窗口，Host 每帧跑脚本的 frame 块。
//
// 用法（在 keelui/ 目录下）：
//     host.exe examples/counter.keel
// 不传参数时默认 examples/counter.keel。
//
// 编译需要：Echo（libecho.a）+ Dear ImGui + GLFW + Keel 头文件，
// 具体命令行见 docs/BUILD.md。
#include "echo/app.h"
#include "KeelUI.hpp"

#include <string>

int main(int argc, char **argv)
{
    const std::string script = argc > 1 ? argv[1] : "examples/counter.keel";

    echo::App app;
    echo::AppConfig cfg;
    cfg.title = "KeelUI 宿主 - " + script;
    cfg.width = 900;
    cfg.height = 600;
    if (!app.init(cfg))
        return 1;

    keelui::Host host;
    host.budget = 100000; // 每帧步数上限
    if (!host.load(script))            // 失败时 host.error 里有文本，
        app.setTitle("KeelUI - 装载失败"); // frame() 会画一个错误窗口，不崩

    while (app.running() && app.beginFrame())
    {
        host.frame(); // 每帧只调这一句：跑 frame 块 + 配对收尾 + 错误处理
        app.endFrame();
    }

    app.shutdown();
    return 0;
}
