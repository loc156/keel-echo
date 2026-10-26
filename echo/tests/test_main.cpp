// Echo UI library - unit tests for the parts that do not need an ImGui context.
//
// Rendering itself is verified by running the demo; everything with logic in it
// (Source, Content, WindowManager, FixedTimestep, log, command parsing) is
// covered here and runs headless.

#include <cstdio>
#include <string>
#include <vector>

#include "echo/echo.h"

using namespace echo;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    ++g_checks;
    if (condition)
        return;
    ++g_failures;
    std::printf("FAIL %s:%d  %s\n", file, line, expression);
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)

class DummyWindow : public Window {
public:
    using Window::Window;
    void draw() override {}
    const char* type() const override { return "DummyWindow"; }
};

void testSource()
{
    Source<int> snapshot(42);
    CHECK(snapshot.get() == 42);

    int live = 7;
    Source<int> pointer(&live);
    CHECK(pointer.get() == 7);
    live = 9;
    CHECK(pointer.get() == 9);

    int counter = 0;
    Source<std::string> callable([&counter] {
        ++counter;
        return std::string("n=") + std::to_string(counter);
    });
    CHECK(callable.get() == "n=1");
    CHECK(callable.get() == "n=2");

    Source<std::string> fromLiteral("hello");
    CHECK(fromLiteral.get() == "hello");

    Source<int> nullPointer(static_cast<const int*>(nullptr));
    CHECK(nullPointer.get() == 0);
}

void testContent()
{
    Content content;
    CHECK(content.empty());

    auto& text = content.add<PlainText>("first");
    CHECK(std::string(text.type()) == "PlainText");
    content.add<Separator>();
    auto& second = content.add<ListItemText>("second");
    (void)second;

    CHECK(content.size() == 3);
    CHECK(content.at(1) != nullptr);
    CHECK(std::string(content.at(1)->type()) == "Separator");
    CHECK(content.at(99) == nullptr);

    std::vector<std::string> types;
    content.forEach([&types](Widget& widget) { types.push_back(widget.type()); });
    CHECK(types.size() == 3);
    CHECK(types[0] == "PlainText");
    CHECK(types[2] == "ListItemText");

    CHECK(content.erase(1));
    CHECK(content.size() == 2);
    CHECK(!content.erase(42));

    content.clear();
    CHECK(content.empty());
}

void testWindowManager()
{
    WindowManager windows;
    CHECK(windows.size() == 0);

    DummyWindow& first = windows.add<DummyWindow>("first", ImVec2(100, 100));
    DummyWindow& second = windows.add<DummyWindow>("second", ImVec2(120, 120));
    CHECK(windows.size() == 2);
    CHECK(first.id() != 0);
    CHECK(first.id() != second.id());

    CHECK(windows.find(first.id()) == &first);
    CHECK(windows.findByTitle("second") == &second);
    CHECK(windows.findByTitle("missing") == nullptr);
    CHECK(windows.find(9999) == nullptr);

    const uint64_t id = first.id();
    CHECK(windows.remove(id));
    CHECK(windows.size() == 1);
    CHECK(windows.find(id) == nullptr);
    CHECK(!windows.remove(id));

    windows.clear();
    CHECK(windows.size() == 0);
}

void testFixedTimestep()
{
    FixedTimestep step(0.1, 10);
    int ticks = 0;
    step.advance(0.35, [&ticks](double dt) {
        CHECK(dt > 0.099 && dt < 0.101);
        ++ticks;
    });
    CHECK(ticks == 3);
    CHECK(step.alpha() > 0.499 && step.alpha() < 0.501);

    // A huge stall must not spiral: at most maxTicksPerFrame ticks per call.
    FixedTimestep limited(0.1, 3);
    int limitedTicks = 0;
    limited.advance(5.0, [&limitedTicks](double) { ++limitedTicks; });
    CHECK(limitedTicks == 3);
    CHECK(limited.accumulator() <= 0.101);
}

void testLog()
{
    log::clear();
    log::setCapacity(4);

    std::vector<LogLevel> seen;
    log::setCallback([&seen](const LogEntry& entry) { seen.push_back(entry.level); });

    log::info("one");
    log::warn("two");
    log::error("three");

    CHECK(log::size() == 3);
    CHECK(log::lastText() == "three");
    CHECK(seen.size() == 3);
    CHECK(std::string(toString(seen[1])) == "WARN");

    log::info("four");
    log::info("five");
    CHECK(log::size() == 4); // ring buffer capacity

    const auto entries = log::entries();
    CHECK(entries.size() == 4);
    CHECK(entries.back().text == "five");

    log::setFile("build/test-log.txt");
    CHECK(!log::file().empty());
    log::error("to-file");
    log::setFile("");
    CHECK(log::file().empty());

    log::setCallback({});
    log::clear();
    CHECK(log::size() == 0);
    log::setCapacity(512);
}

void testDragPayload()
{
    struct Item {
        int column;
        int index;
    };
    struct Other {
        double x;
    };

    const DragPayload payload = DragPayload::of<Item>("demo/item", Item{1, 3});
    CHECK(payload.type() == "demo/item");
    CHECK(payload.size() == sizeof(Item));

    const auto value = payload.get<Item>();
    CHECK(value.has_value());
    CHECK(value->column == 1);
    CHECK(value->index == 3);

    // 大小/类型不匹配时必须安全返回空，而不是越界读。
    CHECK(!payload.get<Other>().has_value());
    CHECK(!payload.get<int>().has_value());

    const DragPayload empty;
    CHECK(empty.empty());
    CHECK(empty.size() == 0);

    // 空载荷不能当拖拽源（beginSource 会直接返回 false）。
    CHECK(!dnd::beginSource(empty));
}

void testConsoleParsing()
{
    const auto args = ConsoleWindow::split("add \"iron sword\" 3");
    CHECK(args.size() == 3);
    CHECK(args[0] == "add");
    CHECK(args[1] == "iron sword");
    CHECK(args[2] == "3");

    CHECK(ConsoleWindow::split("   ").empty());
    CHECK(ConsoleWindow::split("one").size() == 1);
}

void testConsoleCommands()
{
    ConsoleWindow console("test");
    console.registerCommand("ping", [&console](const ConsoleWindow::Args& args) {
        console.addLine("pong " + std::to_string(args.size()));
    });

    CHECK(console.execute("ping"));
    CHECK(console.history().size() == 1);
    CHECK(console.history().back() == "pong 1");

    CHECK(!console.execute("nope"));
    CHECK(console.history().size() == 2);
    CHECK(!console.execute(""));
    CHECK(console.history().size() == 2);

    console.clearHistory();
    CHECK(console.history().empty());
}

} // namespace

int main()
{
    testSource();
    testContent();
    testWindowManager();
    testFixedTimestep();
    testLog();
    testDragPayload();
    testConsoleParsing();
    testConsoleCommands();

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
