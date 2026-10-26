# Keel 装载器契约

`keelld`（`KeelLoader.hpp`）负责"把文本变成可跑的 VM 状态"，它自己**不定义任何语言
语义**：块是运行时由 `{` 定义的，装载器只做四件事——切词、非权威的结构扫描、准备环境、
把一轮跑起来并把错误翻回源码位置。

本文按数据流讲：`load_text` / `load_file` → `Program` → `install(env)` → `Prepared` →
`execute` → `RoundResult`，然后单独讲 `include`、错误定位和宿主预置数据。

语言本身见 [LANGUAGE.md](LANGUAGE.md)，表面语法见 [SURFACE.md](SURFACE.md)，指令清单见
[REFERENCE.md](REFERENCE.md)，宿主侧用法见 [EMBEDDING.md](EMBEDDING.md)。

## 1. 数据流

```text
文本 ──load_text/load_file──> Program（tokens / blocks / diags）
                                      │
                                      ├──install(env)/prepare(env)──> Prepared
                                      │        （清状态、限额、include、prelude、init）
                                      └──execute()──> RoundResult（run + flush + 收错误）

round(p, env) = install(p, env) + execute()；run(p, env) 就是 round 的别名
```

分工是刻意的：`Program` 只描述"文本变成了什么"，`Environment` 描述"这一轮允许什么"，
`Prepared` 描述"环境装好了没有"，`RoundResult` 描述"这一轮跑了什么、错了什么"。想换限额
或沙箱重跑同一份脚本，用同一个 `Program` 再 `install` 一次即可，不必重新切词。

## 2. `Program`：装载的产物

`load_text` / `load_file` 返回的结构：

| 成员 | 含义 |
| --- | --- |
| `file` | 源文件路径；直接传字符串时是 `"<text>"` |
| `entry` | 入口块名，来自 `Options::entry`，默认 `"main"` |
| `syntax` | `Core` 或 `Surface`，来自 `Options::syntax` |
| `source` | 原文（BOM 已剥） |
| `options` | 这次装载用的选项，`include` 会沿用它 |
| `tokens` | 全部 token（含行列号）；Surface 时这里已经是核心 token |
| `blocks` | `[0]` 是入口块（`synthetic = true`，整份 body），其后是静态可见的顶层块 |
| `nested` | 更深的块（只用于行号映射） |
| `diags` | 诊断列表 |

常用方法：`ok()`（没有 fatal 诊断）、`entry_block()`、`find_any(name)` / `find(name)`、
`has_block(name)`、`line_of(name, ip)`、`dump()`。

`Block` 有 `name`、`body`（不含自己的 `{` `}`）、`lines`（与 `body` 一一对应）、`line` /
`end_line`、`top_level`、`synthetic`，以及 `line_at(ip)`。

**结构扫描是非权威的**。`scan_structure` 只认 `名字 { ... }` 这种文本形状，用途只有诊断
和行号映射，它不定义任何块。它会的检查：

- `}` 没有配对 → 诊断「多余的 }」；
- `{` 没有配对 → 诊断「未闭合的 {（块 '…' 缺少配对的 }）」，只报最外层一条；
- 块名与入口块同名 → 诊断，并提示"如果源码是 `main { ... }` 形式，去掉外层 main"；
- 块数超过 `Options::max_blocks` → 诊断；
- `{` 前一个 token 不是 `{` / `}` 时，把它（去掉引号后）当成块名。所以名字是算出来的
  （`"sq" "uare" concat { ... }`）这种写法会被记成一个错误的静态名字，**只影响诊断和
  行号，不影响运行**——运行时 `{` 从栈顶取真名字。

换行、缩进、语句顺序都不影响扫描，它只看 token 序列。

## 3. 输入卫生与上限

`load_text` 在切词前依次检查：

1. 原文长度超过 `Options::max_bytes`（默认 8 MiB）→ 诊断，直接返回；
2. 看起来是 UTF-16（FF FE / FE FF 开头）或含 NUL 字节 → 诊断"只接受 UTF-8 文本"；
3. 剥掉 UTF-8 BOM（Windows 编辑器常加，不剥会粘在第一个 token 上）；
4. 按 `Options::syntax` 走核心 `tokenize` 或 `surface::compile`；
5. token 为空（空源或只有注释）→ 诊断"空源（或只有注释）"；
6. 把全部 token 连同行号做成入口块，插到 `blocks[0]`；
7. 跑结构扫描。

`load_file` 只是"读文件 + `load_text`"，读的时候用同一个 `max_bytes` 当上限；打不开就返回
一个只有一条诊断的 `Program`。

`Options` 的默认值：`entry = "main"`、`syntax = Core`、`hash_comments = true`、
`max_tokens = 200000`、`max_bytes = 8 MiB`、`max_blocks = 256`。

## 4. `Environment` 与 `Limits`

`Environment` 是"这一轮允许什么"：

| 字段 | 默认 | 作用 |
| --- | --- | --- |
| `limits` | 见下 | 收紧软上限 |
| `enable_include` | `true` | 是否注册 `include` 指令 |
| `max_include_depth` | 16 | 调用栈上 `__inc_*` 块的最大个数 |
| `include_paths` | 空 | `include` 相对路径的搜索目录，按顺序 |
| `sandbox_root` | 空 | 非空 = 文件 IO 与 `include` 只许在这个目录里（会规范化成绝对路径） |
| `cwd` | 空 | 相对路径的兜底目录；空 = 进程当前目录 |
| `prelude` | 空 | 每次 `install` 先跑的一段 body（定义标准块/变量用） |
| `stdout_sink` | 空 | **快速 IO**（`f.out` / `f.outln` / `f.raw` 等）的输出去向；`print` / `println` 仍走 `std::cout` |

`Limits` 的默认值：`steps = 200000`、`depth = 200`、`stack = 10000`、`alloc = 0`
（0 表示"不改"，沿用 VM 的 16 MiB）、`max_adjust = 4`。`apply_limits` 会写
`max_steps` / `max_depth` / `max_stack` / `max_adjust`，只有 `alloc` 非 0 时才改
`max_alloc`；它没有失败路径，恒返回 `true`。硬天花板（`hard_*`）不在 `Limits` 里，
只能由宿主直接改 `KeelData` 字段。

## 5. `install` 的固定时序

`prepare` 就是 `install` 的别名。顺序是有原因的，不要重排：

1. `keel::clear_errors()` —— 这一轮的错误容器先清空。
2. 检查 `p.ok()`，不过就返回 `error_text = "脚本有语法/结构错误"`；再检查
   `entry_block()`，没有就返回 `"没有入口块（空的 Program？）"`。
3. Surface 语法 → `surface::register_runtime(a)` 注册 `s.add`（幂等，已存在就跳过）。
4. `enable_include` 为真 → 取这份 VM 的 `IncludeState`，写入 `env` 与 `p.options`，第一次
   时注册 `include` 指令。
5. `a.data.define_block(p.entry, entry->body)` —— **只定义入口块**，其余块等运行时 `{`。
6. `keelstd::reset_state(a.data)` —— 清数据栈、变量表、循环栈、ip、步数、`adjust_used`、
   块调用栈；**保留 `codes` 里的块定义**。
7. `apply_limits(a.data, env.limits)` —— 收紧软上限。
8. `sandbox_root`：空则清掉 VM 的沙箱根，非空则规范化后写进去。
9. `stdout_sink`：有就 `keelfast::set_writer`，没有就 `keelfast::clear_writer`。
10. `enable_include` 为真 → 记下入口块的来源目录（`block_dir[entry]`）和来源文件
    （`block_file[entry]`，文件名形如 `<text>` 时不记）。
11. 有 `prelude` 就装载并跑它：用同一份 `Options` 但把 `entry` 改成 `__prelude`，
    装入、`init`、`run`；prelude 有诊断或运行出错就整个 `install` 失败；跑完清栈
    （prelude 的产出是"块和变量"，不是栈上的临时值）。
12. `a.init(p.entry)` —— 只复位执行位置并压入入口块，**不清栈**。
13. `r.ok = true`。

第 4 步必须在第 5 步之前：`define_block` 当场把 token 编译成函数指针，没注册的 token 会
被编成 `nullptr`，运行时退化成"压一个字面量"。所以**宿主必须先 `register_all`，`install`
自己只补 `include` 和 `s.add` 两条**。

第 11 步也解释了为什么"宿主预置的数据"不能塞在 `prelude` 之前：prelude 跑完会清栈。
第 6 步的 `reset_state` 同样会清栈，所以预置数据的唯一正确位置是 `install` 之后。

## 6. `include`

核心语法里的写法是**路径先压栈**：

```keel
"sub/dep.keel" include
```

`include` 从栈顶弹路径（任何值都用 `toString` 变成路径）。表面语法的 `include "p"` 会
脱糖成同样的顺序，所以两种写法看起来不一样，实际栈效果一致。**注意别在核心语法里写
`include "p"`**：那会先执行 `include`，把栈上原有的东西当路径用。

几个契约：

- 只有 `env.enable_include` 为真时才注册 `include`。没注册时入口块里的 `include` token
  会被当字面量，运行时只是把字符串 `"include"` 压栈。
- **相对路径解析顺序**：包含者所在目录（块的 `block_dir`；入口块的是入口文件所在目录，
  文件名形如 `<text>` 时为空）→ `Environment::include_paths` 依次 → `Environment::cwd`
  （空则进程当前目录）。绝对路径直接检查存在性，不搜索。
- **沙箱**：目标路径规范化后走 `util::check_sandbox`，按路径组件与 `sandbox_root` 比较，
  越界报 `path escapes sandbox: ...`。
- **防循环**：遍历当前块调用栈，每个块的来源文件（`block_file`，规范化绝对路径）与目标
  比较，命中就报 `循环 include: <路径>`。所以 A 包含 B、B 又包含 A 会被拦下；入口文件
  只有在它是真实路径时才会参与这个比较。
- **深度**：调用栈上名字以 `__inc_` 开头的块数量达到 `max_include_depth` 就报错。
- **缓存**：按规范化路径缓存 token，键是（文件大小, 修改时间）。文件变了下次 `include`
  会重读。缓存的是 token，不是块——每次 `include` 都会定义一个新块 `__inc_N`
  （N 递增）并立刻 `push_block` 执行，执行完自然返回。
- **被包含文件的编译**：用 `p.options`（装载入口时那份），所以表面语法的程序包含的文件
  也按表面语法编译；同样检查 BOM、UTF-16、`max_bytes`。任何 fatal 诊断都会变成一条运行时
  错误（形如 `include: <文件>:<行>: <消息>`）并停机。
- **失败一律停机**：`include` 的任何错误都会把 `data.end` 置真，不像普通指令那样继续跑。
- 每次 `include` 都记一份 `block_lines`，这是运行时错误能翻回"文件:行"的依据。

## 7. `prepare` / `execute` / `round`

签名（照抄自头文件）：

```cpp
Prepared    keelld::install(keel::Keel &a, const Program &p,
                            const Environment &env = Environment());
Prepared    keelld::prepare(keel::Keel &a, const Program &p,
                            const Environment &env = Environment());   // = install
RoundResult keelld::execute(keel::Keel &a);
RoundResult keelld::round(keel::Keel &a, const Program &p,
                          const Environment &env = Environment());     // install + execute
RoundResult keelld::run(keel::Keel &a, const Program &p,
                        const Environment &env = Environment());       // = round
```

还有两个便利重载（都用默认 `Environment`）：

```cpp
Prepared    keelld::prepare(keel::Keel &a, const std::string &entry, std::string src,
                            std::size_t max_steps = 200000);
RoundResult keelld::round(keel::Keel &a, const std::string &src,
                          const std::string &entry = "main",
                          std::size_t max_steps = 200000);
```

`Prepared` 只有两个字段：`ok` 和 `error_text`（装载/结构/prelude 失败时的说明，含
`prelude 有语法/结构错误` 和 `prelude 运行出错` 两种前缀）。

`RoundResult` 四个字段：

| 字段 | 含义 |
| --- | --- |
| `ok` | `keel::keel_errors().empty()`，**不是** `data.end` |
| `steps` | 这一轮执行了多少条 token |
| `feedback` | `format_errors_for_ai` 的结果，形如 `块:ip (xN) 描述`，给模型回灌用 |
| `diag_text` | 只有 `round` 在 `install` 失败时才填：`line N: 消息` 逐行拼起来 |

`execute` = `a.run()` + `keelfast::flush_all(a.data)`（`f.*` 有 32 KB 缓冲，不刷会丢尾巴）
+ 收集错误。它不清错误容器——清空是 `install` 干的。

**别把 `data.end` 当成功。** 普通指令报错（未定义变量、除零、`call` 不存在的块）不会让
VM 停，`run()` 正常返回，`end` 也可能是真；判据始终是错误容器。

## 8. 运行时错误怎么翻回"文件:行"

运行时错误只带"块名 + token 下标"（`Error::where()` / `Error::token()`，`position_text()`
拼成 `块名:ip`）。`error_location` 按下面的顺序回退，把它变成源码位置：

1. `error_location(d, p, e)`：`token() < 0` 直接返回 `position_text()`；
2. 用 `Program` 的静态视图：`p.find_any(e.where())` 找到块、`line_at(ip)` 拿到行号 →
   `<Program::file>:<行>`。入口块的 ip 就是全局 token 下标，所以入口块错误落回入口文件；
3. 静态视图找不到（名字是算出来的块）→ 查 `IncludeState` 的 `block_file` /
   `block_lines` → `<被包含文件>:<行>`；
4. 都找不到 → 回到 `块名:ip`。

`format_runtime_errors(vm, p)` 用上面的映射把每条错误拼成 `<位置>: <what>`；
`format_diags(p)` 则是装载期诊断的 `<文件>:<行>[:<列>]: error: <消息>`（目前各个前端
只产生 error 级诊断，没有 warning）。

因此宿主想给用户看"文件:行"，要拿**同一个 `Program`** 调 `format_runtime_errors`；
`RoundResult::feedback` 是给模型看的聚合版，位置仍是 `块:ip`。

## 9. 宿主往栈里塞数据

唯一正确的位置：**`install` / `prepare` 返回之后、`execute` 之前**。

原因：`install` 内部的 `reset_state` 会清空数据栈，`prelude` 跑完也会再清一次；而
`init(entry)` 只复位执行位置，不动数据栈；`execute` 更不会清。所以顺序只能是
`prepare` → 塞数据 → `execute`。

```cpp
keelld::Prepared prep = keelld::prepare(vm, prog, env);
if (!prep.ok) { /* ... */ }
vm.data.stack.push_back(keel::make_number(20));   // 从这一刻起栈里就有 20
keelld::RoundResult r = keelld::execute(vm);
```

同一个 VM 再跑下一段脚本时，`install` 会再次 `reset_state`，上一轮残留的栈和变量都会被
清掉；如果不用 `install` 而直接复用，就得自己调 `keelstd::reset_state(vm.data)`
（它清栈、变量、循环栈、ip、步数，保留 `codes`）。

## 10. 最小宿主示例

下面这段在 MinGW-w64 g++ 11.5.0、`-std=c++17 -Wall -Wextra` 下编译通过，运行输出 `40`
（宿主预置的 20 乘了 2）。

```cpp
#include "KeelLoader.hpp" // VM + 装载器 + 标准库全在这一个头里

#include <cstddef>
#include <cstdio>

int main()
{
    keel::Keel vm;
    keelstd::register_all(vm); // 1) 先注册指令：install 编译入口块时当场查表

    keelld::Program prog = keelld::load_file("hello.keel"); // 2) 源 -> Program（entry 默认 "main"）
    if (!prog.ok())
    {
        std::fputs(keelld::format_diags(prog).c_str(), stderr);
        return 1;
    }

    keelld::Environment env; // 3) 限额 / include / 沙箱 / 输出汇
    env.sandbox_root = ".";
    env.stdout_sink = [](const char *p, std::size_t n) { std::fwrite(p, 1, n, stdout); };

    keelld::Prepared prep = keelld::prepare(vm, prog, env); // 4) 清状态、装入口块、init
    if (!prep.ok)
    {
        std::fputs(prep.error_text.c_str(), stderr);
        return 1;
    }

    vm.data.stack.push_back(keel::make_number(20)); // 5) 宿主数据只能塞在 prepare 之后
    keelld::RoundResult r = keelld::execute(vm);    // 6) run + flush + 收错误
    std::fputs(keelld::format_runtime_errors(vm, prog).c_str(), stderr);
    return r.ok ? 0 : 1;
}
```

配套的 `hello.keel`（核心语法，入口块就是整份 body）。它假设栈上已经有宿主压的 20，
**单独运行会因为缺操作数报错**——这正是"预置数据"这个契约的可见证据：

```keel
// 宿主预置的 20 在栈上
"dbl" { dup + }
"dbl" call println        // 40
```

如果不需要"跑前塞数据"，直接 `keelld::round(vm, prog, env)` 一步到位即可；它就是
`install + execute`。

## 11. 契约清单

- **先 `keelstd::register_all`，再 `install`。** 装载器自己只注册 `include`（和 Surface 的
  `s.add`），不会替你装核心库。
- **改块只走 `define_block`。** 直接写 `codes` 会在"同长度重定义"时继续跑旧的函数指针缓存。
- **`Program` 可以复用，`Environment` 每次 install 重新生效。** 想换限额/沙箱就再
  `install` 一次，不必重新 `load_*`。
- **`Keel` 不可拷贝/不可移动**（`Keel` 的拷贝构造与拷贝赋值是 `delete` 的），因为各库的
  每实例状态按 `&data` 地址登记；需要动态生命周期用 `unique_ptr<Keel>`。
- **`ok` 看错误容器，不看 `data.end`。**
- **`include` 走的是"先压路径再执行"的栈约定**，且要 `enable_include` 打开。
- **沙箱只覆盖文件 IO 与 `include`**（按规范化路径比较），不限制别的能力。
- **`prelude` 与主程序共享限额、沙箱和块表**；prelude 的栈在结束时会清空。
- **`max_bytes` / `max_tokens` / `max_blocks` 是装载期上限**，`steps` / `depth` / `stack` /
  `alloc` 是运行期上限，两组互不相干。
