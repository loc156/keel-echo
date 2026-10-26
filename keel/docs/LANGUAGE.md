# Keel 语言模型与核心语法

Keel 是一个 C++17 单头文件的 Forth 式 token 栈机：源码是一串用空白分开的 token，每个
token 要么是**一条已注册的指令**，要么是**一个字面量**。没有语法树，没有编译器中间层，
块在运行到 `{` 的那一刻才被定义。

本文只讲语言模型与核心语法。指令的完整清单见 [REFERENCE.md](REFERENCE.md)，表面语法
（`let` / `if` / `while` / `fn`）怎么脱糖成核心 token 见 [SURFACE.md](SURFACE.md)，装载器
（`load_text` / `install` / `prepare` / `execute` / `include`）的契约见
[LOADER.md](LOADER.md)，宿主接入见 [EMBEDDING.md](EMBEDDING.md)。

**本文所有脚本的前提**：宿主已经调用 `keelstd::register_all(vm)`（这样 `println`、`list`
等库指令才存在），并用 `keelld` 的 `load_text` / `prepare` / `execute` 跑一轮，入口块名用
默认的 `main`。脚本正文就是入口块的块体，不需要也不能再套一层 `main { ... }`。

## 1. 源码就是入口块的块体

装载器把整份文件切成 token，然后**把全部 token 依次塞进入口块的块体**。文件里没有
"顶层声明"这种东西：第一行就是入口块的第一条指令，最后一行就是入口块的最后一条指令。

这带来的直接后果：

- 写 `main { ... }` 会报错。入口块已经在运行，执行到 `{` 时若块名又是 `main`，就等于
  重定义正在调用栈上的块；装载器的静态扫描会提前把这种写法诊断出来，错误信息会提示
  "去掉外层 main"。
- 块表不是装载器的产物。`{` 从**栈顶**取块名，名字可以是算出来的（`"na" "me" concat`），
  所以装载器不可能静态地知道程序里有哪些块。它对 `名字 {` 形状的扫描只用于诊断和
  `(块, token) -> 行号` 映射，不定义任何块。
- 因此"文件里定义了几个块"是运行期事实，不是文本事实。同一个文件在不同输入下可以定义
  不同名字的块。

```keel
// 整份文件就是入口块 main 的块体；没有 main { } 外壳
"sq" { dup * }            // 运行时定义块 sq
5 "sq" call println       // 25：块只能经 call 执行
```

第二条要注意：`"sq"` 先压栈，`{` 弹出它作为块名，`}` 之前的 `dup *` 被原样存成块体。
执行流在 `{` 处直接跳到配对的 `}` 之后，块体本身不会在定义时执行。

## 2. token 怎么切

词法只有这几条规则（核心前端 `keelld::tokenize`；表面语法另有自己的一份词法，
见 [SURFACE.md](SURFACE.md)）：

- **分隔符**：空格、制表符、换行、回车。连续空白不产生空 token，也没有"空 token"概念。
- **引号保护空格**：单引号或双引号包起来的一段算一个 token，引号本身留在 token 文本里。
  引号内可以出现空格甚至换行，引号内不识别注释。
- **注释**：`//` 到行尾，出现在引号外的任何位置都算（即使紧贴着前一个 token：
  `1//注释` 只有 `1` 一个 token）。`#` 只在**token 起点**时到行尾算注释
  （由 `keelld::Options::hash_comments` 控制，默认开）；写进 token 中间的 `#` 是普通字符。
- **没有转义**：核心词法不处理 `\n`、`\"`，字符串里放不进和定界符相同的引号。表面语法
  有转义层，可以写 `\"` 来放双引号，但一个字符串不能同时含两种引号（见
  [SURFACE.md](SURFACE.md)）。
- **行列号只用于诊断**：换行不结束任何语法结构，token 序列才是唯一的真相；一条 `if`
  可以横跨几十行，只要 `then` 还在同一个块体里。

```keel
// 空白分隔 token；引号保护空格；// 和 # 到行尾都是注释
"a b" println      // a b：引号包住空格，打印时引号已剥掉
3 4 + println      // 7
"x#y" println      // x#y：token 中间的 # 不是注释
```

## 3. 执行模型：一条 token 一步

`keel::Keel::step()` 每次处理一条 token，`run()` 就是 `while (!data.end) step()`。
每一步做四件事：

1. 步数与栈深保险丝检查（见第 10 节）。
2. 若 `ip` 已到当前块体末尾，这一步就是一次块返回。
3. 取当前 token：如果它注册过指令，就执行那个函数；否则用 `keel::parse_value` 把 token
   原文转成值压栈。
4. 如果这一步没有发生跳转（`jumped` 为假），`ip` 前进一格。所有跳转类指令必须自己置
   `jumped`，否则会被这里的自增吃掉。

**"没注册的 token 就是字面量"是整个语言的地基**：它让块名、变量名、任意文本都能直接当
数据写，不需要字符串前缀。反过来说，拼错指令名不会报"未知指令"，只会悄悄往栈上压一个
字符串——这是 Keel 最常见的错误来源。

```keel
// 未注册成指令的 token 不是错误：它就是字面量
"sq" { dup * }
5 sq println      // 打印 sq：块名也要 call 才会执行
depth println     // 1：栈上还剩那个 5
```

`KeelData::stream` 是给自定义指令留的扩展点（把接下来的 N 个 token 收进栈或回调），
内置指令没有任何一条会打开它，所以核心语言里 token 只会"执行"或"压栈"。

## 4. 块：`{` 在运行时定义，名字从栈顶取

`{` 的行为可以逐条写出来：

1. 要求栈顶是一个**非空字符串**，否则报错并停机（消息大意：块名必须是非空字符串）。
2. 在当前块体的 token 里从 `{` 之后开始找配对 `}`，数嵌套深度。
3. 把两个括号之间的 token **原样**存成一个新块（`KeelData::define_block`），名字就是
   栈顶那个字符串。
4. 把 `ip` 挪到 `}` 的下一条，继续执行。

配套规则：

- **不能重定义正在调用栈上的块**。`main` 里定义 `main`、或者 `a` 里定义 `main` 再被
  `main` 调用，都会被拒绝（消息大意：块在调用栈上时不能重定义）。理由是栈上那份返回
  地址会指向新块体，执行位置立刻错位。
- **可以重定义不在栈上的块**，同名后定义覆盖先定义，块表是全局字典。
- **块体是 token 的拷贝**，不是闭包：它不捕获定义处的任何东西，变量都走全局变量表。
- 缺配对的 `}` 报错停机；单独的 `}` 也报错停机。
- 块的调用深度受 `max_depth` 限制，超了报错停机。

调用与返回：

- `call` 从栈顶取块名（要求是字符串），块必须已存在；**不存在只报错、不停机**，随后继续跑。
- `ret` 立即返回；块体执行完也会自动返回。
- 在入口块里返回（无论是 `ret` 还是执行到末尾）就是整个 VM 停机，没有"返回值"这个通道，
  结果就在数据栈上。
- 递归是允许的，受深度上限约束。

```keel
// 块名从栈顶取，可以算出来
"sq" "uare" concat { dup * }     // 定义块 square
6 "square" call println          // 36
```

```keel
// 块可以递归；ret 提前返回，块尾也会自动返回
"fact" {
  dup 1 <= if drop 1 ret then
  dup 1 - "fact" call *
}
5 "fact" call println     // 120
```

递归例子里 `5 "fact" call` 之后栈顶是 120，块与调用者之间只靠数据栈交接参数和结果，
没有别的约定。

## 5. 值

`keel::Value` 的 `Type` 一共七种：

| `type` 返回的字符串 | 说明 |
| --- | --- |
| `nil` | 空值；`nil` 与 `null` 两个字面量都得到它 |
| `bool` | `true` / `false` |
| `number` | 一律是 `double`，没有整数类型（`int` 只是截断） |
| `string` | UTF-8 字节序列 |
| `list` | 列表，`shared_ptr` 共享 |
| `map` | 有序映射（按 key 排序） |
| `unordered_map` | 无序映射 |

**字面量只有五种写法**，由 `keel::parse_value` 决定：

1. `nil` 或 `null` → nil；
2. `true` → 真；
3. `false` → 假；
4. 能被 `keel::fast_atof` 整串解析的 token → 数字（十进制，可带小数点和指数）；
5. 首尾是**同一种**引号（`"` 或 `'`）且长度至少 2 的 token → 去掉首尾引号后的字符串。

其它 token 一律变成"原文即字符串"的值，包括带了一侧引号、引号中间夹了别的字符、
以及拼错的指令名。数字判断在运行时做：装载期只给 token 标个 `Number` 类型用于诊断和
高亮，不决定值。

几个容易踩的点：

- **没有列表/映射字面量**。`[` 是计数循环，`{` 是定义块，都不是容器构造器。容器要用
  `list` / `map` / `umap` 建、用 `append` / `put` 填（`split`、`chars`、`keys`、`values`、
  `vars` 也会直接产出列表）。
- 数字打印走 `Value::toString`，内部是 `ostringstream` 的默认精度（6 位有效数字），
  所以 `100000000` 打印成 `1e+08`。要固定格式就自己拼字符串。
- 容器是 `shared_ptr` 共享的：`put` / `append` / `del` 都是就地改，同一个容器的其它副本
  能看到变化。
- `==` 对容器比较的是**句柄身份**（`shared_ptr` 相同），不是内容。两个分别造出来、
  内容一样的列表 `==` 得 `false`。
- 真值判断（`Value::truthy`）：nil 假、`false` 假、空串假、数字 0 假、空容器假，其余真。
  `and` / `or` / `xor` / `not` 都走这条规则，`if` 与 `until` 也是。

```keel
// 字面量只有五种写法：nil / true / false / 数字 / 引号串
nil println
true println
3.5 println
'hello keel' println
"a" 1 + println     // a1：+ 遇到字符串就拼接
```

```keel
// 没有列表 / 映射字面量：list / map 建，append / put 填
list "xs" store
"xs" load 1 append drop
"xs" load 2 append drop
"xs" load 3 append drop
"xs" load len println        // 3
"xs" load 1 get println      // 2
"a,b,c" "," split len println  // 3：split 也产出列表
map "m" store
"m" load "k" 7 put drop
"m" load "k" get println     // 7
```

## 6. 变量

变量不是声明出来的，是写出来的：

- `值 名 store`：把值存到名字下（名字必须是字符串，否则消耗掉两个操作数并报错）；
- `名 load`：取出来；**未定义就报错，并压一个 nil 继续跑**；
- `名 delvar`：删掉；
- `名 hasvar`：压布尔；
- `vars`：把现有变量名排序后压成一个列表。

名字是运行时字符串，所以也能算出来。变量表挂在 `KeelData` 上，**按 VM 实例存放**：
同一个 VM 里所有块共享同一张表（没有块作用域、没有局部变量、没有遮蔽），不同 VM 之间
互不可见。`keelld` 的 `install` 会清空变量表（`reset_state`），所以"一轮"之内变量一直在。

```keel
// 变量：值 名 store，名 load；按 VM 实例存放，没有块作用域
0 "count" store
"count" load 1 + "count" store
"count" load println          // 1
"count" hasvar println        // true
"count" delvar
"count" hasvar println        // false
vars println                  // []：变量表已空
```

## 7. 条件

核心条件只有三条指令：`条件 if 真分支 else 假分支 then`。

- `if` **弹出并消费**条件；条件真就顺着往下执行真分支，假就跳到同层的 `else` 之后
  （没有 `else` 就跳到 `then`）。
- `else` 是"真分支到此结束"的标记，执行到它时跳到配对的 `then`。
- `then` 什么也不做，只是配对标记。

配对是按**当前块体的 token** 扫出来的：`if` 会让深度加一，`then` 减一，深度为 0 的那个
`then` 才是配对目标。因此一个 `if` 必须和它的 `then` 在同一个块体里，跨块（例如 `if`
在调用者里、`then` 在被调块里）不合法。缺 `then` 时报错停机。

`else` 也可以省略；`else` 后面可以再跟 `if`（就是"否则如果"）。

```keel
// if 消费条件：假则跳到 else（没有 else 就跳到 then）
7 "n" store
"n" load 3 > if "big" else "small" then println    // big
2 "n" store
"n" load 3 > if "big" else "small" then println    // small
```

## 8. 循环

三种循环都是**同一块体内的 token 配对**，循环记录存在实例状态里，并记着"属于哪个块"：

**计数循环 `N [ 体 ]`**：`[` 从栈顶取次数 N（非数字报错），N <= 0 直接跳过整段；
每走到 `]` 次数减一，还有剩余就跳回体首。可以嵌套，嵌套靠 `[` `]` 的深度配对。

**先做后判 `begin 体 until`**：`until` 弹条件，条件真就退出，假就回到体首。
所以体至少执行一次。

**无限循环 `begin 体 again`**：`again` 无条件回到体首，只能靠 `break` 出去。

`break` 弹出当前块里最内层的循环记录，跳到该循环终止指令（`]` / `until` / `again`）之后。
循环记录带块名，在别的块里 `break` 不会误伤调用者的循环，也会报错。

```keel
// N [ ... ] 计数循环；循环记录按块名校验，支持嵌套
0 "acc" store
5 [ "acc" load 2 + "acc" store ]
"acc" load println        // 10
3 [ 2 [ "acc" load 1 + "acc" store ] ]
"acc" load println        // 16
```

```keel
// begin ... until 先做后判；begin ... again 靠 break 退出
0 "i" store
begin
  "i" load 1 + "i" store
  "i" load 3 >=
until
"i" load println      // 3
begin "i" load 12 > if break then "i" load 1 + "i" store again
"i" load println      // 13
```

## 9. 错误模型

错误分两层，**判据完全不同**：

**装载期（诊断）**：词法、结构扫描、表面语法前端产生 `keelld::Diagnostic`。任何一条
`Sev::Error` 都会让 `Program::ok()` 为假，`install` 直接拒绝装载，VM 一步都不会跑。
`Diagnostic` 带文件、行、列（列可能为 0）。

**运行期（错误）**：内置指令出错统一走 `keel::Error(...).report()`，把一条记录追加到
进程级的 `keel_errors()`。它**不中断执行**：多数指令在报错后还会按原栈形状压一个
nil / false，让后面的 token 继续跑。所以"这轮成功"的判据是
`keel::keel_errors().empty()`，不是 `data.end`。

运行期错误的定位信息是"块名 + token 下标"：`step()` 在派发每条 token 之前记下
`current_name` 与 `ip`，`report()` 缺位置时补上。装载器再把它翻成"文件:行"（见
[LOADER.md](LOADER.md)）。

真正会让 VM 停下的只有这几种：

- `assert`（条件不成立）、`fail`（无条件）：报一条错并停机；
- `halt`：干净停机，不报错；
- 结构性错误：`{` 的块名不是非空字符串、缺配对的 `}`、单独的 `}`、`if`/`else` 找不到
  `then`、`begin` 找不到 `until`/`again`、`[` 找不到 `]`；
- `include` 失败：找不到文件、路径越出沙箱、循环包含、嵌套过深、读不了或被包含文件有诊断；
- 资源越限：步数、栈深、调用深度（见第 10 节）；
- 入口块执行到末尾或执行 `ret`。

**报错但不停机**的典型例子：`load` 未定义变量、`call` 不存在的块、`]` 没有配对的 `[`、
`until`/`again` 没有配对的 `begin`、`break` 没有活动循环——它们各记一条错误，然后继续。

错误容器只留最近 100 条，更早的被挤掉；累计条数另有计数。同一位置同一条描述会被
`format_errors_for_ai` 聚合成 `(xN)`，所以死循环里刷屏的错误不会撑爆反馈。

```keel
// 内置指令出错不中断执行（u::err）；assert / fail / halt 才停
"x" load println       // load: undefined variable 'x'，压 nil 继续
errcount println       // 1
lasterr println        // load: undefined variable 'x'
false "boom" assert    // 报错并停机，后面不再执行
"never" println
```

`errcount` / `lasterr` 只看当前错误容器：宿主应当在一轮开始前清空它（`install` 会调用
`keel::clear_errors()`），否则读到的是上一轮残留。

## 10. 限额

`KeelData` 里有两组数：**软上限**（脚本能用 `limit` 改）和**硬天花板**（只有宿主能改）。

| 名字 | 软上限默认 | 硬天花板默认 | 含义 |
| --- | --- | --- | --- |
| `steps` | 10000000 | 100000000 | 执行多少条 token |
| `depth` | 1000 | 10000 | 块调用栈深度 |
| `stack` | 100000 | 1000000 | 数据栈元素个数 |
| `alloc` | 16 MiB | 256 MiB | 单次分配上限 |

上表是 `KeelData` 自己的默认值。**用 `keelld` 装载时会被 `Environment::limits` 覆盖**，
它的默认是 `steps = 200000`、`depth = 200`、`stack = 10000`、`alloc = 0`（0 表示"不改"）、
`max_adjust = 4`。所以同一条脚本在裸 VM 和 `keelld` 下能跑多长不一样，看限额要先看是谁
装的。

`值 名 limit` 的语义：名字取 `steps` / `depth` / `stack` / `alloc`；值 <= 0 一律当 0；
结果被夹到对应硬天花板；压回一个布尔——**true 表示原样生效，false 表示被夹过**。名字不是
字符串、名字未知、值不是数字、`max_adjust` 预算用完这几种失败也都压 false，并各报一条错。
每次成功调用消耗一次 `max_adjust` 预算：VM 默认 20 次，装载器的 `Limits` 默认收紧到 4 次。
`名 getlimit` 读回当前软上限。硬天花板没有查询指令，脚本也越不过去。

检查点分布在不同位置，语义并不统一：

- **steps**：`step()` 每处理一条 token 前自增并比较，超了报 `run: step limit exceeded (...)` 并停机。
- **stack**：`step()` 每步比较一次，超了报 `run: stack size limit exceeded (...)` 并停机。
  注意比较是"当前大小 > 上限"，所以上限就是允许的最大元素个数。
- **depth**：`push_block` 在压栈**之前**检查，超了报调用深度超限并停机（`call` 与
  `include` 都走这里）。
- **alloc**：只在具体分配点检查（容器扩容、字符串切分、整块读文件等），报
  `allocation too large` 后按各自的失败语义压 nil/false，**不停机**。它不是"堆总量配额"，
  VM 不统计累计分配。

```keel
// 软上限能用 limit 调，但被 clamp 到硬天花板；返回值说明是否原样生效
5000 "depth" limit println        // true
"depth" getlimit println          // 5000
20000 "depth" limit println       // false：被硬天花板 10000 截断
"depth" getlimit println          // 10000
```

```keel
// 越限会停机：把栈上限压到 1，再压两个数
1 "stack" limit println   // true
1 2                       // 栈里变成 2 个元素，超过 1：下一步的保险丝报错停机
"never" println
```

## 11. 和别的文档怎么分工

- 指令清单、每条指令的栈效果、全部别名：[REFERENCE.md](REFERENCE.md)。
- `let` / `if` / `while` / `fn` / `return` / `include` 的表面写法与脱糖：[SURFACE.md](SURFACE.md)。
- 源码怎么变成 `Program`、`install` 的固定时序、`include` 的路径规则、错误怎么翻回
  "文件:行"：[LOADER.md](LOADER.md)。
- 宿主怎么注册自己的指令、怎么单步、怎么接输出：[EMBEDDING.md](EMBEDDING.md)。
