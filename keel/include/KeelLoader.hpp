#pragma once
// ============================================================================
//  KeelLoader.hpp —— 脚本装载器（运行时权威版）
// ----------------------------------------------------------------------------
//  一句话：**源码 = 入口块的块体（body）**。装载器只把文件变成 token、
//  定义入口块、把环境准备好；其余块由 VM 在执行到 `{` 时自己定义。
//
//  为什么是 body，而不是"文件里包含 main { ... }"：
//    * `{` 从栈顶取块名，名字可以是算出来的（"na" "me" concat）——
//      装载器**不可能**成为块表的权威，硬要做只能靠"名字必须是字面量"改语言；
//    * 这是 Forth 的模型：文件就是一串词，`INCLUDE` 就是把文件压进输入源，
//      字典边长边跑。`{` 就是那个"边跑边定义"的 Defining Word。
//    * 所以这里不提供"静态定义所有块"的模式。文件要写成 body：
//
//          "sq" { dup * }          // 运行时定义 sq
//          5 "sq" call println     // 运行时调用
//
//      如果你手里是 `main { ... }` 形式的文件，去掉外层 main/大括号即可。
//      装载器会**诊断**这种写法（入口块同名重定义），不会让它在运行时炸得莫名其妙。
//
//  那 loader 还剩下什么活？四件：
//    1. 源 -> token（编码 / BOM / 注释 / 引号 / 行列号 / 上限）；
//    2. 非权威结构扫描：把 `名字 {` 的位置和括号配对记下来，给诊断、断点、行号
//       映射用；**只报告、不定义块**。名字算出来的那种扫描不到，运行时照常工作；
//    3. 环境：入口名、限额、include 路径/沙箱/深度、prelude、stdout 汇；
//    4. 会话：prepare / execute / round，以及把运行时错误 (块, ip) 翻回 文件:行。
//
//  一次 prepare 的固定时序（顺序错了会踩坑）：
//      clear_errors
//        -> define_block(entry, body)     只定义入口块
//        -> reset_state                   清栈/变量/循环栈/ip（保留 codes）
//        -> apply_limits                  收紧 steps/depth/stack/adjust
//        -> set stdout sink / register include
//        -> 跑 prelude（可选，定义标准块/变量；init 不会清变量，所以能留到 main）
//        -> init(entry)                   只清执行位置，不清栈
//  之后 execute() = run + flush 快速 IO + 收错误。
//  （宿主想在跑之前往栈里塞数据：塞在 prepare 之后，因为 reset_state 会清栈。）
//
//  几个千万别踩的坑：
//    * init() 不清 stack；复用实例必须先 reset_state，否则上一轮的栈还在。
//    * define_block 是改块的唯一入口；直接写 codes 会同长度重定义踩旧函数指针。
//    * 注册指令必须早于 define_block（compile_block 当场查 funcs，查不到就编成空指针）。
//    * ok 看 keel_errors().empty()，不是 data.end —— u::err 不中断执行。
//    * 词法器不处理转义（和 fast_split_tokens 一致）：字符串里放不进 `"`，
//      要结构化数据就预置栈、或改用单引号包外层。
// ============================================================================

#include "Keel.hpp"
#include "KeelFastIO.hpp"
#include "KeelInstanceState.hpp"
#include "KeelStd.hpp"
#include "KeelSurface.hpp"
#include "KeelSyntax.hpp"
#include "KeelUtil.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keelld
{
    namespace fs = std::filesystem;
    namespace u = keel::util;
    namespace ioc = keelio_impl;

    // ======================================================================== 基础类型
    // Sev / Diagnostic / TokenKind / Token / Options / tokenize 都在 KeelSyntax.hpp，
    // 表面语法前端也共用它们。

    /// 静态视角下的一个块。**不一定是权威的**：只覆盖 `字面量 {` 这种形状。
    struct Block
    {
        std::string name;
        keel::strings body;     // 不含自己的 `{` `}`
        std::vector<int> lines; // 与 body 一一对应
        int line = 1;           // 名字/`{` 所在行
        int end_line = 1;       // 配对的 `}`
        bool top_level = true;  // 位于入口 body 的顶层（大括号深度 0）
        bool synthetic = false; // true = 入口块本身（整份 body），不是源码里显式定义的

        int line_at(std::size_t ip) const
        {
            return ip < lines.size() ? lines[ip] : -1;
        }
    };

    struct Program
    {
        std::string file = "<text>";
        std::string entry = "main";
        Syntax syntax = Syntax::Core;
        std::string source;        // 原文（BOM 已剥）
        Options options;           // 装载时用的选项（include 沿用）
        std::vector<Token> tokens; // 全部 token（含位置）
        std::vector<Block> blocks; // [0] = 入口块（synthetic），其后是静态可见的顶层块
        std::vector<Block> nested; // 更深的块定义（只用于行号映射）
        std::vector<Diagnostic> diags;

        bool ok() const
        {
            for (const auto &d : diags)
                if (d.fatal())
                    return false;
            return true;
        }

        const Block *entry_block() const
        {
            return blocks.empty() || !blocks.front().synthetic ? nullptr : &blocks.front();
        }

        /// 所有静态可见的块（入口 + 顶层 + 嵌套），后定义的优先。
        const Block *find_any(const std::string &name) const
        {
            for (auto it = blocks.rbegin(); it != blocks.rend(); ++it)
                if (it->name == name)
                    return &*it;
            for (auto it = nested.rbegin(); it != nested.rend(); ++it)
                if (it->name == name)
                    return &*it;
            return nullptr;
        }

        const Block *find(const std::string &name) const { return find_any(name); }

        bool has_block(const std::string &name) const { return find_any(name) != nullptr; }

        /// 某块第 ip 条 token 的源码行；-1 = 静态视角看不见（运行时算名的块）。
        int line_of(const std::string &name, std::size_t ip) const
        {
            const Block *b = find_any(name);
            return b ? b->line_at(ip) : -1;
        }

        std::string dump() const
        {
            std::string out = "file=" + file + " entry=" + entry + " tokens=" +
                              std::to_string(tokens.size()) + " blocks=" +
                              std::to_string(blocks.size()) + " nested=" +
                              std::to_string(nested.size()) + "\n";
            for (const auto &b : blocks)
                out += std::string(b.synthetic ? "  [entry] " : "  ") + b.name + "  (line " +
                       std::to_string(b.line) + ", " + std::to_string(b.body.size()) + " tokens)\n";
            for (const auto &d : diags)
                out += std::string(d.fatal() ? "  [error] " : "  [warn ] ") +
                       (d.line > 0 ? "line " + std::to_string(d.line) : "line ?") +
                       (d.col > 0 ? ":" + std::to_string(d.col) : "") + ": " + d.message + "\n";
            return out;
        }
    };

    // ======================================================================== 输入卫生
    // strip_bom_inplace 在 KeelSyntax.hpp。

    namespace detail
    {
        inline std::string dir_of(const std::string &file)
        {
            if (file.empty() || file.front() == '<')
                return {};
            std::error_code ec;
            const fs::path p = ioc::from_utf8(file);
            const fs::path parent = p.parent_path();
            if (parent.empty())
                return ioc::to_utf8(fs::current_path(ec));
            return ioc::to_utf8(parent);
        }

        inline std::string canon_path(const fs::path &p)
        {
            std::error_code ec;
            fs::path c = fs::weakly_canonical(p, ec);
            if (ec)
                c = fs::absolute(p, ec).lexically_normal();
            return ioc::to_utf8(c);
        }

        /// 路径是否在 root 里（按路径组件比较；Windows 下转小写再比）。
        inline bool path_within(const std::string &root, const std::string &path)
        {
            if (root.empty())
                return true;
            std::string r = root;
            std::string t = path;
#if defined(_WIN32)
            for (char &c : r)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            for (char &c : t)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
#endif
            if (t.size() < r.size())
                return false;
            if (t.compare(0, r.size(), r) != 0)
                return false;
            if (t.size() == r.size())
                return true;
            const char sep = t[r.size()];
            return sep == '\\' || sep == '/';
        }
    } // namespace detail

    // ======================================================================== 词法
    // tokenize() 在 KeelSyntax.hpp（表面语法前端也共用同一份词法）。

    // ======================================================================== 结构扫描
    // 非权威：只找 `名字 { ... }` 这种形状，用来
    //   * 诊断括号不配对、入口块同名重定义；
    //   * 给调试器/错误回填提供 (块, ip) -> 行号。
    // 名字来自栈（算出来的）时这里看不见，运行时照常定义 —— 这是设计，不是缺陷。
    inline void scan_structure(Program &p, const Options &opt)
    {
        struct Open
        {
            std::size_t index = 0;
            std::string name;
            int line = 0;
            int depth = 0;
        };
        std::vector<Open> open;

        for (std::size_t i = 0; i < p.tokens.size(); ++i)
        {
            const Token &t = p.tokens[i];
            if (t.text == "{")
            {
                std::string name;
                if (i > 0)
                {
                    const Token &prev = p.tokens[i - 1];
                    if (prev.text != "{" && prev.text != "}")
                        name = detail::unquote(prev.text);
                }
                open.push_back(Open{i, std::move(name), t.line, static_cast<int>(open.size())});
                continue;
            }
            if (t.text != "}")
                continue;

            if (open.empty())
            {
                p.diags.push_back(Diagnostic{Sev::Error, p.file, t.line, t.col, "多余的 `}`"});
                continue;
            }

            Open o = open.back();
            open.pop_back();

            Block b;
            b.name = o.name;
            b.line = o.line;
            b.end_line = t.line;
            b.top_level = (o.depth == 0);
            b.body.reserve(i - o.index - 1);
            b.lines.reserve(i - o.index - 1);
            for (std::size_t k = o.index + 1; k < i; ++k)
            {
                b.body.push_back(p.tokens[k].text);
                b.lines.push_back(p.tokens[k].line);
            }

            if (b.name.empty())
            {
                // 名字由栈上数据决定（比如宿主预置了块名）：静态视角放弃这一块
                continue;
            }
            if (b.name == p.entry)
            {
                p.diags.push_back(Diagnostic{
                    Sev::Error, p.file, b.line, 0,
                    "body 里定义了与入口同名的块 '" + b.name +
                        "'：执行到 `{` 时会重定义正在运行的主块。"
                        "如果源码是 `main { ... }` 形式，本装载器按 body 装载，去掉外层 main 即可"});
                continue;
            }

            if (p.blocks.size() + p.nested.size() >= opt.max_blocks)
            {
                p.diags.push_back(Diagnostic{Sev::Error, p.file, b.line, 0,
                                             "静态块数量超过上限 " + std::to_string(opt.max_blocks)});
                return;
            }

            if (b.top_level)
                p.blocks.push_back(std::move(b));
            else
                p.nested.push_back(std::move(b));
        }

        for (const auto &o : open)
        {
            p.diags.push_back(Diagnostic{Sev::Error, p.file, o.line, 0,
                                         "未闭合的 `{`（块 '" + o.name + "' 缺少配对的 `}`）"});
            break; // 只报最外层那一个，免得刷屏
        }
    }

    // ======================================================================== 装载
    inline Program load_text(std::string_view src, const std::string &file = "<text>",
                             const Options &opt = Options())
    {
        Program p;
        p.file = file;
        p.entry = opt.entry;
        p.options = opt;
        p.syntax = opt.syntax;
        p.source.assign(src);

        if (p.source.size() > opt.max_bytes)
        {
            p.diags.push_back(Diagnostic{Sev::Error, file, 0, 0,
                                         "源码超过上限 " + std::to_string(opt.max_bytes) + " 字节"});
            return p;
        }
        if (detail::looks_utf16_or_binary(p.source))
        {
            p.diags.push_back(Diagnostic{Sev::Error, file, 0, 0,
                                         "看起来是 UTF-16/二进制文件；这里只接受 UTF-8 文本"});
            return p;
        }

        strip_bom_inplace(p.source);
        if (opt.syntax == Syntax::Surface)
            p.tokens = surface::compile(p.source, file, opt, p.diags);
        else
            p.tokens = tokenize(p.source, p.diags, opt, file);

        if (p.tokens.empty())
        {
            if (p.diags.empty())
                p.diags.push_back(Diagnostic{Sev::Error, file, 0, 0, "空源（或只有注释）"});
            return p;
        }

        // 入口块：整份 body
        Block entry;
        entry.name = opt.entry;
        entry.synthetic = true;
        entry.top_level = true;
        entry.line = p.tokens.front().line;
        entry.end_line = p.tokens.back().line;
        entry.body.reserve(p.tokens.size());
        entry.lines.reserve(p.tokens.size());
        for (const auto &t : p.tokens)
        {
            entry.body.push_back(t.text);
            entry.lines.push_back(t.line);
        }
        p.blocks.push_back(std::move(entry));

        scan_structure(p, opt);
        return p;
    }

    inline Program load_file(const fs::path &path, const Options &opt = Options())
    {
        Program bad;
        bad.entry = opt.entry;
        bad.file = ioc::to_utf8(path);
        bad.options = opt;

        std::string err;
        std::string text;
        if (!ioc::read_file_all(bad.file, opt.max_bytes, text, err))
        {
            bad.diags.push_back(Diagnostic{Sev::Error, bad.file, 0, 0, "打不开文件: " + err});
            return bad;
        }
        return load_text(text, bad.file, opt);
    }

    // ======================================================================== 环境
    struct Limits
    {
        std::size_t steps = 200000; // agent 一轮的步数预算
        std::size_t depth = 200;    // 块调用深度
        std::size_t stack = 10000;  // 数据栈深
        std::size_t alloc = 0;      // 0 = 不改（沿用 VM 默认 16MB）
        int max_adjust = 4;         // 脚本里 limit 最多调几次
    };

    /// 一次装载/运行的"环境"：限额、include 策略、预置、输出汇。
    struct Environment
    {
        Limits limits;
        bool enable_include = true;
        std::size_t max_include_depth = 16;
        std::vector<std::string> include_paths; // 相对路径：包含者目录 -> 这里 -> cwd
        std::string sandbox_root;               // 非空时，include 只能读这里面的文件
        std::string cwd;                        // 空 = 进程当前目录
        std::string prelude;                    // 每次 prepare 先跑的 body（定义标准块/变量）
        std::function<void(const char *, std::size_t)> stdout_sink; // 快速 IO 的输出去向
    };

    struct Prepared
    {
        bool ok = false;
        std::string error_text; // 装载/结构/prelude 失败时的说明
    };

    struct RoundResult
    {
        bool ok = false;
        std::size_t steps = 0;
        std::string feedback;  // 已按位置聚合的错误文本（给 agent 回灌）
        std::string diag_text; // 装载期诊断（文件:行:列: 说明）
    };

    // ======================================================================== include
    // 语义：`include "x.keel"` 在**执行到这一句时**读文件、定义临时块、压栈执行，
    // 执行完返回到 include 的下一条。相对 Forth 的 INCLUDE，但每次包含都进一个块，
    // 这样调用栈/深度上限/错误位置都能复用 VM 的机制。
    //
    // 路径解析顺序：包含者所在目录 -> Environment::include_paths -> cwd。
    // 防循环：目标文件已经在调用栈上的任何一个来源文件里 -> 报错并停机。
    // 缓存：按 (大小, 修改时间) 记住 token；文件改了下次 include 会重读。
    namespace detail
    {
        struct CachedSource
        {
            std::uint64_t size = 0;
            std::int64_t mtime = 0;
            std::vector<Token> tokens;
        };

        struct IncludeState
        {
            Environment env;
            Options options;
            std::unordered_map<std::string, std::string> block_dir;        // 块名 -> 所在目录
            std::unordered_map<std::string, std::string> block_file;       // 块名 -> 规范化文件
            std::unordered_map<std::string, std::vector<int>> block_lines; // 块名 -> ip 行号
            std::unordered_map<std::string, CachedSource> cache;           // 规范化文件 -> token
            int counter = 0;
            bool registered = false;
        };

    } // namespace detail

    using IncludeStates = keel::StateTable<detail::IncludeState>;

    namespace detail
    {
        inline bool resolve_include(const IncludeState &st, const std::string &base_dir,
                                    const std::string &raw, fs::path &out, std::string &err)
        {
            const fs::path given = ioc::from_utf8(raw);
            if (given.is_absolute())
            {
                std::error_code ec;
                if (fs::exists(given, ec))
                {
                    out = given;
                    return true;
                }
                err = "找不到文件: " + raw;
                return false;
            }

            std::vector<fs::path> dirs;
            if (!base_dir.empty())
                dirs.push_back(ioc::from_utf8(base_dir));
            for (const auto &d : st.env.include_paths)
                dirs.push_back(ioc::from_utf8(d));
            if (!st.env.cwd.empty())
                dirs.push_back(ioc::from_utf8(st.env.cwd));
            else
                dirs.push_back(fs::current_path());

            for (const auto &d : dirs)
            {
                std::error_code ec;
                const fs::path cand = d / given;
                if (fs::exists(cand, ec))
                {
                    out = cand;
                    return true;
                }
            }
            err = "找不到 include 目标: " + raw;
            return false;
        }

        inline void include_impl(keel::KeelData &d)
        {
            IncludeState *st = IncludeStates::find(d);
            if (!st)
            {
                u::err("include", "loader: include 没有启用");
                d.end = true;
                return;
            }

            std::string raw;
            if (!u::pop_text(d, "include", raw))
            {
                d.end = true;
                return;
            }

            std::string base = st->env.cwd;
            auto it = st->block_dir.find(d.current_name);
            if (it != st->block_dir.end())
                base = it->second;

            fs::path target;
            std::string err;
            if (!resolve_include(*st, base, raw, target, err))
            {
                u::err("include", err);
                d.end = true;
                return;
            }

            const std::string canon = canon_path(target);

            // 运行时 IO 沙箱统一走 util::check_sandbox（按路径组件比较）
            if (!u::check_sandbox(d, canon, "include"))
            {
                d.end = true;
                return;
            }

            // 循环：目标文件已经在当前调用栈的任何一个来源文件里
            int include_depth = 0;
            for (const auto &name : d.cache_code_ptr)
            {
                auto f = st->block_file.find(name);
                if (f == st->block_file.end())
                    continue;
                if (f->second == canon)
                {
                    u::err("include", "循环 include: " + canon);
                    d.end = true;
                    return;
                }
                if (name.rfind("__inc_", 0) == 0)
                    ++include_depth;
            }
            if (include_depth >= static_cast<int>(st->env.max_include_depth))
            {
                u::err("include", "include 嵌套超过上限 " + std::to_string(st->env.max_include_depth));
                d.end = true;
                return;
            }

            const ioc::FileSig sig = ioc::file_sig(target);
            if (!sig.exists || sig.is_dir)
            {
                u::err("include", "不是可读文件: " + canon);
                d.end = true;
                return;
            }

            auto cit = st->cache.find(canon);
            if (cit == st->cache.end() || cit->second.size != sig.size ||
                cit->second.mtime != sig.mtime)
            {
                std::string text;
                if (!ioc::read_file_all(canon, st->options.max_bytes, text, err))
                {
                    u::err("include", "读不了 '" + canon + "': " + err);
                    d.end = true;
                    return;
                }
                strip_bom_inplace(text);
                if (looks_utf16_or_binary(text))
                {
                    u::err("include", "'" + canon + "' 看起来不是 UTF-8 文本");
                    d.end = true;
                    return;
                }

                std::vector<Diagnostic> diags;
                std::vector<Token> toks =
                    st->options.syntax == Syntax::Surface
                        ? surface::compile(text, canon, st->options, diags)
                        : tokenize(text, diags, st->options, canon);
                for (const auto &dg : diags)
                {
                    if (!dg.fatal())
                        continue;
                    u::err("include", canon + ":" + std::to_string(dg.line) + ": " + dg.message);
                    d.end = true;
                    return;
                }

                CachedSource cs;
                cs.size = sig.size;
                cs.mtime = sig.mtime;
                cs.tokens = std::move(toks);
                st->cache[canon] = std::move(cs);
                cit = st->cache.find(canon);
            }

            const std::string name = "__inc_" + std::to_string(++st->counter);
            keel::strings body;
            std::vector<int> lines;
            body.reserve(cit->second.tokens.size());
            lines.reserve(cit->second.tokens.size());
            for (const auto &t : cit->second.tokens)
            {
                body.push_back(t.text);
                lines.push_back(t.line);
            }
            d.define_block(name, std::move(body));

            const std::string parent = ioc::to_utf8(target.parent_path());
            st->block_dir[name] = parent.empty() ? canon_path(fs::current_path()) : parent;
            st->block_file[name] = canon;
            st->block_lines[name] = std::move(lines);

            d.push_block(name);
        }
    } // namespace detail

    // ======================================================================== 安装
    inline bool apply_limits(keel::KeelData &d, const Limits &l)
    {
        d.max_steps = l.steps;
        d.max_depth = l.depth;
        d.max_stack = l.stack;
        d.max_adjust = l.max_adjust;
        if (l.alloc != 0)
            d.max_alloc = l.alloc;
        return true;
    }

    /// 装一份 Program：只定义入口块，其余交给运行时；然后清状态、设限额、装 include、
    /// 跑 prelude、init。失败时 error_text 说明原因。
    inline Prepared install(keel::Keel &a, const Program &p,
                            const Environment &env = Environment())
    {
        Prepared r;
        keel::clear_errors();

        if (!p.ok())
        {
            r.error_text = "脚本有语法/结构错误";
            return r;
        }
        const Block *entry = p.entry_block();
        if (!entry)
        {
            r.error_text = "没有入口块（空的 Program？）";
            return r;
        }

        // 先注册 include，再 define_block：compile_block 是当场查 funcs 的，
        // 晚一步注册，入口块里的 `include` 会被编成字面量（变成往栈上压 "include"）。
        // 表面语法还要一个 s.add（智能 +：有字符串就拼接，否则相加）。
        if (p.syntax == Syntax::Surface)
            surface::register_runtime(a);

        if (env.enable_include)
        {
            detail::IncludeState &st = IncludeStates::get(a.data);
            st.env = env;
            st.options = p.options;
            if (!st.registered)
            {
                a.register_func("include", &detail::include_impl);
                st.registered = true;
            }
        }

        a.data.define_block(p.entry, entry->body);
        keelstd::reset_state(a.data);
        apply_limits(a.data, env.limits);

        // 文件 IO 沙箱：规范化一次，IO 指令直接比较
        if (env.sandbox_root.empty())
            a.data.sandbox_root.clear();
        else
            a.data.sandbox_root = u::normalize_path(env.sandbox_root);

        if (env.stdout_sink)
            keelfast::set_writer(a.data, env.stdout_sink);
        else
            keelfast::clear_writer(a.data);

        if (env.enable_include)
        {
            detail::IncludeState &st = IncludeStates::get(a.data);
            st.block_dir[p.entry] = detail::dir_of(p.file);
            if (!p.file.empty() && p.file.front() != '<')
                st.block_file[p.entry] = detail::canon_path(ioc::from_utf8(p.file));
        }

        if (!env.prelude.empty())
        {
            Options po = p.options;
            po.entry = "__prelude";
            Program pre = load_text(env.prelude, "<prelude>", po);
            if (!pre.ok())
            {
                r.error_text = "prelude 有语法/结构错误:\n";
                for (const auto &d : pre.diags)
                    if (d.fatal())
                        r.error_text += "  line " + std::to_string(d.line) + ": " + d.message + "\n";
                return r;
            }
            a.data.define_block("__prelude", pre.entry_block()->body);
            a.init("__prelude");
            a.run();
            if (!keel::keel_errors().empty())
            {
                r.error_text = "prelude 运行出错:\n" + keel::format_errors_for_ai(keel::keel_errors());
                return r;
            }
            // prelude 的"环境"是块和变量；栈上的临时值不该漏进 main
            a.data.stack.clear();
            a.data.Int.clear();
            a.data.strCache.clear();
        }

        a.init(p.entry);
        r.ok = true;
        return r;
    }

    // ---------------------------------------------------------------- 兼容旧写法
    // 遗留重载 install(KeelData&, entry, src)：只切词定义块，不装环境（不设限额、
    // 不装 include、不走表面语法）。新代码请用 load_text + install(Keel&, ...)。
    // 注意：这里用的是 fast_split_tokens，**只支持 `//` 注释**；不认 `#`，
    // 也不走表面语法（let/if/while）。要这些请用 load_text + install(Keel&, ...)。
    inline bool install(keel::KeelData &d, const std::string &entry, const std::string &src)
    {
        const auto toks = keel::fast_split_tokens(src);
        if (toks.empty())
            return false;
        d.define_block(entry, toks);
        return true;
    }

    inline Prepared prepare(keel::Keel &a, const Program &p,
                            const Environment &env = Environment())
    {
        return install(a, p, env);
    }

    /// 旧签名：字面 source + 入口名。按 body 装载。
    inline Prepared prepare(keel::Keel &a, const std::string &entry, std::string src,
                            std::size_t max_steps = 200000)
    {
        Options opt;
        opt.entry = entry;
        Program p = load_text(src, "<text>", opt);
        Environment env;
        env.limits.steps = max_steps;
        return install(a, p, env);
    }

    inline RoundResult execute(keel::Keel &a)
    {
        a.run();
        keelfast::flush_all(a.data); // f.* 有缓冲，跑完必须刷

        RoundResult r;
        r.ok = keel::keel_errors().empty();
        r.steps = a.data.steps;
        r.feedback = keel::format_errors_for_ai(keel::keel_errors());
        return r;
    }

    inline RoundResult round(keel::Keel &a, const Program &p,
                             const Environment &env = Environment())
    {
        Prepared prep = install(a, p, env);
        if (!prep.ok)
        {
            RoundResult r;
            r.feedback = prep.error_text;
            for (const auto &d : p.diags)
                if (d.fatal())
                    r.diag_text += (d.line > 0 ? "line " + std::to_string(d.line) + ": " : "") +
                                   d.message + "\n";
            return r;
        }
        return execute(a);
    }

    inline RoundResult round(keel::Keel &a, const std::string &src,
                             const std::string &entry = "main", std::size_t max_steps = 200000)
    {
        Options opt;
        opt.entry = entry;
        Program p = load_text(src, "<text>", opt);
        Environment env;
        env.limits.steps = max_steps;
        return round(a, p, env);
    }

    /// agent 里习惯写的名字
    inline RoundResult run(keel::Keel &a, const Program &p,
                           const Environment &env = Environment())
    {
        return round(a, p, env);
    }

    // ======================================================================== 诊断 / 源映射
    inline std::string format_diags(const Program &p)
    {
        std::string out;
        for (const auto &d : p.diags)
        {
            out += p.file;
            if (d.line > 0)
                out += ":" + std::to_string(d.line);
            if (d.col > 0)
                out += ":" + std::to_string(d.col);
            out += d.fatal() ? ": error: " : ": warning: ";
            out += d.message + "\n";
        }
        return out;
    }

    /// (块, ip) -> "文件:行"：优先用 Program 的静态视图，其次用 include 记下的行号表。
    inline std::string error_location(const keel::KeelData &d, const Program &p,
                                      const keel::Error &e)
    {
        const int ip = e.token();
        if (ip < 0)
            return e.position_text();

        if (const Block *b = p.find_any(e.where()))
        {
            const int line = b->line_at(static_cast<std::size_t>(ip));
            if (line > 0)
                return p.file + ":" + std::to_string(line);
        }

        if (detail::IncludeState *st = IncludeStates::find(d))
        {
            auto f = st->block_file.find(e.where());
            auto l = st->block_lines.find(e.where());
            if (f != st->block_file.end() && l != st->block_lines.end() &&
                ip < static_cast<int>(l->second.size()))
                return f->second + ":" + std::to_string(l->second[static_cast<std::size_t>(ip)]);
        }
        return e.position_text();
    }

    /// 把本轮错误按 文件:行 回填成给人看的文本（agent 回灌仍用 feedback）。
    inline std::string format_runtime_errors(const keel::Keel &a, const Program &p)
    {
        std::string out;
        for (const auto &e : keel::keel_errors())
            out += error_location(a.data, p, e) + ": " + e.what() + "\n";
        return out;
    }

    // ------------------------------------------------------------------ 栈快照
    inline std::string stack_snapshot(const keel::KeelData &d)
    {
        std::string out = "stack(" + std::to_string(d.stack.size()) + "):";
        for (std::size_t i = 0; i < d.stack.size(); ++i)
            out += "\n  [" + std::to_string(i) + "] " + keel::value_type(d.stack[i].type) +
                   " = " + d.stack[i].toString();
        return out;
    }

} // namespace keelld
