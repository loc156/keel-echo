# Keel

一个 Forth 式的小型嵌入式脚本语言，**header-only**（18 个头文件，C++17，零第三方依赖）。

这个语言曾名 **Aspark**（`Aspark.hpp` / `spk::Aspark` / `.asp`），整理成开源仓库时因为
重名问题统一改名成 Keel，功能和设计没有变化。

- 源码 = 一串 token；数据都在一个数据栈上，指令从栈顶取操作数、把结果压回栈。
- `{ }` 是**运行时的定义块指令**：块名从栈顶取，所以可以是算出来的；文件顶层就是"入口块"。
- 自带 8 组内置库（核心 / 字符串 / 容器 / 数学 / 简单 IO + 三个 IO 扩展库），共 225 条指令名（含别名 39 条）。
- 另有一个装载器（文件 → token → 程序 → 环境）和一个可选的"表面语法"前端（`let/if/while/fn`）。

## 10 行跑起来

```cpp
#include "KeelStd.hpp"
#include "KeelLoader.hpp"

int main() {
    keel::Keel vm;
    keelstd::register_all(vm);                       // 装 8 组内置库

    auto program = keelld::load_text(R"(
        "sq" { dup * }
        5 "sq" call println
    )");
    keelld::Environment env;
    if (!keelld::install(vm, program, env).ok) return 1;
    keelld::execute(vm);                             // 打印 25
}
```

编译（不需要 cmake）：

```bash
g++ -std=c++17 -O1 -Wall -Wextra -I keel/include your_app.cpp -o your_app
```

## 头文件表

| 文件 | 负责 |
| --- | --- |
| `KeelValue.hpp` | 值类型 `Value`（nil/bool/number/string/list/map/umap）与错误容器。依赖树的根 |
| `Keel.hpp` | VM 主体：`keel::Keel`（句柄）/ `KeelData`（全部运行时状态）、`step/run`、`{ }` |
| `KeelBase.hpp` | 核心库：栈操作、算术、比较、逻辑、变量、块调用、控制流、限额、断言 |
| `KeelString.hpp` | 字符串：`trim/substr/split/join/replace/find/...` |
| `KeelContainer.hpp` | 容器：`list/map/umap`、`append/get/put/has/keys/sort/...` |
| `KeelMath.hpp` | 数学：`sqrt/pow/sin/atan2/floor/hypot/isnan/...`（需要 `<cmath>` 的都在这里） |
| `KeelIO.hpp` | 简单 IO：`print/println/readline/readfile/writefile/...`（iostream / fstream） |
| `KeelFastIO.hpp` | 快速 IO（`f.*`）：底层输出 + 整读整写，可把 stdout 接到宿主回调 |
| `KeelBigIO.hpp` | 大文件与流（`big.*` / `stream.*`）：句柄、分块、逐行、seek、原子写、目录 |
| `KeelDynSource.hpp` | 动态源（`src.*`）：运行时把脚本文件装进内存并跟着磁盘变 |
| `KeelIOCore.hpp` | 上面三个 IO 库共用的底层小工具（UTF-8 路径等） |
| `KeelInstanceState.hpp` | "每实例一份私有状态"的注册表（文件句柄、源缓存靠它按 VM 清理） |
| `KeelUtil.hpp` | 内置库共用的取值/报错/索引/路径工具，不注册任何指令 |
| `KeelJSON.hpp` | JSON 编解码 `json_encode` / `json_decode`（C++ 侧函数，**不注册指令，`register_all` 也不装它**） |
| `KeelSyntax.hpp` | 前端公共类型：`Token` / `Diagnostic` / `Options` / 词法 |
| `KeelLoader.hpp` | 装载器（`keelld`）：`load_text/load_file` → `Program` → `install/prepare/execute/round` |
| `KeelSurface.hpp` | 表面语法前端：`let/if/while/fn/return/include` 脱糖成核心 token |
| `KeelStd.hpp` | 便利头：`keelstd::register_all` 一次装齐 5 个核心库 + 3 个 IO 扩展库 |

只想要某个库，就只 include 它、调对应的 `xxx_register(vm)`；`register_all` 只是把八个库一次装完。

## 语法 30 秒

```keel
// 三个值压栈，最后一个在栈顶
1 2 +            // 3
"ab" "cd" concat // "abcd"

// 定义块：{ 之前那个字符串是块名
"sq" { dup * }   // 现在 sq 这个块存在了
5 "sq" call      // 25

// 变量：值 名 store / 名 load
42 "answer" store
"answer" load println
```

## 嵌入宿主时要注意的三点

1. **`init()` 不清数据栈。** 复用同一个实例跑第二段脚本前先 `keelstd::reset_state(vm.data)`，否则上一轮的栈还在。
2. **错误要自己取。** 执行完看 `keel::keel_errors()`；一轮开始前 `keel::clear_errors()`。运行时错误带上"块名 + ip"，用 `keelld::format_runtime_errors(vm, program)` 能翻成"文件:行"。
3. **限额是保险丝。** 默认 1000 万步 / 深度 1000 / 栈 10 万 / 单次分配 16MB；`keelld::Limits` 会在安装时收紧，脚本里还能用 `limit` 在硬天花板内再调。

## 文档

| 文件 | 内容 |
| --- | --- |
| [docs/LANGUAGE.md](docs/LANGUAGE.md) | 语言模型与核心语法（body 模型、块、值、变量、控制流、限额） |
| [docs/REFERENCE.md](docs/REFERENCE.md) | **指令表**（225 条，含别名）+ C++ 侧入口 + 错误与限额 |
| [docs/LOADER.md](docs/LOADER.md) | 装载器契约：`Program` / `Environment` / `install` / `include` / 行号映射 |
| [docs/SURFACE.md](docs/SURFACE.md) | 表面语法 `let/if/while/fn`，以及 v1 的限制 |
| [docs/EMBEDDING.md](docs/EMBEDDING.md) | 把 Keel 装进自己的程序：自定义指令、每实例状态、单步调试钩子 |

想用 Keel 画界面，看 [`../keelui/`](../keelui/)。
