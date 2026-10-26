# 把 Keel 装进自己的程序

本文只讲"宿主"这一侧：怎么装载、怎么给脚本提供自己的指令、怎么取错误、怎么单步。
语言本身见 [LANGUAGE.md](LANGUAGE.md)，指令清单见 [REFERENCE.md](REFERENCE.md)，装载器细节见 [LOADER.md](LOADER.md)。

## 最小宿主

```cpp
#include "KeelStd.hpp"     // register_all + reset_state
#include "KeelLoader.hpp"  // load_file / install / execute

#include <cstdio>

int main() {
    keel::Keel vm;
    keelstd::register_all(vm);          // 先装内置指令，再装载脚本

    keelld::Options opt;                // 默认：核心语法、入口块名 "main"
    opt.syntax = keelld::Syntax::Surface;  // 只有你要写 let/if/while 时才需要

    keelld::Program program = keelld::load_file("app.keel", opt);
    if (!program.ok()) {
        std::fprintf(stderr, "%s\n", keelld::format_diags(program).c_str());
        return 1;                       // 语法/结构错误：Program::diags 里有行号
    }

    keelld::Environment env;
    env.limits.steps = 200000;                       // 收紧步数预算
    env.sandbox_root = "sandbox";                    // 文件 IO 只许在这个目录里
    env.stdout_sink = [](const char *p, std::size_t n) { std::fwrite(p, 1, n, stdout); };

    keelld::Prepared prep = keelld::install(vm, program, env);
    if (!prep.ok) {
        std::fprintf(stderr, "%s\n", prep.error_text.c_str());
        return 1;
    }

    keelld::execute(vm);                 // run + flush 快速 IO + 收错误
    if (!keel::keel_errors().empty())   // 运行时错误（已翻成 文件:行）
        std::fprintf(stderr, "%s\n", keelld::format_runtime_errors(vm, program).c_str());
    return 0;
}
```

三条时序上的硬规则（顺序错了会出玄学问题）：

1. **先注册指令，再 `install`。** 入口块是在 `install` 里编译成指令的，晚注册的指令会被当成字面量压栈。
2. **`init()` 不清数据栈。** 复用实例必须先 `keelstd::reset_state(vm.data)`。
3. **要给脚本预置栈上的数据**，塞在 `prepare` 之后（`reset_state` 会清栈）。

## 复用同一个 VM 跑多段脚本

```cpp
keelstd::reset_state(vm.data);      // 清变量表 / 循环栈 / 执行状态 / 数据栈
// codes 里的块定义保留；再 install 一份新 Program 就能跑
```

`reset_state` **不会**清扩展库的状态（已打开的文件句柄、缓存的源码），那些跟着 VM 析构走。

## 加自己的指令

```cpp
#include "KeelStd.hpp"
namespace u = keel::util;

// "double_it  n -- n*2"
static void register_mine(keel::Keel &a) {
    a.register_func("double_it", KeelF {
        double n = 0;
        if (!u::pop_num(d, "double_it", n)) return;   // 类型不对会自动报错并返回
        u::push(d, keel::make_number(n * 2));
    });
}
```

- `KeelF` 就是 `[](keel::KeelData &d)` 的简写。
- 取值一律用 `u::need(d, N, "名字")` 先检查栈深，再用 `u::pop_num / pop_str / pop_text / pop_bool`。
- 报错用 `u::err("指令名", "说明")`：会带上当前的块名和 ip，最后由宿主用 `format_runtime_errors` 翻成行号。
- 分配大块内存前先 `u::check_alloc(d, bytes, "指令名")`，否则脚本能绕过 `max_alloc`。

## 每实例一份私有状态

文件句柄、源码缓存这类东西不能放全局变量，否则两个 VM 会互相踩。用 `keel::StateTable`：

```cpp
struct MyState { int counter = 0; };
using MyStates = keel::StateTable<MyState>;

// 指令里：
MyStates::get(d).counter++;
```

它按 `&d`（即 `KeelData` 的地址）登记，并把清理函数挂到 `d.on_destroy` 的**链头**上（不覆盖别人），VM 析构时依次回调。这也是为什么 `keel::Keel` **不可拷贝**。

## 单步 / 调试

```cpp
vm.init("main");
while (!vm.data.end) {
    // 这里可以看：vm.data.ip（当前指令下标）、vm.data.stack（数据栈）、
    //             vm.data.current_name（当前块名）、vm.data.steps（已走步数）
    vm.step();      // 执行一条指令；块尾那一步就是一次返回
}
```

`run()` 等价于 `while (!data.end) step()`。做慢放/断点就是在循环里控制调 `step()` 的节奏。

## 输出与错误

| 你想 | 用 |
| --- | --- |
| 接住脚本的 `f.*` 输出 | `Environment::stdout_sink`，或直接 `keelfast::set_writer(vm.data, fn)` |
| 立刻吐出缓冲 | `keelfast::flush_all(vm.data)` |
| 清空上一轮错误 | `keel::clear_errors()` |
| 取这一轮错误 | `keel::keel_errors()` |
| 给模型看的错误文本 | `keel::format_errors_for_ai(keel::keel_errors())` |
| 带行号的错误 | `keelld::format_runtime_errors(vm, program)` |

## 限额

`keelld::Limits`：`steps` / `depth` / `stack` / `alloc`（0 = 不改）/ `max_adjust`（脚本里 `limit` 最多调几次）。
`install` 时用 `apply_limits` 写进 `KeelData` 的软上限；硬天花板（`hard_steps` 等）只能由宿主改，脚本越不过去。

## JSON

`KeelJSON.hpp` 提供两个 C++ 侧函数（**不注册指令**）：

```cpp
#include "KeelJSON.hpp"
keel::Value v;
keel::json_decode(R"({"a":[1,2,3]})", v);   // false 表示解析失败
std::string text = keel::json_encode(v, /*pretty=*/true);
```
