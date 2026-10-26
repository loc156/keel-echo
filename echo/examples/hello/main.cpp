#include "echo/echo.h"
#include <string>

using namespace echo;

int main() {
    App app;
    AppConfig cfg;
    if (!app.init(cfg)) return 1;

    WindowManager wm;
    auto& win = wm.add<PanelWindow>("TestWindow");
    win.add<PlainText>("Hello");

    while (app.running() && app.beginFrame()) {
        wm.renderAll();
        app.endFrame();
    }
    return 0;
}