// Echo UI 库 - 示例：数字键盘（计算器）。
//
// 这个例子专门演示"同一行放多个控件"：
//   Grid(4)      把 16 个按钮排成 4 列（列宽自动对齐）
//   ImVec2(-1,h) 按钮宽度 = 占满所在格子
//   Row          把两个按钮排成一行
//
// 计算逻辑故意写得很朴素（没有优先级，边算边存），重点在布局，不在算术。
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include "echo/echo.h"

using namespace echo;

namespace {

struct Calculator {
    std::string display = "0";
    double accumulator = 0.0;
    char pending = 0; // '+', '-', '*', '/'
    bool fresh = true;

    double current() const { return std::strtod(display.c_str(), nullptr); }

    void show(double value)
    {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%g", value);
        display = buffer;
    }

    void apply()
    {
        const double value = current();
        switch (pending) {
        case '+': accumulator += value; break;
        case '-': accumulator -= value; break;
        case '*': accumulator *= value; break;
        case '/':
            if (value != 0.0)
                accumulator /= value;
            break;
        default: accumulator = value; break;
        }
        pending = 0;
    }

    void digit(char d)
    {
        if (fresh) {
            display.assign(1, d);
            fresh = false;
        } else if (display.size() < 14) {
            if (display == "0")
                display.assign(1, d);
            else
                display.push_back(d);
        }
    }

    void op(char o)
    {
        apply();
        pending = o;
        fresh = true;
        show(accumulator);
    }

    void equals()
    {
        apply();
        fresh = true;
        show(accumulator);
    }

    void clear()
    {
        display = "0";
        accumulator = 0.0;
        pending = 0;
        fresh = true;
    }
};

} // namespace

int main(int argc, char** argv)
{
    std::string shotPath;
    int warmupFrames = 30;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--shot" && i + 1 < argc)
            shotPath = argv[++i];
        else if (arg == "--frames" && i + 1 < argc)
            warmupFrames = std::atoi(argv[++i]);
    }

    App app;
    AppConfig config;
    config.title = "Echo 计算器示例";
    config.width = 380;
    config.height = 560;
    config.clearColor = ImVec4(0.05f, 0.06f, 0.08f, 1.0f);
    if (!app.init(config))
        return 1;

    theme::dark();
    theme::setAccent(colors::cyan());

    Calculator calc;

    WindowManager windows;
    windows.setOnQuit([&app] { app.requestQuit(); });

    auto& panel = windows.add<PanelWindow>("计算器", ImVec2(320.0f, 440.0f));
    panel.setPosition(ImVec2(20.0f, 40.0f));

    // 显示屏：数字用强调色，背景压暗。
    panel.add<HighlightText>([&calc] { return calc.display; }, ImVec4(0.08f, 0.09f, 0.12f, 1.0f), colors::cyan());
    panel.add<Spacer>(6.0f);

    // 关键：4 列网格。16 个按钮按"行优先"依次填进去。
    auto& pad = panel.add<Grid>(4);

    auto key = [&pad, &calc](const char* label, std::function<void()> action, const ImVec4* color = nullptr) {
        auto& button = pad.add<Button>(label, std::move(action), ImVec2(-1.0f, 40.0f));
        if (color != nullptr)
            button.setColor(*color);
    };

    const ImVec4 accent = theme::accent();
    const ImVec4 danger = colors::danger();

    key("7", [&calc] { calc.digit('7'); });
    key("8", [&calc] { calc.digit('8'); });
    key("9", [&calc] { calc.digit('9'); });
    key("÷", [&calc] { calc.op('/'); }, &accent);

    key("4", [&calc] { calc.digit('4'); });
    key("5", [&calc] { calc.digit('5'); });
    key("6", [&calc] { calc.digit('6'); });
    key("×", [&calc] { calc.op('*'); }, &accent);

    key("1", [&calc] { calc.digit('1'); });
    key("2", [&calc] { calc.digit('2'); });
    key("3", [&calc] { calc.digit('3'); });
    key("−", [&calc] { calc.op('-'); }, &accent);

    key("C", [&calc] { calc.clear(); }, &danger);
    key("0", [&calc] { calc.digit('0'); });
    key("=", [&calc] { calc.equals(); }, &accent);
    key("+", [&calc] { calc.op('+'); }, &accent);

    // 同一行的两个按钮：Row 容器（等价于"横向排列"）。
    auto& footer = panel.add<Row>(6.0f);
    footer.add<Button>("复制", [&calc] { ImGui::SetClipboardText(calc.display.c_str()); }, ImVec2(90.0f, 0.0f), true);
    footer.add<Button>("清空", [&calc] { calc.clear(); }, ImVec2(90.0f, 0.0f), true);

    panel.add<MutedText>("Grid(4) = 4 列；按钮宽度 -1 = 占满格子");

    while (app.running() && app.beginFrame()) {
        windows.renderAll();
        app.endFrame();

        if (!shotPath.empty() && app.frameIndex() >= static_cast<uint64_t>(warmupFrames)) {
            if (!captureFramebufferBmp(shotPath))
                log::error("截图失败: " + shotPath);
            app.requestQuit();
        }
    }

    windows.clear();
    app.shutdown();
    return 0;
}
