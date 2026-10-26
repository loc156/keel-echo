#pragma once
// ============================================================================
// KeelBase.hpp -- 核心内置函数（只依赖 Keel.hpp，不引入任何标准库头文件）
//
// 约定：
//   * 所有指令的操作数都从 d.stack 顶部取，结果压回 d.stack。
//   * 出错统一走 keel::Error(...).report()（默认打到 stderr，可被
//     set_keel_error_handler 接管），"栈下溢 / 类型不对"不会让引擎崩溃。
//   * 跳转类指令必须 d.jumped = true，否则主循环末尾的 d.ip++ 会吃掉跳转。
//   * 块调用一律用 d.push_block / d.pop_block，别自己动 ip_stack。
//
// 用法：
//   keel::Keel a;
//   keelbase::base_register(a);
//   a.data.codes["main"] = {"1", "2", "+", "dup", "*"};
//   a.init("main");
//   a.run();
//
// 指令表（简写：a b -- c 表示弹两个压一个；这里只列名字，语义与全部别名见
// keel/docs/REFERENCE.md）：
//   [栈]      dup drop swap over rot pick depth clear
//   [算术]    + - * / % neg abs inc dec min max clamp
//   [比较]    == != < > <= >=
//             （== 数字比数值、字符串比文本、列表/映射/umap 按结构比；
//               < > <= >= 数字优先，否则按文本）
//   [逻辑]    and or not xor
//   [转换]    str num int bool type len isnil
//   [字符串]  concat
//   [变量]    store load delvar hasvar vars      （值 名 store / 名 load）
//   [块]      { } call ret lit
//   [控制]    if else then  /  N [ ... ]  /  begin ... until  /  begin ... again
//             break jump jz jnz
//   [限额]    limit getlimit                      （脚本调整软上限，不能越过硬天花板）
//   [错误]    errcount lasterr
//   [杂项]    nop halt assert fail
// ============================================================================
#include "Keel.hpp"
#include "KeelInstanceState.hpp"
#include "KeelUtil.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace keelbase
{
    namespace u = keel::util;

    // ==================================================================== 内部
    namespace detail
    {
        struct LimitRef
        {
            size_t *soft = nullptr;
            size_t hard = 0;
        };

        inline LimitRef find_limit(keel::KeelData &d, const std::string &name)
        {
            if (name == "steps") return {&d.max_steps, d.hard_steps};
            if (name == "depth") return {&d.max_depth, d.hard_depth};
            if (name == "stack") return {&d.max_stack, d.hard_stack};
            if (name == "alloc") return {&d.max_alloc, d.hard_alloc};
            return {};
        }
        // 一个循环的开合记录。begin 类循环 count = -1；[ ] 类循环 count = 剩余次数。
        struct LoopRec
        {
            int start = 0;     // 循环体首指令下标
            int end = 0;       // 终止指令下标（']' / 'until' / 'again'）
            int count = 0;     // 剩余次数
            std::string block; // 所属代码块，防止跨越 push_block 乱跳
        };

        // 每个 KeelData 一份私有状态（变量表 + 循环栈）。
        // 用 keel::StateTable 管理生命周期：多个库各自 get() 时会把清理钩子链起来，
        // 谁也不覆盖谁（若直接给 on_destroy 赋值，注册顺序靠后的库会顶掉前面的清理）。
        struct State
        {
            std::unordered_map<std::string, keel::Value> vars;
            std::vector<LoopRec> loops;
        };

        using States = keel::StateTable<State>;

        inline State &state(const keel::KeelData &d)
        {
            return States::get(const_cast<keel::KeelData &>(d));
        }

        // ---------------------------------------------------------- 流程扫描
        inline int block_size(const keel::KeelData &d)
        {
            return d.current_block ? static_cast<int>(d.current_block->size()) : 0;
        }

        // 从 from 开始找同层的 'then'（'if' 加深度，'then' 减深度）
        inline int find_then(const keel::KeelData &d, int from)
        {
            const int n = block_size(d);
            int depth = 0;
            for (int i = from + 1; i < n; ++i)
            {
                const std::string &t = (*d.current_block)[static_cast<size_t>(i)];
                if (t == "if")
                    ++depth;
                else if (t == "then")
                {
                    if (depth == 0)
                        return i;
                    --depth;
                }
            }
            return -1;
        }

        // 从 if 的 ip 开始找同层的 'else'，没有则 -1
        inline int find_else(const keel::KeelData &d, int from)
        {
            const int n = block_size(d);
            int depth = 0;
            for (int i = from + 1; i < n; ++i)
            {
                const std::string &t = (*d.current_block)[static_cast<size_t>(i)];
                if (t == "if")
                    ++depth;
                else if (t == "then")
                {
                    if (depth == 0)
                        return -1;
                    --depth;
                }
                else if (t == "else" && depth == 0)
                    return i;
            }
            return -1;
        }

        // 从 '[' 开始找同层的 ']'
        inline int find_loop_end(const keel::KeelData &d, int from)
        {
            const int n = block_size(d);
            int depth = 0;
            for (int i = from + 1; i < n; ++i)
            {
                const std::string &t = (*d.current_block)[static_cast<size_t>(i)];
                if (t == "[")
                    ++depth;
                else if (t == "]")
                {
                    if (depth == 0)
                        return i;
                    --depth;
                }
            }
            return -1;
        }

        // 从 'begin' 开始找同层的 'until' / 'again'
        inline int find_begin_end(const keel::KeelData &d, int from)
        {
            const int n = block_size(d);
            int depth = 0;
            for (int i = from + 1; i < n; ++i)
            {
                const std::string &t = (*d.current_block)[static_cast<size_t>(i)];
                if (t == "begin")
                    ++depth;
                else if (t == "until" || t == "again")
                {
                    if (depth == 0)
                        return i;
                    --depth;
                }
            }
            return -1;
        }

        inline void jump_to(keel::KeelData &d, int target)
        {
            d.ip = static_cast<size_t>(target < 0 ? 0 : target);
            d.jumped = true; // 关键：防止主循环 ip++ 覆盖
        }

        // ---------------------------------------------------------- 比较
        inline int cmp_val(const keel::Value &a, const keel::Value &b)
        {
            double x = 0, y = 0;
            if (u::num(a, x) && u::num(b, y))
                return x < y ? -1 : (x > y ? 1 : 0);
            const std::string sa = a.toString(), sb = b.toString();
            const int c = sa.compare(sb);
            return c < 0 ? -1 : (c > 0 ? 1 : 0);
        }

        inline bool eq_val(const keel::Value &a, const keel::Value &b)
        {
            if (a.isNil() || b.isNil())
                return a.isNil() && b.isNil();
            if (a.isList() || b.isList())
                return a.isList() && b.isList() && a.list == b.list;
            if (a.isMap() || b.isMap())
                return a.isMap() && b.isMap() && a.map == b.map;
            if (a.isUMap() || b.isUMap())
                return a.isUMap() && b.isUMap() && a.unmap == b.unmap;
            if (a.isString() && b.isString())
                return a.s == b.s;
            double x = 0, y = 0;
            if (u::num(a, x) && u::num(b, y))
                return x == y;
            return a.toString() == b.toString();
        }

        // ---------------------------------------------------------- 数值二元指令
        template <typename F>
        inline void bin_num(keel::KeelData &d, const char *name, F f)
        {
            if (!u::need(d, 2, name))
                return;
            keel::Value b = u::pop(d);
            keel::Value a = u::pop(d);
            double x = 0, y = 0;
            if (!u::num(a, x) || !u::num(b, y))
            {
                u::err(name, std::string("operands must be numeric, got ") +
                                 keel::value_type(a.type) + " and " + keel::value_type(b.type));
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(f(x, y)));
        }

        template <typename F>
        inline void bin_cmp(keel::KeelData &d, const char *name, F f)
        {
            if (!u::need(d, 2, name))
                return;
            keel::Value b = u::pop(d);
            keel::Value a = u::pop(d);
            u::push(d, keel::make_bool(f(a, b)));
        }

        inline void alias(keel::Keel &a, const char *name, const char *of)
        {
            auto it = a.data.funcs.find(of);
            if (it != a.data.funcs.end())
                a.data.funcs.insert_or_assign(name, it->second);
        }
    } // namespace detail

    // ==================================================================== 注册
    inline void base_register(keel::Keel &a)
    {
        using namespace detail;

        // 状态生命周期交给 keel::StateTable（第一次 state() 时自动链上清理钩子），
        // 这里不再碰 a.data.on_destroy，避免覆盖其它库已经挂上的钩子。

        // ------------------------------------------------------------ 栈操作
        a.register_func("dup", KeelF {
            if (!u::need(d, 1, "dup"))
                return;
            keel::Value tmp = d.stack.back(); // 先拷再 push，避免 push_back 期间引用失效
            u::push(d, std::move(tmp));
        });

        a.register_func("drop", KeelF {
            if (!u::need(d, 1, "drop"))
                return;
            u::pop(d);
        });

        a.register_func("swap", KeelF {
            if (!u::need(d, 2, "swap"))
                return;
            std::swap(d.stack[d.stack.size() - 1], d.stack[d.stack.size() - 2]);
        });

        a.register_func("over", KeelF {
            if (!u::need(d, 2, "over"))
                return;
            u::push(d, d.stack[d.stack.size() - 2]);
        });

        a.register_func("rot", KeelF {
            // a b c -- b c a
            if (!u::need(d, 3, "rot"))
                return;
            const size_t n = d.stack.size();
            keel::Value c = std::move(d.stack[n - 1]);
            d.stack[n - 1] = std::move(d.stack[n - 2]);
            d.stack[n - 2] = std::move(d.stack[n - 3]);
            d.stack[n - 3] = std::move(c);
        });

        a.register_func("pick", KeelF {
            // n pick：0 相当于 dup，1 相当于 over
            double n = 0;
            if (!u::pop_num(d, "pick", n))
                return;
            const int k = u::to_int(n);
            if (k < 0 || static_cast<size_t>(k) >= d.stack.size())
            {
                u::err("pick", "index out of range");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, d.stack[d.stack.size() - 1 - static_cast<size_t>(k)]);
        });

        a.register_func("depth", KeelF {
            u::push(d, keel::make_number(static_cast<double>(d.stack.size())));
        });

        a.register_func("clear", KeelF {
            d.stack.clear();
        });

        // -------------------------------------------------------------- 算术
        a.register_func("+", KeelF {
            // 两边都是数字 -> 加；任一边是字符串 -> 拼接
            if (!u::need(d, 2, "+"))
                return;
            keel::Value b = u::pop(d);
            keel::Value a = u::pop(d);
            double x = 0, y = 0;
            if (u::num(a, x) && u::num(b, y))
            {
                u::push(d, keel::make_number(x + y));
                return;
            }
            if (a.isString() || b.isString() || a.isNil() || b.isNil())
            {
                u::push(d, keel::make_string(a.toString() + b.toString()));
                return;
            }
            u::err("+", std::string("cannot add ") + keel::value_type(a.type) +
                            " and " + keel::value_type(b.type));
            u::push(d, keel::make_nil());
        });
        a.register_func("concat", KeelF {
            // a b concat：两个操作数 toString 后拼接。永远成功，无错误路径。
            // 与 "+" 的区别：两边都是数字时 "+" 做加法，concat 永远做字符串拼接。
            if (!u::need(d, 2, "concat"))
                return;
            keel::Value b = u::pop(d);
            keel::Value a = u::pop(d);
            u::push(d, keel::make_string(a.toString() + b.toString()));
        });

        a.register_func("-", KeelF { bin_num(d, "-", [](double x, double y) { return x - y; }); });
        a.register_func("*", KeelF { bin_num(d, "*", [](double x, double y) { return x * y; }); });

        a.register_func("/", KeelF {
            if (!u::need(d, 2, "/"))
                return;
            keel::Value b = u::pop(d), av = u::pop(d);
            double x = 0, y = 0;
            if (!u::num(av, x) || !u::num(b, y))
            {
                u::err("/", "operands must be numeric");
                u::push(d, keel::make_nil());
                return;
            }
            if (y == 0)
            {
                u::err("/", "division by zero");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(x / y));
        });

        a.register_func("%", KeelF {
            // 余数：符号跟被除数（C 语言 fmod 语义），不引 <cmath>
            if (!u::need(d, 2, "%"))
                return;
            keel::Value b = u::pop(d), av = u::pop(d);
            double x = 0, y = 0;
            if (!u::num(av, x) || !u::num(b, y))
            {
                u::err("%", "operands must be numeric");
                u::push(d, keel::make_nil());
                return;
            }
            if (y == 0)
            {
                u::err("%", "modulo by zero");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(x - u::trunc_d(x / y) * y));
        });

        a.register_func("neg", KeelF {
            double x = 0;
            if (!u::pop_num(d, "neg", x))
                return;
            u::push(d, keel::make_number(-x));
        });

        a.register_func("abs", KeelF {
            double x = 0;
            if (!u::pop_num(d, "abs", x))
                return;
            u::push(d, keel::make_number(x < 0 ? -x : x));
        });

        a.register_func("inc", KeelF {
            double x = 0;
            if (!u::pop_num(d, "inc", x))
                return;
            u::push(d, keel::make_number(x + 1));
        });

        a.register_func("dec", KeelF {
            double x = 0;
            if (!u::pop_num(d, "dec", x))
                return;
            u::push(d, keel::make_number(x - 1));
        });

        a.register_func("min", KeelF { bin_num(d, "min", [](double x, double y) { return x < y ? x : y; }); });
        a.register_func("max", KeelF { bin_num(d, "max", [](double x, double y) { return x > y ? x : y; }); });

        a.register_func("clamp", KeelF {
            // v lo hi -- v'
            if (!u::need(d, 3, "clamp"))
                return;
            keel::Value hv = u::pop(d), lv = u::pop(d), vv = u::pop(d);
            double v = 0, lo = 0, hi = 0;
            if (!u::num(vv, v) || !u::num(lv, lo) || !u::num(hv, hi))
            {
                u::err("clamp", "operands must be numeric");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(v < lo ? lo : (v > hi ? hi : v)));
        });

        // -------------------------------------------------------------- 比较
        a.register_func("==", KeelF { bin_cmp(d, "==", [](const keel::Value &x, const keel::Value &y) { return eq_val(x, y); }); });
        a.register_func("!=", KeelF { bin_cmp(d, "!=", [](const keel::Value &x, const keel::Value &y) { return !eq_val(x, y); }); });
        a.register_func("<", KeelF { bin_cmp(d, "<", [](const keel::Value &x, const keel::Value &y) { return cmp_val(x, y) < 0; }); });
        a.register_func(">", KeelF { bin_cmp(d, ">", [](const keel::Value &x, const keel::Value &y) { return cmp_val(x, y) > 0; }); });
        a.register_func("<=", KeelF { bin_cmp(d, "<=", [](const keel::Value &x, const keel::Value &y) { return cmp_val(x, y) <= 0; }); });
        a.register_func(">=", KeelF { bin_cmp(d, ">=", [](const keel::Value &x, const keel::Value &y) { return cmp_val(x, y) >= 0; }); });

        // -------------------------------------------------------------- 逻辑
        a.register_func("and", KeelF {
            if (!u::need(d, 2, "and"))
                return;
            const bool b = u::pop(d).truthy();
            const bool av = u::pop(d).truthy();
            u::push(d, keel::make_bool(av && b));
        });

        a.register_func("or", KeelF {
            if (!u::need(d, 2, "or"))
                return;
            const bool b = u::pop(d).truthy();
            const bool av = u::pop(d).truthy();
            u::push(d, keel::make_bool(av || b));
        });

        a.register_func("xor", KeelF {
            if (!u::need(d, 2, "xor"))
                return;
            const bool b = u::pop(d).truthy();
            const bool av = u::pop(d).truthy();
            u::push(d, keel::make_bool(av != b));
        });

        a.register_func("not", KeelF {
            if (!u::need(d, 1, "not"))
                return;
            u::push(d, keel::make_bool(!u::pop(d).truthy()));
        });

        // -------------------------------------------------------------- 转换
        a.register_func("str", KeelF {
            if (!u::need(d, 1, "str"))
                return;
            keel::Value v = u::pop(d);
            u::push(d, keel::make_string(v.toString()));
        });

        a.register_func("num", KeelF {
            if (!u::need(d, 1, "num"))
                return;
            keel::Value v = u::pop(d);
            double x = 0;
            if (u::num(v, x))
                u::push(d, keel::make_number(x));
            else
            {
                u::err("num", std::string("cannot convert ") + keel::value_type(v.type) +
                                  " '" + v.toString() + "' to number");
                u::push(d, keel::make_nil());
            }
        });

        a.register_func("int", KeelF {
            double x = 0;
            if (!u::pop_num(d, "int", x))
                return;
            u::push(d, keel::make_number(u::trunc_d(x)));
        });

        a.register_func("bool", KeelF {
            if (!u::need(d, 1, "bool"))
                return;
            u::push(d, keel::make_bool(u::pop(d).truthy()));
        });

        a.register_func("type", KeelF {
            if (!u::need(d, 1, "type"))
                return;
            keel::Value v = u::pop(d);
            u::push(d, keel::make_string(keel::value_type(v.type)));
        });

        a.register_func("len", KeelF {
            if (!u::need(d, 1, "len"))
                return;
            keel::Value v = u::pop(d);
            u::push(d, keel::make_number(static_cast<double>(v.size())));
        });

        a.register_func("isnil", KeelF {
            if (!u::need(d, 1, "isnil"))
                return;
            u::push(d, keel::make_bool(u::pop(d).isNil()));
        });

        // -------------------------------------------------------------- 变量
        // 变量表按 KeelData 实例存放：值 名 store，名 load
        a.register_func("store", KeelF {
            if (!u::need(d, 2, "store"))
                return;
            std::string name;
            if (!u::peek_str(d, 0, "store", name))
            {
                u::drop_n(d, 2);   // 失败也按"值 名"两个操作数消耗，跟成功时一致
                return;
            }
            keel::Value v = u::peek(d, 1);
            u::drop_n(d, 2);
            state(d).vars.insert_or_assign(name, std::move(v));
        });

        a.register_func("load", KeelF {
            std::string name;
            if (!u::pop_str(d, "load", name))
                return;
            auto &vars = state(d).vars;
            auto it = vars.find(name);
            if (it == vars.end())
            {
                u::err("load", "undefined variable '" + name + "'");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, it->second);
        });

        a.register_func("delvar", KeelF {
            if (!u::need(d, 1, "delvar"))
                return;
            std::string name;
            if (!u::peek_str(d, 0, "delvar", name))
            {
                u::drop_n(d, 1);
                return;
            }
            u::drop_n(d, 1);
            state(d).vars.erase(name);
        });

        a.register_func("hasvar", KeelF {
            std::string name;
            if (!u::pop_str(d, "hasvar", name))
                return;
            u::push(d, keel::make_bool(state(d).vars.count(name) != 0));
        });

        a.register_func("vars", KeelF {
            std::vector<std::string> names;
            names.reserve(state(d).vars.size());
            for (const auto &kv : state(d).vars)
                names.push_back(kv.first);
            std::sort(names.begin(), names.end());
            keel::Value list = keel::make_list();
            for (auto &n : names)
                list.push(keel::make_string(std::move(n)));
            u::push(d, std::move(list));
        });

        // ------------------------------------------------------- 代码块 / 调用
        // '{' 直接扫描配对，把 [ip+1, '}') 之间的 token 原样拷进 codes[name]，
        // 然后把 ip 挪到 '}' 之后。原版靠 d.stream 计数，遇到块内字面量
        // （如 { addten 10 + }）会数错、把 '}' 也吞掉，这里改掉。
        a.register_func("{", KeelF {
            if (!d.current_block)
            {
                u::err("{", "no active block");
                d.end = true;
                return;
            }
            if (!u::need(d, 1, "{"))
            {
                d.end = true;
                return;
            }
            keel::Value nv = u::pop(d);
            if (!nv.isString() || nv.s.empty())
            {
                u::err("{", "block name must be a non-empty string");
                d.end = true;
                return;
            }
            const std::string name = std::move(nv.s);
            // 块还在调用栈上时重定义，栈上那份 ip 会指向新内容，错位。
            // 同时覆盖直接自引用（main 里定义 main）和间接引用（main -> a -> b，b 里定义 main）。
            if (std::find(d.cache_code_ptr.begin(), d.cache_code_ptr.end(), name) != d.cache_code_ptr.end())
            {
                u::err("{", "cannot redefine block '" + name + "' while it is on the call stack");
                d.end = true;
                return;
            }

            const keel::strings &src = *d.current_block;
            const int from = static_cast<int>(d.ip);
            int depth = 1, match = -1;
            for (int i = from + 1; i < static_cast<int>(src.size()); ++i)
            {
                if (src[static_cast<size_t>(i)] == "{")
                    ++depth;
                else if (src[static_cast<size_t>(i)] == "}")
                {
                    if (--depth == 0)
                    {
                        match = i;
                        break;
                    }
                }
            }
            if (match < 0)
            {
                u::err("{", "missing paired '}' for block '" + name + "'");
                d.end = true;
                return;
            }

            d.define_block(name, keel::strings(src.begin() + from + 1, src.begin() + match));
            jump_to(d, match + 1); // 跳过整个 { ... }
        });

        a.register_func("}", KeelF {
            u::err("}", "stray '}' without matching '{'");
            d.end = true;
        });

        // 名字 call：把下一个块压栈执行，ret 返回
        a.register_func("call", KeelF {
            if (!u::need(d, 1, "call"))
                return;
            std::string name;
            if (!u::peek_str(d, 0, "call", name))
            {
                u::drop_n(d, 1);
                return;
            }
            u::drop_n(d, 1);
            if (d.codes.find(name) == d.codes.end())
            {
                u::err("call", "unknown block '" + name + "'");
                return;
            }
            d.push_block(name);
        });

        a.register_func("ret", KeelF {
            d.pop_block();
        });

        // 强制把下一个 token 当数据（哪怕它是指令名）
        a.register_func("lit", KeelF {
            if (!d.current_block || d.ip + 1 >= d.current_block->size())
            {
                u::err("lit", "missing literal after 'lit'");
                return;
            }
            u::push(d, keel::parse_value((*d.current_block)[d.ip + 1]));
            d.ip += 1; // 主循环再 +1，正好跳过这个 token
        });

        // -------------------------------------------------------------- 控制流
        // if 条件 else 分支 then
        a.register_func("if", KeelF {
            if (!u::need(d, 1, "if"))
                return;
            const bool c = u::peek(d, 0).truthy();
            u::drop_n(d, 1);
            if (c)
                return; // 顺延执行 then 分支体
            const int else_at = find_else(d, static_cast<int>(d.ip));
            const int then_at = find_then(d, static_cast<int>(d.ip));
            if (then_at < 0 && else_at < 0)
            {
                u::err("if", "missing 'then'");
                d.end = true;
                return;
            }
            jump_to(d, else_at >= 0 ? else_at + 1 : then_at); // 进 else 分支体
        });

        a.register_func("else", KeelF {
            // 真分支执行完了，跳到配对 then
            const int then_at = find_then(d, static_cast<int>(d.ip));
            if (then_at < 0)
            {
                u::err("else", "missing 'then'");
                d.end = true;
                return;
            }
            jump_to(d, then_at);
        });

        a.register_func("then", KeelF {
            (void)d;
            // 空标记
        });

        // N [ 循环体 ] -- 计数循环，支持嵌套
        a.register_func("[", KeelF {
            if (!u::need(d, 1, "["))
                return;
            double n = 0;
            if (!u::peek_num(d, 0, "[", n))
            {
                u::drop_n(d, 1);
                return;
            }
            u::drop_n(d, 1);
            const int from = static_cast<int>(d.ip);
            const int end = find_loop_end(d, from);
            if (end < 0)
            {
                u::err("[", "missing paired ']'");
                d.end = true;
                return;
            }
            const int times = u::to_int(n);
            if (times <= 0)
            {
                jump_to(d, end + 1); // 0 次直接跳过整段
                return;
            }
            state(d).loops.push_back(LoopRec{from + 1, end, times, u::cur_block(d)});
        });

        a.register_func("]", KeelF {
            auto &loops = state(d).loops;
            if (loops.empty() || loops.back().count < 0)
            {
                u::err("]", "']' without matching '['");
                return;
            }
            LoopRec rec = loops.back();
            if (rec.block != u::cur_block(d))
            {
                u::err("]", "']' belongs to block '" + rec.block + "', not '" + u::cur_block(d) + "'");
                loops.pop_back();
                return;
            }
            if (rec.count > 1)
            {
                rec.count -= 1;
                loops.back() = rec;
                jump_to(d, rec.start);
            }
            else
            {
                loops.pop_back();
            }
        });

        // begin ... until  (do-while：until 弹出条件，假则继续)
        // begin ... again  (无限循环，靠 break 退出)
        a.register_func("begin", KeelF {
            const int from = static_cast<int>(d.ip);
            const int end = find_begin_end(d, from);
            if (end < 0)
            {
                u::err("begin", "missing 'until' or 'again'");
                d.end = true;
                return;
            }
            state(d).loops.push_back(LoopRec{from + 1, end, -1, u::cur_block(d)});
        });

        a.register_func("until", KeelF {
            if (!u::need(d, 1, "until"))
                return;
            const bool c = u::peek(d, 0).truthy();
            u::drop_n(d, 1);
            auto &loops = state(d).loops;
            if (loops.empty() || loops.back().count != -1)
            {
                u::err("until", "'until' without matching 'begin'");
                return;
            }
            const LoopRec rec = loops.back();
            if (rec.block != u::cur_block(d))
            {
                u::err("until", "'until' belongs to block '" + rec.block + "'");
                loops.pop_back();
                return;
            }
            if (c)
                loops.pop_back(); // 条件成立，退出循环
            else
                jump_to(d, rec.start); // 继续下一轮
        });

        a.register_func("again", KeelF {
            auto &loops = state(d).loops;
            if (loops.empty() || loops.back().count != -1)
            {
                u::err("again", "'again' without matching 'begin'");
                return;
            }
            const LoopRec rec = loops.back();
            if (rec.block != u::cur_block(d))
            {
                u::err("again", "'again' belongs to block '" + rec.block + "'");
                loops.pop_back();
                return;
            }
            jump_to(d, rec.start);
        });

        a.register_func("break", KeelF {
            auto &loops = state(d).loops;
            if (loops.empty())
            {
                u::err("break", "no active loop");
                return;
            }
            const LoopRec rec = loops.back();
            if (rec.block != u::cur_block(d))
            {
                // 不 pop：这条循环记录属于调用者的块，保留它让调用者的循环继续
                u::err("break", "no active loop in block '" + u::cur_block(d) + "'");
                return;
            }
            loops.pop_back();
            jump_to(d, rec.end + 1); // 跳过终止指令
        });

        // 裸跳转：目标是从 0 开始的指令下标
        a.register_func("jump", KeelF {
            if (!u::need(d, 1, "jump"))
                return;
            double t = 0;
            if (!u::peek_num(d, 0, "jump", t))
            {
                u::drop_n(d, 1);
                return;
            }
            u::drop_n(d, 1);
            const int target = u::to_int(t);
            if (!d.current_block || target < 0 || target >= static_cast<int>(d.current_block->size()))
            {
                u::err("jump", "target " + std::to_string(target) + " out of range");
                return;
            }
            jump_to(d, target);
        });

        a.register_func("jz", KeelF {
            // 条件 目标 jz：条件为假时跳
            if (!u::need(d, 2, "jz"))
                return;
            double t = 0;
            if (!u::peek_num(d, 0, "jz", t))
            {
                u::drop_n(d, 2);
                return;
            }
            const bool c = u::peek(d, 1).truthy();
            u::drop_n(d, 2);
            if (!c)
                jump_to(d, u::to_int(t));
        });

        a.register_func("jnz", KeelF {
            if (!u::need(d, 2, "jnz"))
                return;
            double t = 0;
            if (!u::peek_num(d, 0, "jnz", t))
            {
                u::drop_n(d, 2);
                return;
            }
            const bool c = u::peek(d, 1).truthy();
            u::drop_n(d, 2);
            if (c)
                jump_to(d, u::to_int(t));
        });

        // ---------------------------------------------------------------- 杂项
        a.register_func("nop", KeelF { (void)d; });

        a.register_func("halt", KeelF { d.end = true; });

        a.register_func("assert", KeelF {
            // 条件 "消息" assert
            if (!u::need(d, 2, "assert"))
                return;
            const std::string msg = u::peek(d, 0).toString();
            const bool c = u::peek(d, 1).truthy();
            u::drop_n(d, 2);
            if (!c)
            {
                u::err("assert", msg);
                d.end = true;
            }
        });

        a.register_func("fail", KeelF {
            if (!u::need(d, 1, "fail"))
                return;
            const std::string msg = u::peek(d, 0).toString();
            u::drop_n(d, 1);
            u::err("fail", msg);
            d.end = true;
        });
        // 值 名 limit：把软上限设成值，clamp 到硬天花板。
        // 被 clamp 时压回 false，否则 true。调整次数用完则拒绝。
        a.register_func("limit", KeelF {
            if (!u::need(d, 2, "limit"))
                return;
            std::string name;
            if (!u::peek_str(d, 0, "limit", name))
            {
                u::drop_n(d, 2);
                u::push(d, keel::make_bool(false));
                return;
            }
            double v = 0;
            if (!u::peek_num(d, 1, "limit", v))
            {
                u::drop_n(d, 2);
                u::push(d, keel::make_bool(false));
                return;
            }
            u::drop_n(d, 2);

            auto ref = find_limit(d, name);
            if (!ref.soft)
            {
                u::err("limit", "unknown limit '" + name + "' (steps/depth/stack/alloc)");
                u::push(d, keel::make_bool(false));
                return;
            }
            if (d.adjust_used >= d.max_adjust)
            {
                u::err("limit", "adjust budget exhausted (" + std::to_string(d.max_adjust) + ")");
                u::push(d, keel::make_bool(false));
                return;
            }
            ++d.adjust_used;

            const size_t want = (v <= 0) ? 0 : static_cast<size_t>(v);
            const size_t capped = (want < ref.hard) ? want : ref.hard;
            *ref.soft = capped;
            u::push(d, keel::make_bool(capped == want));
        });

        // 名 getlimit：查当前软上限
        a.register_func("getlimit", KeelF {
            std::string name;
            if (!u::pop_str(d, "getlimit", name))
                return;
            auto ref = find_limit(d, name);
            if (!ref.soft)
            {
                u::err("getlimit", "unknown limit '" + name + "'");
                u::push(d, keel::make_nil());
                return;
            }
            u::push(d, keel::make_number(static_cast<double>(*ref.soft)));
        });
        // ---------------------------------------------------------------- 错误查询
        // errcount：当前错误容器里的条数（受 kMaxErrors=100 截断）。
        //           要"本轮错误"，宿主应在每轮脚本前调 keel::clear_errors()。
        a.register_func("errcount", KeelF {
            u::push(d, keel::make_number(static_cast<double>(keel::keel_errors().size())));
        });

        // lasterr：最近一条错误的 what()（形如 "add: operands must be numeric..."）；
        //          没有错误时压 ""。要带位置请用宿主侧的 format_errors_for_ai。
        a.register_func("lasterr", KeelF {
            const auto &errs = keel::keel_errors();
            if (errs.empty())
            {
                u::push(d, keel::make_string(std::string()));
                return;
            }
            u::push(d, keel::make_string(errs.back().what()));
        });
        // ---------------------------------------------------------------- 别名
        // 手写脚本时常见写法，省得记符号
        alias(a, "add", "+");
        alias(a, "sub", "-");
        alias(a, "mul", "*");
        alias(a, "div", "/");
        alias(a, "mod", "%");
        alias(a, "eq", "==");
        alias(a, "ne", "!=");
        alias(a, "lt", "<");
        alias(a, "gt", ">");
        alias(a, "le", "<=");
        alias(a, "ge", ">=");
        alias(a, "&&", "and");
        alias(a, "||", "or");
        alias(a, "!", "not");
        alias(a, "tostr", "str");
        alias(a, "tostring", "str");
        alias(a, "tonum", "num");
        alias(a, "toint", "int");
        alias(a, "tobool", "bool");
        alias(a, "size", "len");
        alias(a, "length", "len");
        // 不再提供 let / recall / return / do / quote：这些名字会误导语义
        // （let 暗示块作用域、return 暗示返回值、do 像循环关键字）。
        // 表面语法有自己的 let/return，和这里的核心别名无关。
    }

    // 清掉某个实例的变量表和循环栈（同一 KeelData 复用跑新程序时调用）。
    // 注意是清空内容、保留 StateTable 条目：条目的析构钩子已经挂在 on_destroy 链上，
    // 直接 erase 会让链上的 destroy 找不到自己、也不再调用 prev。
    inline void reset_state(const keel::KeelData &d)
    {
        if (auto *s = detail::States::find(d))
        {
            s->vars.clear();
            s->loops.clear();
        }
    }
}
