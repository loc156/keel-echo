#pragma once
// ============================================================================
//  Keel.hpp —— VM 主体：数据栈、指令分派、块调用
// ----------------------------------------------------------------------------
//  对外只有两个东西：
//    * keel::KeelData —— 一个实例的全部运行时状态：数据栈、变量表、块表、
//                        编译后的指令缓存、执行位置、限额、错误位置、沙箱根。
//    * keel::Keel     —— 持有 KeelData 的句柄，只提供 init/step/run/register_func。
//
//  一段脚本最朴素的跑法：
//
//      keel::Keel vm;
//      keelstd::register_all(vm);              // 装内置指令（KeelStd.hpp）
//      vm.data.define_block("main", tokens);   // 通常由装载器代劳
//      vm.init("main");
//      vm.run();                               // 或循环 step() 做单步/慢放
//
//  和装载器的分工：本文件只管"跑"。把文件变成 token、变成 Program、装环境
//  （限额、include、沙箱、stdout 汇、prelude）是 KeelLoader.hpp 的事，见
//  keel/docs/LOADER.md。
//
//  `{` 是运行时定义块：它从栈顶取块名，把 `}` 之前的 token 存成块 —— 所以块名
//  可以是算出来的，装载器不可能成为块表的权威。详见 keel/docs/LANGUAGE.md。
//
//  依赖：只依赖 KeelValue.hpp。
// ============================================================================
#include "KeelValue.hpp"

#include <charconv>
#include <string_view>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace keel
{
    // 极速词法切分：支持按空白符切分，且自动保护引号内的空格
    inline std::vector<std::string> fast_split_tokens(std::string_view sv)
    {
        std::vector<std::string> tokens;
        tokens.reserve(sv.size() / 3 + 1);

        size_t start = 0;
        bool in_quotes = false;
        char quote_char = 0;
        bool in_line_comment = false;

        for (size_t i = 0; i < sv.size(); ++i)
        {
            char c = sv[i];

            if (in_line_comment)
            {
                if (c == '\n')
                {
                    in_line_comment = false;
                    start = i + 1;
                }
                continue;
            }

            if (in_quotes)
            {
                if (c == quote_char)
                    in_quotes = false;
            }
            else
            {
                if (c == '"' || c == '\'')
                {
                    in_quotes = true;
                    quote_char = c;
                }
                else if (c == '/' && i + 1 < sv.size() && sv[i + 1] == '/')
                {
                    // 遇到 // 进注释。先把 [start, i) 之间的 token 收进去。
                    if (i > start)
                        tokens.emplace_back(sv.substr(start, i - start));
                    in_line_comment = true;
                    ++i; // 跳过第二个 '/'
                }
                else if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                {
                    if (i > start)
                        tokens.emplace_back(sv.substr(start, i - start));
                    start = i + 1;
                }
            }
        }

        if (!in_line_comment && start < sv.size())
            tokens.emplace_back(sv.substr(start));

        return tokens;
    }
    using strings = std::vector<std::string>;

    enum class StreamMode : int
    {
        Stack = 0,
        StrCache = 1,
        Catch = 2
    };

    struct KeelData;
    using Func = void (*)(KeelData &d);

    struct CInstruction
    {
        Func func = nullptr;
    };

    struct KeelData
    {
        size_t ip = 0;

        // --- 调用栈与跳转控制 ---
        strings cache_code_ptr;       // 代码块调用栈
        std::vector<size_t> ip_stack; // 对应的返回地址栈
        bool jumped = false;          // 是否发生跳转（防主循环 ip++ 覆盖）

        // --- 源代码与编译缓存 ---
        std::unordered_map<std::string, strings> codes;
        std::unordered_map<std::string, std::vector<CInstruction>> compiled_codes;

        // --- 执行引擎：编译后的指令缓存、块表、执行位置（Keel 只是它的句柄） ---
        std::unordered_map<std::string, Func> funcs;
        void (*strCatch)(std::string) = nullptr; // 扩展点：stream=Catch 时的回调，内置指令不触发

        // 内置库把"每实例私有状态"挂在静态表里时用这个钩子：Keel 析构前调用一次，让库把自己那份状态删掉。
        // 引擎不认识具体库，只穿过函数指针（见 KeelBase的 states()/drop_state）。
        // 不设的话，表只增不删，而且新实例复用同一地址时会继承旧实例的变量表 / 循环栈。
        void (*on_destroy)(KeelData &) = nullptr;

        // --- 运行时数据栈 ---
        std::vector<keel::Value> stack;
        std::vector<int> Int; // 扩展点：当前内置指令不使用
        strings strCache;     // 扩展点：stream=StrCache 时收集 token，内置指令不触发
        std::vector<std::string> pending_block_names; // 扩展点：`{` 的预扫描缓存，内置指令不触发

        // 扩展点：stream > 0 时 `}` 会把 token 收进 strCache / strCatch；
        // 目前没有内置指令会打开它，保留给自定义指令（见 keel/docs/EMBEDDING.md）。
        int stream = 0;
        StreamMode stream_mode = StreamMode::Stack;
        bool end = false;

        // 文件 IO 沙箱：空 = 不限制；非空 = 已规范化的绝对路径。
        // 由 keelld::install 从 Environment::sandbox_root 写入，IO 指令查 util::check_sandbox。
        std::string sandbox_root;

        // --- 资源上限（保险丝） ---
        // 软上限：脚本可通过 `limit` 指令在硬天花板以内调整
        size_t max_steps = 10000000;
        size_t max_depth = 1000;
        size_t max_stack = 100000;
        size_t max_alloc = 16 << 20; // 16MB

        // 硬天花板：宿主设定，脚本不可越
        size_t hard_steps = 100000000;
        size_t hard_depth = 10000;
        size_t hard_stack = 1000000;
        size_t hard_alloc = 256 << 20; // 256MB

        // 调整预算：脚本最多调几次 limit
        int max_adjust = 20;
        int adjust_used = 0;

        // 运行时计数
        size_t steps = 0;

        // --- 内部加速缓存 ---
        std::string current_name;
        const strings *current_block = nullptr;
        const std::vector<CInstruction> *current_compiled = nullptr;

        // 编译指令块（内部用；外部定义/重定义块请走下面的 define_block）
        void compile_block(const std::string &name)
        {
            auto &src = codes[name];
            auto &dest = compiled_codes[name];
            dest.clear();
            dest.reserve(src.size());
            for (const auto &s : src)
            {
                auto it = funcs.find(s);
                dest.push_back(CInstruction{it != funcs.end() ? it->second : nullptr});
            }
        }

        // 定义 / 重定义代码块的唯一入口：写 codes 的同时立刻刷新函数指针缓存。
        // 引擎只在「块长度变化」时自动重编译，所以同长度重定义（比如
        // b { inc } 改成 b { dec }，都只有 1 条）如果不走这里，就会继续
        // 执行 compiled_codes 里旧的函数指针。
        void define_block(const std::string &name, strings body)
        {
            codes[name] = std::move(body);
            compile_block(name);
        }

        // 同步上下文（自动被主循环和跳转调用）         ---不建议使用
        void sync_context()
        {
            if (cache_code_ptr.empty())
            {
                end = true;
                current_name.clear();
                current_block = nullptr;
                current_compiled = nullptr;
                return;
            }
            current_name = cache_code_ptr.back();
            auto it = codes.find(current_name);
            if (it == codes.end())
            {
                end = true;
                current_block = nullptr;
                current_compiled = nullptr;
                return;
            }
            current_block = &it->second;
            current_compiled = &compiled_codes[current_name];
            if (current_block->size() != current_compiled->size())
            {
                compile_block(current_name);
            }
        }

        // 调用新的代码块
        void push_block(const std::string &name)
        {
            if (cache_code_ptr.size() >= max_depth)
            {
                keel::Error("call: block call depth limit exceeded (" + std::to_string(max_depth) + ")", "call").report();
                end = true;
                return;
            }
            // 压入当前块的返回地址
            ip_stack.push_back(ip + 1);
            cache_code_ptr.push_back(name);

            ip = 0;
            jumped = true;
            sync_context();
        }

        // 从当前代码块返回
        void pop_block()
        {
            if (cache_code_ptr.size() > 1)
            {
                cache_code_ptr.pop_back();
                if (!ip_stack.empty())
                {
                    ip = ip_stack.back();
                    ip_stack.pop_back();
                }
                else
                {
                    ip = 0;
                }
            }
            else
            {
                end = true;
                return;
            }
            jumped = true;
            sync_context();
        }
    };

    class Keel
    {
    public:
        KeelData data;
        // 不可拷贝 / 不可移动：KeelData 的内置库状态按 &data 地址登记在静态表里，
        // 拷贝后新实例的状态表是空的（变量表、文件句柄全丢）。
        // 需要动态生命周期请用 unique_ptr<Keel>。
        Keel() = default;
        Keel(const Keel &) = delete;
        Keel &operator=(const Keel &) = delete;

        // 析构前给内置库一次清理机会（否则 KeelBase 的状态表会按地址残留状态）
        ~Keel()
        {
            if (data.on_destroy)
                data.on_destroy(data);
        }

        void init(const std::string &entry_block)
        {
            // 同一个实例二次 init 时必须先丢掉上一次的执行位置和块调用栈，
            // 否则 cache_code_ptr / ip_stack 会累积，ip 还停在上次的半路。
            data.cache_code_ptr.clear();
            data.ip_stack.clear();
            data.ip = 0;
            data.jumped = false;
            data.steps = 0;
            data.adjust_used = 0;
            data.end = false;
            data.cache_code_ptr.push_back(entry_block);
            data.sync_context();
        }

        // 执行一步：一条指令；ip 走到块尾时这一步就是一次块返回（pop_block）。
        // 返回 false 表示 VM 已停（data.end == true）。
        // 调试器逐条调用它来观察栈 / ip 的变化，也可以一帧里连调多次做慢放；
        // run() 就是 while (!data.end) step()。
        bool step()
        {
            if (data.end)
                return false;

            // 保险丝：步数 / 栈深。超限时报一条明确错误再退出，不是静默停。
            if (++data.steps > data.max_steps)
            {
                keel::clear_error_pos();
                keel::Error("run: step limit exceeded (" + std::to_string(data.max_steps) + ")", "run").report();
                data.end = true;
                return false;
            }
            if (data.stack.size() > data.max_stack)
            {
                keel::clear_error_pos();
                keel::Error("run: stack size limit exceeded (" + std::to_string(data.max_stack) + ")", "run").report();
                data.end = true;
                return false;
            }
            if (data.cache_code_ptr.empty())
            {
                data.end = true;
                return false;
            }
            if (data.ip >= data.current_block->size())
            {
                data.pop_block();
                return !data.end;
            }

            // 2. 嗅探动态注入的新代码
            if (data.current_block->size() != data.current_compiled->size())
            {
                data.compile_block(data.current_name);
                data.current_block = &data.codes[data.current_name];
                data.current_compiled = &data.compiled_codes[data.current_name];
            }

            // 3. 取指令
            const CInstruction &instr = (*data.current_compiled)[data.ip];
            const std::string &code = (*data.current_block)[data.ip];

            data.jumped = false; // 每帧开始前清空跳转标志

            // 记下出错位置：内置函数里 u::err 报错时，Error::report() 会取这个
            keel::set_error_pos(&data.current_name, static_cast<int>(data.ip));

            // 4. 执行分发
            //    流捕获优先，并且对「每一个 token」递减计数。
            //    原来这段嵌在 if (instr.func) 里面：遇到字面量（没注册成
            //    指令的 token）既不递减也不捕获，d.stream 会数错，块捕获
            //    会一路把后面的 '}' 也吞进 strCache。
            //    注意：内置指令不会设置 data.stream；这是给自定义指令留的扩展点
            //    （例如 collect N 指令），见 keel/docs/EMBEDDING.md。
            if (data.stream > 0)
            {
                data.stream--;
                switch (data.stream_mode)
                {
                case StreamMode::Stack:
                    data.stack.push_back(code);
                    break;
                case StreamMode::StrCache:
                    data.strCache.push_back(code);
                    break;
                case StreamMode::Catch:
                    if (data.strCatch)
                        data.strCatch(code);
                    break;
                }
            }
            else if (instr.func)
            {
                instr.func(data); // 执行内置函数
            }
            else
            {
                data.stack.push_back(keel::parse_value(code));
            }

            // 5. 只有在没有发生跳转/切换块的情况下，才递增 ip
            if (!data.jumped)
            {
                data.ip++;
            }
            return !data.end;
        }

        void run()
        {
            while (!data.end)
                step();
            // 跑完就清掉位置，免得之后在 VM 外报的错（比如 JSON 解码）被套上旧位置
            keel::clear_error_pos();
        }
        void register_func(std::string name, Func f)
        {
            data.funcs[name] = f;
        }
    };
}

#define KeelF [](keel::KeelData &d) // 定义内置函数最好用这个