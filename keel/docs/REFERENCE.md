# Keel 指令参考手册

本手册按注册库分节，指令名逐个取自源码里的 `register_func("...")` 与 `alias(...)`，顺序即注册顺序；别名排在每节表格末尾。
`keel::Value` 实际有七种值：`nil`、`bool`、`number`（double）、`string`、`list`、`map`（键有序的 `std::map`）、`umap`（无序的 `std::unordered_map`）。
字面量由 `keel::parse_value` 解释：`nil`/`null`、`true`/`false`、可完整解析为数字的文本、成对引号包住的文本，其余一律当字符串。
容器是 `shared_ptr` 共享的：存进变量后再 `append`，所有副本都看得见同一份内容。
栈效果读法：`--` 左边是弹出的操作数（自左向右，最右为栈顶），右边是压回的结果（最右为新的栈顶）。
所以 `a b -- b a` 表示先弹 b、再弹 a，然后压 b、压 a；`-- n` 表示不弹只压一个，`x --` 表示弹掉不压。
出错统一走 `keel::Error(...).report()`，记进 `keel_errors()`、不抛异常；多数指令报错时会压回 `nil`/`false` 以保持栈高。
下文每节标题里的函数名就是该库的注册入口，可单独安装；`keelstd::register_all` 一次装齐前八节。

## 核心库（`keelbase::base_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `dup` | `a -- a a` | 复制栈顶 |
| `drop` | `a --` | 丢弃栈顶 |
| `swap` | `a b -- b a` | 交换栈顶两个 |
| `over` | `a b -- a b a` | 复制次栈顶到栈顶 |
| `rot` | `a b c -- b c a` | 三元素轮转，原栈顶转到最下面 |
| `pick` | `… n -- … x` | 复制从顶往下第 n 个（0 等同只取栈顶，1 等同 over），越界给 nil |
| `depth` | `-- n` | 当前数据栈深度 |
| `clear` | `… --` | 清空整个数据栈 |
| `+` | `a b -- a+b` | 两边都是数字时相加；任一侧是字符串或 nil 时改为拼接 |
| `concat` | `a b -- s` | 两边 `toString` 后拼接，永远成功（数字也拼文本） |
| `-` | `a b -- a-b` | 减法，非数字报错并压 nil |
| `*` | `a b -- a*b` | 乘法，非数字报错并压 nil |
| `/` | `a b -- a/b` | 除法，除零或非数字报错并压 nil |
| `%` | `a b -- a%b` | 取余，符号跟被除数，除零或非数字报错并压 nil |
| `neg` | `a -- -a` | 取负 |
| `abs` | `a -- 非负值` | 取绝对值 |
| `inc` | `a -- a+1` | 加一 |
| `dec` | `a -- a-1` | 减一 |
| `min` | `a b -- 较小值` | 取两个数中较小者 |
| `max` | `a b -- 较大值` | 取两个数中较大者 |
| `clamp` | `v lo hi -- v'` | 把 v 夹到 lo 与 hi 之间 |
| `==` | `a b -- bool` | 相等：nil/容器按结构比较，数字优先，否则比文本 |
| `!=` | `a b -- bool` | 不等 |
| `<` | `a b -- bool` | 小于：两边都能当数字时比数值，否则比文本 |
| `>` | `a b -- bool` | 大于，比较规则同上 |
| `<=` | `a b -- bool` | 小于等于，比较规则同上 |
| `>=` | `a b -- bool` | 大于等于，比较规则同上 |
| `and` | `a b -- bool` | 逻辑与，两边按 truthy 判定 |
| `or` | `a b -- bool` | 逻辑或 |
| `xor` | `a b -- bool` | 逻辑异或 |
| `not` | `a -- bool` | 逻辑非 |
| `str` | `x -- s` | 转字符串（`toString`） |
| `num` | `x -- n` | 转数字，失败报错并压 nil |
| `int` | `x -- n` | 向零截断成整数 |
| `bool` | `x -- bool` | 按 truthy 转布尔 |
| `type` | `x -- s` | 类型名：nil/bool/number/string/list/map/unordered_map |
| `len` | `x -- n` | 字符串按字节计、容器按元素计，其它类型为 0 |
| `isnil` | `x -- bool` | 是否为 nil |
| `store` | `值 名 --` | 写入本实例的变量表，名必须是字符串且在栈顶 |
| `load` | `名 -- 值` | 读变量，未定义时报错并压 nil |
| `delvar` | `名 --` | 删除变量，变量不存在不报错 |
| `hasvar` | `名 -- bool` | 变量是否存在 |
| `vars` | `-- list` | 全部变量名，已排序 |
| `{` | `名 --` | 运行时定义块：把到配对 `}` 之前的内容存成块并跳过整段 |
| `}` | `--` | 孤立的右花括号：报错并停机 |
| `call` | `名 --` | 按名调用块，未知块报错，受调用深度上限约束 |
| `ret` | `--` | 返回调用者；入口块执行它就是结束 |
| `lit` | `-- x` | 把下一个 token 强制当字面量压栈（哪怕它是指令名） |
| `if` | `条件 --` | 假则跳到 `else` 之后，没有 `else` 则跳到 `then` |
| `else` | `--` | 真分支执行完后跳到配对的 `then` |
| `then` | `--` | 分支汇合标记，空操作 |
| `[` | `n --` | 计数循环开始，n<=0 时整段跳过 |
| `]` | `--` | 循环计数减一，未到零则回跳，否则退出 |
| `begin` | `--` | 循环开始，与 `until` 或 `again` 配对 |
| `until` | `条件 --` | 条件为假则回跳继续，为真则退出（do-while） |
| `again` | `--` | 无条件回跳，只能靠 `break` 退出 |
| `break` | `--` | 跳出当前块的最内层循环 |
| `jump` | `目标 --` | 跳到块内绝对指令下标，越界报错 |
| `jz` | `条件 目标 --` | 条件为假时跳转 |
| `jnz` | `条件 目标 --` | 条件为真时跳转 |
| `nop` | `--` | 空操作 |
| `halt` | `--` | 立即停机 |
| `assert` | `条件 消息 --` | 条件为假时报错并停机 |
| `fail` | `消息 --` | 直接报错并停机 |
| `limit` | `值 名 -- bool` | 设软上限，名取 steps/depth/stack/alloc；被硬天花板裁剪或失败都压 false |
| `getlimit` | `名 -- n` | 查当前软上限，未知名报错并压 nil |
| `errcount` | `-- n` | 当前错误容器里的条数（上限 100 条） |
| `lasterr` | `-- s` | 最近一条错误的文本，没有错误时为空串 |
| `add` | `a b -- a+b` | 同 `+` |
| `sub` | `a b -- a-b` | 同 `-` |
| `mul` | `a b -- a*b` | 同 `*` |
| `div` | `a b -- a/b` | 同 `/` |
| `mod` | `a b -- a%b` | 同 `%` |
| `eq` | `a b -- bool` | 同 `==` |
| `ne` | `a b -- bool` | 同 `!=` |
| `lt` | `a b -- bool` | 同 `<` |
| `gt` | `a b -- bool` | 同 `>` |
| `le` | `a b -- bool` | 同 `<=` |
| `ge` | `a b -- bool` | 同 `>=` |
| `&&` | `a b -- bool` | 同 `and` |
| `\|\|` | `a b -- bool` | 同 `or` |
| `!` | `a -- bool` | 同 `not` |
| `tostr` | `x -- s` | 同 `str` |
| `tostring` | `x -- s` | 同 `str` |
| `tonum` | `x -- n` | 同 `num` |
| `toint` | `x -- n` | 同 `int` |
| `tobool` | `x -- bool` | 同 `bool` |
| `size` | `x -- n` | 同 `len` |
| `length` | `x -- n` | 同 `len` |

## 字符串库（`keelstr::str_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `trim` | `s -- s'` | 去掉首尾空白 |
| `ltrim` | `s -- s'` | 只去左侧空白 |
| `rtrim` | `s -- s'` | 只去右侧空白 |
| `upper` | `s -- S` | 转大写，仅 ASCII |
| `lower` | `s -- s'` | 转小写，仅 ASCII |
| `substr` | `s 起 长 -- 子串` | 起为负从尾部算，长为负表示取到末尾 |
| `charat` | `s i -- 单字符` | 下标为负从尾部算，越界给空串并报错 |
| `chars` | `s -- list` | 按字节切成单字符列表 |
| `find` | `s 子 -- 下标` | 首次出现的下标，找不到 -1 |
| `rfind` | `s 子 -- 下标` | 最后一次出现的下标，找不到 -1 |
| `contains` | `s 子 -- bool` | 是否包含子串 |
| `startswith` | `s 前缀 -- bool` | 是否以前缀开头 |
| `endswith` | `s 后缀 -- bool` | 是否以后缀结尾 |
| `count` | `s 子 -- n` | 不重叠出现次数，空子串给 0 |
| `replace` | `s 旧 新 -- s'` | 全部替换，旧串为空时原样返回 |
| `split` | `s 分隔 -- list` | 分隔符为空串时按空白切分并丢弃空段 |
| `join` | `list 分隔 -- s` | 用分隔符拼接列表元素 |
| `repeat` | `s n -- s'` | 重复 n 次，n<=0 或空串得空串 |
| `reverse` | `s -- s'` | 按字节反转 |
| `strip` | `s -- s'` | 同 `trim` |
| `starts` | `s 前缀 -- bool` | 同 `startswith` |
| `ends` | `s 后缀 -- bool` | 同 `endswith` |
| `slice` | `s 起 长 -- 子串` | 同 `substr` |
| `indexof` | `s 子 -- 下标` | 同 `find` |
| `lowercase` | `s -- s'` | 同 `lower` |
| `uppercase` | `s -- S` | 同 `upper` |

## 容器库（`keelcont::cont_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `list` | `-- list` | 新建空列表 |
| `map` | `-- map` | 新建空映射（键有序） |
| `umap` | `-- umap` | 新建空无序映射 |
| `append` | `list v -- list` | 尾部追加，压回同一个列表 |
| `get` | `容器 键 -- 值` | 缺失键给 nil；列表下标越界报错 |
| `put` | `容器 键 值 -- 容器` | 映射按键写；列表下标越界报错（不追加） |
| `has` | `容器 键 -- bool` | 列表判下标、映射判键、字符串判子串 |
| `del` | `容器 键 -- 容器` | 列表删元素、映射删键；列表越界报错 |
| `keys` | `map -- list` | 全部键，映射按序 |
| `values` | `map -- list` | 全部值 |
| `first` | `list -- 值` | 首元素，空表或非列表给 nil |
| `last` | `list -- 值` | 末元素，空表或非列表给 nil |
| `extend` | `list1 list2 -- list1` | 把 list2 就地接到 list1 尾部（同一列表也安全） |
| `sort` | `list -- list` | 就地稳定排序，两边都能当数字时比数值，否则比文本 |
| `dict` | `-- map` | 同 `map` |
| `array` | `-- list` | 同 `list` |
| `push` | `list v -- list` | 同 `append` |

## 数学库（`keelmath::math_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `sqrt` | `x -- y` | 平方根 |
| `exp` | `x -- y` | e 的 x 次幂 |
| `ln` | `x -- y` | 自然对数 |
| `log10` | `x -- y` | 常用对数 |
| `log2` | `x -- y` | 以 2 为底的对数 |
| `sin` | `x -- y` | 正弦，弧度 |
| `cos` | `x -- y` | 余弦，弧度 |
| `tan` | `x -- y` | 正切，弧度 |
| `asin` | `x -- y` | 反正弦，弧度 |
| `acos` | `x -- y` | 反余弦，弧度 |
| `atan` | `x -- y` | 反正切，弧度 |
| `floor` | `x -- y` | 向下取整 |
| `ceil` | `x -- y` | 向上取整 |
| `round` | `x -- y` | 四舍五入 |
| `trunc` | `x -- y` | 向零取整 |
| `isnan` | `x -- bool` | 是否为 NaN |
| `sign` | `x -- -1/0/1` | 符号函数 |
| `pow` | `x y -- x^y` | 幂 |
| `hypot` | `x y -- r` | 直角三角形的斜边 |
| `fmod` | `x y -- r` | 浮点余数，符号跟被除数 |
| `atan2` | `y x -- 角` | 方位角，第一个操作数是 y |
| `pi` | `-- n` | 压入 π |
| `e` | `-- n` | 压入自然常数 |
| `deg` | `弧度 -- 度` | 弧度转角度 |
| `rad` | `度 -- 弧度` | 角度转弧度 |
| `log` | `x -- y` | 同 `ln` |
| `lg` | `x -- y` | 同 `log10` |
| `sqr` | `x -- y` | 同 `sqrt` |
| `signum` | `x -- -1/0/1` | 同 `sign` |

## 简单 IO 库（`keelio::io_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `print` | `x --` | 打到 stdout，不换行 |
| `println` | `x --` | 打到 stdout 并换行 |
| `nl` | `--` | 只打印一个换行 |
| `eprint` | `x --` | 打到 stderr，不换行 |
| `eprintln` | `x --` | 打到 stderr 并换行 |
| `flush` | `--` | 刷新 stdout 与 stderr |
| `readline` | `-- s` | 读一行并去掉 `\r\n`，EOF 给 nil |
| `readall` | `-- s` | 读掉剩余全部 stdin |
| `readfile` | `路径 -- s` | 读文本文件，失败报错并给 nil |
| `writefile` | `内容 路径 -- bool` | 覆盖写，失败报错并给 false |
| `appendfile` | `内容 路径 -- bool` | 追加写，失败报错并给 false |
| `exists` | `路径 -- bool` | 文件或目录是否存在 |
| `dump` | `--` | 打印当前栈内容，不弹栈，调试用 |
| `puts` | `x --` | 同 `println` |
| `echo` | `x --` | 同 `println` |
| `input` | `-- s` | 同 `readline` |
| `readln` | `-- s` | 同 `readline` |

## 快速 IO 库（`keelfast::fast_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `f.out` | `x --` | 写 stdout，不换行 |
| `f.outln` | `x --` | 写 stdout 并换行 |
| `f.err` | `x --` | 写 stderr，不换行 |
| `f.errln` | `x --` | 写 stderr 并换行 |
| `f.raw` | `s --` | 原样写字节，不做格式化 |
| `f.flush` | `--` | 把 stdout/stderr 缓冲全刷出 |
| `f.spit` | `内容 路径 -- bool` | 一次性整写并覆盖 |
| `f.slurp` | `路径 -- s` | 一次性整读，超过 `max_alloc` 报错并给 nil |

## 大文件 / 流库（`keelbig::big_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `big.open` | `路径 模式 -- 句柄` | 打开文件，模式取 r/w/a/rw（也认 read/write/append/readwrite/r+） |
| `big.close` | `句柄 -- bool` | 关闭并释放句柄，先刷写缓冲 |
| `big.read` | `句柄 n -- s` | 最多读 n 字节，n<=0 读剩余，超过 `max_alloc` 报错 |
| `big.readln` | `句柄 -- s` | 读一行并去掉 `\r\n`，EOF 给 nil |
| `big.write` | `句柄 内容 -- bool` | 先进写缓冲，满 256KB 或 flush/close 时落盘 |
| `big.flush` | `句柄 -- bool` | 把写缓冲刷到磁盘 |
| `big.seek` | `句柄 位置 -- 新位置` | 定位，负位置按 0 处理，失败给 nil |
| `big.tell` | `句柄 -- 位置` | 当前位置（已扣除读缓冲里没消费的部分） |
| `big.size` | `句柄 -- 字节数` | 文件总字节数 |
| `big.eof` | `句柄 -- bool` | 是否已到末尾（会按需预读一块） |
| `big.slurp` | `路径 -- s` | 整文件读入，上限 `max_alloc` |
| `big.spit` | `内容 路径 -- bool` | 原子写（临时文件 + 改名） |
| `big.append` | `内容 路径 -- bool` | 追加写 |
| `big.copy` | `源 目标 -- 字节数` | 按 256KB 分块复制，不整读进内存，失败给 nil |
| `big.stat` | `路径 -- map` | 含 exists/size/is_dir/mtime 四个键 |
| `big.list` | `路径 -- list` | 目录项名字，已排序，失败给 nil |
| `big.mkdir` | `路径 -- bool` | 逐级建目录 |
| `big.remove` | `路径 -- bool` | 删文件或空目录 |
| `big.rename` | `源 目标 -- bool` | 改名，Windows 上不覆盖已存在的目标 |
| `stream.new` | `-- 句柄` | 新建内存流 |
| `stream.write` | `流 内容 -- bool` | 在当前位置写，必要时补零扩展 |
| `stream.read` | `流 n -- s` | 读 n 字节，n<=0 读剩余 |
| `stream.seek` | `流 位置 -- 新位置` | 定位，负位置按 0 处理 |
| `stream.size` | `流 -- n` | 流内字节数，无效句柄给 0 |
| `stream.save` | `流 路径 -- bool` | 原子写到文件 |
| `stream.free` | `流 -- bool` | 释放流，句柄不存在给 false |

## 动态源库（`keelsrc::src_register`）

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `src.load` | `路径 -- s` | 取源文本，磁盘签名变了就重读，否则给缓存 |
| `src.reload` | `路径 -- s` | 强制重读，不看缓存 |
| `src.text` | `路径 -- s` | 只取已缓存的那份，不碰磁盘 |
| `src.loaded` | `路径 -- bool` | 该路径当前是否有有效缓存 |
| `src.changed` | `路径 -- bool` | 缓存之后磁盘上的文件是否变了或没了 |
| `src.version` | `路径 -- n` | 缓存文本的版本号（FNV-1a 低 53 位），未缓存给 0 |
| `src.tokens` | `路径 -- list` | 取源文本并切词（空白分隔，引号保护空格） |
| `src.meta` | `路径 -- map` | 含 path/exists/is_dir/size/mtime/version/loads/missing |
| `src.list` | `-- list` | 已缓存的全部路径，已排序 |
| `src.drop` | `路径 -- bool` | 丢掉缓存项 |
| `src.limit` | `字节数 -- 旧上限` | 单次加载上限，夹在 1KB..64MB，默认 8MB |

## 装载器与表面语法附带的指令

这两条不属于上面八个库，分别由 `keelld::install` 与 `keelld::surface::register_runtime` 按需注册。

| 指令 | 栈效果 | 说明 |
| --- | --- | --- |
| `include` | `路径 --` | 把文件装成 `__inc_N` 块并立刻执行；`Environment::enable_include` 为真时才注册 |
| `s.add` | `a b -- a+b` | 表面语法用的智能加：任一侧是字符串就拼接，否则数值相加 |

## C++ 侧入口

```cpp
// Keel.hpp：实例句柄与执行
struct keel::KeelData;                          // 全部运行时状态（栈、块表、限额、沙箱根）
using keel::Func = void (*)(keel::KeelData &);
class keel::Keel {
    KeelData data;
    ~Keel();                                     // 调用 data.on_destroy，让各库清掉自己的状态
    void init(const std::string &entry_block);   // 复位执行位置并压入入口块，不清栈
    bool step();                                 // 执行一条指令；false 表示已停
    void run();                                  // while (!data.end) step();
    void register_func(std::string name, Func f);
};

// KeelStd.hpp：一次装齐
void keelstd::register_all(keel::Keel &a);       // 五个核心库 + 三个 IO 扩展库
void keelstd::register_io_extras(keel::Keel &a);
void keelstd::reset_state(keel::KeelData &d);    // 清栈/变量/循环栈/ip，保留 codes

// KeelLoader.hpp：装载与跑一轮（语义见 LOADER.md）
keelld::Program     keelld::load_text(std::string_view src, const std::string &file = "<text>",
                                      const Options &opt = Options());
keelld::Program     keelld::load_file(const std::filesystem::path &path,
                                      const Options &opt = Options());
keelld::Prepared    keelld::install(keel::Keel &a, const Program &p,
                                    const Environment &env = Environment());
keelld::RoundResult keelld::round(keel::Keel &a, const std::string &src,
                                  const std::string &entry = "main",
                                  std::size_t max_steps = 200000);
keelld::RoundResult keelld::execute(keel::Keel &a);  // run + 刷新快速 IO + 收错误
```

`KeelJSON.hpp`（`keel::json_encode` / `keel::json_decode`）、`KeelIOCore.hpp`、`KeelInstanceState.hpp`、`KeelSyntax.hpp`、`KeelUtil.hpp` 都不注册任何指令。

## 错误与限额

- 报错：`keel::Error(msg[, word[, code]]).report()` 自带位置补全——`step()` 派发每条指令前把「当前块名 + ip」记进 `keel_error_pos()`，`report()` 复制一份再填 `where_`/`token_`，然后追加到 `keel_errors()`。
- 容器：`keel_errors()` 只保留最近 `kMaxErrors`（100）条，`keel_error_total()` 累计总数且不归零；`keel_errors_to_stderr()` 为真时才同时打到 stderr（默认关）；`clear_errors()` 清容器；`format_errors_for_ai()` 把同一 `(块, ip, 字节偏移, 描述)` 聚合成 `(xN)` 的文本。
- 查询：`errcount` 读容器条数，`lasterr` 读 `errs.back().what()`；`ok` 的判据是 `keel_errors().empty()`，不是 `data.end`（`u::err` 不中断执行）。
- 软上限（脚本可用 `limit` 改）：`max_steps` 默认 1e7、`max_depth` 1000、`max_stack` 100000、`max_alloc` 16MB；硬天花板 `hard_steps` 1e8、`hard_depth` 10000、`hard_stack` 1e6、`hard_alloc` 256MB 只能由宿主设定。
- `limit`：`值 名 -- bool`，名取 `steps`/`depth`/`stack`/`alloc`；值 <= 0 按 0 处理，结果被夹到对应硬天花板，压回 `capped == want`（true 表示没被裁剪）。每次调用消耗一次 `max_adjust` 预算（默认 20，装载器 `Limits` 会收紧到 4），预算用完或名字未知时报错并压 false。`getlimit` 只读当前软上限。
- 超限行为：步数超 `max_steps` 时报 `run: step limit exceeded`、栈深超 `max_stack` 时报 `run: stack size limit exceeded`，两者都在 `step()` 里把 `data.end` 置真并返回 false，不会静默停；调用深度超 `max_depth` 时由 `push_block` 报 `block call depth limit exceeded` 并停机；单次分配超 `max_alloc` 时由各库的 `check_alloc` 报 `allocation too large`，只压回 nil/false，不停止 VM。
- `keelld::apply_limits` 用 `Limits{steps, depth, stack, alloc, max_adjust}` 收紧软上限，`alloc` 为 0 表示不改。

指令总数：核心五库 178 条（base 89 + str 26 + cont 17 + math 29 + io 17），三个 IO 扩展库 45 条（fast 8 + big 26 + src 11），装载器与表面语法附带 2 条，合计 225 条；其中别名 39 条，独立指令名 186 个。
